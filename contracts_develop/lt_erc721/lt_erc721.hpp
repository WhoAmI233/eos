/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosiolib/eosio.hpp>
#include <string>

using namespace eosio;
using namespace std;

class lt_erc721 : public eosio::contract
{
    public:
    lt_erc721(account_name self)
        : contract(self), _accounts(_self, _self), _allowances(_self, _self), _tokens(_self, _self) {}


    private:
        struct var 
        {            
            std::string key;       
            std::string value;        
        };

        //@abi table tokens i64 
        struct token 
        {
            uint64_t id;
            
            bool frozen;

            //user associations
            account_name owner;
            account_name issuer;        

            //data        
            std::vector<std::string> keys;
            std::vector<std::string> values;

            std::vector<var> vars;

            uint64_t primary_key() const { return id; }
            
            EOSLIB_SERIALIZE( token, (id)(frozen)(owner)(issuer)(keys)(values)(vars) )
        };

        eosio::multi_index<N(tokens), token> _tokens;

        //@abi table accounts i64 
        struct account
        {
            account_name owner;            
            uint64_t balance;

            uint64_t primary_key() const { return owner; }

            EOSLIB_SERIALIZE( account, (owner)(balance))
        };

        eosio::multi_index<N(accounts), account> _accounts;

        //@abi table allowances i64 
        struct allowance
        {
            uint64_t token_id;   
            account_name to;                         

            uint64_t primary_key() const { return token_id; }

            EOSLIB_SERIALIZE( allowance, (token_id)(to))
        };

        eosio::multi_index<N(allowances), allowance> _allowances;

        void transfer_balances(account_name from, account_name to, int64_t amount=1);

        bool _owns(account_name claimant, uint64_t token_id);

    public:
        // Required methods
        uint64_t total_supply();

        //returns balance of
        uint64_t balance_of(account_name owner);

        //returns who owns a token
        account_name owner_of(uint64_t token_id);

        void approve(account_name from, account_name to, uint64_t token_id);

        void mint(account_name owner, vector<string> keys = {}, vector<string> values = {}, bool is_frozen=false);

        void transfer(account_name sender, account_name to, uint64_t token_id);


        void transferfrom(account_name sender, account_name from, account_name to, uint64_t token_id);

        void burn(account_name burner, uint64_t token_id);

        void burnfrom(account_name burner, account_name from, uint64_t token_id);
};

//typedef erc721<instrument_data> instrument;

EOSIO_ABI(lt_erc721, (transfer)(mint)(approve)(transferfrom)(burn)(burnfrom) )
