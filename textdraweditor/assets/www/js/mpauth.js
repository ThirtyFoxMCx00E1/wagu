/* MP Textdraw Editor - MP account login (the same accounts as the game).
   Talks to the MP Discord bot exactly like the game does (src/net/authcrypto.*, authclient.*):
   MQTT over WebSocket on a public broker, every message end-to-end encrypted:
   ECDH P-256 with the bot's pinned public key -> SHA-256("wagu-auth-v1" || z || ourPub) -> AES-256-GCM
   (AAD "WC" phone->bot, "WS" bot->phone). The broker only sees random bytes. */
(function (G) {
  'use strict';
  var BOT_PUB = '047bfb24d8637c172eb9353130450cca575553ef9cb1d8a9538451fa940eac0d12246f940a367ccc3f5231b61701d19c2a3d56a80eb49215ad71baf7405701e006';
  var TOPIC_REQ = 'wagu/auth1/req', TOPIC_RES = 'wagu/auth1/res/';
  var BROKERS = ['wss://broker.hivemq.com:8884/mqtt', 'wss://broker.emqx.io:8084/mqtt', 'wss://test.mosquitto.org:8081/mqtt'];
  var subtle = G.crypto.subtle, enc = new TextEncoder(), dec = new TextDecoder();
  function hex2b(h) { var o = new Uint8Array(h.length / 2); for (var i = 0; i < o.length; i++) o[i] = parseInt(h.substr(i * 2, 2), 16); return o; }
  function b2hex(b) { return Array.prototype.map.call(b, function (x) { return ('0' + x.toString(16)).slice(-2); }).join(''); }
  function cat() { var n = 0, i, a = arguments; for (i = 0; i < a.length; i++) n += a[i].length; var o = new Uint8Array(n), p = 0; for (i = 0; i < a.length; i++) { o.set(a[i], p); p += a[i].length; } return o; }
  function rnd(n) { var b = new Uint8Array(n); G.crypto.getRandomValues(b); return b; }

  // ---- a tiny MQTT 3.1.1 client over WebSocket (QoS 0/1 subscribe, QoS 0 publish) ----
  function vlen(n) { var o = []; do { var d = n % 128; n = Math.floor(n / 128); if (n) d |= 128; o.push(d); } while (n); return new Uint8Array(o); }
  function str(s) { var b = typeof s === 'string' ? enc.encode(s) : s; return cat(new Uint8Array([b.length >> 8, b.length & 255]), b); }
  function pkt(h, body) { return cat(new Uint8Array([h]), vlen(body.length), body); }
  function Mqtt(url, onMsg, onUp, onDown) {
    var ws = new G.WebSocket(url, 'mqtt'), buf = new Uint8Array(0), self = this, ping;
    ws.binaryType = 'arraybuffer'; this.ws = ws; this.up = false;
    ws.onopen = function () {
      var cid = 'wt' + b2hex(rnd(8));
      ws.send(pkt(0x10, cat(str('MQTT'), new Uint8Array([4, 2, 0, 60]), str(cid))));
    };
    ws.onmessage = function (e) {
      buf = cat(buf, new Uint8Array(e.data));
      for (;;) {
        if (buf.length < 2) return;
        var m = 1, len = 0, i = 1, d;
        do { if (i >= buf.length) return; d = buf[i++]; len += (d & 127) * m; m *= 128; } while (d & 128);
        if (buf.length < i + len) return;
        var t = buf[0], body = buf.slice(i, i + len); buf = buf.slice(i + len);
        if ((t >> 4) === 2) { if (body[1] === 0) { self.up = true; ping = setInterval(function () { try { ws.send(new Uint8Array([0xC0, 0])); } catch (x) {} }, 25000); onUp && onUp(); } }
        else if ((t >> 4) === 3) {
          var tl = (body[0] << 8) | body[1], topic = dec.decode(body.slice(2, 2 + tl)), off = 2 + tl + (((t >> 1) & 3) ? 2 : 0);
          onMsg(topic, body.slice(off));
        }
      }
    };
    ws.onclose = ws.onerror = function () { if (ping) clearInterval(ping); ping = null; var was = self.up || !self.dead; self.up = false; self.dead = true; if (was) onDown && onDown(); };
  }
  Mqtt.prototype.sub = function (topic) { this.ws.send(pkt(0x82, cat(new Uint8Array([0, 1]), str(topic), new Uint8Array([1])))); };
  Mqtt.prototype.pub = function (topic, data) { this.ws.send(pkt(0x30, cat(str(topic), data))); };
  Mqtt.prototype.close = function () { this.dead = true; try { this.ws.send(new Uint8Array([0xE0, 0])); this.ws.close(); } catch (x) {} };

  // ---- the encrypted session ----
  async function newSession() {
    var kp = await subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
    var bot = await subtle.importKey('raw', hex2b(BOT_PUB), { name: 'ECDH', namedCurve: 'P-256' }, false, []);
    var z = new Uint8Array(await subtle.deriveBits({ name: 'ECDH', public: bot }, kp.privateKey, 256));
    var pub = new Uint8Array(await subtle.exportKey('raw', kp.publicKey));
    var k = new Uint8Array(await subtle.digest('SHA-256', cat(enc.encode('wagu-auth-v1'), z, pub)));
    var key = await subtle.importKey('raw', k, 'AES-GCM', false, ['encrypt', 'decrypt']);
    var sid = b2hex(new Uint8Array(await subtle.digest('SHA-256', pub)).slice(0, 8));
    return { key: key, pub: pub, sid: sid };
  }
  async function seal(s, kv) {
    var plain = ''; Object.keys(kv).sort().forEach(function (k) { plain += k + '=' + String(kv[k]).replace(/[\r\n|]/g, '').slice(0, 200) + '\n'; });
    var iv = rnd(12);
    var ct = new Uint8Array(await subtle.encrypt({ name: 'AES-GCM', iv: iv, additionalData: enc.encode('WC'), tagLength: 128 }, s.key, enc.encode(plain)));
    return cat(new Uint8Array([1]), s.pub, iv, ct);
  }
  async function open(s, f) {
    if (f.length < 29 || f[0] !== 1) return null;
    try {
      var p = await subtle.decrypt({ name: 'AES-GCM', iv: f.slice(1, 13), additionalData: enc.encode('WS'), tagLength: 128 }, s.key, f.slice(13));
      var o = {}; dec.decode(p).split('\n').forEach(function (l) { var q = l.indexOf('='); if (q > 0) o[l.slice(0, q)] = l.slice(q + 1); });
      return o;
    } catch (e) { return null; }
  }

  // ---- public API ----
  // MPAuth.connect(onEvent) -> client; events: {op:'link', ok}, {op:'down'} and every bot answer (op=gen/verified/login/signup/...)
  function Client(onEvent) { this.on = onEvent || function () {}; this.rid = 0; this.dev = Client.device(); this.bi = 0; this.queue = []; this.linked = false; }
  Client.device = function () {
    try { var d = G.localStorage.getItem('mp_auth_dev'); if (!d) { d = b2hex(rnd(16)); G.localStorage.setItem('mp_auth_dev', d); } return d; } catch (e) { return b2hex(rnd(16)); }
  };
  Client.prototype.start = async function () {
    var self = this;
    if (!this.s) this.s = await newSession();
    var url = BROKERS[this.bi % BROKERS.length];
    this.broker = url.replace(/^wss:\/\//, '').replace(/[:\/].*$/, '');
    this.m = new Mqtt(url, function (topic, data) {
      if (topic !== TOPIC_RES + self.s.sid) return;
      open(self.s, data).then(function (a) {
        if (!a) return;
        if (a.op === 'hello') { if (a.ok === '1' && !self.linked) { self.linked = true; clearInterval(self.helloT); self.on({ op: 'link', ok: true, broker: self.broker }); self.flush(); } return; }
        self.on(a);
      });
    }, function () {
      self.m.sub(TOPIC_RES + self.s.sid);
      setTimeout(function () {
        var hello = function () { self.raw({ op: 'hello', rid: 'h', dev: self.dev }); };
        hello(); self.helloT = setInterval(hello, 2500);
      }, 200);
    }, function () {
      clearInterval(self.helloT); var was = self.linked; self.linked = false;
      if (self.stopped) return;
      self.bi++; self.on({ op: 'down', was: was });
      setTimeout(function () { self.start(); }, 1200);
    });
    clearTimeout(this.slowT);
    this.slowT = setTimeout(function () { if (!self.linked && !self.stopped) self.on({ op: 'slow', broker: self.broker, up: self.m && self.m.up }); }, 12000);
  };
  Client.prototype.raw = function (kv) { var self = this; seal(this.s, kv).then(function (f) { try { self.m.pub(TOPIC_REQ, f); } catch (e) {} }); };
  Client.prototype.flush = function () { while (this.linked && this.queue.length) this.raw(this.queue.shift()); };
  Client.prototype.send = function (kv) { kv.rid = kv.rid || String(++this.rid); kv.dev = this.dev; this.queue.push(kv); this.flush(); return kv.rid; };
  Client.prototype.stop = function () { this.stopped = true; clearInterval(this.helloT); if (this.m) this.m.close(); };

  // an 8 character code, no 0/O/1/I (same alphabet as the game: 32 letters + digits)
  Client.code = function () { var A = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789', b = rnd(8), o = ''; for (var i = 0; i < 8; i++) o += A[b[i] & 31]; return o; };
  Client.chal = function () { return b2hex(rnd(16)); };

  G.MPAuth = { Client: Client, BROKERS: BROKERS, DISCORD: 'https://discord.gg/tbsDknk34y' };
})(typeof window !== 'undefined' ? window : globalThis);
