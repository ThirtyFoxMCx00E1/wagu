# MP Cloud

Your own Oracle-style "Always Free" cloud console. Friends sign up, get a free tier (default 2 OCPU, 12 GB memory, 100 GB storage, 2 servers, the same as Oracle's A1 free tier) and create servers in a few clicks:

- **This machine** - Incus containers (or VMs) on the box running MP Cloud. Starts in seconds, no Oracle account needed.
- **Oracle Cloud** - real OCI servers in the admin's Oracle account, made with the OCI API. On "Out of host capacity" MP Cloud tries every availability domain and keeps retrying (state `WAITING_CAPACITY`) until Oracle has room, like [oci-instance-creator](https://github.com/ThirtyFoxMCx00E1/oci-instance-creator).

One-click installs on any server:

| Preset | What you get |
|---|---|
| Plain | Ubuntu / Debian with SSH (user `opc`) |
| MP server | HiveMQ + the 24/7 MP servers (`vps-setup.sh`) for Asia, Europe, NA or Oceania, DuckDNS updated |
| Coolify | [Coolify](https://coolify.io) app platform, dashboard on port 8000 (idea from [coolify-oci-free](https://github.com/ThirtyFoxMCx00E1/coolify-oci-free)) |
| Dokploy | [Dokploy](https://dokploy.com), dashboard on port 3000 (idea from [dokploy-oci-free](https://github.com/ThirtyFoxMCx00E1/dokploy-oci-free)) |

## Install

On a Debian 12 or Ubuntu 22.04/24.04 server, as root:

```bash
curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/cloud/install.sh | sudo bash
# HTTPS with your own name:
curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/cloud/install.sh | sudo bash -s -- --domain mycloud.duckdns.org
```

It installs Incus (bridge `incusbr0`, btrfs pool), a Python venv in `/opt/mp-cloud`, the `mp-cloud` systemd service on port 9000 and the firewall rules. Data lives in `/var/lib/mp-cloud` (SQLite + the Oracle key, mode 0600). Update: run the installer again.

Or from your phone, no SSH app needed: add the repo secrets `VPS_HOST` (server IP) and `VPS_SSH_KEY` (private key; optional `VPS_USER`, default `ubuntu`, and `VPS_PORT`) in Settings -> Secrets and variables -> Actions, then Actions -> **MP Cloud install** -> Run workflow.

Open `http://<server-ip>:9000`. **The first account becomes the admin.**

## Connect Oracle (once, admin)

1. Oracle website: Profile -> **API keys** -> **Add API key** -> download the private key, copy the config text.
2. MP Cloud: **Tenancy & quotas** -> **Oracle account** -> paste the config text, pick the `.pem` file -> **Save** (it tests the login and shows the home region and A1 usage).
3. Now "Oracle Cloud" is a placement in **Create instance**. Free servers only exist in the account's home region; pick the home region closest to your players for the lowest ping.

One Oracle account has one free tier, shared by everyone who creates Oracle servers. Set each account's quota in **Accounts**.

## Sign-up

Admin -> Cloud settings: `open`, `invite` (default - set an invite code and give it to friends) or `closed`.

## REST API

Make a key in **API keys**, then:

```bash
KEY=mpc_xxx; URL=http://<server-ip>:9000
curl -H "Authorization: Bearer $KEY" $URL/api/v1/instances
curl -H "Authorization: Bearer $KEY" -H 'Content-Type: application/json' $URL/api/v1/instances \
  -d '{"name":"mp-asia","placement":"oracle","shape":"VM.Standard.A1.Flex","ocpus":2,"mem_gb":12,"disk_gb":50,"preset":"mp","mp_region":"asia","ssh_key":"ssh-ed25519 AAAA..."}'
curl -H "Authorization: Bearer $KEY" -X POST $URL/api/v1/instances/<id>/action -d '{"action":"reboot"}' -H 'Content-Type: application/json'
```

All endpoints are listed at `/api/docs`. Browser sessions also need the header `X-MP-Cloud: 1` on changes (CSRF guard); API keys don't.

GitHub Actions: save the key as the secret `MPC_KEY` and the URL as `MPC_URL`, then `curl` the same calls in a step.

## Try it without a server

```bash
cd cloud && pip install -r requirements.txt
MP_CLOUD_DRIVER=fake MP_CLOUD_DATA=/tmp/mpc uvicorn mpcloud.app:app --port 9000
python3 -m pytest -q tests
```

`MP_CLOUD_DRIVER=fake` makes pretend servers. Other settings: `MP_CLOUD_VM=1` (Incus VMs instead of containers), `MP_CLOUD_POOL` (storage pool), `MP_CLOUD_PORT`.

## Ports of local servers

All local servers share the host's public IP: SSH gets its own port in 22000-22999 (`ssh -p 22xxx opc@host`), and preset ports (MP 1883/8883/8884/1922 UDP..., Coolify 8000, Dokploy 3000) are forwarded 1:1, so only one server per preset per machine. Put more on Oracle Cloud.
