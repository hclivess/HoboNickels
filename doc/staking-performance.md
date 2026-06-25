# Staking / minting performance notes

How the proof-of-stake minter works and where the time goes, plus the
optimizations applied and the ones intentionally deferred.

## The staking path

`StakeMiner` (src/miner.cpp) loops roughly every `-minersleep` ms (default 500):

1. `CreateNewBlock(pwallet, true)` assembles a full candidate block — selects
   mempool transactions (priority sort + disk reads) and builds the merkle tree.
2. `SignPoSBlock` → `CWallet::CreateCoinStake` (src/wallet.cpp): selects stakeable
   coins, loads each coin's `(txindex, block, stake-modifier)` into the cached
   `mapMeta`, then `ScanForStakeKernelHash` scans a ~60s timestamp window for a
   kernel hash meeting target.
3. The PoS kernel math lives in src/kernel.cpp (`CheckStakeKernelHash`,
   `ComputeNextStakeModifier`, `GetKernelStakeModifier`).

Custom HoboNickels logic on top: **Stake-For-Charity** (donate a % of each stake),
configurable **split/combine thresholds**, and the `mapMeta` coin cache (gated by
`fCoinsDataActual`, which is cleared whenever a block arrives or the wallet's tx
set changes).

## Applied (safe, non-consensus)

These cannot change the kernel hash, the chosen kernel, or the coinstake
contents — worst case they affect only this wallet's own staking, never the
network:

- **Cached stake-modifier selection interval** (kernel.cpp). It depends only on
  `GetModiferInterval()` (a per-network constant) so the 64-section sum is the
  same every call; it was recomputed on every per-coin modifier lookup.
- **Cached staking balance** (wallet.cpp `CreateCoinStake`). `GetBalance()` walked
  the whole wallet every attempt; now cached behind `fCoinsDataActual` (the same
  signal that gates the coin cache, cleared exactly when the balance can change).
- **`EraseStakeForCharity` fix** (walletdb.h): the `s4c2` record erase was
  unreachable (it followed a `return`), so disabling S4C left an orphaned record.

## Deferred (need testnet validation / maintainer sign-off)

Each of these changes observable behaviour or touches consensus-adjacent code, so
they should be validated by diffing produced block/coinstake/kernel hashes against
the current binary on testnet before release:

- **Defer/cache `CreateNewBlock`** until a kernel is actually found. Today a full
  block (mempool walk + disk reads + merkle) is built on every ~500ms iteration
  and discarded when no kernel is found (~always). Highest CPU/IO win; most
  invasive. Safe variant: cache the candidate and rebuild only when `pindexBest`
  changes — must keep the produced block bit-identical.
- **Idle back-off sleep** in `StakeMiner` when `pindexBest` is unchanged (cap well
  under the 60s kernel search window so no timestamps are skipped).
- **Factor the invariant kernel-hash prefix** out of the 60-iteration inner loop
  (kernel.cpp): only `nTimeTx` varies. Must keep the hashed byte layout identical;
  gate behind a hash-equality test.
- **Extract `LoadStakingMetadata`** shared by `CreateCoinStake` and
  `GetStakeWeight` (pure refactor; avoids a second disk-read pass).
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
