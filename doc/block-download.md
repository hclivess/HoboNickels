# Block download (headers-first multi-peer IBD) — design, telemetry & tuning log

This documents how initial block download (IBD) actually works in the modern client, how
to read its telemetry, and — importantly — **what has been tried and its measured impact**,
so we don't re-investigate the same things.

## How it works

1. **Headers-first.** The node learns the chain ahead from cheap `getheaders`/`headers`
   (each header ~80 bytes) and queues the block hashes to download. Validation is
   unchanged — every block is still fully checked by `ConnectBlock` when it arrives;
   headers only decide *what* to fetch and *in what order*. Wire-compatible
   (`PROTOCOL_VERSION` unchanged); `-headersfirst=0` restores the legacy inv path.
2. **Shared download queue** (`g_vBlocksToDownload`, chain order) feeds a scheduler in
   `SendMessages` that assigns blocks to peers with per-block accounting
   (`g_mapBlocksInFlight`: hash → peer, request-time), so no block is fetched from two
   peers at once. Each peer gets up to `MAX_BLOCKS_IN_FLIGHT_PER_PEER` blocks.
3. **Recovery / anti-stall.**
   - A request that times out (`BLOCK_DOWNLOAD_TIMEOUT`, 30 s; sooner if the owning peer
     has gone quiet) is re-queued to the front and reassigned.
   - A **stall-breaker**: when the queue drains but the tip is wedged on a slow peer's
     block, the oldest outstanding block is redundantly re-requested from a second peer.
   - Orphan-recovery `getblocks` is rate-limited (see "dupes" below).
4. **Reorder buffer.** Blocks that arrive before their parent connects wait in
   `mapOrphanBlocks` and connect in order — this is normal for parallel download, *not*
   an error. Bounded by `MAX_REORDER_BUFFER` (we stop fetching further ahead) and capped
   by `MAX_ORPHAN_BLOCKS`.

## Telemetry: the `sync:` line (`-debugsync`, on by default while syncing)

```
sync: height=618614 inflight=16 peers=1 queued=1536 orphans=28
```

| Field | Meaning | Healthy |
|---|---|---|
| `height` | current connected tip | climbing steadily |
| `inflight` | blocks requested, not yet received | `≈ 16 × peers` |
| `peers` | **distinct peers actually serving blocks right now** | as many as you have |
| `queued` | learned-from-headers, not yet requested | a few hundred to ~2000 |
| `orphans` | reorder buffer (out-of-order, waiting to connect) | low (tens), well under the cap |

**The single most important number is `peers`.** `inflight = 16 × peers`, so download
throughput is bounded by how many peers actually serve you.

## Key finding: download is bounded by *serving* peers, not connection count

`getconnectioncount` can say 10 while `peers=1`. A connection only serves blocks if it
completed the handshake and has the history (`getpeerinfo` → `startingheight` ≥ your
target, `bytesrecv` growing). On the thin HoboNickels network many connections are
half-open (`startingheight=-1`, `bytesrecv=0`) or don't serve old ranges, so at any moment
only 1–N of them feed you. Observed: `peers` swings between 1 (≈ inflight 16, slow) and 9
(≈ inflight 144, fast) on the same node minutes apart. **If sync feels slow, check
`peers` first** — if it's ~1, you're download-bound by peer availability, not by the
client.

## What's been tried — and its measured impact

| Change | Impact | Status |
|---|---|---|
| Orphan-recovery `getblocks` per-orphan | re-fetched whole ranges → 780 dupes / 5200 blocks (~15%), out-of-order floods | **rate-limited** (≥1 s) → dupes 0 |
| inv → `AskFor` re-requesting scheduled blocks | the real "already have block" source | **deduped** (skip blocks in the scheduler's set) → dupes 0 |
| Loud `ORPHAN BLOCK` log during IBD | thousands of alarming lines for normal buffering | **`-debug=net`-level during IBD** → clean log |
| Reorder-buffer gate (stop fetching ahead at `MAX_REORDER_BUFFER`) + cap raised to 2000 | bounds the buffer so a needed block is never evicted | **kept** — but see the stall below |
| **Per-peer window 16 → 64** | filled the reorder buffer faster than it drains; the gate then blocked the *wedge* block → **tip stalled** (e.g. stuck at 2013, orphans 281 > gate 256, inflight draining) | **REVERTED to 16.** Do not raise the window without the height-window fix below. |
| `peers=` field in `sync:` | made the single-peer-bound condition visible | **kept** |

Measured baseline (window 16, on a fast host, 1–2 serving peers): clean steady sync,
~30–90 blk/s, `dupes=0`, `orphans=0–34`, no stalls.

## Open work (in priority order)

1. **Height-relative download window** (proper fix to safely raise the per-peer window).
   Track each queued block's height and only fetch within `[tip+1, tip+W]` (e.g. W=1024).
   This bounds the reorder buffer by *height distance* instead of raw count, so the wedge
   (tip+1) is always fetchable and the per-peer window can be large (tiny PoS blocks want
   a window far bigger than Bitcoin's 16) without the gate-stall above. This is the lever
   for the single-/few-serving-peer case.
2. **More serving peers** — better peer discovery / higher outbound count so more
   connections actually feed blocks (addresses the root `peers≈1` cause).
3. **UTXO/coins cache** — the validation-side structural gap vs Bitcoin Core. Once
   download is no longer the limit, `ConnectBlock → FetchInputs` (a LevelDB lookup + a
   block-file read per input, serialized under `cs_main`) becomes the wall. An in-memory
   coins view removes those per-input disk reads. Large and consensus-adjacent (must be
   byte-identical) — stage with live diffing. See `bitcoin-core-comparison.md`.

## Rule for this area

Every tuning change here must be **run against the live network and measured** (watch the
`sync:` line: does `height` climb without stalling, do `dupes`/`orphans` stay low?) before
it's committed, and its impact recorded in the table above. Only regression-free,
measured builds get released.
