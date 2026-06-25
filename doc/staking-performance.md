# Staking / minting performance notes

How the proof-of-stake minter works and where the time goes, plus the
optimizations applied and the ones intentionally deferred.

## The staking path

`StakeMiner` (src/miner.cpp) loops roughly every `-minersleep` ms (default 500):

1. `CreateNewBlock(pwallet, true)` assembles a full candidate block — selects
   mempool transactions (priority sort + disk reads) and builds the merkle tree.
2. `SignPoSBlock` → `CWallet::CreateCoinStake` (src/wallet.cpp): selects stakeable
   coins, loads each coin's `(txindex, block, stake-modifier)` into the cached
   `mapMeta`, then `ScanForStakeKernelHash` scans the timestamp window since the
   last attempt for a kernel hash meeting target.
3. The PoS kernel math lives in src/kernel.cpp (`CheckStakeKernelHash`,
   `ComputeNextStakeModifier`, `GetKernelStakeModifier`).

Custom HoboNickels logic on top: **Stake-For-Charity** (donate a % of each stake),
configurable **split/combine thresholds**, and the `mapMeta` coin cache (gated by
`fCoinsDataActual`, which is cleared whenever a block arrives or the wallet's tx
set changes).

## Where the time actually goes (investigation, issue #13)

Profiling the loop by reading the code paths rather than guessing — the per-second
costs in steady state, on a low-traffic chain like HoboNickels:

- **`CreateNewBlock`'s mempool walk is cheap here.** The per-transaction priority
  sort and input disk reads only happen when the mempool is non-empty. HBN is a
  low-volume chain — the mempool is usually empty — so the "full block built and
  thrown away every 500 ms" cost is mostly the (small) coinbase + header + a
  near-empty merkle tree. This only becomes the bottleneck on a *busy* chain.
- **`ScanForStakeKernelHash` is cheap in steady state.** Although the inner loop is
  bounded at 60 timestamps, `SignPoSBlock` only searches the interval *since the
  last search* (`nSearchTime - nLastCoinStakeSearchTime`). When the loop polls
  several times a second, each scan covers ~1 new second, i.e. `#coins × ~1` kernel
  hashes — not `#coins × 60`. The 60-wide scan only happens after a gap (startup, or
  the 30 s post-kernel pause).
