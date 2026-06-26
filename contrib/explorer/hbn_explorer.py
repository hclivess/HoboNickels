#!/usr/bin/env python3
"""HoboNickels compact block explorer.

A single-file, dependency-free (Python 3 standard library only) block explorer
that talks to a local HoboNickelsd over JSON-RPC and serves a small web UI:
chain summary, recent blocks, block pages, transaction pages, and search.

Usage:
    python3 hbn_explorer.py                       # auto-read RPC creds from the datadir's conf
    python3 hbn_explorer.py --testnet
    python3 hbn_explorer.py --port 8080 --rpcport 7373 --rpcuser U --rpcpassword P
    python3 hbn_explorer.py --datadir /path/to/.HoboNickels

Then open http://127.0.0.1:8080/ in a browser.

The daemon must have the RPC server enabled (server=1, rpcuser/rpcpassword set in
HoboNickels.conf). The explorer is read-only and never sends wallet/spend commands.
"""
import argparse
import base64
import html
import json
import os
import sys
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlparse

# --------------------------------------------------------------------------- #
# Configuration / RPC
# --------------------------------------------------------------------------- #

def default_datadir():
    home = os.path.expanduser("~")
    if sys.platform.startswith("win"):
        return os.path.join(os.environ.get("APPDATA", home), "HoboNickels")
    if sys.platform == "darwin":
        return os.path.join(home, "Library", "Application Support", "HoboNickels")
    return os.path.join(home, ".HoboNickels")


def read_conf(datadir):
    conf = {}
    try:
        with open(os.path.join(datadir, "HoboNickels.conf")) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    conf[k.strip()] = v.strip()
    except OSError:
        pass
    return conf


