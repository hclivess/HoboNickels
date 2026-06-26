# Note: would dropping the scrypt block hash speed up sync? (No.)

> **Status: analysis. The answer is no — this is here to correct an earlier claim.**
> An earlier draft of this document argued for a hard fork to replace the scrypt block
> hash with SHA256d *on sync-speed grounds*. That argument was wrong and is retracted
> below. A scrypt-removal hard fork is **not** justified by sync performance.

## Why the sync-speed argument was wrong

The premise was "scrypt is the dominant CPU cost of sync." It is not:

- **scrypt is memoized.** `CBlock::GetHash()` caches its result (`main.h`), so each
  block is scrypt-hashed **once** during IBD no matter how many call sites ask for its
  hash. It is ~1 hash per block, not "several" or "millions."
- **scrypt is cheap in aggregate.** A single scrypt(N=1024, r=1, p=1) is well under a
  millisecond. Across an entire chain that totals on the order of **minutes** of CPU —
  a negligible slice of an hours-long initial sync.
- **A syncing node is not CPU-bound.** Observed CPU during IBD is on the order of **~7%
  of one core** — i.e. the node is ~93% *idle*, blocked on waits. A pure-CPU workload
  (like scrypt) categorically cannot be the bottleneck of a node that is mostly idle;
  if it were, you'd see a pegged core. Removing scrypt would reclaim those few minutes
  and **not move the IBD wall-clock**.

The only true statement in the original framing is a near-tautology: *below* the
checkpoint, where ECDSA script verification is skipped (`main.cpp`, `fScriptChecks`
gate), scrypt is the largest slice of the *residual* CPU — but that residual is ~7% of
a core, so being its biggest slice is irrelevant to wall-clock.

## Where IBD time actually goes

Sync is **wait-bound**, not compute-bound. Two waits dominate:

1. **Network round-trips / download serialization.** The legacy `getblocks → inv →
   getdata` driver pulls ~500 blocks from a *single* peer and only requests the next
   batch after the previous one is delivered, so the node idles a full round-trip every
   500 blocks. This is what headers-first + multi-peer parallel download (default on as
   of 2.0.11) target.
2. **Synchronous random-read disk I/O during validation.** `ConnectBlock → FetchInputs`
   does, per input, a random `ReadTxIndex` LevelDB lookup **plus** a `ReadFromDisk` into
   an old `blkNNNN.dat` (a fresh `fopen`/seek/read with no descriptor cache), plus an
   extra per-tx `ReadTxIndex` for the double-spend check. On a spinning disk that's
   dozens of random seeks per block, serialized under `cs_main` on one thread. `-dbcache`
   helps the LevelDB half only; the block-file seeks are untouched. This is the residual
   bottleneck after the download fixes.

Neither is scrypt. See `bitcoin-core-comparison.md` and `headers-first-sync.md` for the
work that targets these.

## What about dropping scrypt for *other* reasons?

If someone wants to drop scrypt for non-performance reasons (it's a memory-hard PoW hash
with no active mining on this PoS-only chain), note that it is **not** a cosmetic swap —
it is a genuine consensus change and a hard fork:

- **Block identity & chain linking** change for every post-fork block.
- **Stake-modifier entropy.** `GetStakeEntropyBit` derives a bit from `GetHash()`, which
  feeds `ComputeNextStakeModifier`. The stake modifier is consensus and drives the PoS
  kernel, so changing the block hash changes the modifier from the fork height onward.
- It would require a coordinated upgrade (activation height/time) by every node,
  exchange, pool, and explorer, with a long runway and split risk.

That is a lot of consensus risk for **zero** sync-speed benefit. **Recommendation: do
not pursue it as a performance measure.** Redirect that effort to the I/O and pipeline
fixes, which are non-consensus and actually move the wall-clock.
