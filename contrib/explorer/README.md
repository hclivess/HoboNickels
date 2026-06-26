# HoboNickels compact explorer

A single-file, dependency-free block explorer for HoboNickels. It talks to a local
`HoboNickelsd` over JSON-RPC and serves a small web UI — chain summary, recent
blocks, block pages, transaction pages, and search. **Python 3 standard library
only** (no `pip install`), so anyone can run it.

It is **read-only**: it only ever calls read RPCs (`getblockchaininfo`,
`getblockbynumber`, `getblock`, `getrawtransaction`, …) and never touches the wallet.

## Requirements

- Python 3.7+
- A running `HoboNickelsd` with the RPC server enabled, i.e. in `HoboNickels.conf`:
  ```
  server=1
  rpcuser=someuser
  rpcpassword=somelongpassword
  ```

## Run

```sh
# auto-reads rpcuser/rpcpassword/rpcport from the default datadir's HoboNickels.conf
python3 hbn_explorer.py

# then open the printed URL, e.g. http://127.0.0.1:8080/
```

Options:

```sh
python3 hbn_explorer.py --testnet                 # use testnet RPC defaults
python3 hbn_explorer.py --port 8080 --bind 0.0.0.0 # serve on all interfaces
python3 hbn_explorer.py --datadir /path/.HoboNickels
python3 hbn_explorer.py --rpchost 127.0.0.1 --rpcport 7373 --rpcuser U --rpcpassword P
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--port` | 8080 | Web UI port |
| `--bind` | 127.0.0.1 | Web UI bind address (use `0.0.0.0` to expose on your LAN) |
| `--datadir` | OS default | Where to read `HoboNickels.conf` |
| `--testnet` | off | Use testnet RPC port default (17373) |
| `--rpchost`/`--rpcport`/`--rpcuser`/`--rpcpassword` | from conf | Override RPC connection |

## Pages

- `/` — height, PoW/PoS difficulty, money supply, connections, and the most recent blocks.
- `/block/<height-or-hash>` — block header + its transaction list.
- `/tx/<txid>` — a transaction's inputs and outputs (with addresses and amounts).
- search box — accepts a block height, block hash, or txid.

## Notes

- Bind to `127.0.0.1` (the default) unless you intend to expose it; there is no auth
  on the web UI itself.
- It relies on the daemon's transaction index (this build keeps a full tx index), so
  `getrawtransaction` works for any transaction without extra configuration.
