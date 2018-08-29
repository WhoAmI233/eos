#include "lt_erc721.hpp"


void lt_erc721::transfer_balances(account_name from, account_name to, int64_t amount)
{
    if(from != 0){
        auto fromitr = _accounts.find(from);
        _accounts.modify(fromitr, 0, [&](auto &a){
            a.balance -= amount;
        });
    }

    if(to != 0){
        auto toitr = _accounts.find(to);

        if(toitr != _accounts.end()){
            _accounts.modify(toitr, 0, [&](auto &a){
                a.balance += amount;
            });
        }else{
            _accounts.emplace(from, [&](auto &a) {
                a.owner = to;
                a.balance = amount;
            });
        }

        
    }
}


bool lt_erc721::_owns(account_name claimant, uint64_t token_id)
{
    return owner_of(token_id) == claimant;
}


uint64_t lt_erc721::total_supply()
{
    auto tokitr = _tokens.begin();
    uint64_t token_id = 0;
    while(tokitr != _tokens.end()){
        token_id++;
        tokitr++;
    }

    return token_id;
}

uint64_t lt_erc721::balance_of(account_name owner) 
{
    auto account = _accounts.find(owner);
    return account->balance;
}

account_name lt_erc721::owner_of(uint64_t token_id)
{
    auto token = _tokens.find(token_id);
    return token->owner;
}

void lt_erc721::approve(account_name from, account_name to, uint64_t token_id)
{            
    require_auth(from);

    auto tokitr = _tokens.find(token_id);

    //check to see if approver owns the token
    if(tokitr == _tokens.end() || tokitr->owner != from ){            
        eosio_assert(false, "token does not exist");
    }

    auto allowanceitr = _allowances.find(token_id);
    if (allowanceitr == _allowances.end())
    {            
        _allowances.emplace(token_id, [&](auto &a) {                
            a.to = to;
            a.token_id = token_id;                 
        });
    }
    else
    {
        _allowances.modify(allowanceitr, 0, [&](auto &a) {
            a.to = to;                
        });
    }
}

void lt_erc721::mint(account_name owner, vector<string> keys , vector<string> values , bool is_frozen)
{
    require_auth(_self);

    print("minting token to", name{owner});

    uint64_t token_id = total_supply() + 1;

    print("new token id", token_id);

    auto accountitr = _accounts.find(owner);

    if(accountitr == _accounts.end()){
        _accounts.emplace(_self, [&](auto &a){
            a.owner = owner;
            a.balance = 1;
        });
    }

    vector<var> vars;
    for(int i = 0; i<keys.size(); i++){
        vars.push_back(var {keys[i], values[i]} );
    }

    _tokens.emplace(_self, [&](auto &a) {
        a.owner = owner;
        a.issuer = _self;
        a.id = token_id;  
        a.frozen = is_frozen; 
        a.keys = keys;
        a.values = values;   
        a.vars = vars;                              
    });
}

void lt_erc721::transfer(account_name sender, account_name to, uint64_t token_id)
{
    require_auth(sender);

    //find token
    auto tokenitr = _tokens.find(token_id);

    //make sure token exists and sender owns it
    if(tokenitr != _tokens.end() && tokenitr->owner == sender){
        //update token's owner
        _tokens.modify(tokenitr, 0, [&](auto &a){
            a.owner = to;
        });

        //increment/decrement balances 
        transfer_balances(sender, to);  
    } 
}


void lt_erc721::transferfrom(account_name sender, account_name from, account_name to, uint64_t token_id)
{        
    require_auth(sender);

    //try to find allowance and token
    auto allowanceitr = _allowances.find(token_id);
    auto tokenitr = _tokens.find(token_id);

    //make sure the token/allowances for token exist and the users match
    if (tokenitr != _tokens.end() && tokenitr->owner == from &&
        allowanceitr != _allowances.end() && allowanceitr->to == sender)
    {
        _tokens.modify(tokenitr, 0, [&](auto &a){
            a.owner = to;
        });     
        transfer_balances(from, to);     
        _allowances.erase(allowanceitr);
    } 
}     

void lt_erc721::burn(account_name burner, uint64_t token_id)
{
    require_auth(burner);
    auto tokenitr = _tokens.find(token_id);
    
    if(tokenitr != _tokens.end() && tokenitr->owner == burner){
        transfer_balances(burner, 0);     
        //_tokens.erase(tokenitr);
        _tokens.modify(tokenitr, 0, [&](auto &a){
            a.owner = 0;
        }); 
    }
}

void lt_erc721::burnfrom(account_name burner, account_name from, uint64_t token_id)
{
    require_auth(burner);

    //try to find allowance and token
    auto allowanceitr = _allowances.find(token_id);
    auto tokenitr = _tokens.find(token_id);

    //make sure the token/allowances for token exist and the users match
    if (tokenitr != _tokens.end() && tokenitr->owner == from &&
        allowanceitr != _allowances.end() && allowanceitr->to == burner)
    {
        //_tokens.erase(tokenitr);    
        transfer_balances(from, 0);     
        //_allowances.erase(allowanceitr);
        _tokens.modify(tokenitr, 0, [&](auto &a){
            a.owner = 0;
        }); 
    }
}

