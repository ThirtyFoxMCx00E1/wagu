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


def svg(servers, players, now, hist=None):
    """3.41: a central-bank research style chart (like the BSP "exports and imports of services"
    figure): grouped bars on the left axis (players, servers, servers with players), lines on the
    right axis (% of slots in use, % of servers with players, % of players on the biggest server),
    a legend, labels on the latest values, then a note paragraph and the source.
    hist = [{"t": iso, "players", "servers", "active", "slots", "top"}] (oldest first, now last)."""
    W, H = 880, 660
    e = lambda x: html.escape(str(x), quote=True)
    F = 'font-family="Arial,Helvetica,DejaVu Sans,sans-serif"'
    INK, GREY, GRID = "#1f1f1f", "#5c5c5c", "#e3e3e3"
    BAR = ("#4472c4", "#e0504a", "#9bbb59")          # blue, red, green (bars, LHS)
    LIN = ("#8064a2", "#4bacc6", "#f79646")          # purple, cyan, orange (lines, RHS)
    hist = list(hist or [])
    # 10-minute slots while the history is short, hourly peaks once there is a day of it
    def ts(h): return datetime.datetime.fromisoformat(h["t"])
    span = (ts(hist[-1]) - ts(hist[0])).total_seconds() if len(hist) > 1 else 0
    hourly = span >= 20 * 3600
    pts = []
    if hourly:
        buck = {}
        for h in hist:
            k = ts(h).replace(minute=0, second=0, microsecond=0)
            if now - k > datetime.timedelta(hours=24): continue
            c = buck.get(k)
            if c is None or h["players"] > c["players"]: buck[k] = h
        for k in sorted(buck): pts.append((k.strftime("%H:00"), buck[k]))
    else:
        for h in hist[-24:]: pts.append((ts(h).strftime("%H:%M"), h))
    n = max(1, len(pts))
    pct = lambda a, b: 100.0 * a / b if b else 0.0
    bars = [[p[1]["players"] for p in pts], [p[1]["servers"] for p in pts], [p[1]["active"] for p in pts]]
    lines = [[pct(p[1]["players"], p[1]["slots"]) for p in pts],
             [pct(p[1]["active"], p[1]["servers"]) for p in pts],
             [pct(p[1].get("top", 0), p[1]["players"]) for p in pts]]
    names = ["Players online (number, LHS)", "Servers online (number, LHS)", "Servers with players (number, LHS)",
             "Player slots in use (% of slots, RHS)", "Servers with players (% of servers, RHS)", "Players on the biggest server (% of players, RHS)"]
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="MP Multiplayer: %d players online on %d servers">' % (W, H, W, H, players, len(servers))]
    o.append('<title>MP Multiplayer: %d players online on %d servers</title>' % (players, len(servers)))
    o.append('<rect width="%d" height="%d" fill="#ffffff" stroke="#cfcfcf"/>' % (W, H))
    o.append('<text x="28" y="36" %s font-size="18" font-weight="700" fill="%s">MP Multiplayer: players and servers online, %s</text>' % (F, INK, "last 24 hours" if hourly else "latest readings"))
    o.append('<text x="28" y="57" %s font-size="12.5" fill="%s">(%s; updated %s UTC, %s)</text>' % (F, GREY, "hourly peaks" if hourly else "every 10 minutes", now.strftime("%H:%M"), now.strftime("%d %B %Y")))
    dot = "#2e9d43" if servers else "#c9a227"
    o.append('<circle cx="%d" cy="31" r="6" fill="%s"><animate attributeName="opacity" values="1;.35;1" dur="1.6s" repeatCount="indefinite"/></circle>' % (W - 34, dot))
    x0, x1, top, base = 62, W - 58, 134, 480
    o.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#bfbfbf"/>' % (x0, top, x1 - x0, base - top))
    def nice(v, ticks=5):
        for st in (1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000):
            if v / st <= ticks: return st, ((int(v) + st - 1) // st) * st or st
        return 10000, ((int(v) + 9999) // 10000) * 10000
    lstep, lmax = nice(max([1] + [v for b in bars for v in b]))
    rmax = max([5.0] + [v for l in lines for v in l]); rstep = 5 if rmax <= 25 else 10 if rmax <= 50 else 20; rmax = min(100, ((int(rmax) + rstep - 1) // rstep) * rstep) or 100
    v = 0
    while v <= lmax:
        yy = base - (base - top) * v / lmax
        o.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s"/>' % (x0, yy, x1, yy, GRID))
        o.append('<text x="%d" y="%.1f" %s font-size="11" fill="%s" text-anchor="end">%d</text>' % (x0 - 6, yy + 4, F, GREY, v)); v += lstep
    v = 0
    while v <= rmax:
        yy = base - (base - top) * v / rmax
        o.append('<text x="%d" y="%.1f" %s font-size="11" fill="%s">%d</text>' % (x1 + 6, yy + 4, F, GREY, v)); v += rstep
    o.append('<text x="18" y="%d" %s font-size="11" fill="%s" transform="rotate(-90 18 %d)" text-anchor="middle">Number</text>' % ((top + base) // 2, F, GREY, (top + base) // 2))
    o.append('<text x="%d" y="%d" %s font-size="11" fill="%s" transform="rotate(90 %d %d)" text-anchor="middle">Percent</text>' % (W - 16, (top + base) // 2, F, GREY, W - 16, (top + base) // 2))
    gw = (x1 - x0) / (n + 0.9); bw = max(2.0, min(14.0, gw * 0.24))
    for i, (lab, _) in enumerate(pts):
        cx = x0 + gw * (i + 0.5)
        for k in range(3):
            val = bars[k][i]; bh = (base - top) * val / lmax
            bx = cx + (k - 1.5) * bw
            o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"/>' % (bx, base - bh, bw - 0.6, bh, BAR[k]))
        o.append('<text x="%.1f" y="%d" %s font-size="10.5" fill="%s" text-anchor="end" transform="rotate(-90 %.1f %d)">%s</text>' % (cx + 4, base + 8, F, INK, cx + 4, base + 8, e(lab)))
    for k in range(3):
        if not pts: break
        xy = [(x0 + gw * (i + 0.5), base - (base - top) * min(rmax, lines[k][i]) / rmax) for i in range(len(pts))]
        o.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="2"/>' % (" ".join("%.1f,%.1f" % p for p in xy), LIN[k]))
        for (px, py) in xy: o.append('<circle cx="%.1f" cy="%.1f" r="2.2" fill="%s"/>' % (px, py, LIN[k]))
    if pts:   # labels on the latest values (in a column right of the last bars, like the BSP chart)
        lxl = x0 + gw * n + 4
        lab = [(base - (base - top) * bars[k][-1] / lmax, "%d" % bars[k][-1], BAR[k]) for k in range(3)] + \
              [(base - (base - top) * min(rmax, lines[k][-1]) / rmax, "%.1f" % lines[k][-1], LIN[k]) for k in range(3)]
        last = top - 2
        for (yy, t, col) in sorted(lab, key=lambda q: q[0]):   # top to bottom, 12 px apart
            yy = max(last + 12, min(base - 2, yy + 4)); last = yy
            o.append('<text x="%.1f" y="%.1f" %s font-size="10.5" font-weight="700" fill="%s">%s</text>' % (lxl, yy, F, col, t))
    # legend (two columns between the subtitle and the plot)
    for k in range(6):
        lx = 70 + (k // 3) * 400; yy = 82 + 16 * (k % 3)
        if k < 3: o.append('<rect x="%d" y="%d" width="16" height="9" fill="%s"/>' % (lx, yy - 8, BAR[k]))
        else: o.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="2.2"/><circle cx="%d" cy="%d" r="2.4" fill="%s"/>' % (lx, yy - 4, lx + 16, yy - 4, LIN[k - 3], lx + 8, yy - 4, LIN[k - 3]))
        o.append('<text x="%d" y="%d" %s font-size="11" fill="%s">%s</text>' % (lx + 22, yy, F, INK, e(names[k])))
    # note paragraph + source
    slots = sum(s["max"] for s in servers)
    tops = ", ".join(["%s %d" % (s["name"] if len(s["name"]) <= 18 else s["name"][:17] + "\u2026", s["players"]) for s in sorted(servers, key=lambda q: -q["players"]) if s["players"] > 0][:5])
    first = hist[0]["t"][:16].replace("T", " ") if hist else now.strftime("%Y-%m-%d %H:%M")
    pp = hist[-2]["players"] if len(hist) > 1 else None
    mins = int(round((ts(hist[-1]) - ts(hist[-2])).total_seconds() / 60)) if len(hist) > 1 else 10
    trend = "" if pp is None else (" (%+d in the last %d minutes)" % (players - pp, mins) if players != pp else " (no change in the last %d minutes)" % mins)
    note = ["Note: right now %d players%s on %d servers with %d player slots; %d servers have players." % (players, trend, len(servers), slots, sum(1 for s in servers if s["players"] > 0)),
            ("Busiest servers: %s." % tops) if tops else "No server has players right now. Open GTA SA, MP MENU, Join, or Host your own server.",
            "Bars use the left axis (number), lines the right axis (percent). %s." % ("Each bar is the peak of that hour" if hourly else "Each bar is one 10-minute reading; hourly peaks start once a day of data is collected"),
            "Every MP server announces itself to the lobby on the HiveMQ broker; the chart is redrawn every 10 minutes (data since %s UTC)." % first]
    y = 562
    for ln in note:
        o.append('<text x="28" y="%d" %s font-size="11.5" fill="%s">%s</text>' % (y, F, GREY, e(ln))); y += 16
    o.append('<text x="28" y="%d" %s font-size="11.5" fill="%s">Source: MP lobby (live), github.com/ThirtyFoxMCx00E1/wagu</text>' % (H - 12, F, GREY))
    o.append('<text x="%d" y="%d" %s font-size="13" font-weight="700" fill="#b09120" text-anchor="end">MULTIPLAYER</text>' % (W - 28, H - 12, F))
    o.append('<text x="%d" y="%d" %s font-size="13" font-weight="700" fill="#2644a8" text-anchor="end">MP</text>' % (W - 124, H - 12, F))
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
    # 3.41: history.json on the live-status branch (one reading per run, 8 days kept) = the time axis
    hist = []
    try:
        import urllib.request
        rq = urllib.request.Request("https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/live-status/history.json?t=%d" % time.time(), headers={"User-Agent": "wagu-live-status"})
        hist = json.load(urllib.request.urlopen(rq, timeout=15)).get("readings", [])
    except Exception as ex:
        print("history: %s" % ex, file=sys.stderr)
    if not hist:
        try:   # first run: start from the last status.json
            rq = urllib.request.Request("https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/live-status/status.json?t=%d" % time.time(), headers={"User-Agent": "wagu-live-status"})
            pv = json.load(urllib.request.urlopen(rq, timeout=15)); ps = pv.get("servers") or []
            hist = [{"t": pv["updated"], "players": int(pv.get("players", 0)), "servers": len(ps), "active": sum(1 for s in ps if s.get("players", 0) > 0),
                     "slots": sum(int(s.get("max", 0)) for s in ps), "top": max([0] + [int(s.get("players", 0)) for s in ps])}]
        except Exception as ex:
            print("previous status: %s" % ex, file=sys.stderr)
    hist.append({"t": now.isoformat(timespec="seconds"), "players": players, "servers": len(servers), "active": sum(1 for s in servers if s["players"] > 0),
                 "slots": sum(s["max"] for s in servers), "top": max([0] + [s["players"] for s in servers])})
    cut = now - datetime.timedelta(days=8)
    hist = [h for h in hist if datetime.datetime.fromisoformat(h["t"]) >= cut][-1200:]
    json.dump({"readings": hist}, open(os.path.join(out, "history.json"), "w"))
    open(os.path.join(out, "players.svg"), "w").write(svg(servers, players, now, hist))
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
