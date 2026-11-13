/* Moquil - the app (Android WebView + the same page on the web). One page, many views.
   Portrait = Discord mobile layout (bottom bar). Landscape / big screens = Ankama launcher layout
   (left menu, middle page, friends on the right). On the web it sits in a desktop window. */
(function (G) {
  'use strict';
  var VERSION = '1.4';
  var B = G.Moquil || null;                    // the Android bridge (null on the web)
  var IS_WEB = !B;
  var D = document, $ = function (s, r) { return (r || D).querySelector(s); }, $$ = function (s, r) { return Array.prototype.slice.call((r || D).querySelectorAll(s)); };
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function ls(k, v) { try { if (v === undefined) return JSON.parse(localStorage.getItem(k) || 'null'); if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, JSON.stringify(v)); } catch (e) { return null; } }
  function nowS() { return Math.floor(Date.now() / 1000); }
  function today() { var d = new Date(); return d.getFullYear() + '-' + (d.getMonth() + 1) + '-' + d.getDate(); }
  function ago(ts) { var s = nowS() - ts; if (s < 60) return 'now'; if (s < 3600) return Math.floor(s / 60) + 'm'; if (s < 86400) return Math.floor(s / 3600) + 'h'; return Math.floor(s / 86400) + 'd'; }
  function clock(ts) { var d = new Date(ts * 1000); return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }); }
  function dateStr(ms) { return new Date(ms).toLocaleDateString([], { year: 'numeric', month: 'short', day: 'numeric' }); }
  function logLine(s) { var L = G.MOQ_LOG = G.MOQ_LOG || []; L.push(new Date().toISOString().slice(11, 19) + ' ' + s); if (L.length > 300) L.shift(); }
  G.addEventListener('error', function (e) { logLine('error: ' + e.message); });

  // ---------- icons ----------
  var IC = {
    pin: '<path d="M9 3h6l-1 6 4 4H6l4-4z"/><path d="M12 13v8"/>', cal: '<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M3 10h18M8 3v4M16 3v4"/>', reply: '<path d="M10 8L4 13l6 5"/><path d="M4 13h10a6 6 0 0 1 6 6v1"/>', poll: '<path d="M5 20V10M12 20V4M19 20v-7"/>', role: '<path d="M12 3l8 4v5c0 5-3.5 8-8 9-4.5-1-8-4-8-9V7z"/>', trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>', slow: '<circle cx="12" cy="13" r="8"/><path d="M12 9v4l3 2M9 2h6"/>',
    home: '<path d="M3 11l9-8 9 8"/><path d="M5 10v10h5v-6h4v6h5V10"/>', search: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/>',
    store: '<path d="M4 8h16l-1.5 12h-13z"/><path d="M8 8V6a4 4 0 0 1 8 0v2"/>', library: '<rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/>',
    chat: '<path d="M21 12a8 8 0 0 1-11.6 7.1L4 20l1.1-4.6A8 8 0 1 1 21 12z"/>', user: '<circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0"/>',
    users: '<circle cx="9" cy="8" r="3.5"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0"/><circle cx="17" cy="9" r="2.8"/><path d="M16 14.2a5.5 5.5 0 0 1 6 5.8"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    bell: '<path d="M18 8a6 6 0 0 0-12 0c0 7-3 9-3 9h18s-3-2-3-9"/><path d="M13.7 21a2 2 0 0 1-3.4 0"/>', plus: '<path d="M12 5v14M5 12h14"/>',
    mic: '<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M5 10a7 7 0 0 0 14 0M12 17v4"/>', micoff: '<path d="M3 3l18 18"/><path d="M9 9v1a3 3 0 0 0 5.1 2.1M15 9.3V5a3 3 0 0 0-5.9-.7"/><path d="M17 16.9A7 7 0 0 1 5 10M19 10a7 7 0 0 1-.3 2M12 17v4"/>',
    head: '<path d="M3 14v-2a9 9 0 0 1 18 0v2"/><path d="M21 15a2 2 0 0 1-2 2h-1v-5h3zM3 15a2 2 0 0 0 2 2h1v-5H3z"/>', send: '<path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4z"/>',
    back: '<path d="M15 18l-6-6 6-6"/>', x: '<path d="M18 6L6 18M6 6l12 12"/>', star: '<path d="M12 2l3 6.5 7 .9-5 4.8 1.3 7L12 17.8 5.7 21.2 7 14.2 2 9.4l7-.9z"/>',
    dl: '<path d="M12 3v12M7 10l5 5 5-5M4 21h16"/>', info: '<circle cx="12" cy="12" r="9"/><path d="M12 8h.01M11 12h1v5h1"/>', hash: '<path d="M4 9h16M4 15h16M10 3L8 21M16 3l-2 18"/>',
    vol: '<path d="M11 5L6 9H2v6h4l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7M19 5a10 10 0 0 1 0 14"/>', gift: '<rect x="3" y="8" width="18" height="13"/><path d="M12 8v13M3 12h18M12 8S10 3 7.5 3a2.5 2.5 0 0 0 0 5M12 8s2-5 4.5-5a2.5 2.5 0 0 1 0 5"/>',
    smile: '<circle cx="12" cy="12" r="9"/><path d="M8 14s1.5 2 4 2 4-2 4-2M9 9h.01M15 9h.01"/>', image: '<rect x="3" y="3" width="18" height="18"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="M21 15l-5-5L5 21"/>',
    shield: '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>', link: '<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7"/><path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>',
    out: '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4M16 17l5-5-5-5M21 12H9"/>', palette: '<circle cx="12" cy="12" r="9"/><circle cx="7.5" cy="10.5" r="1"/><circle cx="12" cy="7.5" r="1"/><circle cx="16.5" cy="10.5" r="1"/><path d="M12 21a2.5 2.5 0 0 1 0-5h2a3 3 0 0 0 3-3"/>',
    orb: '<circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 1 0 16M8.5 9.5a4 4 0 0 1 4-2"/>', quest: '<path d="M4 4h12l-2 4 2 4H4zM4 4v17"/>', game: '<rect x="2" y="7" width="20" height="11" rx="5"/><path d="M7 11v3M5.5 12.5h3M15 12h.01M18 13.5h.01"/>',
    edit: '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/>', more: '<circle cx="5" cy="12" r="1.3"/><circle cx="12" cy="12" r="1.3"/><circle cx="19" cy="12" r="1.3"/>',
    chev: '<path d="M9 18l6-6-6-6"/>', check: '<path d="M20 6L9 17l-5-5"/>', heart: '<path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1-1.1a5.5 5.5 0 0 0-7.8 7.8L12 21l8.8-8.6a5.5 5.5 0 0 0 0-7.8z"/>',
    lock: '<rect x="4" y="11" width="16" height="10"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>', phone: '<rect x="6" y="2" width="12" height="20" rx="2"/><path d="M11 18h2"/>',
    help: '<circle cx="12" cy="12" r="9"/><path d="M9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3M12 17h.01"/>', bug: '<rect x="8" y="6" width="8" height="14" rx="4"/><path d="M19 7l-3 2M5 7l3 2M19 19l-3-2M5 19l3-2M20 13h-4M4 13h4M12 6V3"/>',
    sparkle: '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z"/><path d="M19 17l.8 2.2L22 20l-2.2.8L19 23l-.8-2.2L16 20l2.2-.8z"/>', crown: '<path d="M3 18h18l-2-10-4.5 4L12 5l-2.5 7L5 8z"/>',
    door: '<path d="M14 3h6v18h-6M10 17l5-5-5-5M15 12H3"/>', fav: '<path d="M12 2l3 6.5 7 .9-5 4.8 1.3 7L12 17.8 5.7 21.2 7 14.2 2 9.4l7-.9z"/>', party: '<path d="M5.8 11.3L2 22l10.7-3.8"/><path d="M4 3h.01M22 8h.01M15 2h.01M22 20h.01M22 2l-2.2.7a2.9 2.9 0 0 0-1.9 3.2c.1.9-.6 1.6-1.4 1.6h-.4c-.9 0-1.6.7-1.8 1.6L14 10M9 2l.6 2.4a2 2 0 0 1-1.9 2.6"/><path d="M11 13c1.9 1.9 2.4 4.5 1.2 5.7s-3.8.7-5.7-1.2S4.1 13 5.3 11.8s3.8-.7 5.7 1.2z"/>',
    copy: '<rect x="9" y="9" width="13" height="13"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>', eye: '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8S1 12 1 12z"/><circle cx="12" cy="12" r="3"/>'
  };
  function ic(n, cls) { return '<svg class="svg ' + (cls || '') + '" viewBox="0 0 24 24">' + (IC[n] || '') + '</svg>'; }

  // ---------- bridge (Android) / web fallbacks ----------
  function openUrl(u) { if (B && B.openUrl) B.openUrl(u); else G.open(u, '_blank', 'noopener'); }
  function download(url, name) {
    if (B && B.download) { B.download(url, name); toast('Downloading ' + name + ' to Download/Moquil'); }
    else { var a = D.createElement('a'); a.href = url; a.download = name; a.rel = 'noopener'; D.body.appendChild(a); a.click(); a.remove(); toast('Downloading ' + name); }
  }
  function notify(title, text) {
    if (!setv('notif')) return;
    if (B && B.notify) { if (permState('notif') === 'granted') B.notify(title, text); return; }
    if (G.Notification && Notification.permission === 'granted' && D.hidden) try { new Notification(title, { body: text, icon: 'img/logo.png' }); } catch (e) {}
  }
  var PERMS = {
    mic: { name: 'Microphone', icon: 'mic', why: 'Moquil needs the microphone for voice chat in servers, groups and the party. You can still use text chat without it.' },
    notif: { name: 'Notifications', icon: 'bell', why: 'Show a notification when a friend messages you, sends a friend request or invites you to a party.' },
    storage: { name: 'Storage', icon: 'dl', why: 'Save downloaded mods (zip files) and apps to the Download/Moquil folder.' },
    audio: { name: 'Audio', icon: 'vol', why: 'Play voice chat and sounds, and change the audio route (speaker / headset) while you talk.' }
  };
  function permState(p) {
    var nv = ls('moq_never') || {};
    if (B && B.permState) { var s = B.permState(p); if (s !== 'granted' && nv[p]) return 'never'; return s; }
    if (nv[p]) return 'never';
    if (p === 'notif') return !G.Notification ? 'granted' : Notification.permission === 'granted' ? 'granted' : Notification.permission === 'denied' ? 'denied' : 'ask';
    if (p === 'mic') return ls('moq_web_mic') || 'ask';
    return 'granted';
  }
  var permCb = {};
  G.MOQ_perm = function (p, granted) { var f = permCb[p]; delete permCb[p]; if (f) f(!!granted); if (CUR.page === 'settings') render(); };
  function requestPerm(p, cb) {
    var nv = ls('moq_never') || {}; delete nv[p]; ls('moq_never', nv);
    if (B && B.requestPerm) { permCb[p] = cb || function () {}; B.requestPerm(p); return; }
    if (p === 'notif' && G.Notification) { Notification.requestPermission().then(function (r) { cb && cb(r === 'granted'); render(); }); return; }
    if (p === 'mic' && navigator.mediaDevices) { navigator.mediaDevices.getUserMedia({ audio: true }).then(function (s) { s.getTracks().forEach(function (t) { t.stop(); }); ls('moq_web_mic', 'granted'); cb && cb(true); render(); }, function () { ls('moq_web_mic', 'denied'); cb && cb(false); render(); }); return; }
    cb && cb(true);
  }
  // the pop-up: Allow / Don't allow / Never ask again
  function askPerm(p, cb) {
    var st = permState(p); if (st === 'granted') { cb && cb(true); return; }
    var P = PERMS[p];
    dialog({
      title: 'Allow ' + P.name + '?', icon: P.icon,
      body: '<div class="perm-ic">' + ic(P.icon, 'l') + '</div><p style="text-align:center;margin:0 0 6px"><b>Moquil</b> wants to use your ' + P.name.toLowerCase() + '.</p><p class="dim small" style="text-align:center;margin:0">' + esc(P.why) + '</p>' + (st === 'never' ? '<p class="small" style="text-align:center;color:#ffb36b">You picked "Never ask" before. Allow it here, or in the phone settings.</p>' : ''),
      buttons: [
        { t: 'Never ask', cls: 'ghost', fn: function () { var nv = ls('moq_never') || {}; nv[p] = 1; ls('moq_never', nv); if (B && B.neverAsk) B.neverAsk(p); cb && cb(false); } },
        { t: "Don't allow", fn: function () { cb && cb(false); } },
        { t: 'Allow', cls: 'go', fn: function () { requestPerm(p, cb); } }
      ]
    });
  }

  // ---------- UI helpers ----------
  function toast(t) { var o = $('.toast'); if (o) o.remove(); o = D.createElement('div'); o.className = 'toast'; o.textContent = t; D.body.appendChild(o); setTimeout(function () { o.remove(); }, 2600); }
  function dialog(o) {
    var bg = D.createElement('div'); bg.className = 'dlg-bg';
    bg.innerHTML = '<div class="dlg glass"><div class="hd">' + (o.icon ? ic(o.icon, 's') : '') + '<span class="grow">' + esc(o.title) + '</span><button class="ibtn" data-x>' + ic('x', 's') + '</button></div><div class="bd">' + o.body + '</div>' +
      ((o.buttons || []).length ? '<div class="ft">' + o.buttons.map(function (b, i) { return '<button class="btn sm ' + (b.cls || '') + '" data-b="' + i + '">' + esc(b.t) + '</button>'; }).join('') + '</div>' : '') + '</div>';
    function close() { bg.remove(); }
    bg.addEventListener('click', function (e) {
      if (e.target === bg || e.target.closest('[data-x]')) { close(); o.onClose && o.onClose(); return; }
      var b = e.target.closest('[data-b]'); if (b) { var r = o.buttons[+b.dataset.b].fn(bg); if (r !== false) close(); }
    });
    D.body.appendChild(bg); o.onOpen && o.onOpen(bg); return { el: bg, close: close };
  }
  function closeDialogs() { Array.prototype.forEach.call(D.querySelectorAll('.dlg-bg'), function (x) { x.remove(); }); }
  function prompt2(title, label, val, cb, opt) {
    opt = opt || {};
    dialog({ title: title, body: '<label class="lbl">' + esc(label) + '</label>' + (opt.area ? '<textarea class="inp" id="pv" maxlength="' + (opt.max || 300) + '">' + esc(val || '') + '</textarea>' : '<input class="inp" id="pv" maxlength="' + (opt.max || 60) + '" value="' + esc(val || '') + '" placeholder="' + esc(opt.ph || '') + '">') + (opt.extra || ''),
      onOpen: function (bg) { var i = $('#pv', bg); setTimeout(function () { i.focus(); }, 50); i.addEventListener('keydown', function (e) { if (e.key === 'Enter' && !opt.area) { $('[data-b="1"]', bg).click(); } }); },
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: opt.ok || 'Save', cls: 'go', fn: function (bg) { return cb($('#pv', bg).value, bg); } }] });
  }
  // ===== Add a Server (Discord style): create your own or join with an invite =====
  function addServer() {
    dialog({ title: 'Add a Server', body: '<div class="small dim" style="text-align:center;margin:-4px 0 12px">Your server is where you and your friends hang out. Make yours and start talking.</div>' +
      '<div class="card list"><div class="li" data-b="1">' + ic('plus') + '<div class="grow"><div class="b">Create My Own</div><div class="tiny dim">A new server with #general and a voice Lounge</div></div>' + ic('chev', 's') + '</div></div>' +
      '<div class="sec" style="justify-content:center">Have an invite already?</div><div class="card list"><div class="li" data-b="2">' + ic('link') + '<div class="grow"><div class="b">Join a Server</div><div class="tiny dim">Paste an invite code from a friend</div></div>' + ic('chev', 's') + '</div></div>',
      buttons: [{ t: 'Close', fn: function () {} }, { t: 'Create', cls: 'hide', fn: function () { setTimeout(createServer, 30); } }, { t: 'Join', cls: 'hide', fn: function () { setTimeout(joinServer, 30); } }] });
  }
  function createServer() {
    var icon = '', banner = '';
    dialog({ title: 'Create Your Server', body: '<div class="small dim" style="text-align:center;margin:-4px 0 12px">Give your new server a personality with a name and an icon. You can change the icon later.</div>' +
      '<div style="display:flex;justify-content:center;margin-bottom:12px"><div class="srv-up" id="sup">' + ic('image') + '<span>ICON</span></div></div>' +
      '<div class="srv-bn" id="sbn">' + ic('image') + '<span>Add a banner (.png / .jpg, optional)</span></div>' +
      '<label class="lbl">Server name</label><input class="inp" id="sn" maxlength="40" value="' + esc((prof().display || SESSION.user) + "'s server") + '">' +
      '<label class="lbl">Description</label><textarea class="inp" id="sd" maxlength="300" placeholder="What is it about?"></textarea>' +
      '<div class="tiny dim" style="margin-top:8px">By creating a server you agree to keep it friendly for MP players.</div>',
      onOpen: function (bg) { $('#sup', bg).addEventListener('click', function () { pickIcon(function (sq) { icon = sq; var u = $('#sup', bg); u.style.background = '#222 url(' + sq + ') center / cover'; u.innerHTML = ''; }); }); $('#sbn', bg).addEventListener('click', function () { pickBanner(function (d) { banner = d; var u = $('#sbn', bg); u.style.background = '#111 url(' + d + ') center / cover'; u.innerHTML = ''; }); }); },
      buttons: [{ t: 'Back', fn: function () { setTimeout(addServer, 30); } }, { t: 'Create', cls: 'go', fn: function (bg) { var n = $('#sn', bg).value; if (!n.trim()) return false; Soc.createCommunity(n, $('#sd', bg).value).then(function (cid) { if (!cid) return; if (icon) Soc.setCommunityIcon(cid, icon); if (banner) Soc.setCommunityBanner(cid, banner); award('founder'); quest('com'); comCh = 'general'; go('community', cid); }); } }] });
  }
  function joinServer() {
    dialog({ title: 'Join a Server', body: '<div class="small dim" style="text-align:center;margin:-4px 0 12px">Enter an invite below to join an existing server.</div><label class="lbl">Invite code or link</label><input class="inp" id="ivc" placeholder="12345678-a1b2c3">' +
      '<div class="tiny dim" style="margin-top:8px">Invites look like 12345678-a1b2c3 or moquil://server/12345678-a1b2c3</div>',
      onOpen: function (bg) { setTimeout(function () { $('#ivc', bg).focus(); }, 50); },
      buttons: [{ t: 'Back', fn: function () { setTimeout(addServer, 30); } }, { t: 'Join Server', cls: 'go', fn: function (bg) {
        var cid = Soc.findCommunity($('#ivc', bg).value); if (!cid) { toast('That invite is not valid'); return false; }
        Soc.viewCommunity(cid); toast('Looking for the server...');
        var tries = 0, wait = function () { var c = Soc.community(cid); if (c && c.name) { Soc.joinCommunity(cid, true); quest('com'); comCh = 'general'; go('community', cid); } else if (++tries < 20) setTimeout(wait, 400); else toast('No server found for that invite'); };
        wait();
      } }] });
  }
  // 4.13.2: pictures come from the files app / file manager: .png and .jpg (PNG stays PNG, transparency kept)
  function pickImage(maxW, maxH, q, cb, pngOnly) {
    var i = D.createElement('input'); i.type = 'file'; i.accept = 'image/png,image/jpeg,.png,.jpg,.jpeg';
    i.onchange = function () {
      var f = i.files && i.files[0]; if (!f) return;
      var isPng = /png$/i.test(f.type) || /\.png$/i.test(f.name), isJpg = /jpe?g$/i.test(f.type) || /\.jpe?g$/i.test(f.name);
      if (!isPng && !isJpg) { toast('Pick a .png or .jpg picture'); return; }
      var r = new FileReader(); r.onload = function () {
        var im = new Image(); im.onload = function () {
          var s = Math.min(1, maxW / im.width, maxH / im.height), c = D.createElement('canvas');
          c.width = Math.max(1, Math.round(im.width * s)); c.height = Math.max(1, Math.round(im.height * s));
          c.getContext('2d').drawImage(im, 0, 0, c.width, c.height); cb(isPng ? c.toDataURL('image/png') : c.toDataURL('image/jpeg', q), isPng);
        }; im.src = r.result;
      }; r.readAsDataURL(f);
    };
    i.click();
  }
  function cropSquare(dataUrl, size, cb) {
    var png = /^data:image\/png/.test(dataUrl);
    var im = new Image(); im.onload = function () { var m = Math.min(im.width, im.height), c = D.createElement('canvas'); c.width = c.height = size; c.getContext('2d').drawImage(im, (im.width - m) / 2, (im.height - m) / 2, m, m, 0, 0, size, size); var o = png ? c.toDataURL('image/png') : c.toDataURL('image/jpeg', .85); if (o.length > 58000) o = c.toDataURL('image/jpeg', .8); cb(o); }; im.src = dataUrl;
  }
  function cropBanner(dataUrl, cb) {   // 4.13.2: server banner 3:1 (480x160), small enough for one signed message
    var im = new Image(); im.onload = function () {
      var r = 3, sw = im.width, sh = im.width / r; if (sh > im.height) { sh = im.height; sw = sh * r; }
      var c = D.createElement('canvas'); c.width = 480; c.height = 160; c.getContext('2d').drawImage(im, (im.width - sw) / 2, (im.height - sh) / 2, sw, sh, 0, 0, 480, 160);
      var q = .82, o = c.toDataURL('image/jpeg', q); while (o.length > 58000 && q > .3) { q -= .1; o = c.toDataURL('image/jpeg', q); } cb(o);
    }; im.src = dataUrl;
  }
  function pickIcon(cb) { pickImage(512, 512, .9, function (d) { cropSquare(d, 128, cb); }); }
  function pickBanner(cb) { pickImage(1200, 1200, .9, function (d) { cropBanner(d, cb); }); }
  // 4.13.2: Server Profile (owner): icon and banner from the files app (.png / .jpg), name and description
  function serverProfile(cid) {
    var c = Soc.community(cid); if (!c || c.ownerId !== Soc.id) return;
    var icon = c.icon || '', banner = c.banner || '';
    var prev = function (bg) {
      var b = $('#spb', bg); b.style.background = banner ? '#111 url(' + banner + ') center / cover' : 'linear-gradient(135deg,' + colorOf(c.cid) + ',' + colorOf(c.cid + 'x') + ')';
      var i = $('#spi', bg); i.innerHTML = srvIcon({ cid: c.cid, name: $('#spn', bg).value || c.name, icon: icon }, 80, 'border:6px solid var(--d-card);border-radius:50%;');
    };
    dialog({ title: 'Server Profile', body: '<div class="small dim" style="text-align:center;margin:-4px 0 12px">Customize how ' + esc(c.name) + ' looks to everyone. Pictures: .png or .jpg from your files.</div>' +
      '<div class="spv"><div class="ban" id="spb"></div><div class="ico" id="spi"></div></div>' +
      '<div class="row" style="gap:8px;flex-wrap:wrap;margin-top:46px"><button class="btn sm go" id="spui">' + ic('image', 's') + ' Upload icon</button><button class="btn sm" id="spri">Remove icon</button><span class="grow"></span><button class="btn sm go" id="spub">' + ic('image', 's') + ' Upload banner</button><button class="btn sm" id="sprb">Remove banner</button></div>' +
      '<label class="lbl">Server name</label><input class="inp" id="spn" maxlength="40" value="' + esc(c.name) + '">' +
      '<label class="lbl">Description</label><textarea class="inp" id="spd" maxlength="300" placeholder="What is it about?">' + esc(c.desc || '') + '</textarea>' +
      '<div class="tiny dim" style="margin-top:8px">Icon: square, shown at 128x128. Banner: 3:1, shown at the top of the channel list and in Discover.</div>',
      onOpen: function (bg) {
        prev(bg);
        $('#spui', bg).onclick = function () { pickIcon(function (d) { icon = d; prev(bg); }); };
        $('#spri', bg).onclick = function () { icon = ''; prev(bg); };
        $('#spub', bg).onclick = function () { pickBanner(function (d) { banner = d; prev(bg); }); };
        $('#sprb', bg).onclick = function () { banner = ''; prev(bg); };
        $('#spn', bg).oninput = function () { prev(bg); };
      },
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Save Changes', cls: 'go', fn: function (bg) {
        var n = $('#spn', bg).value; if (!n.trim()) { toast('The server needs a name'); return false; }
        var jobs = [Soc.updateCommunity(cid, n, $('#spd', bg).value)];
        if (icon !== (c.icon || '')) jobs.push(Soc.setCommunityIcon(cid, icon));
        if (banner !== (c.banner || '')) jobs.push(Soc.setCommunityBanner(cid, banner));
        Promise.all(jobs).then(function () { toast('Server profile saved'); render(); });
      } }] });
  }
  function stars(v, big, mod) {
    var o = '<span class="stars' + (big ? ' big' : '') + '">';
    for (var i = 1; i <= 5; i++) o += '<span class="' + (i <= Math.round(v) ? '' : 'off') + '"' + (mod ? ' data-rate="' + mod + '" data-v="' + i + '"' : '') + '>&#9733;</span>';
    return o + '</span>';
  }

  // ---------- settings / profile / wallet ----------
  var SET_DEF = { theme: 'dark', accent: '#7eb2e2', bg: '#16171a', glass: 37, blur: 3, wall: 'default', wallData: '', wallColor: 'linear-gradient(135deg,#1d2b64,#5b2a86)', dim: 45, round: false, notif: true, sounds: true, dmFriends: true, showAct: true, noiseSup: true, echo: true, inVol: 100, outVol: 100, ptt: false, compact: false };
  var SET = Object.assign({}, SET_DEF, ls('moq_settings') || {});
  function setv(k) { return SET[k]; }
  function saveSet() { ls('moq_settings', SET); applyLook(); }
  function prof() { return Object.assign({ display: '', pronouns: '', bio: '', status: { t: '', e: '', until: 0 }, pres: 'online', avatar: '', banner: '', bannerColor: '#5865f2', deco: '', effect: '', frame: '', nameplate: '', nametag: '', shown: [] }, ls('moq_profile') || {}); }
  function saveProf(p) { ls('moq_profile', p); if (G.Soc && Soc.id) Soc.publishRich(); }
  function wallet() { return Object.assign({ orbs: 300, owned: [], badges: [], q: { day: '', done: {}, claimed: {} }, once: {} }, ls('moq_wallet') || {}); }
  function saveWallet(w) { ls('moq_wallet', w); }
  function memberSince() { var s = ls('moq_since') || {}; var u = (SESSION && SESSION.user || '').toLowerCase(); return s[u] || s._first || Date.now(); }
  (function () { var s = ls('moq_since') || {}; if (!s._first) { s._first = Date.now(); ls('moq_since', s); } })();

  var STORE = [
    { cat: 'deco', title: 'Avatar decorations', items: [['deco-ring', 'Gold ring', 200], ['deco-neon', 'Neon ring', 250], ['deco-fire', 'Fire ring', 400], ['deco-hd', 'Purple haze', 300], ['deco-crown', 'Crown', 600], ['deco-halo', 'Halo', 500], ['deco-samurai', 'Samurai kabuto', 900], ['deco-ronin', 'Ronin hat and katana', 800], ['deco-oni', 'Oni mask', 700], ['deco-jacket', 'Leather jacket', 650], ['deco-grove', 'Grove Street jacket', 650], ['deco-hoodie', 'Hoodie', 450], ['deco-sparkle', 'Golden sparkles', 550], ['deco-lights', 'String lights', 550], ['deco-hearts', 'Love hearts', 500], ['deco-bubbles', 'Bubbles and sparks', 550], ['deco-sakura', 'Sakura branch', 600], ['deco-headphones', 'Headphones', 450], ['deco-catears', 'Cat ears', 400]] },
    { cat: 'effect', title: 'Profile effects', items: [['fx-stars', 'Starry night', 300], ['fx-glow', 'Blue glow', 250], ['fx-fire', 'Embers', 400], ['fx-rain', 'Rain', 300], ['fx-hearts', 'Hearts', 350], ['fx-sakura', 'Sakura petals', 500], ['fx-bubbles', 'Bubbles', 450], ['fx-sparkle', 'Gold glitter', 500], ['fx-lights', 'String lights', 500]] },
    { cat: 'frame', title: 'Profile frames', items: [['frame-gold', 'Gold frame', 300], ['frame-neon', 'Neon frame', 300], ['frame-grove', 'Grove Street', 250], ['frame-temple', 'Temple', 600]] },
    { cat: 'nameplate', title: 'Nameplates', items: [['np-gold', 'Gold', 200], ['np-neon', 'Neon', 250], ['np-fire', 'Fire', 300], ['np-grove', 'Grove', 200], ['np-ice', 'Ice', 250]] },
    { cat: 'nametag', title: 'Nametags', items: [['nt-gold', 'Gold glow', 250], ['nt-neon', 'Neon', 250], ['nt-rainbow', 'Rainbow', 500], ['nt-fire', 'Fire', 300]] }
  ];
  function storeItem(id) { for (var i = 0; i < STORE.length; i++) for (var j = 0; j < STORE[i].items.length; j++) if (STORE[i].items[j][0] === id) return { cat: STORE[i].cat, id: id, name: STORE[i].items[j][1], price: STORE[i].items[j][2] }; return null; }
  var BADGES = {
    member: ['\u{1F7E2}', 'Moquil member', 'Made a Moquil account'], early: ['\u{1F331}', 'Early supporter', 'Joined Moquil in its first year'], linked: ['\u{1F3AE}', 'Linked MP game', 'Linked Moquil with the MP game'],
    friends5: ['\u{1F91D}', 'Social', 'Has 5 friends'], founder: ['\u{1F3F0}', 'Server founder', 'Made a server'], critic: ['\u2B50', 'Critic', 'Rated a mod'],
    voice: ['\u{1F399}', 'On the mic', 'Joined a voice room'], quests10: ['\u{1F3C6}', 'Quest master', 'Finished 10 quests'], collector: ['\u{1F48E}', 'Collector', 'Owns 5 store items'], hivemq: ['\u{1F41D}', 'HiveMQ powered', 'Connected over HiveMQ']
  };
  function award(b) { var w = wallet(); if (w.badges.indexOf(b) >= 0) return; w.badges.push(b); saveWallet(w); toast('Badge unlocked: ' + BADGES[b][1]); if (G.Soc && Soc.id) Soc.publishRich(); }
  var QUESTS = [
    ['open', 'Open Moquil today', 50], ['msg', 'Send a message', 50], ['friend', 'Add or accept a friend', 100], ['com', 'Join or make a server', 100], ['rate', 'Rate a mod', 50], ['voice', 'Join a voice room', 75], ['dl', 'Download a mod', 50]
  ];
  function quest(k) {
    var w = wallet(); if (w.q.day !== today()) w.q = { day: today(), done: {}, claimed: {} };
    if (!w.q.done[k]) { w.q.done[k] = 1; saveWallet(w); var q = QUESTS.filter(function (x) { return x[0] === k; })[0]; if (q && k !== 'open') toast('Quest done: ' + q[1] + ' (claim ' + q[2] + ' Orbs in Quests)'); }
  }
  G.MoqProfile = function () {
    var p = prof(), w = wallet();
    return { v: 2, display: p.display, pronouns: p.pronouns, bio: p.bio, status: (p.status.until && p.status.until < Date.now()) ? { t: '', e: '' } : { t: p.status.t, e: p.status.e, until: p.status.until }, pres: p.pres,
      avatar: p.avatar, banner: p.banner, bannerColor: p.bannerColor, deco: p.deco, effect: p.effect, frame: p.frame, nameplate: p.nameplate, nametag: p.nametag,
      badges: w.badges.slice(0, 12), since: memberSince(), app: 'Moquil ' + VERSION };
  };

  // ---------- look (theme, glass, wallpaper) ----------
  var mqDark = G.matchMedia ? G.matchMedia('(prefers-color-scheme: light)') : null;
  function applyLook() {
    var h = D.documentElement, t = SET.theme;
    if (t === 'system') t = mqDark && mqDark.matches ? 'light' : 'dark';
    h.classList.remove('t-light', 't-black', 't-white');
    if (t === 'light') h.classList.add('t-light'); if (t === 'black') h.classList.add('t-black'); if (t === 'white') h.classList.add('t-white');
    h.classList.toggle('round', !!SET.round);
    var st = h.style;
    st.setProperty('--glass-a', (SET.glass / 100).toFixed(2)); st.setProperty('--glass-b', Math.min(.98, SET.glass / 100 + .22).toFixed(2));
    st.setProperty('--blur', SET.blur + 'px'); st.setProperty('--wall-dim', (SET.dim / 100).toFixed(2));
    st.setProperty('--accent', SET.theme === 'custom' ? SET.accent : ''); st.setProperty('--blue', SET.theme === 'custom' ? SET.accent : '');
    if (SET.theme === 'custom') st.setProperty('--bg', SET.bg); else st.removeProperty('--bg');
    var w = SET.wall === 'none' ? 'none' : SET.wall === 'color' ? SET.wallColor : SET.wall === 'custom' && SET.wallData ? 'url(' + SET.wallData + ')' : 'url(' + new URL('img/ui_bg.jpg', location.href).href + ')';
    st.setProperty('--wall', w);
  }
  if (mqDark && mqDark.addEventListener) mqDark.addEventListener('change', applyLook);
  function layout() {
    var h = D.documentElement, w = G.innerWidth, ht = G.innerHeight;
    h.classList.toggle('portrait', w < 700); h.classList.toggle('wide', w >= 900 && ht >= 520); h.classList.toggle('web', IS_WEB);
  }
  G.addEventListener('resize', function () { var was = D.documentElement.className; layout(); if (D.documentElement.className !== was && SESSION) render(); });

  // ================= AUTH (login / register / forgot / reset) =================
  var SESSION = ls('moq_session');
  var AC = null;   // MP account bot client
  function acct(handlers) {
    if (AC) AC.stop();
    AC = new MPAuth.Client(function (a) {
      if (a.op === 'link') st('Connected to the MP account server (' + a.broker + ').', 'ok');
      else if (a.op === 'down') st('Connection lost, trying another server...', 'bad');
      else if (a.op === 'slow') st('Still connecting (' + a.broker + ')...', '');
      var f = handlers[a.op]; if (f) f(a);
    });
    st('Connecting to the MP account server...'); AC.start(); return AC;
  }
  function st(t, cls) { var s = $('#status'); if (s) { s.textContent = t; s.className = 'status ' + (cls || ''); } }
  function busy(on) { $$('#auth button[type=submit]').forEach(function (b) { b.disabled = !!on; }); }
  function authShell(title, sub, inner, foot, qr) {   // 4.13.1: the Discord login (brand top left, starry purple sky, dark card, QR on the side)
    return '<div class="dauth"><div class="sky"><i class="s1"></i><i class="s2"></i><i class="s3"></i><i class="orb o1"></i><i class="orb o2"></i></div><div class="dbrand"><img src="img/logo.png" alt="">Moquil</div>' +
      '<div class="dcard' + (qr ? ' hasqr' : '') + '"><div class="dform"><h1>' + title + '</h1><div class="sub">' + sub + '</div>' + inner + '<div id="status" class="status"></div><div class="dfoot">' + foot + '</div></div>' +
      (qr ? '<div class="dqr"><div class="qrbox"><img src="img/qr.png" alt="QR code"></div><h2>Open Moquil with a QR Code</h2><p>Scan this with your phone camera to open <b>Moquil</b> on the phone too.</p><a href="#" data-open="discord">Or get help in the MP Discord</a></div>' : '') + '</div></div>';
  }
  function discordBtns() { return '<div class="row" style="margin-top:8px"><button type="button" class="btn sm" id="again">New code</button><span class="grow"></span><button type="button" class="btn sm blue" data-open="discord">Open Discord</button></div>'; }
  function codeFlow(c, onVerified) {
    var rid = '', code = '';
    function gen() { code = MPAuth.Client.code(); $('#code').textContent = '/gen ' + code; rid = c.send({ op: 'gen', code: code }); }
    gen(); $('#again').onclick = gen;
    return { gen: function (a) { if (a.rid !== rid) return; if (a.ok === '1') { st('Waiting for /gen in Discord... (the code works 10 minutes)', 'ok'); return; } if (a.err === 'dup') { gen(); return; } st(a.err || 'The code was not accepted.', 'bad'); }, verified: function (a) { if (a.vtoken) onVerified(a); } };
  }
  function showAuth(view) {
    $('#app').classList.add('hide'); var A = $('#auth'); A.classList.remove('hide');
    if (view === 'register') {
      A.innerHTML = authShell('Create an account', 'One MP account for the game, Moquil and the MP apps',
        '<div id="step1"><p class="small dim" style="margin:0 0 8px">1. Join the <a href="#" data-open="discord">MP Discord server</a>.<br>2. In the #verification channel type:</p><div id="code" class="code">/gen ........</div>' + discordBtns() + '</div>' +
        '<form id="step2" class="hide" autocomplete="off"><div class="chip green" id="dname"></div>' +
        '<label class="lbl">Account name</label><input id="user" class="inp" required minlength="3" maxlength="24" pattern="[A-Za-z0-9_\\[\\].$=@()]+">' +
        '<div class="row"><div class="grow"><label class="lbl">Password</label><input id="pass" type="password" class="inp" required minlength="6"></div><div class="grow"><label class="lbl">Repeat</label><input id="pass2" type="password" class="inp" required></div></div>' +
        '<div class="row"><div class="grow"><label class="lbl">Gender</label><select id="gender" class="inp"><option value="male">Male</option><option value="female">Female</option></select></div><div class="grow"><label class="lbl">Age</label><input id="age" type="number" class="inp" min="13" max="99" value="18" required></div></div>' +
        '<button type="submit" class="btn go wide dbtn">Continue</button></form>',
        '<a href="#" data-auth="login">Already have an account?</a>');
      var vtoken = '', flow;
      var c = acct({ link: function () { if (!flow) flow = codeFlow(c, function (a) {
          if (a.vtoken === vtoken) return; vtoken = a.vtoken;
          if (a.has === '1') { st('Discord verified (' + (a.dname || '') + '). You already have the account ' + a.user + ': log in.', 'ok'); setTimeout(function () { showAuth('login'); $('#user').value = a.user; }, 2200); return; }
          $('#step1').classList.add('hide'); $('#step2').classList.remove('hide'); $('#dname').textContent = 'Discord verified: ' + (a.dname || 'ok'); st('');
        }); },
        gen: function (a) { flow && flow.gen(a); }, verified: function (a) { flow && flow.verified(a); },
        signup: function (a) { busy(false); if (a.ok === '1') { var u = a.user || $('#user').value.trim(); var s = ls('moq_since') || {}; s[u.toLowerCase()] = Date.now(); ls('moq_since', s); loggedIn(u, $('#pass').value, true); } else st(a.err || 'Sign up failed.', 'bad'); } });
      $('#step2').onsubmit = function (e) {
        e.preventDefault(); if ($('#pass').value !== $('#pass2').value) { st('The passwords are not the same.', 'bad'); return; }
        if (!this.checkValidity()) { st('Account name: 3-24 letters, digits or _ . Password: 6 or more.', 'bad'); return; }
        busy(true); st('Creating the account...');
        c.send({ op: 'signup', vtoken: vtoken, user: $('#user').value.trim(), pass: $('#pass').value, gender: $('#gender').value, age: String($('#age').value | 0), chal: MPAuth.Client.chal(), srv: 'moquil' });
      };
      return;
    }
    if (view === 'forgot') {
      A.innerHTML = authShell('Forgot password', 'Prove the account is yours with its Discord account',
        '<p class="small dim" style="margin:0 0 8px">In the #verification channel of the <a href="#" data-open="discord">MP Discord server</a> type:</p><div id="code" class="code">/gen ........</div>' + discordBtns() +
        '<form id="rs" class="hide" autocomplete="off"><div class="chip green" id="who"></div><label class="lbl">New password</label><input id="pass" type="password" class="inp" required minlength="6"><label class="lbl">Confirm password</label><input id="pass2" type="password" class="inp" required>' +
        '<label class="chk" style="margin-top:10px"><input type="checkbox" id="lo" checked> Log out all devices</label><button type="submit" class="btn go wide dbtn">Reset Password</button></form>',
        '<a href="#" data-auth="login">Back to Log In</a>');
      var r = null, flow2, t2 = 0;
      var c2 = acct({ link: function () { if (!flow2) flow2 = codeFlow(c2, function (a) {
          if (a.has !== '1') { st('This Discord account has no MP account yet. Create one.', 'bad'); setTimeout(function () { showAuth('register'); }, 2200); return; }
          r = a; $('#code').parentNode.querySelectorAll('#code, .row, p').forEach(function (x) { x.classList.add('hide'); }); $('#rs').classList.remove('hide');
          $('#who').textContent = 'Account ' + a.user + (a.dname ? ' (Discord ' + a.dname + ')' : ''); st('');
        }); },
        gen: function (a) { flow2 && flow2.gen(a); }, verified: function (a) { flow2 && flow2.verified(a); },
        reset: function (a) { clearTimeout(t2); busy(false); if (a.ok === '1') { st('Password changed. Log in with the new one.', 'ok'); setTimeout(function () { showAuth('login'); $('#user').value = r.user; }, 1600); } else st(a.err || 'The password was not changed.', 'bad'); } });
      $('#rs').onsubmit = function (e) {
        e.preventDefault(); if ($('#pass').value.length < 6 || $('#pass').value !== $('#pass2').value) { st('6 or more characters, the same in both boxes.', 'bad'); return; }
        busy(true); st('Changing the password...');
        c2.send({ op: 'reset', vtoken: r.vtoken, user: r.user, pass: $('#pass').value, logout: $('#lo').checked ? '1' : '0' });
        t2 = setTimeout(function () { busy(false); st('The account server did not answer. Ask the staff in the MP Discord.', 'bad'); }, 20000);
      };
      return;
    }
    // login (Ankama "Welcome back")
    var last = ls('moq_last_user') || '';
    A.innerHTML = authShell('Welcome back', 'We\'re so excited to see you again',
      '<form id="lf" autocomplete="on"><label class="lbl">MP Account Name <em>*</em></label><input id="user" class="inp" required maxlength="24" autocomplete="username" value="' + esc(last) + '">' +
      '<label class="lbl">Password <em>*</em></label><div class="pwbox"><input id="pass" type="password" class="inp" required autocomplete="current-password"><button type="button" class="eye" id="eye" title="Show password">' + ic('eye', 's') + '</button></div>' +
      '<a href="#" data-auth="forgot" class="small fgt">Forgot your password?</a>' +
      '<label class="chk hide"><input type="checkbox" id="rem" checked> Remember me</label>' +
      '<button type="submit" class="btn go wide dbtn">Log In</button></form>',
      'Need an account? <a href="#" data-auth="register">Register</a>', true);
    $('#eye').onclick = function () { var pi = $('#pass'); pi.type = pi.type === 'password' ? 'text' : 'password'; };
    var t = 0;
    var c3 = acct({ login: function (a) {
      clearTimeout(t); busy(false);
      if (a.ok === '1') loggedIn(a.user || $('#user').value.trim(), $('#pass').value, $('#rem').checked); else st(a.err || 'Login failed.', 'bad');
    } });
    $('#lf').onsubmit = function (e) {
      e.preventDefault(); var u = $('#user').value.trim(), p = $('#pass').value; if (!u || !p) { st('Type your account name and password.', 'bad'); return; }
      busy(true); st('Logging in...');
      c3.send({ op: 'login', user: u, pass: p, chal: MPAuth.Client.chal(), srv: 'moquil' });
      t = setTimeout(function () { busy(false); st('No answer from the account server. Try again.', 'bad'); }, 20000);
    };
    if (!last) setTimeout(function () { var u = $('#user'); u && u.focus(); }, 60);
  }
  async function loggedIn(user, pass, remember) {
    st('Loading your Moquil profile...', 'ok'); busy(true);
    try { await Soc.useAccount(user, pass); } catch (e) { logLine('account key: ' + e.message); }
    if (AC) { AC.stop(); AC = null; }
    SESSION = { user: user, at: Date.now(), keep: remember !== false };
    // remembered: stays logged in until Log Out (the web keeps it in this browser, the app on the phone)
    if (remember !== false) ls('moq_session', SESSION); else { ls('moq_session', null); try { sessionStorage.setItem('moq_session', JSON.stringify(SESSION)); } catch (e) {} }
    ls('moq_last_user', user);
    boot();
  }
  function logout() {
    dialog({ title: 'Log out', icon: 'out', body: '<p style="margin:0">Log out of Moquil on this ' + (IS_WEB ? 'browser' : 'phone') + '? Your friends, chats and profile stay with your MP account.</p>',
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Log out', cls: 'red', fn: function () {
        try { if (G.MoqVoice) MoqVoice.leave(); Soc.backupNow(); } catch (e) {}
        setTimeout(function () { try { Soc.stop(); } catch (e) {} ls('moq_session', null); try { sessionStorage.removeItem('moq_session'); } catch (e) {} ls('moq_key', null); SESSION = null; location.hash = ''; location.reload(); }, 700);
      } }] });
  }

  // ================= APP SHELL =================
  var CUR = { page: 'home', tab: 'general', sub: '', arg: '' };
  var NAV = [['home', 'Home', 'home'], ['search', 'Search', 'search'], ['store', 'Store', 'store'], ['library', 'Library', 'library'], ['social', 'Social', 'chat'], ['profile', 'You', 'user']];
  async function boot() {
    $('#auth').classList.add('hide'); $('#app').classList.remove('hide');
    var since = ls('moq_since') || {}; var u = SESSION.user.toLowerCase(); if (!since[u]) { since[u] = since._first || Date.now(); ls('moq_since', since); }
    award('member'); if (new Date(memberSince()).getFullYear() <= 2027) award('early');
    quest('open');
    var p = prof(); if (!p.display) { p.display = SESSION.user; ls('moq_profile', p); }
    renderShell(); route();
    try { await Soc.start(SESSION.user); } catch (e) { logLine('soc start: ' + e.message); toast('Social could not start: ' + e.message); }
    Soc.on('change', soon); Soc.on('presence', soon); Soc.on('people', soon); Soc.on('profile', onProfile); Soc.on('rating', soon); Soc.on('post', soon); Soc.on('restored', function () { toast('Your friends and chats are back'); soon(); });
    Soc.on('link', function (on) { if (on) award('hivemq'); soonShell(); });
    Soc.on('msg', function (x) { if (!x.msg.mine && !(CUR.page === 'chat' && CUR.arg === x.key && !D.hidden)) { var c = Soc.conv(x.key); notify(c ? (c.title || x.msg.fromName) : x.msg.fromName, x.msg.fromName + ': ' + x.msg.text); } });
    Soc.on('notif', function (n) { if (n.kind === 'freq' || n.kind === 'pinv' || n.kind === 'group') notify('Moquil', n.text); });
    MOQ_MODS.forEach(function (m) { Soc.watchRatings(m.id); });
    if (G.MoqVoice) MoqVoice.onChange(soon);
    setInterval(checkStatusClear, 30000); checkStatusClear();
    // first start: the permission pop-ups (each only once; "Never ask" is kept)
    var asked = ls('moq_asked') || {};
    var queue = ['notif', 'mic'].filter(function (p) { return !asked[p] && permState(p) === 'ask'; });
    (function next() { var p = queue.shift(); if (!p) return; asked[p] = 1; ls('moq_asked', asked); setTimeout(function () { askPerm(p, next); }, 900); })();
  }
  function onProfile(id) {
    if (id === Soc.id) {   // my profile came back from the broker (a new device / browser): use it if this one is empty
      var mine = ls('moq_profile'), r = Soc.profile(id);
      if (r && (!mine || !mine.restored) && (!mine || (!mine.bio && !mine.avatar && !mine.pronouns))) {
        var p = prof(); ['display', 'pronouns', 'bio', 'status', 'pres', 'avatar', 'banner', 'bannerColor', 'deco', 'effect', 'frame', 'nameplate', 'nametag'].forEach(function (k) { if (r[k] !== undefined && r[k] !== '') p[k] = r[k]; });
        p.restored = 1; ls('moq_profile', p);
        var w = wallet(); (r.badges || []).forEach(function (b) { if (w.badges.indexOf(b) < 0 && BADGES[b]) w.badges.push(b); });
        ['deco', 'effect', 'frame', 'nameplate', 'nametag'].forEach(function (k) { if (r[k] && w.owned.indexOf(r[k]) < 0) w.owned.push(r[k]); }); saveWallet(w);
        if (r.since) { var s = ls('moq_since') || {}; var u = SESSION.user.toLowerCase(); if (!s[u] || r.since < s[u]) { s[u] = r.since; ls('moq_since', s); } }
      }
    }
    soon();
  }
  function checkStatusClear() { var p = prof(); if (p.status && p.status.until && p.status.until < Date.now() && (p.status.t || p.status.e)) { p.status = { t: '', e: '', until: 0 }; saveProf(p); soon(); } }
  var rT = 0; function soon() { clearTimeout(rT); rT = setTimeout(function () { soonShell(); refresh(); }, 120); }
  function soonShell() { renderSide(); renderRightD(); renderNav(); renderRail(); renderUbar(); }
  // ---- 4.13.1: Discord mobile layout (portrait): server rail on the left, Messages, user bar at the bottom ----
  function isPortrait() { return D.documentElement.classList.contains('portrait'); }
  function renderRail() {
    var r = $('#rail'); if (!r) return; var u = Soc.id ? Soc.unreadChats() + Soc.S.inc.length : 0;
    var on = function (k) { return CUR.page === k || (k === 'dms' && /^(chat|social)$/.test(CUR.page)); };
    var it = function (k, inner, cls, tip, badge, attr) { return '<div class="ri' + (k && on(k) ? ' on' : '') + (cls ? ' ' + cls : '') + '" ' + (attr || 'data-go="' + k + '"') + ' title="' + esc(tip) + '"><i class="pill"></i><div class="sq">' + inner + '</div>' + (badge ? '<span class="badge">' + badge + '</span>' : '') + '</div>'; };
    var coms = Soc.id ? Soc.communities().filter(function (c) { return c.joined || c.ownerId === Soc.id; }) : [];
    var P = isPortrait(), logo = '<img src="img/logo.png" alt="" style="width:100%;height:100%;object-fit:cover">';
    var home = '<div class="ri logo' + (on(P ? 'dms' : 'friends') || (!P && /^(friends|chat|home|library|store|search|user|profile|settings)$/.test(CUR.page)) ? ' on' : '') + '" data-go="' + (P ? 'dms' : 'friends') + '" title="Direct Messages"><i class="pill"></i><div class="sq">' + logo + '</div>' + (u ? '<span class="badge">' + u + '</span>' : '') + '</div>';
    r.innerHTML = home + (P ? it('home', ic('home'), '', 'Home') + it('store', ic('store'), '', 'Shop') + it('library', ic('library'), '', 'Library') : '') + '<div class="rsep"></div>' +
      coms.map(function (c) { var un = (c.posts || []).filter(function (x) { return x.ts > (c.seen || 0) && x.fromId !== Soc.id; }).length; return '<div class="ri srv' + (CUR.page === 'community' && CUR.arg === c.cid ? ' on' : '') + (un ? ' unread' : '') + '" data-com="' + esc(c.cid) + '" title="' + esc(c.name) + '"><i class="pill"></i><div class="sq"' + (c.icon ? ' style="background:#222 url(' + c.icon + ') center / cover"></div>' : ' style="background:' + colorOf(c.cid) + ';color:#fff;font-weight:800;font-size:' + (srvInit(c.name).length > 2 ? 13 : 16) + 'px">' + srvInit(c.name) + '</div>') + (un ? '<span class="badge">' + un + '</span>' : '') + '</div>'; }).join('') +
      it('', ic('plus'), 'add', 'Add a Server', 0, 'data-act="newcom"') + it('discover', ic('search'), 'add', 'Discover servers') + (P ? '' : it('library', ic('dl'), 'add', 'Get the MP apps'));
  }
  function renderUbar() {
    var b = $('#ubar'); if (!b || !SESSION) return; var p = prof(), n = Soc.id ? Soc.unreadInbox() : 0, w = wallet();
    var badge = (w.badges || [])[0], bd = badge && BADGES[badge];
    b.className = 'ubar ' + esc(p.nameplate || '');
    b.innerHTML = '<div class="me" data-go="profile">' + avatar(Soc.id || '', SESSION.user, p, 'av l', true) + '<div class="grow"><div class="row" style="gap:6px"><span class="b ell ' + esc(p.nametag || '') + '" style="font-size:16px">' + esc(p.display || SESSION.user) + '</span>' + (bd ? '<span class="tagchip">' + esc(bd[0] + ' ' + String(bd[1]).split(' ')[0]) + '</span>' : '') + ic('chev', 's') + '</div><div class="small dim ell">' + esc(statusLine(p) || presName(p.pres)) + '</div></div></div>' +
      '<button class="ibtn round donly" data-act="vmute" title="Mute">' + ic(G.MoqVoice && MoqVoice.state.muted ? 'micoff' : 'mic') + '</button><button class="ibtn round donly" data-act="vdeaf" title="Deafen">' + ic('head') + '</button>' + '<button class="ibtn round" data-act="inbox" title="Inbox">' + ic('bell') + (n ? '<span class="badge">' + n + '</span>' : '') + '</button><button class="ibtn round" data-go="settings" title="Settings">' + ic('gear') + '</button>';
  }

  function renderShell() {
    var A = $('#app');
    A.innerHTML = '<div class="win glass"><div class="titlebar"><div class="lights"><i></i><i></i><i></i></div><div class="ttl">Moquil - ' + esc(SESSION.user) + '</div><span class="tiny dim">web</span></div>' +
      '<div class="mp-bar"><b>PLAY</b><span>Moquil ' + VERSION + ' &middot; <span id="netst">connecting...</span></span><i>powered by HiveMQ/ThirtyFoxMC</i></div>' +
      '<div class="shell"><aside class="rail scroll" id="rail"></aside><aside class="side" id="side"></aside><main class="main"><div class="topbar" id="top"></div><div class="page scroll" id="page"></div></main><aside class="right" id="right"></aside></div></div>' +
      '<nav class="nav" id="nav"></nav><div class="ubar" id="ubar"></div>';
    soonShell();
  }
  function unreadSocial() { return (Soc.id ? Soc.unreadChats() + Soc.unreadInbox() + Soc.S.inc.length : 0); }
  function renderNav() {
    var n = $('#nav'); if (!n) return; var u = unreadSocial();
    n.innerHTML = NAV.map(function (x) { return '<div class="ni' + (CUR.page === x[0] || (x[0] === 'social' && /chat|community/.test(CUR.page)) || (x[0] === 'profile' && CUR.page === 'settings') ? ' on' : '') + '" data-go="' + x[0] + '">' + (x[0] === 'profile' ? miniAv() : ic(x[2])) + '<span>' + x[1] + '</span>' + (x[0] === 'social' && u ? '<span class="badge">' + u + '</span>' : '') + '</div>'; }).join('');
    var ns = $('#netst'); if (ns) ns.innerHTML = Soc.id ? (Soc.up() ? '<span style="color:#6ec85a">online</span> &middot; ID ' + Soc.id : 'connecting to ' + esc(Soc.broker || 'HiveMQ') + '...') : 'starting...';
  }
  function miniAv() { var p = prof(); return '<div class="av s" style="width:24px;height:24px">' + avPic(p.avatar, SESSION.user) + '<span class="dot ' + presCls(p.pres, true) + '" style="border-width:2px"></span></div>'; }
  function renderSideOld() {
    var s = $('#side'); if (!s) return; var p = prof(), u = unreadSocial();
    var item = function (k, t, i, b) { return '<div class="nav-i' + (CUR.page === k || (k === 'social' && /chat|community/.test(CUR.page)) ? ' on' : '') + '" data-go="' + k + '">' + ic(i) + '<span>' + t + '</span>' + (b ? '<span class="badge">' + b + '</span>' : '') + '</div>'; };
    var favMods = (ls('moq_favmods') || ['mp', 'mlg', 'camfx']).map(function (id) { return MOQ_MODS.filter(function (m) { return m.id === id; })[0]; }).filter(Boolean);
    var coms = Soc.id ? Soc.communities().filter(function (c) { return c.joined || c.ownerId === Soc.id; }).slice(0, 6) : [];
    s.innerHTML = '<div class="me" data-go="profile">' + avatar(Soc.id || '', SESSION.user, p, 'av s', true) + '<div class="grow"><div class="b ell ' + esc(p.nametag) + '">' + esc(p.display || SESSION.user) + '</div><div class="tiny dim ell">' + esc(statusLine(p) || presName(p.pres)) + '</div></div>' + ic('chev', 's') + '</div>' +
      '<div class="scroll grow" style="padding:6px 0">' + item('home', 'Home', 'home') + item('search', 'Search', 'search') + item('store', 'Store', 'store') + item('library', 'Library', 'library') + item('social', 'Social', 'chat', u) + item('settings', 'Settings', 'gear') +
      '<div class="grp">My apps</div>' + favMods.map(function (m) { return '<div class="mini" data-mod="' + m.id + '"><div class="ic" style="background-image:url(' + m.cover + ')"></div><div class="grow ell">' + esc(m.name) + '</div></div>'; }).join('') +
      '<div class="grp">My servers<a href="#" data-act="newcom">' + ic('plus', 's') + '</a></div>' + (coms.length ? coms.map(function (c) { return '<div class="mini' + (CUR.arg === c.cid ? ' on' : '') + '" data-com="' + esc(c.cid) + '"><div class="ic" style="background:' + colorOf(c.cid) + ';display:flex;align-items:center;justify-content:center;font-weight:800;color:#fff">' + esc(c.name[0] || '#') + '</div><div class="grow ell">' + esc(c.name) + '</div></div>'; }).join('') : '<div class="tiny dim" style="padding:2px 16px">None yet</div>') +
      '</div><div class="footer-link">' + ic('sparkle', 's') + '<span>By ThirtyFox &middot; Powered by HiveMQ</span></div>';
  }
  // right panel (Ankama friends list)
  var friendQ = '', friendTab = 'friends';
  function renderRight() {
    var r = $('#right'); if (!r || !Soc.id) return;
    var keepFocus = D.activeElement && D.activeElement.id === 'fq';
    var fr = Soc.S.friends.filter(function (f) { return !friendQ || f.name.toLowerCase().indexOf(friendQ.toLowerCase()) >= 0 || f.id.indexOf(friendQ) === 0; });
    var favs = fr.filter(function (f) { return Soc.S.favs.indexOf(f.id) >= 0; }), rest = fr.filter(function (f) { return Soc.S.favs.indexOf(f.id) < 0; });
    rest.sort(function (a, b) { return (Soc.isOnline(b.id) ? 1 : 0) - (Soc.isOnline(a.id) ? 1 : 0) || a.name.localeCompare(b.name); });
    var reqs = Soc.S.inc.length;
    var html = '<div class="topbar" style="gap:4px"><div class="tab' + (friendTab === 'friends' ? ' on' : '') + '" data-ftab="friends">Friends</div><div class="tab' + (friendTab === 'req' ? ' on' : '') + '" data-ftab="req">Requests' + (reqs ? ' <span class="badge">' + reqs + '</span>' : '') + '</div><span class="grow"></span><button class="ibtn" data-act="addfriend" title="Add a friend">' + ic('plus', 's') + '</button></div><div style="padding:10px"><input class="inp" id="fq" placeholder="Search a friend" value="' + esc(friendQ) + '"></div><div class="scroll grow">';
    if (friendTab === 'req') {
      html += Soc.S.inc.map(function (f) { return '<div class="mini">' + avatar(f.id, f.name, Soc.profile(f.id), 'av s') + '<div class="grow"><div class="ell b">' + esc(f.name) + '</div><div class="tiny dim">#' + f.id + '</div></div><button class="ibtn" data-acc="' + f.id + '">' + ic('check', 's') + '</button><button class="ibtn" data-dec="' + f.id + '">' + ic('x', 's') + '</button></div>'; }).join('') +
        Soc.S.out.map(function (f) { return '<div class="mini">' + avatar(f.id, f.name, null, 'av s') + '<div class="grow"><div class="ell">' + esc(f.name) + '</div><div class="tiny dim">Sent &middot; #' + f.id + '</div></div><button class="ibtn" data-can="' + f.id + '">' + ic('x', 's') + '</button></div>'; }).join('');
      if (!Soc.S.inc.length && !Soc.S.out.length) html += '<div class="empty small">No friend requests</div>';
    } else {
      if (favs.length) html += '<div class="sec" style="padding:0 12px">Favorites<span class="count">' + favs.length + '</span></div>' + favs.map(friendRow).join('');
      html += '<div class="sec" style="padding:0 12px">Friends<span class="count">' + rest.length + '</span></div>' + (rest.length ? rest.map(friendRow).join('') : '<div class="empty small">Add friends with their 8 digit ID.<br>Your ID: <b>' + Soc.id + '</b></div>');
    }
    r.innerHTML = html + '</div>';
    var q = $('#fq', r); q.oninput = function () { friendQ = q.value; renderRight(); }; if (keepFocus) { q.focus(); q.setSelectionRange(q.value.length, q.value.length); }
  }
  function friendRow(f) {
    var on = Soc.isOnline(f.id), act = Soc.activity(f.id), pr = Soc.profile(f.id);
    var line = on ? (act && act !== 'Moquil' ? 'Playing ' + act : 'On Moquil') : 'Offline';
    if (pr && pr.status && pr.status.t && (!pr.status.until || pr.status.until > Date.now())) line = (pr.status.e ? pr.status.e + ' ' : '') + pr.status.t;
    return '<div class="mini ' + esc(pr && pr.nameplate || '') + '" data-user="' + f.id + '">' + avatar(f.id, f.name, pr, 'av s', on) + '<div class="grow"><div class="ell b ' + esc(pr && pr.nametag || '') + '">' + esc(pr && pr.display || f.name) + '</div><div class="tiny dim ell">' + esc(line) + '</div></div></div>';
  }
  function srvInit(n) { var w = String(n || '#').trim().split(/\s+/).filter(Boolean); return esc((w.length > 1 ? w.slice(0, 3).map(function (x) { return x[0]; }).join('') : (w[0] || '#').slice(0, 2)).toUpperCase()); }
  function srvIcon(c, px, extra) {   // Discord-style server icon: the uploaded picture, or the initials on a colour
    px = px || 42; var st = 'width:' + px + 'px;height:' + px + 'px;font-size:' + Math.round(px * .36) + 'px;' + (extra || '');
    return c.icon ? '<div class="guild img" style="' + st + 'background:#222 url(' + c.icon + ') center / cover"></div>' : '<div class="guild" style="' + st + 'background:' + colorOf(c.cid) + '">' + srvInit(c.name) + '</div>';
  }
  function colorOf(id) { var h = 0, s = String(id); for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0; return 'hsl(' + (h % 360) + ',45%,42%)'; }
  function avPic(src, name, id) { return '<div class="pic" style="' + (src ? 'background-image:url(' + src + ')' : 'background:' + colorOf(id || name)) + '">' + (src ? '' : esc((name || '?').slice(0, 1).toUpperCase())) + '</div>'; }
  function presCls(p, on) { if (!on) return ''; return p === 'idle' ? 'idle' : p === 'dnd' ? 'dnd' : p === 'invisible' ? '' : 'on'; }
  function presName(p) { return { online: 'Online', idle: 'Idle', dnd: 'Do Not Disturb', invisible: 'Invisible' }[p] || 'Online'; }
  function statusLine(p) { if (!p || !p.status || (!p.status.t && !p.status.e)) return ''; if (p.status.until && p.status.until < Date.now()) return ''; return (p.status.e ? p.status.e + ' ' : '') + (p.status.t || ''); }
  function avatar(id, name, pr, cls, online) {
    var me = id === Soc.id || id === ''; if (me) pr = G.MoqProfile();
    var on = me ? Soc.id && Soc.up() : online === undefined ? Soc.isOnline(id) : online;
    return '<div class="' + (cls || 'av') + '">' + avPic(pr && pr.avatar, (pr && pr.display) || name, id) + (pr && pr.deco ? '<span class="deco ' + esc(pr.deco) + '"></span>' : '') + '<span class="dot ' + presCls(pr && pr.pres, on) + '"></span></div>';
  }

  var PAGES = {};
  // ===== 4.13.1: Discord desktop layout (also "desktop site" on a phone): rail | DM or channel sidebar | main | Active Now =====
  var FTAB = 'online', FQ = '';
  function sideNav(k, t, i, b, extra, attr) { var on = CUR.page === k && (attr ? CUR.tab === 'quests' : !(k === 'store' && CUR.tab === 'quests')); return '<div class="dnav' + (on ? ' on' : '') + '" data-go="' + k + '"' + (attr || '') + '>' + ic(i) + '<span class="grow ell">' + t + '</span>' + (extra || '') + (b ? '<span class="badge">' + b + '</span>' : '') + '</div>'; }
  function dmSideList() {
    if (!Soc.id) return '';
    var had = {}, out = Soc.convs().map(function (c) {
      var dm = c.kind === 0, id = dm ? c.key.slice(1) : '', pr = dm ? Soc.profile(id) : null; if (dm) had[id] = 1;
      var nm = dm ? (pr && pr.display || Soc.nameOf(id) || c.title) : c.title, on = CUR.page === 'chat' && CUR.arg === c.key;
      var sub = dm ? (statusLine(pr) || (Soc.isOnline(id) && Soc.activity(id) && Soc.activity(id) !== 'Moquil' ? 'Playing ' + Soc.activity(id) : '')) : (c.members ? c.members.length + ' Members' : 'Group');
      return '<div class="dmi' + (on ? ' on' : '') + (c.unread ? ' unread' : '') + '" data-chat="' + esc(c.key) + '">' + (dm ? avatar(id, nm, pr, 'av s') : '<div class="guild" style="width:32px;height:32px;border-radius:50%;font-size:12px;background:' + colorOf(c.key) + '">' + ic(c.kind === 2 ? 'party' : 'users', 's') + '</div>') +
        '<div class="grow"><div class="nm ell ' + esc(pr && pr.nametag || '') + '">' + esc(nm) + '</div>' + (sub ? '<div class="sb ell">' + esc(sub) + '</div>' : '') + '</div>' + (c.unread ? '<span class="badge">' + c.unread + '</span>' : '') + '</div>';
    });
    Soc.S.friends.filter(function (f) { return !had[f.id]; }).forEach(function (f) {
      var pr = Soc.profile(f.id);
      out.push('<div class="dmi" data-user="' + f.id + '">' + avatar(f.id, f.name, pr, 'av s') + '<div class="grow"><div class="nm ell ' + esc(pr && pr.nametag || '') + '">' + esc(pr && pr.display || f.name) + '</div>' + (statusLine(pr) ? '<div class="sb ell">' + esc(statusLine(pr)) + '</div>' : '') + '</div><button class="wave" data-wave="' + f.id + '">\uD83D\uDC4B Wave</button></div>');
    });
    return out.length ? out.join('') : '<div class="tiny dim" style="padding:10px 16px">No direct messages yet.</div>';
  }
  function serverSide(c) {
    var owner = c.ownerId === Soc.id, cid = c.cid, ev = SV().events(cid), mgr = srvManager(cid);
    return '<div class="sbhd srvhd' + (c.banner ? ' hasbn" style="background-image:linear-gradient(180deg,rgba(0,0,0,.55),rgba(0,0,0,0) 60%),url(' + c.banner + ')' : '') + '" data-srvmenu="' + esc(cid) + '"><span class="grow ell b">' + esc(c.name) + '</span>' + ic('chev', 's') + '</div><div class="scroll grow sbody">' +
      '<div class="srvinfo">' + srvIcon(c, 56) + '<div class="tiny dim" style="margin-top:8px">' + esc(c.desc || 'Welcome to ' + c.name) + '</div><div class="row" style="gap:6px;margin-top:8px"><span class="tiny dim">' + SV().members(cid).length + ' Members</span><span class="grow"></span><button class="btn sm go" data-invite="' + esc(cid) + '">' + ic('user', 's') + ' Invite</button></div></div>' +
      '<div class="chi" data-srvev="' + esc(cid) + '">' + ic('cal', 's') + '<span class="grow">' + (ev.length ? ev.length + ' Event' + (ev.length > 1 ? 's' : '') : 'Events') + '</span>' + (ev.some(SV().eventLive) ? '<span class="chip green">LIVE</span>' : '') + '</div>' +
      (SV().cfg(cid).rules ? '<div class="chi" data-srvrules="' + esc(cid) + '">' + ic('shield', 's') + '<span class="grow">Rules</span></div>' : '') +
      chanLists(c, 'chi') +
      (mgr ? '<div class="chcat">' + ic('chev', 's') + ' Server Settings</div><div class="chi" data-srvset="' + esc(cid) + '">' + ic('gear', 's') + '<span class="grow">Settings</span></div>' : '') +
      (owner ? '<div class="chi" data-srvicon="' + esc(cid) + '">' + ic('image', 's') + '<span class="grow">Server Profile</span></div><div class="chi danger" data-delcom="' + esc(cid) + '">' + ic('x', 's') + '<span class="grow">Delete server</span></div>' :
        '<div class="chcat"></div><div class="chi' + (c.joined ? ' danger' : '') + '" data-join="' + esc(cid) + '">' + ic(c.joined ? 'out' : 'plus', 's') + '<span class="grow">' + (c.joined ? 'Leave server' : 'Join server') + '</span></div>') +
      '</div>';
  }
  // ===== 4.13.18: server features, with Discord.Net 3.19 as the blueprint: roles + permissions, channels (text / announcement /
  // voice, topic, slowmode), replies, edits, reactions, polls, pins, scheduled events, AutoMod, timeouts, kicks and bans =====
  function SV() { return G.MoqSrv; }
  var comRe = '';
  function srvManager(cid) { return ['admin', 'manage_server', 'manage_channels', 'manage_roles', 'manage_events', 'ban', 'kick', 'moderate'].some(function (p) { return SV().can(cid, p); }) || SV().isOwner(cid, Soc.id); }
  function chanLists(c, cls) {
    var cid = c.cid, mine = SV().can(cid, 'manage_channels') && SV().isOwner(cid, Soc.id);
    var add = function (k) { return mine ? '<a class="chadd" data-chnew="' + esc(cid) + '" data-kind="' + k + '" title="Create channel">' + ic('plus', 's') + '</a>' : ''; };
    var txt = SV().textChans(cid).map(function (k) {
      var pins = SV().pins(cid, k.id).length;
      return '<div class="' + cls + (comCh === k.id ? ' on' : '') + '" data-ch="' + esc(k.id) + '">' + ic(k.kind === 'news' ? 'bell' : 'hash', 's') + '<span class="grow ell">' + esc(k.name) + '</span>' + (k.slow ? '<span title="Slowmode">' + ic('slow', 's') + '</span>' : '') + (pins ? '<span class="tiny dim">' + ic('pin', 's') + '</span>' : '') + '</div>';
    }).join('');
    var vc = SV().voiceChans(cid).map(function (k) {
      var room = SV().voiceRoom(cid, k.id), on = G.MoqVoice && MoqVoice.state.room === room;
      return '<div class="' + cls + '" data-voice="' + esc(room) + '" data-vt="' + esc(c.name + ' / ' + k.name) + '">' + ic('vol', 's') + '<span class="grow ell">' + esc(k.name) + '</span>' + (on ? '<span class="chip green">on</span>' : '') + '</div>';
    }).join('');
    return '<div class="chcat">' + ic('chev', 's') + '<span class="grow">Text Channels</span>' + add('text') + '</div>' + txt + '<div class="chcat">' + ic('chev', 's') + '<span class="grow">Voice Channels</span>' + add('voice') + '</div>' + vc;
  }
  function memberPanel(cid) {
    var groups = SV().memberGroups(cid), n = SV().members(cid).length;
    return '<div class="tiny dim up" style="margin-bottom:6px">Members - ' + n + '</div>' + groups.map(function (g) {
      return '<div class="tiny dim up" style="margin:10px 0 4px">' + esc(g.role.name) + ' - ' + g.list.length + '</div>' + g.list.map(function (m) {
        var col = SV().colorOf(cid, m.id), own = SV().isOwner(cid, m.id), to = SV().timedOut(cid, m.id);
        return '<div class="mini" data-mem="' + m.id + '" data-cid="' + esc(cid) + '" style="margin:0">' + avatar(m.id, m.name, Soc.profile(m.id), 'av s') + '<div class="grow ell small"' + (col ? ' style="color:' + col + '"' : '') + '>' + esc((Soc.profile(m.id) || {}).display || m.name) + '</div>' + (own ? '<span title="Server owner" style="color:#f0b232">' + ic('crown', 's') + '</span>' : '') + (to ? '<span title="Timed out">' + ic('slow', 's') + '</span>' : '') + '</div>';
      }).join('');
    }).join('');
  }
  function fmtLeft(sec) { return sec >= 3600 ? Math.ceil(sec / 3600) + 'h' : sec >= 60 ? Math.ceil(sec / 60) + 'm' : sec + 's'; }
  function comComposer(c) {
    var cid = c.cid, why = SV().blockReason(cid, comCh), k = SV().chan(cid, comCh);
    if (why) return '<div id="ccomp" class="card pad small dim" style="text-align:center">' + esc(why) + (!(c.joined || c.ownerId === Soc.id) ? ' <button class="btn sm go" data-join="' + esc(cid) + '">Join</button>' : '') + '</div>';
    var re = comRe && SV().post(cid, comRe);
    return '<div id="ccomp">' + (re ? '<div class="replybar small">' + ic('reply', 's') + ' Replying to <b>' + esc(re.fromName) + '</b><span class="grow ell dim"> ' + esc(re.text.slice(0, 80)) + '</span><a data-act="cancelre">' + ic('x', 's') + '</a></div>' : '') +
      (k.slow ? '<div class="tiny dim" id="cslow" style="padding:2px 6px">' + ic('slow', 's') + ' Slowmode is on: one post every ' + fmtLeft(k.slow) + '</div>' : '') +
      '<div class="composer card"><button class="ibtn" data-act="emoji" title="Emoji">' + ic('smile', 's') + '</button><input class="inp grow" id="ci" maxlength="2000" placeholder="Message ' + (k.kind === 'news' ? '' : '#') + esc(k.name) + '" autocomplete="off">' +
      (SV().can(cid, 'polls') ? '<button class="ibtn" data-poll="' + esc(cid) + '" title="Create poll">' + ic('poll', 's') + '</button>' : '') + '<button class="ibtn" data-act="mdhelp" title="Text formatting">' + ic('edit', 's') + '</button><button class="ibtn" id="csend" title="Send">' + ic('send', 's') + '</button></div></div>';
  }
  function comSend(cid, txt, poll) {
    var wait = SV().slowWait(cid, comCh); if (wait) { toast('Slowmode: wait ' + fmtLeft(wait)); return false; }
    var why = SV().blockReason(cid, comCh); if (why) { toast(why); return false; }
    if (SV().automodHit(cid, txt + ' ' + (poll ? poll.q + ' ' + poll.o.join(' ') : '')) && !SV().can(cid, 'manage_messages') && !SV().isOwner(cid, Soc.id)) { toast('AutoMod blocked this message: it has a word the server does not allow.'); return false; }
    if (/@(everyone|here)\b/.test(txt) && !SV().can(cid, 'mention_everyone')) txt = txt.replace(/@(everyone|here)\b/g, '@\u200b$1');
    var opt = { ch: comCh }; if (comRe) opt.re = comRe; if (poll) opt.poll = poll;
    comRe = '';
    Soc.post(cid, txt, opt).then(function (ok) { if (ok) quest('msg'); refreshCom(true); });
    return true;
  }
  function bindComCompose(c) {
    var i = $('#ci'); if (!i) return;
    var send = function () { var v = i.value.trim(); if (!v) return; if (comSend(c.cid, v)) { i.value = ''; } i.focus(); };
    i.onkeydown = function (e) { if (e.key === 'Enter') { e.preventDefault(); send(); } }; $('#csend').onclick = send;
  }
  function refreshCom(force) {
    var c = CUR.page === 'community' && Soc.community(CUR.arg); if (!c) return;
    var mm = $('#mm'); if (mm) { var h = memberPanel(c.cid); if (mm._h !== h) { mm._h = h; mm.innerHTML = h; } }
    var cc = $('#ccomp'), ci = $('#ci');
    if (cc && !(ci && D.activeElement === ci && ci.value)) { var v = ci ? ci.value : '', nh = comComposer(c); if (cc._h !== nh) { var tmp = D.createElement('div'); tmp.innerHTML = nh; var nw = tmp.firstChild; nw._h = nh; cc.replaceWith(nw); bindComCompose(c); if (v && $('#ci')) $('#ci').value = v; } }
    updateMsgs(force);
  }
  function comMsgHtml(cid, p, prev) {
    var me = p.fromId === Soc.id, pr = me ? G.MoqProfile() : Soc.profile(p.fromId), nm = me ? (prof().display || SESSION.user) : (pr && pr.display || p.fromName);
    var same = prev && prev.fromId === p.fromId && p.ts - prev.ts < 300 && !p.re && !p.poll;
    var col = SV().colorOf(cid, p.fromId), pin = SV().pinned(cid, p.id);
    var re = p.re ? SV().post(cid, p.re) : null;
    var quote = p.re ? '<div class="mre small" data-jump="' + esc(p.re) + '">' + ic('reply', 's') + (re ? '<b' + (SV().colorOf(cid, re.fromId) ? ' style="color:' + SV().colorOf(cid, re.fromId) + '"' : '') + '>' + esc(re.fromName) + '</b> <span class="ell dim">' + esc(re.text.slice(0, 90)) + '</span>' : '<span class="dim">Original message was deleted</span>') + '</div>' : '';
    var poll = '';
    if (p.poll) {
      var vmap = SV().votes(cid, p.id), vv = p.poll.o.map(function (_, i) { return (vmap[i] || []).length; }), tot = vv.reduce(function (a, b) { return a + b; }, 0), my = SV().myVotes(cid, p.id), ended = p.poll.end && p.poll.end < Math.floor(Date.now() / 1000);
      poll = '<div class="poll card"><div class="b">' + esc(p.poll.q) + '</div><div class="tiny dim">' + (p.poll.multi ? 'Select one or more answers' : 'Select one answer') + '</div>' + p.poll.o.map(function (o, i) {
        var pc = tot ? Math.round(vv[i] * 100 / tot) : 0;
        return '<div class="popt' + (my.indexOf(i) >= 0 ? ' on' : '') + (ended ? ' ended' : '') + '" data-vote="' + esc(p.id) + '" data-i="' + i + '"><i style="width:' + pc + '%"></i><span class="grow">' + esc(o) + '</span><span class="tiny">' + vv[i] + ' &middot; ' + pc + '%</span></div>';
      }).join('') + '<div class="tiny dim">' + tot + ' vote' + (tot === 1 ? '' : 's') + ' &middot; ' + (ended ? 'Poll closed' : p.poll.end ? 'Ends ' + tsFmt(p.poll.end, 'R') : 'Open') + '</div></div>';
    }
    var rx = SV().reactions(cid, p.id), rk = Object.keys(rx);
    var rbar = rk.length ? '<div class="rxs">' + rk.map(function (e) { var mine = rx[e].indexOf(Soc.id) >= 0; return '<span class="rx' + (mine ? ' on' : '') + '" data-rx="' + esc(p.id) + '" data-e="' + e + '">' + e + ' <b>' + rx[e].length + '</b></span>'; }).join('') + '</div>' : '';
    return '<div class="msg cmsg' + (pin ? ' pinned' : '') + '" id="m-' + esc(p.id) + '"' + (same ? ' style="padding-top:0"' : '') + '>' + (same ? '<div style="width:40px;flex:none"></div>' : '<div data-mem="' + p.fromId + '" data-cid="' + esc(cid) + '">' + avatar(p.fromId, nm, pr, 'av') + '</div>') +
      '<div class="w">' + quote + (same ? '' : '<div><span class="n ' + esc(pr && pr.nametag || '') + '"' + (col ? ' style="color:' + col + '"' : '') + ' data-mem="' + p.fromId + '" data-cid="' + esc(cid) + '">' + esc(nm) + '</span>' + (SV().isOwner(cid, p.fromId) ? ' <span title="Server owner" style="color:#f0b232">' + ic('crown', 's') + '</span>' : '') + '<span class="tm">' + clock(p.ts) + '</span>' + (pin ? '<span class="tm" title="Pinned">' + ic('pin', 's') + '</span>' : '') + '</div>') +
      (p.text ? '<div class="tx">' + md(p.text) + (p.ed ? ' <span class="tiny dim" title="' + esc(tsFmt(p.ed, 'F')) + '">(edited)</span>' : '') + '</div>' : '') + poll + rbar + '</div>' +
      '<div class="mact"><button class="ibtn" data-rxpick="' + esc(p.id) + '" title="Add reaction">' + ic('smile', 's') + '</button><button class="ibtn" data-reply="' + esc(p.id) + '" title="Reply">' + ic('reply', 's') + '</button><button class="ibtn" data-pmore="' + esc(p.id) + '" title="More">' + ic('more', 's') + '</button></div></div>';
  }
  function memberMenu(cid, uid) {
    var me = Soc.id, c = Soc.community(cid); if (!c) return;
    var pr = Soc.profile(uid), nm = (pr && pr.display) || Soc.nameOf(uid) || uid, cf = SV().cfg(cid), mine = cf.assign[uid] || [];
    var canMod = uid !== me && !SV().isOwner(cid, uid) && SV().above(cid, me, uid);
    var roles = cf.roles.filter(function (r) { return r.id !== 'everyone'; });
    var body = '<div class="row" style="margin-bottom:10px">' + avatar(uid, nm, pr, 'av l') + '<div class="grow"><div class="b">' + esc(nm) + '</div><div class="tiny dim">#' + esc(uid) + (SV().isOwner(cid, uid) ? ' &middot; Server owner' : '') + (SV().timedOut(cid, uid) ? ' &middot; Timed out until ' + tsFmt(SV().timedOut(cid, uid), 'f') : '') + '</div></div></div>' +
      '<div class="tiny dim up">Roles</div><div class="rolechips">' + (mine.length ? mine.map(function (rid) { var r = cf.roles.filter(function (x) { return x.id === rid; })[0]; return r ? '<span class="rchip"><i style="background:' + (r.color || '#99aab5') + '"></i>' + esc(r.name) + '</span>' : ''; }).join('') : '<span class="tiny dim">No roles</span>') + '</div>';
    var btn = [{ t: 'Profile', fn: function () { go('user', uid); } }];
    if (SV().isOwner(cid, me) && roles.length) btn.push({ t: 'Roles', fn: function () { setTimeout(function () { pickRoles(cid, uid); }, 30); } });
    if (canMod && SV().can(cid, 'moderate')) btn.push({ t: SV().timedOut(cid, uid) ? 'Remove timeout' : 'Timeout', fn: function () { setTimeout(function () { if (SV().timedOut(cid, uid)) SV().mod(cid, 'timeout', uid, 0).then(function () { toast('Timeout removed'); refreshCom(); }); else timeoutPick(cid, uid, nm); }, 30); } });
    if (canMod && SV().can(cid, 'kick')) btn.push({ t: 'Kick', cls: 'red', fn: function () { setTimeout(function () { dialog({ title: 'Kick ' + nm, body: 'They are removed from the member list. They can come back with a new invite.', buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Kick', cls: 'red', fn: function () { SV().mod(cid, 'kick', uid).then(function (ok) { toast(ok ? nm + ' was kicked' : 'You cannot kick them'); refreshCom(); }); } }] }); }, 30); } });
    if (canMod && SV().can(cid, 'ban')) btn.push({ t: 'Ban', cls: 'red', fn: function () { setTimeout(function () { prompt2('Ban ' + nm, 'Reason (optional, shown in the ban list)', '', function (why) { SV().mod(cid, 'ban', uid, why).then(function (ok) { toast(ok ? nm + ' was banned' : 'You cannot ban them'); refreshCom(); }); }, { ok: 'Ban', max: 120 }); }, 30); } });
    dialog({ title: 'Member', body: body, buttons: btn });
  }
  function timeoutPick(cid, uid, nm) {
    var L = [[60, '60 seconds'], [300, '5 minutes'], [600, '10 minutes'], [3600, '1 hour'], [86400, '1 day'], [604800, '1 week']];
    dialog({ title: 'Timeout ' + nm, body: '<div class="small dim" style="margin-bottom:8px">They cannot post, react or vote until it ends.</div><div class="card list">' + L.map(function (x, i) { return '<div class="li" data-b="' + (i + 1) + '">' + ic('slow') + '<div class="grow b">' + x[1] + '</div></div>'; }).join('') + '</div>',
      buttons: [{ t: 'Cancel', fn: function () {} }].concat(L.map(function (x) { return { t: x[1], cls: 'hide', fn: function () { SV().mod(cid, 'timeout', uid, x[0]).then(function (ok) { toast(ok ? nm + ' is timed out for ' + x[1] : 'You cannot time them out'); refreshCom(); }); } }; })) });
  }
  function pickRoles(cid, uid) {
    var cf = SV().cfg(cid), mine = cf.assign[uid] || [], roles = cf.roles.filter(function (r) { return r.id !== 'everyone'; });
    dialog({ title: 'Roles', body: roles.map(function (r) { return '<label class="row chk"><input type="checkbox" value="' + esc(r.id) + '"' + (mine.indexOf(r.id) >= 0 ? ' checked' : '') + '><i class="rdot" style="background:' + (r.color || '#99aab5') + '"></i><span class="grow">' + esc(r.name) + '</span></label>'; }).join(''),
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Save', cls: 'go', fn: function (bg) { var sel = Array.prototype.map.call(bg.querySelectorAll('input:checked'), function (x) { return x.value; }); SV().setRoles(cid, uid, sel).then(function () { toast('Roles saved'); refreshCom(); }); } }] });
  }
  function postMenu(cid, pid) {
    var p = SV().post(cid, pid); if (!p) return; var mine = p.fromId === Soc.id, B = [];
    B.push({ t: 'Reply', fn: function () { comRe = pid; refreshCom(); setTimeout(function () { var i = $('#ci'); if (i) i.focus(); }, 60); } });
    B.push({ t: 'React', fn: function () { setTimeout(function () { reactPick(cid, pid); }, 30); } });
    B.push({ t: 'Copy text', fn: function () { copy(p.text); } });
    if (SV().can(cid, 'pin')) B.push({ t: SV().pinned(cid, pid) ? 'Unpin' : 'Pin', fn: function () { SV().mod(cid, SV().pinned(cid, pid) ? 'unpin' : 'pin', pid).then(function () { refreshCom(); }); } });
    if (mine && !p.poll) B.push({ t: 'Edit', fn: function () { setTimeout(function () { prompt2('Edit message', 'Message', p.text, function (v) { v = v.trim(); if (!v) return false; Soc.editPost(cid, pid, v).then(function () { refreshCom(); }); }, { area: true, max: 2000 }); }, 30); } });
    if (mine || SV().can(cid, 'manage_messages')) B.push({ t: 'Delete', cls: 'red', fn: function () { setTimeout(function () { dialog({ title: 'Delete message', body: 'Delete this message for everyone?', buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Delete', cls: 'red', fn: function () { if (mine) Soc.deletePost(cid, pid); else SV().mod(cid, 'del', pid); refreshCom(); } }] }); }, 30); } });
    dialog({ title: 'Message', body: '<div class="small dim ell">' + esc(p.fromName) + ': ' + esc(p.text.slice(0, 140) || (p.poll ? 'Poll: ' + p.poll.q : '')) + '</div>', buttons: B });
  }
  function reactPick(cid, pid) {
    dialog({ title: 'Add reaction', body: '<div class="rxpick">' + SV().REACTS.map(function (e, i) { return '<button class="ibtn" data-b="' + (i + 1) + '">' + e + '</button>'; }).join('') + '</div>',
      buttons: [{ t: 'Close', fn: function () {} }].concat(SV().REACTS.map(function (e) { return { t: e, cls: 'hide', fn: function () { if (!SV().react(cid, pid, e)) toast('You cannot react here right now'); refreshCom(); } }; })) });
  }
  function pollCreate(cid) {
    dialog({ title: 'Create a poll', body: '<label class="lbl">Question</label><input class="inp" id="pq" maxlength="300" placeholder="What should we play tonight?"><label class="lbl">Answers</label><div id="pans">' + [1, 2, 3].map(function (i) { return '<input class="inp pa" maxlength="55" placeholder="Answer ' + i + '" style="margin-bottom:6px">'; }).join('') + '</div><button class="btn sm" data-act="pollmore">' + ic('plus', 's') + ' Add answer</button>' +
      '<div class="row" style="margin-top:10px"><div class="grow"><label class="lbl">Duration</label><select class="inp" id="pdur"><option value="3600">1 hour</option><option value="14400">4 hours</option><option value="28800">8 hours</option><option value="86400" selected>24 hours</option><option value="259200">3 days</option><option value="604800">1 week</option><option value="0">No end</option></select></div><label class="row chk grow" style="margin-top:22px"><input type="checkbox" id="pmulti"> Allow more than one answer</label></div>',
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Post', cls: 'go', fn: function (bg) {
        var q = $('#pq', bg).value.trim(), o = Array.prototype.map.call(bg.querySelectorAll('.pa'), function (x) { return x.value.trim(); }).filter(Boolean);
        if (!q || o.length < 2) { toast('A question and two answers'); return false; }
        var d = +$('#pdur', bg).value; comSend(cid, '', { q: q, o: o.slice(0, 10), multi: $('#pmulti', bg).checked, end: d ? Math.floor(Date.now() / 1000) + d : 0 });
      } }] });
  }
  function pinsDialog(cid) {
    var list = SV().pins(cid, comCh);
    dialog({ title: 'Pinned messages', icon: 'pin', body: list.length ? list.map(function (p) { return '<div class="card pad" style="margin-bottom:6px" data-jump="' + esc(p.id) + '"><div class="small"><b>' + esc(p.fromName) + '</b> <span class="tiny dim">' + tsFmt(p.ts, 'f') + '</span></div><div class="small">' + md(p.text || (p.poll ? 'Poll: ' + p.poll.q : '')) + '</div></div>'; }).join('') : '<div class="empty small">This channel has no pinned messages yet.</div>', buttons: [] });
  }
  function eventsDialog(cid) {
    var list = SV().events(cid), mine = SV().can(cid, 'manage_events') && SV().isOwner(cid, Soc.id);
    var body = list.length ? list.map(function (e) {
      var n = SV().rsvps(cid, e.id).length, going = SV().going(cid, e.id), live = SV().eventLive(e);
      return '<div class="card pad evc" style="margin-bottom:8px"><div class="tiny" style="color:' + (live ? '#23a55a' : '#00a8fc') + '">' + ic('cal', 's') + ' ' + (live ? 'LIVE NOW' : esc(tsFmt(e.at, 'F'))) + '</div><div class="b" style="font-size:16px;margin:4px 0">' + esc(e.title) + '</div>' + (e.desc ? '<div class="small dim">' + md(e.desc) + '</div>' : '') +
        '<div class="row" style="margin-top:8px"><span class="tiny dim grow">' + (e.where ? ic('vol', 's') + ' ' + esc(e.where) + ' &middot; ' : '') + n + ' interested</span><button class="btn sm ' + (going ? 'go' : '') + '" data-rsvp="' + esc(e.id) + '" data-cid="' + esc(cid) + '">' + ic(going ? 'check' : 'bell', 's') + (going ? ' Interested' : ' Interested?') + '</button>' + (mine ? '<button class="btn sm red" data-evdel="' + esc(e.id) + '" data-cid="' + esc(cid) + '">' + ic('trash', 's') + '</button>' : '') + '</div></div>';
    }).join('') : '<div class="empty small">No upcoming events. ' + (mine ? 'Create one for your next MP night.' : '') + '</div>';
    dialog({ title: 'Events', icon: 'cal', body: body, buttons: mine ? [{ t: 'Close', fn: function () {} }, { t: 'Create Event', cls: 'go', fn: function () { setTimeout(function () { eventCreate(cid); }, 30); } }] : [] });
  }
  function eventCreate(cid) {
    var d = new Date(Date.now() + 3600e3); d.setMinutes(0, 0, 0); var pad = function (n) { return (n < 10 ? '0' : '') + n; };
    var dv = d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + 'T' + pad(d.getHours()) + ':00';
    dialog({ title: 'Create Event', icon: 'cal', body: '<label class="lbl">Topic</label><input class="inp" id="et" maxlength="100" placeholder="MP Race night"><label class="lbl">Start</label><input class="inp" id="ea" type="datetime-local" value="' + dv + '">' +
      '<div class="row"><div class="grow"><label class="lbl">Length</label><select class="inp" id="ed"><option value="1800">30 minutes</option><option value="3600" selected>1 hour</option><option value="7200">2 hours</option><option value="14400">4 hours</option><option value="86400">1 day</option></select></div><div class="grow"><label class="lbl">Where</label><select class="inp" id="ew">' + SV().voiceChans(cid).map(function (k) { return '<option>' + esc(k.name) + '</option>'; }).join('') + '<option>MP server</option><option>Somewhere else</option></select></div></div>' +
      '<label class="lbl">Description</label><textarea class="inp" id="edesc" maxlength="600"></textarea>',
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Create', cls: 'go', fn: function (bg) {
        var t = $('#et', bg).value.trim(), at = Math.floor(new Date($('#ea', bg).value).getTime() / 1000); if (!t || !at) { toast('A topic and a start time'); return false; }
        var cf = JSON.parse(JSON.stringify(SV().cfg(cid))); cf.events.push({ id: SV().rid(), title: t, desc: $('#edesc', bg).value, at: at, dur: +$('#ed', bg).value, where: $('#ew', bg).value, by: Soc.id });
        SV().saveCfg(cid, cf).then(function (ok) { toast(ok ? 'Event created' : 'Could not save'); soon(); });
      } }] });
  }
  // ---- Server Settings (owner): Overview, Roles, Channels, AutoMod, Bans ----
  var SSTAB = 'overview';
  function srvSettings(cid) {
    var c = Soc.community(cid); if (!c) return;
    if (!SV().isOwner(cid, Soc.id)) { srvModPanel(cid); return; }
    var cf = JSON.parse(JSON.stringify(SV().cfg(cid))), counts = SV().roleCounts(cid);
    var tabsH = function () { return '<div class="tabs">' + [['overview', 'Overview'], ['roles', 'Roles'], ['chans', 'Channels'], ['automod', 'AutoMod'], ['bans', 'Bans']].map(function (x) { return '<div class="tab' + (SSTAB === x[0] ? ' on' : '') + '" data-sst="' + x[0] + '">' + x[1] + '</div>'; }).join('') + '</div>'; };
    var pane = function () {
      if (SSTAB === 'overview') return '<label class="lbl">Server name</label><input class="inp" id="sn" maxlength="40" value="' + esc(c.name) + '"><label class="lbl">Description</label><textarea class="inp" id="sdesc" maxlength="300">' + esc(c.desc || '') + '</textarea><label class="lbl">Rules (members see them in the side bar)</label><textarea class="inp" id="srules" maxlength="1500" style="min-height:110px" placeholder="1. Be kind.&#10;2. No cheats on the MP server.">' + esc(cf.rules) + '</textarea>';
      if (SSTAB === 'roles') return '<div class="small dim" style="margin-bottom:8px">Roles at the top outrank the ones below: a moderator can only kick, ban or time out members whose top role is lower.</div>' + cf.roles.map(function (r, i) {
        return '<div class="card pad role" data-ri="' + i + '" style="margin-bottom:8px"><div class="row"><input type="color" class="rcol" value="' + (r.color || '#99aab5') + '"' + (r.id === 'everyone' ? ' disabled' : '') + '><input class="inp grow rname" maxlength="32" value="' + esc(r.name) + '"' + (r.id === 'everyone' ? ' disabled' : '') + '><span class="tiny dim">' + (counts[r.id] || 0) + ' ' + ic('users', 's') + '</span>' +
          (r.id !== 'everyone' ? '<button class="ibtn" data-rmove="-1" title="Move up">&#9650;</button><button class="ibtn" data-rmove="1" title="Move down">&#9660;</button><button class="ibtn" data-rdel title="Delete role">' + ic('trash', 's') + '</button>' : '') + '</div>' +
          (r.id !== 'everyone' ? '<label class="row chk tiny"><input type="checkbox" class="rhoist"' + (r.hoist ? ' checked' : '') + '> Show members with this role on their own in the member list</label>' : '') +
          '<details><summary class="tiny dim">Permissions (' + (r.perms.indexOf('admin') >= 0 ? 'all' : r.perms.length) + ')</summary>' + SV().PERMS.map(function (p) { return '<label class="row chk tiny" title="' + esc(p[2]) + '"><input type="checkbox" class="rperm" value="' + p[0] + '"' + (r.perms.indexOf(p[0]) >= 0 ? ' checked' : '') + '><span class="grow"><b>' + esc(p[1]) + '</b> <span class="dim">' + esc(p[2]) + '</span></span></label>'; }).join('') + '</details></div>';
      }).join('') + '<button class="btn sm go" data-radd>' + ic('plus', 's') + ' Create Role</button>';
      if (SSTAB === 'chans') return cf.chans.map(function (k, i) {
        return '<div class="card pad chrow" data-ci="' + i + '" style="margin-bottom:8px"><div class="row">' + ic(k.kind === 'voice' ? 'vol' : k.kind === 'news' ? 'bell' : 'hash', 's') + '<input class="inp grow cname" maxlength="24" value="' + esc(k.name) + '">' + (k.id !== 'general' && k.id !== 'lounge' ? '<button class="ibtn" data-cdel title="Delete channel">' + ic('trash', 's') + '</button>' : '') + '</div>' +
          (k.kind !== 'voice' ? '<input class="inp ctopic" maxlength="300" placeholder="Channel topic" value="' + esc(k.topic || '') + '"><div class="row"><label class="tiny dim">Slowmode</label><select class="inp cslow" style="width:auto">' + [[0, 'Off'], [5, '5s'], [10, '10s'], [30, '30s'], [60, '1m'], [300, '5m'], [900, '15m'], [3600, '1h'], [21600, '6h']].map(function (o) { return '<option value="' + o[0] + '"' + (+k.slow === o[0] ? ' selected' : '') + '>' + o[1] + '</option>'; }).join('') + '</select>' + (k.id !== 'general' ? '<label class="row chk tiny"><input type="checkbox" class="cnews"' + (k.kind === 'news' ? ' checked' : '') + '> Announcement channel</label>' : '') + '</div>' : '') + '</div>';
      }).join('') + '<div class="row"><button class="btn sm go" data-cadd="text">' + ic('hash', 's') + ' Text channel</button><button class="btn sm go" data-cadd="voice">' + ic('vol', 's') + ' Voice channel</button></div>';
      if (SSTAB === 'automod') return '<div class="small dim" style="margin-bottom:8px">Posts with these words are blocked before they are sent and hidden for everyone if they get through. Moderators with Manage Messages are not filtered.</div><textarea class="inp" id="samod" style="min-height:140px" placeholder="one word or phrase per line">' + esc(cf.automod.join('\n')) + '</textarea>';
      var bans = SV().bans(cid);
      return bans.length ? bans.map(function (b) { return '<div class="mini">' + avatar(b.id, Soc.nameOf(b.id) || b.id, Soc.profile(b.id), 'av s') + '<div class="grow small"><b>' + esc(Soc.nameOf(b.id) || b.id) + '</b><div class="tiny dim">' + esc(b.why || 'No reason') + ' &middot; ' + tsFmt(b.ts, 'd') + '</div></div><button class="btn sm" data-unban="' + b.id + '">Revoke</button></div>'; }).join('') : '<div class="empty small">Nobody is banned.</div>';
    };
    var collect = function (bg) {
      if (SSTAB === 'overview') { cf.rules = $('#srules', bg).value; cf._n = $('#sn', bg).value; cf._d = $('#sdesc', bg).value; }
      if (SSTAB === 'roles') Array.prototype.forEach.call(bg.querySelectorAll('.role'), function (el) { var r = cf.roles[+el.dataset.ri]; if (!r) return; if (r.id !== 'everyone') { r.name = $('.rname', el).value; r.color = $('.rcol', el).value === '#99aab5' ? '' : $('.rcol', el).value; r.hoist = $('.rhoist', el).checked; } r.perms = Array.prototype.map.call(el.querySelectorAll('.rperm:checked'), function (x) { return x.value; }); });
      if (SSTAB === 'chans') Array.prototype.forEach.call(bg.querySelectorAll('.chrow'), function (el) { var k = cf.chans[+el.dataset.ci]; if (!k) return; k.name = $('.cname', el).value; if (k.kind !== 'voice') { k.topic = $('.ctopic', el).value; k.slow = +$('.cslow', el).value; var nw = $('.cnews', el); if (nw) k.kind = nw.checked ? 'news' : 'text'; } });
      if (SSTAB === 'automod') cf.automod = $('#samod', bg).value.split('\n');
    };
    var dl = dialog({ title: 'Server Settings', icon: 'gear', body: '<div id="sstabs">' + tabsH() + '</div><div id="sspane" class="scroll" style="max-height:56vh">' + pane() + '</div>',
      onOpen: function (bg) {
        bg.addEventListener('click', function (e) {
          var t = e.target, el, redraw = function () { $('#sstabs', bg).innerHTML = tabsH(); $('#sspane', bg).innerHTML = pane(); };
          if ((el = t.closest('[data-sst]'))) { collect(bg); SSTAB = el.dataset.sst; redraw(); return; }
          if (t.closest('[data-radd]')) { collect(bg); if (cf.roles.length >= 30) return; cf.roles.splice(cf.roles.length - 1, 0, { id: SV().rid().slice(0, 8), name: 'new role', color: SV().ROLE_COLORS[cf.roles.length % 10], perms: [], hoist: true }); redraw(); return; }
          if ((el = t.closest('[data-rmove]'))) { collect(bg); var i = +el.closest('.role').dataset.ri, j = i + +el.dataset.rmove; if (j < 0 || j >= cf.roles.length - 1 || cf.roles[j].id === 'everyone') return; var tmp = cf.roles[i]; cf.roles[i] = cf.roles[j]; cf.roles[j] = tmp; redraw(); return; }
          if ((el = t.closest('[data-rdel]'))) { collect(bg); cf.roles.splice(+el.closest('.role').dataset.ri, 1); redraw(); return; }
          if ((el = t.closest('[data-cadd]'))) { collect(bg); if (cf.chans.length >= 40) return; var kind = el.dataset.cadd, nm = kind === 'voice' ? 'Voice ' + (SV().voiceChans(cid).length + 1) : 'new-channel'; cf.chans.push({ id: SV().chanName(nm).slice(0, 15) + '-' + SV().rid().slice(0, 4), name: nm, kind: kind, topic: '', slow: 0 }); redraw(); return; }
          if ((el = t.closest('[data-cdel]'))) { collect(bg); cf.chans.splice(+el.closest('.chrow').dataset.ci, 1); redraw(); return; }
          if ((el = t.closest('[data-unban]'))) { SV().mod(cid, 'unban', el.dataset.unban).then(function () { toast('Ban revoked'); redraw(); }); return; }
        });
      },
      buttons: [{ t: 'Close', fn: function () {} }, { t: 'Save Changes', cls: 'go', fn: function (bg) {
        collect(bg); var n = cf._n, d = cf._d; delete cf._n; delete cf._d;
        SV().saveCfg(cid, cf).then(function (ok) { toast(ok ? 'Server settings saved' : 'Too big to save'); if (n !== undefined && (n !== c.name || d !== (c.desc || ''))) Soc.updateCommunity(cid, n, d); soon(); render(); });
      } }] });
    return dl;
  }
  function srvModPanel(cid) {   // moderators (not the owner): bans list + how-to
    var bans = SV().bans(cid);
    dialog({ title: 'Moderation', icon: 'shield', body: '<div class="small dim" style="margin-bottom:8px">Tap a member in the member list or a name in chat to time out, kick or ban them. Roles, channels and AutoMod are set by the server owner.</div><div class="tiny dim up">Bans</div>' + (bans.length ? bans.map(function (b) { return '<div class="mini"><div class="grow small"><b>' + esc(Soc.nameOf(b.id) || b.id) + '</b> <span class="tiny dim">' + esc(b.why || '') + '</span></div>' + (SV().can(cid, 'ban') ? '<button class="btn sm" onclick="MoqSrv.mod(\'' + esc(cid) + '\',\'unban\',\'' + b.id + '\')">Revoke</button>' : '') + '</div>'; }).join('') : '<div class="empty small">Nobody is banned.</div>'), buttons: [] });
  }
  function renderSide() {
    var s = $('#side'); if (!s || !SESSION) return;
    var c = CUR.page === 'community' && Soc.id && Soc.community(CUR.arg);
    if (c) { s.innerHTML = serverSide(c); return; }
    if (CUR.page === 'discover') {
      s.innerHTML = '<div class="sbhd"><span class="b" style="font-size:18px;padding:0 8px">Discover</span></div><div class="scroll grow sbody">' + sideNav('discover', 'Servers', 'search') + sideNav('home', 'Mods and news', 'game') + sideNav('store', 'Shop', 'store') + sideNav('library', 'Apps', 'library') + '</div>';
      return;
    }
    var inc = Soc.id ? Soc.S.inc.length : 0;
    s.innerHTML = '<div class="sbhd"><button class="findc" data-go="search">Find or start a conversation</button></div><div class="scroll grow sbody">' +
      sideNav('friends', 'Friends', 'users', inc) + sideNav('home', 'Home', 'home') + sideNav('library', 'Library', 'library') + sideNav('store', 'Shop', 'store', 0, '<span class="promo">ORBS</span>') + sideNav('store', 'Quests', 'quest', 0, '', ' data-tabto="quests"') +
      '<div class="dmh"><span class="grow">Direct Messages</span><a data-act="newgroup" title="Create DM">' + ic('plus', 's') + '</a></div>' + dmSideList() + '</div>';
  }
  function activeNow() {
    var act = Soc.id ? Soc.S.friends.filter(function (f) { return Soc.isOnline(f.id); }) : [];
    return '<div class="anow"><div class="b" style="font-size:20px;margin:4px 0 14px">Active Now</div>' + (act.length ? act.map(function (f) {
      var pr = Soc.profile(f.id), a = Soc.activity(f.id), game = a && a !== 'Moquil' ? a : '';
      var m = game ? MOQ_MODS.filter(function (mm) { return game.toLowerCase().indexOf(mm.name.toLowerCase().split(' ')[0]) >= 0; })[0] || MOQ_MODS[0] : null;
      return '<div class="anc" data-user="' + f.id + '"><div class="row">' + avatar(f.id, f.name, pr, 'av s', true) + '<div class="grow"><div class="b ell">' + esc(pr && pr.display || f.name) + '</div><div class="tiny dim ell">' + esc(game ? game : 'On Moquil') + '</div></div>' + (m ? '<div class="gi" style="background-image:url(' + m.icon + ')"></div>' : '') + '</div></div>';
    }).join('') : '<div class="anempty"><div class="b">It\'s quiet for now...</div><div class="small dim">When a friend starts an activity, like playing MP or hanging out in voice, it shows here.</div></div>') + '</div>';
  }
  PAGES.friends = function () {
    if (!Soc.id) return { top: '<div class="b">Friends</div>', body: '<div class="empty">Starting...</div>' };
    var inc = Soc.S.inc.length, t = FTAB;
    var top = '<div class="ftop">' + ic('users') + '<b>Friends</b><i class="dot-sep"></i>' + [['online', 'Online'], ['all', 'All'], ['pending', 'Pending' + (inc ? ' <span class="badge">' + inc + '</span>' : '')]].map(function (x) { return '<div class="ft' + (t === x[0] ? ' on' : '') + '" data-ftab2="' + x[0] + '">' + x[1] + '</div>'; }).join('') +
      '<div class="ft add' + (t === 'add' ? ' on' : '') + '" data-ftab2="add">Add Friend</div><span class="grow"></span><button class="ibtn" data-act="newgroup" title="New Group DM">' + ic('chat') + '</button><button class="ibtn" data-act="inbox" title="Inbox">' + ic('bell') + (Soc.unreadInbox() ? '<span class="badge">' + Soc.unreadInbox() + '</span>' : '') + '</button></div>';
    var body = '';
    if (t === 'add') {
      body = '<div class="addf"><div class="b up" style="font-size:16px">Add Friend</div><div class="small dim" style="margin:6px 0 14px">You can add friends with their Moquil ID. It is the same 8 digit ID as in the game (Social &gt; Friends).</div>' +
        '<form class="addbox" id="addf"><input class="inp" id="addid" maxlength="9" placeholder="You can add friends with their 8 digit ID" inputmode="numeric"><button class="btn go" type="submit">Send Friend Request</button></form><div class="tiny dim" style="margin-top:10px">Your ID is <b>' + Soc.id + '</b> <a href="#" data-copy="' + Soc.id + '">Copy</a></div></div>' +
        '<div class="sec">Other places to make friends</div><div class="card list"><div class="li" data-act="discover">' + ic('search') + '<div class="grow"><div class="b">Explore Discoverable Servers</div></div>' + ic('chev', 's') + '</div></div>';
      return { top: top, body: body, after: function () { $('#addf').onsubmit = function (e) { e.preventDefault(); var v = $('#addid').value.replace(/\D/g, ''); if (v.length !== 8) { toast('The ID has 8 digits'); return; } if (v === Soc.id) { toast('That is you'); return; } Soc.watchProfile(v); if (Soc.addFriend(v)) { quest('friend'); toast('Friend request sent to #' + v); $('#addid').value = ''; } }; } };
    }
    var list;
    if (t === 'pending') {
      list = Soc.S.inc.map(function (f) { return { f: f, kind: 'in' }; }).concat(Soc.S.out.map(function (f) { return { f: f, kind: 'out' }; }));
      body = '<div class="fsec">Pending &mdash; ' + list.length + '</div>' + (list.length ? list.map(function (x) { var f = x.f;
        return '<div class="frow">' + avatar(f.id, f.name, Soc.profile(f.id), 'av s') + '<div class="grow"><div class="b ell">' + esc(f.name) + ' <span class="tiny dim">#' + f.id + '</span></div><div class="tiny dim">' + (x.kind === 'in' ? 'Incoming Friend Request' : 'Outgoing Friend Request') + '</div></div>' +
          (x.kind === 'in' ? '<button class="rb ok" data-acc="' + f.id + '" title="Accept">' + ic('check', 's') + '</button><button class="rb no" data-dec="' + f.id + '" title="Ignore">' + ic('x', 's') + '</button>' : '<button class="rb no" data-can="' + f.id + '" title="Cancel">' + ic('x', 's') + '</button>') + '</div>'; }).join('') : '<div class="fempty">There are no pending friend requests. Here\'s a Wumpus-free zone for now.</div>');
    } else {
      var fs = Soc.S.friends.filter(function (f) { return t === 'all' || Soc.isOnline(f.id); }).filter(function (f) { return !FQ || f.name.toLowerCase().indexOf(FQ.toLowerCase()) >= 0 || f.id.indexOf(FQ) === 0; });
      fs.sort(function (a, b) { return (Soc.isOnline(b.id) ? 1 : 0) - (Soc.isOnline(a.id) ? 1 : 0) || a.name.localeCompare(b.name); });
      body = '<div class="fsearch"><input class="inp" id="fq2" placeholder="Search" value="' + esc(FQ) + '">' + ic('search', 's') + '</div><div class="fsec">' + (t === 'all' ? 'All friends' : 'Online') + ' &mdash; ' + fs.length + '</div>' +
        (fs.length ? fs.map(function (f) { var pr = Soc.profile(f.id), on = Soc.isOnline(f.id), a = Soc.activity(f.id), w = (Soc.wallet && 0);
          var line = statusLine(pr) || (on ? (a && a !== 'Moquil' ? 'Playing ' + a : 'Online') : 'Offline');
          return '<div class="frow" data-user="' + f.id + '">' + avatar(f.id, f.name, pr, 'av s') + '<div class="grow"><div class="row" style="gap:6px"><span class="b ell ' + esc(pr && pr.nametag || '') + '">' + esc(pr && pr.display || f.name) + '</span><span class="tiny dim fid">#' + f.id + '</span></div><div class="tiny dim ell">' + esc(line) + '</div></div>' +
            '<button class="rb" data-dm="' + f.id + '" title="Message">' + ic('chat', 's') + '</button><button class="rb" data-user="' + f.id + '" title="More">' + ic('more', 's') + '</button></div>'; }).join('') :
          '<div class="fempty">' + (t === 'all' ? 'No friends yet. Tap Add Friend and type a friend\'s 8 digit ID.' : 'No one\'s around to play with right now.') + '</div>');
    }
    return { top: top, body: body, after: function () { var q = $('#fq2'); if (q) { q.oninput = function () { FQ = q.value; var p = q.selectionStart; render(); var q2 = $('#fq2'); if (q2) { q2.focus(); q2.setSelectionRange(p, p); } }; } } };
  };
  // 4.13.2: Open source communities living on Discord (github.com/ThirtyFoxMCx00E1/discord-open-source)
  var OSS_ALL = false;
  function ossSection(q) {
    var L = (G.MOQ_OSS || []).filter(function (o) { return !q || (o.t + ' ' + o.q).toLowerCase().indexOf(q) >= 0; });
    if (!L.length) return '';
    var shown = OSS_ALL || q ? L : L.slice(0, 12);
    return '<div class="sec">Open source communities<span class="count">' + L.length + '</span><span class="grow"></span>' + (!q && L.length > 12 ? '<button class="btn sm" data-act="ossall">' + (OSS_ALL ? 'Show less' : 'Show all') + '</button>' : '') + '</div>' +
      '<div class="oss">' + shown.map(function (o) { return '<div class="o"><div class="row" style="gap:10px"><div class="lg" style="background-image:url(img/oss/' + encodeURI(o.l) + ')"></div><div class="grow"><div class="b ell">' + esc(o.t) + '</div><div class="tiny dim ell">discord.gg/' + esc(o.i) + '</div></div></div><div class="q">' + esc(o.q) + '</div>' +
        '<div class="row" style="gap:6px"><button class="btn sm go" data-open="https://discord.gg/' + esc(o.i) + '">' + ic('chat', 's') + ' Join on Discord</button>' + (o.g ? '<button class="btn sm" data-open="' + esc(o.g) + '">GitHub</button>' : '') + '</div></div>'; }).join('') + '</div>' +
      '<div class="tiny dim" style="margin:8px 2px 0">From the open source list on Discord. Opens the Discord invite in your browser or the Discord app.</div>';
  }
  // Discover (servers) - after the "Find your community" explore design
  PAGES.discover = function () {
    var all = Soc.id ? Soc.communities() : [], q = (CUR.arg || '').toLowerCase();
    var list = all.filter(function (c) { return !q || (c.name + ' ' + (c.desc || '')).toLowerCase().indexOf(q) >= 0; }).sort(function (a, b) { return (b.members || 0) - (a.members || 0) || b.ts - a.ts; });
    var card = function (c) { return '<div class="dsc" data-com="' + esc(c.cid) + '"><div class="ban" style="background:' + (c.banner ? '#111 url(' + c.banner + ') center / cover' : c.icon ? '#222 url(' + c.icon + ') center / cover' : 'linear-gradient(135deg,' + colorOf(c.cid) + ',' + colorOf(c.cid + 'x') + ')') + '"></div><div class="ico">' + srvIcon(c, 44, 'border-radius:14px;border:4px solid var(--d-card);') + '</div><div class="bd"><div class="b ell">' + esc(c.name) + '</div><div class="small dim cl2">' + esc(c.desc || 'A server by ' + (c.ownerName || 'an MP player')) + '</div><div class="tiny dim row" style="gap:12px;margin-top:8px"><span><i class="gdot"></i>' + (c.followers || 0) + ' Following</span><span><i class="gdot g"></i>' + (c.members || 0) + ' Members</span></div></div></div>'; };
    var body = '<div class="dhero"><div class="tx"><h1>Find your server on Moquil</h1><p>From MP crews to mod makers, there is a place for you.</p><form id="dsf"><input class="inp" id="dsq" placeholder="Explore servers" value="' + esc(CUR.arg || '') + '"></form></div></div>' +
      '<div class="sec">' + (q ? 'Results' : 'Featured servers') + '<span class="count">' + list.length + '</span><span class="grow"></span><button class="btn sm go" data-act="newcom">' + ic('plus', 's') + ' Create a server</button></div>' +
      (list.length ? '<div class="dgrid">' + list.slice(0, 48).map(card).join('') + '</div>' : '<div class="fempty">No servers yet. Make the first one.</div>') +
      ossSection(q) +
      '<div class="sec">Popular mods</div><div class="dgrid">' + MOQ_MODS.slice(0, 6).map(function (m) { return '<div class="dsc" data-mod="' + m.id + '"><div class="ban" style="background:#000 url(' + m.cover + ') center / cover"></div><div class="ico"><div class="guild img" style="width:44px;height:44px;border-radius:14px;border:4px solid var(--d-card);background:#000 url(' + m.icon + ') center / cover"></div></div><div class="bd"><div class="b ell">' + esc(m.name) + '</div><div class="small dim cl2">' + esc(m.short || m.desc || '') + '</div></div></div>'; }).join('') + '</div>';
    return { top: '<div class="ftop">' + ic('search') + '<b>Discover</b><span class="grow"></span></div>', body: body, after: function () { $('#dsf').onsubmit = function (e) { e.preventDefault(); go('discover', $('#dsq').value.trim()); }; } };
  };
  function renderRightD() {
    var r = $('#right'); if (!r) return;
    var show = CUR.page === 'friends' && !isPortrait();
    r.style.display = show ? '' : 'none'; if (show) r.innerHTML = activeNow();
  }
  // ---------- router ----------
  var TABS = {};
  function go(page, arg, tab) { if (tab) TABS[page] = tab; else if (page === 'user') TABS.user = 'main'; var h = page + (arg ? '/' + encodeURIComponent(arg) : ''); if (location.hash.replace(/^#/, '') === h) route(); else location.hash = h; }
  var DEF_TAB = { home: 'general', social: 'chats', library: 'all', store: 'shop', profile: 'main', search: '' };
  function route() {
    var h = (location.hash || '').replace(/^#/, ''), p = h.split('/');
    CUR.page = p[0] || (isPortrait() ? 'dms' : 'friends'); CUR.arg = p[1] ? decodeURIComponent(p[1]) : ''; CUR.tab = TABS[CUR.page] || DEF_TAB[CUR.page] || (CUR.page === 'user' ? 'main' : '');
    CUR.sub = CUR.page === 'settings' ? CUR.arg : '';
    render();
  }
  G.addEventListener('hashchange', function () { if (SESSION) route(); });
  G.MOQ_back = function () {   // Android back button: true = handled
    var s = $('.dlg-bg') || $('.sheet-bg'); if (s) { s.remove(); return true; }
    if (CUR.page === 'community' && comView === 'chat' && isPortrait()) { comView = 'list'; render(); return true; }
    if (CUR.page !== 'home' && CUR.page !== 'dms' && CUR.page !== 'friends') { history.back(); return true; }
    return false;
  };

  function render() {
    if (!SESSION) return; soonShell();
    var top = $('#top'), pg = $('#page'); if (!pg) return;
    var R = PAGES[CUR.page] || PAGES.home;
    D.documentElement.classList.toggle('inchat', /^(chat|community)$/.test(CUR.page));
    var r = R(); top.innerHTML = r.top || ''; pg.innerHTML = r.body || '';
    pg.className = 'page scroll' + (r.full ? ' full' : '');
    pg.style.display = r.full ? 'flex' : ''; pg.style.flexDirection = r.full ? 'column' : '';
    r.after && r.after(pg);
  }
  function refresh() {   // re-render without killing what you type
    var a = D.activeElement; if (a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA') && $('#page').contains(a) && a.id !== 'sq') { if (CUR.page === 'chat') updateMsgs(); else if (CUR.page === 'community') refreshCom(); return; }
    if (CUR.page === 'chat') { updateMsgs(); return; }
    if (CUR.page === 'community') { refreshCom(); return; }
    var sc = $('#page') ? $('#page').scrollTop : 0; render(); if ($('#page')) $('#page').scrollTop = sc;
  }
  function tabs(list, cur, attr) { return '<div class="tabs">' + list.map(function (t) { return '<div class="tab' + (cur === t[0] ? ' on' : '') + '" data-' + (attr || 'tab') + '="' + t[0] + '">' + t[1] + '</div>'; }).join('') + '</div>'; }
  function topRight() { var n = Soc.id ? Soc.unreadInbox() : 0; return '<span class="grow"></span><button class="ibtn" data-go="search" title="Search">' + ic('search') + '</button><button class="ibtn" data-act="inbox" title="Inbox">' + ic('bell') + (n ? '<span class="badge">' + n + '</span>' : '') + '</button>'; }
  function title(t, back) { return (back ? '<button class="ibtn" data-act="back">' + ic('back') + '</button>' : '') + '<div class="b up" style="font-size:16px;letter-spacing:.06em">' + t + '</div>'; }

  // ===== 4.13.1: MESSAGES (Discord mobile: actions, friends' activity cards, DM list with Wave) =====
  PAGES.dms = function () {
    if (!Soc.id) return { top: '<div class="b" style="font-size:22px">Messages</div>', body: '<div class="empty">Starting...</div>' };
    var inb = Soc.unreadInbox() + Soc.S.inc.length;
    var body = '<div class="dm-acts"><button class="ibtn sqb" data-go="search" title="Search">' + ic('search') + '</button><button class="ibtn sqb" data-act="inbox" title="Inbox">' + ic('bell') + (inb ? '<span class="cnt">' + inb + '</span>' : '') + '</button><button class="btn sqb grow" data-act="addfriend">' + ic('user', 's') + ' Add Friends</button><button class="btn sqb go blue" data-act="newgroup" title="New group">' + ic('plus') + '</button></div>';
    var act = Soc.S.friends.filter(function (f) { return Soc.isOnline(f.id); }).map(function (f) { return { f: f, a: Soc.activity(f.id) }; });
    act.sort(function (x, y) { return (y.a && y.a !== 'Moquil' ? 1 : 0) - (x.a && x.a !== 'Moquil' ? 1 : 0); });
    if (act.length) body += '<div class="acts hscroll">' + act.slice(0, 12).map(function (x) {
      var pr = Soc.profile(x.f.id), game = x.a && x.a !== 'Moquil' ? x.a : 'Moquil';
      var m = MOQ_MODS.filter(function (mm) { return game.toLowerCase().indexOf(mm.name.toLowerCase().split(' ')[0]) >= 0; })[0] || (game === 'Moquil' ? null : MOQ_MODS[0]);
      return '<div class="actc" data-user="' + x.f.id + '"><div class="gi" style="background-image:url(' + (m ? m.icon : 'img/logo.png') + ')"></div><div class="grow"><div class="row" style="gap:6px">' + avatar(x.f.id, x.f.name, pr, 'av s', true) + '</div><div class="b ell" style="margin-top:6px">' + esc(pr && pr.display || x.f.name) + '</div><div class="small dim ell">' + esc(game === 'Moquil' ? 'On Moquil' : game) + '</div></div></div>';
    }).join('') + '</div>';
    var cs = Soc.convs(), had = {};
    var rows = cs.map(function (c) {
      var last = (c.msgs || [])[c.msgs.length - 1], dm = c.kind === 0, id = dm ? c.key.slice(1) : '', pr = dm ? Soc.profile(id) : null;
      if (dm) had[id] = 1;
      var nm = dm ? (pr && pr.display || Soc.nameOf(id) || c.title) : c.title;
      return '<div class="dmr' + (c.unread ? ' unread' : '') + '" data-chat="' + esc(c.key) + '">' + (dm ? avatar(id, nm, pr, 'av l') : '<div class="guild" style="width:48px;height:48px;border-radius:50%;background:' + colorOf(c.key) + '">' + ic(c.kind === 2 ? 'party' : 'users') + '</div>') +
        '<div class="grow"><div class="row" style="gap:6px"><span class="nm ell ' + esc(pr && pr.nametag || '') + '">' + esc(nm) + '</span><span class="grow"></span><span class="tiny dim">' + (last ? ago(last.ts) : '') + '</span></div><div class="ln ell">' + (last ? esc((last.mine ? 'You: ' : last.fromName ? last.fromName + ': ' : '') + last.text) : 'No messages yet') + '</div></div>' + (c.unread ? '<span class="badge">' + c.unread + '</span>' : '') + '</div>';
    });
    Soc.S.friends.filter(function (f) { return !had[f.id]; }).forEach(function (f) {
      var pr = Soc.profile(f.id);
      rows.push('<div class="dmr" data-user="' + f.id + '">' + avatar(f.id, f.name, pr, 'av l') + '<div class="grow"><span class="nm ell ' + esc(pr && pr.nametag || '') + '">' + esc(pr && pr.display || f.name) + '</span>' + (statusLine(pr) ? '<div class="ln ell">' + esc(statusLine(pr)) + '</div>' : '') + '</div><button class="btn wave" data-wave="' + f.id + '">' + ic('smile', 's') + ' Wave</button></div>');
    });
    body += rows.length ? '<div class="dml">' + rows.join('') + '</div>' : '<div class="empty">' + ic('chat', 'l') + '<p>No messages yet. Tap Add Friends and type a friend\'s 8 digit ID.<br>Your ID: <b>' + Soc.id + '</b></p></div>';
    return { top: '<div class="b" style="font-size:22px">Messages</div>', body: body };
  };
  // ===== HOME (General / News / Discover) =====
  PAGES.home = function () {
    var t = CUR.tab || 'general', body = '';
    if (t === 'general') {
      var mp = MOQ_MODS[0], rel = G.MOQ_REL;
      body = '<div class="hero"><svg class="corner" viewBox="0 0 30 30" fill="none" stroke="#fff" stroke-width="2"><path d="M2 12V2h10M28 18v10H18"/></svg><div class="dots"><span class="chip" style="background:rgba(0,0,0,.4);color:#fff">' + (rel ? esc(rel.tag_name) : 'MP') + '</span></div><div class="in"><div class="logo">MP MULTIPLAYER</div><span class="rec">Recommended for you</span><p>Play GTA San Andreas online with your friends</p>' +
        '<div class="row wrap"><button class="btn go" data-dl="mp">' + ic('dl', 's') + ' Install</button><span class="b" style="font-size:18px">Free</span><button class="btn ghost sm" data-info="mp" style="color:#fff">' + ic('info', 's') + ' More info</button></div></div></div>';
      body += '<div class="sec">Resume my games</div><div class="hscroll">' + MOQ_MODS.map(tile).join('') + '</div>';
      var online = Soc.id ? Soc.S.friends.filter(function (f) { return Soc.isOnline(f.id); }) : [];
      body += '<div class="sec">Friends online<span class="count">' + online.length + '</span></div>' + (online.length ? '<div class="card list">' + online.slice(0, 8).map(function (f) { return '<div class="li" data-user="' + f.id + '">' + avatar(f.id, f.name, Soc.profile(f.id), 'av') + '<div class="grow"><div class="b">' + esc(f.name) + '</div><div class="small dim">' + esc(Soc.activity(f.id) === 'Moquil' ? 'On Moquil' : 'Playing ' + Soc.activity(f.id)) + '</div></div><button class="btn sm" data-dm="' + f.id + '">' + ic('chat', 's') + ' Chat</button></div>'; }).join('') + '</div>' : '<div class="card pad dim small">No friends online right now. <a href="#" data-act="addfriend">Add a friend</a> or share your ID <b>' + (Soc.id || '') + '</b>.</div>');
      var w = wallet(), qd = w.q.day === today() ? w.q : { done: {}, claimed: {} }, left = QUESTS.filter(function (q) { return !qd.claimed[q[0]]; }).length;
      body += '<div class="sec">Quests</div><div class="card quest" data-go="store" data-tabto="quests">' + ic('quest', 'l') + '<div class="grow"><div class="b">' + left + ' quests left today</div><div class="small dim">Earn Orbs and spend them in the Store on decorations, effects, frames and nameplates.</div></div><span class="orbs">' + ic('orb', 's') + w.orbs + '</span></div>';
    } else if (t === 'news') {
      body = NEWS.map(function (n) { return '<div class="card pad" style="margin-bottom:12px"><div class="row"><span class="chip">' + esc(n.tag) + '</span><span class="tiny dim">' + esc(n.date) + '</span></div><div class="b" style="font-size:17px;margin:8px 0 4px">' + esc(n.title) + '</div><div class="small" style="opacity:.9">' + esc(n.text) + '</div>' + (n.mod ? '<button class="btn sm" style="margin-top:10px" data-info="' + n.mod + '">' + ic('info', 's') + ' See it</button>' : '') + '</div>'; }).join('');
    } else {
      var coms = Soc.id ? Soc.communities() : [];
      body = '<div class="sec">Mods and apps</div><div class="tiles">' + MOQ_MODS.map(tile).join('') + '</div>' +
        '<div class="sec">Servers<span class="count">' + coms.length + '</span><span class="grow"></span><button class="btn sm" data-act="newcom">' + ic('plus', 's') + ' New</button></div>' +
        (coms.length ? '<div class="card list">' + coms.slice(0, 30).map(comRow).join('') + '</div>' : '<div class="card pad dim small">Servers made in the game or in Moquil show here.</div>');
    }
    return { top: tabs([['general', 'General'], ['news', 'News'], ['discover', 'Discover']], t) + topRight(), body: body };
  };
  var NEWS = [
    { tag: 'Moquil', date: '2026-11-11', title: 'Moquil 1.0 is here', text: 'Friends, chats, servers, voice rooms, a Store and a Library for the MP mods. The same account and friends as the game, on the phone and on the web.' },
    { tag: 'MP 4.13', date: '2026-11-11', title: 'The game Social page is linked with Moquil', text: 'Social > Moquil in MP Launcher shows a link code. Type it in Moquil > Settings > Connections and both become the same player: friends, chats and servers on both. Friends see your bio, pronouns, status and member since date in the game.', mod: 'mp' },
    { tag: 'MP 4.12.10', date: '2026-11-11', title: 'Mission sync in cutscenes', text: 'Everyone in a synced mission now shows in the cutscenes and on the map, up to 16 players.', mod: 'mp' },
    { tag: 'MLG Production', date: '1.6.2', title: 'Record while driving', text: 'The folded timeline bar has Record / Stop, and getting in a car right after Record starts the take again for the car.', mod: 'mlg' },
    { tag: 'CamFX', date: '2.1', title: 'The 64-bit fix and the SACamFX options', text: 'Fall zoom only when sky diving, drunk effect when falling, and the arm64 script output fix.', mod: 'camfx' }
  ];
  function tile(m) {
    var r = Soc.id ? Soc.rating(m.id) : { avg: 0, n: 0 };
    return '<div class="tile" data-info="' + m.id + '"><div class="img" style="background-image:url(' + m.cover + ')"><span class="k"></span><span class="info-b" data-info="' + m.id + '" title="More information">!</span></div><div class="t ell">' + esc(m.name) + '</div><div class="s row">' + stars(r.avg) + '<span>' + (r.n ? r.avg.toFixed(1) + ' (' + r.n + ')' : 'No ratings') + '</span></div></div>';
  }
  function comRow(c) { return '<div class="li" data-com="' + esc(c.cid) + '">' + srvIcon(c, 42) + '<div class="grow"><div class="b ell">' + esc(c.name) + '</div><div class="small dim ell">' + esc(c.desc || 'By ' + (c.ownerName || '')) + '</div></div><span class="tiny dim">' + (c.members || 0) + ' members</span></div>'; }

  // ===== SEARCH =====
  var SQ = '';
  PAGES.search = function () {
    var q = SQ.trim().toLowerCase(), body = '';
    if (q) {
      var mods = MOQ_MODS.filter(function (m) { return (m.name + ' ' + m.short + ' ' + m.kind).toLowerCase().indexOf(q) >= 0; });
      var coms = Soc.id ? Soc.communities().filter(function (c) { return (c.name + ' ' + (c.desc || '')).toLowerCase().indexOf(q) >= 0; }) : [];
      var ppl = Soc.id ? Soc.results() : [];
      body += '<div class="sec">Mods and apps<span class="count">' + mods.length + '</span></div>' + (mods.length ? '<div class="tiles">' + mods.map(tile).join('') + '</div>' : '<div class="dim small">Nothing</div>');
      body += '<div class="sec">People<span class="count">' + ppl.length + '</span>' + (Soc.searching() ? '<span class="tiny">searching...</span>' : '') + '</div>' + (ppl.length ? '<div class="card list">' + ppl.map(function (p) {
        var fr = Soc.isFriend(p.id), out = Soc.isOut(p.id);
        return '<div class="li" data-user="' + p.id + '">' + avatar(p.id, p.name, Soc.profile(p.id), 'av') + '<div class="grow"><div class="b">' + esc(p.name) + '</div><div class="small dim">#' + p.id + (Soc.isOnline(p.id) ? ' &middot; online' : '') + '</div></div>' + (fr ? '<span class="chip green">Friend</span>' : out ? '<span class="chip">Sent</span>' : '<button class="btn sm go" data-add="' + p.id + '">' + ic('plus', 's') + ' Add</button>') + '</div>';
      }).join('') + '</div>' : '<div class="dim small">Type a name (3+ letters) or an 8 digit ID.</div>');
      body += '<div class="sec">Servers<span class="count">' + coms.length + '</span></div>' + (coms.length ? '<div class="card list">' + coms.map(comRow).join('') + '</div>' : '<div class="dim small">Nothing</div>');
    } else body = '<div class="empty">' + ic('search', 'l') + '<p>Search mods, people (name or #ID) and servers.</p></div>';
    return { top: '<div class="row grow"><span style="position:relative;flex:1"><input class="inp" id="sq" placeholder="Search Moquil" value="' + esc(SQ) + '"></span></div>',
      body: body, after: function () { var i = $('#sq'); i.oninput = function () { SQ = i.value; if (Soc.id) Soc.search(SQ); clearTimeout(i._t); i._t = setTimeout(function () { var p = i.selectionStart; render(); var j = $('#sq'); j.focus(); j.setSelectionRange(p, p); }, 250); }; if (!SQ) i.focus(); } };
  };

  // ===== STORE (shop + quests) =====
  PAGES.store = function () {
    var t = CUR.tab === 'quests' ? 'quests' : 'shop', w = wallet(), p = prof(), body = '';
    body += '<div class="card balance"><div><div class="tiny dim up">Your balance</div><div class="orbs" style="margin-top:4px;font-size:18px">' + ic('orb') + w.orbs + ' Orbs</div></div><button class="btn sm" data-tab="' + (t === 'shop' ? 'quests' : 'shop') + '">' + ic(t === 'shop' ? 'quest' : 'store', 's') + (t === 'shop' ? ' Earn Orbs' : ' Shop') + '</button></div>';
    if (t === 'quests') {
      if (w.q.day !== today()) { w.q = { day: today(), done: { open: 1 }, claimed: {} }; saveWallet(w); }
      body += '<div class="sec">Daily quests &middot; new ones every day</div><div class="card list">' + QUESTS.map(function (q) {
        var done = !!w.q.done[q[0]], cl = !!w.q.claimed[q[0]];
        return '<div class="li">' + ic(done ? 'check' : 'quest') + '<div class="grow"><div class="b">' + q[1] + '</div><div class="bar" style="margin-top:6px"><i style="width:' + (done ? 100 : 0) + '%"></i></div></div><span class="orbs">' + ic('orb', 's') + q[2] + '</span>' + (cl ? '<span class="chip green">Claimed</span>' : '<button class="btn sm ' + (done ? 'go' : '') + '" ' + (done ? 'data-claim="' + q[0] + '"' : 'disabled') + '>Claim</button>') + '</div>';
      }).join('') + '</div>';
      var once = [['link', 'Link Moquil with the MP game', 300, !!w.once.linkDone], ['avatar', 'Set a profile picture', 100, !!p.avatar], ['bio', 'Write your About me', 100, !!p.bio]];
      body += '<div class="sec">One-time quests</div><div class="card list">' + once.map(function (q) { var cl = !!w.once[q[0]]; return '<div class="li">' + ic(q[3] ? 'check' : 'sparkle') + '<div class="grow b">' + q[1] + '</div><span class="orbs">' + ic('orb', 's') + q[2] + '</span>' + (cl ? '<span class="chip green">Claimed</span>' : '<button class="btn sm ' + (q[3] ? 'go' : '') + '" ' + (q[3] ? 'data-claimonce="' + q[0] + '" data-o="' + q[2] + '"' : 'disabled') + '>Claim</button>') + '</div>'; }).join('') + '</div>';
    } else {
      STORE.forEach(function (cat) {
        body += '<div class="sec">' + cat.title + '</div><div class="store-grid">' + cat.items.map(function (it) {
          var own = w.owned.indexOf(it[0]) >= 0, on = p[cat.cat] === it[0];
          return '<div class="card sitem" data-buy="' + it[0] + '">' + (on ? '<span class="chip green own">On</span>' : own ? '<span class="chip own">Owned</span>' : '') + '<div class="prev">' + preview(cat.cat, it[0]) + '</div><div class="b small">' + it[1] + '</div><div class="tiny dim row" style="justify-content:center;margin-top:4px">' + (own ? (on ? 'Tap to take off' : 'Tap to use') : ic('orb', 's') + it[2]) + '</div></div>';
        }).join('') + '</div>';
      });
    }
    return { top: tabs([['shop', 'Shop'], ['quests', 'Quests']], t) + topRight(), body: body };
  };
  function preview(cat, id) {
    var p = prof();
    if (cat === 'deco') return '<div class="av l" style="width:64px;height:64px">' + avPic(p.avatar, SESSION.user) + '<span class="deco ' + id + '"></span></div>';
    if (cat === 'effect') return '<div style="position:absolute;inset:0;background:linear-gradient(135deg,#2a3170,#5b2a86)"><div class="fx ' + id + '"></div></div>';
    if (cat === 'frame') return '<div class="' + id + '" style="width:70%;height:70px;background:rgba(255,255,255,.06)"></div>';
    if (cat === 'nameplate') return '<div class="' + id + '" style="width:90%;padding:8px;text-align:left;font-weight:700">' + esc(p.display || SESSION.user) + '</div>';
    return '<div class="' + id + '" style="font-size:20px;font-weight:900">' + esc(p.display || SESSION.user) + '</div>';
  }

  // ===== LIBRARY =====
  PAGES.library = function () {
    var dl = ls('moq_dl') || {};
    var body = '<div class="tiles">' + MOQ_MODS.map(function (m) {
      var r = Soc.id ? Soc.rating(m.id) : { avg: 0, n: 0 };
      return '<div class="tile card" style="padding:8px"><div class="img" data-info="' + m.id + '" style="background-image:url(' + m.cover + ')"><span class="k"></span><span class="info-b" data-info="' + m.id + '">!</span></div>' +
        '<div class="t ell" style="margin-top:8px">' + esc(m.name) + ' <span class="tiny dim">' + esc(m.ver) + '</span></div><div class="s ell">' + esc(m.short) + '</div><div class="s row" style="margin:6px 0">' + stars(r.avg) + '<span>' + (r.n ? r.avg.toFixed(1) : '-') + '</span><span class="grow"></span><span>' + esc(m.size) + '</span></div>' +
        '<button class="btn go wide sm" data-dl="' + m.id + '">' + ic('dl', 's') + ' ' + (dl[m.id] ? 'Download again' : m.action) + '</button></div>';
    }).join('') + '</div><div class="card pad small dim" style="margin-top:14px">Mods come as zip files with the 32-bit and 64-bit .so files and a README. Copy the one for your game into the AML mods folder. Downloads go to Download/Moquil' + (IS_WEB ? ' (your browser downloads on the web)' : '') + '.</div>';
    return { top: title('Library') + topRight(), body: body };
  };
  // the (!) sheet: almost full screen, gallery, icon, description, rating, download, by / powered by / sponsor
  function modSheet(id) {
    var m = MOQ_MODS.filter(function (x) { return x.id === id; })[0]; if (!m) return;
    var r = Soc.id ? Soc.rating(m.id) : { avg: 0, n: 0, mine: 0 };
    var bg = D.createElement('div'); bg.className = 'sheet-bg';
    bg.innerHTML = '<div class="sheet glass"><div class="grab"></div><div class="row" style="padding:6px 10px"><span class="grow"></span><button class="ibtn" data-close>' + ic('x') + '</button></div><div class="body scroll">' +
      '<div class="gallery" id="gal">' + m.gallery.map(function (g) { return /\.(mp4|webm)$/.test(g) ? '<video src="' + g + '" controls playsinline muted></video>' : '<div style="background-image:url(' + g + ')" data-zoom="' + g + '"></div>'; }).join('') + '</div>' +
      '<div class="gdots">' + m.gallery.map(function (g, i) { return '<i class="' + (i ? '' : 'on') + '"></i>'; }).join('') + '</div>' +
      '<div class="mod-head"><div class="ic" style="background-image:url(' + m.icon + ')"></div><div class="grow"><div class="b" style="font-size:22px">' + esc(m.name) + '</div><div class="small dim">' + esc(m.kind) + ' &middot; ' + esc(m.ver) + ' &middot; ' + esc(m.size) + '</div><div class="row small">' + stars(r.avg) + '<span class="dim">' + (r.n ? r.avg.toFixed(1) + ' &middot; ' + r.n + ' ratings' : 'No ratings yet') + '</span></div></div></div>' +
      '<div style="padding:0 16px"><button class="btn go wide" data-dl="' + m.id + '">' + ic('dl', 's') + ' ' + m.action + ' &middot; Free</button></div>' +
      '<div class="sec" style="padding:0 16px">About</div><div style="padding:0 16px;opacity:.92">' + esc(m.desc) + '</div>' +
      '<div class="sec" style="padding:0 16px">Features</div><ul class="feat">' + m.features.map(function (f) { return '<li>' + esc(f) + '</li>'; }).join('') + '</ul>' +
      '<div class="sec" style="padding:0 16px">Rate it</div><div style="padding:0 16px" class="row">' + stars(r.mine, true, m.id) + '<span class="small dim">' + (r.mine ? 'Your rating: ' + r.mine : 'Tap a star') + '</span></div>' +
      '<div class="card by"><div class="row">' + ic('user') + '<div><div class="tiny dim up">Made by</div><div class="b">ThirtyFox (ThirtyFoxMC)</div></div></div><div class="row">' + ic('sparkle') + '<div><div class="tiny dim up">Powered by</div><div class="b">HiveMQ</div></div></div>' +
      '<div class="row">' + ic('heart') + '<div><div class="tiny dim up">Sponsor</div><div class="b">MP community &middot; TYFMC</div></div></div><div class="row">' + ic('shield') + '<div><div class="tiny dim up">Safe download</div><div class="b">From the MP website</div></div></div></div>' +
      '</div></div>';
    D.body.appendChild(bg);
    var gal = $('#gal', bg); gal.addEventListener('scroll', function () { var i = Math.round(gal.scrollLeft / (gal.firstChild.offsetWidth + 10)); $$('.gdots i', bg).forEach(function (d, k) { d.classList.toggle('on', k === i); }); });
    bg.addEventListener('click', function (e) {
      if (e.target === bg || e.target.closest('[data-close]')) { bg.remove(); return; }
      var z = e.target.closest('[data-zoom]'); if (z) { var v = D.createElement('div'); v.className = 'dlg-bg'; v.innerHTML = '<img src="' + z.dataset.zoom + '" style="max-width:100%;max-height:100%">'; v.onclick = function () { v.remove(); }; D.body.appendChild(v); }
    });
    var off = function () { if (!D.body.contains(bg)) return; var r2 = Soc.rating(m.id); var s = $('.stars.big', bg); if (s) s.outerHTML = stars(r2.mine, true, m.id); };
    Soc.on('rating', off);
  }

  // ===== SOCIAL (Chats / Friends / Servers / Party / Inbox) =====
  PAGES.social = function () {
    var t = CUR.tab || 'chats', body = '';
    if (!Soc.id) return { top: title('Social'), body: '<div class="empty">Starting...</div>' };
    if (t === 'chats') {
      var cs = Soc.convs();
      body = '<div class="row" style="margin-bottom:10px"><button class="btn sm go" data-act="newgroup">' + ic('plus', 's') + ' New group</button><button class="btn sm" data-act="addfriend">' + ic('user', 's') + ' Add friend</button></div>';
      body += cs.length ? '<div class="card list">' + cs.map(function (c) {
        var last = (c.msgs || [])[c.msgs.length - 1], dm = c.kind === 0, id = dm ? c.key.slice(1) : '';
        var nm = dm ? (Soc.profile(id) && Soc.profile(id).display || Soc.nameOf(id) || c.title) : c.title;
        return '<div class="li" data-chat="' + esc(c.key) + '">' + (dm ? avatar(id, nm, Soc.profile(id), 'av') : '<div class="guild" style="width:40px;height:40px;background:' + colorOf(c.key) + '">' + ic(c.kind === 2 ? 'party' : 'users', 's') + '</div>') +
          '<div class="grow"><div class="row"><span class="b ell grow">' + esc(nm) + '</span><span class="tiny dim">' + (last ? ago(last.ts) : '') + '</span></div><div class="small dim ell">' + (last ? esc((last.mine ? 'You: ' : last.fromName ? last.fromName + ': ' : '') + last.text) : 'No messages yet') + '</div></div>' + (c.unread ? '<span class="badge">' + c.unread + '</span>' : '') + '</div>';
      }).join('') + '</div>' : '<div class="empty">' + ic('chat', 'l') + '<p>No chats yet. Open a friend and tap Message.</p></div>';
    } else if (t === 'friends') {
      var fs = Soc.S.friends.slice().sort(function (a, b) { return (Soc.isOnline(b.id) ? 1 : 0) - (Soc.isOnline(a.id) ? 1 : 0) || a.name.localeCompare(b.name); });
      body = '<div class="card pad row wrap" style="margin-bottom:12px"><div class="grow"><div class="tiny dim up">Your ID</div><div class="b" style="font-size:20px;letter-spacing:2px">#' + Soc.id + '</div></div><button class="btn sm" data-copy="' + Soc.id + '">' + ic('copy', 's') + ' Copy</button><button class="btn sm go" data-act="addfriend">' + ic('plus', 's') + ' Add friend</button></div>';
      if (Soc.S.inc.length) body += '<div class="sec">Requests<span class="count">' + Soc.S.inc.length + '</span></div><div class="card list">' + Soc.S.inc.map(function (f) { return '<div class="li">' + avatar(f.id, f.name, null, 'av') + '<div class="grow"><div class="b">' + esc(f.name) + '</div><div class="small dim">#' + f.id + '</div></div><button class="btn sm go" data-acc="' + f.id + '">Accept</button><button class="btn sm" data-dec="' + f.id + '">Decline</button></div>'; }).join('') + '</div>';
      if (Soc.S.out.length) body += '<div class="sec">Sent<span class="count">' + Soc.S.out.length + '</span></div><div class="card list">' + Soc.S.out.map(function (f) { return '<div class="li">' + avatar(f.id, f.name, null, 'av') + '<div class="grow"><div>' + esc(f.name) + '</div><div class="small dim">#' + f.id + '</div></div><button class="btn sm" data-can="' + f.id + '">Cancel</button></div>'; }).join('') + '</div>';
      body += '<div class="sec">Friends<span class="count">' + fs.length + '</span></div>' + (fs.length ? '<div class="card list">' + fs.map(function (f) { var pr = Soc.profile(f.id); return '<div class="li ' + esc(pr && pr.nameplate || '') + '" data-user="' + f.id + '">' + avatar(f.id, f.name, pr, 'av') + '<div class="grow"><div class="b ' + esc(pr && pr.nametag || '') + '">' + esc(pr && pr.display || f.name) + '</div><div class="small dim ell">' + esc(statusLine(pr) || (Soc.isOnline(f.id) ? (Soc.activity(f.id) === 'Moquil' ? 'On Moquil' : 'Playing ' + Soc.activity(f.id)) : 'Offline')) + '</div></div><button class="ibtn" data-dm="' + f.id + '">' + ic('chat', 's') + '</button></div>'; }).join('') + '</div>' : '<div class="dim small">No friends yet.</div>');
    } else if (t === 'communities') {
      var all = Soc.communities(), mine = all.filter(function (c) { return c.joined || c.ownerId === Soc.id; });
      body = '<div class="guilds">' + mine.map(function (c) { return '<div data-com="' + esc(c.cid) + '" title="' + esc(c.name) + '">' + srvIcon(c, 48) + '</div>'; }).join('') + '<div class="guild add" data-act="newcom" title="Add a Server">' + ic('plus') + '</div></div>';
      body += '<div class="sec">Your servers<span class="count">' + mine.length + '</span></div>' + (mine.length ? '<div class="card list">' + mine.map(comRow).join('') + '</div>' : '<div class="dim small">Join one below or make your own.</div>');
      var other = all.filter(function (c) { return mine.indexOf(c) < 0; });
      body += '<div class="sec">Discover<span class="count">' + other.length + '</span></div>' + (other.length ? '<div class="card list">' + other.slice(0, 40).map(comRow).join('') + '</div>' : '<div class="dim small">Nothing to show yet.</div>');
    } else if (t === 'party') {
      var p = Soc.party();
      if (!p) body = '<div class="empty">' + ic('party', 'l') + '<p>A party is up to 5 friends. You see each other in the game and can talk in a voice room.</p><button class="btn go" data-act="mkparty">Make a party</button></div>';
      else body = '<div class="card pad"><div class="row"><div class="b grow" style="font-size:18px">' + esc(p.title) + '</div><button class="btn sm" data-chat="' + esc(p.key) + '">' + ic('chat', 's') + ' Chat</button><button class="btn sm" data-voice="g' + esc(p.gid) + '" data-vt="' + esc(p.title) + '">' + ic('mic', 's') + ' Voice</button><button class="btn sm red" data-act="leaveparty">Leave</button></div>' +
        '<div class="list" style="margin-top:10px">' + p.members.map(function (m) { return '<div class="li" data-user="' + m.id + '">' + avatar(m.id, m.name, Soc.profile(m.id), 'av') + '<div class="grow b">' + esc(m.name) + (m.id === p.owner ? ' ' + ic('crown', 's') : '') + '</div></div>'; }).join('') + '</div>' +
        (p.owner === Soc.id && p.members.length < 5 ? '<div class="sec">Invite a friend</div><div class="card list">' + Soc.S.friends.filter(function (f) { return !p.members.some(function (m) { return m.id === f.id; }); }).map(function (f) { return '<div class="li">' + avatar(f.id, f.name, null, 'av s') + '<div class="grow">' + esc(f.name) + '</div><button class="btn sm go" data-pinv="' + f.id + '">Invite</button></div>'; }).join('') + '</div>' : '') + '</div>';
    } else {
      var ib = Soc.inbox();
      body = '<div class="row" style="margin-bottom:10px"><button class="btn sm" data-act="readall">Mark all read</button><button class="btn sm" data-act="clearinbox">Clear</button></div>' + (ib.length ? '<div class="card list">' + ib.map(function (n) {
        var acts = n.kind === 'freq' && !n.done && Soc.isInc(n.fromId) ? '<button class="btn sm go" data-acc="' + n.fromId + '">Accept</button><button class="btn sm" data-dec="' + n.fromId + '">Decline</button>' : n.kind === 'pinv' && !n.done ? '<button class="btn sm go" data-pacc="' + esc(n.ref) + '">Join</button><button class="btn sm" data-pdec="' + esc(n.ref) + '">No</button>' : '';
        return '<div class="li" ' + (n.kind === 'msg' || n.kind === 'group' ? 'data-chat="' + esc(n.ref) + '"' : n.kind === 'com' ? 'data-com="' + esc(n.ref) + '"' : '') + ' style="' + (n.read ? 'opacity:.7' : '') + '">' + avatar(n.fromId, n.fromName, Soc.profile(n.fromId), 'av s') + '<div class="grow"><div class="small">' + esc(n.text) + '</div><div class="tiny dim">' + ago(n.ts) + '</div></div>' + acts + '</div>';
      }).join('') + '</div>' : '<div class="empty">' + ic('bell', 'l') + '<p>Nothing new.</p></div>');
    }
    var u = Soc.unreadChats(), ni = Soc.unreadInbox();
    return { top: tabs([['chats', 'Chats' + (u ? ' <span class="badge">' + u + '</span>' : '')], ['friends', 'Friends' + (Soc.S.inc.length ? ' <span class="badge">' + Soc.S.inc.length + '</span>' : '')], ['communities', 'Servers'], ['party', 'Party'], ['inbox', 'Inbox' + (ni ? ' <span class="badge">' + ni + '</span>' : '')]], t), body: body };
  };

  // ===== a chat (DM / group / party) =====
  PAGES.chat = function () {
    var c = Soc.id && Soc.conv(CUR.arg); if (!c) return { top: title('Chat', true), body: '<div class="empty">This chat is gone.</div>' };
    Soc.markRead(c.key);
    var dm = c.kind === 0, id = dm ? c.key.slice(1) : '', pr = dm ? Soc.profile(id) : null, nm = dm ? (pr && pr.display || Soc.nameOf(id) || c.title) : c.title;
    var room = dm ? 'd' + [Soc.id, id].sort().join('') : 'g' + c.gid;
    var top = '<button class="ibtn" data-act="back">' + ic('back') + '</button>' + (dm ? avatar(id, nm, pr, 'av s') : ic(c.kind === 2 ? 'party' : 'users')) + '<div class="grow" ' + (dm ? 'data-user="' + id + '"' : '') + ' style="cursor:pointer"><div class="b ell">' + esc(nm) + '</div><div class="tiny dim ell">' + (dm ? (Soc.isOnline(id) ? 'Online' : 'Offline') + (statusLine(pr) ? ' &middot; ' + esc(statusLine(pr)) : '') : c.members.length + ' members') + '</div></div>' +
      '<button class="ibtn" data-voice="' + room + '" data-vt="' + esc(nm) + '" title="Voice">' + ic('mic') + '</button>' + (c.kind === 1 ? '<button class="ibtn" data-gadd="' + esc(c.key) + '" title="Add people">' + ic('plus') + '</button><button class="ibtn" data-gleave="' + esc(c.key) + '" title="Leave">' + ic('door') + '</button>' : '');
    return { top: top, full: true, body: voiceBar(room) + '<div class="msgs scroll" id="msgs"></div>' + composer('Message ' + nm), after: function () { updateMsgs(true); bindComposer(function (txt) { if (Soc.send(c.key, txt)) quest('msg'); updateMsgs(true); }); } };
  };
  function composer(ph) { return '<div class="composer card"><button class="ibtn" data-act="emoji" title="Emoji">' + ic('smile', 's') + '</button><input class="inp grow" id="ci" maxlength="400" placeholder="' + esc(ph) + '" autocomplete="off"><button class="ibtn" data-act="mdhelp" title="Text formatting">' + ic('edit', 's') + '</button><button class="ibtn" data-act="gift" title="Send Orbs">' + ic('gift', 's') + '</button><button class="ibtn" id="csend" title="Send">' + ic('send', 's') + '</button></div>'; }
  function bindComposer(fn) { var i = $('#ci'); var send = function () { var v = i.value.trim(); if (!v) return; i.value = ''; fn(v); i.focus(); }; i.onkeydown = function (e) { if (e.key === 'Enter') { e.preventDefault(); send(); } }; $('#csend').onclick = send; }
  function msgHtml(m, prev) {
    if (!m.fromId) return '<div class="msg sys">' + esc(m.text) + '</div>';
    var same = prev && prev.fromId === m.fromId && m.ts - prev.ts < 300;
    var me = m.fromId === Soc.id, pr = me ? G.MoqProfile() : Soc.profile(m.fromId), nm = me ? (prof().display || SESSION.user) : (pr && pr.display || m.fromName);
    return '<div class="msg"' + (same ? ' style="padding-top:0"' : '') + '>' + (same ? '<div style="width:40px;flex:none"></div>' : '<div data-user="' + m.fromId + '">' + avatar(m.fromId, nm, pr, 'av') + '</div>') + '<div class="w">' + (same ? '' : '<div><span class="n ' + esc(pr && pr.nametag || '') + '">' + esc(nm) + '</span><span class="tm">' + clock(m.ts) + '</span></div>') + '<div class="tx">' + md(m.text) + '</div></div></div>';
  }
  function linkify(s) { return s.replace(/(https?:\/\/[^\s<]+)/g, '<a href="#" data-open="$1">$1</a>'); }
  // 4.13.2: Discord message formatting (discord-api-docs > Reference > Message Formatting):
  // **bold** *italic* __underline__ ~~strike~~ `code` ```blocks``` ||spoiler|| > quote, # / ## / ### headers, -# subtext,
  // [text](https://link), <@id> mentions, <t:unix> / <t:unix:t|T|d|D|f|F|s|S|R> timestamps in your own time zone.
  function tsFmt(sec, st) {
    var d = new Date(sec * 1000); if (isNaN(d)) return '';
    var o = { t: { hour: 'numeric', minute: '2-digit' }, T: { hour: 'numeric', minute: '2-digit', second: '2-digit' }, d: { day: '2-digit', month: '2-digit', year: 'numeric' }, D: { day: 'numeric', month: 'long', year: 'numeric' },
      f: { day: 'numeric', month: 'long', year: 'numeric', hour: 'numeric', minute: '2-digit' }, F: { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric', hour: 'numeric', minute: '2-digit' },
      s: { day: '2-digit', month: '2-digit', year: 'numeric', hour: 'numeric', minute: '2-digit' }, S: { day: '2-digit', month: '2-digit', year: 'numeric', hour: 'numeric', minute: '2-digit', second: '2-digit' } };
    if (st === 'R') {
      var df = (d.getTime() - Date.now()) / 1000, a = Math.abs(df), u = [[31536000, 'year'], [2592000, 'month'], [86400, 'day'], [3600, 'hour'], [60, 'minute'], [1, 'second']];
      for (var i = 0; i < u.length; i++) if (a >= u[i][0] || i === u.length - 1) { var n = Math.round(df / u[i][0]); try { return new Intl.RelativeTimeFormat(undefined, { numeric: 'auto' }).format(n, u[i][1]); } catch (e) { return Math.abs(n) + ' ' + u[i][1] + 's ' + (n < 0 ? 'ago' : 'from now'); } }
    }
    try { return d.toLocaleString(undefined, o[st] || o.f); } catch (e) { return d.toString(); }
  }
  function md(raw) {
    var t = esc(raw), keep = [];
    var hold = function (h) { keep.push(h); return '\u0000' + (keep.length - 1) + '\u0000'; };
    t = t.replace(/```(?:[a-z0-9+#-]*\n)?([\s\S]*?)```/gi, function (_, c) { return hold('<pre class="mdpre">' + c.replace(/^\n|\n$/g, '') + '</pre>'); });
    t = t.replace(/`([^`\n]+)`/g, function (_, c) { return hold('<code class="mdc">' + c + '</code>'); });
    t = t.replace(/&lt;t:(-?\d{1,12})(?::([tTdDfFsSR]))?&gt;/g, function (_, n, st) { var f = tsFmt(+n, st || 'f'); return f ? hold('<span class="mdts" title="' + esc(tsFmt(+n, 'F')) + '">' + esc(f) + '</span>') : _; });
    t = t.replace(/&lt;@!?(\d{8})&gt;/g, function (_, id) { var me = id === Soc.id, p = me ? G.MoqProfile() : Soc.profile(id); return hold('<span class="mdmen" data-user="' + id + '">@' + esc(me ? (prof().display || SESSION.user) : (p && p.display) || Soc.nameOf && Soc.nameOf(id) || id) + '</span>'); });
    t = t.replace(/\[([^\]\n]{1,80})\]\((https?:\/\/[^\s)]+)\)/g, function (_, l, u) { return hold('<a href="#" data-open="' + u + '">' + l + '</a>'); });
    t = t.replace(/(https?:\/\/[^\s<\u0000]+)/g, function (u) { return hold('<a href="#" data-open="' + u + '">' + u + '</a>'); });
    t = t.split('\n').map(function (l) {
      var m;
      if ((m = l.match(/^-# (.+)/))) return '<div class="mdsub">' + m[1] + '</div>';
      if ((m = l.match(/^(#{1,3}) (.+)/))) return '<div class="mdh' + m[1].length + '">' + m[2] + '</div>';
      if ((m = l.match(/^&gt; ?(.*)/))) return '<div class="mdq">' + m[1] + '</div>';
      if ((m = l.match(/^\s*[-*] (.+)/))) return '<div class="mdli">' + m[1] + '</div>';
      return l + '\n';
    }).join('').replace(/\n$/, '');
    t = t.replace(/\|\|(.+?)\|\|/g, '<span class="spoil" title="Spoiler">$1</span>')
      .replace(/\*\*\*(.+?)\*\*\*/g, '<b><i>$1</i></b>').replace(/\*\*(.+?)\*\*/g, '<b>$1</b>').replace(/__(.+?)__/g, '<u>$1</u>')
      .replace(/(^|[^*\w])\*([^*\n]+)\*(?!\*)/g, '$1<i>$2</i>').replace(/(^|[^_\w])_([^_\n]+)_(?!_)/g, '$1<i>$2</i>').replace(/~~(.+?)~~/g, '<s>$1</s>');
    return t.replace(/\u0000(\d+)\u0000/g, function (_, i) { return keep[+i]; }).replace(/<\/pre>\n/g, '</pre>');
  }
  function updateMsgs(force) {
    var box = $('#msgs'); if (!box) return;
    var list = [];
    if (CUR.page === 'chat') { var c = Soc.conv(CUR.arg); if (!c) return; list = c.msgs || []; Soc.markRead(c.key); }
    else if (CUR.page === 'community') { var cm = Soc.community(CUR.arg); if (!cm) return; list = SV().posts(cm.cid, comCh).slice().reverse(); }
    var atEnd = box.scrollHeight - box.scrollTop - box.clientHeight < 80;
    var html = list.length ? list.map(function (m, i) { return cm ? comMsgHtml(cm.cid, m, list[i - 1]) : msgHtml(m, list[i - 1]); }).join('') : '<div class="empty small">' + (CUR.page === 'community' ? 'No posts yet. Say hi.' : 'Say hi.') + '</div>';
    if (box._h === html) return; box._h = html; box.innerHTML = html;
    if (force || atEnd) box.scrollTop = box.scrollHeight;
    var vb = $('#vbar'); if (vb && G.MoqVoice) vb.outerHTML = voiceBar(vb.dataset.room);
  }
  function voiceBar(room) {
    var V = G.MoqVoice && MoqVoice.state; if (!V || V.room !== room) return '<div id="vbar" data-room="' + esc(room) + '" class="hide"></div>';
    var ppl = MoqVoice.people();
    return '<div id="vbar" data-room="' + esc(room) + '" class="card vroom" style="margin-bottom:8px"><div class="row">' + ic('vol', 's') + '<b class="grow">Voice connected</b><span class="tiny dim">' + ppl.length + ' in the room</span></div><div class="vgrid">' + ppl.map(function (p) { return '<div class="card vtile' + (p.talking ? ' talk' : '') + '">' + avatar(p.id, p.name, p.me ? G.MoqProfile() : Soc.profile(p.id), 'av l', true) + '<div class="small ell b">' + esc(p.name) + '</div><div class="tiny dim">' + (p.me ? (V.muted ? 'muted' : 'you') : p.state) + '</div></div>'; }).join('') + '</div>' +
      '<div class="vbar"><button class="btn sm ' + (V.muted ? 'red' : '') + '" data-act="vmute">' + ic(V.muted ? 'micoff' : 'mic', 's') + (V.muted ? ' Unmute' : ' Mute') + '</button><button class="btn sm ' + (V.deaf ? 'red' : '') + '" data-act="vdeaf">' + ic('head', 's') + (V.deaf ? ' Undeafen' : ' Deafen') + '</button><button class="btn sm red" data-act="vleave">' + ic('x', 's') + ' Leave</button></div></div>';
  }

  // ===== a community (the glass server screen: channels, chat, members) =====
  var comCh = 'general', comView = 'list';
  PAGES.community = function () {
    var c = Soc.id && Soc.community(CUR.arg); if (!c) return { top: title('Server', true), body: '<div class="empty">Loading the server...</div>', after: function () { if (Soc.id) Soc.viewCommunity(CUR.arg); } };
    Soc.viewCommunity(c.cid);
    var cid = c.cid, owner = c.ownerId === Soc.id, k = SV().chan(cid, comCh); comCh = k.id;
    var room = SV().voiceRoom(cid, (SV().voiceChans(cid)[0] || { id: 'lounge' }).id), pins = SV().pins(cid, comCh).length;
    var tools = '<button class="ibtn" data-srvev="' + esc(cid) + '" title="Events">' + ic('cal') + '</button><button class="ibtn" data-pins="' + esc(cid) + '" title="Pinned messages">' + ic('pin') + (pins ? '<span class="badge">' + pins + '</span>' : '') + '</button>' + (srvManager(cid) ? '<button class="ibtn" data-srvset="' + esc(cid) + '" title="Server Settings">' + ic('gear') + '</button>' : '');
    var top = '<button class="ibtn" data-act="back">' + ic('back') + '</button>' + srvIcon(c, 34, 'border-radius:10px;') + '<div class="grow"><div class="b ell">' + esc(c.name) + '</div><div class="tiny dim">' + SV().members(cid).length + ' members &middot; ' + (c.followers || 0) + ' following</div></div>' +
      '<button class="ibtn" data-invite="' + esc(cid) + '" title="Invite people">' + ic('plus') + '</button>' + tools +
      (owner ? '' : '<button class="btn sm ' + (c.joined ? '' : 'go') + '" data-join="' + esc(cid) + '">' + (c.joined ? 'Leave' : 'Join') + '</button>');
    var chIc = ic(k.kind === 'news' ? 'bell' : 'hash');
    if (!isPortrait()) top = '<div class="ftop">' + chIc + '<b>' + esc(k.name) + '</b>' + (k.topic || c.desc ? '<i class="dot-sep"></i><span class="small ell grow">' + esc(k.topic || c.desc) + '</span>' : '<span class="grow"></span>') + tools + '<button class="ibtn" data-invite="' + esc(cid) + '" title="Invite people">' + ic('user') + '</button></div>';
    else if (comView === 'chat') top = '<button class="ibtn" data-act="comback">' + ic('back') + '</button>' + ic(k.kind === 'news' ? 'bell' : 'hash', 's') + '<div class="grow"><div class="b ell">' + esc(k.name) + '</div><div class="tiny dim ell">' + esc(k.topic || c.name) + '</div></div>' + tools;
    var chs = '<div class="chs card"><div class="small dim" style="margin-bottom:8px">' + esc(c.desc || 'Welcome to ' + c.name) + '</div>' +
      '<div class="ch-list"><div class="ch" data-srvev="' + esc(cid) + '">' + ic('cal', 's') + ' Events' + (SV().events(cid).some(SV().eventLive) ? ' <span class="chip green">LIVE</span>' : '') + '</div>' + (SV().cfg(cid).rules ? '<div class="ch" data-srvrules="' + esc(cid) + '">' + ic('shield', 's') + ' Rules</div>' : '') + '</div>' +
      '<div class="ch-list">' + chanLists(c, 'ch') + '</div>' +
      '<div class="grow"></div><div class="mini" style="margin:10px 0 0;padding:6px;border-top:1px solid var(--line2)">' + avatar(Soc.id, SESSION.user, null, 'av s') + '<div class="grow ell small b">' + esc(prof().display || SESSION.user) + '</div><button class="ibtn" data-act="vmute">' + ic(G.MoqVoice && MoqVoice.state.muted ? 'micoff' : 'mic', 's') + '</button><button class="ibtn" data-act="vdeaf">' + ic('head', 's') + '</button><button class="ibtn" data-go="settings">' + ic('gear', 's') + '</button></div></div>';
    var vroom = G.MoqVoice && MoqVoice.state.room && MoqVoice.state.room.indexOf('c' + cid) === 0 ? MoqVoice.state.room : room;
    var mid = '<div class="cm card"><div class="row small dim" style="padding-bottom:8px;border-bottom:1px solid var(--line2)">' + ic(k.kind === 'news' ? 'bell' : 'hash', 's') + '<b style="color:var(--text)">' + esc(k.name) + '</b>' + (k.topic ? '<span class="ell grow">' + esc(k.topic) + '</span>' : '') + '</div>' + voiceBar(vroom) + '<div class="msgs scroll" id="msgs"></div>' + comComposer(c) + '</div>';
    var mm = '<div class="mem card scroll" id="mm">' + memberPanel(cid) + '</div>';
    return { top: top, full: true, body: '<div class="comm-split v-' + comView + '">' + chs + mid + mm + '</div>', after: function () { updateMsgs(true); bindComCompose(c); } };
  };

  // ===== PROFILE (Discord) =====
  PAGES.profile = function () { return profilePage(Soc.id || '', true); };
  PAGES.user = function () { if (CUR.arg === Soc.id) return profilePage(Soc.id, true); if (Soc.id) Soc.watchProfile(CUR.arg); return profilePage(CUR.arg, false); };
  function profilePage(id, me) {
    var p = me ? Object.assign(prof(), G.MoqProfile()) : (Soc.profile(id) || {});
    var name = me ? SESSION.user : (Soc.nameOf(id) || (Soc.profile(id) || {}).name || ('#' + id));
    var disp = p.display || name, w = wallet(), badges = me ? w.badges : (p.badges || []);
    var t = CUR.tab === 'activity' ? 'activity' : 'main';
    var on = me ? Soc.up() : Soc.isOnline(id), act = me ? (on ? 'Moquil' : '') : Soc.activity(id);
    var since = me ? memberSince() : p.since;
    var html = '<div class="prof card ' + esc(p.frame || '') + '" style="padding-bottom:14px">' + (p.effect ? '<div class="fx ' + esc(p.effect) + '"></div>' : '') +
      '<div class="banner" style="' + (p.banner ? 'background-image:url(' + p.banner + ')' : 'background:' + esc(p.bannerColor || '#5865f2')) + '">' + (me ? '<button class="ibtn x" data-go="settings" style="background:rgba(0,0,0,.4);color:#fff;left:auto;right:10px">' + ic('gear') + '</button>' : '') + '</div>' +
      '<div class="head">' + avatar(me ? Soc.id : id, disp, p, 'av xl', on) + (statusLine(p) || me ? '<div class="bubble" ' + (me ? 'data-act="status"' : '') + '>' + (statusLine(p) ? linkify(esc(statusLine(p))) : '<span class="dim">' + ic('smile', 's') + ' Set a custom status</span>') + '</div>' : '') + '</div>' +
      '<div style="padding:10px 16px 0;position:relative"><div class="nameplate ' + esc(p.nameplate || '') + '" style="padding:2px 6px;margin-left:-6px"><div class="nm"><span class="' + esc(p.nametag || '') + '">' + esc(disp) + '</span></div></div>' +
      '<div class="handle"><span>' + esc(name) + '</span>' + (p.pronouns ? '<span>&middot; ' + esc(p.pronouns) + '</span>' : '') + '<span>&middot; #' + esc(id) + '</span>' + (badges.length ? '<span class="badges">' + badges.filter(function (b) { return BADGES[b]; }).map(function (b) { return '<span data-badge="' + b + '" title="' + esc(BADGES[b][1]) + '">' + BADGES[b][0] + '</span>'; }).join('') + '</span>' : '') + '</div>' +
      (me ? '<div class="row" style="margin-top:12px"><button class="btn blue grow" data-act="editprof">' + ic('edit', 's') + ' Edit Profile</button><button class="btn" data-act="status">' + ic('smile', 's') + ' Status</button></div>'
          : '<div class="row" style="margin-top:12px">' + (Soc.isFriend(id) ? '<button class="btn go grow" data-dm="' + id + '">' + ic('chat', 's') + ' Message</button><button class="btn" data-fav="' + id + '">' + ic('fav', 's') + (Soc.S.favs.indexOf(id) >= 0 ? ' Unfavorite' : ' Favorite') + '</button><button class="btn red" data-rem="' + id + '">Remove</button>' : Soc.isOut(id) ? '<button class="btn grow" disabled>Request sent</button>' : Soc.isInc(id) ? '<button class="btn go grow" data-acc="' + id + '">Accept friend</button>' : '<button class="btn go grow" data-add="' + id + '">' + ic('plus', 's') + ' Add Friend</button>') + '</div>') +
      '</div></div>';
    html += '<div class="ptabs">' + [['main', 'Main'], ['activity', 'Activity']].map(function (x) { return '<div class="tab' + (t === x[0] ? ' on' : '') + '" data-tab="' + x[0] + '">' + x[1] + '</div>'; }).join('') + '</div>';
    if (t === 'main') {
      if (me) html += '<div class="card balance" data-go="store"><div class="row">' + ic('orb') + '<b>Orbs balance</b></div><span class="orbs">' + w.orbs + '</span></div>';
      if (on && act) html += '<div class="sec">' + (act === 'Moquil' ? 'Online' : 'Playing') + '</div><div class="card pad row"><div class="mini" style="margin:0;padding:0"><div class="ic" style="width:48px;height:48px;background-image:url(' + (act === 'Moquil' ? 'img/logo.png' : 'img/hero.jpg') + ')"></div></div><div class="grow"><div class="b">' + esc(act === 'Moquil' ? 'Moquil' : 'MP Multiplayer') + '</div><div class="small dim">' + esc(act === 'Moquil' ? (IS_WEB && me ? 'On the web' : 'On the phone') : act) + '</div></div></div>';
      if (me) html += '<div class="sec">Voice settings</div><div class="card list"><div class="li" data-go="settings" data-subto="voice">' + ic('mic') + '<div class="grow">Voice and audio</div>' + ic('chev', 's') + '</div></div>';
      html += '<div class="sec">About me</div><div class="card pad" ' + (me ? 'data-act="editprof"' : '') + ' style="white-space:pre-wrap">' + (p.bio ? linkify(esc(p.bio)) : '<span class="dim">' + (me ? 'Tap to write something about you.' : 'Nothing here yet.') + '</span>') + '</div>';
      if (since) html += '<div class="sec">Member since</div><div class="card pad row">' + ic('logo' in IC ? 'logo' : 'sparkle', 's') + '<span>' + dateStr(since) + '</span></div>';
    } else {
      var fr = me ? Soc.S.friends.length : 0;
      html += '<div class="card list">' + (badges.length ? badges.filter(function (b) { return BADGES[b]; }).map(function (b) { return '<div class="li"><span style="font-size:22px">' + BADGES[b][0] + '</span><div class="grow"><div class="b">' + BADGES[b][1] + '</div><div class="small dim">' + BADGES[b][2] + '</div></div></div>'; }).join('') : '<div class="li dim">No badges yet</div>') + '</div>';
      if (me) html += '<div class="sec">Stats</div><div class="card pad small">' + fr + ' friends &middot; ' + Soc.communities().filter(function (c) { return c.joined; }).length + ' servers &middot; ' + Soc.convs().length + ' chats</div>';
    }
    return { top: me ? title('You') + topRight() : title(esc(disp), true), body: html };
  }
  function editProfile() {
    var p = prof(), w = wallet();
    function opts(cat) { var c = STORE.filter(function (x) { return x.cat === cat; })[0]; return '<option value="">None</option>' + c.items.filter(function (it) { return w.owned.indexOf(it[0]) >= 0; }).map(function (it) { return '<option value="' + it[0] + '"' + (p[cat] === it[0] ? ' selected' : '') + '>' + it[1] + '</option>'; }).join(''); }
    dialog({ title: 'Edit profile', icon: 'edit',
      body: '<div class="row"><div class="av l" id="eav">' + avPic(p.avatar, SESSION.user) + '</div><div class="col grow"><button class="btn sm" data-pick="avatar">' + ic('image', 's') + ' Change avatar</button><button class="btn sm ghost" data-pick="noavatar">Remove</button></div></div>' +
        '<label class="lbl">Display name</label><input class="inp" id="edisp" maxlength="32" value="' + esc(p.display) + '">' +
        '<label class="lbl">Pronouns</label><input class="inp" id="epro" maxlength="40" placeholder="she/her, he/him, they/them" value="' + esc(p.pronouns) + '">' +
        '<label class="lbl">About me</label><textarea class="inp" id="ebio" maxlength="190" placeholder="Tell people about you">' + esc(p.bio) + '</textarea><div class="tiny dim" style="text-align:right" id="ebioc"></div>' +
        '<label class="lbl">Banner</label><div class="row"><input type="color" id="ebc" value="' + esc(p.bannerColor || '#5865f2') + '" style="width:48px;height:40px;border:0;background:none"><button class="btn sm" data-pick="banner">' + ic('image', 's') + ' Banner image</button><button class="btn sm ghost" data-pick="nobanner">Remove</button></div>' +
        '<div class="row"><div class="grow"><label class="lbl">Avatar decoration</label><select class="inp" id="edeco">' + opts('deco') + '</select></div><div class="grow"><label class="lbl">Profile effect</label><select class="inp" id="efx">' + opts('effect') + '</select></div></div>' +
        '<div class="row"><div class="grow"><label class="lbl">Profile frame</label><select class="inp" id="efr">' + opts('frame') + '</select></div><div class="grow"><label class="lbl">Nameplate</label><select class="inp" id="enp">' + opts('nameplate') + '</select></div></div>' +
        '<label class="lbl">Nametag</label><select class="inp" id="ent">' + opts('nametag') + '</select><div class="tiny dim" style="margin-top:8px">Get more in the Store with Orbs.</div>',
      onOpen: function (bg) {
        var b = $('#ebio', bg), c = $('#ebioc', bg); var upd = function () { c.textContent = b.value.length + ' / 190'; }; b.oninput = upd; upd();
        bg.addEventListener('click', function (e) {
          var k = e.target.closest('[data-pick]'); if (!k) return; var w2 = k.dataset.pick;
          if (w2 === 'avatar') pickImage(512, 512, .85, function (u) { cropSquare(u, 160, function (sq) { p.avatar = sq; $('#eav', bg).innerHTML = avPic(sq, SESSION.user); }); });
          if (w2 === 'noavatar') { p.avatar = ''; $('#eav', bg).innerHTML = avPic('', SESSION.user); }
          if (w2 === 'banner') pickImage(600, 220, .7, function (u) { p.banner = u; toast('Banner picked'); });
          if (w2 === 'nobanner') p.banner = '';
        });
      },
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Save', cls: 'go', fn: function (bg) {
        p.display = $('#edisp', bg).value.trim().slice(0, 32); p.pronouns = $('#epro', bg).value.trim().slice(0, 40); p.bio = $('#ebio', bg).value.slice(0, 190);
        p.bannerColor = $('#ebc', bg).value; p.deco = $('#edeco', bg).value; p.effect = $('#efx', bg).value; p.frame = $('#efr', bg).value; p.nameplate = $('#enp', bg).value; p.nametag = $('#ent', bg).value;
        saveProf(p); toast('Profile saved'); render();
      } }] });
  }
  var CLEAR = [['0', "Don't clear"], ['1800', '30 minutes'], ['3600', '1 hour'], ['14400', '4 hours'], ['today', 'Today'], ['259200', '3 days'], ['604800', '7 days']];
  var EMOJI = ['\u{1F600}', '\u{1F60E}', '\u{1F3AE}', '\u{1F525}', '\u{1F4AF}', '\u2764\uFE0F', '\u{1F634}', '\u{1F3B5}', '\u{1F697}', '\u{1F3C6}', '\u{1F44B}', '\u2615', '\u{1F4BB}', '\u{1F3AC}', '\u{1F30C}', '\u{1F389}'];
  function statusDialog() {
    var p = prof(), sel = p.status.e || '';
    dialog({ title: 'Set a custom status', icon: 'smile',
      body: '<label class="lbl">Online status</label><div class="row wrap">' + [['online', 'Online', 'on'], ['idle', 'Idle', 'idle'], ['dnd', 'Do Not Disturb', 'dnd'], ['invisible', 'Invisible', '']].map(function (x) { return '<label class="chip" style="cursor:pointer"><input type="radio" name="pres" value="' + x[0] + '"' + (p.pres === x[0] ? ' checked' : '') + '><span class="av" style="width:10px;height:10px"><span class="dot ' + x[2] + '" style="position:static;display:block;width:10px;height:10px;border:0"></span></span>' + x[1] + '</label>'; }).join('') + '</div>' +
        '<label class="lbl">Emoji</label><div class="row wrap" id="emj">' + EMOJI.map(function (e) { return '<button class="ibtn' + (sel === e ? ' on' : '') + '" data-e="' + e + '" style="font-size:20px">' + e + '</button>'; }).join('') + '</div>' +
        '<label class="lbl">What\'s happening?</label><input class="inp" id="stt" maxlength="128" placeholder="Available at: https://discord.gg/..." value="' + esc(p.status.t) + '">' +
        '<label class="lbl">Clear after</label><select class="inp" id="stc">' + CLEAR.map(function (c) { return '<option value="' + c[0] + '"' + (String(p.status.clr || '0') === c[0] ? ' selected' : '') + '>' + c[1] + '</option>'; }).join('') + '</select>' +
        (p.status.until ? '<div class="tiny dim" style="margin-top:6px">Clears ' + new Date(p.status.until).toLocaleString() + '</div>' : ''),
      onOpen: function (bg) { $('#emj', bg).addEventListener('click', function (e) { var b = e.target.closest('[data-e]'); if (!b) return; sel = sel === b.dataset.e ? '' : b.dataset.e; $$('#emj .ibtn', bg).forEach(function (x) { x.classList.toggle('on', x.dataset.e === sel); }); }); },
      buttons: [{ t: 'Clear status', fn: function () { p.status = { t: '', e: '', until: 0 }; saveProf(p); render(); } }, { t: 'Save', cls: 'go', fn: function (bg) {
        var c = $('#stc', bg).value, until = 0;
        if (c === 'today') { var d = new Date(); d.setHours(23, 59, 59, 0); until = d.getTime(); } else if (+c) until = Date.now() + (+c) * 1000;
        var pr = $$('input[name=pres]', bg).filter(function (x) { return x.checked; })[0];
        p.pres = pr ? pr.value : 'online'; p.status = { t: $('#stt', bg).value.trim().slice(0, 128), e: sel, until: until, clr: c };
        saveProf(p); toast('Status saved'); render();
      } }] });
  }

  // ===== SETTINGS (Discord list) =====
  var SETQ = '';
  var SETTINGS = [
    ['Account Settings', [['account', 'Account', 'user'], ['profiles', 'Profiles', 'edit'], ['content', 'Content & Social', 'users'], ['privacy', 'Data & Privacy', 'shield'], ['apps', 'Authorized Apps', 'lock'], ['devices', 'Devices', 'phone'], ['connections', 'Connections', 'link'], ['servers', 'Servers', 'users']]],
    ['App Settings', [['appearance', 'Appearance', 'palette'], ['voice', 'Voice & Audio', 'mic'], ['notifications', 'Notifications', 'bell'], ['permissions', 'Permissions', 'lock']]],
    ['Billing Settings', [['shop', 'Shop', 'store'], ['quests', 'Quests', 'quest']]],
    ['Support', [['support', 'Support', 'help'], ['debug', 'Upload debug logs', 'bug'], ['ack', 'Acknowledgements', 'info']]],
    ['What\'s New', [['whatsnew', 'What\'s New', 'sparkle'], ['getapp', IS_WEB ? 'Get the Android app' : 'Moquil on the web', IS_WEB ? 'phone' : 'link']]],
    ['Developer', [['version', 'App Version', 'info'], ['device', 'Device Info', 'phone'], ['client', 'Client Info', 'game'], ['logs', 'Logs', 'bug'], ['cache', 'Cache Actions', 'x']]]
  ];
  PAGES.settings = function () {
    var s = CUR.sub;
    if (s && SUBS[s]) { var r = SUBS[s](); return { top: title(r.t, true), body: r.b, after: r.after }; }
    var q = SETQ.toLowerCase(), body = '<input class="inp" id="setq" placeholder="Search" value="' + esc(SETQ) + '" style="margin-bottom:6px">';
    SETTINGS.forEach(function (g) {
      var items = g[1].filter(function (i) { return !q || i[1].toLowerCase().indexOf(q) >= 0; }); if (!items.length) return;
      body += '<div class="sec">' + g[0] + '</div><div class="card list">' + items.map(function (i) { return '<div class="li" data-set="' + i[0] + '">' + ic(i[2]) + '<div class="grow">' + i[1] + '</div>' + (i[0] === 'version' ? '<span class="dim small">' + VERSION + (IS_WEB ? ' web' : '') + '</span>' : ic('chev', 's')) + '</div>'; }).join('') + '</div>';
    });
    if (!q) body += '<div class="card list" style="margin-top:16px"><div class="li danger" data-act="logout">' + ic('out') + '<div class="grow b">Log Out</div></div></div><div class="tiny dim" style="text-align:center;margin:14px 0">Moquil ' + VERSION + ' &middot; by ThirtyFox &middot; powered by HiveMQ</div>';
    return { top: title('Settings', CUR.page === 'settings' && D.documentElement.classList.contains('portrait')) + topRight(), body: body, after: function () { var i = $('#setq'); i.oninput = function () { SETQ = i.value; var p = i.selectionStart; render(); var j = $('#setq'); j.focus(); j.setSelectionRange(p, p); }; } };
  };
  function sw(k, label, sub) { return '<div class="li" data-sw="' + k + '"><div class="grow"><div>' + label + '</div>' + (sub ? '<div class="tiny dim">' + sub + '</div>' : '') + '</div><div class="sw' + (SET[k] ? ' on' : '') + '"></div></div>'; }
  function slider(k, label, min, max, unit) { return '<div class="li" style="display:block"><div class="row"><span class="grow">' + label + '</span><b id="v_' + k + '">' + SET[k] + (unit || '') + '</b></div><input type="range" min="' + min + '" max="' + max + '" value="' + SET[k] + '" data-range="' + k + '" data-unit="' + (unit || '') + '"></div>'; }
  var SUBS = {
    servers: function () {   // 4.13.2: your servers: Server Profile (icon / banner from your files), create, join
      var all = Soc.id ? Soc.communities() : [], mine = all.filter(function (c) { return c.ownerId === Soc.id; }), joined = all.filter(function (c) { return c.joined && c.ownerId !== Soc.id; });
      var row = function (c, own) { return '<div class="li">' + srvIcon(c, 40) + '<div class="grow" data-com="' + esc(c.cid) + '"><div class="b ell">' + esc(c.name) + '</div><div class="tiny dim ell">' + (c.members || 0) + ' members' + (c.banner ? ' &middot; banner' : '') + '</div></div>' + (own ? '<button class="btn sm go" data-srvicon="' + esc(c.cid) + '">' + ic('edit', 's') + ' Server Profile</button>' : '<button class="btn sm" data-com="' + esc(c.cid) + '">Open</button>') + '</div>'; };
      return { t: 'Servers', b: '<div class="card list"><div class="li" data-act="newcom">' + ic('plus') + '<div class="grow"><div class="b">Create or join a server</div><div class="tiny dim">Pick an icon and a banner from your files (.png / .jpg)</div></div>' + ic('chev', 's') + '</div></div>' +
        '<div class="sec">Servers you own<span class="count">' + mine.length + '</span></div>' + (mine.length ? '<div class="card list">' + mine.map(function (c) { return row(c, true); }).join('') + '</div>' : '<div class="card pad small dim">You have no servers yet.</div>') +
        '<div class="sec">Servers you joined<span class="count">' + joined.length + '</span></div>' + (joined.length ? '<div class="card list">' + joined.map(function (c) { return row(c, false); }).join('') + '</div>' : '<div class="card pad small dim">Join one from Discover or with an invite.</div>') +
        '<div class="card pad small dim" style="margin-top:12px">Server Profile changes (icon, banner, name, description) show for everyone right away.</div>' };
    },
    account: function () { return { t: 'Account', b: '<div class="card list"><div class="li">' + ic('user') + '<div class="grow"><div class="tiny dim">MP account</div><div class="b">' + esc(SESSION.user) + '</div></div></div><div class="li">' + ic('hash') + '<div class="grow"><div class="tiny dim">Your social ID</div><div class="b">#' + (Soc.id || '') + '</div></div><button class="btn sm" data-copy="' + (Soc.id || '') + '">Copy</button></div><div class="li">' + ic('sparkle') + '<div class="grow"><div class="tiny dim">Member since</div><div class="b">' + dateStr(memberSince()) + '</div></div></div>' +
      '<div class="li" data-act="pwd">' + ic('lock') + '<div class="grow">Change password</div>' + ic('chev', 's') + '</div><div class="li" data-open="discord">' + ic('chat') + '<div class="grow">MP Discord server</div>' + ic('chev', 's') + '</div></div><div class="card pad small dim" style="margin-top:12px">You stay logged in on this ' + (IS_WEB ? 'browser' : 'phone') + ' until you tap Log Out. Log in on the web or another phone with the same MP account and your ID, friends and chats follow you.</div>' +
      '<div class="card list" style="margin-top:12px"><div class="li danger" data-act="logout">' + ic('out') + '<div class="grow b">Log Out</div></div></div>' }; },
    profiles: function () { return { t: 'Profiles', b: '<div class="card list"><div class="li" data-act="editprof">' + ic('edit') + '<div class="grow">Edit profile (name, pronouns, about me, banner)</div>' + ic('chev', 's') + '</div><div class="li" data-act="status">' + ic('smile') + '<div class="grow">Custom status</div>' + ic('chev', 's') + '</div><div class="li" data-go="store">' + ic('store') + '<div class="grow">Decorations, effects, frames, nameplates, nametags</div>' + ic('chev', 's') + '</div></div>' }; },
    content: function () { return { t: 'Content & Social', b: '<div class="card list">' + sw('dmFriends', 'Only friends can message me', 'Chats are always private between friends') + sw('showAct', 'Show my activity', 'Friends see when you play MP') + '</div>' }; },
    privacy: function () { return { t: 'Data & Privacy', b: '<div class="card pad small" style="line-height:1.55">Moquil talks over the public HiveMQ broker, like the MP game. Every message is signed, and private things (friend requests, chats, invites) are end-to-end encrypted (ECDH P-256, AES-256-GCM): the broker only sees random bytes. Your profile (name, avatar, bio, status) is public so friends and the game can show it. Your social key is saved encrypted with your MP account password, so only you can bring it to a new device.</div><div class="card list" style="margin-top:12px"><div class="li" data-act="exportdata">' + ic('dl') + '<div class="grow">Download my data (json)</div></div><div class="li danger" data-act="wipe">' + ic('x') + '<div class="grow">Delete local data on this ' + (IS_WEB ? 'browser' : 'phone') + '</div></div></div>' }; },
    apps: function () { var k = ls('moq_key') || {}; return { t: 'Authorized Apps', b: '<div class="card list"><div class="li"><div class="mini" style="margin:0;padding:0"><div class="ic" style="background-image:url(img/hero.jpg)"></div></div><div class="grow"><div class="b">MP Multiplayer</div><div class="small dim">' + (k.linked ? 'Linked: the game and Moquil are the same player' : 'Not linked yet') + '</div></div><button class="btn sm" data-set="connections">' + (k.linked ? 'Manage' : 'Link') + '</button></div><div class="li"><div class="mini" style="margin:0;padding:0"><div class="ic" style="background-image:url(img/logo.png)"></div></div><div class="grow"><div class="b">MP account bot</div><div class="small dim">Login, sign up and password reset</div></div></div></div>' }; },
    devices: function () { return { t: 'Devices', b: '<div class="card list"><div class="li">' + ic(IS_WEB ? 'game' : 'phone') + '<div class="grow"><div class="b">This ' + (IS_WEB ? 'browser' : 'phone') + '</div><div class="small dim">' + esc(IS_WEB ? navigator.userAgent.slice(0, 90) : (B.deviceInfo ? B.deviceInfo() : 'Android')) + '</div></div><span class="chip green">Now</span></div></div><div class="card pad small dim" style="margin-top:12px">Other phones and browsers with your account use the same ID. Change your password (Forgot password, Log out all devices) to stop them.</div>' }; },
    connections: function () {
      var k = ls('moq_key') || {};
      return { t: 'Connections', b: '<div class="card pad"><div class="row">' + ic('game', 'l') + '<div class="grow"><div class="b" style="font-size:17px">MP Multiplayer (the game)</div><div class="small dim">' + (k.linked ? 'Linked. Your friends, chats, party and servers are the same in the game and here.' : 'Link the game so it and Moquil are the same player.') + '</div></div></div>' +
        '<ol class="small" style="line-height:1.6;padding-left:18px"><li>Open MP Launcher on the phone with the game (MP 4.13 or newer).</li><li>Social &gt; <b>Moquil</b> &gt; Link code. A 10 character code shows.</li><li>Type it here.</li></ol>' +
        '<div class="row"><input class="inp grow" id="lcode" maxlength="12" placeholder="ABCDE12345" style="text-transform:uppercase;letter-spacing:2px"><button class="btn go" data-act="link">Link</button></div><div class="status" id="lst"></div></div>' +
        '<div class="card list" style="margin-top:12px"><div class="li" data-open="discord">' + ic('chat') + '<div class="grow"><div>Discord</div><div class="tiny dim">MP Discord server</div></div>' + ic('chev', 's') + '</div><div class="li" data-open="site">' + ic('game') + '<div class="grow"><div>MP website</div><div class="tiny dim">mpsamp-open.github.io</div></div>' + ic('chev', 's') + '</div></div>' };
    },
    appearance: function () {
      var themes = [['system', 'System'], ['dark', 'Dark'], ['black', 'Black'], ['white', 'White'], ['light', 'Light'], ['custom', 'Custom']];
      var walls = [['default', 'Default', 'url(img/ui_bg.jpg)'], ['none', 'None', 'var(--bg)'], ['color', 'Colors', SET.wallColor], ['custom', 'Custom', SET.wallData ? 'url(' + SET.wallData + ')' : '#333']];
      var grads = ['linear-gradient(135deg,#1d2b64,#5b2a86)', 'linear-gradient(135deg,#0f2027,#2c5364)', 'linear-gradient(135deg,#232526,#414345)', 'linear-gradient(135deg,#42275a,#734b6d)', 'linear-gradient(135deg,#134e5e,#71b280)', 'linear-gradient(135deg,#ff512f,#dd2476)', 'linear-gradient(135deg,#e0eafc,#cfdef3)', '#000000', '#ffffff'];
      return { t: 'Appearance', b:
        '<div class="sec">Theme</div><div class="sw-grid">' + themes.map(function (t) { var bgc = { system: 'linear-gradient(135deg,#1a1d22 50%,#e8ecf1 50%)', dark: '#16171a', black: '#000', white: '#fff', light: '#e8ecf1', custom: 'linear-gradient(135deg,' + SET.accent + ',' + SET.bg + ')' }[t[0]]; return '<div class="sw-opt' + (SET.theme === t[0] ? ' on' : '') + '" data-theme="' + t[0] + '" style="background:' + bgc + '"><span class="lb">' + t[1] + '</span></div>'; }).join('') + '</div>' +
        (SET.theme === 'custom' ? '<div class="card list" style="margin-top:10px"><div class="li"><div class="grow">Accent color</div><input type="color" data-color="accent" value="' + SET.accent + '"></div><div class="li"><div class="grow">Background color</div><input type="color" data-color="bg" value="' + SET.bg + '"></div></div>' : '') +
        '<div class="sec">Transparency</div><div class="card list">' + slider('glass', 'Panel see-through (lower = more transparent)', 0, 90, '%') + slider('blur', 'Blur behind panels', 0, 20, 'px') + slider('dim', 'Darken the wallpaper', 0, 90, '%') + sw('round', 'Rounded corners', 'Off = square boxes like the MP Launcher') + '</div>' +
        '<div class="sec">Background</div><div class="sw-grid">' + walls.map(function (w) { return '<div class="sw-opt' + (SET.wall === w[0] ? ' on' : '') + '" data-wall="' + w[0] + '" style="background:' + w[2] + ';background-size:cover"><span class="lb">' + w[1] + '</span></div>'; }).join('') + '</div>' +
        (SET.wall === 'color' ? '<div class="sw-grid" style="margin-top:10px">' + grads.map(function (g) { return '<div class="sw-opt' + (SET.wallColor === g ? ' on' : '') + '" data-grad="' + esc(g) + '" style="background:' + g + '"></div>'; }).join('') + '<div class="sw-opt" style="background:#444;align-items:center;justify-content:center"><input type="color" data-solid value="#3a3f4a" style="width:60%;height:60%;border:0;background:none"></div></div>' : '') +
        (SET.wall === 'custom' ? '<div class="row" style="margin-top:10px"><button class="btn sm go" data-act="pickwall">' + ic('image', 's') + ' Pick a wallpaper</button>' + (SET.wallData ? '<button class="btn sm" data-act="nowall">Remove</button>' : '') + '</div>' : '') +
        '<div class="card list" style="margin-top:14px"><div class="li" data-act="resetlook">' + ic('x') + '<div class="grow">Reset appearance</div></div></div>' };
    },
    voice: function () {
      return { t: 'Voice & Audio', b: '<div class="card list">' + sw('noiseSup', 'Noise suppression') + sw('echo', 'Echo cancellation') + sw('ptt', 'Push to talk', 'Hold the mic button to talk') + slider('inVol', 'Input volume', 0, 200, '%') + slider('outVol', 'Output volume', 0, 200, '%') + '</div>' +
        '<div class="card list" style="margin-top:12px"><div class="li" data-act="mictest">' + ic('mic') + '<div class="grow"><div>Mic test</div><div class="tiny dim" id="mtv">Tap to hear yourself for 5 seconds</div></div></div><div class="li" data-set="permissions">' + ic('lock') + '<div class="grow">Microphone permission: <b>' + permState('mic') + '</b></div>' + ic('chev', 's') + '</div></div>' };
    },
    notifications: function () { return { t: 'Notifications', b: '<div class="card list">' + sw('notif', 'Push notifications', 'Messages, friend requests and party invites') + sw('sounds', 'Click sounds', 'The MP click on every tap') + '<div class="li" data-set="permissions">' + ic('lock') + '<div class="grow">Notification permission: <b>' + permState('notif') + '</b></div>' + ic('chev', 's') + '</div></div>' }; },
    permissions: function () {
      var list = IS_WEB ? ['mic', 'notif'] : ['mic', 'audio', 'notif', 'storage'];
      return { t: 'Permissions', b: '<div class="card pad small dim" style="margin-bottom:12px">Moquil asks before it uses these. Allow, Don\'t allow, or Never ask again.</div><div class="card list">' + list.map(function (p) {
        var s = permState(p), P = PERMS[p];
        return '<div class="li">' + ic(P.icon) + '<div class="grow"><div class="b">' + P.name + '</div><div class="tiny dim">' + esc(P.why) + '</div></div>' + (s === 'granted' ? '<span class="chip green">Allowed</span>' : '<button class="btn sm go" data-perm="' + p + '">' + (s === 'never' ? 'Never ask (change)' : s === 'denied' ? 'Not allowed (ask)' : 'Ask') + '</button>') + '</div>';
      }).join('') + '</div>' + (B && B.openAppSettings ? '<button class="btn wide" style="margin-top:12px" data-act="appset">Open the phone settings for Moquil</button>' : '') };
    },
    shop: function () { setTimeout(function () { go('store', '', 'shop'); }, 0); return { t: 'Shop', b: '' }; },
    quests: function () { setTimeout(function () { go('store', '', 'quests'); }, 0); return { t: 'Quests', b: '' }; },
    support: function () { return { t: 'Support', b: '<div class="card list"><div class="li" data-open="discord">' + ic('chat') + '<div class="grow"><div class="b">MP Discord server</div><div class="tiny dim">Ask the staff and the server</div></div>' + ic('chev', 's') + '</div><div class="li" data-open="site">' + ic('game') + '<div class="grow"><div class="b">MP website</div><div class="tiny dim">Downloads and guides</div></div>' + ic('chev', 's') + '</div></div>' }; },
    debug: function () { return { t: 'Upload debug logs', b: '<div class="card pad small">Copy the logs and paste them in the MP Discord #support channel.</div><button class="btn go wide" style="margin-top:12px" data-act="copylogs">' + ic('copy', 's') + ' Copy logs</button>' }; },
    ack: function () { return { t: 'Acknowledgements', b: '<div class="card pad small" style="line-height:1.6">Moquil by ThirtyFox (ThirtyFoxMC).<br>Messaging over the public <b>HiveMQ</b> MQTT broker (broker.hivemq.com).<br>Voice with WebRTC and Google STUN.<br>MP Multiplayer, MLG Production and CamFX by the TYFMC team.<br>Layout ideas from the Ankama Launcher and Discord. No code or art from them is included.</div>' }; },
    getapp: function () {
      var apk = MOQ_SITE + 'downloads/Moquil.apk', web = 'https://mpsamp-open.github.io/moquils-web/';
      return { t: IS_WEB ? 'Get the Android app' : 'Moquil on the web', b: '<div class="card pad col">' +
        '<div class="row"><div class="mini" style="margin:0;padding:0"><div class="ic" style="background-image:url(img/logo.png)"></div></div><div class="grow"><div class="b">Moquil</div><div class="small dim">The same app on Android (portrait and landscape) and in any browser. Log in once on each device: it remembers you until you press Log Out.</div></div></div>' +
        '<button class="btn go" data-open="' + apk + '">' + ic('dl', 's') + ' Download Moquil.apk</button>' +
        '<button class="btn" data-open="' + web + '">' + ic('link', 's') + ' Open Moquil on the web</button>' +
        '<div class="tiny dim">Web: ' + esc(web) + '<br>Also at ' + esc(MOQ_SITE) + 'moquil/</div></div>' };
    },
    whatsnew: function () { return { t: 'What\'s New', b: NEWS.map(function (n) { return '<div class="card pad" style="margin-bottom:10px"><div class="row"><span class="chip">' + esc(n.tag) + '</span><span class="tiny dim">' + esc(n.date) + '</span></div><div class="b" style="margin:6px 0 2px">' + esc(n.title) + '</div><div class="small dim">' + esc(n.text) + '</div></div>'; }).join('') }; },
    version: function () { return { t: 'App Version', b: '<div class="card pad"><div class="b" style="font-size:22px">Moquil ' + VERSION + '</div><div class="small dim">' + (IS_WEB ? 'Web version' : 'Android ' + (B.appVersion ? B.appVersion() : '')) + '</div></div>' }; },
    device: function () { return { t: 'Device Info', b: '<div class="card pad small" style="word-break:break-word">' + esc(B && B.deviceInfo ? B.deviceInfo() : navigator.userAgent) + '<br>Screen ' + screen.width + 'x' + screen.height + ' &middot; window ' + innerWidth + 'x' + innerHeight + ' &middot; ' + (D.documentElement.classList.contains('portrait') ? 'portrait' : 'landscape') + '</div>' }; },
    client: function () { return { t: 'Client Info', b: '<div class="card pad small" style="line-height:1.6">ID #' + (Soc.id || '') + '<br>Broker ' + esc(Soc.broker) + ' (' + (Soc.up() ? 'connected' : 'not connected') + ')<br>Friends ' + Soc.S.friends.length + ' &middot; chats ' + Soc.convs().length + ' &middot; servers ' + Soc.communities().length + '<br>Public key ' + esc((Soc.pubHex || '').slice(0, 24)) + '...</div>' }; },
    logs: function () { return { t: 'Logs', b: '<pre class="card pad tiny scroll" style="white-space:pre-wrap;max-height:60vh">' + esc((G.MOQ_LOG || ['(empty)']).join('\n')) + '</pre>' }; },
    cache: function () { return { t: 'Cache Actions', b: '<div class="card list"><div class="li" data-act="clearcache">' + ic('x') + '<div class="grow"><div>Clear cache</div><div class="tiny dim">Images and downloaded pages. You stay logged in.</div></div></div><div class="li" data-act="resync">' + ic('link') + '<div class="grow"><div>Reconnect and sync</div><div class="tiny dim">Get your friends and chats again from the backup</div></div></div><div class="li danger" data-act="wipe">' + ic('x') + '<div class="grow">Delete all local data</div></div></div>' }; }
  };

  // ---------- clicks (one handler for the whole app) ----------
  D.addEventListener('click', function (e) {
    var t = e.target, el;
    if ((el = t.closest('[data-open]'))) { e.preventDefault(); var o = el.dataset.open; openUrl(o === 'discord' ? MPAuth.DISCORD : o === 'site' ? MOQ_SITE : o); return; }
    if ((el = t.closest('[data-auth]'))) { e.preventDefault(); showAuth(el.dataset.auth); return; }
    if (!SESSION) return;
    if ((el = t.closest('[data-rate]'))) { var mv = +el.dataset.v; Soc.rate(el.dataset.rate, mv); quest('rate'); award('critic'); toast('Thanks for rating ' + mv + ' stars'); return; }
    if ((el = t.closest('[data-dl]'))) { e.stopPropagation(); var m = MOQ_MODS.filter(function (x) { return x.id === el.dataset.dl; })[0]; if (!m) return; var go2 = function () { download(m.file, m.fileName); var d = ls('moq_dl') || {}; d[m.id] = Date.now(); ls('moq_dl', d); quest('dl'); }; if (!IS_WEB && permState('storage') !== 'granted' && permState('storage') !== 'never') askPerm('storage', function () { go2(); }); else go2(); return; }
    if ((el = t.closest('[data-info]'))) { if (t.closest('.sheet')) return; modSheet(el.dataset.info); return; }
    if ((el = t.closest('[data-mod]'))) { modSheet(el.dataset.mod); return; }
    if ((el = t.closest('[data-rsvp]'))) { SV().rsvp(el.dataset.cid, el.dataset.rsvp); closeDialogs(); eventsDialog(el.dataset.cid); return; }
    if ((el = t.closest('[data-evdel]'))) { var ec = el.dataset.cid, ecf = JSON.parse(JSON.stringify(SV().cfg(ec))), eid = el.dataset.evdel; ecf.events = ecf.events.filter(function (x) { return x.id !== eid; }); SV().saveCfg(ec, ecf).then(function () { closeDialogs(); eventsDialog(ec); }); return; }
    if ((el = t.closest('[data-jump]'))) { closeDialogs(); var jm = $('#m-' + el.dataset.jump); if (jm) { jm.scrollIntoView({ block: 'center' }); jm.classList.add('flash'); setTimeout(function () { jm.classList.remove('flash'); }, 1600); } return; }
    if (t.closest('.dlg-bg') || t.closest('.sheet-bg')) return;
    if ((el = t.closest('[data-go]'))) { var pg = el.dataset.go; if (el.dataset.subto) go('settings', el.dataset.subto); else go(pg, '', el.dataset.tabto); return; }
    if ((el = t.closest('[data-tab]'))) { CUR.tab = TABS[CUR.page] = el.dataset.tab; render(); return; }
    if ((el = t.closest('[data-ftab2]'))) { FTAB = el.dataset.ftab2; render(); return; }
    if ((el = t.closest('[data-srvmenu]'))) { var smc = Soc.community(el.dataset.srvmenu); if (!smc) return; var so = smc.ownerId === Soc.id; dialog({ title: smc.name, body: '<div class="card list"><div class="li" data-invite="' + esc(smc.cid) + '">' + ic('user') + '<div class="grow b">Invite People</div></div><div class="li" data-srvev="' + esc(smc.cid) + '">' + ic('cal') + '<div class="grow b">Events</div></div>' + (srvManager(smc.cid) ? '<div class="li" data-srvset="' + esc(smc.cid) + '">' + ic('gear') + '<div class="grow b">Server Settings</div></div>' : '') + (so ? '<div class="li" data-srvicon="' + esc(smc.cid) + '">' + ic('image') + '<div class="grow b">Server Profile</div></div><div class="li" data-delcom="' + esc(smc.cid) + '" style="color:#f23f42">' + ic('x') + '<div class="grow b">Delete Server</div></div>' : (smc.joined ? '<div class="li" data-join="' + esc(smc.cid) + '" style="color:#f23f42">' + ic('out') + '<div class="grow b">Leave Server</div></div>' : '')) + '</div>', buttons: [] }); return; }
    // 4.13.18 server features
    if ((el = t.closest('[data-srvset]'))) { closeDialogs(); srvSettings(el.dataset.srvset); return; }
    if ((el = t.closest('[data-srvev]'))) { closeDialogs(); eventsDialog(el.dataset.srvev); return; }
    if ((el = t.closest('[data-srvrules]'))) { var rc = el.dataset.srvrules; dialog({ title: 'Rules', icon: 'shield', body: '<div class="small">' + md(SV().cfg(rc).rules) + '</div>', buttons: [] }); return; }
    if ((el = t.closest('[data-pins]'))) { pinsDialog(el.dataset.pins); return; }
    if ((el = t.closest('[data-poll]'))) { pollCreate(el.dataset.poll); return; }
    if ((el = t.closest('[data-chnew]'))) { e.stopPropagation(); SSTAB = 'chans'; srvSettings(el.dataset.chnew); return; }
    if ((el = t.closest('[data-mem]'))) { e.stopPropagation(); memberMenu(el.dataset.cid, el.dataset.mem); return; }
    if ((el = t.closest('[data-rx]'))) { if (!SV().react(CUR.arg, el.dataset.rx, el.dataset.e)) toast('You cannot react here right now'); refreshCom(); return; }
    if ((el = t.closest('[data-rxpick]'))) { reactPick(CUR.arg, el.dataset.rxpick); return; }
    if ((el = t.closest('[data-reply]'))) { comRe = el.dataset.reply; refreshCom(); setTimeout(function () { var i = $('#ci'); if (i) i.focus(); }, 60); return; }
    if ((el = t.closest('[data-pmore]'))) { postMenu(CUR.arg, el.dataset.pmore); return; }
    if ((el = t.closest('[data-vote]'))) { if (!SV().vote(CUR.arg, el.dataset.vote, +el.dataset.i)) toast('This poll is closed'); refreshCom(); return; }
    if ((el = t.closest('[data-ftab]'))) { friendTab = el.dataset.ftab; renderRight(); return; }
    if ((el = t.closest('[data-set]'))) { location.hash = 'settings/' + el.dataset.set; return; }
    if ((el = t.closest('[data-chat]'))) { go('chat', el.dataset.chat); return; }
    if ((el = t.closest('[data-com]'))) { comCh = 'general'; comView = 'list'; comRe = ''; go('community', el.dataset.com); return; }
    if ((el = t.closest('[data-wave]'))) { e.stopPropagation(); var wk = Soc.openDm(el.dataset.wave); if (wk) { Soc.send(wk.key || wk, '\uD83D\uDC4B'); toast('You waved'); soon(); } return; }
    if ((el = t.closest('[data-dm]'))) { e.stopPropagation(); var k = Soc.openDm(el.dataset.dm); if (k) go('chat', k.key || k); return; }
    if ((el = t.closest('[data-add]'))) { e.stopPropagation(); if (Soc.addFriend(el.dataset.add)) { quest('friend'); toast('Friend request sent'); } soon(); return; }
    if ((el = t.closest('[data-acc]'))) { e.stopPropagation(); Soc.accept(el.dataset.acc); quest('friend'); if (Soc.S.friends.length >= 5) award('friends5'); soon(); return; }
    if ((el = t.closest('[data-dec]'))) { e.stopPropagation(); Soc.decline(el.dataset.dec); soon(); return; }
    if ((el = t.closest('[data-can]'))) { e.stopPropagation(); Soc.cancel(el.dataset.can); soon(); return; }
    if ((el = t.closest('[data-rem]'))) { var rid = el.dataset.rem; dialog({ title: 'Remove friend', body: 'Remove ' + esc(Soc.nameOf(rid)) + ' from your friends? Your chat with them is deleted.', buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Remove', cls: 'red', fn: function () { Soc.removeFriend(rid); history.back(); } }] }); return; }
    if ((el = t.closest('[data-fav]'))) { Soc.toggleFav(el.dataset.fav); render(); return; }
    if ((el = t.closest('[data-user]'))) { go('user', el.dataset.user); return; }
    if ((el = t.closest('[data-pacc]'))) { Soc.acceptParty(el.dataset.pacc); soon(); return; }
    if ((el = t.closest('[data-pdec]'))) { Soc.declineParty(el.dataset.pdec); soon(); return; }
    if ((el = t.closest('[data-pinv]'))) { Soc.inviteParty(el.dataset.pinv); toast('Invite sent'); soon(); return; }
    if ((el = t.closest('[data-join]'))) { var jc = Soc.community(el.dataset.join); if (jc) { Soc.joinCommunity(jc.cid, !jc.joined); if (!jc.joined) {} else quest('com'); } render(); return; }
    if ((el = t.closest('[data-invite]'))) { var iv = el.dataset.invite, ivs = Soc.community(iv); dialog({ title: 'Invite friends to ' + (ivs ? ivs.name : 'the server'), body: '<label class="lbl">Send a server invite to a friend</label><div class="row"><input class="inp grow" readonly value="moquil://server/' + esc(iv) + '"><button class="btn go" data-cpy="moquil://server/' + esc(iv) + '">Copy</button></div><div class="tiny dim" style="margin-top:8px">They tap + Add a Server, then Join a Server, and paste it.</div>', buttons: [{ t: 'Done', cls: 'go', fn: function () {} }] }); return; }
    if ((el = t.closest('[data-cpy]'))) { copy(el.dataset.cpy); return; }
    if ((el = t.closest('.spoil'))) { el.classList.add('on'); return; }
    if ((el = t.closest('[data-srvicon]'))) { closeDialogs(); serverProfile(el.dataset.srvicon); return; }
    if ((el = t.closest('[data-delcom]'))) { var dc = el.dataset.delcom; dialog({ title: 'Delete server', body: 'Delete it for everyone? The posts go too.', buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Delete', cls: 'red', fn: function () { Soc.deleteCommunity(dc); go('social', '', 'communities'); } }] }); return; }
    if ((el = t.closest('[data-ch]'))) { comCh = el.dataset.ch; comView = 'chat'; comRe = ''; render(); return; }
    if ((el = t.closest('[data-voice]'))) { joinVoice(el.dataset.voice, el.dataset.vt); return; }
    if ((el = t.closest('[data-gadd]'))) { var gk = el.dataset.gadd, gc = Soc.conv(gk); pickFriends('Add to ' + gc.title, function (ids) { ids.forEach(function (id) { Soc.addToGroup(gk, id); }); updateMsgs(true); }, gc.members.map(function (m) { return m.id; })); return; }
    if ((el = t.closest('[data-gleave]'))) { var lk = el.dataset.gleave; dialog({ title: 'Leave group', body: 'Leave this group?', buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Leave', cls: 'red', fn: function () { Soc.leaveGroup(lk); go('social', '', 'chats'); } }] }); return; }
    if ((el = t.closest('[data-copy]'))) { copy(el.dataset.copy); return; }
    if ((el = t.closest('[data-claim]'))) { var w = wallet(), q = QUESTS.filter(function (x) { return x[0] === el.dataset.claim; })[0]; if (w.q.done[q[0]] && !w.q.claimed[q[0]]) { w.q.claimed[q[0]] = 1; w.orbs += q[2]; w.qn = (w.qn || 0) + 1; saveWallet(w); toast('+' + q[2] + ' Orbs'); if (w.qn >= 10) award('quests10'); } render(); return; }
    if ((el = t.closest('[data-claimonce]'))) { var w2 = wallet(); if (!w2.once[el.dataset.claimonce]) { w2.once[el.dataset.claimonce] = 1; w2.orbs += +el.dataset.o; saveWallet(w2); toast('+' + el.dataset.o + ' Orbs'); } render(); return; }
    if ((el = t.closest('[data-buy]'))) { buy(el.dataset.buy); return; }
    if ((el = t.closest('[data-badge]'))) { var bd = BADGES[el.dataset.badge]; toast(bd[1] + ': ' + bd[2]); return; }
    if ((el = t.closest('[data-sw]'))) { var sk = el.dataset.sw; SET[sk] = !SET[sk]; saveSet(); render(); return; }
    if ((el = t.closest('[data-theme]'))) { SET.theme = el.dataset.theme; saveSet(); render(); return; }
    if ((el = t.closest('[data-wall]'))) { SET.wall = el.dataset.wall; saveSet(); if (SET.wall === 'custom' && !SET.wallData) pickWall(); render(); return; }
    if ((el = t.closest('[data-grad]'))) { SET.wallColor = el.dataset.grad; saveSet(); render(); return; }
    if ((el = t.closest('[data-perm]'))) { askPerm(el.dataset.perm, function () { render(); }); return; }
    if ((el = t.closest('[data-act]'))) { ACT[el.dataset.act] && ACT[el.dataset.act](el); return; }
  });
  D.addEventListener('input', function (e) {
    var r = e.target.closest('[data-range]'); if (r) { SET[r.dataset.range] = +r.value; var v = $('#v_' + r.dataset.range); if (v) v.textContent = r.value + r.dataset.unit; saveSet(); return; }
    var c = e.target.closest('[data-color]'); if (c) { SET[c.dataset.color] = c.value; saveSet(); return; }
    var s = e.target.closest('[data-solid]'); if (s) { SET.wallColor = s.value; saveSet(); }
  });
  function copy(s) { try { navigator.clipboard.writeText(s).then(function () { toast('Copied'); }, function () { fallback(); }); } catch (e) { fallback(); } function fallback() { var t = D.createElement('textarea'); t.value = s; D.body.appendChild(t); t.select(); try { D.execCommand('copy'); toast('Copied'); } catch (e) {} t.remove(); } }
  function pickWall() { pickImage(1600, 1600, .75, function (u) { SET.wall = 'custom'; SET.wallData = u; try { saveSet(); } catch (e) { toast('That image is too big'); } render(); }); }
  function buy(id) {
    var it = storeItem(id); if (!it) return; var w = wallet(), p = prof();
    if (w.owned.indexOf(id) >= 0) { p[it.cat] = p[it.cat] === id ? '' : id; saveProf(p); toast(p[it.cat] ? it.name + ' is on' : it.name + ' is off'); render(); return; }
    dialog({ title: 'Buy ' + it.name, icon: 'store', body: '<div class="sitem" style="cursor:default"><div class="prev" style="height:120px">' + preview(it.cat, id) + '</div></div><p style="text-align:center;margin:6px 0 0">Price: <b>' + it.price + ' Orbs</b> &middot; You have ' + w.orbs + '</p>',
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: w.orbs >= it.price ? 'Buy and use' : 'Not enough Orbs', cls: w.orbs >= it.price ? 'go' : '', fn: function () {
        if (w.orbs < it.price) { go('store', '', 'quests'); return; }
        w.orbs -= it.price; w.owned.push(id); saveWallet(w); p[it.cat] = id; saveProf(p); if (w.owned.length >= 5) award('collector'); toast('You got ' + it.name); render();
      } }] });
  }
  function pickFriends(ttl, cb, exclude) {
    exclude = exclude || []; var fs = Soc.S.friends.filter(function (f) { return exclude.indexOf(f.id) < 0; });
    dialog({ title: ttl, icon: 'users', body: fs.length ? '<div class="list">' + fs.map(function (f) { return '<label class="li chk"><input type="checkbox" value="' + f.id + '">' + avatar(f.id, f.name, Soc.profile(f.id), 'av s') + '<span class="grow">' + esc(f.name) + '</span></label>'; }).join('') + '</div>' : '<div class="dim">No friends to add.</div>',
      buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Add', cls: 'go', fn: function (bg) { cb($$('input:checked', bg).map(function (i) { return i.value; })); } }] });
  }
  function joinVoice(room, ttl) {
    if (G.MoqVoice && MoqVoice.state.room === room) { MoqVoice.leave(); return; }
    askPerm('mic', function (ok) {
      if (!ok) { toast('Voice needs the microphone'); return; }
      MoqVoice.join(room, ttl).then(function () { quest('voice'); award('voice'); toast('Joined voice: ' + ttl); soon(); render(); }, function (e) { toast('Could not open the microphone: ' + (e && e.message || e)); });
    });
  }
  var ACT = {
    back: function () { history.back(); },
    logout: logout,
    inbox: function () { go('social', '', 'inbox'); },
    editprof: editProfile, status: statusDialog,
    addfriend: function () { prompt2('Add a friend', 'Their 8 digit ID (in the game: Social > Friends, or in Moquil: Social > Friends)', '', function (v) { v = v.replace(/\D/g, ''); if (v.length !== 8) { toast('The ID has 8 digits'); return false; } if (v === Soc.id) { toast('That is you'); return false; } Soc.watchProfile(v); if (Soc.addFriend(v)) { quest('friend'); toast('Friend request sent to #' + v); } else toast('Already a friend or sent'); soon(); }, { ph: '12345678', ok: 'Send request', max: 9 }); },
    newgroup: function () { pickFriends('New group', function (ids) { if (!ids.length) return; prompt2('Group name', 'Name (optional)', '', function (n) { var k = Soc.createGroup(n, ids); go('chat', k); }); }); },
    newcom: function () { addServer(); },
    comback: function () { comView = 'list'; render(); },
    discover: function () { go('discover'); },
    mkparty: function () { Soc.createParty(); render(); },
    leaveparty: function () { Soc.leaveParty(); render(); },
    readall: function () { Soc.markInboxRead(); render(); },
    clearinbox: function () { Soc.clearInbox(); render(); },
    vmute: function () { if (G.MoqVoice) MoqVoice.mute(); soon(); render(); },
    vdeaf: function () { if (G.MoqVoice) MoqVoice.deafen(); soon(); render(); },
    vleave: function () { if (G.MoqVoice) MoqVoice.leave(); render(); },
    emoji: function () { var i = $('#ci'); if (!i) return; var d = dialog({ title: 'Emoji', body: '<div class="row wrap">' + EMOJI.map(function (e) { return '<button class="ibtn" data-b2="' + e + '" style="font-size:22px">' + e + '</button>'; }).join('') + '</div>', onOpen: function (bg) { bg.addEventListener('click', function (ev) { var b = ev.target.closest('[data-b2]'); if (b) { i.value += b.dataset.b2; d.close(); i.focus(); } }); } }); },
    gift: function () { var c = CUR.page === 'chat' && Soc.conv(CUR.arg); if (!c || c.kind !== 0) { toast('Orb gifts work in private chats'); return; } var w = wallet(); prompt2('Send Orbs', 'How many? You have ' + w.orbs, '50', function (v) { v = parseInt(v, 10) | 0; if (v <= 0 || v > w.orbs) { toast('Not enough Orbs'); return false; } w.orbs -= v; saveWallet(w); Soc.send(c.key, '\u{1F381} sent you ' + v + ' Orbs'); updateMsgs(true); toast('Sent'); }, { ok: 'Send', max: 6 }); },
    link: function () {
      var code = ($('#lcode').value || '').trim(), s = $('#lst'); s.className = 'status'; s.textContent = 'Looking for the game...';
      Soc.linkWithCode(code).then(async function (r) {
        s.className = 'status ok'; s.textContent = 'Linked with ' + (r.name || 'the game') + ' (#' + r.id + '). Loading...';
        await Soc.saveToAccount(); await Soc.restart(SESSION.user);
        var w = wallet(); w.once.linkDone = 1; saveWallet(w); award('linked'); toast('Linked with the MP game'); render();
      }, function (e) { s.className = 'status bad'; s.textContent = e.message; });
    },
    pwd: function () { ls('moq_session', null); SESSION = null; showAuth('forgot'); },
    exportdata: function () { var o = { profile: prof(), wallet: wallet(), settings: SET, id: Soc.id, friends: Soc.S.friends, communities: Soc.communities().map(function (c) { return { cid: c.cid, name: c.name }; }) }; var b = new Blob([JSON.stringify(o, null, 1)], { type: 'application/json' }); var a = D.createElement('a'); a.href = URL.createObjectURL(b); a.download = 'moquil-data.json'; a.click(); },
    wipe: function () { dialog({ title: 'Delete local data', body: 'This logs you out and deletes everything Moquil saved on this ' + (IS_WEB ? 'browser' : 'phone') + '. Your account, ID and backup stay online.', buttons: [{ t: 'Cancel', fn: function () {} }, { t: 'Delete', cls: 'red', fn: function () { try { Soc.stop(); } catch (e) {} localStorage.clear(); location.hash = ''; location.reload(); } }] }); },
    clearcache: function () { if (B && B.clearCache) B.clearCache(); if (G.caches) caches.keys().then(function (k) { k.forEach(function (n) { caches.delete(n); }); }); toast('Cache cleared'); },
    resync: function () { Soc.restart(SESSION.user).then(function () { toast('Reconnected'); }); },
    copylogs: function () { copy('Moquil ' + VERSION + (IS_WEB ? ' web' : '') + ' ID ' + Soc.id + '\n' + (G.MOQ_LOG || []).join('\n')); },
    appset: function () { B.openAppSettings(); },
    pickwall: pickWall, nowall: function () { SET.wallData = ''; SET.wall = 'default'; saveSet(); render(); },
    ossall: function () { OSS_ALL = !OSS_ALL; render(); },
    mdhelp: function () {   // 4.13.2: the Discord formatting cheat sheet; tap a row to put it in the message box
      var ex = [['**bold**', 'Bold'], ['*italic*', 'Italic'], ['__underline__', 'Underline'], ['~~strike~~', 'Strikethrough'], ['||spoiler||', 'Spoiler (tap to show)'], ['`code`', 'Inline code'], ['```\ncode block\n```', 'Code block'], ['> quote', 'Quote'], ['# Big header', 'Header (#, ##, ###)'], ['-# small text', 'Subtext'], ['- item', 'List'], ['[MP website](https://mpsamp-open.github.io/)', 'Masked link'], ['<t:' + Math.floor(Date.now() / 1000 + 3600) + ':R>', 'Timestamp (t T d D f F s S R)'], ['<@' + (Soc.id || '12345678') + '>', 'Mention by #ID']];
      dialog({ title: 'Text Formatting', body: '<div class="small dim" style="margin:-4px 0 10px">Moquil uses the same formatting as Discord. Tap one to add it to your message.</div><div class="card list">' + ex.map(function (e, i) { return '<div class="li" data-mdx="' + i + '"><div class="grow"><div class="tiny dim">' + esc(e[1]) + '</div><code class="mdc">' + esc(e[0]) + '</code></div><div class="mdprev">' + md(e[0]) + '</div></div>'; }).join('') + '</div>',
        onOpen: function (bg) { bg.addEventListener('click', function (ev) { var r = ev.target.closest('[data-mdx]'); if (!r) return; var i = $('#ci'); if (i) { i.value = (i.value ? i.value + ' ' : '') + ex[+r.dataset.mdx][0].replace(/\n/g, ' '); i.focus(); } bg.remove(); }); }, buttons: [{ t: 'Close', fn: function () {} }] });
    },
    resetlook: function () { ['theme', 'accent', 'bg', 'glass', 'blur', 'wall', 'wallData', 'wallColor', 'dim', 'round'].forEach(function (k) { SET[k] = SET_DEF[k]; }); saveSet(); render(); },
    mictest: function () { askPerm('mic', function (ok) { if (!ok) return; navigator.mediaDevices.getUserMedia({ audio: true }).then(function (s) { var a = new Audio(); a.srcObject = s; a.play(); var v = $('#mtv'); if (v) v.textContent = 'Talk now...'; setTimeout(function () { s.getTracks().forEach(function (t) { t.stop(); }); if ($('#mtv')) $('#mtv').textContent = 'Done. Tap to test again'; }, 5000); }, function (e) { toast('Mic error: ' + e.message); }); }); }
  };
  ACT.pollmore = function () { var w = $('#pans'); if (!w || w.children.length >= 10) return; var i = D.createElement('input'); i.className = 'inp pa'; i.maxLength = 55; i.placeholder = 'Answer ' + (w.children.length + 1); i.style.marginBottom = '6px'; w.appendChild(i); i.focus(); };
  ACT.cancelre = function () { comRe = ''; refreshCom(); };

  // ---------- start ----------
  function start() {
    applyLook(); layout();
    if (!SESSION) { try { SESSION = JSON.parse(sessionStorage.getItem('moq_session') || 'null'); } catch (e) {} }
    fetch(MOQ_SITE + 'release.json', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (j) { G.MOQ_REL = j; var a = (j.assets || []).filter(function (x) { return /client\.zip$/.test(x.name); })[0]; if (a) MOQ_MODS[0].size = (a.size / 1048576).toFixed(0) + ' MB'; MOQ_MODS[0].ver = (j.tag_name || '').replace(/^v/, ''); if (SESSION && CUR.page === 'home') refresh(); }).catch(function () {});
    if (SESSION && SESSION.user) boot(); else showAuth('login');
  }
  G.MOQ = { go: go, render: render, toast: toast };
  if (D.readyState === 'loading') D.addEventListener('DOMContentLoaded', start); else start();
})(window);
