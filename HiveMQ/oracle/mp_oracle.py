#!/usr/bin/env python3
"""MP Oracle: our own Oracle Cloud Always Free claimer for the MP region servers.

One command does everything the forks (oracle-freetier-instance-creation, oracle-cloud-free-arm-instance,
oci-claimer) do, plus the MP part:
  1. checks the OCI login (API key) and shows the home region + what is already used
  2. creates the network once: VCN mp-vcn, internet gateway, route, public subnet and a security list
     with every MP port open (TCP 22 80 1883 8000 8883 8884 7681, UDP 1922 1930 1931 1932)
  3. asks for the free Ampere A1 server (2 OCPU / 12 GB, Ubuntu 24.04 arm64, public IP) and keeps
     asking on "Out of host capacity" (every availability domain, slows down on 429 TooManyRequests)
  4. the server installs itself on first boot (cloud-init): DuckDNS update, iptables ports open,
     then vps-setup.sh <region> = HiveMQ + Java 21 + bridge + the 24/7 servers (Mega Vil on UDP 1932)
  5. prints the public IP, points DuckDNS at it (if DUCKDNS_TOKEN is set) and tells Discord

Usage (any Linux / Termux with python3, or the GitHub Actions workflow "MP Oracle claim"):
  pip install oci
  python3 oracle/mp_oracle.py check
  python3 oracle/mp_oracle.py claim --region europe            # keeps trying until it gets the server
  python3 oracle/mp_oracle.py claim --region europe --minutes 50   # stop after 50 min (Actions)
  python3 oracle/mp_oracle.py status
  python3 oracle/mp_oracle.py claim --dry-run                  # show what it would create

Login: ~/.oci/config (OCI_CONFIG=path) or the environment (the Actions secrets):
  OCI_USER, OCI_TENANCY, OCI_FINGERPRINT, OCI_REGION, OCI_KEY (the private key .pem text)
Optional: SSH_PUBLIC_KEY, DUCKDNS_TOKEN, DISCORD_WEBHOOK, MP_REGION, MP_OCPUS (2), MP_MEMORY_GB (12)

Always Free rules (Oracle docs): free servers only in the account's home region, 2 OCPU + 12 GB of
A1 in total, one account per person. So one account = one MP region; the other regions need their
own owner's account or a paid VPS (vps-setup.sh works on any Ubuntu / Debian 12 VPS).
"""
import argparse
import base64
import json
import os
import sys
import tempfile
import time
import urllib.parse
import urllib.request

PREFIX = "mp"
SHAPE = "VM.Standard.A1.Flex"
TCP_PORTS = [22, 80, 1883, 8000, 8883, 8884, 7681]
UDP_PORTS = [1922, 1930, 1931, 1932]
# MP region -> DuckDNS name (same as vps-setup.sh)
DUCK = {"asia": "mpsamp", "europe": "eusamp", "na": "nasamp", "oceania": "ocsamp"}
# low-ping home region to pick at Oracle sign-up for each MP region
BEST = {"asia": "ap-singapore-1 (Singapore)", "europe": "eu-frankfurt-1 (Frankfurt)",
        "na": "us-ashburn-1 (Ashburn, Virginia)", "oceania": "ap-sydney-1 (Sydney)"}
SETUP = "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh"


def say(msg):
    print("\033[1;36m[mp-oracle]\033[0m " + msg, flush=True)


def warn(msg):
    print("\033[1;33m[mp-oracle]\033[0m " + msg, flush=True)


def die(msg, code=1):
    print("\033[1;31m[mp-oracle]\033[0m " + msg, file=sys.stderr, flush=True)
    sys.exit(code)


def mp_region_for(oci_region):
    r = (oci_region or "").lower()
    if r.startswith(("eu-", "uk-", "me-", "af-", "il-")):
        return "europe"
    if r.startswith(("us-", "ca-", "mx-", "sa-")):
        return "na"
    if r in ("ap-sydney-1", "ap-melbourne-1") or r.startswith("ap-auckland"):
        return "oceania"
    return "asia"


def norm_region(r):
    r = (r or "").strip().lower()
    alias = {"as": "asia", "eu": "europe", "us": "na", "north america": "na", "oc": "oceania", "au": "oceania"}
    r = alias.get(r, r)
    if r and r not in DUCK:
        die("region must be asia, europe, na or oceania (got %s)" % r)
    return r


