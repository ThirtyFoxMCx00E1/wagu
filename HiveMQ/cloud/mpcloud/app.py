"""MP Cloud - your own Oracle-Cloud-style "Always Free" console.

Accounts with a free quota (2 OCPU / 12 GB / 100 GB / 2 servers by default, like Oracle's A1 in
2026), create / start / stop / reboot / terminate servers, one-click MP server (HiveMQ + the 24/7
MP servers via vps-setup.sh), API keys for scripts and GitHub Actions, audit log, admin page.

Run:  uvicorn mpcloud.app:app --host 0.0.0.0 --port 9000      (cloud/install.sh does this for you)
"""
import hashlib
import hmac
import json
import os
import re
import secrets
import threading
import time
from pathlib import Path
from typing import Optional

from fastapi import Depends, FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .db import DB
from .drivers import IMAGES, DriverError, make_driver
from .oci_driver import SHAPES as OCI_SHAPES, OCIDriver
from .userdata import MP_REGIONS, PRESETS, user_data

VERSION = "1.0"
DATA = os.environ.get("MP_CLOUD_DATA", "/var/lib/mp-cloud")
SESSION_DAYS = 14
SSH_PORT_RANGE = (22000, 22999)
STATIC = Path(__file__).parent / "static"

db = DB(os.path.join(DATA, "mpcloud.db"))
driver = make_driver()                      # servers on THIS machine (Incus)
oracle = OCIDriver(DATA, db.settings)       # servers in your real Oracle account (OCI API)
app = FastAPI(title="MP Cloud", version=VERSION, docs_url="/api/docs", redoc_url=None)
# the console restarted while a server was being made / removed: that job is gone
db.x("UPDATE instances SET state='FAILED', error='MP Cloud restarted while this was running - terminate it and create it again' "
     "WHERE state IN ('PROVISIONING','TERMINATING') AND backend='local'")
# Oracle launches that were waiting for capacity are picked up again below (resume_oracle)


# ---------------------------------------------------------------- passwords + tokens
def hash_pw(pw):
    salt = secrets.token_bytes(16)
    h = hashlib.scrypt(pw.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32)
    return "scrypt$%s$%s" % (salt.hex(), h.hex())


def check_pw(pw, stored):
    try:
        _, salt, h = stored.split("$")
        got = hashlib.scrypt(pw.encode(), salt=bytes.fromhex(salt), n=2 ** 14, r=8, p=1, dklen=32)
        return hmac.compare_digest(got.hex(), h)
    except Exception:
        return False


def sha(s):
    return hashlib.sha256(s.encode()).hexdigest()


_fails = {}   # ip -> [count, first_at]  (login brute force brake)


def brake(ip, ok=False):
    now = time.time()
    c = _fails.get(ip)
    if c and now - c[1] > 900:
        c = None
    if ok:
        _fails.pop(ip, None)
        return
    if c and c[0] >= 10:
        raise HTTPException(429, "Too many wrong passwords. Try again in 15 minutes.")
    _fails[ip] = [c[0] + 1, c[1]] if c else [1, now]


# ---------------------------------------------------------------- auth
def current_user(request: Request):
    auth = request.headers.get("authorization", "")
    if auth.lower().startswith("bearer "):
        tok = auth[7:].strip()
        k = db.one("SELECT * FROM api_keys WHERE token_hash=?", (sha(tok),))
        if not k:
            raise HTTPException(401, "API key not valid")
        db.x("UPDATE api_keys SET last_used=? WHERE id=?", (time.time(), k["id"]))
        u = db.one("SELECT * FROM users WHERE id=?", (k["user_id"],))
    else:
        tok = request.cookies.get("mpc_session", "")
        s = db.one("SELECT * FROM sessions WHERE token_hash=? AND expires>?", (sha(tok), time.time())) if tok else None
        if not s:
            raise HTTPException(401, "Sign in first")
        # cookie requests that change something must come from our own page (CSRF guard)
        if request.method not in ("GET", "HEAD") and request.headers.get("x-mp-cloud") != "1":
            raise HTTPException(403, "missing X-MP-Cloud header")
        u = db.one("SELECT * FROM users WHERE id=?", (s["user_id"],))
    if not u:
        raise HTTPException(401, "Account not found")
    return u


def admin_user(u=Depends(current_user)):
    if not u["is_admin"]:
        raise HTTPException(403, "Administrators only")
    return u


def public_user(u):
    d = {k: u[k] for k in ("id", "email", "name", "is_admin", "ocpu_limit", "mem_limit",
                           "disk_limit", "inst_limit", "created")}
    d["is_admin"] = bool(d["is_admin"])
    return d


