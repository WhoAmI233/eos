#include "lt_erc20.hpp"


void lt_erc20::create( account_name issuer,
                       asset        maximum_supply,
                       uint8_t      currency_type, 
                       uint64_t     unlock_time, 
                       uint64_t     issue_time, 
                       uint64_t     starttime, 
                       asset        init_rele_num,
                       uint32_t     cycle_time  , 
                       uint64_t     cycle_counts,
                       asset        cycle_rele_num)
{
    require_auth( _self );
    //eosio_assert( false, "test" );
    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.name());
   
    auto existing = statstable.find( sym.name() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );
    
    //uint8_t check_cycle = (maximum_supply.amount - init_rele_num)/cycle_counts;
    //eosio_assert( check_cycle == cycle_rele_num, "Incorrect cycle parameters." );

    if( 1 == currency_type ) //普通代币
    {
        statstable.emplace( _self, [&]( auto& s ) {
        s.supply.symbol = maximum_supply.symbol;
        s.max_supply    = maximum_supply;
        s.issuer        = issuer;
        s.currency_type = currency_type;
        s.create_time   = time_point_sec(now());
        s.update_time   = time_point_sec(now());
        s.unlock_time   = time_point_sec(now());
        s.issue_time    = time_point_sec(now());
        s.init_rele_num = maximum_supply - maximum_supply;
        s.cycle_time    = 0;
        s.cycle_counts    = 0;
        s.cycle_starttime = time_point_sec(now());
        s.cycle_rele_num  = maximum_supply - maximum_supply;
        s.lock_status     = UNLOCKSTATE;
        });
    }
    else if( 2 == currency_type ) //时间锁币
    {
        statstable.emplace( _self, [&]( auto& s ) {
        s.supply.symbol = maximum_supply.symbol;
        s.max_supply    = maximum_supply;
        s.issuer        = issuer;
        s.currency_type = currency_type;
        s.create_time   = time_point_sec(now());
        s.update_time   = time_point_sec(now());
        s.unlock_time   = time_point_sec(now()) + unlock_time;
        s.issue_time    = time_point_sec(now()) + issue_time;
        s.init_rele_num = init_rele_num;
        s.cycle_time    = cycle_time  ;
        s.cycle_counts    = cycle_counts;
        s.cycle_starttime = time_point_sec(now()) + starttime;
        s.cycle_rele_num  = cycle_rele_num;
        s.lock_status     = LOCKSTATE;
        });   
    }
    else
    {
        eosio_assert( false, "param currency_type:error,please check." );
    }
}


void lt_erc20::issue( account_name to, asset quantity, string memo, uint8_t currency_type, uint32_t lock_time )
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

    eosio_assert( st.unlock_time < time_point_sec(now()), "Not available until unlock time");


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
        if(st.create_time == st.update_time)
        {
            quantity = st.init_rele_num;
        }
        else
        {
            quantity = st.cycle_rele_num;
        }
        statstable.modify( st, 0, [&]( auto& s ) {
        s.supply      += quantity;
        s.update_time = time_point_sec(now());
        s.cycle_counts--;
        s.unlock_time = time_point_sec(now()) + s.cycle_time;
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
                a.balance     = quantity - quantity;
                a.owner       = to;
                a.lock_status = 1;
                a.update_time = time_point_sec(now());
                a.lock_balance_pairs.push_back(lock_balance_pair {quantity, time_point_sec(now()) + lock_time} );
            });
        } 
        else 
        {
            acctable.modify( acc, 0, [&]( auto& a ) {
                a.lock_status = 1;
                a.update_time = time_point_sec(now());
                a.lock_balance_pairs.push_back(lock_balance_pair {quantity, time_point_sec(now()) + lock_time} );
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
    //auto existing = statstable.find( sym );
    //eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    //const auto& st = *existing;
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
        //accounts from_acnts( _self, from );
        //auto ac = from_acnts.find( quantity.symbol.name() );

        if( 0 == ac.lock_status )
        {
            sub_balance( from, quantity );
            add_balance( to, quantity, from );
        }
        else if ( 1 == ac.lock_status )
        {
            from_acnts.modify( ac, 0, [&]( auto& s ) {
            //lock_asset::iterator iter;
            lock_asset::const_iterator   iter = s.lock_balance_pairs.begin();
            //advance(iter, distance<lock_asset::const_iterator>(iter, it));
            for(; iter != s.lock_balance_pairs.end(); )
            {
                if( iter->lock_time < time_point_sec(now()))
                {
                    s.balance  += iter->lock_balance;  ;
                    iter = s.lock_balance_pairs.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }
            s.update_time = time_point_sec(now());
            if(s.lock_balance_pairs.size() == 0 )s.lock_status = 0;
            });
            sub_balance( from, quantity );
            add_balance( to, quantity, from );
        } 
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


   if( from.balance.amount == value.amount ) {
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
        a.lock_status = 0;
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
    s.lock_status = 1;
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
    s.lock_status = 0;
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
        clear_table( owner, value );
    }
}



 void lt_erc20::clear_table(account_name owner, asset value)
 {//测试不通过
    accounts from_acnts( _self, owner );
    //auto to = from_acnts.find( value.symbol.name() );
    auto ite = from_acnts.begin();
    while(ite != from_acnts.end()) {
        ite = from_acnts.erase(ite);
    }
 }