def load_config():
    try:
        import oci  # noqa: F401
    except ImportError:
        die("the Oracle SDK is missing: pip install oci")
    import oci
    if os.environ.get("OCI_USER") and os.environ.get("OCI_KEY"):
        key = os.environ["OCI_KEY"].replace("\\n", "\n").strip() + "\n"
        kf = tempfile.NamedTemporaryFile("w", delete=False, suffix=".pem")
        kf.write(key)
        kf.close()
        os.chmod(kf.name, 0o600)
        cfg = {"user": os.environ["OCI_USER"].strip(), "tenancy": os.environ.get("OCI_TENANCY", "").strip(),
               "fingerprint": os.environ.get("OCI_FINGERPRINT", "").strip(),
               "region": os.environ.get("OCI_REGION", "").strip(), "key_file": kf.name}
    else:
        path = os.path.expanduser(os.environ.get("OCI_CONFIG", "~/.oci/config"))
        if not os.path.exists(path):
            die("no login: put the API key config in %s (Profile -> API keys -> Add API key -> copy the "
                "config) or set OCI_USER / OCI_TENANCY / OCI_FINGERPRINT / OCI_REGION / OCI_KEY" % path)
        cfg = oci.config.from_file(path, os.environ.get("OCI_PROFILE", "DEFAULT"))
    try:
        oci.config.validate_config(cfg)
    except Exception as e:  # InvalidConfig lists the missing / wrong keys
        die("the OCI login is not complete: %s" % e)
    return cfg


