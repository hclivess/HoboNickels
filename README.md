# HoboNickels (HBN)

A proof-of-stake / proof-of-work hybrid cryptocurrency, originally a 2014-era
Bitcoin / Peercoin / Novacoin fork.

## Modernized build

This tree has been modernized to build and run on a current toolchain
(**Ubuntu 24.04**, **GCC 13**, **OpenSSL 3**, **Boost 1.83**, **Berkeley DB 5.3**,
**Qt 5**) via a new CMake build system. Consensus behaviour — signatures,
addresses, hashing — is unchanged.

```sh
sudo apt-get install -y build-essential cmake pkg-config \
    libboost-all-dev libssl-dev libdb++-dev libminiupnpc-dev zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/HoboNickelsd --help
```

See [`doc/build-unix-modern.md`](doc/build-unix-modern.md) for full options
(UPnP, Qt5 GUI, tests) and Docker usage.

---

## HoboNickels Crypto Tokens

* Staking:
  * 100% Max Yearly Stake Reward
  * 20% Min Stake Reward
  * 250 Max Reward Cap
  * 1 day min weight, 30 days max weight.
  
* Based on NVC/BitGems/Bottlecaps
* Proof of Work/Proof of Stake Hybrid. 
* Scrypt
* Linear Difficulty Retarget
* 25 Confirms
* 5 Tokens Per Block
* Maximum of 120000000 Tokens
* Default P2P Port: 7372
* Default RPC Port: 7373
* Dynamically Loadable Wallets 
* Updated Coin Control
* Easy Accessible Peer, Stake, and Block information
* Stake For Charity
* Built in Block Browser and Network Graph
* Configurable splitthreshold and combinethreshold
