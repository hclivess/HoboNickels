# Performance notes (sync / validation / memory)

General performance work on the daemon, complementing
[staking-performance.md](staking-performance.md) (which covers the PoS minter).
Everything here is **non-consensus** — it preserves byte-identical block hashes,
addresses, signatures and accept/reject decisions, and the P2P wire format.

## Already in place (earlier modernization)

- libsecp256k1 ECDSA verification with an OpenSSL fallback (the dominant IBD cost).
- Parallel script-check queue (`-par`, auto-detects cores) and a working signature cache.
- Dynamic checkpointing — signature checks skipped below the trusted height (`-checkpointdepth`).
- LevelDB read-cache / write-buffer tuning (`-dbcache`, default 64 MB).
- Staking caches: per-source-block metadata cache, modifier-selection-interval cache,
  staking-balance cache.

## Applied (this round — safe, isolated)

Findings from a full codebase optimization audit; only the clearly-safe, isolated
ones were taken. Each preserves identical behaviour:

- **Memoized `CBlock::GetHash()`** (main.h). `GetHash()` runs the memory-hard scrypt
  (N=1024, ~131 KB scratchpad) and was recomputed ~5–7× per block during IBD
  (ProcessBlock, CheckBlock, AcceptBlock, AddToBlockIndex, SetBestChain, …). It now
  caches the result in a `mutable` field, invalidated in `SetNull()`, on deserialize,
  and at the only header-mutation sites (`UpdateTime`, `SignPoSBlock`, the genesis
  grind). Output is byte-identical — the header is immutable for a received/finalized
  block. **The biggest single CPU win for IBD.**
- **No per-coin `CBlock`/`CTxIndex` copies in the staking scan** (kernel.cpp). The
  kernel loop copied a whole `CBlock` (its tx vector) and `CTxIndex` for every coin on
  every staking poll, yet reads only `block.nTime` / `txindex.pos`. Now const refs.
- **`GetCoinAge` logging guarded by `fDebug`** (main.cpp). The per-input
  `CBigNum::ToString()` (a BigNum→decimal conversion) ran on every coinstake connect
  even when `-debug` was off; now skipped unless logging is on (identical output).
- **`CSignatureCache::Set()` lock fix** (script.cpp). It mutated the cache under a
  *shared* (read) lock — a latent data race; now an exclusive `unique_lock`.
- **LevelDB compression disabled** (txdb-leveldb.cpp). The txindex stores
  incompressible 32-byte hashes, so Snappy only burned CPU per SST read/write.
- **`-checkblocks` help text** corrected (250, was wrongly documented as 2500).

## Deferred — worthwhile but need care / live validation

Real wins the audit surfaced that were **not** taken yet, because they touch
consensus-critical, concurrency-sensitive, or large surfaces and cannot be
runtime-validated in this environment:

- **`ConnectInputs` by const reference** (high value). It currently deep-copies every
  previous transaction per input across the whole IBD validation path. The catch:
  the spent-pointer write-back (`txindex.vSpent[n] = posThisTx` → `mapTestPool`)
  mutates the inputs copy, so a correct const-ref version must replicate the
  `mapTestPool` accumulation exactly. `ConnectInputs` is *the* block-validation
  function — needs block-validation testing before merge.
- **EC point-decompression skip in `CheckSig`** — the libsecp fast path already
  parses the raw pubkey; the OpenSSL `CKey` decode is redundant. Touches the
  signature-verify path, so validate carefully.
- **Open block-file `FILE*` LRU cache** — `FetchInputs` does an `fopen`/`fseek`/
  `fread`/`fclose` per input; an LRU of handles removes the churn. Needs thread-safe
  handle management.
- **Condition-variable wakeup for the message handler** — replaces a fixed 100 ms
  sleep, cutting relay latency. Threading change — design against missed wakeups /
  deadlock.
- **Networking micro-wins** — atomic byte counters, O(1) recv-size, caching immutable
  config (`GetArg`) at startup, pruning `mapAlreadyAskedFor`. Low value; fiddly.

## Big bets (deliberate projects)

- **Headers-first + parallel multi-peer block download.** IBD is single-peer,
  inv-based, 500-block serial round-trips — the dominant wall-clock limiter on a fast
  CPU. Wire-compatible (`getheaders` is already defined), but a substantial
  download-scheduler rewrite. Consensus-adjacent.
- **A compact UTXO/coins cache.** Today every input reads its `CTxIndex` from LevelDB
  *and* re-reads the entire previous transaction off disk to use one output. A coins
  cache (outpoint → value+scriptPubKey+height) removes that. Architectural;
  consensus-adjacent (must reproduce identical accept/reject incl. coinbase/coinstake
  maturity); changes the local DB schema, not the wire format. The single biggest
  structural I/O win — do not rush.
- Smaller structural items: fold the two per-block LevelDB commits into one;
  `mapBlockIndex` → `unordered_map`; skip the redundant second `CheckBlock` for the
  IBD common case; hardware CRC32C in bundled LevelDB.

## Off-limits (would fork the chain)

The kernel hash & its serialization, `ComputeNextStakeModifier` / the
`GetKernelStakeModifier` walk / the chosen kernel coin, `GetCoinAge` /
`GetProofOfStakeReward`, `MoneyRange`/`MAX_MONEY`, `PROTOCOL_VERSION` and the P2P
message format, and address/signature/hashing. `-Ofast`/`-ffast-math` must never be
enabled (non-deterministic FP). Optimizations may speed these paths up only if the
computed bytes stay identical.