def new_session(resp: Response, user_id, request: Request):
    tok = secrets.token_urlsafe(32)
    db.x("INSERT INTO sessions(token_hash,user_id,expires) VALUES(?,?,?)",
         (sha(tok), user_id, time.time() + SESSION_DAYS * 86400))
    secure = request.url.scheme == "https" or request.headers.get("x-forwarded-proto") == "https"
    resp.set_cookie("mpc_session", tok, max_age=SESSION_DAYS * 86400, httponly=True,
                    samesite="strict", secure=secure, path="/")


class SignUp(BaseModel):
    email: str = Field(min_length=3, max_length=120)
    name: str = Field(min_length=1, max_length=60)
    password: str = Field(min_length=8, max_length=200)
    invite: Optional[str] = ""


class Login(BaseModel):
    email: str
    password: str


EMAIL_RE = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")


@app.post("/api/v1/auth/signup")
def signup(body: SignUp, request: Request, response: Response):
    st = db.settings()
    first = db.one("SELECT COUNT(*) n FROM users")["n"] == 0
    email = body.email.strip().lower()
    if not EMAIL_RE.match(email):
        raise HTTPException(400, "That email doesn't look right")
    if not first:
        if st["signup"] == "closed":
            raise HTTPException(403, "Sign-up is closed on this cloud. Ask the admin for an account.")
        if st["signup"] == "invite" and not (st["invite_code"] and hmac.compare_digest(body.invite or "", st["invite_code"])):
            raise HTTPException(403, "Wrong invite code")
    if db.one("SELECT id FROM users WHERE email=?", (email,)):
        raise HTTPException(409, "There is already an account with that email")
    uid = db.x("INSERT INTO users(email,name,pw_hash,is_admin,ocpu_limit,mem_limit,disk_limit,inst_limit,created)"
               " VALUES(?,?,?,?,?,?,?,?,?)",
               (email, body.name.strip(), hash_pw(body.password), 1 if first else 0,
                float(st["free_ocpus"]), float(st["free_mem_gb"]), int(st["free_disk_gb"]),
                int(st["free_instances"]), time.time()))
    db.log(uid, "account.create", email, "first account = administrator" if first else "")
    new_session(response, uid, request)
    return {"ok": True, "admin": first}


@app.post("/api/v1/auth/login")
def login(body: Login, request: Request, response: Response):
    ip = request.client.host if request.client else "?"
    brake(ip)
    u = db.one("SELECT * FROM users WHERE email=?", (body.email.strip().lower(),))
    if not u or not check_pw(body.password, u["pw_hash"]):
        raise HTTPException(401, "Wrong email or password")
    brake(ip, ok=True)
    db.x("DELETE FROM sessions WHERE expires<?", (time.time(),))
    new_session(response, u["id"], request)
    db.log(u["id"], "account.login", u["email"], ip)
    return {"ok": True}


@app.post("/api/v1/auth/logout")
def logout(request: Request, response: Response):
    tok = request.cookies.get("mpc_session", "")
    if tok:
        db.x("DELETE FROM sessions WHERE token_hash=?", (sha(tok),))
    response.delete_cookie("mpc_session", path="/")
    return {"ok": True}


class PwChange(BaseModel):
    old: str
    new: str = Field(min_length=8, max_length=200)


@app.post("/api/v1/me/password")
def change_pw(body: PwChange, u=Depends(current_user)):
    if not check_pw(body.old, u["pw_hash"]):
        raise HTTPException(400, "The current password is wrong")
    db.x("UPDATE users SET pw_hash=? WHERE id=?", (hash_pw(body.new), u["id"]))
    db.x("DELETE FROM sessions WHERE user_id=?", (u["id"],))
    db.log(u["id"], "account.password")
    return {"ok": True}


# ---------------------------------------------------------------- public info
@app.get("/api/v1/public")
def public_info():
    st = db.settings()
    return {"version": VERSION, "region": st["region_label"], "signup": st["signup"],
            "first_run": db.one("SELECT COUNT(*) n FROM users")["n"] == 0,
            "free": {"ocpus": float(st["free_ocpus"]), "mem_gb": float(st["free_mem_gb"]),
                     "disk_gb": int(st["free_disk_gb"]), "instances": int(st["free_instances"])},
            "driver": driver.name, "oracle": oracle.configured()}


