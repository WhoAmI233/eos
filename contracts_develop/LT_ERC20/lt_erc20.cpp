#include "lt_erc20.hpp"


void lt_erc20::create( account_name issuer,
                       asset        maximum_supply,
                       uint8_t      currency_type)
{
    require_auth( _self );
    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.name());
   
    auto existing = statstable.find( sym.name() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );
    
    statstable.emplace( _self, [&]( auto& s ) {
    s.supply.symbol = maximum_supply.symbol;
    s.max_supply    = maximum_supply;
    s.issuer        = issuer;
    s.currency_type = currency_type;
    s.create_time   = time_point_sec(now());
    s.update_time   = time_point_sec(now());
    });
}


void lt_erc20::issue( account_name to, asset quantity, string memo, 
                        uint8_t      currency_type, 
                        uint64_t     unlock_time,
                        uint64_t     issue_time, 
                        asset        init_rele_num,
                        uint64_t     cycle_time  , 
                        uint8_t      cycle_counts,
                        uint64_t     cycle_starttime,
                        asset        cycle_rele_num )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    auto sym_name = sym.name();
    stats statstable( _self, sym_name );
    auto existing = statstable.find( sym_name );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    accounts acctable( _self, to );
    auto acc = acctable.find( sym_name );

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    eosio_assert( currency_type == st.currency_type, "currency_type mismatch");
    eosio_assert( unlock_time < cycle_starttime, "The unlocking time is not the same as the start time of the cycle");
    
    if( 1 == currency_type ) //普通代币
    {
        statstable.modify( st, 0, [&]( auto& s ) {
        s.supply += quantity;
        s.update_time = time_point_sec(now());
        });
        add_balance( st.issuer, quantity, st.issuer );

        if( to != st.issuer ) {
        SEND_INLINE_ACTION( *this, transfer, {st.issuer,N(active)}, {st.issuer, to, quantity, memo, currency_type} );
        }
    }
    else if( 2 == currency_type ) //带时间锁
    {
        statstable.modify( st, 0, [&]( auto& s ) {
        s.supply      += quantity;
        s.update_time = time_point_sec(now());
        });

        add_balance( st.issuer, quantity, st.issuer );
        accounts from_acnts( _self, st.issuer );
        const auto& from = from_acnts.get( sym_name, "no balance object found" );
        eosio_assert( from.balance.amount >= quantity.amount, "overdrawn balance" );

        if( from.balance.amount == quantity.amount ) {
            from_acnts.erase( from );
        } else {
            from_acnts.modify( from, st.issuer, [&]( auto& a ) {
                a.balance     -= quantity;
                a.update_time = time_point_sec(now());
            });
        }
        
        if( acc == acctable.end() ) {
            acctable.emplace( st.issuer, [&]( auto& a ){
                a.balance     = init_rele_num;
                a.owner       = to;
                a.update_time = time_point_sec(now());
                a.lock_balance_pairs.push_back(lock_balance_pair {
                    quantity - init_rele_num, 
                    time_point_sec(now()) + unlock_time,
                    time_point_sec(now()) + issue_time,
                    init_rele_num,
                    cycle_time,
                    cycle_counts,
                    time_point_sec(now()) + cycle_starttime,
                    cycle_rele_num                            
                    } );
            });
        } 
        else 
        {
            acctable.modify( acc, 0, [&]( auto& a ) {
                a.balance     += init_rele_num;
                a.update_time = time_point_sec(now());
                a.lock_balance_pairs.push_back(lock_balance_pair {
                    quantity- init_rele_num, 
                    time_point_sec(now()) + unlock_time,
                    time_point_sec(now()) + issue_time,
                    init_rele_num,
                    cycle_time,
                    cycle_counts,
                    time_point_sec(now()) + cycle_starttime,
                    cycle_rele_num                               
                    } );
            });
        }
    }
    else
    {
        eosio_assert( false, "param currency_type:error,please check." );
    }
    
}

