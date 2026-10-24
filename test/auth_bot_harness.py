# test helper: the real bot code (authcore + the MQTT bridge of main.py) without Discord.
# Every code a phone registers gets "/gen" by a fake Discord user after 1 s.
import os, sys, time, threading, tempfile
sys.path.insert(0, os.path.expanduser("~/workspace/discordpy-bot"))
os.environ.setdefault("DISCORD_TOKEN", "x")
os.environ["DATA_DIR"] = tempfile.mkdtemp()
os.environ["AUTH_PRIVATE_KEY"] = open(os.path.expanduser("~/workspace/secrets/AUTH_PRIVATE_KEY.txt")).read().strip()
import main
main.bridge.start()
seen = set()
print("harness up", flush=True)
end = time.time() + float(sys.argv[1] if len(sys.argv) > 1 else 120)
while time.time() < end:
    with main.core.lock:
        codes = list(main.core.codes.keys())
    for c in codes:
        if c in seen: continue
        seen.add(c)
        time.sleep(1.0)
        # wrong code first: must be refused
        t0, p0 = main.core.gen(999, "other", "ZZZZZZZZ" if c != "ZZZZZZZZ" else "YYYYYYYY")
        assert not p0, t0
        text, pushes = main.core.gen(111, "tester#0001", c)
        print("gen", c, "->", text, len(pushes), flush=True)
        for sid, fr in pushes: main.bridge.push(sid, fr)
        # the same code twice: refused (one use)
        t2, p2 = main.core.gen(998, "tester#0001", c)
        assert not p2
    time.sleep(0.2)