def usage(user_id):
    r = db.one("SELECT COUNT(*) n, COALESCE(SUM(ocpus),0) oc, COALESCE(SUM(mem_gb),0) mem,"
               " COALESCE(SUM(disk_gb),0) disk FROM instances WHERE user_id=?", (user_id,))
    return {"instances": r["n"], "ocpus": r["oc"], "mem_gb": r["mem"], "disk_gb": r["disk"]}


@app.get("/api/v1/me")
def me(u=Depends(current_user)):
    st = db.settings()
    return {"user": public_user(u), "usage": usage(u["id"]), "region": st["region_label"],
            "public_host": st["public_host"], "driver": driver.name}


# ---------------------------------------------------------------- catalog
@app.get("/api/v1/catalog")
def catalog(u=Depends(current_user)):
    return {
        "images": [{"id": k, "label": v["label"], "oracle": k.startswith("ubuntu")} for k, v in IMAGES.items()],
        "presets": [{"id": k, "label": v["label"], "desc": v["desc"], "min_mem": v["min_mem"]} for k, v in PRESETS.items()],
        "mp_regions": [{"id": k, "label": v["label"], "domain": v["duck"] + ".duckdns.org"} for k, v in MP_REGIONS.items()],
        "placements": [
            {"id": "local", "label": "This machine", "desc": "Incus server on the MP Cloud host", "ready": driver.name == "fake" or driver.available(),
             "shapes": [{"id": "MP.Local.Flex", "label": "MP Local Flex", "flex": True}]},
            {"id": "oracle", "label": "Oracle Cloud", "desc": "Real OCI server in the admin's Oracle account", "ready": oracle.configured(),
             "shapes": [{"id": k, "label": v["label"], "flex": v["flex"], "ocpus": v.get("ocpus"), "mem_gb": v.get("mem_gb")}
                        for k, v in OCI_SHAPES.items()]},
        ],
    }


# ---------------------------------------------------------------- instances
class CreateInst(BaseModel):
    name: str = Field(min_length=1, max_length=40)
    placement: str = "local"           # local | oracle
    shape: str = "MP.Local.Flex"
    image: str = "ubuntu-24.04"
    ocpus: float = Field(1, ge=1, le=64)
    mem_gb: float = Field(6, ge=0.5, le=512)
    disk_gb: int = Field(50, ge=10, le=2000)
    ssh_key: Optional[str] = ""
    preset: str = "plain"              # plain | mp | coolify | dokploy
    mp_region: Optional[str] = ""
    duckdns_token: Optional[str] = ""


class Action(BaseModel):
    action: str


NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._-]{0,39}$")
SSH_RE = re.compile(r"^(ssh-(ed25519|rsa)|ecdsa-sha2-nistp\d+|sk-ssh-ed25519@openssh.com) [A-Za-z0-9+/=]+( .*)?$")
_create_lock = threading.Lock()
BUSY = ("PROVISIONING", "WAITING_CAPACITY", "TERMINATING")


def inst_out(i, st=None):
    st = st or db.settings()
    local = i["backend"] == "local"
    host = st["public_host"] if local else (i["ipv4"] or "")
    ports = json.loads(i["ports"] or "[]")
    if local:
        ssh = next((p for p in ports if p[0] == "tcp" and p[2] == 22), None)
        ssh_cmd = ("ssh -p %d opc@%s" % (ssh[1], host or "<this-machine>")) if ssh else None
    else:
        ssh_cmd = ("ssh opc@%s" % host) if host else None
    pr = PRESETS.get(i["preset"], PRESETS["plain"])
    links = []
    if host and i["state"] == "RUNNING":
        if i["preset"] == "coolify":
            links.append({"label": "Coolify dashboard", "url": "http://%s:8000" % host})
        if i["preset"] == "dokploy":
            links.append({"label": "Dokploy dashboard", "url": "http://%s:3000" % host})
        if i["preset"] == "mp":
            links.append({"label": "Web terminal (ttyd)", "url": "https://%s:7681" % host})
    return {"id": i["id"], "name": i["name"], "placement": i["backend"], "shape": i["shape"],
            "image": i["image"], "image_label": IMAGES.get(i["image"], {}).get("label", i["image"]),
            "preset": i["preset"], "preset_label": pr["label"],
            "ocpus": i["ocpus"], "mem_gb": i["mem_gb"], "disk_gb": i["disk_gb"],
            "mp_region": i["mp_region"], "state": i["state"], "note": i["status_note"],
            "private_ip": i["ipv4"] if local else None, "public_ip": None if local else i["ipv4"],
            "public_host": host,
            "ports": ([{"proto": p[0], "public": p[1], "private": p[2]} for p in ports] if local else
                      [{"proto": "tcp", "public": 22, "private": 22}] +
                      [{"proto": "tcp", "public": n, "private": n} for n in pr["tcp"]] +
                      [{"proto": "udp", "public": n, "private": n} for n in pr["udp"]]),
            "ssh": ssh_cmd, "links": links,
            "error": i["error"], "created": i["created"], "user_id": i["user_id"]}


