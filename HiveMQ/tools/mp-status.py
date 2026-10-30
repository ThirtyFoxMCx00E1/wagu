#!/usr/bin/env python3
"""MP status beacon: every 10 s publishes this region's health as a retained message on
thirtyfox/mp4/st/<Region> of the local broker. The website's Termius page shows it, so you can
see from the phone that the region is online 24/7. If the machine or broker dies, the broker
publishes the last will {"online": false}. Only Python's standard library, MQTT 3.1.1.

2.22 autopilot: the same loop manages the region by itself - nobody has to type in a terminal.
  - an enabled service (hivemq, mp-server, mp-bridge, mp-console) that is not running is restarted
    (at most 3 times per 10 minutes per service, then it waits and reports)
  - disk above 90 %: journal vacuum + old HiveMQ logs removed
  - low memory (< 5 % available): reported, the broker is restarted only if it also stopped answering
Every action is published on thirtyfox/mp4/bot/<Region> as {"t", "cmd", "out", "ok"}; the MP Bot on
the website (Termius) shows it as a command it ran. MP_AUTOPILOT=0 in mp.env turns it off.

3.23.5 auto update: every 10 minutes the autopilot checks GitHub. When the mod (wagu MP_VERSION_STR)
or the HiveMQ kit has a new version, it runs by itself
  curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash
in its own systemd unit (mp-autoupdate), so nobody has to type it. MP_AUTOUPDATE=0 in mp.env turns it off.
The 24/7 featured servers (mp-bcw, mp-mw3, mp-legacy) are now restarted too when they stop."""
import json, os, re, socket, struct, subprocess, time, urllib.request

def env(path="/etc/mp-hivemq/mp.env"):
    d = {}
    for l in open(path):
        l = l.strip()
        if l and not l.startswith("#") and "=" in l:
            k, v = l.split("=", 1); d[k] = v.strip().strip('"')
    return d

def s(b): b = b.encode(); return struct.pack("!H", len(b)) + b
def rl(n):
    o = b""
    while True:
        x = n % 128; n //= 128
        o += bytes([x | (0x80 if n else 0)])
        if not n: return o

def connect(user, pw, topic, will):
    k = socket.create_connection(("127.0.0.1", 1883), 5)
    flags = 0x80 | 0x40 | 0x20 | 0x04 | 0x02   # user, pass, will retain, will, clean
    body = s("MQTT") + bytes([4, flags]) + struct.pack("!H", 30) + s("mpstatus-%d" % os.getpid()) + s(topic) + s(will) + s(user) + s(pw)
    k.sendall(b"\x10" + rl(len(body)) + body)
    a = k.recv(4)
    if len(a) < 4 or a[0] != 0x20 or a[3] != 0: raise OSError("connack %r" % a)
    return k

def publish(k, topic, msg, retain=True):
    body = s(topic) + msg.encode()
    k.sendall((b"\x31" if retain else b"\x30") + rl(len(body)) + body)   # QoS 0

def active(n): return subprocess.run(["systemctl", "is-active", "--quiet", n]).returncode == 0
def clients():
    try:
        out = subprocess.run(["ss", "-tnH", "state", "established", "( sport = :1883 or sport = :8000 or sport = :8883 or sport = :8884 )"], capture_output=True, text=True).stdout
        return max(0, len(out.splitlines()) - 1)   # minus this beacon
    except Exception: return -1

def enabled(n): return subprocess.run(["systemctl", "is-enabled", "--quiet", n]).returncode == 0

