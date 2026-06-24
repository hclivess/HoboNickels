# Building HoboNickels on a modern Linux (CMake)

These instructions cover the modernized build, which targets modern
toolchains (tested on **Ubuntu 24.04**, GCC 13, **OpenSSL 3**, **Boost 1.83**,
**Berkeley DB 5.3**, and **Qt 5.15**). The legacy `src/makefile.unix` and the
`.pro` file are retained for reference but are no longer the recommended path.

## 1. Dependencies

```sh
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config \
    libboost-all-dev libssl-dev libdb++-dev \
    libminiupnpc-dev zlib1g-dev
```

For the Qt5 GUI also install:

```sh
sudo apt-get install -y \
    qtbase5-dev qttools5-dev qttools5-dev-tools qtbase5-dev-tools
```

## 2. Configure & build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

This builds:

* `build/HoboNickelsd` — the headless daemon.
* `build/test_hobonickels` — the unit-test binary (when `BUILD_TESTS=ON`).

### Useful options

| Option            | Default | Description                              |
| ----------------- | ------- | ---------------------------------------- |
| `ENABLE_UPNP`     | `OFF`   | Build with UPnP port mapping.            |
| `WITH_QT_GUI`     | `OFF`   | Build the Qt5 wallet (`HoboNickels-qt`). |
| `WITH_WALLET`     | `ON`    | Wallet support via Berkeley DB.          |
| `BUILD_TESTS`     | `ON`    | Build the Boost.Test suite.              |

Example (everything on):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_UPNP=ON -DWITH_QT_GUI=ON
cmake --build build -j"$(nproc)"
```

## 3. Run the tests

```sh
ctest --test-dir build --output-on-failure
```

## 4. Run the daemon

```sh
./build/HoboNickelsd -printtoconsole
```

On first run it will ask you to set an `rpcuser`/`rpcpassword` in
`~/.HoboNickels/HoboNickels.conf`.

## Docker

A multi-stage `Dockerfile` is provided:

```sh
docker build -t hobonickels .
docker run --rm hobonickels --help
docker run -d -v hbn-data:/data --name hbn hobonickels
```

## Notes on the modernization

* **OpenSSL 3** — `CBigNum` and the ECDSA code were ported off the now-opaque
  `BIGNUM`/`ECDSA_SIG` internals onto the public accessor API. Signatures,
  key recovery and addresses are unchanged (consensus-compatible).
* **Bundled LevelDB 1.19** is compiled via its own Makefile through CMake's
  `ExternalProject`.
* The dead IRC peer-discovery path and the unused scrypt assembly were removed;
  peer discovery uses DNS seeding.
* A handful of upstream-divergent unit-test files are temporarily excluded from
  the build (they reference APIs that never existed in this tree); the
  crypto/consensus suites run and pass.
