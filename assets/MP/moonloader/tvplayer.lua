-- TV Player for MonetLoader for MP (3.8.5-mp or newer = MP 4.13.42+: the picture is a real 3D mesh on the TV): watch videos on the TVs in GTA SA.
-- Works in single player and on MP servers (only you see your TV).
--
-- Files (Android/data/com.rockstargames.gtasa/files/TYFMC/MP/moonloader/):
--   tvplayer.lua                    this script
--   tvplayer/screens.lua            the TV screen shapes (model id -> mesh), from the tvplayer mod
--   tvplayer/config/tvplayer.ini    settings ([Player]); the PC [Keys] are not used on the phone
--   tvplayer/videos/*.mp4           your videos (mp4 / 3gp / webm / mkv - what the phone can decode)
--
-- How to use: walk up to a TV (a TV object, or a TV that is part of the map like the one in CJ's house). A [TV] button shows on the right.
-- Tap it to start the first video. Buttons while it plays: previous, -10 s, pause, +10 s, next, volume -, volume +, stop.
-- The sound gets quieter as you walk away and stops after StopDistance metres.
script_name('TV Player')
script_author('ThirtyFoxMCx00E1 (MP port), tvplayer mod')
script_version('1.3-mp')

local DIR = getWorkingDirectory() .. '/tvplayer'
local cfg = { Volume = 80, PromptDistance = 2.5, StopDistance = 20, FullVolumeDistance = 4, SeekSeconds = 10,
  VolumeStep = 10, Loop = 0, AutoNext = 1, Stretch = 0, Brightness = 100, DepthTest = 1, DepthOffset = 0.01, OsdSeconds = 4, Debug = 0 }

local function loadIni(path)
  local f = io.open(path, 'r')
  if not f then return end
  local sec = ''
  for raw in f:lines() do
    local line = raw:gsub('[\r\n]', '')
    local s = line:match('^%s*%[(.-)%]')
    if s then sec = s
    elseif sec == 'Player' then
      local k, v = line:match('^%s*([%w_]+)%s*=%s*([^;#]-)%s*$')
      if k and cfg[k] ~= nil and tonumber(v) then cfg[k] = tonumber(v) end
    end
  end
  f:close()
end

local screens = {}
local videos = {}
local font
local sw, sh = 0, 0
local st = { id = 0, obj = nil, mdl = 0, index = 1, osdUntil = 0, msg = '', paused = false, gamePaused = false }
local near = { obj = nil, mdl = 0, dist = 999 }
local buttons = {}      -- the buttons on screen now (onTouch tests these)
local nextButtons = {}  -- the ones being drawn this frame

local function now() return os.clock() end

-- 1.2: the buttons read the finger straight from MP (MP_GetPointer, MP 4.13.41+) every frame. The [TV] button was
-- drawn but taps never reached onTouch on some phones (MonetLoader's touch queue / an older libmonetloader.so /
-- MP's UI keeping the finger). onTouch still works too; one tap only counts once.
local mpPointer
do
  local okf, ffi = pcall(require, 'ffi')
  if okf and ffi then
    pcall(ffi.cdef, 'int MP_GetPointer(int finger, int* x, int* y);')
    local so = ffi.abi('64bit') and 'libMP64.so' or 'libMP32.so'
    local names = { so:sub(4, -4), so }
    local maps = io.open('/proc/self/maps', 'r')   -- the full path AML loaded MP from (same handle, MP stays loaded)
    if maps then
      for l in maps:lines() do local p = l:match('(/%S*/' .. so:gsub('%.', '%%.') .. ')$') if p then names[#names + 1] = p break end end
      maps:close()
    end
    for _, n in ipairs(names) do
      local okl, lib = pcall(ffi.load, n)
      if okl and lib then
        local oks, fn = pcall(function() return lib.MP_GetPointer end)
        if oks and fn then
          local px, py = ffi.new('int[1]'), ffi.new('int[1]')
          mpPointer = function(f) if lib.MP_GetPointer(f, px, py) ~= 0 then return true, px[0], py[0] end return false end
          break
        end
      end
    end
  end
