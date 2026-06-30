# Windows SmartScreen & code signing

When a user downloads `HoboNickels-qt.exe` / `HoboNickelsd.exe` from the internet, Windows
attaches a "Mark of the Web" tag and **SmartScreen** shows *"Windows protected your PC —
unknown publisher"* (and, if SmartScreen is set to *Block*, refuses to run it). This happens
to **every unsigned executable**; it is not specific to HoboNickels and there is no code
change that removes it. The only real fixes are below.

## For users (works today, unsigned)

1. **Run it anyway:** at the SmartScreen prompt click **More info → Run anyway**.
2. **Or unblock first:** right-click the file → **Properties** → tick **Unblock** → OK
   (removes the Mark-of-the-Web). PowerShell equivalent: `Unblock-File .\HoboNickelsd.exe`.
3. **Verify integrity** against the release `SHA256SUMS.txt` before trusting a download:
   ```powershell
   Get-FileHash .\HoboNickels-qt-2.3.6-modern-windows-x86_64.zip -Algorithm SHA256
   ```
   and compare to the line in `SHA256SUMS.txt` on the release page.

## The real fix: Authenticode code signing

Signing the `.exe` with a certificate from a public CA makes the publisher name trusted:

- **EV (Extended Validation) certificate** — gets **instant SmartScreen reputation** (no
  warning from the first download). Requires a hardware token; ~$200–600/yr.
- **OV (Organization Validation) / standard certificate** — cheaper (~$100–300/yr), but
  SmartScreen reputation **builds up over downloads/time**, so early users may still see a
  warning until enough installs accrue.
- **Free for open source:** [SignPath.io](https://signpath.io) sponsors free Authenticode
  signing for OSS projects (and there are CA OSS programs). This is the no-cost path.

A self-signed certificate does **not** help — SmartScreen only trusts CA-chained certs.

## CI is already wired to sign — just add the cert

`.github/workflows/ci.yml` signs every Windows `.exe` automatically **if** two repository
secrets are present (otherwise it logs a notice and ships unsigned). To enable:

1. Obtain a code-signing cert as a password-protected `.pfx`.
2. Base64-encode it: `base64 -w0 cert.pfx > cert.b64` (Linux/macOS) or
   `[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))` (PowerShell).
3. In **Settings → Secrets and variables → Actions**, add:
   - `WINDOWS_PFX_BASE64` — the base64 string
   - `WINDOWS_PFX_PASSWORD` — the `.pfx` password
4. Re-run the build / cut a release. The signing step uses `signtool sign /fd SHA256 /tr
   http://timestamp.digicert.com /td SHA256` (RFC-3161 timestamp, so signatures stay valid
   after the cert expires).

No secrets, no behavior change — the build stays green and ships unsigned, exactly as now.
