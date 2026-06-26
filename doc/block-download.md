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
4. **Height-relative window.** We only fetch blocks within `BLOCK_DOWNLOAD_WINDOW` (1024)
   heights of the tip, so the look-ahead can't outrun the connect rate. This bounds the
   reorder buffer by *height distance* (so a still-needed block is never evicted from
   `mapOrphanBlocks` → no stalls) while always leaving the wedge (tip+1) fetchable.
   The per-peer window is large (128) because PoS blocks are tiny — a Bitcoin-style 16
   barely uses the link, especially with one serving peer.
5. **Reorder buffer.** Blocks that arrive before their parent connects wait in
   `mapOrphanBlocks` and connect in order — normal for parallel download, *not* an error
   (logged only at `-debug=net` during IBD). Capped by `MAX_ORPHAN_BLOCKS` as a DoS
   backstop, but the height window keeps it well below that.

## Telemetry: the `sync:` line (`-debugsync`, on by default while syncing)

```
sync: height=618614 inflight=16 peers=1 queued=1536 orphans=28
```

| Field | Meaning | Healthy |
|---|---|---|
| `height` | current connected tip | climbing steadily |
| `inflight` | blocks requested, not yet received | `≈ 128 × peers` (capped at the height window) |
| `peers` | **distinct peers actually serving blocks right now** | as many as you have |
| `queued` | learned-from-headers, not yet requested | a few hundred to ~2000 |
| `orphans` | reorder buffer (out-of-order, waiting to connect) | low (tens), well under the cap |

**The single most important number is `peers`.** `inflight ≈ 128 × peers` (up to the
height window), so download throughput is bounded by how many peers actually serve you.

## Key finding: download is bounded by *serving* peers, not connection count

`getconnectioncount` can say 10 while `peers=1`. A connection only serves blocks if it
completed the handshake and has the history (`getpeerinfo` → `startingheight` ≥ your
target, `bytesrecv` growing). On the thin HoboNickels network many connections are
half-open (`startingheight=-1`, `bytesrecv=0`) or don't serve old ranges, so at any moment
only 1–N of them feed you. Observed: `peers` swings between 1 and several on the same node
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
| **Per-peer window 16 → 64**, raw-count reorder gate | filled the reorder buffer faster than it drains; the gate then blocked the *wedge* block → **tip stalled** (e.g. stuck at 2013, orphans 281 > gate 256, inflight draining) | superseded by the height window below |
| `peers=` field in `sync:` | made the single-peer-bound condition visible | **kept** |
| **Height-relative download window** (W=1024) + **per-peer window 16 → 128** + height-targeted stall-breaker + active-peer-aware reassignment | only fetch within `[tip+1, tip+W]` (wedge always fetchable, buffer bounded by height so it never evicts); big window fills a single peer's pipe; don't reassign a block from a peer that's still delivering (kills the residual dupes) | **shipped.** Measured: **~90–170 blk/s vs ~19 with window 16 (≈5–9×)**; `inflight` reaches `128 × serving-peers`; with stable peers `dupes=0`, `orphans=0`; during a peer-scarcity stall the buffer rises (bounded, ≤W, no eviction) and recovers, with some transient dupes. No permanent stalls across many runs. |

## Open work (in priority order)

1. **More serving peers** — better peer discovery / higher outbound count so more
   connections actually feed blocks (addresses the root `peers≈1` cause; the client
   already targets 16 outbound, so this is mostly network-side).
2. **Cut the transient stall-time dupes** — during a peer-scarcity stall the 30 s timeout
   reassigns a quiet peer's blocks; if it then resumes, those arrive as duplicates
   (`dupes` rose to ~300 in a stally run, 0 in smooth runs). Reassigning only the
   tip-critical blocks from a quiet peer (not its whole far-ahead backlog) would trim it.
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
