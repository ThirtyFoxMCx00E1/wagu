# MP Textdraw Editor (app)

Optional Android app with the same tools as the in-game `/tde` editor. Landscape, GTA SA background,
login / register / forgot / reset password with your MP account (the Discord `/gen` code, like in the game).

- One project = one `.mptd` file (JSON, format `mp-textdraw`, see `src/net/mptd.h`). Every save is also
  copied to `Download/MP Textdraws/`.
- Use it on your server: MP Launcher > Add server > Plugins > type **Textdraw** > **Import file**.
- Pawn export for SA-MP and open.mp; import of `.mptd`, `.pwn` and `.txt` (SA-MP editor exports).

Build: GitHub Actions > **MP Textdraw Editor** > Run workflow (the APK is the run's artifact; fill in a
release tag to attach it to that release). Locally: `SDK=... JAVA_HOME=... KEYSTORE=... KEYPASS=... bash build.sh`.

Login pages after [bootstrap-5-login-page](https://github.com/ThirtyFoxMCx00E1/bootstrap-5-login-page) (MIT,
Muhamad Nauval Azhar). Editor after [samp-textdraw-editor](https://github.com/ThirtyFoxMCx00E1/samp-textdraw-editor).
Bootstrap 5.3.3 CSS is bundled (MIT).
