MonetLoader for MP (MonetLoader 3.8.6-mp, based on MonetLoaderOSS)
==================================================================
PC MoonLoader .lua scripts run on Android on MP servers.

Where things go
  Scripts:    Android/data/com.rockstargames.gtasa/files/TYFMC/MP/moonloader/*.lua
  Libraries:  .../files/TYFMC/MP/moonloader/lib/
  Configs:    .../files/TYFMC/MP/moonloader/config/
  Log:        .../files/TYFMC/MP/moonloader/moonloader.log

What works on MP servers (MP 4.13.29+)
  samp.events: onServerMessage, onChatMessage, onShowDialog, onPlayerJoin, onPlayerQuit,
               onSendChat, onSendCommand, onSendDialogResponse (read, edit or block)
  sampAddChatMessage, sampSendChat, sampProcessChatInput, sampRegisterChatCommand,
  sampShowDialog, sampHasDialogRespond, sampSendDialogResponse, sampIsDialogActive,
  sampGetPlayerNickname / Color / Score / Ping, sampIsPlayerConnected, sampGetMaxPlayerId,
  sampGetCharHandleBySampPlayerId, sampGetPlayerIdByCharHandle, sampGetCurrentServerName / Address,
  isSampLoaded / isSampfuncsLoaded / isSampAvailable = true
  All game opcodes (CLEO functions), mimgui, encoding, inicfg, jsoncfg, effil, lfs, socket, ssl,
  requests, cjson, SAMemory (mobile), memory.
  printStyledString / emulated game texts show as GTA big messages.

Not on MP servers (MP has its own sync, there is no SA-MP RakNet)
  sampSendOnfootData / IncarData / AimData ..., sampSpawnPlayer, sampRequestClass, SA-MP textdraw /
  object / vehicle id RPCs. Scripts that only use these do nothing (no crash).

PC-only libraries (Windows .dll) - use the mobile one instead
  imgui (MoonImGui, d3d9)   -> mimgui        (require 'mimgui')
  iconv.dll                 -> encoding      (require 'encoding')
  lanes                     -> effil         (built in)
  MoonAdditions, RakLua dll, SFlua, sampapi (PC offsets), hooks.lua (x86), lpeg, lrexlib-pcre,
  luasql-sqlite, libtcc, xml core.dll, mgtweaks -> not available on Android.

Libraries added from samp-essentials (pure Lua)
  argparse, inspect, penlight, lume, lustache, vkeys, rkeys, windows.message, bind, strings,
  utf8, utf8data, UTF8, xml2lua, xmlhandler, XmlParser, json2lua, lua2json, carbJsonConfig,
  aeslua, SNET, websocket, htmlparser, path, compat, extensions-lite, arizona-events,
  fAwesome5, faIcons, fa-icons, tabler_icons, imspinner, mimgui_hotkeys, mimhotkey, mimtoasts, ADDONS

TV Player (MP 4.13.34, MonetLoader 3.8.2-mp)
  moonloader/tvplayer.lua               the script
  moonloader/tvplayer/screens.lua       TV screen shapes (from the tvplayer mod)
  moonloader/tvplayer/config/tvplayer.ini
  moonloader/tvplayer/videos/*.mp4      your videos (H.264 mp4 plays on every phone)
  Walk to a TV, tap [TV]. Buttons: |< << || >> >| V- V+ X. Works offline and on MP servers.
  Lua API added for it: videoOpen / videoPlay / videoPause / videoSeek / videoGetPosition /
  videoGetDuration / videoSetVolume / videoSetLoop / videoGetTexture / videoGetSize / videoIsEnded /
  videoClose, renderDrawTextureTriangles(tex, {x,y,u,v,...}, argb).
  3.8.5-mp (MP 4.13.42): renderDrawTexture3D(tex, {x,y,z,u,v,...}, argb[, depthTest]) draws textured
  triangles in WORLD space inside the game's 3D pass (with the depth buffer, so peds and walls in front
  hide it) and isRender3DAvailable() says if it works. tvplayer.lua 1.3-mp uses it: the video is a mesh
  on the TV's screen surface instead of a flat overlay on top of everything. The video decoder now uses
  AImageReader (any phone's decoder output) and makes a new texture per frame (no more black picture).
  3.8.6-mp (MP 4.13.43): fixes the TV mesh staying black. Video frames are uploaded through RwImage +
  RwRasterSetFromImage (the path MP's GIFs and logos use) to a square power-of-two texture, and the
  first frame is always shown. New videoGetStats(id) -> frames, uploads (frames decoded, textures made).
  tvplayer.lua 1.4-mp shows a hint on screen (and in moonloader.log) when no frame was decoded after 4 s.
  Diagnostics: adb logcat -s MonetVideo
  tvplayer.lua 1.5-mp (MP 4.13.44): plays IMAGE SEQUENCES - a folder in videos/ with 00001.jpg, 00002.jpg...
  (30 fps, 256x256) + audio.mp3 + info.ini. Every frame is loaded like a normal picture, so no phone video
  decoder is needed (that is what stayed black). The bundled tvplayer.mp4 is now videos/tvplayer/ (3008 frames).
  Make your own: tools/tv_frames.sh in the MP source, or see videos/README.txt (ffmpeg, also in Termux).
  Copy the WHOLE moonloader folder with its subfolders. A small preview shows in the top right while the info
  line is on: if it moves but the TV stays black, set Render3D=0 in tvplayer/config/tvplayer.ini.
  3.8.7-mp (MP 4.13.45): tvplayer.lua 1.6-mp puts the video on the TV MODEL's own screen: the TV's screen material
  shows the frame and the game draws it like the rest of the TV (lit, hidden by anything in front of it). New Lua API:
  setEntityScreenTexture(entityPtr, tex, {cx,cy,cz}, {nx,ny,nz}, w, h) -> ok, info / restoreEntityScreenTexture([ptr]),
  getRender3DStats() -> draws, ok, available. renderDrawTexture3D keeps its vertices in a persistent buffer (the phone's
  renderer reads them after the call; 3.8.5 / 3.8.6 freed them first, so the mesh stayed black). The corner preview is
  off by default (Preview=1 turns it on). Surface=0 in tvplayer.ini uses the 3D mesh only.
  Hitmarker (MP 4.13.44): hitmarker.lua + resources/hitmarker/ - "Dynamic Hitmarker" by fausto200
  (https://ko-fi.com/fausto200), ported from PC MoonLoader. White cross on a hit, gold + skull on a
  headshot, red on a kill, blue + broken shield when armour breaks, damage numbers, xN combo and sounds.
  The PC package's MoonLoader.asi, bass.dll, lua51.dll and lib/*.dll are Windows files: not needed here.
  samp.events (lib/samp/) is included: copy the whole lib folder WITH its subfolders.