void lt_erc20::transfer( account_name from,
                      account_name to,
                      asset        quantity,
                      string       memo,
                      uint8_t currency_type )
{
    eosio_assert( from != to, "cannot transfer to self" );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.name();
    stats statstable( _self, sym );
    const auto& st = statstable.get( sym );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );
    eosio_assert( currency_type == st.currency_type, "currency_type mismatch");

    if( 1 == currency_type ) //普通代币
    {
        sub_balance( from, quantity );
        add_balance( to, quantity, from );
    }
    else if( 2 == currency_type ) //带时间锁
    {
        accounts from_acnts( _self, from );
        const auto& ac = from_acnts.get( quantity.symbol.name(), "no balance object found 2" );
    
        from_acnts.modify( ac, 0, [&]( auto& s ) {
        lock_asset::iterator   iter = s.lock_balance_pairs.begin();
        for(; iter != s.lock_balance_pairs.end(); )//此处进行周期性释放限制
        {
            if( iter->unlock_time <= time_point_sec(now()) )
            {
                for( int64_t  i = iter->cycle_counts; i > 0; i-- )
                {
                    if( iter->lock_balance >=  i*iter->cycle_rele_num && time_point_sec(now()) > (iter->cycle_starttime + i*iter->cycle_time) )
                    {
                        iter->lock_balance = iter->lock_balance -  i*iter->cycle_rele_num;
                        s.balance  +=  i*iter->cycle_rele_num; 
                        break;
                    }
                }
            }
            if( iter->cycle_starttime + iter->cycle_counts*iter->cycle_time < time_point_sec(now()) )
            {
                iter = s.lock_balance_pairs.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
        s.update_time = time_point_sec(now());
        });
        sub_balance( from, quantity );
        add_balance( to, quantity, from );
    }
    else
    {
        eosio_assert( false, "param currency_type:error,please check." );
    }
}

void lt_erc20::sub_balance( account_name owner, asset value ) {
   accounts from_acnts( _self, owner );

   const auto& from = from_acnts.get( value.symbol.name(), "no balance object found 1" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );


   if( from.balance.amount == value.amount && from.lock_balance_pairs.size() == 0 ) {
      from_acnts.erase( from );
   } else {
      from_acnts.modify( from, owner, [&]( auto& a ) {
          a.balance     -= value;
          a.update_time = time_point_sec(now());
      });
   }
}

void lt_erc20::add_balance( account_name owner, asset value, account_name ram_payer )
{
   accounts to_acnts( _self, owner );
   auto to = to_acnts.find( value.symbol.name() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
        a.owner       = owner;
        a.update_time = time_point_sec(now());
      });
   } else {
      to_acnts.modify( to, 0, [&]( auto& a ) {
        a.balance += value;
        a.update_time = time_point_sec(now());
      });
   }
}

void lt_erc20::foo( string foo_name, account_name owner, asset value )
{
    if( foo_name == "lock_currency")
    {
    }
    else if( foo_name == "unlock_currency")
    {
    }
    else if( foo_name == "clear_account_table")
    {
        clear_account_table( owner );
    }
}

 void lt_erc20::clear_account_table(account_name owner)
 {
    accounts from_acnts( _self, owner );
    auto ite = from_acnts.begin();
    while(ite != from_acnts.end()) {
        ite = from_acnts.erase(ite);
    }
 }

uint64_t lt_erc20::total_card_supply()
{
    cards card_table(_self, _self); 
    auto carditr = card_table.begin();
    uint64_t card_id = 0;
    while(carditr != card_table.end()){
        card_id++;
        carditr++;
    }
    return card_id;
}

