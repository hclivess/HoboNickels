# Proposal: replace the scrypt block hash with SHA256d (hard fork)

> **Status: proposal / discussion only.** This is *not* implemented and must not be
> without a coordinated network upgrade. It is a hard fork.

## Motivation

HoboNickels' block hash (`CBlock::GetHash`) is **scrypt** (`scrypt_blockhash`,
N=1024, ~128 KB scratchpad) — a *memory-hard* hash designed to be slow. On this
chain it is the single hottest hash: **every block and (with headers-first sync)
every header** is scrypt-hashed. During the initial block download that is millions
of memory-hard hashes, and it is the dominant CPU cost once script checks are skipped
below the checkpoint.

HoboNickels is, in practice, **proof-of-stake only** — the proof-of-work miner is not
even wired up (no `generate`/`setgenerate`), so the *reason* scrypt was chosen (ASIC-
resistant CPU/GPU PoW mining) no longer applies. Replacing scrypt with **SHA256d**
(double SHA-256) for the block hash would make block hashing roughly two orders of
magnitude faster, directly and substantially speeding up sync and lowering CPU use.

## Why this is a hard fork (and not a trivial swap)

The block hash is not cosmetic — it is consensus-critical in three ways:

1. **Block identity & chain linking.** Every block is keyed by its hash and links to
   its parent by hash. Changing the function changes all post-fork block hashes.
2. **Proof-of-work check.** PoW blocks are validated against the target using this
   hash. (Dead in practice on HBN, but still in the rules.)
3. **Stake-modifier entropy — the important one.** `GetStakeEntropyBit` derives a bit
   from `GetHash()` (the block hash), which feeds `ComputeNextStakeModifier`. The
   stake modifier is **consensus** and drives the PoS kernel. So changing the block
   hash changes the stake modifier from the fork height onward — a real consensus
   change to the proof-of-stake path, not just a label swap.

Because old nodes compute scrypt and would reject SHA256d-hashed blocks (and would
compute a different stake modifier), every node must upgrade. That is the definition
of a hard fork.

## Sketch of the change

- Pick an **activation height** `H` well in the future. Below `H`, `GetHash` stays
  scrypt (preserving all historical hashes and the existing chain); at and above `H`,
  `GetHash` is SHA256d.
- `CBlock::GetHash()` becomes height-dependent — but blocks don't know their height
  in isolation, so in practice this is gated on **block time** (like the existing
  `VERSION1_5_SWITCH_TIME` switches) rather than height, or the hash is computed
  against the connecting tip's height.
- Historical blocks keep their scrypt hashes; `mapBlockIndex` ends up with a mix
  (scrypt below the switch, SHA256d above). `hashPrevBlock` of the first post-switch
  block is the scrypt hash of the last pre-switch block — the chain stays linked.
- `GetStakeEntropyBit` and any other `GetHash`-derived consensus value automatically
  follow the new hash above the switch; this must be reviewed block-for-block.

## Tradeoffs

**Pros**
- ~100× faster block/header hashing → much faster IBD and lower steady-state CPU.
- Removes the main remaining sync-CPU bottleneck that the non-fork optimizations
  (headers-first, multi-peer download, hash memoization) can only partially hide.
- Drops a memory-hard function whose original purpose (PoW mining) is unused.

**Cons / risks**
- **Hard fork:** requires a coordinated upgrade by exchanges, pools, explorers, and
  wallet users before `H`. Un-upgraded nodes split onto a dead chain.
- Touches the **stake-modifier** computation — needs careful, well-tested,
  byte-reviewed implementation and a long activation runway.
- Loses scrypt PoW entirely (acceptable only because PoW is already inactive).
- A new switch point is one more piece of permanent consensus complexity.

## Recommendation

Worth doing **only** with real community coordination and a long activation runway,
because the upside (sync speed) is large but the cost (a stake-modifier-touching hard
fork) is real. Until/unless that happens, the **non-fork** path already shipped is the
pragmatic answer: headers-first + multi-peer parallel download (cut the wall-clock),
`GetHash` memoization + connecting-headers skip (hash each block/header once, and
never hash junk), and `-O3` on the scrypt TU. Those don't make scrypt cheap, but they
remove most of the redundant scrypt work without forking the chain.

If the community does want to pursue this, the safe sequence is: implement behind a
far-future time switch on testnet, diff stake modifiers and block acceptance against
the current binary across the switch, and only then schedule mainnet activation.
