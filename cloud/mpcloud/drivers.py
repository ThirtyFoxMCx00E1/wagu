"""Where the servers really run.

IncusDriver  - real servers: Incus system containers (or VMs) on this machine. Incus is the
               light, open-source LXD fork; it runs fine on the Oracle Ampere A1 (ARM) server,
               any VPS or a spare PC with Debian 12 / Ubuntu.
FakeDriver   - pretend servers for trying the console on a laptop / in tests (MP_CLOUD_DRIVER=fake).
"""
import json
import os
import shutil
import subprocess
import threading
import time

IMAGES = {
    "debian-12": {"label": "Debian 12 (bookworm)", "incus": "images:debian/12/cloud"},
    "ubuntu-24.04": {"label": "Ubuntu 24.04 LTS", "incus": "images:ubuntu/24.04/cloud"},
    "ubuntu-22.04": {"label": "Ubuntu 22.04 LTS", "incus": "images:ubuntu/22.04/cloud"},
}


class DriverError(Exception):
    pass


def host_info_generic():
    cpus = os.cpu_count() or 1
    mem_gb = 0.0
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    mem_gb = int(line.split()[1]) / 1024 / 1024
                    break
    except OSError:
        pass
    disk = shutil.disk_usage("/")
    return {"cpus": cpus, "mem_gb": round(mem_gb, 1), "disk_free_gb": round(disk.free / 1e9, 1),
            "arch": os.uname().machine}


class IncusDriver:
    name = "incus"

    def __init__(self, binary=None, use_vm=False, storage_pool=None):
        self.bin = binary or shutil.which("incus") or "incus"
        self.use_vm = use_vm
        self.pool = storage_pool

    def _run(self, *args, input=None, timeout=900):
        try:
            p = subprocess.run([self.bin, *args], input=input, capture_output=True, text=True, timeout=timeout)
        except FileNotFoundError:
            raise DriverError("incus is not installed (run cloud/install.sh)")
        except subprocess.TimeoutExpired:
            raise DriverError("incus %s timed out" % args[0])
        if p.returncode != 0:
            raise DriverError((p.stderr or p.stdout).strip()[-800:] or "incus %s failed" % args[0])
        return p.stdout

    def available(self):
        try:
            self._run("info", timeout=20)
            return True
        except DriverError:
            return False

    def create(self, name, image, ocpus, mem_gb, disk_gb, user_data, ports):
        img = IMAGES[image]["incus"]
        args = ["launch", img, name,
                "-c", "limits.cpu=%d" % max(1, int(round(ocpus))),
                "-c", "limits.memory=%dMiB" % int(mem_gb * 1024),
                "-c", "security.nesting=true",          # Java / systemd / iptables inside work
                "-c", "boot.autostart=true",            # back up after a host reboot (24/7)
                "-c", "cloud-init.user-data=" + user_data,
                "-d", "root,size=%dGiB" % int(disk_gb)]
        if self.pool:
            args += ["-s", self.pool]
        if self.use_vm:
            args.append("--vm")
        self._run(*args, timeout=1800)
        for i, (proto, host_port, guest_port) in enumerate(ports):
            self._run("config", "device", "add", name, "p%s%d" % (proto, host_port), "proxy",
                      "listen=%s:0.0.0.0:%d" % (proto, host_port),
                      "connect=%s:127.0.0.1:%d" % (proto, guest_port))

    def action(self, name, action):
        if action == "start":
            self._run("start", name)
        elif action == "stop":
            self._run("stop", name, "--timeout", "60")
        elif action == "reboot":
            self._run("restart", name, "--timeout", "60")
        else:
            raise DriverError("unknown action")

    def delete(self, name):
        try:
            self._run("delete", name, "--force")
        except DriverError as e:
            if "not found" not in str(e).lower():
                raise

    def states(self):
        """backend name -> (STATE, private IPv4)"""
        out = {}
        for c in json.loads(self._run("list", "--format", "json", timeout=60) or "[]"):
            ip = None
            for nic, n in ((c.get("state") or {}).get("network") or {}).items():
                if nic == "lo":
                    continue
                for a in n.get("addresses") or []:
                    if a.get("family") == "inet" and a.get("scope") == "global":
                        ip = ip or a.get("address")
            out[c["name"]] = ((c.get("status") or "unknown").upper(), ip)
        return out

    def log(self, name):
        try:
            return self._run("exec", name, "--", "sh", "-c",
                             "tail -n 200 /root/mp-setup.log 2>/dev/null || tail -n 200 /var/log/cloud-init-output.log",
                             timeout=30)
        except DriverError as e:
            return "(no log yet: %s)" % e

    def host_info(self):
        return host_info_generic()


class FakeDriver:
    """Same interface, nothing real: lets you click through the console anywhere."""
    name = "fake"

    def __init__(self, delay=1.5):
        self.delay = delay
        self.lock = threading.Lock()
        self.vms = {}

    def available(self):
        return True

    def create(self, name, image, ocpus, mem_gb, disk_gb, user_data, ports):
        time.sleep(self.delay)
        with self.lock:
            n = len(self.vms) + 10
            self.vms[name] = {"state": "RUNNING", "ip": "10.42.0.%d" % n, "log": user_data}

    def action(self, name, action):
        with self.lock:
            if name not in self.vms:
                raise DriverError("not found")
            self.vms[name]["state"] = "STOPPED" if action == "stop" else "RUNNING"

    def delete(self, name):
        with self.lock:
            self.vms.pop(name, None)

    def states(self):
        with self.lock:
            return {k: (v["state"], v["ip"] if v["state"] == "RUNNING" else None) for k, v in self.vms.items()}

    def log(self, name):
        with self.lock:
            v = self.vms.get(name)
            return ("[fake] cloud-init user-data this server got:\n\n" + v["log"]) if v else "(gone)"

    def host_info(self):
        h = host_info_generic()
        h["cpus"], h["mem_gb"] = max(h["cpus"], 4), max(h["mem_gb"], 24.0)
        return h


def make_driver():
    kind = os.environ.get("MP_CLOUD_DRIVER", "incus").lower()
    if kind == "fake":
        return FakeDriver(float(os.environ.get("MP_CLOUD_FAKE_DELAY", "1.5")))
    return IncusDriver(use_vm=os.environ.get("MP_CLOUD_VM") == "1",
                       storage_pool=os.environ.get("MP_CLOUD_POOL") or None)
