# Snap-sync — automatic instant sync over P2P

A fresh node downloads a **chain snapshot** (the already-built chainstate) from a peer and
comes up synced in seconds, instead of downloading and re-validating millions of blocks.
Fully automatic, peer-to-peer, no manual files. **No consensus change** and wire-compatible
(the new messages are additive; old peers ignore them, `PROTOCOL_VERSION` is unchanged).

## Why this works

HoboNickels has no UTXO set (it's txindex-based), so a node's "chainstate" is the block
files (`blk*.dat`) plus the LevelDB index (`txleveldb`). Copying those into a fresh datadir
loads the full, already-validated chain instantly — verified empirically: a copied
chainstate comes up at the same tip offline with no re-validation. Snap-sync is just the
automatic P2P delivery of that copy.

**Trust** is anchored by the hardcoded checkpoints. After a snapshot's index loads, every
hardcoded checkpoint at/below the tip must be present in it by hash at its height
(`Checkpoints::VerifyHardenedInChain`); a forged snapshot can't contain them and is
rejected + discarded. So you trust the checkpoints already baked into the client (plus the
serving peer for the un-checkpointed recent tail, which is then synced + validated normally).

## How it works

1. **Serve.** A synced node runs `createsnapshot` (RPC) to write a consistent copy of its
   chainstate to `<datadir>/snapshot/` with a manifest (height, tip, per-file size+sha256).
   It then answers peers: `getsnapshot` → the manifest; `getsnapchunk(file,offset,len)` →
   the bytes (read-only, ≤1 MB, path-traversal-safe).
2. **Fetch.** A fresh node (`-snapsync`, default on) asks **every connected peer** for a
   manifest in parallel and commits to the first that returns a valid one; it then downloads
   that peer's files in chunks, verifies each file's sha256, and stages them to
   `<datadir>/snapshot/` with a `READY` marker. Normal block sync is paused meanwhile so the
   datadir stays fresh. If the chosen peer stalls mid-download it restarts discovery, and if
   **no peer offers a snapshot within ~2 minutes** (the common case on a network where nobody
   is seeding — e.g. all old clients, which ignore the request) it gives up and syncs
   normally. So a node never hangs waiting for a snapshot that isn't there.
3. **Apply.** On completion the node re-executes itself; the clean startup detects the
   staged snapshot, replaces the genesis-only chainstate with it, loads the index, and
   verifies the checkpoints. Then it syncs the (small) tail to the real tip normally.

## Sync modes

| Mode | Default | Trust | Speed |
|---|---|---|---|
| **Snap-sync** (`-snapsync=1`) | yes (fresh node) | hardcoded checkpoints + peer for the tail | instant |
| **Normal sync** (`-snapsync=0`) | — | fully trustless | full validation |

Now that snap-sync is the fast default, **normal sync fully validates** — it re-verifies
every block's signatures above the last hardcoded checkpoint (`-checkpointdepth=0`, the new
default; `-checkpointdepth=N` opts back into "only the recent N" for speed). So users who
want to verify everything themselves just run `-snapsync=0`.

## Operating a snapshot seed

Run `createsnapshot` periodically on a synced node (e.g. after it advances, or via cron)
so it serves a reasonably-recent snapshot. The snapshot is a consistent point-in-time copy;
fetchers get it then sync the tail. The closer the snapshot is to a recent hardcoded
checkpoint, the tighter the trust anchor — add a fresh checkpoint at a recent height when
cutting releases.

## Tested

Validated two-node on localhost: a fresh client with `-connect=<seed>` auto-fetched a
height-9455 snapshot (5 files, hashes verified), re-executed, applied + checkpoint-verified,
then synced the tail to the seed's tip (9472) — fully automatic, no intervention. (The
checkpoint check was vacuous only because the test snapshot is below the first hardcoded
checkpoint; a real high snapshot verifies against them.) **Before trusting on mainnet:**
exercise it with a snapshot above a hardcoded checkpoint, confirm a tampered snapshot is
rejected, and confirm the re-exec on your platform/launcher.
