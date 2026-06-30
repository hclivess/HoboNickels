# Dynamic checkpointing & sync-time signature validation

## TL;DR

Per-input ECDSA/script verification is the dominant *CPU* cost of connecting a
block. We skip it for blocks at or below a **trusted checkpoint** — their hashes
are immutably anchored, so re-running signatures on them is pointless.

- This already existed for the **compiled-in hardcoded checkpoint** (height
  7,990,000 as of the 2.3.x line): blocks below it never re-verify signatures.
- **New in 2.3.5:** the skip horizon now *also* follows the **master-signed
  dynamic sync checkpoint** (`Checkpoints::nSyncCheckpointHeight`), so the
  "assumevalid" point advances automatically with the network between releases
  instead of being frozen at build time.
  - Toggle: `-dyncheckpoint` (default `1`; `0` = fully trustless normal sync).
  - Safety: the most recent `DYNAMIC_CHECKPOINT_VERIFY_WINDOW` (500) blocks are
    always re-verified, so a compromised checkpoint key cannot suppress signature
    checks right up to the tip.

## What this does and does not speed up

Measured reality on the live HBN network (see also `sync-bottleneck-reality`):
the dominant cost of a **cold sync** is **block download / slow-peer stall plus
disk I/O**, *not* ECDSA verification — the bulk of history (genesis → hardcoded
checkpoint) already skips signatures. Dynamic checkpointing therefore:

- **Helps:** reindex/restart reconnects, reorg re-validation, and the catch-up
  "tip gap" between the last release's hardcoded checkpoint and the current tip
  (this gap grows the longer it has been since a release shipped a new
  hardcoded checkpoint — exactly the case dynamic checkpointing removes).
- **Does NOT:** trust-skip blocks you have not downloaded yet — a block's height
  cannot be known before you have the block, so the horizon can only advance to
  checkpoints whose block is already in the index. For a genuinely *instant*
  cold sync, use **snap-sync** (`-snapsync`, default on), which fetches a
  checkpoint-verified chain snapshot instead of replaying history.

## Why it is safe

Skipping signatures is sound only below a trusted anchor. Two independent
anchors gate the horizon:

1. **Hardcoded checkpoint hashes** (compiled in) — `CBlock::CheckBlock` rejects
   any substituted history below them outright.
2. **Master-signed dynamic checkpoint** — `Checkpoints::CheckSync` forces the
   accepted main chain to descend from it.

So every block we `ConnectBlock` at or below the horizon is provably on the
trusted chain. The 500-block always-verify window bounds exposure in the
hypothetical event the checkpoint key is compromised, and `-dyncheckpoint=0`
restores fully trustless normal sync from genesis.

## Where it lives

- `src/main.cpp` — `CBlock::ConnectBlock`: computes `nVerifyFromHeight` and the
  per-block `fScriptChecks` gate (the only place per-input signatures run).
- `src/checkpoints.{h,cpp}` — `nSyncCheckpointHeight` (lock-free atomic),
  published from `WriteSyncCheckpoint`. Read with `memory_order_relaxed` on the
  hot path; a stale-low read only ever causes *extra* (always-safe) verification.
- `src/init.cpp` — `-dyncheckpoint` help text.

## How to measure on your node

To confirm the impact for your own chain state, time a reindex with the horizon
on vs. off and compare block-connect throughput:

```
HoboNickelsd -reindex -dyncheckpoint=1   # dynamic horizon (default)
HoboNickelsd -reindex -dyncheckpoint=0   # fully trustless, re-verifies all above the hardcoded checkpoint
```

TODO: capture before/after block-connect rates on the live node and record them
here, so we stop guessing and have numbers (per the "measure & document"
discipline).
