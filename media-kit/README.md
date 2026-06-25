# HoboNickels media kit

The original graphics extracted from the wallet, organized so they can be
**modernized / redrawn**. These are *copies*; the live assets the app actually
loads still live under `src/qt/res/` and are wired in through `src/qt/bitcoin.qrc`
(by the **alias** names in the tables below). To swap in a modernized asset, drop
it in place of the matching `src/qt/res/...` file (same alias / same pixel size, or
update the `.ui`/code that sizes it).

## Brand

The HoboNickels mark is a **metallic coin**: silver / gunmetal body, a **gold**
gear-and-ring (the wallet's accent colour — `#c8920f` light, `#e0a82e` dark, set in
`src/qt/thememanager.cpp`), black "HBN", and a binary-digit motif. Keep that palette
when modernizing.

| File | Size | Role |
| --- | --- | --- |
| `brand/HoboNickels.png` | 640×632 | Master raster logo (full detail). |
| `brand/HoboNickels-128.png` | 130×128 | App / window icon (`bitcoin` alias). *Note: not square — fix to 128×128.* |
| `brand/HoboNickels-80/48/32/16.png` | 80/48/32/16 | Icon size ramp (`toolbar` = 16px). 48 is 49×48 — fix to square. |
| `brand/HoboNickels-testnet.png` | 512×512 | Testnet logo variant. |
| `brand/HoboNickels_16-testnet.png` | 16×16 | Testnet toolbar mark. |
| `brand/bitcoin_testnet.png` | 256×256 | Testnet window icon. |
| `app-icons/HoboNickels.ico` | 256×256 | Windows application icon. |
| `app-icons/HoboNickels.icns` | (multi) | macOS application icon. |
| `vector-sources/bitcoin.svg` | 256×256 | **Editable vector of the coin** — the best starting point for a redraw. |

## UI / action icons (`ui-icons/`)

Toolbar and dialog icons. Most are raster-only (no vector source) and **non-uniform
sizes** — the biggest modernization opportunity is a consistent, vector icon set.

| File | Size | Alias | Used for |
| --- | --- | --- | --- |
| `overview.png` | 128×128 | overview | Overview tab |
| `history.png` | 50×28 | history | Transactions tab |
| `send.png` | 50×35 | send | Send tab |
| `receive.png` | 50×48 | receiving_addresses | Receive / addresses tab |
| `address-book.png` | 29×40 | address-book | Address book |
| `add.png` / `edit.png` / `remove.png` | 32×32 | add/edit/remove | Address-book actions |
| `editcopy.png` / `editpaste.png` | 32×32 | editcopy/editpaste | Copy / paste |
| `configure.png` | 16×16 | options | Settings / Options |
| `export.png` / `export2.png` | 32 / 24 | export/export2 | Export table to CSV |
| `import.png` | 24×24 | import | Import |
| `filesave.png` | 32×32 | filesave | Backup wallet |
| `key.png` | 32×32 | key | Encrypt / change passphrase |
| `lock_closed.png` / `lock_open.png` | 32×32 | lock_closed/lock_open | Wallet lock state |
| `qrcode.png` | 64×64 | qrcode | QR-code dialog |
| `debugwindow.png` | 50×43 | debugwindow | Debug / RPC console |
| `inspect.png` | 24×24 | inspect | Coin control / inspect |
| `repair.png` | 24×24 | repair | Wallet repair |
| `Load.png` / `unload_wallet.png` | 24×24 | load_wallet/unload_wallet | Multi-wallet load / unload |
| `blexp.png` | 24×22 | blexp | Block explorer link |
| `info.png` | 16×16 | info | Info |
| `traffic.png` | 16×16 | traffic | Network traffic graph |
| `p2p.png` | 32×32 | p2p | Peers |
| `quit.png` | 32×32 | quit | Exit |
| `tx_mined/tx_input/tx_output/tx_inout.png` | 64×64 | tx_* | Transaction-detail type glyphs |
| `receive_old.png` | 32×32 | — | Legacy/unused receive icon |

## Status icons (`status-icons/`, all 16×16)

Small status-bar / transaction-list glyphs — these must stay 16×16 (or ship @2x
32×32 for HiDPI).

| File(s) | Alias | Meaning |
| --- | --- | --- |
| `connect0_16.png` … `connect4_16.png` | connect_0 … connect_4 | Peer-connection strength bars (0–4) |
| `synced.png` / `notsynced.png` | synced | Chain in / out of sync |
| `staking_on.png` / `staking_off.png` | staking_on/off | Minting active / inactive |
| `clock1.png` … `clock5.png` | transaction_1 … transaction_5 | Tx confirmation progress (1–5) |
| `transaction0.png` | transaction_0 | Unconfirmed transaction |
| `transaction2.png` | transaction_confirmed | Confirmed transaction |
| `transaction_conflicted.png` | transaction_conflicted | Conflicted transaction |

## Splash / marketing (`splash/`)

| File | Size | Role |
| --- | --- | --- |
| `hbn_revolution.png` | 762×744 | **Master coin render** (transparent) — source for the splash, About image, and README logo. |
| `splash2.png` (legacy) | 450×487 | Old startup splash, superseded by the coin render. |
| `about.png` (legacy) | 259×256 | Old About dialog image, superseded. |
| `splash2.jpg` | 96×96 | Small/legacy splash variant. |

The shipping splash (`src/qt/res/images/splash2.png`, 512×500), About image
(`src/qt/res/images/about.png`, 256×250) and the README logo
(`brand/hbn-coin.png`, 256×250) are all scaled from `hbn_revolution.png`.

## Vector sources (`vector-sources/`)

Editable SVGs — the only assets with vector originals. `bitcoin.svg` is the coin;
`clock1–5.svg` + `clock_green.svg` are the confirmation clocks; `inout.svg` and
`questionmark.svg` are tx glyphs. The remaining raster icons have **no vector
source** and would need to be re-created.

## Animations (`animations/`)

`update_spinner.mng` — legacy MNG busy spinner (`update_spinner` alias). MNG is
effectively obsolete; a modern replacement would be an animated SVG/GIF or a Qt
`QMovie`-friendly format.

## Suggested modernization priorities

1. **Logo / app icon** — redraw the coin from `bitcoin.svg`, export a clean square
   ramp (16/32/48/64/128/256/512 + `.ico` + `.icns`), keeping silver+gold+black.
2. **Unified UI icon set** — one coherent vector set (single stroke weight, grid),
   replacing today's mixed 16–128px raster icons; ship SVG or @1x/@2x PNG.
3. **Status glyphs** — crisp 16×16 (+32×32 @2x) for the connection bars, sync,
   staking and confirmation clocks; they read tiny in the status bar.
4. **Splash / about** — a higher-resolution splash (current 450×487) for HiDPI.
5. Fix the **non-square** brand sizes (128/48/80) and the inconsistent icon
   dimensions noted above.
