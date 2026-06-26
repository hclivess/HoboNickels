# HoboNickels documentation

## Modernized (current)

- **[MODERNIZATION.md](MODERNIZATION.md)** — what changed and why (OpenSSL 3,
  libsecp256k1, Boost 1.83, C++17, Qt5, CMake, CI/Docker, faster sync). Start here.
- **[build-unix-modern.md](build-unix-modern.md)** — building with CMake (daemon,
  Qt5 GUI, tests, options, Docker).
- **[configuration.md](configuration.md)** — runtime options reference (new
  performance/sync/staking/network knobs and how to set them).
- **[staking-performance.md](staking-performance.md)** — how proof-of-stake
  minting works, the performance optimizations applied, deferred ideas (which
  must never omit mempool transactions), and the consensus off-limits zone.
- **[performance.md](performance.md)** — daemon sync/validation/memory
  optimizations: what's applied, the deferred roadmap, and the off-limits zone.
- **[headers-first-sync.md](headers-first-sync.md)** — headers-first block download
  (`-headersfirst`, default on): how it works, why it's wire-compatible, the PoS
  wrinkle, and the test checklist.
- **[gui.md](gui.md)** — the Qt5 wallet: modernized look-and-feel (high-DPI,
  Fusion style, light/dark themes via `-uitheme`), the RPC console, and staking.

## Legacy (pre-modernization, kept for reference)

- `build-unix.txt`, `build-osx.txt`, `build-msw.txt` — original build notes for
  the old makefile/`.pro` system. Superseded by `build-unix-modern.md`.
- `readme-qt.rst`, `coding.txt`, `release-process.txt`,
  `translation_process.md`, `assets-attribution.txt` — original project docs.
