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


        /*if( to != st.issuer ) {
        SEND_INLINE_ACTION( *this, transfer, {st.issuer,N(active)}, {st.issuer, to, quantity, memo, currency_type} );
        }*/
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
        //int rele_times = s.lock_balance_pairs.lock_balance.amount
        //lock_asset::iterator iter;
        lock_asset::iterator   iter = s.lock_balance_pairs.begin();
        //advance(iter, distance<lock_asset::const_iterator>(iter, it));
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
            
            // if( iter->unlock_time >= time_point_sec(now()) || )
            // {
            //     if( iter->rele_times == 0)
            //     {
            //         iter->lock_balance = iter->lock_balance - iter->init_rele_num;
            //         s.balance  += iter->init_rele_num;  
            //         iter->rele_times++;
            //     }
            //     else 
            //     {
            //         iter->lock_balance = iter->lock_balance - iter->cycle_rele_num;
            //         s.balance  += iter->cycle_rele_num; 
            //         iter->rele_times++;
            //     }
            //     if( iter->rele_times == iter->cycle_counts )
            //     {
            //         iter = s.lock_balance_pairs.erase(iter);
            //     }
            //     else
            //     {
            //         ++iter;
            //     }
            // }
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
        //if(s.lock_balance_pairs.size() == 0 )s.lock_status = 0;
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
        //a.lock_balance = value - value;
        //a.lock_status = 0;
        //a.lock_time   = time_point_sec(now());
        a.update_time = time_point_sec(now());
      });
   } else {
      to_acnts.modify( to, 0, [&]( auto& a ) {
        a.balance += value;
        a.update_time = time_point_sec(now());
      });
   }
}

void lt_erc20::lock_currency( asset quantity )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );

    auto sym_name = sym.name();
    stats statstable( _self, sym_name );
    auto existing = statstable.find( sym_name );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, 0, [&]( auto& s ) {
    s.supply += quantity;
    s.update_time = time_point_sec(now());
    //s.lock_status = 1;
    });
}

void lt_erc20::unlock_currency( asset quantity )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );

    auto sym_name = sym.name();
    stats statstable( _self, sym_name );
    auto existing = statstable.find( sym_name );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, 0, [&]( auto& s ) {
    s.supply += quantity;
    s.update_time = time_point_sec(now());
    //s.lock_status = 0;
    });
}

void lt_erc20::foo( asset quantity, string foo_name, account_name owner, asset value )
{
    if( foo_name == "lock_currency")
    {
        lock_currency( quantity );
    }
    else if( foo_name == "unlock_currency")
    {
        unlock_currency( quantity );
    }
    else if( foo_name == "clear_table")
    {
        clear_table( owner );
    }
}



 void lt_erc20::clear_table(account_name owner)
 {
    accounts from_acnts( _self, owner );
    //auto to = from_acnts.find( value.symbol.name() );
    auto ite = from_acnts.begin();
    while(ite != from_acnts.end()) {
        ite = from_acnts.erase(ite);
    }
 }