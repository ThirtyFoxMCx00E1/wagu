script_name("Dynamic Hitmarker Dual Asset")
script_author("fausto200")
script_version("1.0-mp")
script_description("Dynamic hitmarker by fausto200 (https://ko-fi.com/fausto200), ported to MonetLoader for MP (Android) by ThirtyFoxMC")

-- Port notes (1.0-mp, MP 4.13.44):
--  * Original: "hitmarker-dinamico" by fausto200 for PC MoonLoader (MoonLoader.asi + mimgui). All credit for the
--    design, sounds and images goes to him: https://ko-fi.com/fausto200 . This port keeps his settings below as they are.
--  * Android has no MoonLoader.asi / .dll files: MonetLoader for MP (built into libMP) runs this script.
--  * mimgui -> MonetLoader's renderDraw* API (renderDrawTextureTriangles for the PNG atlas, renderDrawLine,
--    renderFontDrawText), the same drawing TV Player uses.
--  * The PC camera addresses (0xB6F028 / 0xB6EC14) do not exist in libGTASA.so: the crosshair is the game's
--    default 3rd person spot (0.53, 0.40), or the screen centre with a sniper, rocket launcher or camera.
--  * getBonePosition is CPed::GetBonePosition from libGTASA through ffi (fallback: ped position + 0.55 m).
--  * Times use the game timer (os.clock is CPU time on Android). Paths use "/".
--  * Files: moonloader/hitmarker.lua and moonloader/resources/hitmarker/ (png + mp3).

-- ==================== CONFIGURACION ====================
local TAMANO_X          = 24     -- tamano del aspa en pixeles a 1080p (se escala con la resolucion)
local USAR_LINEAS       = false  -- false: usa las rayas de hitmarker.png; true: lineas simples por codigo
local GROSOR_LINEA      = 2.0    -- grosor de las rayas (solo si USAR_LINEAS)
local HUECO_CENTRO      = 0.42   -- 0 a 1: separacion de las rayas del centro (solo si USAR_LINEAS)
local ESCALA_CALAVERA   = 1.5    -- tamano de la calavera respecto al aspa
local DUR_IMPACTO       = 0.24   -- segundos en pantalla: impacto normal
local DUR_MUERTE        = 0.60   -- segundos en pantalla: muerte
local DUR_CALAVERA      = 0.50   -- segundos minimos cuando es headshot
local SOMBRA            = true   -- sombra negra suave para que se lea sobre cualquier fondo

-- Zona de cabeza (metros, relativos al hueso de la cabeza)
local HEAD_RADIO        = 0.10   -- radio base de la zona de cabeza
local HEAD_EMBUDO       = 0.07   -- radio extra hacia arriba (tiros apuntados desde arriba). Bajalo si cuela cuello/hombros
local HEAD_Z_MIN        = -0.06  -- donde empieza la zona (negativo = un poco por debajo del hueso)
local HEAD_Z_MAX        = 0.30   -- donde termina la zona por arriba
local HEAD_Z_PISO       = -0.09  -- por debajo de esto NUNCA es cabeza (cuello y hombros). Sube hacia -0.05 si se cuelan cuellos; baja a -0.12 si fallan headshots
local HEAD_BAJO_FACTOR  = 0.8    -- por debajo del hueso la mira debe estar mas centrada (0.8 = 80% del radio). Bajalo (0.6) para ser mas estricto con el cuello
local HEAD_TOL_METRO    = 0.0015 -- radio extra por cada metro de distancia
local HEAD_RADIO_MAX    = 0.18   -- tope del radio base con la distancia

local SIN_MELEE         = true   -- true: no aparece con armas cuerpo a cuerpo (puÃ±os, bate, cuchillo, katana...). Si pones false, aparece con todo
local DIST_MELEE        = 3.5    -- con arma cuerpo a cuerpo en la mano, el dano a un ped a menos de esta distancia (m) se considera del golpe, no de un explosivo anterior
local VENTANA_EXPLOSIVO = 8.0    -- segundos tras tener en mano un explosivo/lanzallamas en los que el dano sigue contando (la granada explota, el fuego sigue quemando, el C4 se detona despues)
local REQUIERE_DISPARO  = true   -- con arma de fuego, solo cuenta si acabas de disparar
local VENTANA_DISPARO   = 0.6    -- segundos tras disparar en los que un dano cuenta como tuyo

