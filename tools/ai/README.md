# AI modding tools for MP (tools/ai)

Tools an AI agent (or you) can run to build MP content and fix MP crashes. Everything except Ghidra is
in this folder (see [VENDORED.md](VENDORED.md)); one command installs it:

```sh
bash tools/ai/setup.sh            # Python 3.10+: gta-scout, gta-flow, reagent, ghidra-bridge -> tools/ai/.venv
bash tools/ai/setup.sh --ghidra   # + Ghidra (latest release, needs JDK 21+) -> tools/ai/ghidra/
. tools/ai/.venv/bin/activate
```

## Which tool for what

| task in MP | tool | how |
|---|---|---|
| Host map editor / Day Z mapping: find an object by its look | gta-scout | Its 2,716 reviewed model descriptions + tags are built into `Host/DayZ/mapping/catalog.txt` by `tools/make_dz_mapping_catalog.py`, so `/mapedit find wood pallet` or `/mapping find red barrel` works on the phone. Rebuild: `python3 tools/make_dz_mapping_catalog.py "" tools/ai/gta-scout/data/annotations/sa-2026-10-03.json` (then copy the file to the website's `Host/DayZ/mapping/`). |
| New props / a custom map pack (Day Z pack style) | gta-scout + Blender | `asset-catalog search` over your own gta3.img: find the dff/txd to reuse, preview it in Blender, export new objects. See gta-scout/AGENTS.md. |
| Traffic (cars driving) on a custom map | gta-flow | `sa-traffic` builds and validates NODES*.DAT for a new road network or edits the vanilla one. Made for the PC layout; test on Android before shipping it in a pack. See gta-flow/AGENTS.md. |
| A crash log says `libGTASA.so + 0x34EB76` | Ghidra + ghidra-bridge | Import libGTASA.so in Ghidra (project mp-gtasa), `ghidra-bridge --config tools/ai/mp/ghidra-bridge.yaml export all`, then `ghidra-bridge ... containing 0x<0x10000 + offset>` (Ghidra's default image base for the .so) and `decompile` it. |
| Hook a new game function (src/main.cpp HOOK / aml->GetSym) | ghidra-bridge | `search CPed`, `decompile <name>`, `xrefs-to <name>` give the mangled symbol and what it does. MP hooks by symbol name, so no address map is needed. |
| Reverse a whole function / class with an AI loop | reagent | `re-agent doctor` checks the setup, `re-agent reverse <function>` writes source with Ghidra evidence and checks parity. Built for the PC gta-reversed project; for MP use it as a research helper, not to generate hooks blindly. |

## Notes for an agent starting from zero

- Read the repo README (changelog) first; the Android build is `bash build_ndk.sh "armeabi-v7a arm64-v8a"`.
- No GTA files are in this repo. Tools that read game data need the user's own gta3.img / libGTASA.so.
- `tools/ai/.venv`, `tools/ai/ghidra/` and `tools/ai/mp/exports/` are git-ignored; never commit them.
- Keep the user as the git author; end commits with `Co-authored-by: Perplexity <noreply@perplexity.ai>`.
