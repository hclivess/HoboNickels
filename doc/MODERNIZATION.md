# HoboNickels modernization

This documents the modernization of HoboNickels from its 2014-era Bitcoin /
Peercoin / Novacoin fork state to a codebase that builds and runs on current
toolchains, with faster sync and a cleaner build — **without any consensus
change** (the network protocol, kernel, signatures and addresses are unchanged,
so a modernized node stays fully compatible with the existing chain).

See also:
- [`build-unix-modern.md`](build-unix-modern.md) — how to build (CMake).
- [`configuration.md`](configuration.md) — new/relevant runtime options.
- [`staking-performance.md`](staking-performance.md) — PoS staking internals & tuning.

---

## 1. Toolchain & dependencies

Targets and verified on: **Ubuntu 24.04, GCC 13, OpenSSL 3.0, Boost 1.83,
Berkeley DB 5.3, Qt 5.15, libsecp256k1**, plus **Windows x86_64** via MSYS2/mingw64.

| Area | Before | After |
| --- | --- | --- |
| Build | hand-written `makefile.unix` / `.pro` | top-level **CMake** |
| Crypto | OpenSSL 1.0 (opaque-struct era) | **OpenSSL 3** + **libsecp256k1** verify |
| C++ | pre-C++11 idioms | **C++17** |
| Boost | ~1.4x APIs | **1.83** |
| GUI | Qt4-era | **Qt5** |
| CI / packaging | none | **GitHub Actions** (Linux+Windows+Docker) + **Dockerfile** |

## 2. Crypto port (OpenSSL 3) — consensus-preserving

- **`bignum.h`** — `CBigNum` no longer derives from the now-opaque `BIGNUM`; it
  owns a heap `BIGNUM*` and uses the public `BN_*` accessor API. All
  serialization (`getvch`/`setvch`, `GetCompact`/`SetCompact`, `getuint256`, …)
  is byte-for-byte unchanged.
- **`key.cpp`** — `ECDSA_SIG` r/s via `ECDSA_SIG_get0/set0`; low-S via
  `EC_GROUP_get0_order`; `_GFp` coordinate functions replaced with the modern
  ones. The `EC_KEY`/`ECDSA` API (deprecated but functional in OpenSSL 3) is
  kept so signatures, key recovery and addresses are identical.
- **`crypter.cpp`** — `EVP_CIPHER_CTX` heap-allocated (opaque since 1.1).

## 3. Faster ECDSA verification (libsecp256k1)

`CheckSig` (script.cpp) verifies with **libsecp256k1** (far faster than OpenSSL's
generic EC), falling back to OpenSSL for any signature/pubkey it cannot strictly
parse — so the accept/reject decision is **identical** to the OpenSSL-only build
(no fork). A unit test (`crypto_selftest/secp256k1_matches_openssl`) checks the
two libraries agree. On Linux/Docker the system library is used; on Windows it is
built from source (static) via CMake FetchContent.

## 4. Faster initial sync

- **Headers-first download** (default on) — learn the chain ahead from cheap ~80-byte
  headers via `getheaders`, then pull full blocks through a rolling window, instead of
  the legacy 500-block `getblocks → inv → getdata` round-trips. Wire-compatible, no
  `PROTOCOL_VERSION` change, peers don't need to upgrade. See
  [`headers-first-sync.md`](headers-first-sync.md).
- **Multi-peer parallel block download** — the in-flight window is spread across all
  connected peers with per-block accounting (no duplicate fetches) and a timeout
  re-queue, instead of serializing the download on a single peer. The largest IBD win
  on a fast CPU.
- **scrypt cost reduction** — `GetHash` is memoized (each block/header is hashed once),
  non-connecting junk headers are dropped *before* any scrypt work, and the scrypt TU
  is compiled at `-O3`. See [`proposals/drop-scrypt-hardfork.md`](proposals/drop-scrypt-hardfork.md)
  for the larger (fork-requiring) option of dropping scrypt entirely.
- **Dynamic checkpointing** — signatures are re-verified only for the most recent
  `-checkpointdepth` blocks (default 500); older history is trusted. The
  hardcoded checkpoints are a hard floor and still hash-anchor the chain, so
  substituted history is rejected. `-checkpointdepth=0` re-verifies everything.
- **Recent checkpoints** added up to height 7,990,000.
- **LevelDB tuning** — default `-dbcache` raised 25 → 256 MB plus a write-buffer
  bump (fewer disk reads / compactions during IBD).
- **Bootstrap nodes** baked into the DNS-seed list so a fresh client auto-connects.
- **Staking** — safe, behavior-preserving optimizations; see
  [`staking-performance.md`](staking-performance.md).

DoS hardening that came with the new peer-facing surface (headers handler) is
documented in [`headers-first-sync.md`](headers-first-sync.md): connecting-headers-only
gating of scrypt work, per-message caps, an `AskFor` queue cap, and a `mapOrphanBlocks`
cap. A full pass over what else modern Bitcoin Core / the BIPs offer — imported vs.
staged vs. out-of-scope — is in [`bitcoin-core-comparison.md`](bitcoin-core-comparison.md).

## 5. Thorough C++17 cleanup

~300 `BOOST_FOREACH` → range-`for` (declarations preserved verbatim), plus
`boost::shared_ptr`/`array`/`tuple` → `std::` equivalents, `std::auto_ptr` →
`std::unique_ptr`, allocator `throw()` → `noexcept`, and dead boost includes
removed. Load-bearing Boost (asio, signals2, program_options, thread, filesystem,
interprocess, spirit) is retained. Behaviour preserved.

## 6. Qt5 GUI

The Qt4-era wallet (`src/qt`, 15 `.ui` forms) builds and runs under Qt 5.15 via
CMake (`-DWITH_QT_GUI=ON`): `<QComboBox>`/`<QPainterPath>` header moves,
`boost::placeholders`, explicit `qt5_wrap_ui`. Builds on Linux and Windows.

## 7. Build, CI & packaging

- `cmake -S . -B build && cmake --build build -j$(nproc)` — see
  [`build-unix-modern.md`](build-unix-modern.md).
- **GitHub Actions** builds + tests the daemon, the Qt5 GUI, and a Docker image on
  every push, for both Linux and Windows; each Windows binary is packaged as a
  self-contained zip (all dependent DLLs bundled, verified to run with the
  toolchain off `PATH`).
- **Dockerfile** — multi-stage build producing a runnable `HoboNickelsd` image.

## 8. Removed / dead code

- IRC peer discovery (`irc.cpp`, hardcoded to a defunct network) — removed; DNS
  seeding is the discovery path.
- The unused scrypt assembly (`OPTIMIZED_SALSA` was never defined; the C++
  `scrypt.cpp` fallback was always used) — dropped from the build.

## 9. Releases

Binaries (Linux + Windows daemon & Qt5 GUI) are published on the GitHub
[Releases](https://github.com/hclivess/HoboNickels/releases) page. Windows
downloads are self-contained zips.
