# Bitcoin Core / BIP comparison & import roadmap

HoboNickels forked from an early-2010s Bitcoin/Novacoin/Peercoin lineage. This is a
deliberate pass over what modern Bitcoin Core (and the BIP process) added since, split
into **what was safe to import** (done, non-consensus, wire-compatible) and **what is
staged** (designed but deferred because it is a large rewrite and/or needs the chain
owner's live-network testing). 

**Hard constraint for everything here:** no consensus fork. HoboNickels' PoS kernel,
stake modifier, scrypt block hash, address format, and validation rules are unchanged.
SegWit (BIP141/143/144), Taproot (BIP340-342), bech32 (BIP173/350), RBF, and
`assumevalid`-style validity skipping are **out of scope** — they change consensus or
the address/script system and would split this chain.

## Imported (shipped in the -modern line)

| Area | What | Notes |
|---|---|---|
| Sync | **Headers-first download** | `getheaders` look-ahead drives a rolling download window. Wire-compatible, `PROTOCOL_VERSION` unchanged. Default on. See `headers-first-sync.md`. |
| Sync | **Multi-peer parallel block download** | Per-block in-flight accounting (`g_mapBlocksInFlight`), per-peer window, timeout re-queue — the in-flight window is spread across all peers instead of one. The single biggest IBD win on a fast CPU. |
| Sync | **`-dbcache` default 64 → 256 MB** | The per-input `ReadTxIndex` pattern makes the block-tree/coin cache unusually hot during IBD. |
| Sync/CPU | **`GetHash` memoization + connecting-headers skip + `-O3` scrypt** | Hash each block/header exactly once; never scrypt-hash a non-connecting (junk) header; compile the scrypt TU at `-O3`. |
| DoS | **Orphan-block cap, headers per-msg cap, AskFor queue cap** | Bound the new and pre-existing unbounded peer-driven maps. |
| RPC | **Cookie auth (Bitcoin Core style)** | `<datadir>/.cookie` lets local tools authenticate with no configured `rpcpassword`; remote callers can't read it, so it's as safe as the localhost-default binding. |
| Tooling | **Single-file block explorer** | `contrib/explorer/`. |

## Staged — large, safe-in-principle, deferred

These are genuinely valuable and do **not** require a consensus fork, but each is a
substantial standalone effort that should be implemented and **tested on the live
network by the chain owner** one at a time, not batched. Ordered by value/effort.

> **The residual IBD bottleneck is disk I/O, not CPU.** A syncing node sits at ~7% CPU,
> so it is wait-bound. After the 2.0.11 download fixes remove the network gating, the
> remaining cost is synchronous random-read disk I/O in `ConnectBlock → FetchInputs`:
> per input, a random `ReadTxIndex` LevelDB lookup **plus** a `ReadFromDisk` block-file
> seek (a fresh `fopen`/seek/read, no descriptor cache), plus a per-tx `ReadTxIndex` for
> double-spend detection — all serialized under `cs_main` on one thread. Items 1–3 below
> target exactly this and are the highest-value next work.

### 1. Block-file descriptor cache (high value, **small**, non-consensus)
`ReadFromDisk` opens (`fopen`) and closes a `blkNNNN.dat` for **every input read** with
no persistent handle. An LRU of open file descriptors eliminates the per-input
`open`/`close` syscall pair on the hottest IBD path. Small, self-contained, pure I/O
win, zero consensus risk. *Cheapest meaningful sync speedup left.*

### 2. Input read-ahead / prefetch (high value, medium, non-consensus)
The connect path is strictly read-then-compute serial: one random seek at a time, with
the disk idle while the CPU works and vice-versa. Prefetching the next queued block's
inputs (or batching `ReadTxIndex` lookups) gives the disk multiple outstanding requests
and overlaps I/O with compute — which is what actually lifts CPU above ~7% on the
validation side. *Raise `-dbcache` further (512 MB) for large chains alongside this.*

### 3. UTXO/coins cache for connection (high value, large, consensus-adjacent)
Modern Core validates against an in-memory coins view (`CCoinsViewCache`) flushed
periodically, instead of per-input `ReadTxIndex`/`txindex` LevelDB lookups — removing
most of the random reads above entirely. The bigger, structural version of items 1–2.
It is non-consensus (same validation result) but touches the hottest validation path, so
it must be byte-for-byte equivalent and diffed against the current binary across a full
resync before trust. *Risk: a wrong cache = silent consensus divergence. Must be staged
with live diffing.*

### 2. Deterministic HD wallet — BIP32 + BIP39 + BIP44 (high value, large, format change)
Today's wallet is a random keypool (`wallet.dat` BDB). BIP32 hierarchical keys from a
single seed + a BIP39 mnemonic backup is the single biggest *wallet* UX modernization:
one phrase backs up all future keys. Non-consensus (addresses are still the existing
base58 P2PKH). *Risk: wallet format/version bump and recovery semantics — needs careful
backward-compat and a lot of wallet testing. Stage behind a new wallet version flag.*

### 3. Mempool persistence (medium)
Core writes `mempool.dat` on shutdown and reloads on start (no re-relay needed to refill
the mempool). Self-contained, no wire change. Good, contained next pick.

### 4. `sendheaders` — BIP130 (medium)
Announce new blocks via `headers` instead of `inv`+`getdata`, cutting a round-trip on
block relay. Wire-additive (negotiated), no `PROTOCOL_VERSION` flag day. Pairs naturally
with the headers-first code already in place.

### 5. Block-relay-only / addr-rate-limiting / inbound eviction (medium, hardening)
Modern Core's anti-eclipse and DoS work: feeler connections, rotating block-relay-only
peers, `addr` rate limiting, and smarter inbound eviction (`AttemptToEvictConnection`).
Each is independently importable net.cpp hardening.

### 6. Fee estimation (medium/large)
`estimatesmartfee` from observed confirmation times. Useful for a wallet but lower
priority on a low-fee PoS chain; large to do well.

### 7. Compact block relay — BIP152 (large)
Reduces block propagation bandwidth via short-ids. Wire-additive and non-consensus, but
a large protocol addition; lower priority than IBD wins for a small network.

### 8. SQLite wallet / descriptor wallets (large)
Core's modern wallet backend. Large; only worth it after (2).

### 9. `regtest` mode (medium, test infra)
A local instant-mine regression network makes everything above testable deterministically
without the live chain. Worth doing early as *test infrastructure* for the rest — though
on a PoS chain "regtest" needs a stake-friendly variant.

## Explicitly out of scope (consensus / would fork the chain)
SegWit, Taproot, bech32/bech32m addresses, RBF, `assumevalid` validity-skipping, any
change to the scrypt block hash (see `drop-scrypt-hardfork.md` — that one *is*
a fork, documented as a proposal), the PoS kernel, the stake modifier, or the money
supply / reward schedule.