-- Efectos extra
local COLOR_HEADSHOT    = { 1.0, 0.82, 0.25 }  -- color del aspa en headshot (dorado). Pon {1,1,1} para blanco
local DETECTAR_CHALECO  = true   -- detecta peds con chaleco: mira azul mientras absorbe, y escudo roto al romperse
local COLOR_CHALECO     = { 0.35, 0.68, 1.0 }  -- color del aspa al golpear chaleco (azul)
local DUR_ESCUDO        = 0.60   -- segundos que se ve el escudo roto
local ESCALA_ESCUDO     = 1.0    -- tamano del escudo roto respecto a la calavera (1.0 = igual de grande)
local EFECTO_ANILLO     = true   -- onda expansiva en headshot y muerte
local EFECTO_CHISPAS    = true   -- chispas que salen disparadas en muerte y headshot
local DANO_NUMEROS      = true   -- numero de dano flotante a la izquierda de la mira
local COMBO_ACTIVO      = true   -- contador "xN" cuando impactas varias veces seguidas
local COMBO_VENTANA     = 1.2    -- segundos maximos entre impactos para mantener el combo

-- Sonidos e imagenes: se buscan primero en moonloader/resource/hitmarker/ (o resources/hitmarker/), luego en resource/, resources/ y la raiz
local SONIDOS_ACTIVOS   = true
local SONIDO_HEADSHOT   = "csgo_headshot.mp3"
local SONIDO_CUERPO     = "Hitmarker_Noise.mp3"
local VOL_HEADSHOT      = 0.45
local VOL_CUERPO        = 1.0
local SONIDO_CHALECO    = "chaleco_roto.mp3"   -- sonido al romperse el chaleco
local VOL_CHALECO       = 1.4   -- puede pasar de 1.0 (amplifica); si distorsiona bajalo a 1.2
local INTERVALO_SONIDO  = 0.06   -- segundos minimos entre sonidos


-- ==================== GEOMETRIA DE TUS PNG (669x373) ====================
local IMG_W, IMG_H = 669, 373
local CX, CY = 309.5, 186.5
local ANCHO_ASPA = 176
-- {x0, y0, x1, y1, dirX, dirY}
local RAYAS = {
    { 222,  98, 300, 176, -1, -1 },
    { 319,  98, 398, 176,  1, -1 },
    { 222, 197, 300, 276, -1,  1 },
    { 319, 197, 398, 276,  1,  1 },
}
local CAL = { 373, 150, 444, 223 }
-- a solid white pixel of hitmarker.png: filled shapes (shield, sparks) are drawn with it
local BLANCO_U, BLANCO_V = 261 / IMG_W, 137 / IMG_H

local texNormal, texHeadshot = nil, nil
local fonts = {}

-- Evento de impacto actual
local evInicio = -10
local evMuerte, evHead = false, false
local evChaleco, evRotura = false, false
local evDano, evCombo = 0, 1

local pedHealthCache = {}
local pedArmorCache = {}
local cabezaCache = {}
local pedDeadCache = {}
local ultimoDisparo = -10
local ultimoExplosivo = -10
local ultimoImpacto = -10
local ultimaLimpieza = 0

-- ==================== UTILIDADES ====================
local function now()
    local ok, t = pcall(getGameTimer)
    if ok and type(t) == "number" then return t / 1000 end
    return os.clock()
end
local function c01(t) if t < 0 then return 0 elseif t > 1 then return 1 end return t end
local function easeOutCubic(t) t = c01(t); return 1 - (1 - t) ^ 3 end
local function easeOutBack(t)
    t = c01(t)
    local c1 = 1.70158
    local c3 = c1 + 1
    return 1 + c3 * (t - 1) ^ 3 + c1 * (t - 1) ^ 2
end

local function argb(r, g, b, a)
    local function k(v) v = math.floor(c01(v) * 255 + 0.5); return v end
    return k(a) * 0x1000000 + k(r) * 0x10000 + k(g) * 0x100 + k(b)
end

-- Posicion 2D de la reticula (Android: la del juego en 3a persona, o el centro con mira de francotirador)
local function getDynamicCrosshairPos()
    local sw, sh = getScreenResolution()
    local arma = getCurrentCharWeapon(playerPed) or 0
    -- francotirador (34), lanzacohetes (35, 36), camara (43): mira en el centro
    if arma == 34 or arma == 35 or arma == 36 or arma == 43 then
        return sw * 0.5, sh * 0.5
    end
    return sw * 0.53, sh * 0.40