class Oracle:
    def __init__(self, cfg):
        import oci
        self.oci = oci
        self.cfg = cfg
        self.tenancy = cfg["tenancy"]
        retry = oci.retry.NoneRetryStrategy()
        self.idn = oci.identity.IdentityClient(cfg, retry_strategy=retry)
        self.cmp = oci.core.ComputeClient(cfg, retry_strategy=retry)
        self.net = oci.core.VirtualNetworkClient(cfg, retry_strategy=retry)

    # ------------------------------------------------------------ info
    def home_region(self):
        try:
            subs = self.idn.list_region_subscriptions(self.tenancy).data
            for s in subs:
                if s.is_home_region:
                    return s.region_name
        except Exception:
            pass
        return self.cfg.get("region")

    def ads(self):
        return [a.name for a in self.idn.list_availability_domains(self.tenancy).data]

    def instances(self):
        out = []
        for i in self.oci.pagination.list_call_get_all_results(self.cmp.list_instances, self.tenancy).data:
            if i.lifecycle_state not in ("TERMINATED", "TERMINATING"):
                out.append(i)
        return out

    def a1_used(self):
        oc = mem = 0.0
        for i in self.instances():
            if i.shape == SHAPE and i.shape_config:
                oc += i.shape_config.ocpus or 0
                mem += i.shape_config.memory_in_gbs or 0
        return oc, mem

    def public_ip(self, inst):
        for va in self.cmp.list_vnic_attachments(self.tenancy, instance_id=inst.id).data:
            if va.lifecycle_state == "ATTACHED":
                v = self.net.get_vnic(va.vnic_id).data
                if v.public_ip:
                    return v.public_ip
        return None

    # ------------------------------------------------------------ network (once)
    def _find(self, items, name):
        for x in items:
            if x.display_name == name and getattr(x, "lifecycle_state", "AVAILABLE") not in ("TERMINATED", "TERMINATING"):
                return x
        return None

    def security_rules(self):
        m = self.oci.core.models
        rules = []
        for p in TCP_PORTS:
            rules.append(m.IngressSecurityRule(protocol="6", source="0.0.0.0/0", description="MP tcp %d" % p,
                                               tcp_options=m.TcpOptions(destination_port_range=m.PortRange(min=p, max=p))))
        for p in UDP_PORTS:
            rules.append(m.IngressSecurityRule(protocol="17", source="0.0.0.0/0", description="MP udp %d" % p,
                                               udp_options=m.UdpOptions(destination_port_range=m.PortRange(min=p, max=p))))
        # ICMP 3,4 (path MTU) + ping, so the mod's ping test works
        rules.append(m.IngressSecurityRule(protocol="1", source="0.0.0.0/0", icmp_options=m.IcmpOptions(type=3, code=4)))
        rules.append(m.IngressSecurityRule(protocol="1", source="0.0.0.0/0", icmp_options=m.IcmpOptions(type=8)))
        return rules

    def network(self, dry=False):
        m = self.oci.core.models
        vcn = self._find(self.net.list_vcns(self.tenancy).data, PREFIX + "-vcn")
        if dry:
            say("network: %s" % ("mp-vcn exists" if vcn else "would create mp-vcn 10.42.0.0/16 + gateway + subnet 10.42.0.0/24"))
            say("ports: TCP %s, UDP %s" % (" ".join(map(str, TCP_PORTS)), " ".join(map(str, UDP_PORTS))))
            return None
        if not vcn:
            say("creating the network mp-vcn")
            vcn = self.net.create_vcn(m.CreateVcnDetails(compartment_id=self.tenancy, cidr_blocks=["10.42.0.0/16"],
                                                         display_name=PREFIX + "-vcn", dns_label="mpvcn")).data
            vcn = self.oci.wait_until(self.net, self.net.get_vcn(vcn.id), "lifecycle_state", "AVAILABLE").data
        igw = self._find(self.net.list_internet_gateways(self.tenancy, vcn_id=vcn.id).data, PREFIX + "-igw")
        if not igw:
            say("creating the internet gateway")
            igw = self.net.create_internet_gateway(m.CreateInternetGatewayDetails(
                compartment_id=self.tenancy, vcn_id=vcn.id, is_enabled=True, display_name=PREFIX + "-igw")).data
        self.net.update_route_table(vcn.default_route_table_id, m.UpdateRouteTableDetails(route_rules=[
            m.RouteRule(destination="0.0.0.0/0", destination_type="CIDR_BLOCK", network_entity_id=igw.id)]))
        self.net.update_security_list(vcn.default_security_list_id, m.UpdateSecurityListDetails(
            ingress_security_rules=self.security_rules(),
            egress_security_rules=[m.EgressSecurityRule(destination="0.0.0.0/0", protocol="all")]))
        sub = self._find(self.net.list_subnets(self.tenancy, vcn_id=vcn.id).data, PREFIX + "-subnet")
        if not sub:
            say("creating the public subnet")
            sub = self.net.create_subnet(m.CreateSubnetDetails(
                compartment_id=self.tenancy, vcn_id=vcn.id, cidr_block="10.42.0.0/24", display_name=PREFIX + "-subnet",
                dns_label="mpsub", route_table_id=vcn.default_route_table_id,
                security_list_ids=[vcn.default_security_list_id], prohibit_public_ip_on_vnic=False)).data
            sub = self.oci.wait_until(self.net, self.net.get_subnet(sub.id), "lifecycle_state", "AVAILABLE").data
        say("network ready (ports open: TCP %s, UDP %s)" % (" ".join(map(str, TCP_PORTS)), " ".join(map(str, UDP_PORTS))))
        return sub.id

    # ------------------------------------------------------------ image
    def image(self):
        for ver in ("24.04", "22.04"):
            imgs = self.cmp.list_images(self.tenancy, operating_system="Canonical Ubuntu", operating_system_version=ver,
                                        shape=SHAPE, sort_by="TIMECREATED", sort_order="DESC").data
            imgs = [i for i in imgs if "aarch64" in (i.display_name or "") or "arm" in (i.display_name or "").lower()] or imgs
            if imgs:
                return imgs[0]
        die("no Ubuntu arm64 image found for %s in this region" % SHAPE)


def user_data(region):
    """cloud-init for the new server: DuckDNS, iptables, vps-setup.sh <region>."""
    duck = DUCK[region]
    token = os.environ.get("DUCKDNS_TOKEN", "").strip()
    tcp = " ".join(map(str, TCP_PORTS))
    udp = " ".join(map(str, UDP_PORTS))
    lines = [
        "#!/bin/bash",
        "# MP Oracle first boot: %s server (%s.duckdns.org)" % (region, duck),
        "exec > /root/mp-oracle-boot.log 2>&1",
        "set -x",
    ]
    if token:
        lines += [
            "echo '*/5 * * * * root curl -fsS \"https://www.duckdns.org/update?domains=%s&token=%s&ip=\" >/dev/null' > /etc/cron.d/duckdns" % (duck, token),
            "chmod 600 /etc/cron.d/duckdns",
            "for i in 1 2 3; do curl -fsS 'https://www.duckdns.org/update?domains=%s&token=%s&ip=' && break; sleep 10; done" % (duck, token),
            "sleep 60",
        ]
    lines += [
        "# Oracle Ubuntu images block everything but SSH in iptables: open the MP ports",
        "for p in %s; do iptables -I INPUT -p tcp --dport $p -j ACCEPT; done" % tcp,
        "for p in %s; do iptables -I INPUT -p udp --dport $p -j ACCEPT; done" % udp,
        "command -v netfilter-persistent >/dev/null && netfilter-persistent save || true",
        "curl -fsSL %s | bash -s %s" % (SETUP, region),
    ]
    return "\n".join(lines) + "\n"


