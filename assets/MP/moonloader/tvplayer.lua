-- TV Player for MonetLoader for MP (MP 4.13.42+: the picture is a real 3D mesh on the TV): watch videos on the TVs in GTA SA.
-- Works in single player and on MP servers (only you see your TV).
--
-- Files (Android/data/com.rockstargames.gtasa/files/TYFMC/MP/moonloader/):
--   tvplayer.lua                    this script
--   tvplayer/screens.lua            the TV screen shapes (model id -> mesh), from the tvplayer mod
--   tvplayer/config/tvplayer.ini    settings ([Player]); the PC [Keys] are not used on the phone
--   tvplayer/videos/<name>/         1.5: a video as an IMAGE SEQUENCE: 00001.jpg, 00002.jpg ... (30 fps) + audio.mp3
--                                   (+ optional info.ini with Fps=30). Made with ffmpeg, see tvplayer/videos/README.txt.
--                                   Every frame is a plain JPG loaded like MP's other images, so no video decoder is needed.
--   tvplayer/videos/*.mp4           also still played with the phone's video decoder (black on some phones: use frames)
--
-- 1.6: the video is put on the TV MODEL's own screen (the screen material of the TV shows the frame), drawn by the game
--      like the rest of the TV. Needs MonetLoader for MP 3.8.7 (MP 4.13.45+). Falls back to the 3D mesh, then the 2D picture.
--
-- How to use: walk up to a TV (a TV object, or a TV that is part of the map like the one in CJ's house). A [TV] button shows on the right.
-- Tap it to start the first video. Buttons while it plays: previous, -10 s, pause, +10 s, next, volume -, volume +, stop.
-- The sound gets quieter as you walk away and stops after StopDistance metres.
script_name('TV Player')
script_author('ThirtyFoxMCx00E1 (MP port), tvplayer mod')
script_version('1.6-mp')

local DIR = getWorkingDirectory() .. '/tvplayer'
local cfg = { Volume = 80, PromptDistance = 2.5, StopDistance = 20, FullVolumeDistance = 4, SeekSeconds = 10,
  VolumeStep = 10, Loop = 0, AutoNext = 1, Stretch = 0, Brightness = 100, DepthTest = 1, DepthOffset = 0.01, OsdSeconds = 4, Debug = 0,
  Fps = 30, Render3D = 1, Preview = 0, Surface = 1 }

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

-- 1.5: a real clock (os.clock is CPU time on Android, it runs too fast or too slow for a video)
local now
do
  local okf, ffi = pcall(require, 'ffi')
  if okf and ffi and pcall(ffi.cdef, 'typedef struct { long tv_sec; long tv_nsec; } tvp_timespec; int clock_gettime(int clk, tvp_timespec* ts);') then
    local ts = ffi.new('tvp_timespec[1]')
    local okc = pcall(function() return ffi.C.clock_gettime(1, ts) end)
    if okc then now = function() ffi.C.clock_gettime(1, ts) return tonumber(ts[0].tv_sec) + tonumber(ts[0].tv_nsec) / 1e9 end end
  end
  if not now then now = function() if getGameTimer then return getGameTimer() / 1000 end return os.clock() end end
end

local function exists(path) local f = io.open(path, 'rb') if f then f:close() return true end return false end

-- 1.5: image sequence player. A folder of JPG frames (00001.jpg ...) and audio.mp3, played at Fps frames a second
-- on a real clock that starts with the sound. Each frame is loaded with renderLoadTextureFromFile (the same loader
-- MP's hitmarker and buttons use) and the textures of older frames are freed a few frames later.
local function seqOpen(dir)
  if not exists(dir .. '/00001.jpg') then return nil end
  local p = { kind = 'seq', dir = dir, fps = cfg.Fps or 30, count = 0, pos = 0, playing = false, t0 = 0,
    vol = 1, loop = false, idx = 0, tex = nil, old = {}, frames = 0, uploads = 0 }
  local f = io.open(dir .. '/info.ini', 'r')
  if f then
    for l in f:lines() do
      local k, v = l:match('^%s*([%w_]+)%s*=%s*([%d%.]+)')
      if k and k:lower() == 'fps' and tonumber(v) and tonumber(v) > 0 then p.fps = tonumber(v) end
      if k and k:lower() == 'frames' and tonumber(v) then p.count = math.floor(tonumber(v)) end
    end
    f:close()
  end
  if p.count <= 0 or not exists(string.format('%s/%05d.jpg', dir, p.count)) then
    -- count the frames: double, then binary search the last one
    local hi = 1
    while exists(string.format('%s/%05d.jpg', dir, hi * 2)) do hi = hi * 2 end
    local lo, top = hi, hi * 2
    while top - lo > 1 do local mid = math.floor((lo + top) / 2) if exists(string.format('%s/%05d.jpg', dir, mid)) then lo = mid else top = mid end end
    p.count = lo
  end
  for _, a in ipairs({ 'audio.mp3', 'audio.ogg', 'audio.wav', 'audio.m4a' }) do
    if exists(dir .. '/' .. a) and loadAudioStream then
      local ok, h = pcall(loadAudioStream, dir .. '/' .. a)
      if ok and h then p.audio = h break end
    end
  end
  return p
end
local function seqPos(p) if p.playing then return p.pos + (now() - p.t0) end return p.pos end
local function seqDur(p) return p.count / p.fps end
local function seqAudio(p, what)
  if not p.audio then return end
  pcall(function()
    if what == 'play' then setAudioStreamPosition(p.audio, seqPos(p)) setAudioStreamState(p.audio, 3)   -- 3 = resume here (1 restarts at 0)
    elseif what == 'pause' then setAudioStreamState(p.audio, 2)
    elseif what == 'stop' then setAudioStreamState(p.audio, 0) end
  end)
end
-- 1.6: textures are released 12 loops after they were last used. The phone's renderer draws a little behind the script,
-- and a texture freed while it was still queued showed as a missing (black) frame now and then.
local grave, loopNo = {}, 0
local function bury(t) if t then grave[#grave + 1] = { t, loopNo } end end
local function releaseOld(all)
  local keep = {}
  for _, g in ipairs(grave) do
    if all or loopNo - g[2] > 12 then if renderReleaseTexture then pcall(renderReleaseTexture, g[1]) end
    else keep[#keep + 1] = g end
  end
  grave = keep
end
local function seqFree(p, keep)
  while #p.old > keep do bury(table.remove(p.old, 1)) end
end
local function seqTexture(p)
  local t = seqPos(p)
  if t >= seqDur(p) then
    if p.loop then p.pos = 0 p.t0 = now() t = 0 seqAudio(p, 'play') else t = seqDur(p) - 0.001 end
  end
  local i = math.max(1, math.min(p.count, math.floor(t * p.fps) + 1))
  if i ~= p.idx then
    local ok, tex = pcall(renderLoadTextureFromFile, string.format('%s/%05d.jpg', p.dir, i))
    p.frames = p.frames + 1
    if ok and tex then
      if p.tex then p.old[#p.old + 1] = p.tex end
      p.tex = tex
      p.uploads = p.uploads + 1
    end
    p.idx = i
    seqFree(p, 3)
  end
  return p.tex
end

-- one interface for both players (image sequence / phone video decoder)
local P = {}
function P.open(path)
  local p = seqOpen(path)
  if p then return p end
  if not videoOpen then return nil end
  local id = videoOpen(path)
  if not id or id == 0 then return nil end
  return { kind = 'codec', id = id }
end
function P.play(p) if p.kind == 'seq' then if not p.playing then p.t0 = now() p.playing = true seqAudio(p, 'play') end else videoPlay(p.id) end end
function P.pause(p) if p.kind == 'seq' then if p.playing then p.pos = seqPos(p) p.playing = false seqAudio(p, 'pause') end else videoPause(p.id) end end
function P.pos(p) if p.kind == 'seq' then return math.min(seqPos(p), seqDur(p)) end return videoGetPosition(p.id) end
function P.dur(p) if p.kind == 'seq' then return seqDur(p) end return videoGetDuration(p.id) end
function P.seek(p, t)
  if p.kind == 'seq' then
    p.pos = math.max(0, math.min(t, seqDur(p) - 0.05)) p.t0 = now()
    if p.playing then seqAudio(p, 'play') end
  else videoSeek(p.id, t) end
end
function P.volume(p, v) if p.kind == 'seq' then if p.audio and math.abs((p.vol or -1) - v) > 0.004 then p.vol = v pcall(setAudioStreamVolume, p.audio, v) end else videoSetVolume(p.id, v) end end
function P.loop(p, on) if p.kind == 'seq' then p.loop = on else videoSetLoop(p.id, on) end end
function P.ended(p) if p.kind == 'seq' then return not p.loop and seqPos(p) >= seqDur(p) end return videoIsEnded(p.id) end
function P.texture(p) if p.kind == 'seq' then return seqTexture(p) end return videoGetTexture(p.id) end
function P.stats(p)
  if p.kind == 'seq' then return p.frames, p.uploads end
  if videoGetStats then return videoGetStats(p.id) end
  return -1, -1
end
function P.close(p)
  if p.kind == 'seq' then
    seqAudio(p, 'stop')
    if p.audio then pcall(releaseAudioStream, p.audio) p.audio = nil end
    if p.tex then p.old[#p.old + 1] = p.tex p.tex = nil end
    seqFree(p, 0)
  else videoClose(p.id) end
end

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
        if f ~= '.' and f ~= '..' and exists(DIR .. '/videos/' .. f .. '/00001.jpg') then videos[#videos + 1] = f   -- 1.5: image sequence
        elseif e == 'mp4' or e == '3gp' or e == 'webm' or e == 'mkv' or e == 'm4v' or e == 'mov' then videos[#videos + 1] = f end
      end
    end)
    if not okd then print('TV Player: no folder ' .. DIR .. '/videos') end
  end
  table.sort(videos)
  if #videos == 0 and exists(DIR .. '/videos/tvplayer/00001.jpg') then videos[1] = 'tvplayer' end
  if #videos == 0 and exists(DIR .. '/videos/tvplayer.mp4') then videos[1] = 'tvplayer.mp4' end
end

local function stop()
  if restoreEntityScreenTexture then pcall(restoreEntityScreenTexture) end
  if st.p then P.close(st.p) end
  st.p = nil
  st.id = 0
  st.obj = nil
  st.ent = nil
  st.paused = false
  st.mode = nil
  st.surfRetry = nil
  st.surfWarned = nil
end

local function start(index)
  if #videos == 0 then osd('No videos in moonloader/tvplayer/videos') return end
  if index < 1 then index = #videos end
  if index > #videos then index = 1 end
  if st.p then P.close(st.p) st.p = nil st.id = 0 end
  st.index = index
  st.p = P.open(DIR .. '/videos/' .. videos[index])
  if not st.p then osd("Can't play " .. videos[index]) return end
  st.id = 1
  print(string.format('TV Player: %s (%s)', videos[index], st.p.kind == 'seq' and string.format('image sequence, %d frames at %g fps, sound %s', st.p.count, st.p.fps, tostring(st.p.audio ~= nil)) or 'video decoder'))
  P.loop(st.p, cfg.Loop ~= 0)
  P.volume(st.p, cfg.Volume / 100)
  P.play(st.p)
  st.paused = false
  st.startedAt = now(); st.diag = false
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
  -- 1.6: on the TV model's own screen material (the game draws it like the rest of the TV)
  if cfg.Surface ~= 0 and setEntityScreenTexture and s.center and now() >= (st.surfRetry or 0) then
    local ptr = st.ent and st.ent.ptr
    if not ptr and st.obj and getObjectPointer then local okp, op = pcall(getObjectPointer, st.obj) if okp then ptr = op end end
    if ptr and ptr ~= 0 then
      local ok, res, info = pcall(setEntityScreenTexture, ptr, tex, s.center, n, s.width or 0.5, s.height or 0.4)
      if ok and res then
        if st.mode ~= 'surface' then st.mode = 'surface' print('TV Player: picture on the TV model (' .. tostring(info) .. ')') end
        return
      end
      st.surfRetry = now() + 5
      if not st.surfWarned then st.surfWarned = true print('TV Player: TV screen material not used (' .. tostring(ok and info or res) .. ') - 3D mesh instead') end
    end
  end
  if cfg.Render3D ~= 0 and isRender3DAvailable and isRender3DAvailable() then
    if st.mode ~= 'mesh' then st.mode = 'mesh' end
    off = math.max(off, 0.02)   -- 1.4: a little in front of the glass (curved CRT screens hid a 1 cm mesh)
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
    if st.paused then P.play(st.p) st.paused = false osd('Play') else P.pause(st.p) st.paused = true osd('Pause') end
  elseif id == 'back' then P.seek(st.p, P.pos(st.p) - cfg.SeekSeconds) osd('-' .. cfg.SeekSeconds .. ' s')
  elseif id == 'fwd' then P.seek(st.p, P.pos(st.p) + cfg.SeekSeconds) osd('+' .. cfg.SeekSeconds .. ' s')
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
  if not renderLoadTextureFromFile and not videoOpen then
    print('TV Player: needs MonetLoader for MP (MP 4.13.34 or newer)')
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
    loopNo = loopNo + 1
    releaseOld(false)
    sw, sh = getScreenResolution()
    pollFingers()
    if st.pending then local a = st.pending st.pending = nil act(a) end
    nextButtons = {}
    if now() - lastScan > 0.5 then scan() lastScan = now() end
    local paused = isPauseMenuActive and isPauseMenuActive()
    if st.id ~= 0 then
      -- the game's pause menu pauses the TV too
      if paused and not st.gamePaused and not st.paused then P.pause(st.p) st.gamePaused = true
      elseif not paused and st.gamePaused then st.gamePaused = false if not st.paused then P.play(st.p) end end
      local d = tvDistance()
      if d > cfg.StopDistance then stop() osd('TV off (too far)')
      else
        local k = 1
        if d > cfg.FullVolumeDistance then k = math.max(0, 1 - (d - cfg.FullVolumeDistance) / math.max(0.1, cfg.StopDistance - cfg.FullVolumeDistance)) end
        P.volume(st.p, cfg.Volume / 100 * k)
        if st.p and P.ended(st.p) and cfg.Loop == 0 then
          if cfg.AutoNext ~= 0 and #videos > 1 then start(st.index + 1) else stop() osd('End') end
        end
      end
    end
    if not paused then
      if st.id ~= 0 and st.p then
        local tex = P.texture(st.p)
        if tex then
          drawScreen(tex)
          -- 1.5: a small 2D preview of the frame (top right) while the info line shows: if the preview moves but
          -- the TV stays black, the 3D drawing is the problem - set Render3D=0 in tvplayer.ini
          if cfg.Preview ~= 0 and now() < st.osdUntil then
            local pw = sh * 0.24
            renderDrawBox(sw - pw * 4 / 3 - sh * 0.03 - 3, sh * 0.03 - 3, pw * 4 / 3 + 6, pw + 6, 0xC0000000)
            renderDrawTexture(tex, sw - pw * 4 / 3 - sh * 0.03, sh * 0.03, pw * 4 / 3, pw, 0, 0xFFFFFFFF)
          end
        end
        -- 1.4: say why the TV stays black (no frames from the phone's decoder / frames but no texture)
        if not st.diag and st.startedAt and not st.paused and now() - st.startedAt > 4 then
          st.diag = true
          local fr, up = P.stats(st.p)
          print(string.format('TV Player: %s - %d frames decoded, %d textures, 3D %s, drawn as %s', videos[st.index] or '?', fr, up,
            tostring(isRender3DAvailable and isRender3DAvailable()), tostring(st.mode or '2D')))
          if getRender3DStats then print(string.format('TV Player: 3D mesh draws %d, ok %d', getRender3DStats())) end
          if st.p.kind == 'seq' then
            if up == 0 then osd("No picture: can't load " .. string.format('%s/%05d.jpg', videos[st.index], math.max(1, st.p.idx))) end
          elseif fr == 0 then osd("No picture: the phone can't decode this video - use an image sequence (videos/README.txt)")
          elseif up == 0 then osd('No picture: frames decoded but no texture (send moonloader.log)') end
        end
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
        if st.p then line = line .. '   ' .. fmtTime(P.pos(st.p)) .. ' / ' .. fmtTime(P.dur(st.p)) end
        local tw = renderGetFontDrawTextLength(font, line)
        renderDrawBox((sw - tw) / 2 - 10, sh * 0.72 - 4, tw + 20, renderGetFontDrawHeight(font) + 8, 0xA0000000)
        renderFontDrawText(font, line, (sw - tw) / 2, sh * 0.72, 0xFFFFFFFF)
      end
    end
    buttons = nextButtons
  end
end

function onScriptTerminate(s, quit)
  if s == thisScript() then stop() releaseOld(true) end
end
