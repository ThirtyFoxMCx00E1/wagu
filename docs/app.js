// MP Multiplayer website: live server list + region ping. Read-only, no secrets.
(function () {
  'use strict';
  var REG = ['Asia', 'Europe', 'NA', 'Oceania'];
  var PUBLIC = ['wss://broker.hivemq.com:8884/mqtt', 'wss://broker.emqx.io:8084/mqtt', 'wss://test.mosquitto.org:8081/'];
  var TOPIC = 'thirtyfox/mp5/l/+';   // 3.04.6: lobby of MP 3.04.6+ (mp4 = old versions)
  var FRESH = 15000;
  var $ = function (id) { return document.getElementById(id); };
  var servers = new Map(), regions = [null, null, null, null], ping = [null, null, null, null];
  var filter = -1, client = null, lobbyName = '';

  function esc(x) { return String(x).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }

  // ---- regions.txt (same format the mod reads) ----
  function parseRegions(txt) {
    var cur = -2;
    txt.split(/\r?\n/).forEach(function (raw) {
      var line = raw.replace(/#.*/, '').trim();
      if (!line) return;
      var m = line.match(/^\[(.+)\]$/);
      if (m) {
        var n = m[1].trim().toLowerCase();
        cur = REG.findIndex(function (r) { return r.toLowerCase() === n; });
        if (n === 'north america' || n === 'us') cur = 2;
        if (n === 'eu') cur = 1;
        if (n === 'au' || n === 'oc') cur = 3;
        if (cur >= 0 && !regions[cur]) regions[cur] = {};
        return;
      }
      if (cur < 0) return;
      var eq = line.indexOf('=');
      if (eq < 0) return;
      regions[cur][line.slice(0, eq).trim().toLowerCase()] = line.slice(eq + 1).trim();
    });
    regions = regions.map(function (r) { return r && (r.host || r.web) ? r : null; });
  }

  function okWss(u) { return /^wss:\/\/[^\s/]+/i.test(u || ''); }

  // ---- ping: time to open a secure WebSocket to the region's broker ----
  function wsPing(url) {
    return new Promise(function (res) {
      var t0 = performance.now(), done = false, ws;
      var fin = function (v) { if (done) return; done = true; try { ws.close(); } catch (e) {} res(v); };
      try { ws = new WebSocket(url, 'mqtt'); } catch (e) { return res(-1); }
      ws.onopen = function () { fin(Math.round(performance.now() - t0)); };
      ws.onerror = function () { fin(-1); };
      setTimeout(function () { fin(-1); }, 6000);
    });
  }
  function measure() {
    return Promise.all(regions.map(function (r, i) {
      if (!r || !okWss(r.web)) return Promise.resolve();
      return wsPing(r.web).then(function (a) {
        return wsPing(r.web).then(function (b) {
          var v = [a, b].filter(function (x) { return x >= 0; });
          ping[i] = v.length ? Math.min.apply(null, v) : -1;
        });
      });
    })).then(renderRegions);
  }
  function bestRegion() {
    var b = -1;
    ping.forEach(function (p, i) { if (p != null && p >= 0 && (b < 0 || p < ping[b])) b = i; });
    return b;
  }

  // ---- lobby ----
  function setStatus(cls, html) {
    var el = $('lobby-status');
    el.className = 'status ' + cls;
    el.innerHTML = '<span class="dot"></span>' + html;
  }
  function brokerList() {
    var list = [], b = bestRegion();
    if (b >= 0) list.push({ url: regions[b].web, user: regions[b].web_user, pass: regions[b].web_pass, label: REG[b] + ' server' });
    regions.forEach(function (r, i) {
      if (r && okWss(r.web) && i !== b) list.push({ url: r.web, user: r.web_user, pass: r.web_pass, label: REG[i] + ' server' });
    });
    PUBLIC.forEach(function (u) { list.push({ url: u, label: 'public lobby (' + new URL(u).hostname + ')' }); });
    return list;
  }
  var bi = 0, list = [];
  function connect() {
    if (typeof mqtt === 'undefined') { setStatus('bad', 'Live list could not load. Reload the page.'); return; }
    var br = list[bi % list.length];
    setStatus('', 'Connecting to the ' + esc(br.label) + '…');
    var opts = { clientId: 'web' + Math.random().toString(16).slice(2, 12), clean: true, connectTimeout: 8000, reconnectPeriod: 0, protocolVersion: 4 };
    if (br.user) { opts.username = br.user; opts.password = br.pass || ''; }
    var c = mqtt.connect(br.url, opts), failed = false;
    client = c;
    c.on('connect', function () {
      lobbyName = br.label;
      setStatus('ok', 'Live via the ' + esc(br.label));
      c.subscribe(TOPIC);
    });
    c.on('message', function (topic, payload) {
      var tok = topic.split('/').pop(), p = payload.toString();
      if (!p) { servers.delete(tok); render(); return; }
      var f = p.split('|');
      if (f[0] !== 'v1' || f.length < 7) return;
      var name = f.slice(6).join('|').slice(0, 64);
      servers.set(tok, { port: +f[1] || 0, players: +f[2] || 0, max: +f[3] || 0, region: +f[4], name: name, seen: Date.now() });
      render();
    });
    var fail = function () {
      if (failed) return;
      failed = true;
      try { c.end(true); } catch (e) {}
      bi++;
      setStatus('bad', 'Lobby unreachable, trying the next one…');
      setTimeout(connect, 2000);
    };
    c.on('error', fail);
    c.on('close', function () { if (c === client) fail(); });
  }

  // ---- render ----
  function isDefault(s) { return s.port === 1922 && s.name.toLowerCase() === 'mp lan server'; }
  function isBcw(s) { return s.port === 1930 && /^brief case war$/i.test(s.name || ''); }
  function isMw3(s) { return /^(call of duty: )?modern warfare 3$|^mw3$/i.test(s.name || ''); }   // 3.23
  function isRp(s) { return /^mega vil futuristic$/i.test((s.name || '').trim()); }   // 3.25: Mega Vil Futuristic - sky blue
  function render() {
    var now = Date.now();
    servers.forEach(function (s, k) { if (now - s.seen > FRESH) servers.delete(k); });
    var all = Array.from(servers.values());
    var players = all.reduce(function (a, s) { return a + s.players; }, 0);
    $('st-servers').textContent = all.length;
    $('st-players').textContent = players;
    var rows = all.filter(function (s) { return filter < 0 || s.region === filter; })
      .sort(function (a, b) { return (isDefault(b) - isDefault(a)) || (isBcw(b) - isBcw(a)) || (isMw3(b) - isMw3(a)) || (isRp(b) - isRp(a)) || (b.players - a.players) || a.name.localeCompare(b.name); });
    var tb = $('server-rows');
    if (!rows.length) {
      var msg = filter < 0 ? 'No servers online right now. Host one from the game and it shows up here.' : 'No servers in ' + REG[filter] + ' right now.';
      tb.innerHTML = client && client.connected ? '<tr class="empty"><td colspan="4">' + esc(msg) + '</td></tr>' : tb.innerHTML;
    } else {
      tb.innerHTML = rows.map(function (s) {
        var full = s.max && s.players >= s.max;
        var bcw = isBcw(s);   // 3.04: Brief Case War - red banner + briefcase
        var mw3 = !bcw && isMw3(s);   // 3.23: Modern Warfare 3 - green banner + guns
        var rp = !bcw && !mw3 && isRp(s);   // 3.25: Mega Vil Futuristic - sky blue banner + RP icon
        return '<tr' + (bcw ? ' class="bcw"' : mw3 ? ' class="mw3"' : rp ? ' class="rp"' : '') + '><td>' + (bcw ? '<img class="bcw-ic" src="bcw_icon.png" alt="">' : mw3 ? '<img class="bcw-ic" src="mw3_icon.png" alt="">' : rp ? '<img class="bcw-ic" src="rp_icon.png" alt="">' : '') + '<span class="sname">' + esc(s.name || 'Unnamed server') + '</span><span class="ssub">Port ' + s.port + (bcw ? ' &middot; 10 vs 10 &middot; private gamemode' : mw3 ? ' &middot; 8 teams &middot; 24/7 featured' : rp ? ' &middot; Roleplay RP &middot; 24/7 featured' : '') + '</span></td>' +
          '<td>' + esc(REG[s.region] || 'Unknown') + '</td>' +
          '<td class="num' + (full ? ' full' : '') + '">' + s.players + ' / ' + s.max + '</td>' +
          '<td>' + (bcw ? '<span class="tag bcw">Brief Case War 24/7</span>' : mw3 ? '<span class="tag mw3">MW3 24/7</span>' : rp ? '<span class="tag rp">Roleplay 24/7</span>' : isDefault(s) ? '<span class="tag def">Default 24/7</span>' : '<span class="tag">Player hosted</span>') + '</td></tr>';
      }).join('');
    }
    renderRegions();
  }
  function renderRegions() {
    var counts = [0, 0, 0, 0];
    servers.forEach(function (s) { if (s.region >= 0 && s.region < 4) counts[s.region]++; });
    var b = bestRegion();
    $('region-cards').innerHTML = REG.map(function (name, i) {
      var r = regions[i], p = ping[i], state, cls, big;
      if (!r) { cls = 'none'; state = 'No server yet, uses the public lobby'; big = '<span class="rping">–</span>'; }
      else if (!okWss(r.web)) { cls = 'ok'; state = 'Set up (no web endpoint for ping)'; big = '<span class="rping">–</span>'; }
      else if (p == null) { cls = ''; state = 'Measuring…'; big = '<span class="rping">…</span>'; }
      else if (p < 0) { cls = 'bad'; state = 'Not reachable'; big = '<span class="rping">–</span>'; }
      else { cls = 'ok'; state = 'Online'; big = '<span class="rping">' + p + '<small>ms</small></span>'; }
      return '<article class="rcard' + (i === b ? ' best' : '') + '">' +
        (i === b ? '<span class="rbest">Best for you</span>' : '') +
        '<span class="rname">' + name + '</span>' +
        '<span class="rstate ' + cls + '"><span class="dot"></span>' + esc(state) + '</span>' +
        '<span class="ssub">' + counts[i] + ' server' + (counts[i] === 1 ? '' : 's') + ' listed</span>' + big + '</article>';
    }).join('');
  }

  // ---- filters ----
  document.querySelectorAll('.chip').forEach(function (btn) {
    btn.addEventListener('click', function () {
      filter = +btn.getAttribute('data-r');
      document.querySelectorAll('.chip').forEach(function (x) { x.classList.toggle('on', x === btn); x.setAttribute('aria-pressed', x === btn); });
      render();
    });
  });

  // ---- latest release ----
  fetch('release.json', { cache: 'no-cache' })   // 3.35: openmpsa.com - the release info lives on this site
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (j) {
      if (!j || !/^v?\d/i.test(j.tag_name || '')) return;
      var v = String(j.tag_name).replace(/^v/i, '').slice(0, 12);
      $('st-version').textContent = v;
      if ($('dl-ver')) $('dl-ver').textContent = v;
    }).catch(function () {});

  // ---- start ----
  renderRegions();
  fetch('regions.txt', { cache: 'no-cache' })
    .then(function (r) { return r.ok ? r.text() : ''; })
    .catch(function () { return ''; })
    .then(function (t) { parseRegions(t || ''); renderRegions(); return measure(); })
    .then(function () { list = brokerList(); connect(); });
  setInterval(render, 3000);
  setInterval(measure, 60000);

  // 3.04.6: version history (versions.json is made from the README changelog).
  // 3.10 / 3.11: every version in "supported" (3.11 + 3.10 + 3.04.6, they play together) gets a download button;
  // older ones allowed cheats and every server refuses them.
  function md(t) {
    return esc(t).replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>').replace(/`([^`]+)`/g, '<code>$1</code>');
  }
  function loadVersions() {
    var box = $('v-list'); if (!box) return;
    fetch('versions.json', { cache: 'no-cache' }).then(function (r) { return r.json(); }).then(function (j) {
      var latest = j.latest; if ($('v-latest')) $('v-latest').textContent = latest;
      var SHOW = 12;
      box.innerHTML = j.versions.map(function (v, i) {
        var top = i === 0;
        var ok = top || (j.supported || []).indexOf(v.v) >= 0;
        var full = v.summary || '';
        var m = full.match(/^(.{20,160}?[.!?])(\s|$)/);   // collapsed line: first sentence only
        var short = m ? m[1] : (full.length > 160 ? full.slice(0, 157) + '...' : full);
        var rest = full.length > short.length ? '<p>' + md(full) + '</p>' : '';
        var items = rest + (v.items.length ? '<ul>' + v.items.map(function (x) { return '<li>' + md(x) + '</li>'; }).join('') + '</ul>' : '');
        var dl = ok
          ? (top ? '<p class="vdl"><a class="btn btn-main" href="downloads/MP-client.zip">Download MP ' + esc(v.v) + '</a></p>'
                 : '<p class="vnodl">Plays with ' + esc(latest) + '. Only the latest version is downloadable: <a class="link" href="#download">download MP ' + esc(latest) + '</a>.</p>')
          : '<p class="vnodl">No download: this version is shut out of every server. Download MP ' + esc(latest) + ' instead.</p>';
        return '<details class="vcard' + (top ? ' latest' : '') + (i >= SHOW ? ' vmore' : '') + '"' + (top ? ' open' : '') + (i >= SHOW ? ' hidden' : '') + '><summary><span class="vnum">' + esc(v.v) + '</span>' +
          '<span class="vsum">' + md(short) + '</span>' +
          (top ? '<span class="vtag ok">Latest</span>' : ok ? '<span class="vtag ok">Supported - plays with ' + esc(latest) + '</span>' : '<span class="vtag no">Blocked</span>') +
          '</summary><div class="vbody">' + (ok ? dl + items : items + dl) + '</div></details>';
      }).join('') + (j.versions.length > SHOW ? '<button type="button" class="btn btn-ghost vall" id="v-all">Show all ' + j.versions.length + ' versions</button>' : '');
      var all = $('v-all');
      if (all) all.addEventListener('click', function () {
        Array.prototype.forEach.call(box.querySelectorAll('.vmore'), function (d) { d.hidden = false; });
        all.remove();
      });
    }).catch(function () { box.innerHTML = '<p class="status">Could not load the version history.</p>'; });
  }
  loadVersions();
})();
