# Mid-IBD sync stall + rescan-every-restart — root cause and fix (v2.3.1-modern)

## Symptom (reported from a live mainnet node)

A node syncing the chain (initial block download) would, after hours/days, **stop
advancing**: `SetBestChain` quit logging new heights, no error appeared in
`debug.log`, the process kept running but the tip sat still for hours. Restarting
the client always un-stuck it — it would **rescan**, then resume syncing from where
it left off — until it eventually stalled again. RAM sat around ~0.5 GB.

Two distinct issues were tangled together here.

## Issue 1 — the wedge (this is the real bug; it is ours)

During headers-first IBD, headers were re-requested in only two places:

1. once, when a sync peer is first chosen (`SendMessages`, the `fStartSync` path);
2. a **rolling look-ahead** in the `"block"` message handler, which fires *only when
   a block arrives* and *only* once the tip has advanced 1000 past the last request
   (`main.cpp`, the `g_nLastHeadersRequestHeight + 1000` check).

There was **no timer- or queue-based header re-request**, and a new sync peer is
only selected when the current one *disconnects* (`net.cpp`: `if (!fHaveSyncNode)
StartSync(...)`; `pnodeSync` is cleared only in `~CNode`).

So if the single sync peer went **silent but stayed connected** (common on an
under-seeded network) — or simply returned a short headers batch — headers stopped,
the block-download queue drained, no blocks arrived, the look-ahead never re-armed,
and the peer was never rotated. The tip wedged until a manual restart (which clears
`pnodeSync` and issues a fresh `getheaders`). The original `getheaders` could not
even be retried at the same tip because `PushGetHeaders` de-duplicates an unchanged
range. The existing stall-breaker only re-requests *blocks already queued/in-flight*,
so it cannot help when the problem is *header starvation* (nothing queued at all).

The underlying single-sync-peer stall is an old upstream weakness that Bitcoin Core
later fixed with stall timeouts that disconnect an unresponsive sync peer. Our
headers-first rewrite made the look-ahead the sole header pump and did not port that
fix — so this wedge is ours to fix.

### Fix

- **Header-starvation watchdog** (`main.cpp`, `SendMessages`): when in IBD with a
  completely empty pipeline (`g_vBlocksToDownload` and `g_mapBlocksInFlight` both
  empty), force a fresh `getheaders` (rate-limited to every `SYNC_HEADERS_RETRY` =
  20 s). This is the case nothing previously recovered from.
- **`PushGetHeaders(..., fForce)`** (`net.cpp`/`net.h`): a force flag bypasses the
  unchanged-tip de-dup, so a stuck tip can re-ask. Used by the watchdog and the
  sync-kickoff.
- **Sync-peer rotation watchdog** (`net.cpp`, `ThreadMessageHandler2`): track tip
  progress; if the tip hasn't advanced for `SYNC_PEER_STALL_TIMEOUT` = 90 s while a
  sync peer is assigned during IBD, drop that peer so `StartSync` selects another and
  issues a fresh `getheaders`. The timer resets on any tip advance or peer change, so
  only a genuinely silent peer is ever dropped. The 20 s re-ask tries to recover the
  peer first; the 90 s rotation is the backstop.

## Issue 2 — rescan on every restart (this one is inherited upstream)

The wallet's sync position (`bestblock` locator) was written **only when not in
IBD** (`CBlock::SetBestChain`, the `if (!fIsInitialDownload)` gate — original
Novacoin/PPCoin-era code, untouched by the modernization). During a long IBD the
recorded position never advanced, so every restart rescanned the wallet from its old
position (effectively genesis) up to the tip. Harmless (rescan only re-reads blocks
already on disk; it never deletes coins/history), but slow and alarming.

### Fix

`CBlock::SetBestChain` now also persists the locator **periodically during IBD**
(every `WALLET_LOCATOR_WRITE_INTERVAL` = 300 s — one cheap walletdb write), so a
restart only rescans a short recent window. This is the same fix newer Bitcoin Core
adopted.

## Issue 3 — snapshot apply hardening (latent; cannot affect a synced node)

Two real defects in the (fresh-node-only) snapshot apply path, fixed while here:

- **Verify before delete** (`ApplySnapshotIfPresent`): the existing chainstate was
  deleted *before* the snapshot's integrity was checked, so a corrupt/truncated
  snapshot could leave the node with no chainstate. The manifest parse + per-file
  sha256 check now run **before** anything is deleted.
