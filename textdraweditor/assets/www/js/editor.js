/* MP Textdraw Editor (app) - the same tools as the in-game /tde editor (src/mp_tde.inc), after
   samp-textdraw-editor (Nexor). The screen is SA-MP's 640x448 textdraw space. A project is ONE
   .mptd file (JSON, format "mp-textdraw", see src/net/mptd.h): import it in MP Launcher > Add server >
   Plugins > Textdraw. Export = Pawn for SA-MP or open.mp. */
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var N = window.MPTDE || null;                       // Android bridge (MainActivity.Bridge)
  var cv = $('cv'), g = cv.getContext('2d');
  var W = 640, H = 448;
  var user = '';
  try { user = localStorage.getItem('mptde_user') || ''; } catch (e) {}
  if (!user && !/guest=1/.test(location.search)) { location.replace('index.html'); return; }
  $('who').textContent = user ? user + ' (log out)' : 'offline (log in)';

  // ------------------------------------------------------------------ project model
  function now() { var d = new Date(), p = function (n) { return (n < 10 ? '0' : '') + n; }; return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes()); }
  function newItem() {
    return { 'var': '', type: 'global', text: 'New textdraw', x: 270, y: 170, letterX: 0.3, letterY: 1.5, textX: 0, textY: 0, align: 1,
      color: 'FFFFFFFF', useBox: 0, boxColor: '00000096', shadow: 1, outline: 1, bgColor: '00000096', font: 1, proportional: 1,
      selectable: 0, model: 0, rot: [0, 0, 0, 1], vehCol: [-1, -1], group: 0 };
  }
  function newProject(name) {
    return { format: 'mp-textdraw', version: 1, name: name || 'Untitled', author: user || 'guest', created: now(), updated: now(),
      settings: { globalVar: 'Text_Global', playerVar: 'Text_Player' }, textdraws: [], deleted: [] };
  }
  function norm(pr) {
    var p = newProject(pr.name);
    p.author = pr.author || p.author; p.created = pr.created || p.created; p.updated = pr.updated || p.updated;
    if (pr.settings) { p.settings.globalVar = pr.settings.globalVar || p.settings.globalVar; p.settings.playerVar = pr.settings.playerVar || p.settings.playerVar; }
    var fix = function (t) { var n = newItem(); for (var k in n) if (t[k] !== undefined) n[k] = t[k];
      if (t.player !== undefined && t.type === undefined) n.type = t.player ? 'player' : 'global';
      ['color', 'boxColor', 'bgColor'].forEach(function (k) { n[k] = hex8(n[k]); }); if (t.deletedAt) n.deletedAt = t.deletedAt; return n; };
    p.textdraws = (pr.textdraws || []).map(fix); p.deleted = (pr.deleted || []).map(fix);
    return p;
  }
  function hex8(v) {
    if (typeof v === 'number') return ((v >>> 0).toString(16).toUpperCase()).padStart(8, '0');
    v = String(v || '').replace(/^0x|^#/i, '').toUpperCase();
    if (v.length === 6) v += 'FF';
    return /^[0-9A-F]{8}$/.test(v) ? v : 'FFFFFFFF';
  }
  function rgba(h) { h = hex8(h); return 'rgba(' + parseInt(h.substr(0, 2), 16) + ',' + parseInt(h.substr(2, 2), 16) + ',' + parseInt(h.substr(4, 2), 16) + ',' + (parseInt(h.substr(6, 2), 16) / 255).toFixed(3) + ')'; }

  var S = { pr: newProject(), file: '', sel: -1, mode: 'pos', undo: [], redo: [], dirty: false, grid: false, snap: 1 };

  function snapshot() { S.undo.push(JSON.stringify({ t: S.pr.textdraws, d: S.pr.deleted, s: S.sel })); if (S.undo.length > 100) S.undo.shift(); S.redo = []; S.dirty = true; }
  function restore(js) { var o = JSON.parse(js); S.pr.textdraws = o.t; S.pr.deleted = o.d; S.sel = Math.min(o.s, S.pr.textdraws.length - 1); }
  function undo() { if (!S.undo.length) return toast('Nothing to undo'); S.redo.push(JSON.stringify({ t: S.pr.textdraws, d: S.pr.deleted, s: S.sel })); restore(S.undo.pop()); S.dirty = true; refresh(); }
  function redo() { if (!S.redo.length) return toast('Nothing to redo'); S.undo.push(JSON.stringify({ t: S.pr.textdraws, d: S.pr.deleted, s: S.sel })); restore(S.redo.pop()); S.dirty = true; refresh(); }
  function cur() { return S.pr.textdraws[S.sel] || null; }

  // ------------------------------------------------------------------ storage (Android bridge or browser)
  var Store = {
    list: function () { if (N) { try { return JSON.parse(N.listProjects()); } catch (e) { return []; } }
      var o = []; for (var i = 0; i < localStorage.length; i++) { var k = localStorage.key(i); if (k.indexOf('mptd:') === 0) o.push(k.substr(5)); } return o.sort(); },
    load: function (n) { return N ? N.loadProject(n) : localStorage.getItem('mptd:' + n); },
    save: function (n, txt) { if (N) return N.saveProject(n, txt); localStorage.setItem('mptd:' + n, txt); download(n + '.mptd', txt); return 'Downloads/' + n + '.mptd'; },
    del: function (n) { if (N) N.deleteProject(n); else localStorage.removeItem('mptd:' + n); },
    exportFile: function (fn, txt) { if (N) return N.exportFile(fn, txt); download(fn, txt); return 'Downloads/' + fn; }
  };
  function download(fn, txt) { var a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([txt], { type: 'text/plain' })); a.download = fn; a.click(); }
  function safeName(n) { return String(n || '').replace(/[^A-Za-z0-9 _.\-()]/g, '_').replace(/^\.+/, '').substr(0, 48) || 'Untitled'; }

  function saveProject(as) {
    var name = S.file;
    if (!name || as) { prompt1('Save project as', 'Project name', S.pr.name || 'Untitled', function (v) { S.file = safeName(v); S.pr.name = v; saveProject(false); }); return; }
    S.pr.updated = now(); if (!S.pr.author || S.pr.author === 'guest') S.pr.author = user || 'guest';
    var where = Store.save(name, JSON.stringify(S.pr, null, 2));
    S.dirty = false; try { localStorage.setItem('mptde_last', name); } catch (e) {}
    toast(where ? 'Saved ' + where : 'Saved ' + name + '.mptd'); refresh();
  }
  function openProject(n) {
    var txt = Store.load(n); if (!txt) return toast('Cannot read ' + n);
    try { S.pr = norm(JSON.parse(txt)); } catch (e) { return toast('Not an .mptd file: ' + e.message); }
    S.file = n; S.sel = S.pr.textdraws.length ? 0 : -1; S.undo = []; S.redo = []; S.dirty = false; refresh();
    toast('Opened ' + n + ' (' + S.pr.textdraws.length + ' textdraws)');
  }

  // ------------------------------------------------------------------ Pawn export / import (same as net/mptd.h)
  function toInt(h) { return parseInt(hex8(h), 16) | 0; }
  function exportPawn(openmp) {
    var B = function (v) { return openmp ? (v ? 'true' : 'false') : String(v); };
    var AL = function (a) { return !openmp ? String(a) : a === 2 ? 'TEXT_DRAW_ALIGN_CENTER' : a === 3 ? 'TEXT_DRAW_ALIGN_RIGHT' : 'TEXT_DRAW_ALIGN_LEFT'; };
    var FN = ['TEXT_DRAW_FONT_0', 'TEXT_DRAW_FONT_1', 'TEXT_DRAW_FONT_2', 'TEXT_DRAW_FONT_3', 'TEXT_DRAW_FONT_SPRITE_DRAW', 'TEXT_DRAW_FONT_MODEL_PREVIEW'];
    var FO = function (f) { return !openmp ? String(f) : FN[f < 0 || f > 5 ? 1 : f]; };
    var F = function (v) { return Number(v).toFixed(3); };
    var col = openmp ? 'Colour' : 'Color', o = '', pr = S.pr, st = pr.settings;
    var items = pr.textdraws, counts = [[0, 0], [0, 0]];
    items.forEach(function (t) { counts[t.type === 'player' ? 1 : 0][t['var'] ? 1 : 0]++; });
    for (var pass = 0; pass < 4; ++pass) {
      var pl = pass & 1, named = pass >> 1;
      if (!counts[pl][named]) continue;
      if (o) o += '####################################################################################################\r\n\r\n';
      if (!named) o += (pl ? 'new PlayerText: ' : 'new Text: ') + (pl ? st.playerVar : st.globalVar) + (pl ? '[MAX_PLAYERS][' : '[') + counts[pl][0] + '];\r\n\r\n';
      else { items.forEach(function (t) { if ((t.type === 'player' ? 1 : 0) === pl && t['var']) o += (pl ? 'new PlayerText: ' : 'new Text: ') + t['var'] + (pl ? '[MAX_PLAYERS]' : '') + ';\r\n'; }); o += '\r\n'; }
      var idx = 0;
      items.forEach(function (t) {
        if ((t.type === 'player' ? 1 : 0) !== pl || !t['var'] !== !named) return;
        var v = named ? t['var'] + (pl ? '[playerid]' : '') : (pl ? st.playerVar + '[playerid][' + idx + ']' : st.globalVar + '[' + idx + ']');
        var P = pl ? 'PlayerTextDraw' : 'TextDraw', a = pl ? 'playerid, ' + v : v;
        o += v + ' = ' + (pl ? 'CreatePlayerTextDraw(playerid, ' : 'TextDrawCreate(') + F(t.x) + ', ' + F(t.y) + ', "' + t.text + '");\r\n';
        if (+t.letterX || +t.letterY) o += P + 'LetterSize(' + a + ', ' + F(t.letterX) + ', ' + F(t.letterY) + ');\r\n';
        if (+t.textX || +t.textY) o += P + 'TextSize(' + a + ', ' + F(t.textX) + ', ' + F(t.textY) + ');\r\n';
        o += P + 'Alignment(' + a + ', ' + AL(+t.align) + ');\r\n';
        o += P + col + '(' + a + ', ' + toInt(t.color) + ');\r\n';
        if (+t.useBox) { o += P + 'UseBox(' + a + ', ' + B(1) + ');\r\n'; o += P + 'Box' + col + '(' + a + ', ' + toInt(t.boxColor) + ');\r\n'; }
        o += P + 'SetShadow(' + a + ', ' + (t.shadow | 0) + ');\r\n';
        o += P + 'SetOutline(' + a + ', ' + (t.outline | 0) + ');\r\n';
        o += P + 'Background' + col + '(' + a + ', ' + toInt(t.bgColor) + ');\r\n';
        o += P + 'Font(' + a + ', ' + FO(t.font | 0) + ');\r\n';
        o += P + 'SetProportional(' + a + ', ' + B(t.proportional ? 1 : 0) + ');\r\n';
        if ((t.font | 0) === 5) {
          o += P + 'SetPreviewModel(' + a + ', ' + (t.model | 0) + ');\r\n';
          o += P + 'SetPreviewRot(' + a + ', ' + F(t.rot[0]) + ', ' + F(t.rot[1]) + ', ' + F(t.rot[2]) + ', ' + F(t.rot[3]) + ');\r\n';
          o += P + (openmp ? 'SetPreviewVehicleColours(' : 'SetPreviewVehCol(') + a + ', ' + (t.vehCol[0] | 0) + ', ' + (t.vehCol[1] | 0) + ');\r\n';
        }
        if (+t.selectable) o += P + 'SetSelectable(' + a + ', ' + B(1) + ');\r\n';
        o += '\r\n'; ++idx;
      });
    }
    return o;
  }
  function pawnInt(v, font) {
    if (v === 'true') return 1; if (v === 'false') return 0;
    if (/ALIGN_CENT(ER|RE)/.test(v)) return 2; if (/ALIGN_RIGHT/.test(v)) return 3; if (/ALIGN_LEFT/.test(v)) return 1;
    if (/SPRITE_DRAW/.test(v)) return 4; if (/MODEL_PREVIEW/.test(v)) return 5;
    if (font) { var m = /FONT_(\d)/.exec(v); if (m) return +m[1]; }
    if (/^0x/i.test(v)) return parseInt(v.substr(2), 16) >>> 0;
    return parseInt(v, 10) || 0;
  }
  function splitArgs(s) { var o = [], c = '', q = false; for (var i = 0; i < s.length; i++) { var ch = s[i]; if (ch === '"' && s[i - 1] !== '\\') { q = !q; c += ch; continue; } if (ch === ',' && !q) { o.push(c.trim()); c = ''; continue; } c += ch; } o.push(c.trim()); return o; }
  function unq(s) { return s.length >= 2 && s[0] === '"' ? s.substr(1, s.length - 2) : s; }
  function importPawn(src) {
    var vars = [], added = 0, st = S.pr.settings;
    src.split('\n').forEach(function (ln) {
      var cm = ln.indexOf('//'); if (cm >= 0 && (ln.indexOf('"') < 0 || ln.indexOf('"') > cm)) ln = ln.substr(0, cm);
      var lp = ln.indexOf('('), rp = ln.lastIndexOf(')'); if (lp < 0 || rp < lp) return;
      var head = ln.substr(0, lp), eq = head.indexOf('='), fn = head.substr(eq < 0 ? 0 : eq + 1).trim(), a = splitArgs(ln.substring(lp + 1, rp));
      if (fn === 'TextDrawCreate' || fn === 'CreatePlayerTextDraw') {
        if (eq < 0) return; var pl = fn === 'CreatePlayerTextDraw', o = pl ? 1 : 0; if (a.length < o + 3) return;
        var v = head.substr(0, eq).trim(); if (/^new /.test(v)) { v = v.substr(4); var c = v.indexOf(':'); if (c >= 0) v = v.substr(c + 1); v = v.trim(); }
        var it = newItem(); it.type = pl ? 'player' : 'global'; it.x = parseFloat(a[o]) || 0; it.y = parseFloat(a[o + 1]) || 0; it.text = unq(a[o + 2]).substr(0, 255);
        it.letterX = 0; it.letterY = 0; it.shadow = 2; it.outline = 0; it.font = 1; it.proportional = 0; it.bgColor = '000000FF'; it.boxColor = '80808080';
        var base = v.split('[')[0]; if (!(v.indexOf('[') >= 0 && (base === st.globalVar || base === st.playerVar || base === 'Text_Global' || base === 'Text_Player'))) it['var'] = base;
        S.pr.textdraws.push(it); vars.push([v, it]); ++added; return;
      }
      var pl2 = fn.indexOf('PlayerTextDraw') === 0; if (!pl2 && fn.indexOf('TextDraw') !== 0) return;
      var o2 = pl2 ? 1 : 0; if (a.length < o2 + 2) return;
      var t = null; for (var i = vars.length - 1; i >= 0; i--) if (vars[i][0] === a[o2]) { t = vars[i][1]; break; } if (!t) return;
      var f = fn.substr(pl2 ? 14 : 8), Fv = function (i) { return parseFloat(a[o2 + i]) || 0; }, Iv = function (i, fo) { return a[o2 + i] !== undefined ? pawnInt(a[o2 + i], fo) : 0; };
      var H8 = function (n) { return hex8(n >>> 0); };
      if (f === 'LetterSize') { t.letterX = Fv(1); t.letterY = Fv(2); }
      else if (f === 'TextSize') { t.textX = Fv(1); t.textY = Fv(2); }
      else if (f === 'Alignment') { t.align = Iv(1); if (t.align < 1 || t.align > 3) t.align = 1; }
      else if (f === 'Color' || f === 'Colour') t.color = H8(Iv(1));
      else if (f === 'UseBox') t.useBox = Iv(1) ? 1 : 0;
      else if (f === 'BoxColor' || f === 'BoxColour') t.boxColor = H8(Iv(1));
      else if (f === 'SetShadow') t.shadow = Iv(1);
      else if (f === 'SetOutline') t.outline = Iv(1);
      else if (f === 'BackgroundColor' || f === 'BackgroundColour') t.bgColor = H8(Iv(1));
      else if (f === 'Font') { t.font = Iv(1, true); if (t.font < 0 || t.font > 5) t.font = 1; }
      else if (f === 'SetProportional') t.proportional = Iv(1) ? 1 : 0;
      else if (f === 'SetSelectable') t.selectable = Iv(1) ? 1 : 0;
      else if (f === 'SetPreviewModel') t.model = Iv(1);
      else if (f === 'SetPreviewRot') t.rot = [Fv(1), Fv(2), Fv(3), a.length > o2 + 4 ? Fv(4) : 1];
      else if (/^SetPreviewVeh/.test(f)) t.vehCol = [Iv(1) | 0, Iv(2) | 0];
      else if (f === 'SetString') t.text = unq(a[o2 + 1]).substr(0, 255);
    });
    return added;
  }
  function importText(txt, fname) {
    snapshot();
    var n = 0;
    try { var j = JSON.parse(txt); if (j && j.textdraws) { var p = norm(j); if (!S.pr.textdraws.length && !S.file) { S.pr = p; S.file = safeName(p.name || fname); n = p.textdraws.length; } else { S.pr.textdraws = S.pr.textdraws.concat(p.textdraws); n = p.textdraws.length; } } }
    catch (e) { n = importPawn(txt); }
    if (!n) { S.undo.pop(); return toast('No textdraws found in ' + fname); }
    S.sel = S.pr.textdraws.length - 1; refresh(); toast('Imported ' + n + ' textdraws from ' + fname);
  }

  // ------------------------------------------------------------------ rendering (approximation of the GTA SA text)
  var FONTS = ['bold italic {s}px "Times New Roman", serif', 'bold {s}px Arial, sans-serif', 'bold {s}px "Arial Narrow", Arial, sans-serif', 'bold {s}px Georgia, serif'];
  function lines(t) { return String(t.text).replace(/~n~/gi, '\n').replace(/~[a-z]~/gi, '').split('\n'); }
  function bounds(t) {   // in 640x448 units
    var f = t.font | 0;
    if (f === 4 || f === 5) return { x: +t.x, y: +t.y, w: Math.max(4, +t.textX || 0), h: Math.max(4, +t.textY || 0) };
    var ls = lines(t), lh = Math.max(1, +t.letterY * 9), cw = Math.max(0.3, +t.letterX * 30), w = 0;
    ls.forEach(function (l) { w = Math.max(w, l.length * cw * (t.proportional ? 0.62 : 0.8)); });
    var h = ls.length * lh + 2, x = +t.x;
    if (t.useBox) {
      if (+t.align === 2) { var bw = Math.abs(+t.textY) || w; return { x: x - bw / 2, y: +t.y, w: bw, h: h, tw: w }; }
      if (+t.align === 3) { var l3 = +t.textX || x - w; return { x: Math.min(l3, x), y: +t.y, w: Math.abs(x - l3) || w, h: h, tw: w }; }
      var r = +t.textX > x ? +t.textX : x + w; return { x: x, y: +t.y, w: r - x, h: h, tw: w };
    }
    if (+t.align === 2) x -= w / 2; else if (+t.align === 3) x -= w;
    return { x: x, y: +t.y, w: w, h: h, tw: w };
  }
  function draw() {
    var sx = cv.width / W, sy = cv.height / H;
    g.setTransform(1, 0, 0, 1, 0, 0); g.clearRect(0, 0, cv.width, cv.height);
    g.setTransform(sx, 0, 0, sy, 0, 0);
    if (S.grid) { g.strokeStyle = 'rgba(255,255,255,.12)'; g.lineWidth = 0.5; for (var gx = 0; gx <= W; gx += 20) { g.beginPath(); g.moveTo(gx, 0); g.lineTo(gx, H); g.stroke(); } for (var gy = 0; gy <= H; gy += 20) { g.beginPath(); g.moveTo(0, gy); g.lineTo(W, gy); g.stroke(); } }
    S.pr.textdraws.forEach(function (t, i) {
      var b = bounds(t), f = t.font | 0;
      if (f === 4) { g.fillStyle = rgba(t.color); g.globalAlpha = 0.35; g.fillRect(b.x, b.y, b.w, b.h); g.globalAlpha = 1; g.strokeStyle = rgba(t.color); g.lineWidth = 0.8; g.strokeRect(b.x, b.y, b.w, b.h); label(t.text, b); }
      else if (f === 5) { g.fillStyle = rgba(t.bgColor); g.fillRect(b.x, b.y, b.w, b.h); g.strokeStyle = '#ddd'; g.lineWidth = 0.6; g.strokeRect(b.x, b.y, b.w, b.h); label('model ' + t.model, b); }
      else {
        if (t.useBox) { g.fillStyle = rgba(t.boxColor); g.fillRect(b.x, b.y, b.w, b.h); }
        var lh = Math.max(1, +t.letterY * 9), fs = lh * 1.05, ls = lines(t), hs = (+t.letterX * 30 * (t.proportional ? 0.62 : 0.8)) / Math.max(0.1, fs * 0.55);
        g.save(); g.font = FONTS[f].replace('{s}', fs.toFixed(2)); g.textBaseline = 'top';
        var tx = +t.x, al = +t.align;
        ls.forEach(function (l, k) {
          var y = +t.y + k * lh + 1;
          g.save(); g.translate(tx, y); g.scale(hs, 1);
          g.textAlign = al === 2 ? 'center' : al === 3 ? 'right' : 'left';
          if (+t.outline > 0) { g.lineWidth = +t.outline * 1.2 / hs; g.strokeStyle = rgba(t.bgColor); g.lineJoin = 'round'; g.strokeText(l, 0, 0); }
          else if (+t.shadow > 0) { g.fillStyle = rgba(t.bgColor); g.fillText(l, +t.shadow / hs, +t.shadow); }
          g.fillStyle = rgba(t.color); g.fillText(l, 0, 0); g.restore();
        });
        g.restore();
      }
      if (i === S.sel) { g.setLineDash([3, 2]); g.strokeStyle = '#36a3ff'; g.lineWidth = 1; g.strokeRect(b.x - 1, b.y - 1, b.w + 2, b.h + 2); g.setLineDash([]);
        g.fillStyle = '#36a3ff'; g.fillRect(+t.x - 2, +t.y - 2, 4, 4); }
    });
    var t = cur();
    $('hint').textContent = (t ? '#' + S.sel + '  x ' + (+t.x).toFixed(1) + '  y ' + (+t.y).toFixed(1) + '  ' : '') + 'drag: ' + ({ pos: 'position', tsize: 'text size', lsize: 'letter size' })[S.mode] + '  |  ' + S.pr.textdraws.length + ' textdraws';
  }
  function label(s, b) { g.save(); g.fillStyle = '#fff'; g.font = '6px Arial'; g.textBaseline = 'top'; g.fillText(String(s).substr(0, 40), b.x + 2, b.y + 2); g.restore(); }
  function fit() {
    var st = $('stage'), r = st.getBoundingClientRect(), k = Math.min(r.width / W, r.height / H), dpr = window.devicePixelRatio || 1;
    cv.style.width = Math.floor(W * k) + 'px'; cv.style.height = Math.floor(H * k) + 'px';
    cv.width = Math.floor(W * k * dpr); cv.height = Math.floor(H * k * dpr); draw();
  }
  window.addEventListener('resize', fit);

  // ------------------------------------------------------------------ canvas touch / mouse
  var drag = null;
  function pt(e) { var r = cv.getBoundingClientRect(); return { x: (e.clientX - r.left) / r.width * W, y: (e.clientY - r.top) / r.height * H }; }
  cv.addEventListener('pointerdown', function (e) {
    var p = pt(e), hit = -1;
    for (var i = S.pr.textdraws.length - 1; i >= 0; i--) { var b = bounds(S.pr.textdraws[i]); if (p.x >= b.x - 3 && p.x <= b.x + b.w + 3 && p.y >= b.y - 3 && p.y <= b.y + b.h + 3) { hit = i; break; } }
    if (hit < 0 && cur()) hit = S.sel;    // drag the selected one from anywhere (small textdraws on a phone)
    if (hit < 0) return;
    if (hit !== S.sel) { S.sel = hit; refresh(); }
    var t = cur(); cv.setPointerCapture(e.pointerId);
    drag = { p: p, x: +t.x, y: +t.y, tx: +t.textX, ty: +t.textY, lx: +t.letterX, ly: +t.letterY, moved: false };
  });
  cv.addEventListener('pointermove', function (e) {
    if (!drag) return; var t = cur(); if (!t) return;
    var p = pt(e), dx = p.x - drag.p.x, dy = p.y - drag.p.y;
    if (!drag.moved) { if (Math.abs(dx) + Math.abs(dy) < 1.5) return; snapshot(); drag.moved = true; }
    var sn = function (v) { return S.snap > 0 ? Math.round(v / S.snap) * S.snap : Math.round(v * 10) / 10; };
    if (S.mode === 'pos') { t.x = sn(drag.x + dx); t.y = sn(drag.y + dy); }
    else if (S.mode === 'tsize') { t.textX = Math.round((drag.tx + dx) * 10) / 10; t.textY = Math.round((drag.ty + dy) * 10) / 10; }
    else { t.letterX = Math.max(0, Math.round((drag.lx + dx * 0.005) * 1000) / 1000); t.letterY = Math.max(0, Math.round((drag.ly + dy * 0.03) * 1000) / 1000); }
    draw(); props();
  });
  cv.addEventListener('pointerup', function () { if (drag && drag.moved) refresh(); drag = null; });
  cv.addEventListener('pointercancel', function () { drag = null; });

  // ------------------------------------------------------------------ toolbar (icons = assets/MP/tde_icons.png, 7x5 cells)
  function setv(k, v) { var t = cur(); if (!t) return toast('Select or create a textdraw first'); snapshot(); t[k] = v; refresh(); }
  function togg(k) { var t = cur(); if (!t) return toast('Select or create a textdraw first'); setv(k, t[k] ? 0 : 1); }
  var TOOLS = [
    { c: 0, n: 'Projects', f: projectsDlg }, { c: 1, n: 'Settings', f: settingsDlg }, { c: 2, n: 'List', f: listDlg }, { c: 3, n: 'Depot', f: depotDlg },
    { c: 4, n: 'New', f: function () { snapshot(); var it = newItem(); var t = cur(); if (t) { it.type = t.type; } S.pr.textdraws.push(it); S.sel = S.pr.textdraws.length - 1; refresh(); } },
    { c: 5, n: 'Copy', f: function () { var t = cur(); if (!t) return; snapshot(); var c = JSON.parse(JSON.stringify(t)); c.x += 5; c.y += 5; if (c['var']) c['var'] += '_2'; S.pr.textdraws.push(c); S.sel = S.pr.textdraws.length - 1; refresh(); } },
    { c: 6, n: 'Delete', f: function () { var t = cur(); if (!t) return; snapshot(); t.deletedAt = now(); S.pr.deleted.push(t); S.pr.textdraws.splice(S.sel, 1); S.sel = Math.min(S.sel, S.pr.textdraws.length - 1); refresh(); toast('Moved to the depot (tap Depot to bring it back)'); } },
    { c: 7, n: 'Position', m: 'pos' }, { c: 8, n: 'Text size', m: 'tsize' }, { c: 9, n: 'Letter size', m: 'lsize' },
    { c: 10, n: 'Text', f: function () { var t = cur(); if (!t) return; prompt1('Text', 'Text (~n~ new line, ~r~ ~g~ ~b~ ~y~ ~w~ colours)', t.text, function (v) { setv('text', v.substr(0, 255)); }); } },
    { c: 11, n: 'Font 0', f: function () { setv('font', 0); } }, { c: 12, n: 'Font 1', f: function () { setv('font', 1); } },
    { c: 13, n: 'Font 2', f: function () { setv('font', 2); } }, { c: 14, n: 'Font 3', f: function () { setv('font', 3); } },
    { c: 15, n: 'Sprite (font 4)', f: spriteDlg }, { c: 16, n: 'Model (font 5)', f: modelDlg },
    { c: 17, n: 'Align left', f: function () { setv('align', 1); } }, { c: 18, n: 'Align centre', f: function () { setv('align', 2); } }, { c: 19, n: 'Align right', f: function () { setv('align', 3); } },
    { c: 20, alt: 21, k: 'proportional', n: 'Proportional', f: function () { togg('proportional'); } },
    { c: 22, n: 'Outline', f: function () { numDlg('outline', 'Outline size', 0, 10); } },
    { c: 23, n: 'Shadow', f: function () { numDlg('shadow', 'Shadow size', 0, 10); } },
    { c: 24, n: 'Colour', f: function () { colorDlg('color', 'Text colour'); } }, { c: 25, n: 'Background colour', f: function () { colorDlg('bgColor', 'Background / outline colour'); } },
    { c: 26, n: 'Box colour', f: function () { colorDlg('boxColor', 'Box colour'); } },
    { c: 27, alt: 28, k: 'useBox', n: 'Box', f: function () { togg('useBox'); } },
    { c: 29, alt: 30, k: 'selectable', n: 'Selectable', f: function () { togg('selectable'); } },
    { c: 31, n: 'Preview models', f: modelDlg },
    { c: 32, n: 'Global textdraw', f: function () { setv('type', 'global'); } }, { c: 33, n: 'Player textdraw', f: function () { setv('type', 'player'); } },
    { c: 34, n: 'Undo (hold: redo)', f: undo, hold: redo }
  ];
  function iconPos(c) { return (-(c % 7) * 56 + 0) + 'px ' + (-Math.floor(c / 7) * 56 + 0) + 'px'; }
  TOOLS.forEach(function (tl) {
    var b = document.createElement('button'); b.className = 'tb'; b.title = tl.n; b.setAttribute('aria-label', tl.n);
    b.style.backgroundPosition = iconPos(tl.c);
    var held = 0, ht = 0;
    if (tl.hold) { b.onpointerdown = function () { held = 0; ht = setTimeout(function () { held = 1; tl.hold(); }, 500); }; b.onpointerup = b.onpointerleave = function () { clearTimeout(ht); }; }
    b.onclick = function () { if (held) { held = 0; return; } if (tl.m) { S.mode = tl.m; refresh(); } else tl.f(); };
    tl.el = b; $('tools').appendChild(b);
  });
  function toolState() {
    var t = cur();
    TOOLS.forEach(function (tl) {
      var on = false;
      if (tl.m) on = S.mode === tl.m;
      else if (t) {
        if (tl.k) { tl.el.style.backgroundPosition = iconPos(t[tl.k] ? tl.alt : tl.c); on = !!t[tl.k]; }
        else if (/^Font \d/.test(tl.n)) on = (t.font | 0) === +tl.n.substr(5);
        else if (tl.c === 15) on = (t.font | 0) === 4; else if (tl.c === 16) on = (t.font | 0) === 5;
        else if (tl.c >= 17 && tl.c <= 19) on = +t.align === tl.c - 16;
        else if (tl.c === 32) on = t.type !== 'player'; else if (tl.c === 33) on = t.type === 'player';
      }
      tl.el.classList.toggle('on', on);
    });
  }

  // ------------------------------------------------------------------ properties panel (every field of the textdraw)
  function props() {
    var t = cur(), p = $('props');
    if (!t) { p.innerHTML = '<p class="text-secondary mt-2">No textdraw selected.<br>Tap New to add one, or Projects to open a file.</p>'; return; }
    if (p.dataset.sel === String(S.sel) && document.activeElement && p.contains(document.activeElement)) return;   // do not fight the keyboard
    p.dataset.sel = S.sel;
    var f = function (k, lbl, type, step) { return '<label>' + lbl + '</label><input class="form-control" data-k="' + k + '" type="' + (type || 'number') + '" step="' + (step || 'any') + '" value="' + esc(t[k]) + '">'; };
    var sel = function (k, lbl, opts) { return '<label>' + lbl + '</label><select class="form-select" data-k="' + k + '">' + opts.map(function (o) { return '<option value="' + o[0] + '"' + (String(t[k]) === String(o[0]) ? ' selected' : '') + '>' + o[1] + '</option>'; }).join('') + '</select>'; };
    var colr = function (k, lbl) { return '<label>' + lbl + ' <span class="sw" style="background:' + rgba(t[k]) + '"></span></label><input class="form-control" data-k="' + k + '" type="text" maxlength="8" value="' + t[k] + '">'; };
    var row = function (a, b) { return '<div class="row"><div class="col">' + a + '</div><div class="col">' + b + '</div></div>'; };
    p.innerHTML = '<div class="small text-secondary">Textdraw #' + S.sel + ' (' + t.type + ')</div>' +
      '<label>Text</label><textarea class="form-control" data-k="text" rows="2" maxlength="255">' + esc(t.text) + '</textarea>' +
      row(f('x', 'X'), f('y', 'Y')) + row(f('letterX', 'Letter X', 0, 0.01), f('letterY', 'Letter Y', 0, 0.05)) + row(f('textX', 'Text size X'), f('textY', 'Text size Y')) +
      row(sel('font', 'Font', [[0, '0'], [1, '1'], [2, '2'], [3, '3'], [4, '4 sprite'], [5, '5 model']]), sel('align', 'Alignment', [[1, 'Left'], [2, 'Centre'], [3, 'Right']])) +
      row(colr('color', 'Colour'), colr('bgColor', 'Background')) + row(colr('boxColor', 'Box colour'), sel('useBox', 'Box', [[0, 'Off'], [1, 'On']])) +
      row(f('outline', 'Outline', 0, 1), f('shadow', 'Shadow', 0, 1)) + row(sel('proportional', 'Proportional', [[0, 'Off'], [1, 'On']]), sel('selectable', 'Selectable', [[0, 'Off'], [1, 'On']])) +
      row(sel('type', 'Type', [['global', 'Global'], ['player', 'Player']]), f('var', 'Variable', 'text')) +
      ((t.font | 0) === 5 ? row(f('model', 'Model id', 0, 1), '<label>Rot X,Y,Z,Zoom</label><input class="form-control" data-k="rot" type="text" value="' + t.rot.join(',') + '">') +
        '<label>Vehicle colours</label><input class="form-control" data-k="vehCol" type="text" value="' + t.vehCol.join(',') + '">' : '') +
      '<div class="d-flex gap-1 mt-2"><button class="btn btn-sm btn-outline-light flex-fill" id="nudgeL">&larr;</button><button class="btn btn-sm btn-outline-light flex-fill" id="nudgeU">&uarr;</button><button class="btn btn-sm btn-outline-light flex-fill" id="nudgeD">&darr;</button><button class="btn btn-sm btn-outline-light flex-fill" id="nudgeR">&rarr;</button></div>' +
      '<div class="d-flex gap-1 mt-1"><button class="btn btn-sm btn-outline-warning flex-fill" id="pu">Undo</button><button class="btn btn-sm btn-outline-warning flex-fill" id="pr">Redo</button></div>' +
      '<div class="d-flex gap-1 mt-1"><button class="btn btn-sm btn-outline-secondary flex-fill" id="up">Bring forward</button><button class="btn btn-sm btn-outline-secondary flex-fill" id="down">Send back</button></div>';
    p.querySelectorAll('[data-k]').forEach(function (el) {
      el.onchange = function () {
        var k = el.dataset.k, v = el.value, t2 = cur(); if (!t2) return;
        if (k === 'rot') v = v.split(',').map(Number).concat([0, 0, 0, 1]).slice(0, 4).map(function (n, i) { return isNaN(n) ? (i === 3 ? 1 : 0) : n; });
        else if (k === 'vehCol') v = v.split(',').map(function (n) { return parseInt(n, 10); }).concat([-1, -1]).slice(0, 2).map(function (n) { return isNaN(n) ? -1 : n; });
        else if (/olor$/.test(k)) v = hex8(v);
        else if (k === 'text' || k === 'var' || k === 'type') v = k === 'var' ? v.replace(/[^A-Za-z0-9_@]/g, '') : v;
        else v = parseFloat(v) || 0;
        snapshot(); t2[k] = v; p.dataset.sel = ''; refresh();
      };
    });
    var nud = function (dx, dy) { var t3 = cur(); if (!t3) return; snapshot(); t3.x = Math.round((+t3.x + dx) * 10) / 10; t3.y = Math.round((+t3.y + dy) * 10) / 10; p.dataset.sel = ''; refresh(); };
    $('nudgeL').onclick = function () { nud(-S.snap || -0.5, 0); }; $('nudgeR').onclick = function () { nud(S.snap || 0.5, 0); };
    $('nudgeU').onclick = function () { nud(0, -S.snap || -0.5); }; $('nudgeD').onclick = function () { nud(0, S.snap || 0.5); };
    $('pu').onclick = undo; $('pr').onclick = redo; $('up').onclick = function () { move(1); }; $('down').onclick = function () { move(-1); };
  }
  function move(d) { var i = S.sel, j = i + d, a = S.pr.textdraws; if (i < 0 || j < 0 || j >= a.length) return; snapshot(); var x = a[i]; a[i] = a[j]; a[j] = x; S.sel = j; $('props').dataset.sel = ''; refresh(); }
  function esc(s) { return String(s === undefined ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); }
  function refresh() { $('pname').textContent = S.pr.name + (S.file ? '' : ' (not saved)'); $('dirty').classList.toggle('d-none', !S.dirty); $('props').dataset.sel = ''; props(); toolState(); draw(); }

  // ------------------------------------------------------------------ dialogs
  function modal(title, html, onReady) { $('mtitle').textContent = title; $('mbody').innerHTML = html; $('modal').classList.remove('d-none'); if (onReady) onReady($('mbody')); }
  function close() { $('modal').classList.add('d-none'); $('mbody').innerHTML = ''; }
  $('mclose').onclick = close; $('modal').onclick = function (e) { if (e.target === $('modal')) close(); };
  function toast(msg) { var d = document.createElement('div'); d.textContent = msg; d.style.cssText = 'position:fixed;left:50%;top:10px;transform:translateX(-50%);background:#1d3550;border:1px solid #36a3ff;color:#fff;padding:6px 12px;border-radius:6px;z-index:20;max-width:80vw;font-size:12px'; document.body.appendChild(d); setTimeout(function () { d.remove(); }, 2600); }
  function prompt1(title, lbl, val, ok) {
    modal(title, '<label class="form-label small">' + lbl + '</label><input class="form-control mb-2" id="pv" value="' + esc(val) + '"><div class="d-flex"><button class="btn btn-primary btn-sm ms-auto" id="pok">OK</button></div>', function () {
      var i = $('pv'); i.focus(); i.select();
      var go = function () { var v = i.value; close(); ok(v); };
      $('pok').onclick = go; i.onkeydown = function (e) { if (e.key === 'Enter') go(); };
    });
  }
  function numDlg(k, title, lo, hi) {
    var t = cur(); if (!t) return toast('Select or create a textdraw first');
    var h = '<div class="d-flex flex-wrap gap-1">'; for (var i = lo; i <= hi; i++) h += '<button class="btn btn-sm ' + ((t[k] | 0) === i ? 'btn-primary' : 'btn-outline-light') + '" data-v="' + i + '">' + i + '</button>'; h += '</div>';
    modal(title, h, function (b) { b.querySelectorAll('[data-v]').forEach(function (x) { x.onclick = function () { close(); setv(k, +x.dataset.v); }; }); });
  }
  function colorDlg(k, title) {
    var t = cur(); if (!t) return toast('Select or create a textdraw first');
    var h = '<div class="pal mb-2">' + TDE_PALETTE.map(function (c) { return '<div style="background:' + rgba(c) + '" data-c="' + c + '" title="' + c + '"></div>'; }).join('') + '</div>' +
      '<div class="row g-2 align-items-end"><div class="col-5"><label class="small">HEX RRGGBBAA</label><input class="form-control" id="hx" maxlength="8" value="' + t[k] + '"></div>' +
      '<div class="col-5"><label class="small">Alpha <span id="av">' + parseInt(t[k].substr(6, 2), 16) + '</span></label><input type="range" class="form-range" min="0" max="255" id="al" value="' + parseInt(t[k].substr(6, 2), 16) + '"></div>' +
      '<div class="col-2"><button class="btn btn-primary w-100" id="cok">OK</button></div></div><div class="mt-2" id="pv" style="height:24px;border-radius:4px;background:' + rgba(t[k]) + '"></div>';
    modal(title, h, function (b) {
      var hx = $('hx'), al = $('al'), up = function () { $('pv').style.background = rgba(hx.value); };
      b.querySelectorAll('[data-c]').forEach(function (x) { x.onclick = function () { hx.value = x.dataset.c.substr(0, 6) + ('0' + (+al.value).toString(16)).slice(-2).toUpperCase(); up(); }; });
      al.oninput = function () { $('av').textContent = al.value; hx.value = hex8(hx.value).substr(0, 6) + ('0' + (+al.value).toString(16)).slice(-2).toUpperCase(); up(); };
      hx.oninput = up;
      $('cok').onclick = function () { close(); setv(k, hex8(hx.value)); };
    });
  }
  function spriteDlg() {
    var t = cur(); if (!t) return toast('Select or create a textdraw first');
    modal('Sprites (font 4)', '<input class="form-control mb-2" id="sq" placeholder="Search, e.g. radar or LD_"><div class="sprites" id="sl"></div>', function () {
      var q = $('sq'), fill = function () { var s = q.value.toLowerCase(), n = 0, h = '';
        TDE_SPRITES.forEach(function (sp) { if (n < 300 && (!s || sp.toLowerCase().indexOf(s) >= 0)) { h += '<button class="btn btn-sm btn-outline-light text-start text-truncate" data-s="' + sp + '">' + sp + '</button>'; n++; } });
        $('sl').innerHTML = h || '<span class="text-secondary">No sprite matches</span>';
        $('sl').querySelectorAll('[data-s]').forEach(function (x) { x.onclick = function () { close(); snapshot(); var c = cur(); c.font = 4; c.text = x.dataset.s; if (!+c.textX) c.textX = 40; if (!+c.textY) c.textY = 40; c.color = 'FFFFFFFF'; refresh(); }; }); };
      q.oninput = fill; fill();
    });
  }
  function modelDlg() {
    var t = cur(); if (!t) return toast('Select or create a textdraw first');
    var common = [[411, 'Infernus'], [522, 'NRG-500'], [560, 'Sultan'], [562, 'Elegy'], [0, 'CJ skin'], [294, 'Skin 294'], [348, 'Desert Eagle'], [356, 'M4'], [1274, 'Dollar icon'], [1239, 'Info icon'], [19134, 'Arrow'], [1318, 'Down arrow']];
    modal('Preview model (font 5)', '<div class="row g-2"><div class="col-4"><label class="small">Model id</label><input class="form-control" id="mid" type="number" value="' + (t.model || 411) + '"></div>' +
      '<div class="col-8"><label class="small">Rotation X, Y, Z, zoom</label><input class="form-control" id="mrot" value="' + t.rot.join(',') + '"></div>' +
      '<div class="col-6"><label class="small">Vehicle colours</label><input class="form-control" id="mvc" value="' + t.vehCol.join(',') + '"></div>' +
      '<div class="col-6 d-flex align-items-end"><button class="btn btn-primary w-100" id="mok">Use model</button></div></div>' +
      '<div class="d-flex flex-wrap gap-1 mt-2">' + common.map(function (m) { return '<button class="btn btn-sm btn-outline-light" data-m="' + m[0] + '">' + m[0] + ' ' + m[1] + '</button>'; }).join('') + '</div>', function (b) {
      b.querySelectorAll('[data-m]').forEach(function (x) { x.onclick = function () { $('mid').value = x.dataset.m; }; });
      $('mok').onclick = function () {
        var r = $('mrot').value.split(',').map(Number).concat([0, 0, 0, 1]).slice(0, 4), vc = $('mvc').value.split(',').map(function (n) { return parseInt(n, 10); }).concat([-1, -1]).slice(0, 2);
        close(); snapshot(); var c = cur(); c.font = 5; c.model = parseInt($('mid').value, 10) || 0; c.rot = r.map(function (n, i) { return isNaN(n) ? (i === 3 ? 1 : 0) : n; }); c.vehCol = vc.map(function (n) { return isNaN(n) ? -1 : n; });
        if (!+c.textX) c.textX = 90; if (!+c.textY) c.textY = 90; if (c.text === 'New textdraw') c.text = '_'; refresh();
      };
    });
  }
  function listDlg() {
    var h = S.pr.textdraws.map(function (t, i) { return '<div class="list-item' + (i === S.sel ? ' sel' : '') + '" data-i="' + i + '">#' + i + ' <span class="badge bg-' + (t.type === 'player' ? 'warning text-dark' : 'info text-dark') + '">' + t.type + '</span> <span class="sw" style="background:' + rgba(t.color) + '"></span> ' + esc(String(t.text).substr(0, 50)) + (t['var'] ? ' <span class="text-secondary">(' + esc(t['var']) + ')</span>' : '') + '</div>'; }).join('');
    modal('Textdraws (' + S.pr.textdraws.length + ')', h || '<span class="text-secondary">No textdraws yet</span>', function (b) { b.querySelectorAll('[data-i]').forEach(function (x) { x.onclick = function () { S.sel = +x.dataset.i; close(); refresh(); }; }); });
  }
  function depotDlg() {
    var h = S.pr.deleted.map(function (t, i) { return '<div class="list-item d-flex align-items-center" data-i="' + i + '"><span class="text-truncate">' + esc(String(t.text).substr(0, 44)) + ' <span class="text-secondary small">' + (t.deletedAt || '') + '</span></span><button class="btn btn-sm btn-outline-success ms-auto" data-r="' + i + '">Restore</button></div>'; }).join('');
    modal('Depot - deleted textdraws (' + S.pr.deleted.length + ')', (h || '<span class="text-secondary">The depot is empty</span>') + (S.pr.deleted.length ? '<div class="d-flex mt-2"><button class="btn btn-sm btn-outline-danger ms-auto" id="dclr">Empty the depot</button></div>' : ''), function (b) {
      b.querySelectorAll('[data-r]').forEach(function (x) { x.onclick = function () { snapshot(); var t = S.pr.deleted.splice(+x.dataset.r, 1)[0]; delete t.deletedAt; S.pr.textdraws.push(t); S.sel = S.pr.textdraws.length - 1; close(); refresh(); }; });
      if ($('dclr')) $('dclr').onclick = function () { snapshot(); S.pr.deleted = []; close(); refresh(); };
    });
  }
  function settingsDlg() {
    var st = S.pr.settings;
    modal('Settings', '<div class="row g-2"><div class="col-6"><label class="small">Project name</label><input class="form-control" id="sn" value="' + esc(S.pr.name) + '"></div>' +
      '<div class="col-6"><label class="small">Author</label><input class="form-control" id="sa" value="' + esc(S.pr.author) + '"></div>' +
      '<div class="col-6"><label class="small">Global array name</label><input class="form-control" id="sg" value="' + esc(st.globalVar) + '"></div>' +
      '<div class="col-6"><label class="small">Player array name</label><input class="form-control" id="sp" value="' + esc(st.playerVar) + '"></div>' +
      '<div class="col-6"><label class="small">Snap to (0 = off)</label><select class="form-select" id="ss">' + [0, 0.5, 1, 2, 5, 10].map(function (v) { return '<option' + (v === S.snap ? ' selected' : '') + '>' + v + '</option>'; }).join('') + '</select></div>' +
      '<div class="col-6 d-flex align-items-end"><div class="form-check"><input class="form-check-input" type="checkbox" id="sgr"' + (S.grid ? ' checked' : '') + '><label class="form-check-label" for="sgr">Show grid</label></div></div>' +
      '<div class="col-12 small text-secondary">Created ' + esc(S.pr.created) + ', updated ' + esc(S.pr.updated) + (S.file ? ', file ' + esc(S.file) + '.mptd' : '') + '</div>' +
      '<div class="col-12 d-flex"><button class="btn btn-primary ms-auto" id="sok">OK</button></div></div>', function () {
      $('sok').onclick = function () { var v = function (id) { return $(id).value.trim(); };
        S.pr.name = v('sn') || S.pr.name; S.pr.author = v('sa'); st.globalVar = v('sg').replace(/\W/g, '') || 'Text_Global'; st.playerVar = v('sp').replace(/\W/g, '') || 'Text_Player';
        S.snap = parseFloat(v('ss')) || 0; S.grid = $('sgr').checked; S.dirty = true; close(); refresh(); };
    });
  }
  function projectsDlg() {
    var list = Store.list();
    var h = '<div class="d-flex flex-wrap gap-1 mb-2">' +
      '<button class="btn btn-sm btn-outline-light" id="pnew">New</button><button class="btn btn-sm btn-primary" id="psave">Save</button><button class="btn btn-sm btn-outline-light" id="psaveas">Save as</button>' +
      '<button class="btn btn-sm btn-outline-light" id="pimp">Import (.mptd / .pwn / .txt)</button><button class="btn btn-sm btn-outline-info" id="pexs">Export Pawn (SA-MP)</button><button class="btn btn-sm btn-outline-info" id="pexo">Export Pawn (open.mp)</button>' +
      (N && N.share ? '<button class="btn btn-sm btn-outline-success" id="pshare">Share .mptd</button>' : '') + '</div>' +
      '<div class="small text-secondary mb-1">Saved projects' + (N ? ' (also copied to Download/MP Textdraws)' : '') + ':</div>' +
      (list.length ? list.map(function (n) { return '<div class="list-item d-flex align-items-center' + (n === S.file ? ' sel' : '') + '"><span class="text-truncate" data-o="' + esc(n) + '">' + esc(n) + '.mptd</span><button class="btn btn-sm btn-outline-danger ms-auto" data-d="' + esc(n) + '">Delete</button></div>'; }).join('') : '<span class="text-secondary">No saved projects yet</span>') +
      '<p class="small text-secondary mt-2 mb-0">Use it on your server: MP Launcher &gt; Add server &gt; Plugins &gt; type Textdraw &gt; Import file. Or open the same file in the game with /tde open.</p>';
    modal('Projects', h, function (b) {
      $('pnew').onclick = function () { var go = function () { S.pr = newProject(); S.file = ''; S.sel = -1; S.undo = []; S.redo = []; S.dirty = false; close(); refresh(); };
        if (S.dirty) confirm1('Discard the unsaved changes?', go); else go(); };
      $('psave').onclick = function () { close(); saveProject(false); };
      $('psaveas').onclick = function () { close(); saveProject(true); };
      $('pimp').onclick = function () { close(); $('file').click(); };
      $('pexs').onclick = function () { close(); var fn = safeName(S.file || S.pr.name) + '.pwn'; toast('Exported ' + Store.exportFile(fn, exportPawn(false))); };
      $('pexo').onclick = function () { close(); var fn = safeName(S.file || S.pr.name) + '_openmp.pwn'; toast('Exported ' + Store.exportFile(fn, exportPawn(true))); };
      if ($('pshare')) $('pshare').onclick = function () { if (!S.file) return toast('Save the project first'); saveProject(false); N.share(S.file); };
      b.querySelectorAll('[data-o]').forEach(function (x) { x.onclick = function () { var go = function () { close(); openProject(x.dataset.o); }; if (S.dirty) confirm1('Discard the unsaved changes?', go); else go(); }; });
      b.querySelectorAll('[data-d]').forEach(function (x) { x.onclick = function () { confirm1('Delete ' + x.dataset.d + '.mptd?', function () { Store.del(x.dataset.d); if (S.file === x.dataset.d) S.file = ''; projectsDlg(); }); }; });
    });
  }
  function confirm1(q, ok) { modal('Confirm', '<p>' + esc(q) + '</p><div class="d-flex gap-2 justify-content-end"><button class="btn btn-sm btn-outline-light" id="cno">Cancel</button><button class="btn btn-sm btn-danger" id="cyes">Yes</button></div>', function () { $('cno').onclick = close; $('cyes').onclick = function () { close(); ok(); }; }); }
  $('file').onchange = function () { var f = this.files[0]; if (!f) return; var r = new FileReader(); r.onload = function () { importText(String(r.result), f.name.replace(/\.[^.]+$/, '')); }; r.readAsText(f); this.value = ''; };

  // the Android app hands over a file opened from a file manager / share
  window.MPTDE_import = function (txt, name) { importText(txt, name || 'file'); };
  window.MPTDE_back = function () { if (!$('modal').classList.contains('d-none')) { close(); return true; } return false; };

  // auto-save every minute (like /tde in the game) once the project has a file name
  setInterval(function () { if (S.dirty && S.file) { S.pr.updated = now(); Store.save(S.file, JSON.stringify(S.pr, null, 2)); S.dirty = false; refresh(); } }, 60000);
  document.addEventListener('keydown', function (e) {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
    var t = cur();
    if (e.ctrlKey && e.key === 'z') undo(); else if (e.ctrlKey && e.key === 'y') redo(); else if (e.ctrlKey && e.key === 's') { e.preventDefault(); saveProject(false); }
    else if (t && /^Arrow/.test(e.key)) { var d = e.shiftKey ? 5 : (S.snap || 0.5); snapshot(); if (e.key === 'ArrowLeft') t.x -= d; if (e.key === 'ArrowRight') t.x += d; if (e.key === 'ArrowUp') t.y -= d; if (e.key === 'ArrowDown') t.y += d; refresh(); }
  });

  var last = ''; try { last = localStorage.getItem('mptde_last') || ''; } catch (e) {}
  if (last && Store.list().indexOf(last) >= 0) openProject(last);
  refresh(); fit(); setTimeout(fit, 100);
})();