end

local function findResourceFile(filenames)
    -- Primero la carpeta propia del mod (resource/hitmarker), luego las generales como respaldo
    local folders = {
        "/resources/hitmarker/",
        "/resource/hitmarker/",
        "/resources/",
        "/resource/",
        "/",
    }
    local base = getWorkingDirectory()
    for _, folder in ipairs(folders) do
        for _, name in ipairs(filenames) do
            local fullPath = base .. folder .. name
            if doesFileExist(fullPath) then
                return fullPath
            end
        end
    end
    return nil
end

local sndHead, sndBody, sndArmor = nil, nil, nil
local ultimoSonido = -10

local function cargarSonido(nombre)
    local ruta = findResourceFile({ nombre })
    if not ruta then
        print("[Hitmarker] Sound not found: " .. nombre)
        return nil
    end
    local ok, stream = pcall(loadAudioStream, ruta)
    if ok and stream then return stream end
    print("[Hitmarker] Could not load the sound: " .. nombre)
    return nil
end

local function reproducir(stream, volumen)
    if not stream then return end
    pcall(function()
        setAudioStreamVolume(stream, volumen)
        setAudioStreamState(stream, 0)
        setAudioStreamState(stream, 1)
    end)
end

function onScriptTerminate(scr, quit)
    if scr == thisScript() then
        if sndHead then pcall(releaseAudioStream, sndHead) end
        if sndBody then pcall(releaseAudioStream, sndBody) end
        if sndArmor then pcall(releaseAudioStream, sndArmor) end
        if texNormal then pcall(renderReleaseTexture, texNormal) end
        if texHeadshot then pcall(renderReleaseTexture, texHeadshot) end
    end
end

local function loadTextures()
    local normalPath = findResourceFile({ "hitmarker.png" })
    if normalPath then
        local ok, t = pcall(renderLoadTextureFromFile, normalPath)
        if ok and t then texNormal = t end
    end
    local headPath = findResourceFile({ "hitmarker_head.png" })
    if headPath then
        local ok, t = pcall(renderLoadTextureFromFile, headPath)
        if ok and t then texHeadshot = t end
    end
    if not texNormal then print("[Hitmarker] hitmarker.png not found in moonloader/resources/hitmarker/ - drawing lines") end
end

local function fuente(size)
    size = math.max(8, math.floor(size + 0.5))
    if fonts[size] == nil then
        local ok, f = pcall(renderCreateFont, "Arial", size, 4)
        fonts[size] = ok and f or false
    end
    return fonts[size] or nil
end

-- ==================== DIBUJO ====================
local hasTris = type(renderDrawTextureTriangles) == "function"

-- quad con UV: dos triangulos
local function quad(tex, x0, y0, x1, y1, u0, v0, u1, v1, col)
    if hasTris then
        renderDrawTextureTriangles(tex, {
            x0, y0, u0, v0,  x1, y0, u1, v0,  x1, y1, u1, v1,
            x0, y0, u0, v0,  x1, y1, u1, v1,  x0, y1, u0, v1,
        }, col)
    end
end

local function triangulo(x1, y1, x2, y2, x3, y3, col)
    if hasTris and texNormal then
        renderDrawTextureTriangles(texNormal, {
            x1, y1, BLANCO_U, BLANCO_V,  x2, y2, BLANCO_U, BLANCO_V,  x3, y3, BLANCO_U, BLANCO_V,
        }, col)
    end
end

local function circulo(cx, cy, radio, col, grosor)
    local n = 40
    local px, py = cx + radio, cy
    for i = 1, n do
        local a = i / n * math.pi * 2
        local x, y = cx + math.cos(a) * radio, cy + math.sin(a) * radio
        renderDrawLine(px, py, x, y, grosor, col)
        px, py = x, y
    end
end

local function dibujarRaya(r, cx, cy, s, spread, col, offX, offY)
    local u0, v0, u1, v1 = r[1] / IMG_W, r[2] / IMG_H, r[3] / IMG_W, r[4] / IMG_H
    local ox = r[5] * spread + offX
    local oy = r[6] * spread + offY
    quad(texNormal,
        cx + (r[1] - CX) * s + ox, cy + (r[2] - CY) * s + oy,
        cx + (r[3] - CX) * s + ox, cy + (r[4] - CY) * s + oy,
        u0, v0, u1, v1, col)