def pick_ports(preset):
    """local servers: SSH on a free 22xxx port; preset ports 1:1 (one of each preset per public IP)."""
    used = set()
    for r in db.q("SELECT ports FROM instances WHERE backend='local'"):
        for p in json.loads(r["ports"] or "[]"):
            used.add((p[0], p[1]))
    ssh = next((n for n in range(*SSH_PORT_RANGE) if ("tcp", n) not in used), None)
    if ssh is None:
        raise HTTPException(409, "No SSH ports left on this machine")
    ports = [("tcp", ssh, 22)]
    pr = PRESETS[preset]
    want = [("tcp", n) for n in pr["tcp"]] + [("udp", n) for n in pr["udp"]]
    taken = [p for p in want if p in used or (p[0] == "tcp" and p[1] in (CONSOLE_PORT,))]
    if taken:
        raise HTTPException(409, "Ports %s are already used by another server on this machine. "
                                 "Put this one on Oracle Cloud, or terminate the other one."
                            % ", ".join("%s/%d" % (a, b) for a, b in taken))
    ports += [(a, b, b) for a, b in want]
    return ports


CONSOLE_PORT = int(os.environ.get("MP_CLOUD_PORT", "9000"))


def set_state(inst_id, state, note=None, **cols):
    sets, args = ["state=?", "status_note=?"], [state, note]
    for k, v in cols.items():
        sets.append("%s=?" % k)
        args.append(v)
    db.x("UPDATE instances SET %s WHERE id=?" % ", ".join(sets), (*args, inst_id))


def provision_local(i):
    extra = json.loads(i["extra"] or "{}")
    try:
        ud = user_data(i["ssh_key"], i["preset"], i["mp_region"], extra.get("duck", ""), on_oracle=False)
        driver.create(i["backend_name"], i["image"], i["ocpus"], i["mem_gb"], i["disk_gb"], ud,
                      [tuple(p) for p in json.loads(i["ports"])])
        set_state(i["id"], "RUNNING", None, error=None, extra=None)   # DuckDNS token: not kept
        db.log(i["user_id"], "instance.running", i["name"])
    except Exception as e:  # noqa: BLE001 - shown to the owner
        set_state(i["id"], "FAILED", None, error=str(e)[-500:], extra=None)
        db.log(i["user_id"], "instance.failed", i["name"], str(e)[-300:])


def provision_oracle(i):
    inst_id = i["id"]
    extra = json.loads(i["extra"] or "{}")

    def keep_going():
        r = db.one("SELECT state FROM instances WHERE id=?", (inst_id,))
        return bool(r) and r["state"] in ("PROVISIONING", "WAITING_CAPACITY")

    def on_wait(msg):
        if keep_going():
            set_state(inst_id, "WAITING_CAPACITY", msg)

    try:
        ud = user_data(i["ssh_key"], i["preset"], i["mp_region"], extra.get("duck", ""), on_oracle=True)
        ocid = oracle.launch("mpc-" + re.sub(r"[^a-z0-9-]", "-", i["name"].lower())[:30], i["image"], i["shape"],
                             i["ocpus"], i["mem_gb"], i["disk_gb"], ud, i["ssh_key"], keep_going, on_wait,
                             retry_s=int(db.settings().get("oci_retry_s") or 60))
        if not ocid:
            return   # terminated while waiting: the terminate job removes the row
        set_state(inst_id, "PROVISIONING", "Oracle created it - starting up", backend_name=ocid, extra=None)
        db.log(i["user_id"], "instance.oracle.created", i["name"], ocid)
        ip = oracle.wait_running(ocid)
        set_state(inst_id, "RUNNING", None if ip else "No public IP yet", ipv4=ip, error=None)
        db.log(i["user_id"], "instance.running", i["name"], ip or "")
    except Exception as e:  # noqa: BLE001
        set_state(inst_id, "FAILED", None, error=str(e)[-500:], extra=None)
        db.log(i["user_id"], "instance.failed", i["name"], str(e)[-300:])


def provision(inst_id):
    i = db.one("SELECT * FROM instances WHERE id=?", (inst_id,))
    if i:
        (provision_oracle if i["backend"] == "oracle" else provision_local)(i)


