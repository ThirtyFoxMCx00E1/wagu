MonetLoader for MP (MonetLoader 3.8.0-mp, based on MonetLoaderOSS)
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
