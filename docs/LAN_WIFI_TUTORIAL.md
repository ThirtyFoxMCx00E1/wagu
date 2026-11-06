# How to play local Wi-Fi (LAN) multiplayer - MP 4.10

You can play MP with friends on the same Wi-Fi router or on a phone hotspot, without any 24/7 server.
One phone hosts the server, the others join it from the **Lan** tab.

## What you need

- Every phone has the **same MP version** (4.10). An older phone gets "Server uses a different MP version" or "Your MP version is too old".
- Every phone is on the **same network**: the same Wi-Fi router, or connected to the host phone's hotspot.
- The host phone keeps GTA open and the **screen on** (Android stops background games and the server stops with it).

## Step 1 - the host phone

1. (Hotspot) Turn on the hotspot on the host phone. Mobile data can stay off.
2. Start GTA SA, open the MP launcher, go to **+ Add server**.
3. Type a **Server name** (for example `Juan LAN`). Do not use the default server's name or the names of the featured servers (Brief Case War, MW3, Mega Vil Futuristic, Project Day Z Legacy): MP renames them to "your-name's server".
4. Leave **Host / IP empty** (empty = host it on this phone).
5. **Port**: 1923 or higher (1922 is the default server's port, MP moves you to 1923 by itself).
6. A password is optional (max 16 characters). Tell your friends the same password.
7. Tap **Add server (host on this phone)**. The chat says `Your server "Juan LAN" is online (port 1923)` and you join it.

## Step 2 - the other phones

1. Connect to the same Wi-Fi or to the host's hotspot.
2. Open the MP launcher and tap the **Lan** tab (top), then the **refresh** button.
3. The host's server shows with `IP: 192.168.x.x:1923 (Wi-Fi)`. Tap it and **Join**.
4. If it does not show: **+ Add server**, type the host's IP in **Host / IP** (see "Find the host IP" below) and the same port, tap Add server, then join it from **Favorites**.

## Find the host IP

- Hotspot: the host phone is usually `192.168.43.1` (older Android) or the gateway shown in the joiner's Wi-Fi details (Settings > Wi-Fi > the hotspot > Gateway / Router).
- Router: on the host phone, Settings > Wi-Fi > your network > IP address (for example `192.168.1.23`).
- In MP: the host's server row shows `IP: ...:port (Wi-Fi)` on the host phone too.

## Common errors and the fix

| What you see | Why | Fix |
|---|---|---|
| `You are offline. Turn on Wi-Fi or mobile data.` | the phone has no network at all | connect to the Wi-Fi / hotspot first (the hotspot host counts as online, even without data) |
| `No Wi-Fi servers on this network.` | the router blocks phones from seeing each other (AP / client isolation, "guest network"), or the host is not running | use the main network (not guest), turn off AP isolation in the router, or add the host IP by hand (Step 2.4) |
| Each phone ends up alone on its own server with the same name | the joiner could not reach the host, so MP hosted a second copy | stop the copy on the joiner, check the IP / port, then join again. Only one phone presses Host |
| `Server uses a different MP version.` / `Your MP version is too old.` | different MP versions | update every phone to the same version from mpsamp-open.github.io |
| `Wrong server password.` | the saved password is different | edit the server and type the host's password |
| `Server is full.` | the host's max players | raise Max players in + Add server on the host |
| Stuck on the black connection screen | the host phone went to the background / screen off, or the hotspot dropped | keep the host's screen on; 4.10 also lifts the join screen by itself once you are connected |
| Login screen / "log in first" | the default server and the featured servers (Brief Case War, MW3, Mega Vil, Day Z) need the Discord login, and that needs internet | play LAN on your own custom server (no login), or give the hotspot mobile data so the login can reach the bot |

## Notes

- A LAN server is a normal custom server: gamemodes, permissions, plugins and rules from + Add server work the same.
- When the host leaves, another phone on the network takes the server over automatically (players stay).
- Firewalls / VPN apps on a phone (ad blockers, "data saver" VPNs) can block the LAN port: turn them off while playing.