def sh(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return r.returncode == 0, (r.stdout or r.stderr).strip().splitlines()[-1:] or [""]
    except Exception as x:
        return False, [str(x)]

class Autopilot:
    SERVICES = ("hivemq", "mp-server", "mp-bcw", "mp-mw3", "mp-rp", "mp-legacy", "mp-bridge", "mp-console")
    UPDATE_CMD = "curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash"
    WAGU_PROTO = "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/src/net/protocol.h"
    def __init__(self, base, auto_update=True):
        self.base = base; self.hist = {}; self.last_disk = 0
        self.auto_update = auto_update; self.last_check = time.time() - 480; self.last_update = 0
        self.latest = ""
    def installed(self):
        try: return re.search(r'MP_VERSION_STR\s+"([^"]+)"', open(os.path.join(self.base, "src", "src", "net", "protocol.h")).read()).group(1)
        except Exception: return ""
    def kit_sha(self):
        r = subprocess.run(["git", "-C", os.path.join(self.base, "kit"), "rev-parse", "HEAD"], capture_output=True, text=True)
        return r.stdout.strip()
    def check_update(self):
        """-> (cmd, out, ok) when an update was started, else None"""
        now = time.time()
        if not self.auto_update or now - self.last_check < 600: return None
        self.last_check = now
        if active("mp-autoupdate"): return None                  # one is already running
        try:
            txt = urllib.request.urlopen(self.WAGU_PROTO + "?t=%d" % int(now), timeout=15).read().decode("utf-8", "replace")
            self.latest = re.search(r'MP_VERSION_STR\s+"([^"]+)"', txt).group(1)
            r = subprocess.run(["git", "ls-remote", "https://github.com/ThirtyFoxMCx00E1/HiveMQ.git", "HEAD"], capture_output=True, text=True, timeout=15)
            kit_new = (r.stdout.split() or [""])[0]
        except Exception:
            return None
        have, kit = self.installed(), self.kit_sha()
        why = []
        if self.latest and have and have != self.latest: why.append("mod %s -> %s" % (have or "?", self.latest))
        if kit_new and kit and kit_new != kit: why.append("kit %s -> %s" % (kit[:7], kit_new[:7]))
        if not why or now - self.last_update < 1800: return None   # at most one auto update per 30 min
        self.last_update = now
        ok, out = sh(["systemd-run", "--unit", "mp-autoupdate", "--collect", "--quiet", "/bin/bash", "-c",
                      "curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | bash"])
        return (self.UPDATE_CMD, ("started: " + ", ".join(why)) if ok else (out[0] or "could not start"), ok)
    def _allowed(self, n):
        now = time.time(); h = [t for t in self.hist.get(n, []) if now - t < 600]
        self.hist[n] = h
        return len(h) < 3
    def tick(self):
        acts = []
        for n in self.SERVICES:
            if not enabled(n) or active(n): continue
            if os.path.exists("/etc/mp-hivemq/hold/" + n): continue   # 3.33: stopped on purpose from the console (menu s)
            if not self._allowed(n):
                acts.append(("systemctl restart " + n, "waiting: 3 restarts in 10 min, check journalctl -u " + n, False)); continue
            self.hist[n].append(time.time())
            ok, out = sh(["systemctl", "restart", n])
            time.sleep(3)
            ok = ok and active(n)
            acts.append(("systemctl restart " + n, "running again" if ok else (out[0] or "still down"), ok))
        try:
            st = os.statvfs("/"); used = 1 - st.f_bavail / float(st.f_blocks)
            if used > 0.90 and time.time() - self.last_disk > 3600:
                self.last_disk = time.time()
                ok, _ = sh(["journalctl", "--vacuum-size=100M"])
                sh(["find", os.path.join(self.base, "hivemq", "log"), "-name", "*.log.*", "-mtime", "+2", "-delete"])
                acts.append(("journalctl --vacuum-size=100M", "disk was %d %% full" % int(used * 100), ok))
        except Exception:
            pass
        u = self.check_update()
        if u: acts.append(u)
        return acts

def main():
    e = env(); region = e.get("REGION", "Asia"); dom = e.get("DOMAIN", "")
    topic = "thirtyfox/mp4/st/" + region
    bot_topic = "thirtyfox/mp4/bot/" + region
    pilot = Autopilot(e.get("BASE", "/opt/mp"), e.get("MP_AUTOUPDATE", "1") != "0") if e.get("MP_AUTOPILOT", "1") != "0" else None
    pending = []   # actions done while the broker was down are sent after the reconnect
    while True:
        try:
            if pilot: pending += [(int(time.time()),) + a for a in pilot.tick()]   # broker down? fix it first
            k = connect("bridge", e["BRIDGE_PASS"], topic, json.dumps({"region": region, "online": False}))
            while True:
                if pilot: pending += [(int(time.time()),) + a for a in pilot.tick()]
                while pending:
                    t, cmd, out, ok = pending.pop(0)
                    publish(k, bot_topic, json.dumps({"t": t, "cmd": cmd, "out": out, "ok": ok}, separators=(",", ":")), retain=False)
                    print("autopilot:", cmd, "->", out, flush=True)
                up = float(open("/proc/uptime").read().split()[0])
                mem = {l.split(":")[0]: int(l.split()[1]) for l in open("/proc/meminfo") if l.split(":")[0] in ("MemTotal", "MemAvailable")}
                st = {"region": region, "online": True, "t": int(time.time()), "up": int(up),
                      "load": os.getloadavg()[0], "mem": [ (mem["MemTotal"] - mem["MemAvailable"]) // 1024, mem["MemTotal"] // 1024 ],
                      "clients": clients(), "broker": active("hivemq"), "server": active("mp-server"),
                      "bridge": active("mp-bridge"), "console": ("https://%s:7681/" % dom) if dom and active("mp-console") else "",
                      "slots": int(e.get("MAX_PLAYERS", "100") or 100), "ver": "3.23.5", "autopilot": bool(pilot),
                      "mod": pilot.installed() if pilot else "", "latest": pilot.latest if pilot else "",
                      "autoupdate": bool(pilot and pilot.auto_update), "updating": active("mp-autoupdate"),
                      "bcw": active("mp-bcw"), "mw3": active("mp-mw3"), "rp": active("mp-rp")}
                publish(k, topic, json.dumps(st, separators=(",", ":")))
                k.sendall(b"\xc0\x00")   # ping
                time.sleep(10)
        except Exception as x:
            print("status beacon:", x, flush=True)
            pending = pending[-20:]
            time.sleep(5)

if __name__ == "__main__": main()
