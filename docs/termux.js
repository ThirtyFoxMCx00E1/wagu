// MP Termux (2.22): Termux in the browser for the 24/7 phone server. Nobody types: the MP Bot
// (autobot.js) runs every command by itself and reacts to the live lobby. Everything shown is
// real: install.sh + server.conf from GitHub, broker.txt + regions.txt from this site, the live
// server list over MQTT/WebSocket, WebSocket ping, and the latest GitHub release.
(function () {
  'use strict';
  var REG = ['Asia', 'Europe', 'NA', 'Oceania'];
  var PUBLIC = ['wss://broker.hivemq.com:8884/mqtt', 'wss://broker.emqx.io:8084/mqtt', 'wss://test.mosquitto.org:8081/'];
  var ROOT = 'thirtyfox/mp5/', FRESH = 15000;
  var RAW = 'https://thirtyfoxmcx00e1.github.io/openmp.com/';   // 3.35: openmp.com (no repo links)
  var API = 'release.json';
  var $ = function (id) { return document.getElementById(id); };
  var out = $('out'), stick = window.MPAutoScroll(out);
  var servers = new Map(), events = [], evSeen = 0, conns = [], brokers = [], regionsTxt = '', installSh = '';
  var phoneSeen = null, phoneWas = null, phoneChanged = false, lastPing = 0, release = null;

  function esc(x) { return String(x).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function print(html) { var d = document.createElement('div'); d.innerHTML = html; out.appendChild(d); while (out.childNodes.length > 500) out.removeChild(out.firstChild); stick(); }
  function p(cls, t) { print('<span class="' + cls + '">' + esc(t) + '</span>'); }
  function mp(t) { print('<span class="c">[mp]</span> ' + esc(t)); }
  function ago(ms) { var s = Math.round(ms / 1000); return s < 60 ? s + 's' : Math.round(s / 60) + 'm'; }
  function now() { return new Date().toTimeString().slice(0, 8); }
  function getText(url) { return fetch(url, { cache: 'no-cache' }).then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.text(); }); }
  function cfgLines(txt) { return txt.split(/\r?\n/).filter(function (l) { return l.trim() && !/^\s*[#;]/.test(l); }); }

  // ------------------------------------------------------------ lobby (MQTT over WebSocket)
  function isPhoneServer(s) { return s.port === 1922 && /mp lan server/i.test(s.name); }
  function onList(topic, payload) {
    var tok = topic.split('/').pop(), s = payload.toString(), had = servers.get(tok);
    if (!s) { if (had) events.push(now() + '  server closed: ' + had.name); servers.delete(tok); return; }
    var f = s.split('|');
    if (f[0] !== 'v1' || f.length < 7) return;
    var r = +f[4], sv = { port: +f[1] || 0, players: +f[2] || 0, max: +f[3] || 0, region: isNaN(r) ? -1 : r, name: f.slice(6).join('|').slice(0, 64), seen: Date.now() };
    if (!had) events.push(now() + '  server up: ' + sv.name + ' (port ' + sv.port + ', ' + sv.players + '/' + sv.max + ')');
    else if (had.players !== sv.players) events.push(now() + '  ' + sv.name + ': players ' + had.players + ' -> ' + sv.players);
    servers.set(tok, sv);
  }
  function prune() {
    var t = Date.now();
    servers.forEach(function (s, k) { if (t - s.seen > FRESH) { events.push(now() + '  server timed out: ' + s.name); servers.delete(k); } });
    var ph = null; servers.forEach(function (s) { if (isPhoneServer(s)) ph = s; });
    var up = !!ph;
    if (phoneWas !== null && phoneWas !== up) phoneChanged = true;
    phoneWas = up; phoneSeen = ph;
    while (events.length > 300) { events.shift(); evSeen = Math.max(0, evSeen - 1); }
  }
  function connect(url, user, pass) {
    if (typeof mqtt === 'undefined' || brokers.indexOf(url) >= 0) return;
    brokers.push(url);
    var o = { clientId: 'webtermux' + Math.random().toString(16).slice(2, 12), clean: true, connectTimeout: 8000, reconnectPeriod: 4000, keepalive: 30, protocolVersion: 4 };
    if (user) { o.username = user; o.password = pass || ''; }
    var c = mqtt.connect(url, o);
    c.on('connect', function () { c.subscribe(ROOT + 'l/+'); live(); });
    c.on('close', live); c.on('error', function () {});
    c.on('message', function (t, pl) { onList(t, pl); });
    conns.push({ url: url, c: c });
  }
  function live() {
    prune();
    var on = conns.filter(function (x) { return x.c.connected; }).length, players = 0;
    servers.forEach(function (s) { players += s.players; });
    $('live').className = 'live ' + (on ? (phoneSeen ? 'ok' : 'warn') : 'bad');
    $('live-t').textContent = (on ? on + ' broker' + (on === 1 ? '' : 's') : 'connecting…') + ' · ' + servers.size + ' servers · ' + players + ' players · 24/7 phone ' + (phoneSeen ? 'up' : 'not seen');
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
    'termux-info': function () {
      var ua = navigator.userAgent, os = (ua.match(/Android [\d.]+|iPhone OS [\d_]+|Windows NT [\d.]+|Mac OS X [\d_]+|Linux/) || ['unknown'])[0].replace(/_/g, '.');
      print('<span class="g">MP Termux 2.22</span> <span class="m">(web)</span>');
      print('OS        ' + esc(os));
      print('Screen    ' + screen.width + 'x' + screen.height + ' @' + (window.devicePixelRatio || 1) + 'x');
      print('Time zone ' + esc(Intl.DateTimeFormat().resolvedOptions().timeZone || '?'));
      print('Network   ' + (navigator.onLine ? '<span class="g">online</span>' : '<span class="r">offline</span>'));
      p('m', 'This terminal runs in the browser. The MP Bot types every command and presses Enter by itself.');
    },
    'install': function () {
      return (installSh ? Promise.resolve(installSh) : getText('install.sh').catch(function () { return getText(RAW + 'install.sh'); }).then(function (t) { installSh = t; return t; })).then(function (t) {
        var steps = t.split('\n').map(function (l) { var m = l.match(/^\s*say\s+"([^"]+)"/); return m ? m[1] : null; }).filter(Boolean);
        p('m', 'termux/install.sh - what it does on the phone (' + steps.length + ' steps):');
        steps.forEach(function (s) { mp(s.replace(/\$\w+/g, '…')); });
        p('m', 'commands afterwards: mp-server status | log | restart | stop | start | config | update');
      }).catch(function (e) { p('r', 'curl: (6) could not fetch install.sh: ' + e.message); });
    },
    'cat-conf': function () {
      return (installSh ? Promise.resolve(installSh) : getText('install.sh').catch(function () { return getText(RAW + 'install.sh'); }).then(function (t) { installSh = t; return t; })).then(function (t) {
        var m = t.match(/cat > "\$CONF" <<'EOF'\n([\s\S]*?)\nEOF/);
        if (!m) { p('r', 'cat: server.conf: template not found'); return; }
        m[1].split('\n').forEach(function (l) { print(/^\s*#/.test(l) ? '<span class="m">' + esc(l) + '</span>' : esc(l)); });
      }).catch(function (e) { p('r', 'cat: ' + e.message); });
    },
    'cat-broker': function () {
      return getText('broker.txt').then(function (t) { cfgLines(t).forEach(function (l) { print(esc(l)); }); p('m', '(comments hidden · full file: broker.txt)'); })
        .catch(function (e) { p('r', 'cat: broker.txt: ' + e.message); });
    },
    'cat-regions': function () {
      return (regionsTxt ? Promise.resolve(regionsTxt) : getText('regions.txt')).then(function (t) {
        var l = cfgLines(t);
        if (!l.length) { p('m', '(no region yet - public lobby)'); return; }
        l.forEach(function (x) { print(/^\[/.test(x) ? '<span class="g">' + esc(x) + '</span>' : esc(x)); });
      }).catch(function (e) { p('r', 'cat: regions.txt: ' + e.message); });
    },
    'status': function () {
      prune();
      if (phoneSeen) {
        print('<span class="g">MP server running (24/7)</span>  ' + esc(phoneSeen.name) + ' · port ' + phoneSeen.port + ' · ' + phoneSeen.players + '/' + phoneSeen.max + ' players · ' + esc(REG[phoneSeen.region] || '?') + ' · seen ' + ago(Date.now() - phoneSeen.seen) + ' ago');
      } else {
        print('<span class="y">MP server not seen in the lobby</span> <span class="m">(looking for "MP LAN Server" on port 1922)</span>');
      }
      var on = conns.filter(function (x) { return x.c.connected; }).map(function (x) { return new URL(x.url).hostname; });
      p('m', 'lobby via ' + (on.length ? on.join(', ') : 'nothing yet') + ' · ' + servers.size + ' server(s) listed');
    },
    'list': function () {
      prune();
      var all = Array.from(servers.values()).sort(function (a, b) { return b.players - a.players || a.name.localeCompare(b.name); });
      if (!all.length) { p('m', 'no servers online right now'); return; }
      all.forEach(function (s) { print('<span class="' + (s.max && s.players >= s.max ? 'r' : 'g') + '">' + (s.players + '/' + s.max + '      ').slice(0, 8) + '</span>' + esc(s.name || 'Unnamed') + ' <span class="m">· ' + esc(REG[s.region] || '?') + ' · port ' + s.port + '</span>' + (isPhoneServer(s) ? ' <span class="c">24/7</span>' : '')); });
    },
    'log': function () {
      prune();
      var fresh = events.slice(evSeen); evSeen = events.length;
      if (!fresh.length) { p('m', '(no new lobby events)'); return; }
      fresh.slice(-25).forEach(function (e) { print('<span class="m">' + esc(e.slice(0, 8)) + '</span>' + esc(e.slice(8))); });
    },
    'ping': function () {
      lastPing = Date.now();
      var list = brokers.length ? brokers : PUBLIC;
      return list.reduce(function (pr, u) {
        return pr.then(function () {
          return wsPing(u).then(function (a) { return wsPing(u).then(function (b) {
            var h = new URL(u).hostname, v = [a, b].filter(function (x) { return x >= 0; });
            if (!v.length) print('PING ' + esc(h) + ': <span class="r">no answer</span>');
            else print('PING ' + esc(h) + ': ' + v.map(function (x) { return 'time=' + x + ' ms'; }).join('  ') + '  <span class="g">min ' + Math.min.apply(null, v) + ' ms</span>');
          }); });
        });
      }, Promise.resolve());
    },
    'release': function () {
      var api = function () { return fetch(API, { cache: 'no-cache' }).then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); }); };
      // release.json is written next to this page on every release (works while the repo is private)
      return getText('release.json').then(function (t) { return JSON.parse(t); }).catch(api).then(function (j) {
        release = j;
        if (j.packages) j.packages.forEach(function (pk) { (pk.assets || []).forEach(function (a) { j.assets = (j.assets || []).concat([{ name: a.name + '  [' + pk.tag + ']', size: a.size, download_count: a.download_count }]); }); });
        print('<span class="g">' + esc(j.tag_name) + '</span>  ' + esc(j.name || '') + ' <span class="m">· ' + esc((j.published_at || '').slice(0, 10)) + '</span>');
        (j.assets || []).forEach(function (a) {
          var mb = a.size / 1048576;
          print('  ' + esc(a.name) + ' <span class="m">' + (mb >= 1024 ? (mb / 1024).toFixed(2) + ' GB' : mb.toFixed(1) + ' MB') + '' + (a.download_count ? ' · ' + a.download_count + ' downloads' : '') + '</span>');
        });
      }).catch(function (e) { p('r', 'release: ' + e.message + ' (tries again later)'); });
    },
    'doctor': function () {
      prune();
      if (phoneSeen) { p('g', 'all good: the 24/7 phone server is listed - nothing to fix'); return; }
      p('y', 'the 24/7 phone server is not in the lobby. On the phone, in Termux:');
      print('  1. <span class="g">mp-server status</span>   <span class="m">is it running?</span>');
      print('  2. <span class="g">mp-server restart</span>  <span class="m">start it again (run.sh restarts it after crashes)</span>');
      print('  3. <span class="g">termux-wake-lock</span>   <span class="m">and turn off battery optimisation for Termux</span>');
      print('  4. <span class="g">mp-server log</span>      <span class="m">look for "lobby" / "broker" errors</span>');
      print('  5. <span class="g">mp-server update</span>   <span class="m">newest source + rebuild</span>');
      p('m', 'not installed yet? curl -fsSL ' + RAW + 'termux/install.sh | bash');
    },
    'vps-update': function () {
      var U = 'curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash';
      p('m', 'mp-server vps-update ubuntu@your-vps   - updates the 24/7 VPS over SSH (asked once, then saved)');
      print('  it runs: <span class="g">' + esc(U) + '</span>');
      p('m', '  the VPS autopilot also runs it by itself within 10 min of a new release' + (release && release.tag_name ? ' (latest ' + release.tag_name + ')' : ''));
    },
    'vps-setup': function () {   // 3.28.2: all four regions
      var S = 'curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/vps-setup.sh | bash -s ';
      p('m', 'a new 24/7 VPS (HiveMQ + bridge + servers) from this phone over SSH - one VPS per region:');
      [['asia', 'mpsamp'], ['europe', 'eusamp'], ['na', 'nasamp'], ['oceania', 'ocsamp']].forEach(function (r) {
        print('  <span class="g">' + esc('pkg install -y openssh && ssh root@YOUR_VPS_IP "' + S + r[0] + '"') + '</span> <span class="m">' + r[1] + '.duckdns.org</span>');
      });
      p('m', '  first point the DuckDNS name at the VPS IP and open TCP 1883 8883 8884 + UDP 1932');
      p('m', '  Debian 12 (no sudo / curl): log in as root, then  wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/vps-setup.sh | bash -s europe');
      p('m', '  Debian 12 installer (677 MB): github.com/ThirtyFoxMCx00E1/HiveMQ/releases/tag/Derbian');
    },
    'clear': function () { out.innerHTML = ''; }
  };
  // what the bot types -> which command runs
  var ALIAS = {
    'termux-info': 'termux-info',
    'curl -fsSL $MP/install.sh | grep say': 'install',
    'cat ~/mp-server/server.conf': 'cat-conf',
    'cat $MP_DATA/broker.txt': 'cat-broker',
    'cat $MP_DATA/regions.txt': 'cat-regions',
    'mp-server status': 'status', 'mp-server list': 'list', 'mp-server log': 'log', 'mp-server doctor': 'doctor', 'mp-server vps-update': 'vps-update', 'mp-server vps-setup': 'vps-setup', 'curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash': 'vps-update',
    'ping -c 2 $BROKERS': 'ping', 'curl -s $MP/release.json': 'release', 'clear': 'clear'
  };
  function run(line) {
    print('<span class="g">~ $</span> ' + esc(line));
    var deb = window.MPDebian && MPDebian.match(line);
    if (deb) { MPDebian.lines(deb).forEach(function (l) { p(l[0], l[1]); }); return; }   // 3.31
    var c = ALIAS[line];
    if (!c || !CMDS[c]) { p('r', line.split(' ')[0] + ': command not found'); return; }
    return CMDS[c]();
  }

  // ------------------------------------------------------------ MP Bot planner
  var boot = ['termux-info', 'curl -fsSL $MP/install.sh | grep say', 'cat ~/mp-server/server.conf', 'cat $MP_DATA/broker.txt', 'cat $MP_DATA/regions.txt', 'curl -s $MP/release.json', 'curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash', 'mp-server status'];
  var relCheck = false, relSeen = '';
  var rot = ['mp-server status', 'mp-server list', 'mp-server log', 'mp-server status', 'mp-server log'], ri = 0, cycles = 0, lastRel = Date.now();
  function next() {
    if (boot.length) return { cmd: boot.shift(), why: 'start-up check' };
    var dj = window.MPDebian && MPDebian.tick(); if (dj) return dj;   // 3.31: the Debian one-line setup
    prune();
    if (phoneChanged) { phoneChanged = false; return phoneSeen ? { cmd: 'mp-server status', why: '24/7 phone server came back' } : { cmd: 'mp-server doctor', why: '24/7 phone server left the lobby' }; }
    if (events.length - evSeen >= 3) return { cmd: 'mp-server log', why: (events.length - evSeen) + ' new lobby events' };
    if (Date.now() - lastPing > 90000) return { cmd: 'ping -c 2 $BROKERS', why: 'ping every 90 s', wait: 7000 };
    if (Date.now() - lastRel > 600000) { lastRel = Date.now(); relCheck = true; return { cmd: 'curl -s $MP/release.json', why: 'checking for a new release' }; }
    if (relCheck && release) { relCheck = false; if (release.tag_name !== relSeen) { relSeen = release.tag_name; return { cmd: 'curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash', why: 'release ' + release.tag_name + ': typing the VPS update' }; } }
    if (++cycles % 10 === 0) return { cmd: 'clear', why: 'keeping the screen short', wait: 600 };
    if (!phoneSeen && cycles % 4 === 0) return { cmd: 'mp-server doctor', why: '24/7 phone server still not seen' };
    return { cmd: rot[ri++ % rot.length], why: 'routine check' };
  }

  // ------------------------------------------------------------ start
  print('<span class="g">Welcome to Termux!</span>  <span class="m">(MP edition, runs in your browser)</span>');
  print('<span class="m">Docs:       </span>https://thirtyfoxmcx00e1.github.io/openmp.com/#host');
  print('<span class="m">Bot:        </span>MP Bot types and presses Enter by itself - no chat, no typing');
  print('');
  live();
  getText('regions.txt').catch(function () { return ''; }).then(function (t) {
    regionsTxt = t || '';
    var blocks = [], cur = null;
    regionsTxt.split(/\r?\n/).forEach(function (raw) {
      var l = raw.replace(/(^|\s)#.*/, '').trim();
      if (/^\[.+\]$/.test(l)) { cur = {}; blocks.push(cur); return; }
      var eq = l.indexOf('=');
      if (!cur || eq < 0) return;
      var k = l.slice(0, eq).trim().toLowerCase(), v = l.slice(eq + 1).trim();
      if (!(k in cur)) cur[k] = v;
    });
    blocks.forEach(function (r) { if (/^wss:\/\//i.test(r.web || '')) connect(r.web, r.web_user, r.web_pass); });
    if (!brokers.length) connect(PUBLIC[0]);
    MPBot({ out: out, line: $('typed'), keyEnter: $('kenter'), status: $('botst'), run: run, next: next, startMs: 1200 });
  });
  setInterval(live, 3000);
})();
