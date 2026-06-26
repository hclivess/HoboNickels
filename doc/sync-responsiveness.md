# Sync responsiveness — keeping RPC and the GUI live during sync

A fast-syncing node connects blocks back-to-back, and block connection runs under the global
`cs_main` lock. Anything else that needs `cs_main` — every wallet RPC (`getinfo`,
`getbalance`, `listtransactions`, … all take `LOCK2(cs_main, cs_wallet)`) and the Qt wallet's
periodic polls — has to wait for it. When the connect path held `cs_main` for long stretches,
RPC calls hung for **seconds** and the GUI's block counter, progress bar and balance froze and
then jumped. This got *worse* as sync got faster, and worse the longer the wallet ran. Two
unbounded holds were responsible; both are now bounded. **No consensus change** — the chain
connected is byte-for-byte identical (verified), only the *scheduling* of the work changed.

## What held the lock too long

1. **A whole download batch per message-handler pass.** `ProcessMessages` drained a peer's
   *entire* queued backlog in one pass, taking `cs_main` per block with no break. With the
   multi-peer download pipeline a peer can deliver hundreds of blocks at once, so one pass
   monopolised `cs_main` for the whole batch (seconds) before releasing it.

2. **A whole orphan cascade per block (the big one, and why it builds up over time).** During
   fast multi-peer sync, blocks arrive out of order: while the *wedge* (tip+1) is in flight on
   a slow peer, the blocks behind it pile up as buffered "orphans" (observed: ~900–1000). When
   the wedge finally lands, one `ProcessBlock` connected the **entire** buffered run in a single
   `cs_main` hold — a multi-second freeze every time the buffer flushed. This is the lag that
   shows up "after the wallet has been running a while": the longer it runs, the more often a
   big buffer accumulates and flushes.

## The fix

- **Bound each `ProcessMessages` pass** to ~30 ms of wall-clock; whatever's left stays queued
  for the next pass (`main.cpp`). When a pass stops early it flags `g_fMsgMoreWork`, and the
  message-handler thread loops again after a 2 ms yield instead of its full 100 ms sleep
  (`net.cpp`) — so bounding the hold does **not** throttle sync.

- **Bound the orphan cascade and resume it across passes.** `ProcessBufferedBlocks(start, max)`
  connects buffered out-of-order blocks in batches bounded by time (~30 ms) and count (256),
  leaving the rest buffered; `DrainBufferedBlocks()` runs once per message-handler pass to
  finish the remainder, releasing `cs_main` between batches. A long catch-up still drains fast
  (the thread loops promptly while work remains) but never freezes the node (`main.cpp`,
  `net.cpp`).

The GUI already polled with `TRY_LOCK` (it never hard-blocks), so once `cs_main` is freed
~30×/second the block counter, progress bar and balance update smoothly again.

## Measured impact (live mainnet, `getinfo` latency sampled during active block connect)

Same fresh-from-genesis `-snapsync=0` sync, 90 samples each during heavy download:

| build | p50 | p95 | p99 / max | calls >1 s | calls >2 s |
|---|---|---|---|---|---|
| before (released 2.3.0) | 143 ms | 1868 ms | **4076 ms** | 10 / 90 | 4 / 90 |
| + pass cap only | 83 ms | 979 ms | 8146 ms¹ | 4 / 90 | 4 / 90 |
| **+ bounded orphan drain** | 106 ms | **494 ms** | **953 ms** | **0 / 90** | **0 / 90** |

¹ The pass cap alone fixed the common case but not the orphan-cascade tail — a single buffer
flush still froze for 8 s. Bounding the cascade is what removed the multi-second spikes.

**Result:** worst-case RPC latency dropped from **4–8 s to under 1 s**, with zero hangs over a
second. Sync throughput is unchanged (~130–190 blk/s, network-dependent).

## Correctness

The orphan-drain refactor connects the same blocks in the same order, just in bounded batches.
A from-genesis sync of the old and new binaries produced **identical block hashes** at heights
1000 / 5000 / 10000 / 15000. Block acceptance, validation and the resulting chain are unchanged.
