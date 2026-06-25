# Configuration options

Options can be set in the config file (one per line, no leading dash) or on the
command line (with a leading dash). Config file locations:

- Linux: `~/.HoboNickels/HoboNickels.conf`
- Windows: `%APPDATA%\HoboNickels\HoboNickels.conf`

`HoboNickelsd --help` lists every option. Below are the ones added or changed by
the modernization, plus the most relevant performance/staking/network knobs.

## Performance (new / changed)

| Option | Default | Description |
| --- | --- | --- |
| `-dbcache=<n>` | **64** (was 25) | LevelDB read cache size in MB. Larger = fewer disk reads during the initial block download. |
| `-checkpointdepth=<n>` | **500** | Re-verify signatures only for the most recent `<n>` blocks; trust older history (dynamic checkpoint). `0` = verify every block. The hardcoded checkpoints remain a hard floor and hash-anchor the chain. |
| `-par=<n>` | 0 (auto) | Number of parallel script-verification threads (0 = use all cores). |
| `-maxsigcachesize=<n>` | 50000 | Signature-verification cache size. |

### Choosing `-checkpointdepth`
The default 500 trusts all but the most recent ~500 blocks. The trust uses the
**median** of peers' reported heights, so inflating it requires a majority of
your peers to lie; the hardcoded checkpoints (to height 7,990,000) anchor the
chain below them. For maximum safety at the cost of slower sync, set
`-checkpointdepth=0` to verify every signature.

## Networking

| Option | Description |
| --- | --- |
| `-addnode=<ip>` | Add a peer and keep trying to connect (and keep normal peer discovery). |
| `-connect=<ip>` | Connect **only** to the given node(s) — disables other discovery. |
| `-dnsseed` | Use DNS seeding for peer discovery (default on). The maintained bootstrap nodes (incl. `coins.dognose.net`) are built in. |
| `-port=<n>` | P2P port (default 7372). |
| `-rpcport=<n>` | RPC port (default 7373). |

Example — add the known good peers explicitly (they are also baked in as seeds):
```
addnode=192.183.213.223
addnode=92.232.112.174
addnode=94.23.147.205
addnode=coins.dognose.net
```

## Staking (Stake-For-Charity & coin management)

| Option / RPC | Description |
| --- | --- |
| `-minersleep=<ms>` | Delay between staking attempts (default 500). |
| `-reservebalance=<amt>` | Amount kept un-staked / spendable. |
| `splitthreshold` (RPC) | Split staking outputs above this size. |
| `combinethreshold` (RPC) | Combine staking outputs below this size. |
| Stake-For-Charity (GUI / RPC) | Donate a configurable percentage of each stake to an address, between min/max amounts. |

## Wallet maintenance (keeping wallet.dat small)

A long-running staking `wallet.dat` only ever **grows** on disk: it's a Berkeley DB
file, and BDB never returns freed pages to the OS, so every stake (one permanent
coinstake record) plus rewritten records leave slack that piles up. Historically the
only workaround was to restore an older, smaller backup. **The wallet now compacts
itself automatically**, so this no longer happens:

| Option / RPC | Default | Description |
| --- | --- | --- |
| `-walletcompact` | **1 (on)** | Automatic compaction. The wallet is rewritten to its live records **at load** (reclaiming any slack inherited from older builds) and again **during the session** whenever the file grows past `-walletcompactfactor`× its last-compacted size. This bounds the file forever — it can't accumulate unbounded slack no matter how long it runs or how much it stakes. Set `0` to disable. |
| `-walletcompactfactor` | **2** | Re-compact when the file reaches this multiple of its last-compacted size. |
| `-walletcompactinterval` | **3600** | Seconds between the (cheap) size checks. |
| `compactwallet` (RPC) | — | Compact on demand. Rewrites `wallet.dat` to its live records, crash-safe (the original is replaced atomically), and reports size before/after. |
| `-zapwallettxes` | — | Clears the wallet's transaction list (diagnostic; implies `-rescan`) — now also compacts the file afterwards, so it actually shrinks on disk. |

Compaction reclaims BDB **slack** (the unbounded part). The genuine record growth
(one coinstake per stake) is real data, not slack, and only grows slowly; the
split/combine defaults (above) further reduce the rate. See
[performance.md](performance.md) for the full analysis.

## GUI (Qt wallet)

| Option | Default | Description |
| --- | --- | --- |
| `-uitheme=<theme>` | **light** | Colour theme of the Qt wallet: `light`, `dark`, or `native` (the pre-modernization platform look). High-DPI scaling is always on. See [gui.md](gui.md). |
| `-lang=<xx>` | system | UI language. |

## RPC quick reference (peers / sync)

- `getpeerinfo`, `getconnectioncount` — connection state.
- `getblockcount`, `getblockhash <height>` — chain tip / a block hash (handy for
  generating new hardcoded checkpoints).
- `getstakeweight`, `getmininginfo` — staking status.

## Modern RPC endpoints (added)

Lightweight, read-only endpoints added for parity with modern Bitcoin-style
tooling (explorers, wallets, monitoring). All are cheap (in-memory lookups, no
chain scans) and safe to expose:

| RPC | Returns |
| --- | --- |
| `uptime` | Server uptime in seconds. |
| `getblockheader <hash>` | A block's header fields (height, time, bits, difficulty, merkleroot, prev/next, PoS flags) straight from the block index — no transaction list, no block read from disk. |
| `getblockchaininfo` | Aggregate chain state: chain (main/test), blocks, bestblockhash, PoW/PoS difficulty, money supply, chainwork, initialblockdownload. |
| `getnetworkinfo` | version, subversion, protocolversion, timeoffset, connections, proxy, relayfee, testnet. |
| `getmempoolinfo` | Mempool `size` (tx count) and `bytes` (serialized size). |
| `getwalletinfo` | Active wallet: version, balance/unconfirmed/immature/stake, txcount, keypool, encrypted/locked, paytxfee. |

Deliberately **not** added (computationally heavy or abuse-prone): `getblockstats`,
`scantxoutset`, and `getchaintips` (the last scans the whole block index).
