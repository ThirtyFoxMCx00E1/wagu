#!/usr/bin/env python3
"""3.40.1: live "players online" card for the README.

Listens to the MP lobby (the same MQTT topic the website and the game read)
for a few seconds, keeps only servers that sent a FRESH announcement (not a
retained/saved one), and writes:

  out/players.svg   - the live card shown at the top of the README
  out/players.json  - shields.io endpoint badge: "players online"
  out/servers.json  - shields.io endpoint badge: "servers online"
  out/discord.json  - shields.io endpoint badge: Discord online / members
  out/status.json   - raw data (servers, players, time)

Run by .github/workflows/live-status.yml every 10 minutes; the files are
force-pushed to the `live-status` branch so main's history stays clean.
Usage: python3 tools/lobby_status.py [out_dir] [listen_seconds]
"""
import json, os, sys, time, html, datetime

import paho.mqtt.client as mqtt

TOPIC = "thirtyfox/mp5/l/+"
BROKERS = [("broker.hivemq.com", 1883), ("broker.emqx.io", 1883), ("test.mosquitto.org", 1883)]
REG = ["Asia", "Europe", "NA", "Oceania"]
DISCORD_INVITE = "tbsDknk34y"


def listen(host, port, secs):
    live, saved = {}, {}
    ok = {"c": False}

    def on_connect(c, u, flags, rc, props=None):
        if rc == 0:
            ok["c"] = True
            c.subscribe(TOPIC)

    def on_message(c, u, msg):
        tok = msg.topic.rsplit("/", 1)[-1]
        p = msg.payload.decode("utf-8", "replace")
        if not p:                       # host went offline (empty retained msg)
            live.pop(tok, None); saved.pop(tok, None); return
        f = p.split("|")
        if f[0] != "v1" or len(f) < 7:
            return
        def num(x):
            try: return int(x)
            except ValueError: return 0
        s = {"name": "|".join(f[6:])[:40].strip() or "MP server", "port": num(f[1]),
             "players": num(f[2]), "max": num(f[3]), "region": num(f[4])}
        (saved if msg.retain else live)[tok] = s

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="mpstatus%d" % (time.time() * 1000 % 1e9))
    c.on_connect, c.on_message = on_connect, on_message
    c.connect(host, port, 30)
    c.loop_start()
    t0 = time.time()
    while time.time() - t0 < secs:
        time.sleep(0.5)
    c.loop_stop()
    try: c.disconnect()
    except Exception: pass
    if not ok["c"]:
        raise RuntimeError("no CONNACK from %s" % host)
    return live


def is_default(s):
    return s["port"] == 1922 and s["name"].lower() == "mp lan server"