def resume_oracle():
    """after a restart: Oracle servers still waiting for capacity keep trying."""
    for i in db.q("SELECT id, backend_name FROM instances WHERE backend='oracle' AND state IN ('PROVISIONING','WAITING_CAPACITY')"):
        if i["backend_name"].startswith("pending-"):
            threading.Thread(target=provision, args=(i["id"],), daemon=True).start()


@app.get("/api/v1/instances")
def list_instances(all: int = 0, u=Depends(current_user)):
    refresh_states()
    if all and u["is_admin"]:
        rows = db.q("SELECT * FROM instances ORDER BY created DESC")
    else:
        rows = db.q("SELECT * FROM instances WHERE user_id=? ORDER BY created DESC", (u["id"],))
    st = db.settings()
    return [inst_out(r, st) for r in rows]


@app.post("/api/v1/instances")
def create_instance(body: CreateInst, u=Depends(current_user)):
    name = body.name.strip()
    if not NAME_RE.match(name):
        raise HTTPException(400, "Name: letters, numbers, space . _ - (max 40)")
    if body.placement not in ("local", "oracle"):
        raise HTTPException(400, "placement: local or oracle")
    if body.image not in IMAGES:
        raise HTTPException(400, "Unknown image")
    if body.preset not in PRESETS:
        raise HTTPException(400, "Unknown preset")
    if body.preset == "mp" and body.mp_region not in MP_REGIONS:
        raise HTTPException(400, "Pick the MP region for an MP server")
    key = (body.ssh_key or "").strip()
    if key and not SSH_RE.match(key):
        raise HTTPException(400, "That is not an SSH public key (it starts with ssh-ed25519 or ssh-rsa)")
    if body.ocpus != int(body.ocpus):
        raise HTTPException(400, "OCPUs: whole numbers")
    ocpus, mem = body.ocpus, body.mem_gb
    if body.placement == "oracle":
        if not oracle.configured():
            raise HTTPException(409, "The admin hasn't connected an Oracle account yet")
        if body.shape not in OCI_SHAPES:
            raise HTTPException(400, "Oracle shape: VM.Standard.A1.Flex or VM.Standard.E2.1.Micro")
        if not body.image.startswith("ubuntu"):
            raise HTTPException(400, "On Oracle pick Ubuntu 24.04 or 22.04")
        if not OCI_SHAPES[body.shape]["flex"]:
            ocpus, mem = OCI_SHAPES[body.shape]["ocpus"], OCI_SHAPES[body.shape]["mem_gb"]
        if body.disk_gb < 47:
            raise HTTPException(400, "Oracle boot volumes are at least 47 GB")
    else:
        body.shape = "MP.Local.Flex"
    if mem < PRESETS[body.preset]["min_mem"]:
        raise HTTPException(400, "%s needs at least %g GB of memory" % (PRESETS[body.preset]["label"], PRESETS[body.preset]["min_mem"]))
    with _create_lock:
        use = usage(u["id"])
        if use["instances"] + 1 > u["inst_limit"]:
            raise HTTPException(403, "Your free tier allows %d servers. Terminate one first." % u["inst_limit"])
        if use["ocpus"] + ocpus > u["ocpu_limit"] + 1e-9:
            raise HTTPException(403, "Over your free OCPUs: %g of %g used" % (use["ocpus"], u["ocpu_limit"]))
        if use["mem_gb"] + mem > u["mem_limit"] + 1e-9:
            raise HTTPException(403, "Over your free memory: %g of %g GB used" % (use["mem_gb"], u["mem_limit"]))
        if use["disk_gb"] + body.disk_gb > u["disk_limit"]:
            raise HTTPException(403, "Over your free storage: %d of %d GB used" % (use["disk_gb"], u["disk_limit"]))
        short = secrets.token_hex(6)
        if body.placement == "local":
            host = driver.host_info()
            tot = db.one("SELECT COALESCE(SUM(mem_gb),0) m FROM instances WHERE backend='local'")["m"]
            if host["mem_gb"] and tot + mem > host["mem_gb"] - 0.5:
                raise HTTPException(507, "Out of host capacity: this machine has %.1f GB and %.1f GB is given out. "
                                         "Put the server on Oracle Cloud instead." % (host["mem_gb"], tot))
            ports, bname = pick_ports(body.preset), "mpc-" + short
        else:
            ports, bname = [], "pending-" + short
        rl = db.settings()["region_label"]
        inst_id = "ocid1.instance.mp.%s.%s" % (rl.split("-")[1] if rl.count("-") >= 1 else "local", secrets.token_hex(12))
        extra = json.dumps({"duck": (body.duckdns_token or "").strip()}) if (body.duckdns_token or "").strip() else None
        db.x("INSERT INTO instances(id,user_id,name,backend,backend_name,preset,shape,image,ocpus,mem_gb,disk_gb,"
             "mp_region,ssh_key,state,ports,extra,created) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
             (inst_id, u["id"], name, body.placement, bname, body.preset, body.shape, body.image, ocpus, mem,
              body.disk_gb, body.mp_region or None, key or None, "PROVISIONING", json.dumps(ports), extra, time.time()))
    db.log(u["id"], "instance.create", name, {"placement": body.placement, "shape": body.shape, "ocpus": ocpus,
                                               "mem_gb": mem, "preset": body.preset})
    threading.Thread(target=provision, args=(inst_id,), daemon=True).start()
    return inst_out(db.one("SELECT * FROM instances WHERE id=?", (inst_id,)))


