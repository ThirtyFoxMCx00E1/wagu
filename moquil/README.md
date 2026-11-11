# Moquil

The MP social app: friends, private chats, group chats, the party, communities and voice rooms, like Discord,
for the MP mod (libMP32.so / libMP64.so). One app, two ways to open it:

- **Android**: `Moquil.apk` (portrait and landscape). Download: https://mpsamp-open.github.io/downloads/Moquil.apk
- **Web**: https://mpsamp-open.github.io/moquils-web/ (also https://mpsamp-open.github.io/moquil/).
  On a big screen it opens as a desktop window.

## Account

Log in, sign up (Discord `/gen` code) and forgot password use the MP account bot (`js/mpauth.js`), the same as
MP Launcher. Moquil keeps you logged in until you press **Log Out** ("Remember me" off: until the browser closes).
Your social key (your #ID) is saved encrypted under your account (`wagu/soc1/a/...`, PBKDF2 of the password),
so every device you log in on is the same player.

## Link with the game

MP Launcher > Social > **Moquil** > Make link code, then Moquil > Settings > Connections > type the code.
Moquil becomes the same #ID as the game: the same friends, and chats you send from one show on the other.

## Files

- `assets/www/` - the app (HTML / CSS / JS). `tools/sync_moquil_web.sh` copies it to `docs/moquil/` and the
  `mpsamp-open/moquils-web` repo (the website).
  - `js/soc.js` - the MP social protocol (same as `src/net/social.cpp`) over MQTT WebSocket (broker.hivemq.com)
  - `js/voice.js` - voice rooms (WebRTC, signalling over MQTT)
  - `js/mods.js` - the Library (mods and apps)
  - `js/app.js` - the UI
- `src/.../MainActivity.java` - WebView + permissions, downloads (Download/Moquil), notifications
- `build.sh` - aapt2 + javac + d8 + apksigner (no Gradle); `.github/workflows/moquil.yml` builds it