def notify(text):
    hook = os.environ.get("DISCORD_WEBHOOK", "").strip()
    if not hook:
        return
    try:
        req = urllib.request.Request(hook, data=json.dumps({"content": text}).encode(),
                                     headers={"Content-Type": "application/json", "User-Agent": "mp-oracle"})
        urllib.request.urlopen(req, timeout=15).read()
    except Exception as e:
        warn("Discord message failed: %s" % e)


def duck_update(region, ip):
    token = os.environ.get("DUCKDNS_TOKEN", "").strip()
    if not token:
        warn("no DUCKDNS_TOKEN: set %s.duckdns.org to %s yourself at duckdns.org" % (DUCK[region], ip))
        return
    q = urllib.parse.urlencode({"domains": DUCK[region], "token": token, "ip": ip})
    try:
        r = urllib.request.urlopen("https://www.duckdns.org/update?" + q, timeout=15).read().decode().strip()
        say("DuckDNS %s.duckdns.org -> %s: %s" % (DUCK[region], ip, r))
    except Exception as e:
        warn("DuckDNS update failed: %s" % e)


def github_output(**kv):
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a") as f:
            for k, v in kv.items():
                f.write("%s=%s\n" % (k, v))


def cmd_check(o, args):
    home = o.home_region()
    reg = norm_region(args.region) or mp_region_for(home)
    say("login ok - tenancy %s..." % o.tenancy[:24])
    say("home region: %s  (MP region %s, DuckDNS %s.duckdns.org)" % (home, reg, DUCK[reg]))
    if mp_region_for(home) != reg:
        warn("the home region %s is far from %s players: for %s pick %s when you make the account"
             % (home, reg, reg, BEST[reg]))
    oc, mem = o.a1_used()
    say("Ampere A1 used: %g OCPU / %g GB of the free 2 OCPU / 12 GB" % (oc, mem))
    say("availability domains: %s" % ", ".join(o.ads()))
    return reg


def cmd_status(o, args):
    found = False
    for i in o.instances():
        if i.display_name.startswith(PREFIX + "-"):
            found = True
            ip = o.public_ip(i) if i.lifecycle_state == "RUNNING" else None
            say("%s  %s  %s  %s" % (i.display_name, i.lifecycle_state, i.shape, ip or "-"))
            if ip:   # 3.32: the MP Oracle page shows it
                github_output(ip=ip, region=i.display_name[len(PREFIX) + 1:], created="false")
    if not found:
        say("no MP server yet: python3 oracle/mp_oracle.py claim")


