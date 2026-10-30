import os, sys, tempfile, time
os.environ["MP_CLOUD_DATA"] = tempfile.mkdtemp(prefix="mpc-test-")
os.environ["MP_CLOUD_DRIVER"] = "fake"
os.environ["MP_CLOUD_FAKE_DELAY"] = "0"
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from fastapi.testclient import TestClient  # noqa: E402
from mpcloud.app import app  # noqa: E402

H = {"X-MP-Cloud": "1"}
KEY = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIB7fK0exampleexampleexampleexample test@phone"


def client():
    return TestClient(app)


def wait_state(c, iid, want, t=5):
    end = time.time() + t
    while time.time() < end:
        s = c.get("/api/v1/instances/" + iid).json()["state"]
        if s == want:
            return s
        time.sleep(0.1)
    return s


def test_flow():
    admin = client()
    assert admin.get("/api/v1/public").json()["first_run"] is True
    r = admin.post("/api/v1/auth/signup", json={"email": "a@x.io", "name": "Admin", "password": "password1"}, headers=H)
    assert r.status_code == 200 and r.json()["admin"] is True
    me = admin.get("/api/v1/me").json()
    assert me["user"]["is_admin"] and me["user"]["ocpu_limit"] == 2

    # CSRF header is required for cookie mutations
    assert admin.post("/api/v1/api-keys", json={"name": "k"}).status_code == 403

    # second signup needs the invite code
    u2 = client()
    assert u2.post("/api/v1/auth/signup", json={"email": "b@x.io", "name": "B", "password": "password2"}, headers=H).status_code == 403
    assert admin.patch("/api/v1/admin/settings", json={"invite_code": "hello"}, headers=H).status_code == 200
    assert u2.post("/api/v1/auth/signup", json={"email": "b@x.io", "name": "B", "password": "password2", "invite": "hello"}, headers=H).status_code == 200
    assert u2.get("/api/v1/me").json()["user"]["is_admin"] is False
    assert u2.get("/api/v1/admin/overview").status_code == 403

    # create a local MP server
    body = {"name": "srv1", "placement": "local", "ocpus": 2, "mem_gb": 8, "disk_gb": 50, "ssh_key": KEY, "preset": "mp", "mp_region": "asia"}
    r = u2.post("/api/v1/instances", json=body, headers=H)
    assert r.status_code == 200, r.text
    iid = r.json()["id"]
    assert wait_state(u2, iid, "RUNNING") == "RUNNING"
    i = u2.get("/api/v1/instances/" + iid).json()
    assert i["ssh"].startswith("ssh -p ") and any(p["private"] == 1883 for p in i["ports"])

    # quota: no OCPU left
    r = u2.post("/api/v1/instances", json=dict(body, name="srv2", ocpus=1, mem_gb=2, preset="plain"), headers=H)
    assert r.status_code in (400, 403) and "error" in r.json()

    # other user cannot see it
    assert admin.get("/api/v1/instances/" + iid).status_code in (200, 404)
    assert len(admin.get("/api/v1/instances").json()) == 0

    # admin MP server would clash on the 1:1 MP ports
    r = admin.post("/api/v1/instances", json=dict(body, name="srv3", ocpus=1, mem_gb=4), headers=H)
    assert r.status_code == 409, r.text

    # actions
    assert u2.post("/api/v1/instances/%s/action" % iid, json={"action": "stop"}, headers=H).status_code == 200
    assert wait_state(u2, iid, "STOPPED") == "STOPPED"
    assert u2.post("/api/v1/instances/%s/action" % iid, json={"action": "start"}, headers=H).status_code == 200
    assert wait_state(u2, iid, "RUNNING") == "RUNNING"
    assert u2.post("/api/v1/instances/%s/action" % iid, json={"action": "explode"}, headers=H).status_code == 400
    assert "log" in u2.get("/api/v1/instances/%s/log" % iid).json()

    # bad ssh key
    r = admin.post("/api/v1/instances", json=dict(body, name="bad", preset="plain", ocpus=1, mem_gb=1, ssh_key="hello"), headers=H)
    assert r.status_code == 400

    # oracle placement without account
    r = admin.post("/api/v1/instances", json=dict(body, name="oc", placement="oracle", shape="VM.Standard.A1.Flex", preset="plain", ocpus=1, mem_gb=6), headers=H)
    assert r.status_code == 409

    # API key (Bearer, no CSRF header needed)
    tok = u2.post("/api/v1/api-keys", json={"name": "ci"}, headers=H).json()["token"]
    bare = client()
    lst = bare.get("/api/v1/instances", headers={"Authorization": "Bearer " + tok}).json()
    assert [x["id"] for x in lst] == [iid]
    assert bare.get("/api/v1/instances", headers={"Authorization": "Bearer mpc_wrong"}).status_code == 401

    # admin raises quota
    uid = u2.get("/api/v1/me").json()["user"]["id"]
    assert admin.patch("/api/v1/admin/users/%d" % uid, json={"ocpu_limit": 4}, headers=H).status_code == 200
    assert u2.get("/api/v1/me").json()["user"]["ocpu_limit"] == 4

    # terminate
    assert u2.delete("/api/v1/instances/" + iid, headers=H).status_code == 200
    end = time.time() + 5
    while time.time() < end and u2.get("/api/v1/instances/" + iid).status_code != 404:
        time.sleep(0.1)
    assert u2.get("/api/v1/instances/" + iid).status_code == 404
    assert any(a["action"].startswith("instance") for a in admin.get("/api/v1/audit").json())

    # page + static served
    assert "MP Cloud" in bare.get("/").text
    assert bare.get("/static/app.js").status_code == 200