def owned(inst_id, u):
    i = db.one("SELECT * FROM instances WHERE id=?", (inst_id,))
    if not i or (i["user_id"] != u["id"] and not u["is_admin"]):
        raise HTTPException(404, "Server not found")
    return i


def drv(i):
    return oracle if i["backend"] == "oracle" else driver


@app.get("/api/v1/instances/{inst_id}")
def get_instance(inst_id: str, u=Depends(current_user)):
    refresh_states()
    return inst_out(owned(inst_id, u))


@app.post("/api/v1/instances/{inst_id}/action")
def instance_action(inst_id: str, body: Action, u=Depends(current_user)):
    i = owned(inst_id, u)
    if body.action not in ("start", "stop", "reboot"):
        raise HTTPException(400, "action: start, stop or reboot")
    if i["state"] in BUSY or i["state"] == "FAILED":
        raise HTTPException(409, "Not possible while it is %s" % i["state"].replace("_", " ").lower())
    try:
        drv(i).action(i["backend_name"], body.action)
    except DriverError as e:
        raise HTTPException(502, str(e))
    set_state(inst_id, "STOPPED" if body.action == "stop" else "RUNNING", None)
    db.log(u["id"], "instance." + body.action, i["name"])
    return {"ok": True}


@app.delete("/api/v1/instances/{inst_id}")
def terminate(inst_id: str, u=Depends(current_user)):
    i = owned(inst_id, u)
    if i["state"] == "TERMINATING":
        return {"ok": True}
    if i["state"] == "PROVISIONING" and i["backend"] == "local":
        raise HTTPException(409, "Wait until it is created, then terminate it")
    was_waiting = i["backend"] == "oracle" and i["backend_name"].startswith("pending-")
    set_state(inst_id, "TERMINATING", None)

    def work():
        try:
            if not was_waiting:
                drv(i).delete(i["backend_name"])
            db.x("DELETE FROM instances WHERE id=?", (inst_id,))
            db.log(i["user_id"], "instance.terminate", i["name"])
        except Exception as e:  # noqa: BLE001
            set_state(inst_id, "FAILED", None, error="terminate: " + str(e)[-400:])
    threading.Thread(target=work, daemon=True).start()
    return {"ok": True}


@app.get("/api/v1/instances/{inst_id}/log")
def instance_log(inst_id: str, u=Depends(current_user)):
    i = owned(inst_id, u)
    if i["state"] in ("PROVISIONING", "WAITING_CAPACITY"):
        return {"log": i["status_note"] or "Creating the server (the first one downloads the image, 1-3 minutes)..."}
    if i["backend_name"].startswith("pending-"):
        return {"log": i["error"] or "(no server)"}
    return {"log": drv(i).log(i["backend_name"])}


_last_refresh = {"local": 0.0, "oracle": 0.0}


def refresh_states():
    now = time.time()
    if now - _last_refresh["local"] >= 3:
        _last_refresh["local"] = now
        try:
            live = driver.states()
        except DriverError:
            live = None
        if live is not None:
            for i in db.q("SELECT id,backend_name,state,ipv4 FROM instances WHERE backend='local' AND state IN ('RUNNING','STOPPED','FROZEN','UNKNOWN')"):
                s = live.get(i["backend_name"])
                state, ip = s if s else ("UNKNOWN", None)
                if state != i["state"] or ip != i["ipv4"]:
                    db.x("UPDATE instances SET state=?, ipv4=? WHERE id=?", (state, ip, i["id"]))
    if now - _last_refresh["oracle"] >= 30 and oracle.configured():
        _last_refresh["oracle"] = now
        for i in db.q("SELECT id,backend_name,state FROM instances WHERE backend='oracle' AND state IN ('RUNNING','STOPPED','UNKNOWN')"):
            try:
                s = oracle.state(i["backend_name"])
            except DriverError:
                continue
            if s == "TERMINATED":
                set_state(i["id"], "FAILED", None, error="This server was terminated in the Oracle website")
            elif s != i["state"]:
                db.x("UPDATE instances SET state=? WHERE id=?", (s, i["id"]))


