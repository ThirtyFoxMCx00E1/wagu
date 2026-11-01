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
    """3.41: a Eurostat style figure (like "Principal extra-EU trade partners for transport services,
    EU, 2014 and 2024"): two stacked panels with grouped vertical bars, two periods each (light = peak
    of the last 24 hours, dark = now). Top panel (gold): players per server. Bottom panel (blue):
    player slots in use per server (%). The 9 biggest servers, then "Other servers". A legend under
    each panel, notes, the source line and the logo at the bottom right.
    hist = [{"t", "players", "servers", "active", "slots", "top", "s": [[name, players, max], ...]}]."""
    W, H = 760, 800
    e = lambda x: html.escape(str(x), quote=True)
    F = 'font-family="Arial,Helvetica,DejaVu Sans,sans-serif"'
    INK, GREY, GRID, AX = "#000000", "#404040", "#d9d9d9", "#7f7f7f"
    GOLD = ("#ecdc9c", "#b0901e")      # light (peak 24 h), dark (now)  - like Exports 2014 / 2024
    BLUE = ("#6f8be0", "#2a3fa8")      # light (peak 24 h), dark (now)  - like Imports 2014 / 2024
    hist = list(hist or [])
    def ts(h): return datetime.datetime.fromisoformat(h["t"])
    day = [h for h in hist if now - ts(h) <= datetime.timedelta(hours=24) and h.get("s")]
    # per server: peak players / peak % of slots over the last 24 hours, and now
    peak, peakp, mx = {}, {}, {}
    for h in day:
        for nm, pl, m in h["s"]:
            peak[nm] = max(peak.get(nm, 0), pl); mx[nm] = m or mx.get(nm, 0)
            peakp[nm] = max(peakp.get(nm, 0.0), 100.0 * pl / m if m else 0.0)
    cur = {}
    for s in servers:
        cur[s["name"]] = s["players"]; mx[s["name"]] = s["max"]
        peak[s["name"]] = max(peak.get(s["name"], 0), s["players"])
        peakp[s["name"]] = max(peakp.get(s["name"], 0.0), 100.0 * s["players"] / s["max"] if s["max"] else 0.0)
    order = sorted(peak, key=lambda k: (-peak[k], -cur.get(k, 0), k.lower()))
    main_, rest = order[:9], order[9:]
    pct = lambda a, b: 100.0 * a / b if b else 0.0
    cats = [(k, peak[k], cur.get(k, 0), peakp.get(k, 0.0), pct(cur.get(k, 0), mx.get(k, 0))) for k in main_]
    if rest or not cats:
        rp = sum(peak[k] for k in rest); rn = sum(cur.get(k, 0) for k in rest); rm = sum(mx.get(k, 0) for k in rest)
        cats.append(("Other servers", rp, rn, pct(rp, rm), pct(rn, rm)))
    hourly = len(day) > 1
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="MP Multiplayer: %d players online on %d servers">' % (W, H, W, H, players, len(servers))]
    o.append('<title>MP Multiplayer: %d players online on %d servers</title>' % (players, len(servers)))
    o.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    o.append('<text x="12" y="28" %s font-size="19" font-weight="700" fill="%s">Principal MP servers by players, last 24 hours and now</text>' % (F, INK))
    o.append('<text x="12" y="50" %s font-size="15" fill="%s">(number of players and %% of player slots; updated %s UTC, %s)</text>' % (F, INK, now.strftime("%H:%M"), now.strftime("%d %B %Y")))
    dot = "#2e9d43" if servers else "#c9a227"
    o.append('<circle cx="%d" cy="22" r="6" fill="%s"><animate attributeName="opacity" values="1;.35;1" dur="1.6s" repeatCount="indefinite"/></circle>' % (W - 20, dot))
    def label_lines(t):
        if len(t) > 26: t = t[:25] + "\u2026"
        if len(t) <= 11: return [t]
        w = t.split(" "); a, b = "", ""
        for x in w:
            if not b and len((a + " " + x).strip()) <= 12: a = (a + " " + x).strip()
            else: b = (b + " " + x).strip()
        if not a: a, b = t[:11], t[11:]
        if len(b) > 13: b = b[:12] + "\u2026"
        return [a, b] if b else [a]
    def panel(title, y0, vals, cols, fmt, unit_max):
        x0, x1, top, base = 46, W - 14, y0 + 32, y0 + 222
        o.append('<text x="%d" y="%d" %s font-size="14" font-weight="700" fill="%s" text-anchor="middle">%s</text>' % ((x0 + x1) // 2, y0 + 14, F, INK, e(title)))
        vmax = max([0.0] + [v for pair in vals for v in pair])
        if unit_max: vmax = max(vmax, 1.0)
        step = 1
        for st in (1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000):
            if vmax / st <= 8: step = st; break
        top_v = max(step, ((int(vmax) + step - 1) // step) * step)
        if top_v - vmax < step * 0.25: top_v += step   # a little headroom above the tallest bar
        v = 0
        while v <= top_v:
            yy = base - (base - top) * v / top_v
            o.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" stroke-width="1"/>' % (x0, yy, x1, yy, GRID if v else AX))
            o.append('<text x="%d" y="%.1f" %s font-size="12" fill="%s" text-anchor="end">%d</text>' % (x0 - 6, yy + 4, F, INK, v))
            v += step
        n = max(1, len(cats)); gw = (x1 - x0) / n; bw = min(20.0, gw * 0.24)
        for i, c in enumerate(cats):
            cx = x0 + gw * (i + 0.5)
            for k in range(2):
                val = vals[i][k]; bh = (base - top) * val / top_v
                bx = cx - bw - 3 + k * (bw + 6)
                if bh > 0: o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"><title>%s: %s</title></rect>' % (bx, base - bh, bw, bh, cols[k], e(c[0]), fmt(val)))
            o.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="%s"/>' % (x0 + gw * i, base, x0 + gw * i, base + 5, AX))
            for j, ln in enumerate(label_lines(c[0])):
                o.append('<text x="%.1f" y="%d" %s font-size="12" fill="%s" text-anchor="middle">%s</text>' % (cx, base + 18 + 14 * j, F, INK, e(ln)))
        o.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s"/>' % (x1, base, x1, base + 5, AX))
        ly = base + 58; lx = (x0 + x1) // 2 - 92
        for k, nm in enumerate(("Peak, last 24 h", "Now")):
            o.append('<rect x="%d" y="%d" width="9" height="9" fill="%s"/>' % (lx + k * 118, ly - 9, cols[k]))
            o.append('<text x="%d" y="%d" %s font-size="12" font-weight="700" fill="%s">%s</text>' % (lx + 14 + k * 118, ly, F, INK, nm))
    panel("Players", 64, [(c[1], c[2]) for c in cats], GOLD, lambda v: "%d players" % v, True)
    panel("Player slots in use (%)", 360, [(c[3], c[4]) for c in cats], BLUE, lambda v: "%.1f%%" % v, True)
    slots = sum(s["max"] for s in servers)
    pp = hist[-2]["players"] if len(hist) > 1 else None
    mins = int(round((ts(hist[-1]) - ts(hist[-2])).total_seconds() / 60)) if len(hist) > 1 else 10
    trend = "" if pp is None else (" (%+d in the last %d minutes)" % (players - pp, mins) if players != pp else " (no change in the last %d minutes)" % mins)
    note = ["Note: right now %d players%s on %d servers with %d player slots; %d servers have players." % (players, trend, len(servers), slots, sum(1 for s in servers if s["players"] > 0)),
            "Servers are sorted by their peak in the last 24 hours (%s); the rest are added up in \"Other servers\"." % ("%d readings, one every 10 minutes" % len(day) if hourly else "the first readings are still being collected"),
            "Player slots in use = players / the server's maximum players."]
    y = 692
    for ln in note:
        o.append('<text x="12" y="%d" %s font-size="12" fill="%s">%s</text>' % (y, F, INK, e(ln))); y += 17
    o.append('<text x="12" y="%d" %s font-size="12" fill="%s"><tspan font-style="italic">Source:</tspan> MP lobby (online data code: live-status, github.com/ThirtyFoxMCx00E1/wagu)</text>' % (y + 2, F, INK))
    # logo: "mp lobby" + a small blue badge (bottom right, like the Eurostat mark)
    o.append('<text x="%d" y="%d" %s font-size="22" font-weight="700" fill="%s" text-anchor="end">mplobby</text>' % (W - 44, H - 14, F, "#7a7a7a"))
    o.append('<rect x="%d" y="%d" width="28" height="19" fill="#2a3fa8"/>' % (W - 38, H - 31))
    o.append('<text x="%d" y="%d" %s font-size="11" font-weight="700" fill="#ffd617" text-anchor="middle">MP</text>' % (W - 24, H - 17, F))
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
                 "slots": sum(s["max"] for s in servers), "top": max([0] + [s["players"] for s in servers]),
                 "s": [[s["name"], s["players"], s["max"]] for s in servers][:60]})
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
