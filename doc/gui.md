# Qt wallet GUI

The desktop wallet (`HoboNickels-qt`) is the Qt5 client. Build it with
`-DWITH_QT_GUI=ON` (see [build-unix-modern.md](build-unix-modern.md)); CI builds
it for Linux and Windows on every change.

## Look and feel (modernized)

The wallet shipped with the dated 2014-era native Qt theme and no high-DPI
support. The modernization (`src/qt/thememanager.{h,cpp}`, wired up in
`src/qt/bitcoin.cpp`) addresses both without touching any of the Designer `.ui`
layouts:

- **High-DPI scaling** is enabled before the `QApplication` is constructed
  (`Qt::AA_EnableHighDpiScaling` + `AA_UseHighDpiPixmaps`), so the UI is no longer
  tiny on 4K / HiDPI / Retina displays.
- **Fusion style + a modern palette.** The wallet now uses Qt's built-in,
  cross-platform Fusion style with a flat palette and a conservative stylesheet
  (rounded inputs/buttons, a flat toolbar, themed selection and progress bar). The
  accent colour is the **gold** of the HoboNickels coin.
- **Two themes plus an opt-out**, selected with the `-uitheme` option:

  | `-uitheme` | Result |
  | --- | --- |
  | `light` *(default)* | Clean light theme, gold accent. |
  | `dark` | Gunmetal dark theme, gold accent — matches the coin's metal/gold palette. |
  | `native` | Leave the platform style and palette untouched (the pre-modernization look). |

  Set it on the command line (`HoboNickels-qt -uitheme=dark`) or in
  `HoboNickels.conf` (`uitheme=dark`).

- **Theme-safe status labels.** A few dialogs hard-coded `color: black` on status
  labels, which is invisible on a dark background; those now clear their stylesheet
  and inherit the palette text colour, so they read correctly in every theme.

The stylesheet only sets spacing/structure and the accent; foreground/background
colours come from the `QPalette`. That keeps text legible in both themes and
avoids the brittle per-widget restyling that tends to break old `.ui` forms.

### Why a config option rather than an in-app toggle

The theme is applied once at startup (style + palette are application-global), so
switching it live would require re-applying to every open window. A `-uitheme`
option is simple, scriptable, and persists via the config file. An in-app toggle
in the Options dialog (apply-on-restart) is a reasonable future addition.

## RPC console

The debug window's RPC console (`src/qt/rpcconsole.cpp`) dispatches through the
same `tableRPC` as the daemon, so every RPC the daemon supports — including the
endpoints added in the modernization (`getblockheader`, `getblockchaininfo`,
`getmempoolinfo`, `getnetworkinfo`, `getwalletinfo`, `uptime`) — is available in
the console and listed by `help`, with no GUI-side wiring.

## Staking in the GUI

The GUI links the same wallet/consensus core as the daemon, so the staking
performance work (see [staking-performance.md](staking-performance.md)) applies to
the GUI minter transparently — there is no GUI-specific staking code path to
update.