# ---------------------------------------------------------------- API keys
class NewKey(BaseModel):
    name: str = Field(min_length=1, max_length=60)


@app.get("/api/v1/api-keys")
def keys(u=Depends(current_user)):
    return db.q("SELECT id,name,prefix,created,last_used FROM api_keys WHERE user_id=? ORDER BY id DESC", (u["id"],))


@app.post("/api/v1/api-keys")
def new_key(body: NewKey, u=Depends(current_user)):
    if db.one("SELECT COUNT(*) n FROM api_keys WHERE user_id=?", (u["id"],))["n"] >= 10:
        raise HTTPException(403, "10 API keys max. Delete one first.")
    tok = "mpc_" + secrets.token_urlsafe(32)
    db.x("INSERT INTO api_keys(user_id,name,prefix,token_hash,created) VALUES(?,?,?,?,?)",
         (u["id"], body.name.strip(), tok[:10], sha(tok), time.time()))
    db.log(u["id"], "apikey.create", body.name)
    return {"token": tok, "note": "Shown once. Use it as: Authorization: Bearer <token>"}


@app.delete("/api/v1/api-keys/{key_id}")
def del_key(key_id: int, u=Depends(current_user)):
    db.x("DELETE FROM api_keys WHERE id=? AND user_id=?", (key_id, u["id"]))
    db.log(u["id"], "apikey.delete", str(key_id))
    return {"ok": True}


# ---------------------------------------------------------------- audit
@app.get("/api/v1/audit")
def audit(u=Depends(current_user)):
    if u["is_admin"]:
        return db.q("SELECT a.*, u.email FROM audit a LEFT JOIN users u ON u.id=a.user_id ORDER BY a.id DESC LIMIT 200")
    return db.q("SELECT a.*, ? email FROM audit a WHERE user_id=? ORDER BY id DESC LIMIT 200", (u["email"], u["id"]))


# ---------------------------------------------------------------- admin
@app.get("/api/v1/admin/overview")
def admin_overview(u=Depends(admin_user)):
    tot = db.one("SELECT COUNT(*) n, COALESCE(SUM(ocpus),0) oc, COALESCE(SUM(mem_gb),0) mem, COALESCE(SUM(disk_gb),0) disk FROM instances")
    users = db.q("SELECT id,email,name,is_admin,ocpu_limit,mem_limit,disk_limit,inst_limit,created FROM users ORDER BY id")
    for x in users:
        x["usage"] = usage(x["id"])
        x["is_admin"] = bool(x["is_admin"])
    st = db.settings()
    st.pop("invite_code", None)
    st = {k: v for k, v in st.items() if not k.startswith("oci_")}
    return {"host": driver.host_info(), "allocated": tot, "oracle_ready": oracle.configured(), "users": users, "settings": st,
            "invite_set": bool(db.settings()["invite_code"]), "driver": driver.name,
            "driver_ok": driver.available()}


class OracleLogin(BaseModel):
    config: str = ""                    # the config text Oracle shows after Add API key
    key: str = ""                       # the private key file (.pem); empty = keep the saved one
    retry_s: Optional[int] = None


@app.get("/api/v1/admin/oracle")
def oracle_get(u=Depends(admin_user)):
    s = db.settings()
    return {"configured": oracle.configured(), "user": s.get("oci_user", ""), "tenancy": s.get("oci_tenancy", ""),
            "fingerprint": s.get("oci_fingerprint", ""), "region": s.get("oci_region", ""),
            "retry_s": int(s.get("oci_retry_s") or 60), "key_saved": os.path.exists(oracle.key_path())}