def svg(servers, players, now, prev=None):
    """3.40.2: a statistics-office style chart (white page, title + unit line, two bar panels,
    a note paragraph and the source line) - like the Eurostat trade charts."""
    W = 840
    e = lambda x: html.escape(str(x), quote=True)
    F = 'font-family="Arial,Helvetica,DejaVu Sans,sans-serif"'
    INK, GREY, GRID = "#1f1f1f", "#5c5c5c", "#d9d9d9"
    GOLD, OLIVE, BLUE, LBLUE = "#b09120", "#e0cf7a", "#2644a8", "#a9b8e8"
    rows = servers[:10]
    slots = sum(s["max"] for s in servers)
    prev = prev or {}
    pserv = prev.get("servers") or []
    pmap = {}
    for s in pserv: pmap[(s.get("name"), s.get("port"))] = pmap.get((s.get("name"), s.get("port")), 0) + int(s.get("players", 0))
    def reg(lst):
        r4 = [0, 0, 0, 0]
        for s in lst:
            r = s.get("region", 0); r = r if 0 <= r < 4 else 0
            r4[r] += int(s.get("players", 0))
        return r4
    regp, regq = reg(servers), reg(pserv)
    pwhen = (prev.get("updated") or "")[11:16]
    la = "%s UTC (10 min before)" % pwhen if pwhen else "10 min before"
    lb = "%s UTC (now)" % now.strftime("%H:%M")
    H = 760
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="MP Multiplayer: %d players online on %d servers">' % (W, H, W, H, players, len(servers))]
    o.append('<title>MP Multiplayer: %d players online on %d servers</title>' % (players, len(servers)))
    o.append('<rect width="%d" height="%d" fill="#ffffff" stroke="#cfcfcf"/>' % (W, H))
    o.append('<text x="28" y="40" %s font-size="19" font-weight="700" fill="%s">Players online on MP Multiplayer servers, live lobby, %s</text>' % (F, INK, now.strftime("%d %B %Y")))
    o.append('<text x="28" y="62" %s font-size="13" fill="%s">(number of players; updated %s UTC)</text>' % (F, GREY, now.strftime("%H:%M")))
    dot = "#2e9d43" if servers else "#c9a227"
    o.append('<circle cx="%d" cy="34" r="6" fill="%s"><animate attributeName="opacity" values="1;.35;1" dur="1.6s" repeatCount="indefinite"/></circle>' % (W - 34, dot))

    def panel(y0, h, title, cats, a, b, ca, cb, la, lb):
        x0, x1 = 70, W - 28
        o.append('<text x="%d" y="%d" %s font-size="14" font-weight="700" fill="%s" text-anchor="middle">%s</text>' % ((x0 + x1) // 2, y0, F, INK, e(title)))
        top, base = y0 + 18, y0 + h
        vmax = max([1] + a + b)
        step = 1
        for st in (1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000):
            if vmax / st <= 6: step = st; break
        vtop = ((vmax + step - 1) // step) * step
        v = 0
        while v <= vtop:
            yy = base - (base - top) * v / vtop
            o.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" stroke-width="1"/>' % (x0, yy, x1, yy, GRID))
            o.append('<text x="%d" y="%.1f" %s font-size="11" fill="%s" text-anchor="end">%d</text>' % (x0 - 6, yy + 4, F, GREY, v))
            v += step
        n = max(1, len(cats)); gw = (x1 - x0) / n; bw = min(26, gw * 0.32)
        for i, c in enumerate(cats):
            cx = x0 + gw * (i + 0.5)
            for k, (val, col) in enumerate(((a[i], ca), (b[i], cb))):
                bh = (base - top) * val / vtop
                bx = cx - bw - 1 if k == 0 else cx + 1
                o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"/>' % (bx, base - bh, bw, bh, col))
                if k == 1 or val: o.append('<text x="%.1f" y="%.1f" %s font-size="10.5" fill="%s" text-anchor="middle">%d</text>' % (bx + bw / 2, base - bh - 4, F, INK if k == 1 else GREY, val))
            nm = c if len(c) <= 16 else c[:15] + "\u2026"
            o.append('<text x="%.1f" y="%d" %s font-size="11" fill="%s" text-anchor="end" transform="rotate(-30 %.1f %d)">%s</text>' % (cx + 4, base + 16, F, INK, cx + 4, base + 16, e(nm)))
        o.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="1.2"/>' % (x0, base, x1, base, GREY))
        ly = base + 70
        o.append('<rect x="%d" y="%d" width="12" height="12" fill="%s"/><text x="%d" y="%d" %s font-size="12" fill="%s">%s</text>' % (W // 2 - 190, ly - 10, ca, W // 2 - 173, ly, F, INK, e(la)))
        o.append('<rect x="%d" y="%d" width="12" height="12" fill="%s"/><text x="%d" y="%d" %s font-size="12" fill="%s">%s</text>' % (W // 2 + 20, ly - 10, cb, W // 2 + 37, ly, F, INK, e(lb)))

    if rows:
        panel(98, 200, "Servers", [s["name"] for s in rows], [pmap.get((s["name"], s["port"]), 0) for s in rows], [s["players"] for s in rows], OLIVE, GOLD, la, lb)
    else:
        o.append('<text x="%d" y="200" %s font-size="14" fill="%s" text-anchor="middle">No server is hosting right now. Open GTA SA, MP MENU, Host to start one.</text>' % (W // 2, F, GREY))
    panel(408, 170, "Regions", REG, regq, regp, LBLUE, BLUE, la, lb)
    more = len(servers) - len(rows)
    pp = prev.get("players")
    trend = "" if pp is None else (" (%+d since %s UTC)" % (players - int(pp), pwhen) if players != int(pp) else " (no change since %s UTC)" % pwhen)
    note = ["Note: %d players%s on %d servers with %d player slots%s." % (players, trend, len(servers), slots, "; +%d smaller servers not shown" % more if more > 0 else ""),
            "Every MP server announces itself to the lobby on the HiveMQ broker; this chart is redrawn every 10 minutes.",
            "Join from GTA SA on Android: MP MENU, Join, or host your own server (MP MENU, Host)."]
    y = 688
    for ln in note:
        o.append('<text x="28" y="%d" %s font-size="11.5" fill="%s">%s</text>' % (y, F, GREY, e(ln))); y += 16
    o.append('<text x="28" y="%d" %s font-size="11.5" fill="%s">Source: MP lobby (%s)</text>' % (H - 10, F, GREY, "live"))
    o.append('<text x="%d" y="%d" %s font-size="13" font-weight="700" fill="%s" text-anchor="end">MP <tspan fill="%s">MULTIPLAYER</tspan></text>' % (W - 28, H - 10, F, BLUE, GOLD))
    o.append("</svg>")
    return "\n".join(o)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "out"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20
    os.makedirs(out, exist_ok=True)
    live, used = None, None
    for h, p in BROKERS:
        try:
            live = listen(h, p, secs); used = h
            break
        except Exception as ex:
            print("broker %s failed: %s" % (h, ex), file=sys.stderr)
    if live is None:
        live = {}
    servers = sorted(live.values(), key=lambda s: (not is_default(s), -s["players"], s["name"].lower()))
    players = sum(s["players"] for s in servers)
    now = datetime.datetime.now(datetime.timezone.utc)
    prev = None   # 3.40.2: the last run's status.json (live-status branch) = the "10 min before" bars
    try:
        import urllib.request
        rq = urllib.request.Request("https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/live-status/status.json?t=%d" % time.time(), headers={"User-Agent": "wagu-live-status"})
        prev = json.load(urllib.request.urlopen(rq, timeout=15))
    except Exception as ex:
        print("previous status: %s" % ex, file=sys.stderr)
    open(os.path.join(out, "players.svg"), "w").write(svg(servers, players, now, prev))
    color = "brightgreen" if players else ("yellow" if servers else "lightgrey")
    json.dump({"schemaVersion": 1, "label": "players online", "message": str(players), "color": color, "cacheSeconds": 600},
              open(os.path.join(out, "players.json"), "w"))
    json.dump({"schemaVersion": 1, "label": "servers online", "message": str(len(servers)), "color": "orange" if servers else "lightgrey", "cacheSeconds": 600},
              open(os.path.join(out, "servers.json"), "w"))
    json.dump({"updated": now.isoformat(timespec="seconds"), "broker": used, "players": players,
               "servers": servers}, open(os.path.join(out, "status.json"), "w"), indent=1)
    # Discord members / online (the invite API, no bot / widget needed)
    try:
        import urllib.request
        rq = urllib.request.Request("https://discord.com/api/v10/invites/%s?with_counts=true" % DISCORD_INVITE, headers={"User-Agent": "wagu-live-status"})
        dj = json.load(urllib.request.urlopen(rq, timeout=15))
        msg = "%d online · %d members" % (dj.get("approximate_presence_count", 0), dj.get("approximate_member_count", 0))
    except Exception as ex:
        print("discord: %s" % ex, file=sys.stderr); msg = "join"
    json.dump({"schemaVersion": 1, "label": "discord", "message": msg, "color": "5865F2", "namedLogo": "discord", "logoColor": "white", "cacheSeconds": 600},
              open(os.path.join(out, "discord.json"), "w"))
    print("%d players on %d servers (via %s), discord: %s" % (players, len(servers), used, msg))


if __name__ == "__main__":
    main()