- **Rejected snapshot no longer silently reloaded** (`init.cpp`): a snapshot that
  failed post-load checkpoint verification used to delete only its staging dir,
  leaving the wrong-chain chainstate in the datadir — which the *next* startup loaded
  unverified (no manifest → apply skipped → verification skipped). Rejection now
  writes a `snapshot-rejected` marker; the next startup wipes that chainstate
  **before** anything opens it, forcing a clean resync. (We cannot wipe it at
  rejection time — LevelDB has `txleveldb` open then.)

## Issue 4 — minor leak

`g_mapPeerLastBlockTime` grew one entry per NodeId ever seen (never pruned, because
`FinalizeNode` runs off `cs_main`). It is now bounded: records older than
`PEER_BLOCKTIME_MAX_AGE` = 1800 s are dropped in the existing under-`cs_main` prune.
This was not the cause of the ~0.5 GB RSS (that is mostly `-dbcache=256` plus the
in-memory block index); lower `-dbcache` to reduce memory.

## Issue 5 — memory footprint

Reported ~0.5 GB RSS during sync. Breakdown: the LevelDB caches dominated the
*controllable* part — `block_cache` fills toward `-dbcache` (was 256 MB) and
`write_buffer_size` was capped at 64 MB (LevelDB keeps two live memtables, so ~128 MB
resident) — together ~384 MB. The rest is the in-memory `mapBlockIndex` (one entry per
block; architectural, grows with height) plus the orphan buffer.

Since sync here is **wait-bound (peer/pipeline latency), not disk-bound**, a large read
cache buys little real sync speed. So (`txdb-leveldb.cpp`):

- `-dbcache` default lowered **256 → 128 MB** (halves the block cache).
- `write_buffer_size` cap lowered **64 → 32 MB** (bounds memtable RAM to ~64 MB).

Combined, ~190 MB less LevelDB RAM (~0.5 GB → ~0.3 GB), with negligible sync impact on
a peer-bound network. `-dbcache=256` restores the old behaviour on RAM-rich boxes.
`mapBlockIndex` growth toward full height is inherent to this codebase and would need a
structural change (out of scope for a patch release).

## Comparison with modern Peercoin (Bitcoin Core 25.2)

Peercoin (v0.15.1) is rebased on Bitcoin Core 25.2, so it has Core's mature sync/wallet
stack. We compared each fix; all four are **directionally aligned** with Core:

- **Sync-peer stall / rotation** — Core disconnects a stalling peer (dynamic 2→64 s
  backoff) and reassigns across a parallel pool; our 90 s tip-stall disconnect +
  reselect + 10 s redundant re-request of the wedge block is the same spirit, gentler,
  and correctly tuned for a single-sync-peer codebase. Constants are in Core's ballpark.
- **Headers** — Core pairs a cheap re-ask (`MaybeSendGetHeaders`, 2 min time-dedup)
  with a hard `m_headers_sync_timeout` (15 min) drop + `nSyncStarted--` reselect. Our
  20 s forced re-ask + 90 s drop/reselect is a complete mirror (no idle-forever mode).
- **Wallet locator** — Core persists on chainstate-flush events, never per block; our
  300 s timer-during-IBD matches that, and our locator is reorg-safe by construction.
- **Snapshot** — Core validates before adopting and cleans up invalid snapshots; our
  verify-before-delete + reject-marker-wipe + (new) wipe-staging-on-integrity-failure
  match. Trust difference to document: we verify the applied chain *contains* the
  hardcoded checkpoints; we do not full-revalidate between checkpoints (Core's
  background chainstate does), so **the checkpoint set is the security boundary** —
  keep it dense/recent.

## Deferred follow-ups (post-release, not blocking)

- Exponential backoff + cap on `SYNC_PEER_STALL_TIMEOUT` (mirror Core's 2→64 s) to
  protect slow-downlink nodes with few peers from churning good peers.
- Only-sync-peer exemption: extend the window instead of disconnecting when it is the
  sole eligible peer.
- One final `SetBestChain(pindexBest)` on clean shutdown → zero rescan after a graceful
  stop (needs exposing the file-static `SetBestChain` to the shutdown path).
- Replace `PushGetHeaders`' boolean force flag with Core's time-based getheaders dedup.

## Validation status

Compiles clean. **Not yet validated on the live network.** Before tagging/publishing
2.3.1-modern this needs a real-network soak confirming: (a) IBD no longer wedges when
a peer goes silent (watch for the new `sync: ... re-requesting headers` /
`dropping unresponsive sync peer` log lines and continued tip progress), and (b) a
restart mid-IBD rescans only a short window, not from genesis. No consensus rules were
touched (validation, block/tx acceptance, and coinstake rules are unchanged); these
are scheduling/peer-selection/persistence changes only.