@app.put("/api/v1/admin/oracle")
def oracle_put(body: OracleLogin, u=Depends(admin_user)):
    cfg = {}
    for line in body.config.splitlines():
        m = re.match(r"^\s*([a-z_]+)\s*=\s*(.+?)\s*$", line, re.I)
        if m:
            cfg[m.group(1).lower()] = m.group(2)
    s = db.settings()
    vals = {k: cfg.get(k, s.get("oci_" + k, "")) for k in ("user", "tenancy", "fingerprint", "region")}
    miss = [k for k, v in vals.items() if not v]
    if miss:
        raise HTTPException(400, "The config text is missing: " + ", ".join(miss))
    key = body.key.strip()
    if key:
        if "ENCRYPTED" in key:
            raise HTTPException(400, "This key has a passphrase - make the API key again without one")
        if not re.search(r"BEGIN (RSA )?PRIVATE KEY", key):
            raise HTTPException(400, "That is not the private key (.pem) file")
    elif not os.path.exists(oracle.key_path()):
        raise HTTPException(400, "Add the private key (.pem) file")
    for k, v in oracle.save_login(vals["user"], vals["tenancy"], vals["fingerprint"], vals["region"], key).items():
        db.set_setting(k, v)
    if body.retry_s:
        db.set_setting("oci_retry_s", max(30, min(int(body.retry_s), 3600)))
    db.log(u["id"], "admin.oracle", vals["region"])
    return {"ok": True}


@app.post("/api/v1/admin/oracle/test")
def oracle_test(u=Depends(admin_user)):
    try:
        return oracle.test()
    except DriverError as e:
        raise HTTPException(502, str(e))


class UserPatch(BaseModel):
    ocpu_limit: Optional[float] = None
    mem_limit: Optional[float] = None
    disk_limit: Optional[int] = None
    inst_limit: Optional[int] = None
    is_admin: Optional[bool] = None


@app.patch("/api/v1/admin/users/{uid}")
def patch_user(uid: int, body: UserPatch, u=Depends(admin_user)):
    t = db.one("SELECT * FROM users WHERE id=?", (uid,))
    if not t:
        raise HTTPException(404, "No such account")
    for k, v in body.model_dump(exclude_none=True).items():
        if k == "is_admin" and uid == u["id"] and not v:
            raise HTTPException(400, "You can't remove your own admin")
        db.x("UPDATE users SET %s=? WHERE id=?" % k, (int(v) if k == "is_admin" else v, uid))
    db.log(u["id"], "admin.user", t["email"], body.model_dump(exclude_none=True))
    return {"ok": True}


@app.delete("/api/v1/admin/users/{uid}")
def delete_user(uid: int, u=Depends(admin_user)):
    if uid == u["id"]:
        raise HTTPException(400, "You can't delete yourself")
    if db.one("SELECT COUNT(*) n FROM instances WHERE user_id=?", (uid,))["n"]:
        raise HTTPException(409, "Terminate that account's servers first")
    t = db.one("SELECT email FROM users WHERE id=?", (uid,))
    db.x("DELETE FROM users WHERE id=?", (uid,))
    db.log(u["id"], "admin.user.delete", t["email"] if t else str(uid))
    return {"ok": True}


class SettingsPatch(BaseModel):
    signup: Optional[str] = None
    invite_code: Optional[str] = None
    region_label: Optional[str] = None
    public_host: Optional[str] = None
    free_ocpus: Optional[float] = None
    free_mem_gb: Optional[float] = None
    free_disk_gb: Optional[int] = None
    free_instances: Optional[int] = None


@app.patch("/api/v1/admin/settings")
def patch_settings(body: SettingsPatch, u=Depends(admin_user)):
    d = body.model_dump(exclude_none=True)
    if "signup" in d and d["signup"] not in ("open", "invite", "closed"):
        raise HTTPException(400, "signup: open, invite or closed")
    for k, v in d.items():
        db.set_setting(k, v)
    d.pop("invite_code", None)
    db.log(u["id"], "admin.settings", "", d)
    return {"ok": True}


# ---------------------------------------------------------------- errors + page
@app.exception_handler(HTTPException)
def http_err(request, exc):
    return JSONResponse({"error": exc.detail}, status_code=exc.status_code)


@app.middleware("http")
async def headers(request: Request, call_next):
    resp = await call_next(request)
    resp.headers["X-Content-Type-Options"] = "nosniff"
    resp.headers["Referrer-Policy"] = "no-referrer"
    resp.headers["X-Frame-Options"] = "DENY"
    if not request.url.path.startswith("/api/docs"):
        resp.headers["Content-Security-Policy"] = ("default-src 'self'; img-src 'self' data:; style-src 'self'; "
                                                   "script-src 'self'; connect-src 'self'; frame-ancestors 'none'")
    return resp


resume_oracle()

app.mount("/static", StaticFiles(directory=STATIC), name="static")


@app.get("/")
def index():
    return FileResponse(STATIC / "index.html")