end

local function dibujarCalavera(cx, cy, s, col, offX, offY)
    local u0, v0, u1, v1 = CAL[1] / IMG_W, CAL[2] / IMG_H, CAL[3] / IMG_W, CAL[4] / IMG_H
    local hw = (CAL[3] - CAL[1]) * s * 0.5
    local hh = (CAL[4] - CAL[2]) * s * 0.5
    quad(texHeadshot, cx - hw + offX, cy - hh + offY, cx + hw + offX, cy + hh + offY, u0, v0, u1, v1, col)
end

local function dibujarLineas(cx, cy, half, spread, col, grosor, offX, offY)
    local ini = half * HUECO_CENTRO + spread
    local fin = half + spread
    for _, r in ipairs(RAYAS) do
        local dx, dy = r[5], r[6]
        renderDrawLine(cx + dx * ini + offX, cy + dy * ini + offY, cx + dx * fin + offX, cy + dy * fin + offY, grosor, col)
    end
end

-- Texto con sombra
local function dibujarTexto(txt, x, y, size, r, g, b, a, ui)
    local f = fuente(size)
    if not f then return end
    pcall(function()
        renderFontDrawText(f, txt, x + 1.5 * ui, y + 1.5 * ui, argb(0, 0, 0, a * 0.6))
        renderFontDrawText(f, txt, x, y, argb(r, g, b, a))
    end)
end

local function anchoTexto(txt, size)
    local f = fuente(size)
    if f then
        local ok, w = pcall(renderGetFontDrawTextLength, f, txt)
        if ok and type(w) == "number" then return w end
    end
    return #txt * size * 0.5
end

-- Escudo roto: dos mitades rellenas que se separan y caen, con la grieta en zigzag y fragmentos
local ESC_IZQ = { {0, -1}, {-0.9, -0.85}, {-0.9, 0.1}, {-0.55, 0.65}, {0, 1} }
local ESC_GRIETA = { {0, -1}, {0.18, -0.5}, {-0.16, -0.1}, {0.14, 0.4}, {0, 1} }
local ESC_FRAG = { {0.2, 0.1, 0.6, -0.5}, {-0.1, 0.4, -0.7, -0.2}, {0.1, -0.3, 0.9, 0.3}, {-0.2, 0.0, -0.4, 0.6} }

local function poligonoMitad(mirror)
    local pts = {}
    for i = 1, #ESC_IZQ do pts[#pts + 1] = { ESC_IZQ[i][1] * mirror, ESC_IZQ[i][2] } end
    for i = #ESC_GRIETA - 1, 2, -1 do pts[#pts + 1] = { ESC_GRIETA[i][1], ESC_GRIETA[i][2] } end
    return pts
end
local ESC_POLI_IZQ = poligonoMitad(1)
local ESC_POLI_DER = poligonoMitad(-1)