end
local fingerDown = { false, false, false, false }
local lastTapId, lastTapAt = nil, -1
local function osd(text)
  st.msg = text
  st.osdUntil = now() + cfg.OsdSeconds
end

local function listVideos()
  videos = {}
  local ok, lfs = pcall(require, 'lfs')
  if ok and lfs then
    local okd = pcall(function()
      for f in lfs.dir(DIR .. '/videos') do
        local e = f:lower():match('%.(%w+)$')
        if e == 'mp4' or e == '3gp' or e == 'webm' or e == 'mkv' or e == 'm4v' or e == 'mov' then videos[#videos + 1] = f end
      end
    end)
    if not okd then print('TV Player: no folder ' .. DIR .. '/videos') end
  end
  table.sort(videos)
  if #videos == 0 and io.open(DIR .. '/videos/tvplayer.mp4', 'rb') then videos[1] = 'tvplayer.mp4' end
end

local function stop()
  if st.id ~= 0 then videoClose(st.id) end
  st.id = 0
  st.obj = nil
  st.ent = nil
  st.paused = false
end

local function start(index)
  if #videos == 0 then osd('No videos in moonloader/tvplayer/videos') return end
  if index < 1 then index = #videos end
  if index > #videos then index = 1 end
  if st.id ~= 0 then videoClose(st.id) st.id = 0 end
  st.index = index
  st.id = videoOpen(DIR .. '/videos/' .. videos[index])
  if st.id == 0 then osd("Can't play " .. videos[index]) return end
  videoSetLoop(st.id, cfg.Loop ~= 0)
  videoSetVolume(st.id, cfg.Volume / 100)
  videoPlay(st.id)
  st.paused = false
  osd(string.format('%d/%d  %s', index, #videos, videos[index]))
end

-- the TV's axes from the game (exact for any rotation)
-- 3.8.3-mp: a TV can be a script object (st.obj) or part of the map (a building / dummy, st.ent from getNearbyEntities):
-- the TV in CJ's house is a map building and getAllObjects never listed it (black screen, no [TV] button)
local function entBasis(e)
  return { e.x, e.y, e.z, e.rx, e.ry, e.rz, e.fx, e.fy, e.fz, e.ux, e.uy, e.uz }
end
local function tvValid()
  if st.ent then return true end
  return st.obj ~= nil and doesObjectExist(st.obj)
end
local function basis(obj)
  if st.ent then return entBasis(st.ent) end
  local ox, oy, oz = getOffsetFromObjectInWorldCoords(obj, 0, 0, 0)
  local ax, ay, az = getOffsetFromObjectInWorldCoords(obj, 1, 0, 0)
  local bx, by, bz = getOffsetFromObjectInWorldCoords(obj, 0, 1, 0)
  local cx, cy, cz = getOffsetFromObjectInWorldCoords(obj, 0, 0, 1)
  return { ox, oy, oz, ax - ox, ay - oy, az - oz, bx - ox, by - oy, bz - oz, cx - ox, cy - oy, cz - oz }
end
local function toWorld(b, x, y, z)
  return b[1] + x * b[4] + y * b[7] + z * b[10], b[2] + x * b[5] + y * b[8] + z * b[11], b[3] + x * b[6] + y * b[9] + z * b[12]
end

-- 1.3: MonetLoader 3.8.5-mp draws the picture as a real 3D mesh on the TV's screen surface (renderDrawTexture3D),
-- inside the game's 3D pass with the depth buffer: whatever stands in front of the TV (CJ, other peds, a wall)
-- hides the picture, the picture never shows through a body like a ghost, and it is not a flat camera overlay.
-- Older MonetLoader builds only have the 2D overlay (renderDrawTextureTriangles) - that is kept as a fallback.
local warned2d = false
local function drawScreen(tex)
  if not tvValid() then return end
  local s = screens[st.mdl]
  if not s then return end
  local b = basis(st.obj)
  local n = s.normal or { 0, -1, 0 }
  local off = cfg.DepthOffset or 0.01
  local v = s.verts
  local cnt = #v / 8
  local g = math.floor(math.min(255, math.max(0, 255 * cfg.Brightness / 100)))
  local color = 0xFF000000 + g * 0x10000 + g * 0x100 + g
  if isRender3DAvailable and isRender3DAvailable() then
    local out = {}
    for i = 0, cnt - 1 do
      local k = i * 8
      local x, y, z = toWorld(b, v[k + 1] + n[1] * off, v[k + 2] + n[2] * off, v[k + 3] + n[3] * off)
      out[#out + 1] = x; out[#out + 1] = y; out[#out + 1] = z; out[#out + 1] = v[k + 7]; out[#out + 1] = v[k + 8]
    end
    if #out >= 15 then renderDrawTexture3D(tex, out, color, cfg.DepthTest ~= 0) end
    return
  end
  if not warned2d then
    warned2d = true
    print('TV Player: this MonetLoader has no 3D drawing (renderDrawTexture3D) - using the 2D overlay. Update MP to 4.13.42+.')
  end
  if cfg.DepthTest ~= 0 and s.center then
    -- 2D fallback: hidden behind a wall = not drawn (the overlay is drawn on top of the 3D scene)
    local f = st.ent and 0.35 or 0.08
    local cx, cy, cz = toWorld(b, s.center[1] + n[1] * f, s.center[2] + n[2] * f, s.center[3] + n[3] * f)
    local kx, ky, kz = getActiveCameraCoordinates()
    if not isLineOfSightClear(kx, ky, kz, cx, cy, cz, true, false, false, false, false) then return end
  end
  local out = {}
  for t = 0, cnt / 3 - 1 do
    local tri, ok = {}, true
    for k = 0, 2 do
      local i = (t * 3 + k) * 8
      local x, y, z = toWorld(b, v[i + 1] + n[1] * off, v[i + 2] + n[2] * off, v[i + 3] + n[3] * off)
      local r, px, py, pz = convert3DCoordsToScreenEx(x, y, z, true, true)
      if not r or pz <= 0.05 then ok = false break end
      tri[#tri + 1] = px; tri[#tri + 1] = py; tri[#tri + 1] = v[i + 7]; tri[#tri + 1] = v[i + 8]
    end
    if ok then for k = 1, 12 do out[#out + 1] = tri[k] end end
  end
  if #out < 12 then return end
  renderDrawTextureTriangles(tex, out, color)
end

-- touch buttons -------------------------------------------------------------------------------
local function button(id, x, y, w, h, label)
  renderDrawBoxWithBorder(x, y, w, h, 0xC0101010, math.max(2, sh * 0.003), 0xFFE0E0E0)
  local tw = renderGetFontDrawTextLength(font, label)
  renderFontDrawText(font, label, x + (w - tw) / 2, y + (h - renderGetFontDrawHeight(font)) / 2, 0xFFFFFFFF)
  nextButtons[#nextButtons + 1] = { id = id, x = x, y = y, w = w, h = h }
end

local function act(id)
  if id == 'tv' then
    st.obj, st.ent, st.mdl = near.obj, near.ent, near.mdl
    start(st.index)
  elseif id == 'stop' then stop() osd('TV off')
  elseif id == 'pause' then
    if st.paused then videoPlay(st.id) st.paused = false osd('Play') else videoPause(st.id) st.paused = true osd('Pause') end
  elseif id == 'back' then videoSeek(st.id, videoGetPosition(st.id) - cfg.SeekSeconds) osd('-' .. cfg.SeekSeconds .. ' s')
  elseif id == 'fwd' then videoSeek(st.id, videoGetPosition(st.id) + cfg.SeekSeconds) osd('+' .. cfg.SeekSeconds .. ' s')
  elseif id == 'prev' then start(st.index - 1)
  elseif id == 'next' then start(st.index + 1)
  elseif id == 'vdown' then cfg.Volume = math.max(0, cfg.Volume - cfg.VolumeStep) osd('Volume ' .. cfg.Volume)
  elseif id == 'vup' then cfg.Volume = math.min(100, cfg.Volume + cfg.VolumeStep) osd('Volume ' .. cfg.Volume)
  end
end

-- 1.1: MonetLoader for MP 3.8.4 gets the touches from MP, in the same pixels as getScreenResolution / renderDraw*.
-- Before, the touches came raw from the screen (device pixels, the game often renders smaller) or not at all, so
-- the [TV] button was drawn but a tap on it did nothing. A finger slightly next to a button still counts.
local function hitButton(x, y)
  local pad = sh * 0.025
  for _, b in ipairs(buttons) do
    if x >= b.x - pad and x <= b.x + b.w + pad and y >= b.y - pad and y <= b.y + b.h + pad then return b.id end
  end
end
local function tap(id)
  if not id then return false end
  if id == lastTapId and now() - lastTapAt < 0.35 then return true end   -- the same tap from onTouch and from the poll
  lastTapId, lastTapAt = id, now()
  st.pending = id   -- run in main()
  return true
end
function onTouch(kind, num, x, y)
  if kind ~= 2 then return end -- 2 = finger down
  if tap(hitButton(x, y)) then return false end
end
-- 1.2: every frame: a new finger on a button = a tap
local function pollFingers()
  if not mpPointer then return end
  for f = 0, 3 do
    local down, x, y = mpPointer(f)
    if down and not fingerDown[f + 1] then tap(hitButton(x, y)) end
    fingerDown[f + 1] = down
  end
end

local function fmtTime(s)
  s = math.floor(s or 0)
  return string.format('%d:%02d', math.floor(s / 60), s % 60)
end

local function scan()
  near.obj, near.ent, near.mdl, near.dist = nil, nil, 0, 999
  if not isPlayerPlaying(PLAYER_HANDLE) then return end
  local px, py, pz = getCharCoordinates(PLAYER_PED)
  for _, o in ipairs(getAllObjects()) do
    local m = getObjectModel(o)
    if screens[m] then
      local ok, x, y, z = getObjectCoordinates(o)
      if ok == true or type(ok) == 'number' then
        if type(ok) == 'number' then x, y, z = ok, x, y end
        local d = math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2)
        if d < near.dist then near.obj, near.ent, near.mdl, near.dist = o, nil, m, d end
      end
    end
  end
  -- map TVs (buildings / dummies): MonetLoader for MP 3.8.3+
  if getNearbyEntities then
    local list = getNearbyEntities(px, py, pz, math.max(cfg.StopDistance or 15, cfg.PromptDistance or 3) + 2, 128)
    for _, e in ipairs(list) do
      if screens[e.model] then
        local d = math.sqrt((e.x - px) ^ 2 + (e.y - py) ^ 2 + (e.z - pz) ^ 2)
        if d < near.dist then near.obj, near.ent, near.mdl, near.dist = nil, e, e.model, d end
      end
    end
    if cfg.Debug ~= nil and cfg.Debug ~= 0 and not st.debugDone then
      st.debugDone = true
      local ids = {}
      for _, e in ipairs(list) do
        local d = math.sqrt((e.x - px) ^ 2 + (e.y - py) ^ 2 + (e.z - pz) ^ 2)
        if d < 6 then ids[#ids + 1] = string.format('%d(type %d, %.1f m)', e.model, e.type, d) end
      end
      print('TV Player debug: models within 6 m: ' .. table.concat(ids, ', '))
    end
  end
  -- the playing map TV: keep its pointer fresh (it is gone when the interior streams out)
  if st.ent then
    local still = false
    if getNearbyEntities then
      for _, e in ipairs(getNearbyEntities(st.ent.x, st.ent.y, st.ent.z, 0.6, 32)) do
        if e.ptr == st.ent.ptr and e.model == st.mdl then st.ent = e still = true break end
      end
    end
    if not still then stop() osd('TV off') end
  end
end

local function tvDistance()
  if not tvValid() then return 999 end
  local x, y, z
  if st.ent then x, y, z = st.ent.x, st.ent.y, st.ent.z
  else
    local ok
    ok, x, y, z = getObjectCoordinates(st.obj)
    if type(ok) == 'number' then x, y, z = ok, x, y end
  end
  local px, py, pz = getCharCoordinates(PLAYER_PED)
  return math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2)
end

function main()
  loadIni(DIR .. '/config/tvplayer.ini')
  local ok, t = pcall(dofile, DIR .. '/screens.lua')
  if not ok or type(t) ~= 'table' then
    print('TV Player: ' .. DIR .. "/screens.lua missing - copy the tvplayer folder next to tvplayer.lua")
    return
  end
  screens = t
  if not videoOpen then
    print('TV Player: needs MonetLoader for MP 3.8.2 or newer (video player)')
    return
  end
  listVideos()
  font = renderCreateFont('Arial', 12, 5)
  print(mpPointer and 'TV Player: buttons read the touch from MP (MP_GetPointer)' or 'TV Player: MP_GetPointer not found (MP older than 4.13.41?) - buttons use onTouch')
  if not getNearbyEntities then print('TV Player: MonetLoader for MP 3.8.3+ also finds the TVs that are part of the map (CJ\'s house)') end
  print(string.format('TV Player: %d TV models, %d videos', (function() local c = 0 for _ in pairs(screens) do c = c + 1 end return c end)(), #videos))
  local lastScan = 0
  while true do
    wait(0)
    sw, sh = getScreenResolution()
    pollFingers()
    if st.pending then local a = st.pending st.pending = nil act(a) end
    nextButtons = {}
    if now() - lastScan > 0.5 then scan() lastScan = now() end
    local paused = isPauseMenuActive and isPauseMenuActive()
    if st.id ~= 0 then
      -- the game's pause menu pauses the TV too
      if paused and not st.gamePaused and not st.paused then videoPause(st.id) st.gamePaused = true
      elseif not paused and st.gamePaused then st.gamePaused = false if not st.paused then videoPlay(st.id) end end
      local d = tvDistance()
      if d > cfg.StopDistance then stop() osd('TV off (too far)')
      else
        local k = 1
        if d > cfg.FullVolumeDistance then k = math.max(0, 1 - (d - cfg.FullVolumeDistance) / math.max(0.1, cfg.StopDistance - cfg.FullVolumeDistance)) end
        videoSetVolume(st.id, cfg.Volume / 100 * k)
        if videoIsEnded(st.id) and cfg.Loop == 0 then
          if cfg.AutoNext ~= 0 and #videos > 1 then start(st.index + 1) else stop() osd('End') end
        end
      end
    end
    if not paused then
      if st.id ~= 0 then
        local tex = videoGetTexture(st.id)
        if tex then drawScreen(tex) end
        if tvDistance() <= cfg.PromptDistance * 2 then
          local bw, bh, gap = sh * 0.085, sh * 0.075, sh * 0.012
          local ids = { { 'prev', '|<' }, { 'back', '<<' }, { 'pause', st.paused and '>' or '||' }, { 'fwd', '>>' }, { 'next', '>|' }, { 'vdown', 'V-' }, { 'vup', 'V+' }, { 'stop', 'X' } }
          local total = #ids * bw + (#ids - 1) * gap
          local x0, y0 = (sw - total) / 2, sh * 0.80
          for i, b in ipairs(ids) do button(b[1], x0 + (i - 1) * (bw + gap), y0, bw, bh, b[2]) end
        end
      elseif (near.obj or near.ent) and near.dist <= cfg.PromptDistance then
        button('tv', sw * 0.84, sh * 0.40, sh * 0.17, sh * 0.11, 'TV')
      end
      if now() < st.osdUntil then
        local line = st.msg
        if st.id ~= 0 then line = line .. '   ' .. fmtTime(videoGetPosition(st.id)) .. ' / ' .. fmtTime(videoGetDuration(st.id)) end
        local tw = renderGetFontDrawTextLength(font, line)
        renderDrawBox((sw - tw) / 2 - 10, sh * 0.72 - 4, tw + 20, renderGetFontDrawHeight(font) + 8, 0xA0000000)
        renderFontDrawText(font, line, (sw - tw) / 2, sh * 0.72, 0xFFFFFFFF)
      end
    end
    buttons = nextButtons
  end
end

function onScriptTerminate(s, quit)
  if s == thisScript() then stop() end
end