void lt_erc20::issuecard(account_name issuer, 
                        account_name owner, 
                        uint64_t     activation_starttime, 
                        uint64_t     activation_endtime, 
                        uint64_t     exchange_starttime,
                        uint64_t     exchange_endtime,
                        bool         activation_state,
                        bool         disassembly_state,
                        bool         exchange_state,
                        asset        supply,
                        uint64_t     unlock_time,
                        uint64_t     issue_time, 
                        asset        init_rele_num,
                        uint64_t     cycle_time  , 
                        uint8_t      cycle_counts,
                        uint64_t     cycle_starttime,
                        asset        cycle_rele_num)
{
    require_auth(_self);

    print("create card to:", name{owner});

    uint64_t card_id = total_card_supply() + 1;

    print(";new card id:", card_id);

    //print("issuer length:",name{owner}.to_string().length());

    // accounts account_table(_self, owner);
    // auto accountitr = account_table.find(owner);

    // if(accountitr == account_table.end()){
    //     account_table.emplace(_self, [&](auto &a){
    //         a.owner = owner;
    //         a.balance = supply - supply;
    //     });
    // }

    stats statstable( _self, supply.symbol.name() );
    auto existing = statstable.find( supply.symbol.name() );
    print("\n supply.symbol:", supply.symbol.name());

    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;
    eosio_assert( supply.amount <= st.max_supply.amount - st.supply.amount, "supply exceeds available supply");
    statstable.modify( st, 0, [&]( auto& s ) {
        s.supply      += supply;
        s.update_time = time_point_sec(now());
        });

    // action(
    // permission_level{ _self, N(active) },
    // N(ltoooooerc2o), N(issue),  //调用 ltoooooerc2o 的 Transfer 合约
    // std::make_tuple(_self, to, quantity, std::string(""))
    // ).send();

    cards card_table(_self, _self); 
    card_table.emplace(_self, [&](auto &a) {
        a.id = card_id;
        a.parent_id = 0;
        a.owner = owner;  
        a.issuer = issuer; 
        a.activation_starttime = time_point_sec(now()) + activation_starttime;
        a.activation_endtime = time_point_sec(now()) + activation_endtime;
        a.exchange_starttime = time_point_sec(now()) + exchange_starttime;
        a.exchange_endtime = time_point_sec(now()) + exchange_endtime;
        a.issue_time = time_point_sec(now());
        a.update_time = time_point_sec(now());
        a.activation_state = activation_state;
        a.disassembly_state = disassembly_state;   
        a.exchange_state = exchange_state;
        a.supply.push_back(lock_balance_pair {
                    supply, 
                    time_point_sec(now()) + unlock_time,
                    time_point_sec(now()) + issue_time,
                    init_rele_num,
                    cycle_time,
                    cycle_counts,
                    time_point_sec(now()) + cycle_starttime,
                    cycle_rele_num                               
                    } );                          
    });


}

 
void lt_erc20::transcard( account_name from,
                        account_name to,
                        uint64_t card_id,
                        string       memo )
{
    eosio_assert( from != to, "cannot transfer to self" );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist");
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    require_recipient( from );
    require_recipient( to );
    
    cards card_table(_self, _self); 
    auto existing = card_table.find( card_id );
    eosio_assert( existing != card_table.end(), "This card does not exist." );
    const auto& exist_card = *existing;
    // print("exist_card.owner: ",exist_card.owner);
    // print("from: ",from);
    eosio_assert( exist_card.activation_state == true, "This card is still not activated and can not be operated." );
    eosio_assert( exist_card.exchange_state == false, "This card has been exchanged." );
    // eosio_assert( exist_card.disassembly_state == false, "This card has been splited." );
    eosio_assert( exist_card.owner == from, "This card does not belong to this owner." );
    eosio_assert( time_point_sec(now()) < exist_card.exchange_endtime  , "This card has expired." );
    
    card_table.modify( exist_card, 0, [&]( auto& s ) {
        s.owner       = to;
        s.update_time = time_point_sec(now());
        });

}

void lt_erc20::activatcard( account_name owner,
                        uint64_t card_id)
{
    require_auth( owner );
    
    cards card_table(_self, _self); 
    auto existing = card_table.find( card_id );
    eosio_assert( existing != card_table.end(), "This card does not exist." );
    const auto& exist_card = *existing;
    eosio_assert( exist_card.owner == owner, "This card does not belong to this holder." );
    eosio_assert( exist_card.activation_state == false, "This card has been activated." );
    eosio_assert( time_point_sec(now()) < exist_card.exchange_endtime , "This card has expired." );
    eosio_assert( time_point_sec(now()) >= exist_card.activation_starttime && time_point_sec(now()) <= exist_card.activation_endtime  , "This card is not in the activation period." );
    
    card_table.modify( exist_card, 0, [&]( auto& s ) {
        s.activation_state       = true;
        s.update_time = time_point_sec(now());
        });

}