class RPC:
    def __init__(self, host, port, user, password):
        self.url = "http://%s:%d/" % (host, port)
        self.auth = base64.b64encode(("%s:%s" % (user, password)).encode()).decode()
        self._id = 0

    def call(self, method, *params):
        self._id += 1
        body = json.dumps({"jsonrpc": "1.0", "id": self._id,
                           "method": method, "params": list(params)}).encode()
        req = urllib.request.Request(self.url, data=body, headers={
            "Authorization": "Basic " + self.auth,
            "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                data = json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            try:
                data = json.loads(e.read().decode())
            except Exception:
                raise RuntimeError("RPC HTTP %s" % e.code)
        except urllib.error.URLError as e:
            raise RuntimeError("cannot reach daemon at %s (%s)" % (self.url, e.reason))
        if data.get("error"):
            err = data["error"]
            raise RuntimeError(err.get("message", str(err)) if isinstance(err, dict) else str(err))
        return data["result"]


# --------------------------------------------------------------------------- #
# HTML helpers
# --------------------------------------------------------------------------- #

GOLD = "#c8920f"
CSS = """
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:#1e1f22;color:#e3e3e6;font:14px/1.5 system-ui,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
a{color:%(gold)s;text-decoration:none}a:hover{text-decoration:underline}
header{background:#2b2d31;border-bottom:1px solid #3a3d42;padding:14px 20px;display:flex;align-items:center;gap:16px;flex-wrap:wrap}
header h1{font-size:18px;margin:0}header h1 a{color:#e3e3e6}
.brand{color:%(gold)s}
form.search{margin-left:auto;display:flex;gap:6px}
input[type=text]{background:#1e1f22;border:1px solid #3a3d42;color:#e3e3e6;border-radius:6px;padding:6px 10px;min-width:280px}
button{background:%(gold)s;color:#101010;border:0;border-radius:6px;padding:6px 12px;font-weight:600;cursor:pointer}
main{max-width:1000px;margin:0 auto;padding:20px}
.cards{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:20px}
.card{background:#2b2d31;border:1px solid #3a3d42;border-radius:8px;padding:12px 16px;min-width:150px;flex:1}
.card .k{color:#9aa0a6;font-size:12px;text-transform:uppercase;letter-spacing:.04em}
.card .v{font-size:18px;font-weight:600;margin-top:2px}
table{width:100%%;border-collapse:collapse;background:#2b2d31;border:1px solid #3a3d42;border-radius:8px;overflow:hidden}
th,td{padding:8px 12px;text-align:left;border-bottom:1px solid #2f3136;font-variant-numeric:tabular-nums}
th{color:#9aa0a6;font-size:12px;text-transform:uppercase;letter-spacing:.04em;background:#26282c}
tr:last-child td{border-bottom:0}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12.5px;word-break:break-all}
.pill{display:inline-block;padding:1px 8px;border-radius:10px;font-size:11px;font-weight:600}
.pos{background:#173a2a;color:#46c07f}.pow{background:#3a2f17;color:%(gold)s}
.err{background:#3a1d1d;border:1px solid #5a2a2a;color:#ff8888;padding:12px 16px;border-radius:8px}
h2{font-size:16px;margin:24px 0 10px}
footer{color:#6b6e73;text-align:center;padding:24px;font-size:12px}
.kv td:first-child{color:#9aa0a6;width:200px}
""" % {"gold": GOLD}


def page(title, body, net=""):
    netlabel = (" <span class='pill pow'>%s</span>" % html.escape(net)) if net else ""
    return ("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>%s</title><style>%s</style></head><body>"
            "<header><h1><a href='/'>HBN <span class='brand'>Explorer</span></a>%s</h1>"
            "<form class='search' action='/search'>"
            "<input type='text' name='q' placeholder='block height / hash / txid' autocomplete='off'>"
            "<button>Search</button></form></header><main>%s</main>"
            "<footer>HoboNickels compact explorer &middot; read-only RPC</footer>"
            "</body></html>") % (html.escape(title), CSS, netlabel, body)


def h(x):
    return html.escape(str(x))


def fmt_amount(x):
    try:
        return "%.6f" % float(x)
    except (TypeError, ValueError):
        return h(x)


def fmt_time(ts):
    try:
        return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(int(ts)))
    except (TypeError, ValueError):
        return h(ts)


def flags_pill(flags):
    if flags and "proof-of-stake" in flags:
        return "<span class='pill pos'>PoS</span>"
    return "<span class='pill pow'>PoW</span>"


# --------------------------------------------------------------------------- #
# Pages
# --------------------------------------------------------------------------- #

def card(k, v):
    return "<div class='card'><div class='k'>%s</div><div class='v'>%s</div></div>" % (h(k), v)


def render_home(rpc, recent=20):
    try:
        info = rpc.call("getblockchaininfo")
    except RuntimeError:
        info = {}
    try:
        count = rpc.call("getblockcount")
    except RuntimeError:
        count = info.get("blocks", 0)
    try:
        conns = rpc.call("getconnectioncount")
    except RuntimeError:
        conns = "?"

    cards = (card("Height", h(count)) +
             card("PoW difficulty", h(round(float(info.get("difficulty", 0)), 6))) +
             card("PoS difficulty", h(round(float(info.get("difficulty_pos", 0)), 6))) +
             card("Money supply", fmt_amount(info.get("moneysupply", 0))) +
             card("Connections", h(conns)))

    rows = []
    top = int(count)
    for height in range(top, max(-1, top - recent), -1):
        try:
            blk = rpc.call("getblockbynumber", height)
        except RuntimeError:
            continue
        ntx = len(blk.get("tx", []))
        rows.append(
            "<tr><td><a href='/block/%d'>%d</a></td><td>%s</td>"
            "<td>%s</td><td>%d</td>"
            "<td class='mono'><a href='/block/%s'>%s…</a></td></tr>" % (
                height, height, flags_pill(blk.get("flags", "")),
                fmt_time(blk.get("time")), ntx,
                h(blk.get("hash", "")), h(str(blk.get("hash", ""))[:20])))

    table = ("<h2>Recent blocks</h2><table><tr><th>Height</th><th>Type</th>"
             "<th>Time (UTC)</th><th>Txs</th><th>Hash</th></tr>%s</table>"
             % "".join(rows))
    chain = info.get("chain", "")
    return page("HBN Explorer", "<div class='cards'>%s</div>%s" % (cards, table),
                net="testnet" if chain == "test" else "")


def resolve_block(rpc, ident):
    ident = ident.strip()
    if ident.isdigit():
        return rpc.call("getblockbynumber", int(ident), True)
    return rpc.call("getblock", ident, True)


def render_block(rpc, ident):
    blk = resolve_block(rpc, ident)
    txs = blk.get("tx", [])
    kv = [
        ("Height", h(blk.get("height"))),
        ("Hash", "<span class='mono'>%s</span>" % h(blk.get("hash"))),
        ("Type", flags_pill(blk.get("flags", "")) + " " + h(blk.get("flags", ""))),
        ("Time (UTC)", fmt_time(blk.get("time"))),
        ("Confirmations", h(blk.get("confirmations"))),
        ("Difficulty", h(blk.get("difficulty"))),
        ("Mint", fmt_amount(blk.get("mint", 0))),
        ("Merkle root", "<span class='mono'>%s</span>" % h(blk.get("merkleroot"))),
    ]
    if blk.get("previousblockhash"):
        kv.append(("Previous", "<a class='mono' href='/block/%s'>%s</a>"
                   % (h(blk["previousblockhash"]), h(blk["previousblockhash"]))))
    if blk.get("nextblockhash"):
        kv.append(("Next", "<a class='mono' href='/block/%s'>%s</a>"
                   % (h(blk["nextblockhash"]), h(blk["nextblockhash"]))))
    detail = "<table class='kv'>%s</table>" % "".join(
        "<tr><td>%s</td><td>%s</td></tr>" % (h(k), v) for k, v in kv)

    rows = []
    for t in txs:
        txid = t.get("txid") if isinstance(t, dict) else t
        rows.append("<tr><td class='mono'><a href='/tx/%s'>%s</a></td></tr>"
                    % (h(txid), h(txid)))
    txtable = ("<h2>Transactions (%d)</h2><table><tr><th>TXID</th></tr>%s</table>"
               % (len(txs), "".join(rows)))
    return page("Block %s" % blk.get("height"),
                "<h2>Block %s</h2>%s%s" % (h(blk.get("height")), detail, txtable))


def render_tx(rpc, txid):
    tx = rpc.call("getrawtransaction", txid, 1)
    vins = []
    for vin in tx.get("vin", []):
        if "coinbase" in vin:
            vins.append("<tr><td colspan='2'><span class='pill pow'>coinbase / coinstake</span></td></tr>")
        else:
            vins.append("<tr><td class='mono'><a href='/tx/%s'>%s</a></td>"
                        "<td>vout %s</td></tr>" % (h(vin.get("txid")), h(vin.get("txid")),
                                                   h(vin.get("vout"))))
    vouts = []
    for vout in tx.get("vout", []):
        spk = vout.get("scriptPubKey", {})
        addrs = ", ".join(spk.get("addresses", [])) or h(spk.get("type", ""))
        vouts.append("<tr><td>%s</td><td>%s</td><td class='mono'>%s</td></tr>"
                     % (h(vout.get("n")), fmt_amount(vout.get("value", 0)), h(addrs)))

    meta = []
    if tx.get("blockhash"):
        meta.append(("Block", "<a class='mono' href='/block/%s'>%s</a>"
                     % (h(tx["blockhash"]), h(tx["blockhash"]))))
    if tx.get("confirmations") is not None:
        meta.append(("Confirmations", h(tx.get("confirmations"))))
    if tx.get("time"):
        meta.append(("Time (UTC)", fmt_time(tx.get("time"))))
    metatable = ("<table class='kv'>%s</table>" % "".join(
        "<tr><td>%s</td><td>%s</td></tr>" % (h(k), v) for k, v in meta)) if meta else ""

    body = ("<h2>Transaction</h2><p class='mono'>%s</p>%s"
            "<h2>Inputs</h2><table><tr><th>From TXID</th><th></th></tr>%s</table>"
            "<h2>Outputs</h2><table><tr><th>#</th><th>Value</th><th>Address(es)</th></tr>%s</table>"
            % (h(txid), metatable, "".join(vins), "".join(vouts)))
    return page("Tx %s" % txid[:16], body)


def render_search(rpc, q):
    q = q.strip()
    if not q:
        return None, "/"
    # height -> block ; 64-hex -> try block then tx
    if q.isdigit():
        return None, "/block/%s" % quote(q)
    if len(q) == 64:
        try:
            rpc.call("getblock", q)
            return None, "/block/%s" % quote(q)
        except RuntimeError:
            return None, "/tx/%s" % quote(q)
    return page("Search", "<div class='err'>Could not interpret <b>%s</b> as a height, "
                "block hash, or txid.</div>" % h(q)), None


# --------------------------------------------------------------------------- #
# HTTP server
# --------------------------------------------------------------------------- #

class Handler(BaseHTTPRequestHandler):
    rpc = None  # set in main()

    def _send(self, body, status=200, ctype="text/html; charset=utf-8"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _error_page(self, msg, status=200):
        self._send(page("Error", "<div class='err'>%s</div>" % h(msg)), status)

    def do_GET(self):
        u = urlparse(self.path)
        parts = [p for p in u.path.split("/") if p != ""]
        try:
            if not parts:
                self._send(render_home(self.rpc))
            elif parts[0] == "block" and len(parts) == 2:
                self._send(render_block(self.rpc, parts[1]))
            elif parts[0] == "tx" and len(parts) == 2:
                self._send(render_tx(self.rpc, parts[1]))
            elif parts[0] == "search":
                q = parse_qs(u.query).get("q", [""])[0]
                body, redirect = render_search(self.rpc, q)
                if redirect:
                    self.send_response(302)
                    self.send_header("Location", redirect)
                    self.end_headers()
                else:
                    self._send(body)
            elif parts[0] == "favicon.ico":
                self._send(b"", 204, "image/x-icon")
            else:
                self._error_page("Not found: %s" % self.path, 404)
        except RuntimeError as e:
            self._error_page("RPC error: %s" % e)
        except Exception as e:  # pragma: no cover - defensive
            self._error_page("Internal error: %s" % e, 500)

    def log_message(self, *args):
        pass  # quiet


def main():
    ap = argparse.ArgumentParser(description="HoboNickels compact block explorer")
    ap.add_argument("--port", type=int, default=8080, help="web UI port (default 8080)")
    ap.add_argument("--bind", default="127.0.0.1", help="web UI bind address (default localhost)")
    ap.add_argument("--datadir", default=None, help="HoboNickels data directory (for the conf)")
    ap.add_argument("--testnet", action="store_true", help="use testnet RPC defaults")
    ap.add_argument("--rpchost", default="127.0.0.1")
    ap.add_argument("--rpcport", type=int, default=None)
    ap.add_argument("--rpcuser", default=None)
    ap.add_argument("--rpcpassword", default=None)
    args = ap.parse_args()

    datadir = args.datadir or default_datadir()
    conf = read_conf(datadir)
    testnet = args.testnet or conf.get("testnet") in ("1", "true")
    rpcport = args.rpcport or int(conf.get("rpcport", 7373 if not testnet else 17373))
    rpcuser = args.rpcuser or conf.get("rpcuser")
    rpcpassword = args.rpcpassword or conf.get("rpcpassword")
    if not rpcuser or not rpcpassword:
        sys.exit("error: no RPC credentials. Set rpcuser/rpcpassword in %s, or pass "
                 "--rpcuser/--rpcpassword." % os.path.join(datadir, "HoboNickels.conf"))

    Handler.rpc = RPC(args.rpchost, rpcport, rpcuser, rpcpassword)
    try:
        Handler.rpc.call("getblockcount")
    except RuntimeError as e:
        sys.exit("error: %s\nIs HoboNickelsd running with server=1?" % e)

    srv = ThreadingHTTPServer((args.bind, args.port), Handler)
    print("HBN explorer on http://%s:%d/  (RPC %s:%d, %s)"
          % (args.bind, args.port, args.rpchost, rpcport, "testnet" if testnet else "mainnet"))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