- **The real recurring cost is the per-block `mapMeta` rebuild.** `fCoinsDataActual`
  is cleared on *every* attached block (via `SyncWithWallets → SetCoinsDataActual`),
  so `mapMeta` is rebuilt from scratch each block: for **every** stakeable coin it
  does `ReadTxIndex` (LevelDB) + `block.ReadFromDisk` (a seek+read into the block
  `.dat` file) + `GetKernelStakeModifier` (an index walk). That is **O(#coins) block
  reads from disk per block**. For a staking wallet with many UTXOs this dominates
  the loop, and it is paid again on the very next block even though nothing about
  those buried source blocks changed. The same loop exists twice, in
  `CreateCoinStake` and `GetStakeWeight`.

Conclusion: on this chain the block-build deferral (below) targets a cost that is
near-zero when the mempool is empty. The higher-value, always-on win is to stop
re-reading every coin's source block from disk on every block.

## Applied (safe, non-consensus)

These cannot change the kernel hash, the chosen kernel, or the coinstake
contents — worst case they affect only this wallet's own staking, never the
network:

- **Persistent staking-metadata cache** (wallet.cpp + wallet.h + main.cpp).
  A `mapStakeMetaCache` keyed on a source block's disk position
  `(nFile, nBlockPos)` holds its `(block header, stake-modifier)`. The `mapMeta`
  rebuild now looks each coin's source block up there first and only falls back to
  the disk read + modifier walk on a miss; the cache survives the per-block
  `mapMeta` clears (the whole point), is re-pruned to exactly the current coin set
  on each pass (a `swap` of a freshly-populated map, so spent coins' entries drop),
  and is cleared wholesale on reorg (`DisconnectBlock → SyncWithWallets(fConnect=
  false) → CWallet::ClearStakeMetaCache`). This turns the O(#coins) disk reads per
  block into O(#new-or-changed coins) — in steady state, near zero.
  *Why it is safe:* a buried block's header and its stake modifier are immutable, so
  a cache hit returns byte-identical data to a fresh read → identical `mapMeta` →
  identical kernel hashes, chosen kernel and coinstake → identical blocks. The only
  staleness window would be a reorg deep enough to change a mature coin's modifier,
  which (a) is cleared explicitly on disconnect and (b) could at worst affect only
  this wallet's own block, which `CheckStake` re-validates before broadcast.
- **Cached stake-modifier selection interval** (kernel.cpp). It depends only on
  `GetModiferInterval()` (a per-network constant) so the 64-section sum is the
  same every call; it was recomputed on every per-coin modifier lookup.
- **Cached staking balance** (wallet.cpp `CreateCoinStake`). `GetBalance()` walked
  the whole wallet every attempt; now cached behind `fCoinsDataActual` (the same
  signal that gates the coin cache, cleared exactly when the balance can change).
- **`EraseStakeForCharity` fix** (walletdb.h): the `s4c2` record erase was
  unreachable (it followed a `return`), so disabling S4C left an orphaned record.

> **Validation status.** The metadata cache is CI-validated on all platforms
> (daemon + GUI, Linux + Windows + Docker) and reasoned byte-identical / network-safe
> as above, but it has **not been observed on a live staking node** — a fresh node
> here cannot stake (the loop is peer-gated, the public testnet is effectively dead,
> and there is no `generate` RPC to self-mine spendable coins). It was merged on the
> maintainer's decision; operators running a staker should confirm staking continues
> normally and, for full assurance, diff produced block / coinstake / kernel hashes
> against the previous binary on a real chain.

## Deferring the per-attempt block build (busy-chain win; lower priority here)

A full block (mempool walk + per-input disk reads + merkle) is built on every
~500 ms staking attempt and discarded when no kernel is found (~always). On a busy
chain this is the biggest win; on HBN, with a usually-empty mempool, it is small
(see the investigation above). It carries a hard requirement either way: **a staked
block must always include every eligible mempool transaction** — omitting them is
not acceptable (it would stop transactions confirming network-wide). So any
deferral must produce exactly the block the current code would, just more cheaply.

Two mempool-preserving ways to do it, both needing testnet validation:

1. **Probe, then build.** Run the cheap kernel scan first (read-only, on the
   already-cached `mapMeta`); only when it finds a kernel, build the full block
   the normal way (`CreateNewBlock` + `SignPoSBlock`). The produced block is
   byte-identical to today and the coinstake still claims the block fees. The
   care item is matching the probe's search window to `SignPoSBlock`'s (its
   `nLastCoinStakeSearchTime` would need to be shared) so a stake is never
   missed.
2. **Cache the mempool selection.** Keep building the full block every attempt,
   but cache the expensive selected-transaction set and reuse it while the
   mempool (`nTransactionsUpdated`) and `pindexBest` are unchanged, rebuilding
   only the cheap coinbase/header. Identical blocks, never skips mempool, no
   risk of missed stakes.

Option 2 is the safer first step. Either way, validate by diffing produced
block/coinstake/kernel hashes against the current binary on testnet.

## Deferred (need testnet validation / maintainer sign-off)

Each of these changes observable behaviour or touches consensus-adjacent code, so
they should be validated by diffing produced block/coinstake/kernel hashes against
the current binary on testnet before release:
- **Idle back-off sleep** in `StakeMiner` when `pindexBest` is unchanged (cap well
  under the 60s kernel search window so no timestamps are skipped).
- **Factor the invariant kernel-hash prefix** out of the kernel-hash inner loop
  (kernel.cpp): only `nTimeTx` varies. Must keep the hashed byte layout identical;
  gate behind a hash-equality test.
- **Extract `LoadStakingMetadata`** shared by `CreateCoinStake` and
  `GetStakeWeight` (pure refactor; the metadata cache above now lives in both
  copies of the loop, which is the strongest reason to deduplicate them).
- **S4C semantics**: the exact-equality maturity check
  (`GetDepthInMainChain() == nCoinbaseMaturity+20`) is reorg-fragile, and the
  two-key S4C write is non-atomic. Behavioural changes — review separately.

## Consensus off-limits (do NOT "optimize")

`CheckStakeKernelHash` hash formula and serialization, `ComputeNextStakeModifier`
selection, the `GetKernelStakeModifier` forward walk (result depends on the future
chain — a naive cache can select a different kernel after a reorg), `GetWeight`
feeding `bnCoinDayWeight`, and the chosen kernel coin / coinstake output
construction (split/combine vout layout). Changing any computed value here forks
the chain.

The metadata cache above deliberately caches only the *resolved* modifier of a
*buried* block (immutable, and invalidated on reorg); it does not re-implement or
short-circuit the `GetKernelStakeModifier` walk itself.