def cmd_claim(o, args):
    reg = cmd_check(o, args)
    name = "%s-%s" % (PREFIX, reg)
    for i in o.instances():
        if i.display_name == name:
            ip = o.public_ip(i) if i.lifecycle_state == "RUNNING" else None
            say("%s already exists (%s) %s" % (name, i.lifecycle_state, ip or ""))
            if ip:
                duck_update(reg, ip)
                github_output(ip=ip, region=reg, created="false")
            return 0
    ocpus = float(os.environ.get("MP_OCPUS", args.ocpus))
    mem = float(os.environ.get("MP_MEMORY_GB", args.memory))
    used_oc, used_mem = o.a1_used()
    if used_oc + ocpus > 2 or used_mem + mem > 12:
        die("not enough free A1 left (%g OCPU / %g GB used): delete the old A1 server or lower MP_OCPUS / MP_MEMORY_GB"
            % (used_oc, used_mem), 3)
    img = o.image()
    say("image: %s" % img.display_name)
    say("server: %s %g OCPU / %g GB, %d GB disk, public IP, then vps-setup.sh %s" % (SHAPE, ocpus, mem, args.disk, reg))
    if args.dry_run:
        o.network(dry=True)
        print(user_data(reg).replace(os.environ.get("DUCKDNS_TOKEN", "") or "\0", "<DUCKDNS_TOKEN>"))
        return 0
    subnet = o.network()
    m = o.oci.core.models
    meta = {"user_data": base64.b64encode(user_data(reg).encode()).decode()}
    ssh = os.environ.get("SSH_PUBLIC_KEY", "").strip()
    if not ssh and os.path.exists(os.path.expanduser("~/.ssh/id_ed25519.pub")):
        ssh = open(os.path.expanduser("~/.ssh/id_ed25519.pub")).read().strip()
    if ssh:
        meta["ssh_authorized_keys"] = ssh
    else:
        warn("no SSH_PUBLIC_KEY: you will use the web console (https://%s.duckdns.org:7681) instead of SSH" % DUCK[reg])
    ads = o.ads()
    end = time.time() + args.minutes * 60 if args.minutes else None
    wait, tries = args.wait, 0
    say("asking Oracle for the server (keeps trying on 'Out of host capacity'%s)"
        % (", up to %d min" % args.minutes if args.minutes else ""))
    while True:
        for ad in ads:
            tries += 1
            det = m.LaunchInstanceDetails(
                availability_domain=ad, compartment_id=o.tenancy, display_name=name, shape=SHAPE,
                shape_config=m.LaunchInstanceShapeConfigDetails(ocpus=ocpus, memory_in_gbs=mem),
                source_details=m.InstanceSourceViaImageDetails(image_id=img.id, boot_volume_size_in_gbs=args.disk),
                create_vnic_details=m.CreateVnicDetails(subnet_id=subnet, assign_public_ip=True, display_name=name + "-vnic"),
                metadata=meta, freeform_tags={"mp": reg})
            try:
                inst = o.cmp.launch_instance(det).data
            except o.oci.exceptions.ServiceError as e:
                txt = "%s %s %s" % (e.status, e.code, e.message)
                if e.status == 429:
                    wait = min(wait * 2, 300)
                    warn("try %d %s: too many requests, waiting %ds" % (tries, ad, wait))
                    time.sleep(wait)
                elif "capacity" in (e.message or "").lower() or e.status in (500, 503):
                    print("  try %d %s: out of capacity" % (tries, ad), flush=True)
                    wait = args.wait
                elif e.code == "LimitExceeded" or "limit" in (e.message or "").lower():
                    die("Oracle says the free limit is used: %s" % txt, 3)
                else:
                    die("Oracle error: %s" % txt, 2)
                continue
            say("got it: %s in %s, starting..." % (name, ad))
            inst = o.oci.wait_until(o.cmp, o.cmp.get_instance(inst.id), "lifecycle_state", "RUNNING",
                                    max_wait_seconds=900).data
            ip = None
            for _ in range(30):
                ip = o.public_ip(inst)
                if ip:
                    break
                time.sleep(10)
            say("RUNNING - public IP %s" % ip)
            if ip:
                duck_update(reg, ip)
            say("it installs HiveMQ + the servers by itself now (about 10-15 min). Log on the server: /root/mp-oracle-boot.log")
            notify("MP Oracle: %s server is up - IP %s (%s.duckdns.org). Installing HiveMQ + Mega Vil now, about 15 min."
                   % (reg.upper(), ip, DUCK[reg]))
            github_output(ip=ip or "", region=reg, created="true")
            return 0
        if end and time.time() + wait > end:
            say("no capacity yet after %d tries - the next run tries again" % tries)
            github_output(ip="", region=reg, created="false")
            return 0
        time.sleep(wait)


def main():
    ap = argparse.ArgumentParser(description="MP Oracle: claim the Oracle Always Free A1 server for an MP region")
    ap.add_argument("command", choices=["check", "claim", "status"])
    ap.add_argument("--region", default=os.environ.get("MP_REGION", ""), help="asia, europe, na or oceania (default: from the home region)")
    ap.add_argument("--ocpus", type=float, default=2)
    ap.add_argument("--memory", type=float, default=12)
    ap.add_argument("--disk", type=int, default=100, help="boot disk GB (free: 200 GB in total)")
    ap.add_argument("--wait", type=int, default=60, help="seconds between rounds (do not go below 30)")
    ap.add_argument("--minutes", type=int, default=0, help="stop after N minutes (0 = forever)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    args.wait = max(30, args.wait)
    o = Oracle(load_config())
    sys.exit({"check": lambda: cmd_check(o, args) and 0, "claim": lambda: cmd_claim(o, args),
              "status": lambda: cmd_status(o, args)}[args.command]() or 0)


if __name__ == "__main__":
    main()
