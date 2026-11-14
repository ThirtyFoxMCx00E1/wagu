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
DISCORD_INVITE = "QSdtut3SWV"


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


def svg(servers, players, now, hist=None, daily=None):
    """4.13.22: a SteamDB style "charts for every day" card (Screenshots/images.png): dark panel, the title,
    the zoom buttons, the Players line (blue) and the Servers line (red) on one axis with the labels on the
    right, the peak marked with a grey band + a black tooltip, a navigator strip with the whole history under
    it (the zoomed range highlighted) and the legend at the bottom.
    hist  = the 10-minute readings [{"t", "players", "servers", ...}] (8 days),
    daily = one row per day [{"d": "YYYY-MM-DD", "players": peak, "servers": peak}] (kept ~3 years)."""
    W, H = 760, 416
    e = lambda x: html.escape(str(x), quote=True)
    F = 'font-family="Arial,Helvetica,DejaVu Sans,sans-serif"'
    BG, GRID, TXT, DIM, BLUE, RED = "#1b1e24", "#2c3038", "#c6d4df", "#8b939e", "#2f9be9", "#c8402c"
    def ts(h): return datetime.datetime.fromisoformat(h["t"])
    def num(v):
        v = int(round(v)); s = "{:,}".format(v).replace(",", " ")
        return s
    def short(v):
        if v >= 1e6: return "%gM" % round(v / 1e6, 1)
        if v >= 1e3: return "%gk" % round(v / 1e3, 1)
        return "%g" % v
    hist = sorted(list(hist or []), key=ts)
    pts = [(ts(h), int(h.get("players", 0)), int(h.get("servers", 0))) for h in hist]
    if not pts or pts[-1][0] < now - datetime.timedelta(minutes=1):
        pts.append((now, players, len(servers)))
    span = (pts[-1][0] - pts[0][0]).total_seconds() if len(pts) > 1 else 0
    zooms = [("1d", 1), ("1w", 7), ("1m", 30), ("3m", 91), ("1y", 365), ("all", 0)]
    active = "1d" if span <= 86400 * 1.05 else "1w"
    days = 1 if active == "1d" else 7
    t1 = pts[-1][0]; t0 = t1 - datetime.timedelta(days=days)
    view = [p for p in pts if p[0] >= t0]
    if len(view) < 2 and len(pts) >= 2: view = pts[-2:]
    if len(view) >= 2: t0 = min(t0, view[0][0]) if active != "1d" else view[0][0] if (t1 - view[0][0]).total_seconds() < 86400 else t0
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="MP charts: %d players online on %d servers">' % (W, H, W, H, players, len(servers))]
    o.append('<title>MP charts for every day: %d players online on %d servers</title>' % (players, len(servers)))
    o.append('<rect width="%d" height="%d" rx="6" fill="%s"/>' % (W, H, BG))
    o.append('<text x="20" y="38" %s font-size="25" fill="%s">MP charts for every day</text>' % (F, TXT))
    # zoom buttons
    x = 20
    o.append('<text x="%d" y="70" %s font-size="13" fill="%s">Zoom</text>' % (x, F, TXT)); x += 46
    for lab, _ in zooms:
        w = 30 if len(lab) <= 2 else 32
        if lab == active: o.append('<rect x="%d" y="55" width="%d" height="21" rx="3" fill="#3a4049"/>' % (x - 4, w))
        o.append('<text x="%d" y="70" %s font-size="13" font-weight="700" fill="%s" text-anchor="middle">%s</text>' % (x - 4 + w / 2, F, "#ffffff" if lab == active else "#4ea8ff", lab))
        x += w + 12
    # live dot + download icon (top right, like the image)
    dot = "#2e9d43" if servers else "#c9a227"
    o.append('<circle cx="%d" cy="30" r="5" fill="%s"><animate attributeName="opacity" values="1;.35;1" dur="1.6s" repeatCount="indefinite"/></circle>' % (W - 64, dot))
    o.append('<text x="%d" y="34" %s font-size="11" fill="%s" text-anchor="end">LIVE</text>' % (W - 72, F, DIM))
    o.append('<g fill="%s"><rect x="%d" y="54" width="4" height="10"/><path d="M%d 63 l7 7 l7 -7 z"/><rect x="%d" y="72" width="16" height="3"/></g>' % (DIM, W - 34, W - 39, W - 40))
    # plot
    X0, X1, Y0, Y1 = 28, W - 50, 92, 296
    vmax = max([1] + [p[1] for p in view] + [p[2] for p in view])
    step = 1
    for st in (1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000, 5000000):
        if vmax / st <= 5: step = st; break
    top = ((vmax + step - 1) // step) * step
    if top - vmax < step * 0.2: top += step
    sx = lambda t: X0 + (X1 - X0) * ((t - t0).total_seconds() / max(1.0, (t1 - t0).total_seconds()))
    sy = lambda v: Y1 - (Y1 - Y0) * v / top
    v = 0
    while v <= top:
        yy = sy(v)
        o.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" stroke-width="1"/>' % (X0, yy, X1, yy, GRID))
        o.append('<text x="%d" y="%.1f" %s font-size="11" fill="%s">%s</text>' % (X1 + 8, yy + 4, F, DIM, short(v)))
        v += step
    # x labels: hours for 1d, days for 1w
    tick = datetime.timedelta(hours=4) if active == "1d" else datetime.timedelta(days=1)
    tt = t0.replace(minute=0, second=0, microsecond=0)
    if active != "1d": tt = tt.replace(hour=0)
    while tt <= t1:
        if tt >= t0:
            xx = sx(tt)
            o.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="%s"/>' % (xx, Y1, xx, Y1 + 5, GRID))
            lab = tt.strftime("%H:%M") if active == "1d" else "%d. %s" % (tt.day, tt.strftime("%b"))
            o.append('<text x="%.1f" y="%d" %s font-size="11" fill="%s" text-anchor="middle">%s</text>' % (xx, Y1 + 18, F, DIM, lab))
        tt += tick
    # peak band + tooltip
    pk = max(view, key=lambda p: (p[1], p[0])) if view else None
    if pk:
        px = sx(pk[0])
        o.append('<rect x="%.1f" y="%d" width="22" height="%d" fill="#ffffff" fill-opacity="0.12"/>' % (px - 11, Y0 - 12, Y1 - Y0 + 12))
    def line(idx, col):
        if len(view) < 2: return
        d = " ".join(("M" if i == 0 else "L") + "%.1f %.1f" % (sx(p[0]), sy(p[idx])) for i, p in enumerate(view))
        o.append('<path d="%s" fill="none" stroke="%s" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>' % (d, col))
    line(2, RED); line(1, BLUE)
    if pk:
        px = sx(pk[0])
        o.append('<circle cx="%.1f" cy="%.1f" r="4" fill="%s" stroke="#ffffff" stroke-width="1.5"/>' % (px, sy(pk[1]), BLUE))
        o.append('<path d="M%.1f %.1f l4 4 l-4 4 l-4 -4 z" fill="%s" stroke="#ffffff" stroke-width="1.2"/>' % (px, sy(pk[2]) - 4, RED))
        tw, th = 200, 54
        tx = px + 14 if px + 14 + tw < X1 else px - 14 - tw
        ty = max(Y0 - 8, min(Y1 - th, sy(pk[1]) - th / 2 + 20))
        o.append('<rect x="%.1f" y="%.1f" width="%d" height="%d" rx="2" fill="#0b0c0f" fill-opacity="0.94"/>' % (tx, ty, tw, th))
        o.append('<text x="%.1f" y="%.1f" %s font-size="11" fill="#ffffff">%s</text>' % (tx + 8, ty + 16, F, e(pk[0].strftime("%A, %b %-d, %Y %H:%M UTC"))))
        o.append('<circle cx="%.1f" cy="%.1f" r="3" fill="%s"/><text x="%.1f" y="%.1f" %s font-size="11" fill="#ffffff">Players: <tspan font-weight="700">%s</tspan></text>' % (tx + 11, ty + 29, BLUE, tx + 18, ty + 33, F, num(pk[1])))
        o.append('<circle cx="%.1f" cy="%.1f" r="3" fill="%s"/><text x="%.1f" y="%.1f" %s font-size="11" fill="#ffffff">Servers: <tspan font-weight="700">%s</tspan></text>' % (tx + 11, ty + 44, RED, tx + 18, ty + 48, F, num(pk[2])))
    # navigator: the whole history (one point per day + now)
    NY0, NY1 = 330, 360
    o.append('<rect x="%d" y="%d" width="%d" height="%d" fill="#15171c" stroke="%s"/>' % (X0, NY0, X1 - X0, NY1 - NY0, GRID))
    nav = [(datetime.datetime.fromisoformat(r["d"] + "T12:00:00+00:00"), int(r.get("players", 0))) for r in (daily or [])]
    nav = [n for n in nav if n[0] < t0] + [(p[0], p[1]) for p in pts]
    nav.sort(key=lambda n: n[0])
    if len(nav) >= 2:
        n0, n1 = nav[0][0], nav[-1][0]
        if (n1 - n0).total_seconds() < 86400 * 7: n0 = n1 - datetime.timedelta(days=7)
        nmax = max(1, max(n[1] for n in nav))
        nx = lambda t: X0 + (X1 - X0) * ((t - n0).total_seconds() / max(1.0, (n1 - n0).total_seconds()))
        ny = lambda v: NY1 - 3 - (NY1 - NY0 - 8) * v / nmax
        d = " ".join(("M" if i == 0 else "L") + "%.1f %.1f" % (nx(t), ny(v)) for i, (t, v) in enumerate(nav))
        o.append('<path d="%s" fill="none" stroke="%s" stroke-width="1.2"/>' % (d, BLUE))
        a, b = max(X0, nx(t0)), X1
        o.append('<rect x="%.1f" y="%d" width="%.1f" height="%d" fill="#4ea8ff" fill-opacity="0.18"/>' % (a, NY0, max(4.0, b - a), NY1 - NY0))
        for hx in (a, b):
            o.append('<rect x="%.1f" y="%d" width="7" height="14" rx="2" fill="#3a6f9e" stroke="#9cc7ee" stroke-width="1"/>' % (hx - 3.5, (NY0 + NY1) // 2 - 7))
        # labels: years when the history is long, else months / days
        longspan = (n1 - n0).days > 400
        tt = n0.replace(month=1, day=1, hour=0, minute=0, second=0, microsecond=0) if longspan else n0.replace(hour=0, minute=0, second=0, microsecond=0)
        while tt <= n1:
            if tt >= n0 and nx(tt) < X1 - 30:
                o.append('<text x="%.1f" y="%d" %s font-size="10" fill="%s">%s</text>' % (nx(tt) + 3, NY0 + 12, F, DIM, tt.year if longspan else "%d. %s" % (tt.day, tt.strftime("%b"))))
            tt = tt.replace(year=tt.year + 1) if longspan else tt + datetime.timedelta(days=1 if (n1 - n0).days <= 10 else 7)
    # legend
    ly = 392; lx = W // 2 - 92
    o.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="2"/><text x="%d" y="%d" %s font-size="13" fill="%s">Players</text>' % (lx, ly - 4, lx + 16, ly - 4, BLUE, lx + 22, ly, F, TXT))
    lx += 100
    o.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="2"/><text x="%d" y="%d" %s font-size="13" fill="%s">Servers</text>' % (lx, ly - 4, lx + 16, ly - 4, RED, lx + 22, ly, F, TXT))
    o.append('<text x="%d" y="%d" %s font-size="10" fill="%s" text-anchor="end">now %d players on %d servers - updated %s UTC - mplobby</text>' % (W - 12, H - 8, F, DIM, players, len(servers), now.strftime("%H:%M, %d %b %Y")))
    o.append("</svg>")
    return "\n".join(o)


def roll_daily(daily, hist):
    """4.13.22: one row per UTC day (the peak players / servers of that day), kept ~3 years for the navigator."""
    by = {r["d"]: dict(r) for r in (daily or []) if r.get("d")}
    for h in hist:
        d = h["t"][:10]; r = by.setdefault(d, {"d": d, "players": 0, "servers": 0})
        r["players"] = max(r["players"], int(h.get("players", 0))); r["servers"] = max(r["servers"], int(h.get("servers", 0)))
    return [by[k] for k in sorted(by)][-1100:]


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
    hist, daily = [], []
    try:
        import urllib.request
        rq = urllib.request.Request("https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/live-status/history.json?t=%d" % time.time(), headers={"User-Agent": "wagu-live-status"})
        hj = json.load(urllib.request.urlopen(rq, timeout=15))
        hist, daily = hj.get("readings", []), hj.get("daily", [])
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
                 "slots": sum(s["max"] for s in servers), "top": max([0] + [s["players"] for s in servers]),
                 "s": [[s["name"], s["players"], s["max"]] for s in servers][:60]})
    cut = now - datetime.timedelta(days=8)
    hist = [h for h in hist if datetime.datetime.fromisoformat(h["t"]) >= cut][-1200:]
    daily = roll_daily(daily, hist)
    json.dump({"readings": hist, "daily": daily}, open(os.path.join(out, "history.json"), "w"))
    open(os.path.join(out, "players.svg"), "w").write(svg(servers, players, now, hist, daily))
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
