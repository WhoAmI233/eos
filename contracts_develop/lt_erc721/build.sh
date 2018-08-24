export PATH=$PATH:/usr/local/eosio/bin/:/usr/local/eosio/include/:/home/charm/eos/build/tools:/home/charm/eos/build/programs/nodeos:/home/charm/eos/build/programs/cleos:/home/charm/opt/wasm/bin:/home/charm/eos/build/programs/eosio-abigen
cleos wallet unlock -n liantong --password PW5KhXT5tXkppEyRbX7rxtkAKL82LDPhJBfjPYH98Hq2Xw71gfXFU
eosiocpp -o /home/charm/eos/contracts_develop/lt_erc721/lt_erc721.wast /home/charm/eos/contracts_develop/lt_erc721/lt_erc721.cpp
eosiocpp -g /home/charm/eos/contracts_develop/lt_erc721/lt_erc721.abi /home/charm/eos/contracts_develop/lt_erc721/lt_erc721.hpp
cleos -u "http://dev03.cryptolions.io:8890"  set contract ltoooooerc21 /home/charm/eos/contracts_develop/lt_erc721/ -p ltoooooerc21@active