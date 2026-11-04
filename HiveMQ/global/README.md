# Global hubs (4.05)

The 4 regions (Asia, Europe, NA, Oceania) already work as one network: the mod pings every broker in
`regions.txt` and uses the fastest, MpBridge copies the server lists between them, and with
`branch = on` it carries the game frames VPS to VPS, so a phone always talks to its nearest hub.
4.05 adds two light installers so any cheap VPS can become another hub without HiveMQ / Java:

| Script | Broker | RAM | Good for |
|---|---|---|---|
| `edge-mosquitto.sh` | Eclipse Mosquitto | 5-20 MB | 512 MB VPS, 100+ phones, lowest overhead |
| `edge-emqx.sh` | EMQX 5 (Docker) | ~300 MB | 1 GB+ VPS, web dashboard with live clients / topics |
| `emqx/Dockerfile` | EMQX via Sidekick | ~300 MB | `sidekick launch` (Traefik + SSL for the dashboard) |

Both use the same logins (`mp`, `web`, `bridge`, `admin`) and topic rules (`thirtyfox/#`) as the
HiveMQ regions, set TCP_NODELAY, keep only small QoS 0 queues (old positions are dropped, not
queued) and install the same `99-mp-network.conf` (BBR, fq, no slow start after idle, no corking).

## Fast locations per region

Low-latency routes (CN2 GIA / premium) to pick when renting the VPS (BandwagonHost codes):

| Region | Best | Also good |
|---|---|---|
| Asia | Singapore (Vultr, DigitalOcean, Linode), Hong Kong CN2 GIA (HKHK_8), Tokyo CN2 GIA (JPTYO_8) | Hong Kong CMI (HKHK_3), Osaka Softbank (JPOS_1) |
| Europe | Amsterdam AS9929 premium (EUNL_9) | Amsterdam (EUNL_3) |
| NA | Los Angeles DC6 CN2 GIA-E (USCA_6), DC9 CN2 GIA (USCA_9) | LA DC2 / DC3 / DC4 / DC8, Fremont (USCA_FMT, USCA_FMT8), New York (USNY_2), New Jersey (USNJ), Vancouver (CABC_1, CABC_6 CN2 GIA) |
| Oceania | Sydney (AUSYD_1) | - |

Other providers with nearby hubs: Vultr / DigitalOcean (Singapore, New York, Tokyo),
Hetzner / Contabo (Frankfurt), OVHcloud / Linode (London).

For Philippine players Singapore, Hong Kong CN2 GIA or Tokyo usually give the lowest ping; one hub per region
is enough, more hubs only help when players are far apart (US East vs US West).

## Add a hub

1. Rent the VPS (Ubuntu LTS), point a DuckDNS name at it.
2. `sudo DOMAIN=hk.example.duckdns.org EMAIL=you@mail bash edge-mosquitto.sh` (or `edge-emqx.sh`).
3. Add its block to `docs/regions.txt` (see `regions-template.txt`) and a line to `bridge.conf`,
   restart `mp-bridge`. Phones pick it up at the next start.

## One click per region (4.05.2)

Right now mpsamp / eusamp / nasamp / ocsamp all point to one home connection. To give every region
its own VPS:

1. Rent one VPS per region (Ubuntu 22.04 / 24.04 or Debian 12, 2 GB+ RAM) and add your SSH public key
   when you create it.
2. Repo secrets (Settings -> Secrets and variables -> Actions), once: `VPS_SSH_KEY` (the private key),
   `DUCKDNS_TOKEN`, `MP_BRIDGE_PASS`, `MP_ADMIN_PASS` (two long random texts). Optional variable `MP_EMAIL`.
3. Actions -> **MP region VPS** -> Run with the region and the VPS IP. It points the region's DuckDNS
   name at the VPS, installs HiveMQ + the region's 24/7 server + TLS + tuning (MpBridge on Asia) and
   checks that the broker accepts the `mp` login. Phones, the website and the other regions use it at once.

Do Asia first (Singapore / Hong Kong / Tokyo) - that is where most players are.

## Consoles

- MQTTX (desktop / CLI / web): connect with `admin` to watch `thirtyfox/#` live.
- EMQX dashboard: `http://<ip>:18083`.
- `mosquitto_sub -h <host> -u admin -P <pass> -t 'thirtyfox/#' -v`.

## Credits

[libks](https://github.com/signalwire/libks), [signalwire-c](https://github.com/signalwire/signalwire-c),
[FreeSWITCH](https://github.com/signalwire/freeswitch), [Eclipse Mosquitto](https://github.com/eclipse-mosquitto/mosquitto),
[EMQX](https://github.com/emqx/emqx), [MQTTX](https://github.com/emqx/MQTTX), [Mainflux](https://github.com/mainflux/mainflux),
[awesome-mqtt](https://github.com/awesome-mqtt/awesome-mqtt), [vps-hosting-providers](https://github.com/qfft560/vps-hosting-providers),
[Sidekick](https://github.com/MightyMoud/sidekick).

VPS hosting references: [Aeon-MLTB](https://github.com/AeonOrg/Aeon-MLTB), [whmcs-vps-reseller](https://github.com/Rad-Web-Hosting/whmcs-vps-reseller), [kvm-vps](https://github.com/topics/kvm-vps), [vps-admin](https://github.com/topics/vps-admin), [vps-managment](https://github.com/topics/vps-managment), [vpshosting](https://github.com/topics/vpshosting) (GitHub topics).
