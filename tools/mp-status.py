#!/usr/bin/env python3
"""MP status beacon: every 10 s publishes this region's health as a retained message on
thirtyfox/mp4/st/<Region> of the local broker. The website's Termius page shows it, so you can
see from the phone that the region is online 24/7. If the machine or broker dies, the broker
publishes the last will {"online": false}. Only Python's standard library, MQTT 3.1.1."""
import json, os, socket, struct, subprocess, time

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

def publish(k, topic, msg):
    body = s(topic) + msg.encode()
    k.sendall(b"\x31" + rl(len(body)) + body)   # QoS 0, retained

def active(n): return subprocess.run(["systemctl", "is-active", "--quiet", n]).returncode == 0
def clients():
    try:
        out = subprocess.run(["ss", "-tnH", "state", "established", "( sport = :1883 or sport = :8000 or sport = :8883 or sport = :8884 )"], capture_output=True, text=True).stdout
        return max(0, len(out.splitlines()) - 1)   # minus this beacon
    except Exception: return -1

def main():
    e = env(); region = e.get("REGION", "Asia"); dom = e.get("DOMAIN", "")
    topic = "thirtyfox/mp4/st/" + region
    while True:
        try:
            k = connect("bridge", e["BRIDGE_PASS"], topic, json.dumps({"region": region, "online": False}))
            while True:
                up = float(open("/proc/uptime").read().split()[0])
                mem = {l.split(":")[0]: int(l.split()[1]) for l in open("/proc/meminfo") if l.split(":")[0] in ("MemTotal", "MemAvailable")}
                st = {"region": region, "online": True, "t": int(time.time()), "up": int(up),
                      "load": os.getloadavg()[0], "mem": [ (mem["MemTotal"] - mem["MemAvailable"]) // 1024, mem["MemTotal"] // 1024 ],
                      "clients": clients(), "broker": active("hivemq"), "server": active("mp-server"),
                      "bridge": active("mp-bridge"), "console": ("https://%s:7681/" % dom) if dom and active("mp-console") else "",
                      "slots": int(e.get("MAX_PLAYERS", "100") or 100), "ver": "2.21"}
                publish(k, topic, json.dumps(st, separators=(",", ":")))
                k.sendall(b"\xc0\x00")   # ping
                time.sleep(10)
        except Exception as x:
            print("status beacon:", x, flush=True)
            time.sleep(5)

if __name__ == "__main__": main()
