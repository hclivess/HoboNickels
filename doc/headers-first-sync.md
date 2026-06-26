# Headers-first block sync

Faster initial block download (IBD) by learning the chain ahead from cheap block
**headers**, then pulling the full blocks through the normal download path — instead
of the legacy `getblocks → inv → getdata` loop that discovers only ~500 blocks per
round-trip.

**On by default** (`-headersfirst=1`); `-headersfirst=0` falls back to the legacy
inv-based driver.

## Why it's safe to deploy

- **Wire-compatible — no protocol change.** It uses the `getheaders`/`headers` and
  `getdata`/`block` messages that already exist; `PROTOCOL_VERSION` (70010) is
  unchanged. The existing/old client **already answers `getheaders`** (server handler
  in `main.cpp`), so a headers-first node syncs fine against today's network. Peers do
  **not** need to upgrade. It's incrementally deployable — upgrade one node and it
  syncs faster immediately; nothing else has to change.
- **Consensus-identical.** Headers only decide *what* to fetch and *in what order*.
  Every block is still fully validated by `ProcessBlock`/`ConnectBlock` (kernel,
  stake modifier, scripts, coinstake) exactly as before. A headers-first node and a
  legacy node converge on the same tip; if they didn't, that's a bug to catch in
  testing, not a rule change.

## The proof-of-stake wrinkle

Bitcoin's headers-first can *validate* from headers alone (the PoW is in the header).
HoboNickels can't: a PoS block's proof lives in the coinstake transaction and the
stake modifier, which are in the **block**, not the header. So this implementation
deliberately does **not** try to validate the chain from headers. Headers build the
**download plan**; validation waits for the block. The win is pipelining the block
download against a known-ahead chain, not skipping any checks.

## How it works (this implementation)

1. **Driver.** When sync starts, the node sends `getheaders` (instead of `getblocks`)
   from its current tip (`SendMessages`, gated by `-headersfirst`).
2. **Headers handler** (`strCommand == "headers"`). For each of the up-to-2000
   returned headers, if we don't already have the block, queue it via the existing
   `AskFor` priority queue — which the normal `getdata`/`block` machinery then
   downloads. Headers are ~80 bytes, so the look-ahead is cheap.
3. **Rolling look-ahead.** As blocks arrive and the tip advances, the `block` handler
   requests the next header batch — throttled to every ~1000 blocks of real progress
   and de-duplicated in `PushGetHeaders`, so it never spins on an unchanged tip.

Net effect vs. legacy: a ~2000-block rolling window fed by cheap headers and filled
by pipelined `getdata`, instead of 500-block batches gated by a full round-trip each.

## Roadmap (next iterations)

- **Multi-peer parallel download** — request the in-flight window across several peers
  at once (the biggest remaining IBD win on a fast CPU). Needs per-block in-flight
  accounting so two peers don't fetch the same block.
- **Full chain-ahead** — a lightweight header index so the look-ahead isn't bounded
  by download progress (request headers far past the current tip).

Both are wire-compatible (still no `PROTOCOL_VERSION` change) and build on this base.

## Testing checklist (before promoting / trusting)

- Sync a fresh node with `-headersfirst=1` against the live network and confirm it
  reaches the **same tip hash** as a `-headersfirst=0` node at the same height.
- Confirm no increase in rejected/orphaned blocks and that reorgs still work.
- Spot-check `getblock`/`getblockhash` agree with a legacy-synced node at several
  heights.
