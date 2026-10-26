"""cloud-init for new MP Cloud servers (Oracle calls this "user data") + the one-click presets."""
import json

SETUP = "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/vps-setup.sh"

# MP region -> DuckDNS name (same as vps-setup.sh / oracle/mp_oracle.py)
MP_REGIONS = {
    "asia": {"label": "Asia", "duck": "mpsamp"},
    "europe": {"label": "Europe", "duck": "eusamp"},
    "na": {"label": "North America", "duck": "nasamp"},
    "oceania": {"label": "Oceania", "duck": "ocsamp"},
}
# the MP ports (HiveMQ README)
MP_TCP = [80, 1883, 8000, 8883, 8884, 7681]
MP_UDP = [1922, 1930, 1931, 1932]

# one-click presets. ports = what must be reachable from the internet
#   (Oracle: opened in the security list + iptables; local: forwarded from this machine's IP)
PRESETS = {
    "plain": {"label": "Plain server", "desc": "Empty Ubuntu / Debian with SSH", "min_mem": 0.5,
              "tcp": [], "udp": []},
    "mp": {"label": "MP server", "desc": "HiveMQ + Java 21 + bridge + the 24/7 MP servers (vps-setup.sh)",
           "min_mem": 4, "tcp": MP_TCP, "udp": MP_UDP},
    "coolify": {"label": "Coolify", "desc": "Open-source app platform (like Heroku / Vercel), dashboard on :8000",
                "min_mem": 2, "tcp": [80, 443, 8000, 6001, 6002], "udp": [],
                "script": "curl -fsSL https://cdn.coollabs.io/coolify/install.sh | bash"},
    "dokploy": {"label": "Dokploy", "desc": "Open-source deploy panel (Docker / Swarm), dashboard on :3000",
                "min_mem": 2, "tcp": [80, 443, 3000], "udp": [],
                "script": "curl -sSL https://dokploy.com/install.sh | sh"},
}
# everything the Oracle security list opens once for all MP Cloud servers
ALL_OPEN_PORTS = sorted({("tcp", 22)} | {("tcp", p) for v in PRESETS.values() for p in v["tcp"]}
                        | {("udp", p) for v in PRESETS.values() for p in v["udp"]})


def user_data(ssh_key=None, preset="plain", mp_region=None, duckdns_token="", on_oracle=False):
    """#cloud-config: user "opc" (like Oracle) with your SSH key + sudo, SSH server, then the preset."""
    p = PRESETS[preset or "plain"]
    user = {"name": "opc", "shell": "/bin/bash", "groups": ["sudo"],
            "sudo": "ALL=(ALL) NOPASSWD:ALL", "lock_passwd": True}
    if ssh_key:
        user["ssh_authorized_keys"] = [ssh_key.strip()]
    cfg = {
        "package_update": True,
        "packages": ["openssh-server", "curl", "ca-certificates", "cron"],
        "users": ["default", user],
        "ssh_pwauth": False,
        "runcmd": [["systemctl", "enable", "--now", "ssh"]],
    }
    script = ["set -x"]
    if on_oracle and (p["tcp"] or p["udp"]):
        # Oracle's Ubuntu images drop everything but SSH in iptables: open this preset's ports
        script += ["iptables -I INPUT -p tcp --dport %d -j ACCEPT" % n for n in p["tcp"]]
        script += ["iptables -I INPUT -p udp --dport %d -j ACCEPT" % n for n in p["udp"]]
        script.append("command -v netfilter-persistent >/dev/null && netfilter-persistent save || true")
    if preset == "mp":
        duck = MP_REGIONS[mp_region]["duck"]
        tok = (duckdns_token or "").strip()
        if tok:
            url = "https://www.duckdns.org/update?domains=%s&token=%s&ip=" % (duck, tok)
            script += [
                "echo '*/5 * * * * root curl -fsS \"%s\" >/dev/null' > /etc/cron.d/duckdns" % url,
                "chmod 600 /etc/cron.d/duckdns",
                "curl -fsS '%s' || true" % url,
            ]
        script.append("curl -fsSL %s | bash -s %s" % (SETUP, mp_region))
    elif p.get("script"):
        script.append(p["script"])
    if len(script) > 1:
        cfg["write_files"] = [{"path": "/root/mp-setup.sh", "permissions": "0700",
                               "content": "#!/bin/bash\n" + "\n".join(script) + "\n"}]
        cfg["runcmd"].append(["bash", "-c", "/root/mp-setup.sh > /root/mp-setup.log 2>&1"])
    # JSON is valid YAML: no PyYAML needed
    return "#cloud-config\n" + json.dumps(cfg, indent=1) + "\n"
