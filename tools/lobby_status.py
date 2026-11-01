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


def svg(servers, players, now):
    W, rowh = 840, 30
    rows = servers[:8]
    H = 150 + max(1, len(rows)) * rowh + 34
    e = lambda x: html.escape(str(x), quote=True)
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="MP Multiplayer: %d players online on %d servers">' % (W, H, W, H, players, len(servers), )]
    o.append('<title>MP Multiplayer: %d players online on %d servers</title>' % (players, len(servers)))
    o.append('<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1b1b1b"/><stop offset="1" stop-color="#0d0d0d"/></linearGradient></defs>')
    o.append('<rect width="%d" height="%d" rx="14" fill="url(#g)" stroke="#2a2a2a"/>' % (W, H))
    F = 'font-family="Verdana,DejaVu Sans,Segoe UI,sans-serif"'
    M = 'font-family="DejaVu Sans Mono,Consolas,monospace"'
    o.append('<text x="28" y="44" %s font-size="22" font-weight="700" fill="#e9e4d8">MP <tspan fill="#f0a830">MULTIPLAYER</tspan> · LIVE LOBBY</text>' % F)
    dot = "#5fd16f" if servers else "#e8b931"
    o.append('<circle cx="%d" cy="37" r="7" fill="%s"><animate attributeName="opacity" values="1;.35;1" dur="1.6s" repeatCount="indefinite"/></circle>' % (W - 40, dot))
    o.append('<text x="%d" y="43" %s font-size="13" fill="#8d877a" text-anchor="end">%s</text>' % (W - 56, F, "online" if servers else "quiet"))
    # big numbers
    o.append('<text x="28" y="104" %s font-size="44" font-weight="700" fill="#5fd16f">%d</text>' % (F, players))
    o.append('<text x="28" y="126" %s font-size="13" fill="#8d877a">PLAYERS ONLINE</text>' % F)
    o.append('<text x="250" y="104" %s font-size="44" font-weight="700" fill="#f0a830">%d</text>' % (F, len(servers)))
    o.append('<text x="250" y="126" %s font-size="13" fill="#8d877a">SERVERS ONLINE</text>' % F)
    slots = sum(s["max"] for s in servers)
    o.append('<text x="470" y="104" %s font-size="44" font-weight="700" fill="#e9e4d8">%d</text>' % (F, slots))
    o.append('<text x="470" y="126" %s font-size="13" fill="#8d877a">PLAYER SLOTS</text>' % F)
    y = 150
    o.append('<line x1="28" y1="%d" x2="%d" y2="%d" stroke="#2a2a2a"/>' % (y - 8, W - 28, y - 8))
    if not rows:
        o.append('<text x="28" y="%d" %s font-size="14" fill="#8d877a">No server is hosting right now. Open GTA SA, MP MENU → Host to start one.</text>' % (y + 18, F))
        y += rowh
    for s in rows:
        full = s["max"] and s["players"] >= s["max"]
        name = s["name"] if len(s["name"]) <= 34 else s["name"][:33] + "…"
        tag = ' <tspan fill="#f0a830" font-size="11">DEFAULT</tspan>' if is_default(s) else ""
        o.append('<text x="28" y="%d" %s font-size="14" fill="#e9e4d8">%s%s</text>' % (y + 18, F, e(name), tag))
        reg = REG[s["region"]] if 0 <= s["region"] < 4 else "—"
        o.append('<text x="560" y="%d" %s font-size="13" fill="#8d877a">%s</text>' % (y + 18, M, e(reg)))
        o.append('<text x="%d" y="%d" %s font-size="14" fill="%s" text-anchor="end">%d / %d</text>' % (W - 28, y + 18, M, "#e2554b" if full else "#5fd16f", s["players"], s["max"]))
        y += rowh
    more = len(servers) - len(rows)
    foot = "Updated %s UTC · refreshes every 10 min" % now.strftime("%Y-%m-%d %H:%M")
    if more > 0:
        foot = "+%d more servers · " % more + foot
    o.append('<text x="28" y="%d" %s font-size="12" fill="#8d877a">%s</text>' % (H - 16, F, e(foot)))
    o.append('<text x="%d" y="%d" %s font-size="12" fill="#8d877a" text-anchor="end">thirtyfoxmcx00e1.github.io/wagu</text>' % (W - 28, H - 16, F))
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
    open(os.path.join(out, "players.svg"), "w").write(svg(servers, players, now))
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
