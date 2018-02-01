## simulation framework for blockchain/dlt, wip.

Deterministic and repeatable simulation framework for experimenting with consensus protocols for blockchain and other types of distributed ledgers.


### Requirements

- CryptoPP 6.x
- CMake
- C++14 compatible compiler
- ncurses

### Build

```
git-clone git@github.com:jgh-/dlt-sim
cd dlt-sim
./bootstrap.sh
mkdir build
cd build
cmake ..
make
```

Executables will be built into the build folder


### Running

If you build your own class (see `obelisk.cc` for an example) running will obviously depend on you.  For simulations that come with this
project, you can run it via 
`./[consensus_name] [seed]`, where seed is a 64-bit integer in base-10.  Seed is an optional paramter, so if it is not included the program will run with a random seed.

### Included consensus protocols (so far)

- Obelisk ([Skycoin](http://github.com/skycoin/whitepapers))