void lt_erc20::exchangecard( account_name owner,
                        uint64_t card_id)
{
    require_auth( owner );
    
    cards card_table(_self, _self); 
    auto existing = card_table.find( card_id );
    eosio_assert( existing != card_table.end(), "This card does not exist." );
    const auto& exist_card = *existing;
    eosio_assert( exist_card.owner == owner, "This card does not belong to this holder." );
    eosio_assert( exist_card.activation_state == true, "This card is still not activated and can not be operated." );
    eosio_assert( exist_card.exchange_state == false, "This card has been exchanged." );
    eosio_assert( exist_card.disassembly_state == false || exist_card.parent_id == 0, "This card has been splited." );
    eosio_assert( time_point_sec(now()) < exist_card.exchange_endtime , "This card has expired." );
    eosio_assert( time_point_sec(now()) > exist_card.exchange_starttime , "This card does not reach the time to exchange." );
    

    accounts account_table( _self, owner );
    lock_asset::const_iterator  iter_card_lockasset = exist_card.supply.begin();
    auto acc = account_table.find( iter_card_lockasset->lock_balance.symbol.name() );
    if( acc == account_table.end() ) 
    {
        account_table.emplace( exist_card.issuer, [&]( auto& a ){
            a.balance     = iter_card_lockasset->init_rele_num;
            a.owner       = owner;
            a.update_time = time_point_sec(now());
            a.lock_balance_pairs.push_back(lock_balance_pair {
                iter_card_lockasset->lock_balance - iter_card_lockasset->init_rele_num, 
                iter_card_lockasset->unlock_time,
                iter_card_lockasset->issue_time,
                iter_card_lockasset->init_rele_num,
                iter_card_lockasset->cycle_time,
                iter_card_lockasset->cycle_counts,
                iter_card_lockasset->cycle_starttime,
                iter_card_lockasset->cycle_rele_num                            
                } );
        });
    } 
    else 
    {
        account_table.modify( acc, 0, [&]( auto& a ) {
            a.balance     += iter_card_lockasset->init_rele_num;
            a.update_time = time_point_sec(now());
            a.lock_balance_pairs.push_back(lock_balance_pair {
                iter_card_lockasset->lock_balance - iter_card_lockasset->init_rele_num, 
                iter_card_lockasset->unlock_time,
                iter_card_lockasset->issue_time,
                iter_card_lockasset->init_rele_num,
                iter_card_lockasset->cycle_time,
                iter_card_lockasset->cycle_counts,
                iter_card_lockasset->cycle_starttime,
                iter_card_lockasset->cycle_rele_num                           
                } );
        });
    }

    card_table.modify( exist_card, 0, [&]( auto& s ) {
        s.exchange_state = true;
        s.update_time = time_point_sec(now());
        });

}



void lt_erc20::splitcard( account_name owner,
                        uint64_t card_id,
                        uint64_t counts)
{
    require_auth( owner );
    
    cards card_table(_self, _self); 
    auto existing = card_table.find( card_id );
    eosio_assert( existing != card_table.end(), "This card does not exist." );
    const auto& exist_card = *existing;
    eosio_assert( exist_card.owner == owner, "This card does not belong to this holder." );
    eosio_assert( exist_card.activation_state == true, "This card is still not activated and can not be operated." );
    eosio_assert( exist_card.exchange_state == false, "This card has been exchanged." );
    eosio_assert( exist_card.disassembly_state == false, "This card has been splited." );
    eosio_assert( time_point_sec(now()) < exist_card.exchange_endtime , "This card has expired." );
    
    uint64_t child_card_id = total_card_supply();
    lock_asset::const_iterator  iter_card_lockasset = exist_card.supply.begin();
    // asset split_lock_balance  =
    // split_init_rele_num
    // split_cycle_rele_num
    for( int i = 1; i <= counts; i++)
    {        
        child_card_id = child_card_id + 1;
        card_table.emplace(_self, [&](auto &a) {
        a.id = child_card_id;
        a.parent_id = card_id;
        a.owner = exist_card.owner;  
        a.issuer = exist_card.issuer; 
        a.activation_starttime = exist_card.activation_starttime;
        a.activation_endtime = exist_card.activation_endtime;
        a.exchange_starttime = exist_card.exchange_starttime;
        a.exchange_endtime = exist_card.exchange_endtime;
        a.issue_time = exist_card.issue_time;
        a.update_time = time_point_sec(now());
        a.activation_state = true;
        a.disassembly_state = true;   
        a.exchange_state = false;
        a.supply.push_back(lock_balance_pair {
                    iter_card_lockasset->lock_balance / counts, 
                    iter_card_lockasset->unlock_time,
                    iter_card_lockasset->issue_time,
                    iter_card_lockasset->init_rele_num / counts,
                    iter_card_lockasset->cycle_time,
                    iter_card_lockasset->cycle_counts,
                    iter_card_lockasset->cycle_starttime,
                    iter_card_lockasset->cycle_rele_num / counts                              
                    } );                          
        });
    }

    card_table.modify( exist_card, 0, [&]( auto& s ) {
        s.disassembly_state = true;
        s.update_time = time_point_sec(now());
        });

}