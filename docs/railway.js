// MP Railway (3.21.7): our own Railway for the MP Discord bot. This page shows the bot's live,
// signed status (the bot publishes it every 30 s on the public MQTT brokers; the signature is
// checked with the public key built into the mod, so nobody can fake "online"), the deployments
// (commits on discordpy-bot main, deployed by MP Railway within 2 min) and the MP Bot terminal
// (autobot.js types every command and presses Enter by itself).
(function () {
  'use strict';
  // ?app=NAME shows another MP Railway app (default: the Discord bot)
  var qApp = (location.search.match(/[?&]app=([a-z0-9][a-z0-9_-]{0,39})/) || [])[1];
  var APP = qApp || 'discordpy-bot', TOPIC = 'wagu/railway/' + APP + '/status';
  var PUBLIC = ['wss://broker.hivemq.com:8884/mqtt', 'wss://broker.emqx.io:8084/mqtt', 'wss://test.mosquitto.org:8081/'];
  var PUBKEY = '047bfb24d8637c172eb9353130450cca575553ef9cb1d8a9538451fa940eac0d12246f940a367ccc3f5231b61701d19c2a3d56a80eb49215ad71baf7405701e006';
  var COMMITS = 'https://api.github.com/repos/ThirtyFoxMCx00E1/discordpy-bot/commits?sha=main&per_page=8';
  var FRESH = 95000;   // 3 missed beacons = not online
  var $ = function (id) { return document.getElementById(id); };
  var out = $('out'), stick = window.MPAutoScroll(out);
  var conns = [], st = null, stAt = 0, stVerified = null, stBroker = '', events = [], evSeen = 0, wasOn = null, changed = false;
  var commits = null, lastCommits = 0, lastPing = 0, keyP = null;

  function esc(x) { return String(x).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function print(html) { var d = document.createElement('div'); d.innerHTML = html; out.appendChild(d); while (out.childNodes.length > 500) out.removeChild(out.firstChild); stick(); }
  function p(cls, t) { print('<span class="' + cls + '">' + esc(t) + '</span>'); }
  function rw(t) { print('<span class="b">[railway]</span> ' + esc(t)); }
  function now() { return new Date().toTimeString().slice(0, 8); }
  function dur(s) { s = Math.max(0, Math.round(s)); var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60); return d ? d + 'd ' + h + 'h' : h ? h + 'h ' + m + 'm' : m ? m + 'm ' + (s % 60) + 's' : s + 's'; }
  function ago(t) { return dur(Date.now() / 1000 - t) + ' ago'; }
  function hexBytes(h) { var a = new Uint8Array(h.length / 2); for (var i = 0; i < a.length; i++) a[i] = parseInt(h.substr(i * 2, 2), 16); return a; }
  function online() { return !!(st && st.online && Date.now() - stAt < FRESH && Date.now() / 1000 - (st.ts || 0) < FRESH / 1000 + 60); }

  // ------------------------------------------------------------ signature (ECDSA P-256, raw r||s)
  function key() {
    if (!keyP) keyP = (window.crypto && crypto.subtle) ? crypto.subtle.importKey('raw', hexBytes(PUBKEY), { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']) : Promise.reject(new Error('no WebCrypto'));
    return keyP;
  }
  function verify(text, sigHex) {
    if (!/^[0-9a-f]{128}$/i.test(sigHex || '')) return Promise.resolve(false);
    return key().then(function (k) {
      return crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, k, hexBytes(sigHex), new TextEncoder().encode('WAGUSTATUS1|' + text));
    }).catch(function () { return false; });
  }

  // ------------------------------------------------------------ status over MQTT
  function onStatus(url, payload) {
    var raw = payload.toString(), j;
    if (!raw) return;
    try { j = JSON.parse(raw); } catch (e) { return; }
    var host = new URL(url).hostname;
    if (j && typeof j.s === 'string') {
      verify(j.s, j.sig).then(function (ok) {
        var d; try { d = JSON.parse(j.s); } catch (e) { return; }
        if (!ok) { events.push(now() + '  ignored a status with a bad signature from ' + host); return; }
        if (st && stVerified && (d.ts || 0) < (st.ts || 0)) return;   // an older copy from another broker
        var first = !st || st.ts !== d.ts;
        st = d; stAt = Date.now(); stVerified = true; stBroker = host;
        if (first) events.push(now() + '  beacon from ' + host + ': ' + (d.online ? 'online' : 'starting') + ', ' + d.guilds + ' server(s), ping ' + d.ping + ' ms');
        render();
      });
    } else if (j && j.online === false) {   // "last will": the broker says the bot's connection is gone
      if (st && stVerified && (j.ts || 0) && (j.ts || 0) < (st.ts || 0)) return;
      if (st && stVerified && !j.ts && online()) return;   // a stale will on one broker, fresh beacon on another
      st = j; stAt = Date.now(); stVerified = false; stBroker = host;
      events.push(now() + '  ' + host + ': the bot went offline');
      render();
    }
  }
  function connect(url) {
    if (typeof mqtt === 'undefined') return;
    var c = mqtt.connect(url, { clientId: 'webrailway' + Math.random().toString(16).slice(2, 12), clean: true, connectTimeout: 8000, reconnectPeriod: 5000, keepalive: 30, protocolVersion: 4 });
    c.on('connect', function () { c.subscribe(TOPIC); render(); });
    c.on('close', render); c.on('error', function () {});
    c.on('message', function (t, pl) { onStatus(url, pl); });
    conns.push({ url: url, c: c });
  }

  // ------------------------------------------------------------ dashboard
  function set(id, v) { $(id).textContent = v; }
  function render() {
    var on = online(), brk = conns.filter(function (x) { return x.c.connected; }).length;
    if (wasOn !== null && wasOn !== on) changed = true;
    wasOn = on;
    var svc = $('svc'), cls = !st ? '' : on ? 'on' : (st.online ? 'stale' : 'off');
    svc.className = 'card svc ' + cls;
    set('svc-badge', !st ? (brk ? 'no status yet' : 'connecting') : on ? 'Online' : st.online ? 'No heartbeat' : 'Offline');
    set('svc-sub', !st ? 'MP login bot · waiting for the first status… (none = the bot never ran on MP Railway yet)'
      : on ? 'Active · ' + (st.host || 'MP Railway') + ' · last heartbeat ' + ago(st.ts)
      : 'Last seen ' + (st.ts ? ago(st.ts) : 'unknown') + ' · ' + (st.host || 'MP Railway'));
    if (st) {
      set('k-bot', st.bot || '-'); set('k-ver', st.ver ? 'bot ' + st.ver : '-'); set('k-commit', st.commit || '-');
      set('k-up', on && st.started ? dur(st.ts - st.started) : '-'); set('k-guilds', st.guilds != null ? st.guilds : '-');
      set('k-acc', st.accounts != null ? st.accounts : '-'); set('k-ping', st.ping >= 0 ? st.ping + ' ms' : '-'); set('k-brk', st.brokers != null ? st.brokers + ' / 3' : '-');
      $('k-cmds').innerHTML = (st.cmds || []).map(function (c) { return '<span>' + esc(c) + '</span>'; }).join('') + (st.login === false ? '<span>login key missing</span>' : '');
    }
    var sig = $('k-sig');
    sig.className = 'sig ' + (stVerified ? 'ok' : st ? 'bad' : '');
    sig.textContent = stVerified ? 'signature: valid (mod public key 047bfb24…) via ' + stBroker : st ? 'signature: none (offline notice from ' + stBroker + ')' : 'signature: -';
    $('live').className = 'live ' + (on ? 'ok' : st ? 'bad' : brk ? 'warn' : 'bad');
    set('live-t', (brk ? brk + ' broker' + (brk === 1 ? '' : 's') : 'connecting…') + ' · bot ' + (on ? 'online' : st ? 'offline' : 'not seen'));
    renderDeps();
  }
  function renderDeps() {
    if (!commits) return;
    var cur = st && st.commit;
    $('deps').innerHTML = commits.map(function (c) {
      var sha = c.sha.slice(0, 7), msg = (c.commit.message || '').split('\n')[0], when = (c.commit.committer && c.commit.committer.date || '').slice(0, 10);
      return '<li' + (cur && sha === cur ? ' class="cur"' : '') + '><span class="sha">' + esc(sha) + '</span><span class="msg" title="' + esc(msg) + '">' + esc(msg) + '</span><span class="when">' + esc(when) + '</span></li>';
    }).join('') || '<li class="m">no commits</li>';
  }
  function loadCommits() {
    lastCommits = Date.now();
    return fetch(COMMITS, { headers: { Accept: 'application/vnd.github+json' } }).then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .then(function (j) { commits = j; renderDeps(); return j; });
  }
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

  // ------------------------------------------------------------ commands
  var CMDS = {
    'version': function () {
      print('<span class="g">MP Railway 3.21.7</span> <span class="m">(web view)</span> - our own Railway: git push -> build -> restart, 24/7');
      p('m', 'runs on the HiveMQ server (systemd) or a Termux phone · no cloud account · secrets stay on the server');
    },
    'status': function () {
      if (!st) { p('y', APP + '  no status yet'); p('m', '  the bot has not published a status on any broker - it never ran on MP Railway, or it is starting'); return; }
      var on = online();
      print(esc(APP) + '  ' + (on ? '<span class="g">online</span>' : '<span class="r">offline</span>') + '  <span class="m">bot ' + esc(st.ver || '?') + ' · commit ' + esc(st.commit || '?') + ' · ' + esc(st.host || 'MP Railway') + '</span>');
      if (on) {
        print('  Logged in as ' + esc(st.bot) + ', in ' + st.guilds + ' server(s) · uptime ' + dur(st.ts - st.started) + ' · gateway ' + st.ping + ' ms');
        print('  Slash commands: <span class="c">' + esc((st.cmds || []).join(', ')) + '</span>' + (st.login ? '' : '  <span class="r">(AUTH_PRIVATE_KEY missing: /gen is off)</span>'));
        print('  ' + st.accounts + ' account(s) · ' + st.brokers + '/3 MQTT brokers · Python ' + esc(st.py || '?'));
      } else p('m', '  last heartbeat ' + (st.ts ? ago(st.ts) : 'unknown') + ' - run "mp-railway doctor"');
      p(stVerified ? 'g' : 'm', '  ' + $('k-sig').textContent);
    },
    'logs': function () {
      var fresh = events.slice(evSeen); evSeen = events.length;
      if (!fresh.length) { p('m', '(no new events - the next heartbeat comes within 30 s)'); return; }
      fresh.slice(-20).forEach(function (e) { print('<span class="m">' + esc(e.slice(0, 8)) + '</span>' + esc(e.slice(8))); });
    },
    'deployments': function () {
      return loadCommits().then(function (j) {
        p('m', 'discordpy-bot main - every push is deployed by MP Railway within 2 minutes:');
        j.slice(0, 6).forEach(function (c) {
          var sha = c.sha.slice(0, 7), live = st && st.commit === sha;
          print('  <span class="b">' + sha + '</span> ' + esc((c.commit.message || '').split('\n')[0].slice(0, 90)) + ' <span class="m">' + esc((c.commit.committer.date || '').slice(0, 16).replace('T', ' ')) + '</span>' + (live ? ' <span class="g">← live</span>' : ''));
        });
      }).catch(function (e) { p('r', 'gh: ' + e.message + ' (GitHub API limit? tries again later)'); });
    },
    'variables': function () {
      print('  DISCORD_TOKEN      <span class="m">******** (on the server only: /etc/mp-railway/discordpy-bot.env, mode 600)</span>');
      print('  AUTH_PRIVATE_KEY   <span class="m">******** </span>' + (st && stVerified ? (st.login ? '<span class="g">set - matches the mod key 047bfb24…</span>' : '<span class="r">missing</span>') : '<span class="m">(shown when the bot is online)</span>'));
      print('  VERIFY_CHANNEL     verification');
      print('  DATA_DIR           /var/lib/mp-railway/discordpy-bot  <span class="m">(accounts.db)</span>');
      p('m', '  change on the server: sudo mp-railway env set DISCORD_TOKEN   (asks, hidden, restarts the bot)');
    },
    'commands': function () {
      var c = st && st.cmds && st.cmds.length ? st.cmds : ['/gen', '/stats', '/char'];
      var info = { '/gen': 'only in #verification: links the game code to your Discord', '/stats': 'your MP account card (only you see it)', '/char': 'your character card (only you see it)' };
      c.forEach(function (x) { print('  <span class="c">' + esc(x) + '</span>  <span class="m">' + esc(info[x] || '') + '</span>'); });
      if (!online()) p('y', '  Discord only shows these while the bot is online and synced (bot offline now)');
    },
    'ping': function () {
      lastPing = Date.now();
      return PUBLIC.reduce(function (pr, u) {
        return pr.then(function () { return wsPing(u).then(function (a) {
          var h = new URL(u).hostname;
          print('PING ' + esc(h) + ': ' + (a < 0 ? '<span class="r">no answer</span>' : 'time=' + a + ' ms'));
        }); });
      }, Promise.resolve());
    },
    'doctor': function () {
      if (online()) { p('g', 'all good: the bot is online and the commands are synced - nothing to fix'); return; }
      p('y', 'the bot is not online. On the HiveMQ server (Termius console r) MP Railway, or SSH):');
      print('  1. <span class="g">sudo mp-railway status</span>     <span class="m">online / crashing / offline?</span>');
      print('  2. <span class="g">sudo mp-railway logs</span>       <span class="m">"Improper token" = new token: sudo mp-railway env set DISCORD_TOKEN</span>');
      print('  3. <span class="g">sudo mp-railway restart</span>');
      print('  4. <span class="g">sudo mp-railway setup</span>      <span class="m">first time on this server (ONE server only)</span>');
      p('m', 'not installed yet? curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/railway/install.sh | sudo bash');
    },
    'clear': function () { out.innerHTML = ''; }
  };
  var ALIAS = {
    'mp-railway --version': 'version', 'mp-railway status': 'status', 'mp-railway logs --tail 20': 'logs',
    'mp-railway deployments': 'deployments', 'mp-railway env': 'variables', 'mp-railway commands': 'commands',
    'ping -c 1 $BROKERS': 'ping', 'mp-railway doctor': 'doctor', 'clear': 'clear'
  };
  function run(line) {
    print('<span class="b">mp@railway:~$</span> ' + esc(line));
    var deb = window.MPDebian && MPDebian.match(line);
    if (deb) { MPDebian.lines(deb).forEach(function (l) { p(l[0], l[1]); }); return; }   // 3.31
    var c = ALIAS[line];
    if (!c || !CMDS[c]) { p('r', line.split(' ')[0] + ': command not found'); return; }
    return CMDS[c]();
  }

  // ------------------------------------------------------------ MP Bot planner
  var boot = ['mp-railway --version', 'mp-railway deployments', 'mp-railway env', 'mp-railway commands', 'mp-railway status'];
  var rot = ['mp-railway status', 'mp-railway logs --tail 20', 'mp-railway status', 'mp-railway commands'], ri = 0, cycles = 0;
  function next() {
    if (boot.length) return { cmd: boot.shift(), why: 'start-up check' };
    var dj = window.MPDebian && MPDebian.tick(); if (dj) return dj;   // 3.31: the Debian one-line setup
    if (changed) { changed = false; return online() ? { cmd: 'mp-railway status', why: 'the bot came online' } : { cmd: 'mp-railway doctor', why: 'the bot went offline' }; }
    if (events.length - evSeen >= 2) return { cmd: 'mp-railway logs --tail 20', why: (events.length - evSeen) + ' new events' };
    if (Date.now() - lastPing > 120000) return { cmd: 'ping -c 1 $BROKERS', why: 'broker ping every 2 min', wait: 7000 };
    if (Date.now() - lastCommits > 300000) return { cmd: 'mp-railway deployments', why: 'checking for a new deployment' };
    if (++cycles % 12 === 0) return { cmd: 'clear', why: 'keeping the screen short', wait: 600 };
    if (!online() && cycles % 4 === 0) return { cmd: 'mp-railway doctor', why: 'bot still offline' };
    return { cmd: rot[ri++ % rot.length], why: 'routine check' };
  }

  // ------------------------------------------------------------ start
  print('<span class="b">MP Railway</span>  <span class="m">our own Railway · the MP Discord bot 24/7</span>');
  print('<span class="m">Bot:   </span>MP Bot types and presses Enter by itself - no typing');
  print('');
  PUBLIC.forEach(connect);
  render();
  loadCommits().catch(function () {});
  MPBot({ out: out, line: $('typed'), keyEnter: $('kenter'), status: $('botst'), run: run, next: next, startMs: 1600 });
  setInterval(render, 5000);
})();