local function trazoCerrado(poly, cx, cy, sc, ox, oy, col, grosor)
    for i = 1, #poly do
        local a, b = poly[i], poly[i % #poly + 1]
        renderDrawLine(cx + a[1] * sc + ox, cy + a[2] * sc + oy, cx + b[1] * sc + ox, cy + b[2] * sc + oy, grosor, col)
    end
end

local function rellenar(poly, cx, cy, sc, ox, oy, col)
    local gx, gy = 0, 0
    for _, p in ipairs(poly) do gx = gx + p[1]; gy = gy + p[2] end
    gx, gy = gx / #poly, gy / #poly
    local mx, my = cx + gx * sc + ox, cy + gy * sc + oy
    for i = 1, #poly do
        local a, b = poly[i], poly[i % #poly + 1]
        triangulo(mx, my, cx + a[1] * sc + ox, cy + a[2] * sc + oy, cx + b[1] * sc + ox, cy + b[2] * sc + oy, col)
    end
end

local function dibujarEscudoRoto(cx, cy, sc, t, alpha, ui)
    local sep = easeOutCubic(t / 0.35) * 6 * ui
    local caida = (t * t) * 26 * ui
    local r, g, b = COLOR_CHALECO[1], COLOR_CHALECO[2], COLOR_CHALECO[3]
    local relleno = argb(r, g, b, alpha)
    local borde = argb(r * 0.45, g * 0.55, b * 0.9, alpha)
    local sombra = argb(0, 0, 0, alpha * 0.45)

    local ixo, iyo = -sep, caida * 0.8
    local dxo, dyo = sep, caida

    trazoCerrado(ESC_POLI_IZQ, cx, cy, sc, ixo + 1.2 * ui, iyo + 1.2 * ui, sombra, 3.0 * ui)
    trazoCerrado(ESC_POLI_DER, cx, cy, sc, dxo + 1.2 * ui, dyo + 1.2 * ui, sombra, 3.0 * ui)
    rellenar(ESC_POLI_IZQ, cx, cy, sc, ixo, iyo, relleno)
    rellenar(ESC_POLI_DER, cx, cy, sc, dxo, dyo, relleno)
    trazoCerrado(ESC_POLI_IZQ, cx, cy, sc, ixo, iyo, borde, 1.4 * ui)
    trazoCerrado(ESC_POLI_DER, cx, cy, sc, dxo, dyo, borde, 1.4 * ui)

    local mv = easeOutCubic(t / 0.5)
    local k = sc * 0.2
    for _, f in ipairs(ESC_FRAG) do
        local fx = cx + f[1] * sc + f[3] * sc * mv * 0.9
        local fy = cy + f[2] * sc + f[4] * sc * mv * 0.9 + caida * 1.3
        local o = 1.0 * ui
        triangulo(fx + o, fy - k + o, fx + k + o, fy + k * 0.6 + o, fx - k * 0.7 + o, fy + k * 0.5 + o, sombra)
        triangulo(fx, fy - k, fx + k, fy + k * 0.6, fx - k * 0.7, fy + k * 0.5, relleno)
    end
end

local CHISPAS_ANG = { 0, 45, 90, 135, 180, 225, 270, 315 }

-- Llamado cada frame desde main() (MonetLoader dibuja renderDraw* en el frame actual)
function drawFrame(ahora)
    local D = evMuerte and DUR_MUERTE or DUR_IMPACTO
    if evHead then D = math.max(D, DUR_CALAVERA) end
    if evRotura then D = math.max(D, DUR_ESCUDO) end
    local t = ahora - evInicio
    if t < 0 or t >= D or isPauseMenuActive() then return end

    local _, sh = getScreenResolution()
    local ui = sh / 1080
    local cx, cy = getDynamicCrosshairPos()

    local inicioFade = D * (evMuerte and 0.35 or 0.5)
    local alpha = 1.0
    if t > inicioFade then alpha = c01(1 - (t - inicioFade) / (D - inicioFade)) end

    local entra = 1 - easeOutCubic(t / 0.08)
    local amp = evMuerte and 0.35 or (evHead and 0.3 or 0.2)
    local base = evMuerte and 1.15 or 1.0
    local s = (TAMANO_X * ui / ANCHO_ASPA) * base * (1 + amp * entra)
    local mitad = s * ANCHO_ASPA * 0.5

    local spread = entra * 4 * ui
    if evMuerte and t > 0.1 then
        spread = spread + easeOutCubic((t - 0.1) / (D - 0.1)) * 9 * ui
    end

    local r, g, b = 1.0, 1.0, 1.0
    if evChaleco then r, g, b = COLOR_CHALECO[1], COLOR_CHALECO[2], COLOR_CHALECO[3] end
    if evHead then r, g, b = COLOR_HEADSHOT[1], COLOR_HEADSHOT[2], COLOR_HEADSHOT[3] end
    if evMuerte then r, g, b = 1.0, 0.18, 0.18 end

    if EFECTO_ANILLO and (evHead or evMuerte or evRotura) then
        local p = c01(t / 0.28)
        if p < 1 then
            local radio = mitad * (0.5 + 1.6 * easeOutCubic(p))
            local a = (1 - p) * 0.8 * alpha
            circulo(cx, cy, radio, argb(0, 0, 0, a * 0.35), 3.5 * ui)
            circulo(cx, cy, radio, argb(r, g, b, a), 2.0 * ui)
        end
    end

    if EFECTO_CHISPAS and (evHead or evMuerte or evRotura) then
        local p = c01(t / 0.35)
        if p < 1 then
            local e = easeOutCubic(p)
            local ini = mitad * 0.9 + e * 22 * ui
            local fin = ini + (1 - p) * 9 * ui
            local a = (1 - p) * alpha
            for _, ang in ipairs(CHISPAS_ANG) do
                local rad = math.rad(ang + 22.5)
                local dx, dy = math.cos(rad), math.sin(rad)
                renderDrawLine(cx + dx * ini, cy + dy * ini, cx + dx * fin, cy + dy * fin, 1.8 * ui, argb(r, g, b, a))
            end
        end
    end

    local pasadas = SOMBRA and { true, false } or { false }
    for _, sombra in ipairs(pasadas) do
        local col = sombra and argb(0, 0, 0, alpha * 0.45) or argb(r, g, b, alpha)
        local lineas = USAR_LINEAS or not texNormal or not hasTris
        local ox = (sombra and not lineas) and 1.2 * ui or 0
        local oy = ox

        if lineas then
            local grosor = GROSOR_LINEA * ui + (sombra and 1.8 * ui or 0)
            dibujarLineas(cx, cy, mitad, spread, col, grosor, 0, 0)
        else
            for _, ray in ipairs(RAYAS) do
                dibujarRaya(ray, cx, cy, s, spread, col, ox, oy)
            end
        end

        if evHead and texHeadshot then
            local sc = s * ESCALA_CALAVERA * (0.4 + 0.6 * easeOutBack(t / 0.14))
            local sx = cx + mitad + 6 * ui + (CAL[3] - CAL[1]) * sc * 0.5
            local sy = cy - 7 * ui * easeOutCubic(t / D)
            local sox = sombra and 1.2 * ui or 0
            dibujarCalavera(sx, sy, sc, col, sox, sox)
        end
    end

    if evRotura then
        local scCal = s * ESCALA_CALAVERA * (0.4 + 0.6 * easeOutBack(t / 0.14))
        local mitadCal = (CAL[3] - CAL[1]) * scCal * 0.5
        local ex = cx + mitad + 6 * ui + mitadCal
        local ey = cy - 7 * ui * easeOutCubic(t / D)
        dibujarEscudoRoto(ex, ey, mitadCal * ESCALA_ESCUDO, t, alpha, ui)
    end

    if DANO_NUMEROS and evDano > 0 then
        local txt = string.format("-%d", evDano)
        local size = (evHead and 26 or 22) * ui
        local w = anchoTexto(txt, size)
        local x = cx - mitad - 12 * ui - w
        local y = cy - size * 0.5 - 16 * ui * easeOutCubic(t / D)
        dibujarTexto(txt, x, y, size, r, g, b, alpha, ui)
    end

    if COMBO_ACTIVO and evCombo >= 2 then
        local txt = "x" .. evCombo
        local size = 20 * ui
        local w = anchoTexto(txt, size)
        dibujarTexto(txt, cx - w * 0.5, cy + mitad + 8 * ui, size, 1.0, 1.0, 1.0, alpha * 0.9, ui)
    end
end

-- ==================== HUESOS (libGTASA) ====================
local boneFn = nil
do
    local okF, ffi = pcall(require, "ffi")
    if okF then
        pcall(ffi.cdef, [[
            typedef struct { float x, y, z; } HM_RwV3d;
            void _ZN4CPed15GetBonePositionER5RwV3djb(void* thiz, HM_RwV3d* posn, uint32_t bone, bool calledFromCam);
        ]])
        local okL, gta = pcall(ffi.load, "GTASA")
        if okL then
            local okS, fn = pcall(function() return gta._ZN4CPed15GetBonePositionER5RwV3djb end)
            if okS and fn then
                local posn = ffi.new("HM_RwV3d[1]")
                boneFn = function(ped, bone)
                    local p = getCharPointer(ped)
                    if not p or p == 0 then return nil end
                    fn(ffi.cast("void*", p), posn, bone, false)
                    return posn[0].x, posn[0].y, posn[0].z
                end
            end
        end
    end
end
if type(getBonePosition) ~= "function" then
    function getBonePosition(ped, bone)
        if boneFn then return boneFn(ped, bone) end
        return nil
    end
end

-- ==================== DETECCION DE HEADSHOT ====================
local function distPuntoRayo(px, py, pz, ax, ay, az, bx, by, bz)
    local dx, dy, dz = bx - ax, by - ay, bz - az
    local len2 = dx * dx + dy * dy + dz * dz
    if len2 < 1e-6 then return nil end
    local t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / len2
    if t < 0 then return nil end
    local qx, qy, qz = ax + dx * t, ay + dy * t, az + dz * t
    return math.sqrt((px - qx) ^ 2 + (py - qy) ^ 2 + (pz - qz) ^ 2), qx, qy, qz
end

local function posCabeza(ped)
    local okH, hx, hy, hz = pcall(getBonePosition, ped, 6)
    if okH and type(hx) == "number" then return hx, hy, hz end
    local x, y, z = getCharCoordinates(ped)
    return x, y, z + 0.55
end

-- La mira (rayo 3D) debe pasar por un cilindro sobre el hueso de la cabeza que se ensancha hacia arriba.
-- Por debajo del hueso la zona es mas estrecha (mira bien centrada) y por debajo de HEAD_Z_PISO nunca cuenta.
local function impactoEnCabeza(ped, previa)
    local hx, hy, hz
    if previa then hx, hy, hz = previa[1], previa[2], previa[3] else hx, hy, hz = posCabeza(ped) end

    local sx, sy = getDynamicCrosshairPos()
    local okA, ax, ay, az = pcall(convertScreenCoordsToWorld3D, sx, sy, 1.0)
    local okB, bx, by, bz = pcall(convertScreenCoordsToWorld3D, sx, sy, 60.0)
    if not (okA and okB and type(ax) == "number" and type(bx) == "number") then return false end

    local dRef, qxr, qyr, qzr = distPuntoRayo(hx, hy, hz, ax, ay, az, bx, by, bz)
    if not dRef then return false end
    local vert = qzr - hz
    local lat = math.sqrt((qxr - hx) ^ 2 + (qyr - hy) ^ 2)

    local px, py, pz = getCharCoordinates(playerPed)
    local rango = math.sqrt((hx - px) ^ 2 + (hy - py) ^ 2 + (hz - pz) ^ 2)
    local base = math.min(HEAD_RADIO + HEAD_TOL_METRO * rango, HEAD_RADIO_MAX)

    local pasa = false
    for i = 0, 5 do
        local f = i / 5
        local z = hz + HEAD_Z_MIN + (HEAD_Z_MAX - HEAD_Z_MIN) * f
        local di = distPuntoRayo(hx, hy, z, ax, ay, az, bx, by, bz)
        -- el embudo solo ensancha por encima del hueso
        local ensanche = (z > hz) and HEAD_EMBUDO * ((z - hz) / (HEAD_Z_MAX)) or 0
        if di and di <= base + ensanche then pasa = true end
    end

    if vert < HEAD_Z_PISO then return false end
    if vert < 0 and lat > base * HEAD_BAJO_FACTOR then return false end
    return pasa
end

function main()
    while not isPlayerPlaying(playerHandle) do wait(100) end

    loadTextures()
    print(string.format('[Hitmarker] 1.0-mp ready (textures: %s / %s, bones: %s)', tostring(texNormal ~= nil), tostring(texHeadshot ~= nil), tostring(boneFn ~= nil)))

    if SONIDOS_ACTIVOS then
        sndHead = cargarSonido(SONIDO_HEADSHOT)
        sndBody = cargarSonido(SONIDO_CUERPO)
        sndArmor = cargarSonido(SONIDO_CHALECO)
    end

    while true do
        wait(0)
        local ahora = now()
        drawFrame(ahora)

        if isCharShooting(playerPed) then ultimoDisparo = ahora end
        local arma = getCurrentCharWeapon(playerPed) or 0
        local armaDeFuego = arma >= 22 and arma <= 38
        local armaDeBala = (arma >= 22 and arma <= 34) or arma == 38
        -- granada(16), molotov(18), lanzacohetes(35), buscador(36), lanzallamas(37), C4(39), detonador(40)
        local armaExplosiva = arma == 16 or arma == 18 or arma == 35 or arma == 36 or arma == 37 or arma == 39 or arma == 40
        if armaExplosiva then ultimoExplosivo = ahora end

        local permitido = (not REQUIERE_DISPARO) or (not armaDeFuego) or (ahora - ultimoDisparo) < VENTANA_DISPARO
        if SIN_MELEE then
            if armaDeBala then
                -- armas de bala: regla normal (tiene que haber disparo reciente)
            else
                -- explosivos y fuego: el dano llega con retraso, asi que basta con haberlos tenido en mano hace poco
                permitido = (ahora - ultimoExplosivo) < VENTANA_EXPLOSIVO
            end
        end

        local pjx, pjy, pjz = getCharCoordinates(playerPed)
        for _, ped in ipairs(getAllChars()) do
            if ped ~= playerPed and doesCharExist(ped) then
                local currentHealth = getCharHealth(ped)
                local isDead = isCharDead(ped) or (currentHealth <= 0)
                local lastHealth = pedHealthCache[ped]
                local wasDead = pedDeadCache[ped]
                local currentArmor = 0
                if DETECTAR_CHALECO then
                    local okA, ar = pcall(getCharArmour, ped)
                    if okA and type(ar) == "number" then currentArmor = ar end
                end
                local lastArmor = pedArmorCache[ped]
                local armorDrop = lastArmor ~= nil and currentArmor < lastArmor

                if lastHealth and (currentHealth < lastHealth or armorDrop or (isDead and not wasDead)) then
                    -- Con melee en la mano, un golpe cerca no cuenta aunque hayas usado un explosivo hace poco
                    local permitidoPed = permitido
                    if permitidoPed and SIN_MELEE and not armaDeBala and arma >= 0 and arma <= 15 then
                        local mx, my, mz = getCharCoordinates(ped)
                        local ddx, ddy, ddz = mx - pjx, my - pjy, mz - pjz
                        if ddx * ddx + ddy * ddy + ddz * ddz < DIST_MELEE * DIST_MELEE then permitidoPed = false end
                    end

                    if permitidoPed and hasCharBeenDamagedByChar(ped, playerPed) then
                        local headshotDetected = impactoEnCabeza(ped, cabezaCache[ped])
                        if not headshotDetected and isDead then
                            headshotDetected = isCharPlayingAnim(ped, "KO_shot_face")
                        end

                        -- Combo: impactos seguidos dentro de la ventana
                        if (ahora - ultimoImpacto) < COMBO_VENTANA then evCombo = evCombo + 1 else evCombo = 1 end
                        ultimoImpacto = ahora

                        -- Chaleco: la cabeza lo ignora; si no, azul mientras absorbe y escudo roto al llegar a 0
                        local chaleco, rotura = false, false
                        if armorDrop and not headshotDetected then
                            chaleco = true
                            rotura = lastArmor > 0 and currentArmor <= 0
                        end

                        evInicio = ahora
                        evMuerte = isDead
                        evHead = headshotDetected
                        evChaleco = chaleco
                        evRotura = rotura
                        local perdidaVida = math.max(0, lastHealth - math.max(currentHealth, 0))
                        local perdidaChaleco = armorDrop and (lastArmor - currentArmor) or 0
                        evDano = math.max(0, math.floor(perdidaVida + perdidaChaleco + 0.5))

                        if SONIDOS_ACTIVOS and (ahora - ultimoSonido) >= INTERVALO_SONIDO then
                            ultimoSonido = ahora
                            if headshotDetected then
                                reproducir(sndHead, VOL_HEADSHOT)
                            elseif rotura then
                                reproducir(sndArmor, VOL_CHALECO)
                            else
                                reproducir(sndBody, VOL_CUERPO)
                            end
                        end
                    end
                end

                pedHealthCache[ped] = currentHealth
                pedArmorCache[ped] = currentArmor
                pedDeadCache[ped] = isDead

                -- Guarda la posicion de la cabeza de peds vivos y cercanos (solo con arma de fuego)
                if not isDead and armaDeFuego then
                    local ex, ey, ez = getCharCoordinates(ped)
                    local dx, dy, dz = ex - pjx, ey - pjy, ez - pjz
                    if dx * dx + dy * dy + dz * dz < 8100 then   -- menos de 90 m
                        local hx, hy, hz = posCabeza(ped)
                        cabezaCache[ped] = { hx, hy, hz }
                    end
                end
            end
        end

        -- Limpieza periodica de peds que ya no existen
        if ahora - ultimaLimpieza > 5 then
            ultimaLimpieza = ahora
            for ped in pairs(pedHealthCache) do
                if not doesCharExist(ped) then
                    pedHealthCache[ped] = nil
                    pedArmorCache[ped] = nil
                    pedDeadCache[ped] = nil
                    cabezaCache[ped] = nil
                end
            end
        end
    end
end
