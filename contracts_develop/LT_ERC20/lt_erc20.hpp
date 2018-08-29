#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

#include <string>
#include <eosiolib/time.hpp>
#include <vector>
#include <iterator>

using namespace eosio;
using namespace std;
using std::string;

   class lt_erc20 : public contract {
      public:
         lt_erc20( account_name self ):contract(self){}

         void create( account_name issuer,
                      asset        maximum_supply,
                      uint8_t      currency_type);

         void issue( account_name to, asset quantity, string memo , 
                        uint8_t      currency_type, 
                        uint64_t     unlock_time,
                        uint64_t     issue_time, 
                        asset        init_rele_num,
                        uint64_t     cycle_time  , 
                        uint8_t      cycle_counts,
                        uint64_t     cycle_starttime,
                        asset        cycle_rele_num );

         void transfer( account_name from,
                        account_name to,
                        asset        quantity,
                        string       memo , 
                        uint8_t currency_type);

         void issuecards(account_name issuer, 
                        account_name owner, 
                        uint64_t     activation_time, 
                        uint64_t     exchange_time, 
                        uint64_t     expiry_time,
                        bool         activation_state,
                        bool         disassembly_state,
                        bool         exchange_state,
                        asset        supply);
      
         void foo( string foo_name, account_name owner, asset value );
      
      
      private:
         struct lock_balance_pair{
            asset          lock_balance;
            time_point_sec unlock_time;
            time_point_sec issue_time;
            asset          init_rele_num;
            uint64_t       cycle_time; //单位秒
            uint8_t        cycle_counts;
            time_point_sec cycle_starttime;
            asset          cycle_rele_num;
         };

         typedef vector<lock_balance_pair> lock_asset;
         /// @abi table accounts i64
         struct account {
            asset          balance;
            account_name   owner;

            time_point_sec update_time;
            lock_asset     lock_balance_pairs;

            uint64_t primary_key()const { return balance.symbol.name(); }
         };
         typedef eosio::multi_index<N(accounts), account> accounts;
         

         /// @abi table stats i64
         struct currency {
            asset          supply;
            asset          max_supply;
            account_name   issuer;
            uint8_t        currency_type; //1：普通代币；2：时间锁币
            time_point_sec create_time;
            time_point_sec update_time;

            uint64_t primary_key()const { return supply.symbol.name(); }
         };
         typedef eosio::multi_index<N(stats), currency> stats;
         
         /// @abi table cards i64
         struct card 
         {
            uint64_t id;
            uint64_t parent_id;
            account_name owner;
            account_name issuer;   
            bool activation_state;
            time_point_sec activation_time;
            time_point_sec exchange_time;
            time_point_sec issue_time;
            time_point_sec update_time;
            time_point_sec expiry_time;
            bool disassembly_state;
            bool exchange_state; 
            asset supply;           

            uint64_t primary_key() const { return id; }
         };
         typedef eosio::multi_index<N(cards), card> cards;

         void sub_balance( account_name owner, asset value );
         void add_balance( account_name owner, asset value, account_name ram_payer );
         uint64_t total_card_supply();

         void clear_account_table( account_name owner); 

      public:
         struct transfer_args {
            account_name  from;
            account_name  to;
            asset         quantity;
            string        memo;
         };
   };

EOSIO_ABI( lt_erc20, (create)(issue)(transfer)(foo)(issuecards) )
