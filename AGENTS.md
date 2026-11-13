# Notes for AI agents working on wagu (MP)

- What changed and why: README.md, section Changelog (newest first).
- AI modding tools (map objects, custom map traffic, Ghidra / libGTASA crash offsets, new hooks):
  tools/ai/README.md - one command installs them: `bash tools/ai/setup.sh`.
- Android build: `bash build_ndk.sh "armeabi-v7a arm64-v8a"` (NDK_HOME set, ndk-build on PATH).
  Server: `cd server && make`. Moquil (the social app): moquil/, web copy via tools/sync_moquil_web.sh.
- Git: keep the repo owner as author; end commits with `Co-authored-by: Perplexity <noreply@perplexity.ai>`.
