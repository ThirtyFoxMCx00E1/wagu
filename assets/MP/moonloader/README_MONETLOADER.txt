MonetLoader for MP (MonetLoader 3.8.5-mp, based on MonetLoaderOSS)
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
  Diagnostics: adb logcat -s MonetVideo
  samp.events (lib/samp/) is included: copy the whole lib folder WITH its subfolders.
