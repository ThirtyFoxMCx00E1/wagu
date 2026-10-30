"""Real Oracle Cloud servers through the OCI API (the same API the Oracle website uses).

The admin saves ONE Oracle API key in MP Cloud (Admin -> Oracle account). Every MP Cloud account's
"Oracle" servers are then created in that tenancy, with MP Cloud's own free-tier quotas on top.

Launching works like oci-instance-creator / hitrov oci-arm-host-capacity: every availability domain
is tried, and on "Out of host capacity" the server waits (state WAITING_CAPACITY) and tries again
every N seconds until Oracle has room - no need to click Create again and again.
"""
import base64
import os
import threading
import time

from .drivers import DriverError
from .userdata import ALL_OPEN_PORTS

PREFIX = "mpc"
SHAPES = {
    "VM.Standard.A1.Flex": {"label": "Ampere A1 Flex (ARM)", "flex": True, "always_free": True},
    "VM.Standard.E2.1.Micro": {"label": "AMD E2.1 Micro (1/8 OCPU, 1 GB)", "flex": False, "always_free": True,
                               "ocpus": 1, "mem_gb": 1},
}
UBUNTU = {"ubuntu-24.04": "24.04", "ubuntu-22.04": "22.04"}


class OCIDriver:
    name = "oracle"

    def __init__(self, data_dir, get_settings):
        self.dir = os.path.join(data_dir, "oci")
        self.get_settings = get_settings
        self._lock = threading.Lock()
        self._c = None
        self._sig = None

    # ------------------------------------------------------------ login
    def key_path(self):
        return os.path.join(self.dir, "oci_api_key.pem")

    def save_login(self, user, tenancy, fingerprint, region, key_pem):
        os.makedirs(self.dir, mode=0o700, exist_ok=True)
        if key_pem:
            fd = os.open(self.key_path(), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write(key_pem.strip() + "\n")
        self._c = None
        return {"oci_user": user, "oci_tenancy": tenancy, "oci_fingerprint": fingerprint, "oci_region": region}

    def configured(self):
        s = self.get_settings()
        return all(s.get(k) for k in ("oci_user", "oci_tenancy", "oci_fingerprint", "oci_region")) and os.path.exists(self.key_path())

    def clients(self):
        if not self.configured():
            raise DriverError("Oracle account not set up yet (Admin -> Oracle account)")
        try:
            import oci
        except ImportError:
            raise DriverError("the Oracle SDK is missing: pip install oci")
        s = self.get_settings()
        sig = (s["oci_user"], s["oci_tenancy"], s["oci_fingerprint"], s["oci_region"], os.path.getmtime(self.key_path()))
        with self._lock:
            if self._c and self._sig == sig:
                return self._c
            cfg = {"user": s["oci_user"], "tenancy": s["oci_tenancy"], "fingerprint": s["oci_fingerprint"],
                   "region": s["oci_region"], "key_file": self.key_path()}
            try:
                oci.config.validate_config(cfg)
            except Exception as e:
                raise DriverError("Oracle login is not complete: %s" % e)
            r = oci.retry.NoneRetryStrategy()
            self._c = {"oci": oci, "cfg": cfg, "tenancy": cfg["tenancy"],
                       "idn": oci.identity.IdentityClient(cfg, retry_strategy=r),
                       "cmp": oci.core.ComputeClient(cfg, retry_strategy=r),
                       "net": oci.core.VirtualNetworkClient(cfg, retry_strategy=r)}
            self._sig = sig
            return self._c

    def _err(self, e):
        c = self._c
        if c and isinstance(e, c["oci"].exceptions.ServiceError):
            return DriverError("Oracle: %s %s - %s" % (e.status, e.code, e.message))
        return DriverError("Oracle: %s" % e)

    def test(self):
        """Admin -> Oracle account -> Test: home region, ADs, A1 in use."""
        c = self.clients()
        try:
            home = next((x.region_name for x in c["idn"].list_region_subscriptions(c["tenancy"]).data if x.is_home_region), None)
            ads = [a.name for a in c["idn"].list_availability_domains(c["tenancy"]).data]
            oc = mem = 0.0
            n = 0
            for i in c["oci"].pagination.list_call_get_all_results(c["cmp"].list_instances, c["tenancy"]).data:
                if i.lifecycle_state in ("TERMINATED", "TERMINATING"):
                    continue
                n += 1
                if i.shape == "VM.Standard.A1.Flex" and i.shape_config:
                    oc += i.shape_config.ocpus or 0
                    mem += i.shape_config.memory_in_gbs or 0
        except Exception as e:
            raise self._err(e)
        return {"home_region": home, "region": c["cfg"]["region"], "availability_domains": ads,
                "instances": n, "a1_ocpus_used": oc, "a1_mem_used": mem}

    # ------------------------------------------------------------ network (once)
    def _find(self, items, name):
        for x in items:
            if x.display_name == name and getattr(x, "lifecycle_state", "AVAILABLE") not in ("TERMINATED", "TERMINATING"):
                return x
        return None

    def network(self):
        c = self.clients()
        net, m, t = c["net"], c["oci"].core.models, c["tenancy"]
        vcn = self._find(net.list_vcns(t).data, PREFIX + "-vcn")
        if not vcn:
            vcn = net.create_vcn(m.CreateVcnDetails(compartment_id=t, cidr_blocks=["10.43.0.0/16"],
                                                    display_name=PREFIX + "-vcn", dns_label="mpcvcn")).data
            vcn = c["oci"].wait_until(net, net.get_vcn(vcn.id), "lifecycle_state", "AVAILABLE").data
        igw = self._find(net.list_internet_gateways(t, vcn_id=vcn.id).data, PREFIX + "-igw")
        if not igw:
            igw = net.create_internet_gateway(m.CreateInternetGatewayDetails(
                compartment_id=t, vcn_id=vcn.id, is_enabled=True, display_name=PREFIX + "-igw")).data
        net.update_route_table(vcn.default_route_table_id, m.UpdateRouteTableDetails(route_rules=[
            m.RouteRule(destination="0.0.0.0/0", destination_type="CIDR_BLOCK", network_entity_id=igw.id)]))
        rules = []
        for proto, port in ALL_OPEN_PORTS:
            pr = m.PortRange(min=port, max=port)
            if proto == "tcp":
                rules.append(m.IngressSecurityRule(protocol="6", source="0.0.0.0/0", description="tcp %d" % port,
                                                   tcp_options=m.TcpOptions(destination_port_range=pr)))
            else:
                rules.append(m.IngressSecurityRule(protocol="17", source="0.0.0.0/0", description="udp %d" % port,
                                                   udp_options=m.UdpOptions(destination_port_range=pr)))
        rules.append(m.IngressSecurityRule(protocol="1", source="0.0.0.0/0", icmp_options=m.IcmpOptions(type=3, code=4)))
        rules.append(m.IngressSecurityRule(protocol="1", source="0.0.0.0/0", icmp_options=m.IcmpOptions(type=8)))
        net.update_security_list(vcn.default_security_list_id, m.UpdateSecurityListDetails(
            ingress_security_rules=rules, egress_security_rules=[m.EgressSecurityRule(destination="0.0.0.0/0", protocol="all")]))
        sub = self._find(net.list_subnets(t, vcn_id=vcn.id).data, PREFIX + "-subnet")
        if not sub:
            sub = net.create_subnet(m.CreateSubnetDetails(
                compartment_id=t, vcn_id=vcn.id, cidr_block="10.43.0.0/24", display_name=PREFIX + "-subnet",
                dns_label="mpcsub", route_table_id=vcn.default_route_table_id,
                security_list_ids=[vcn.default_security_list_id], prohibit_public_ip_on_vnic=False)).data
            sub = c["oci"].wait_until(net, net.get_subnet(sub.id), "lifecycle_state", "AVAILABLE").data
        return sub.id

    def image(self, image, shape):
        c = self.clients()
        ver = UBUNTU.get(image)
        if not ver:
            raise DriverError("On Oracle pick Ubuntu 24.04 or 22.04 (Debian is not an Oracle platform image)")
        imgs = c["cmp"].list_images(c["tenancy"], operating_system="Canonical Ubuntu", operating_system_version=ver,
                                    shape=shape, sort_by="TIMECREATED", sort_order="DESC").data
        if not imgs:
            raise DriverError("No Ubuntu %s image for %s in this region" % (ver, shape))
        return imgs[0].id

    # ------------------------------------------------------------ launch with capacity retry
    def launch(self, display, image, shape, ocpus, mem_gb, disk_gb, user_data, ssh_key, keep_going, on_wait, retry_s=60):
        """Blocks until the server exists. keep_going() False = the user terminated it while waiting."""
        c = self.clients()
        m, oci = c["oci"].core.models, c["oci"]
        try:
            subnet = self.network()
            img = self.image(image, shape)
            ads = [a.name for a in c["idn"].list_availability_domains(c["tenancy"]).data]
        except DriverError:
            raise
        except Exception as e:
            raise self._err(e)
        meta = {"user_data": base64.b64encode(user_data.encode()).decode()}
        if ssh_key:
            meta["ssh_authorized_keys"] = ssh_key
        sc = m.LaunchInstanceShapeConfigDetails(ocpus=ocpus, memory_in_gbs=mem_gb) if SHAPES[shape]["flex"] else None
        tries, wait = 0, retry_s
        while keep_going():
            for ad in ads:
                if not keep_going():
                    return None
                tries += 1
                det = m.LaunchInstanceDetails(
                    availability_domain=ad, compartment_id=c["tenancy"], display_name=display, shape=shape,
                    shape_config=sc,
                    source_details=m.InstanceSourceViaImageDetails(image_id=img, boot_volume_size_in_gbs=disk_gb),
                    create_vnic_details=m.CreateVnicDetails(subnet_id=subnet, assign_public_ip=True),
                    metadata=meta, freeform_tags={"mp-cloud": display})
                try:
                    return c["cmp"].launch_instance(det).data.id
                except oci.exceptions.ServiceError as e:
                    msg = (e.message or "").lower()
                    if e.status == 429:
                        wait = min(wait * 2, 600)
                        on_wait("Oracle says too many requests - waiting %d s (try %d)" % (wait, tries))
                    elif "capacity" in msg or e.status in (500, 503):
                        wait = retry_s
                        on_wait("Out of host capacity in %s - trying again (try %d)" % (ad.split(":")[-1], tries))
                    elif e.code == "LimitExceeded" or "limit" in msg:
                        raise DriverError("Oracle: your free limit for %s is used up (%s)" % (shape, e.message))
                    else:
                        raise self._err(e)
            slept = 0
            while slept < wait and keep_going():
                time.sleep(2)
                slept += 2
        return None

    def wait_running(self, ocid):
        c = self.clients()
        try:
            c["oci"].wait_until(c["cmp"], c["cmp"].get_instance(ocid), "lifecycle_state", "RUNNING", max_wait_seconds=900)
        except Exception as e:
            raise self._err(e)
        for _ in range(30):
            ip = self.public_ip(ocid)
            if ip:
                return ip
            time.sleep(10)
        return None

    def public_ip(self, ocid):
        c = self.clients()
        try:
            for va in c["cmp"].list_vnic_attachments(c["tenancy"], instance_id=ocid).data:
                if va.lifecycle_state == "ATTACHED":
                    v = c["net"].get_vnic(va.vnic_id).data
                    if v.public_ip:
                        return v.public_ip
        except Exception:
            return None
        return None

    # ------------------------------------------------------------ manage
    def action(self, ocid, action):
        c = self.clients()
        oa = {"start": "START", "stop": "SOFTSTOP", "reboot": "SOFTRESET"}[action]
        try:
            c["cmp"].instance_action(ocid, oa)
        except Exception as e:
            raise self._err(e)

    def delete(self, ocid):
        c = self.clients()
        try:
            c["cmp"].terminate_instance(ocid, preserve_boot_volume=False)
        except Exception as e:
            if getattr(e, "status", 0) == 404:
                return
            raise self._err(e)

    def state(self, ocid):
        c = self.clients()
        try:
            i = c["cmp"].get_instance(ocid).data
        except Exception as e:
            if getattr(e, "status", 0) == 404:
                return "TERMINATED"
            raise self._err(e)
        s = i.lifecycle_state
        return {"STOPPING": "STOPPED", "STARTING": "RUNNING", "PROVISIONING": "PROVISIONING"}.get(s, s)

    def log(self, ocid):
        return ("This server runs in your Oracle account. Its setup log is on the server:\n"
                "  ssh opc@<public IP>   then   sudo tail -f /root/mp-setup.log\n"
                "(or the Oracle website -> Compute -> Instances -> Console connection)")
