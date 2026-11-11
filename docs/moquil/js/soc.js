/* Moquil - MP SOCIAL network core. The same protocol as the game (src/net/social.cpp, "wagu/soc1/"),
   so Moquil and the MP mod (libMP32.so / libMP64.so) share friends, private chats, group chats, the party
   and communities over the public HiveMQ broker.
     - Your ID = first 8 digits of SHA-256(public key) (ECDSA / ECDH P-256). Every message is signed.
     - Private things (friend requests, chats, invites) are end-to-end encrypted (ECDH -> AES-256-GCM).
     - Mailbox = retained messages on m/<id>/<msg id>, cleared after reading.
   Moquil extras (the game ignores them, old games too):
     - p/<id>   rich profile: bio, pronouns, status + clear time, avatar, banner, decoration, effect, frame,
                nameplate, nametag, badges, member since
     - r/<mod>/<id>  your rating of a mod (1-5)
     - v/<room>/... voice room signalling (WebRTC)
     - k/<hash> link code: the game puts its key here (encrypted with the code) so Moquil becomes the same ID */
(function (G) {
  'use strict';
  var ROOT = 'wagu/soc1/', FS = '\x1f', RS = '\x1e';
  var BROKERS = ['wss://broker.hivemq.com:8884/mqtt', 'wss://broker.hivemq.com:8000/mqtt'];
  var subtle = G.crypto.subtle, enc = new TextEncoder(), dec = new TextDecoder();
  var PRES_EVERY = 60, PRES_FRESH = 150, CONV_KEEP = 150, PARTY_MAX = 5;

  function hex(b) { var s = ''; for (var i = 0; i < b.length; i++) s += (b[i] < 16 ? '0' : '') + b[i].toString(16); return s; }
  function unhex(h) { if (h.length & 1) return null; var o = new Uint8Array(h.length / 2); for (var i = 0; i < o.length; i++) { var v = parseInt(h.substr(i * 2, 2), 16); if (isNaN(v)) return null; o[i] = v; } return o; }
  function cat() { var n = 0, i, a = arguments; for (i = 0; i < a.length; i++) n += a[i].length; var o = new Uint8Array(n), p = 0; for (i = 0; i < a.length; i++) { o.set(a[i], p); p += a[i].length; } return o; }
  function rnd(n) { var b = new Uint8Array(n); G.crypto.getRandomValues(b); return b; }
  function b64u(b) { var s = ''; for (var i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); }
  function unb64u(s) { s = s.replace(/-/g, '+').replace(/_/g, '/'); while (s.length % 4) s += '='; var r = atob(s), o = new Uint8Array(r.length); for (var i = 0; i < r.length; i++) o[i] = r.charCodeAt(i); return o; }
  function now() { return Math.floor(Date.now() / 1000); }
  function sha(b) { return subtle.digest('SHA-256', b).then(function (x) { return new Uint8Array(x); }); }
  // no control characters (the separators are control characters); cut to a byte length on a UTF-8 boundary
  function clean(s, max) {
    s = String(s == null ? '' : s).replace(/[\x00-\x1f]/g, ' ');
    var b = enc.encode(s); if (b.length <= max) return s;
    var e = max; while (e > 0 && (b[e] & 0xC0) === 0x80) e--;
    return dec.decode(b.slice(0, e));
  }
  function cleanName(s) { return clean(s, 24).replace(/[:,]/g, '_'); }

  // ---- MQTT 3.1.1 over WebSocket (QoS 0, retained publish, will, unsubscribe) ----
  function vlen(n) { var o = []; do { var d = n % 128; n = Math.floor(n / 128); if (n) d |= 128; o.push(d); } while (n); return new Uint8Array(o); }
  function mstr(s) { var b = typeof s === 'string' ? enc.encode(s) : s; return cat(new Uint8Array([b.length >> 8, b.length & 255]), b); }
  function pkt(h, body) { return cat(new Uint8Array([h]), vlen(body.length), body); }
  function Mqtt(url, will, cb) {
    var ws = new G.WebSocket(url, 'mqtt'), buf = new Uint8Array(0), self = this;
    ws.binaryType = 'arraybuffer'; this.ws = ws; this.up = false; this.pid = 1;
    ws.onopen = function () {
      var cid = 'mq' + hex(rnd(8)), flags = 2, body = cat(mstr('MQTT'), new Uint8Array([4]));
      if (will) flags |= 4 | 32;   // will flag + will retain (QoS 0)
      body = cat(body, new Uint8Array([flags, 0, 60]), mstr(cid));
      if (will) body = cat(body, mstr(will), new Uint8Array([0, 0]));   // empty retained = offline
      ws.send(pkt(0x10, body));
    };
    ws.onmessage = function (e) {
      buf = cat(buf, new Uint8Array(e.data));
      for (;;) {
        if (buf.length < 2) return;
        var m = 1, len = 0, i = 1, d;
        do { if (i >= buf.length) return; d = buf[i++]; len += (d & 127) * m; m *= 128; } while (d & 128);
        if (buf.length < i + len) return;
        var t = buf[0], body = buf.slice(i, i + len); buf = buf.slice(i + len);
        if ((t >> 4) === 2) { if (body[1] === 0) { self.up = true; self.ping = setInterval(function () { try { ws.send(new Uint8Array([0xC0, 0])); } catch (x) {} }, 25000); cb.up(); } else ws.close(); }
        else if ((t >> 4) === 3) {
          var tl = (body[0] << 8) | body[1], topic = dec.decode(body.slice(2, 2 + tl)), off = 2 + tl + (((t >> 1) & 3) ? 2 : 0);
          cb.msg(topic, body.slice(off), !!(t & 1));
        }
      }
    };
    ws.onclose = ws.onerror = function () { if (self.ping) clearInterval(self.ping); self.ping = null; if (self.dead) return; self.dead = true; self.up = false; cb.down(); };
  }
  Mqtt.prototype.nid = function () { this.pid = (this.pid % 65000) + 1; return new Uint8Array([this.pid >> 8, this.pid & 255]); };
  Mqtt.prototype.sub = function (f) { try { this.ws.send(pkt(0x82, cat(this.nid(), mstr(f), new Uint8Array([0])))); } catch (e) {} };
  Mqtt.prototype.unsub = function (f) { try { this.ws.send(pkt(0xA2, cat(this.nid(), mstr(f)))); } catch (e) {} };
  Mqtt.prototype.pub = function (topic, data, retain) { try { this.ws.send(pkt(retain ? 0x31 : 0x30, cat(mstr(topic), typeof data === 'string' ? enc.encode(data) : data))); return true; } catch (e) { return false; } };
  Mqtt.prototype.close = function (lastTopic) { this.dead = true; try { if (lastTopic) this.pub(lastTopic, '', true); this.ws.send(new Uint8Array([0xE0, 0])); this.ws.close(); } catch (x) {} };

  // ---- keys ----
  var K = { priv: null, pubKey: null, pubHex: '', id: '', ecdh: null };
  async function idFromPub(pubHex) {
    var raw = unhex(pubHex); if (!raw || raw.length !== 65) return '';
    var h = await sha(raw); var v = ((h[0] << 24) >>> 0) + (h[1] << 16) + (h[2] << 8) + h[3];
    return ('0000000' + (v % 100000000)).slice(-8);
  }
  async function importPair(dHex, qHex) {
    var d = unhex(dHex), q = unhex(qHex);
    if (!d || d.length !== 32 || !q || q.length !== 65 || q[0] !== 4) throw new Error('bad key');
    var jwk = { kty: 'EC', crv: 'P-256', d: b64u(d), x: b64u(q.slice(1, 33)), y: b64u(q.slice(33)), ext: true };
    K.priv = await subtle.importKey('jwk', jwk, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['sign']);
    K.ecdh = await subtle.importKey('jwk', jwk, { name: 'ECDH', namedCurve: 'P-256' }, false, ['deriveBits']);
    K.pubHex = hex(q); K.id = await idFromPub(K.pubHex); K.dHex = dHex;
  }
  async function loadOrMakeKey() {
    var s = null; try { s = JSON.parse(localStorage.getItem('moq_key') || 'null'); } catch (e) {}
    if (s && s.d && s.q) { try { await importPair(s.d, s.q); return; } catch (e) {} }
    var kp = await subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
    var jwk = await subtle.exportKey('jwk', kp.privateKey);
    var d = hex(unb64u(jwk.d)), q = '04' + hex(unb64u(jwk.x)) + hex(unb64u(jwk.y));
    localStorage.setItem('moq_key', JSON.stringify({ d: d, q: q, made: Date.now() }));
    await importPair(d, q);
  }
  async function sign(bytes) { return hex(new Uint8Array(await subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, K.priv, bytes))); }
  var pubCache = {};
  function verifyKey(pubHex) {
    if (!pubCache[pubHex]) pubCache[pubHex] = subtle.importKey('raw', unhex(pubHex), { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
    return pubCache[pubHex];
  }
  var shared = {};
  function sharedKey(pubHex) {
    if (!shared[pubHex]) shared[pubHex] = (async function () {
      var peer = await subtle.importKey('raw', unhex(pubHex), { name: 'ECDH', namedCurve: 'P-256' }, false, []);
      var z = new Uint8Array(await subtle.deriveBits({ name: 'ECDH', public: peer }, K.ecdh, 256));
      var k = await sha(cat(enc.encode('wagu-soc-v1'), z));
      return subtle.importKey('raw', k, 'AES-GCM', false, ['encrypt', 'decrypt']);
    })();
    return shared[pubHex];
  }
  async function seal(pubHex, plain) {
    var key = await sharedKey(pubHex), iv = rnd(12);
    var ct = new Uint8Array(await subtle.encrypt({ name: 'AES-GCM', iv: iv, additionalData: enc.encode('WS1'), tagLength: 128 }, key, enc.encode(plain)));
    return hex(cat(iv, ct));
  }
  async function unseal(pubHex, h) {
    var b = unhex(h); if (!b || b.length < 28) return null;
    try { var key = await sharedKey(pubHex); return dec.decode(await subtle.decrypt({ name: 'AES-GCM', iv: b.slice(0, 12), additionalData: enc.encode('WS1'), tagLength: 128 }, key, b.slice(12))); }
    catch (e) { return null; }
  }

  // ---- envelope: WS1 | kind | fromId | fromName | ts | to | body | pub | sig ----
  async function makeEnv(kind, to, body) {
    var s = 'WS1' + FS + kind + FS + K.id + FS + S.name + FS + now() + FS + to + FS + body + FS + K.pubHex + FS;
    return s + await sign(enc.encode(s));
  }
  async function parseEnv(bytes) {
    if (bytes.length < 200 || bytes.length > 64 * 1024) return null;
    var p = dec.decode(bytes); if (p.indexOf('WS1' + FS) !== 0) return null;
    var f = p.split(FS); if (f.length !== 9) return null;
    var e = { kind: f[1], from: f[2], name: cleanName(f[3]), ts: parseInt(f[4], 10) || 0, to: f[5], body: f[6], pub: f[7] };
    if (e.pub.length !== 130 || await idFromPub(e.pub) !== e.from) return null;
    var sig = unhex(f[8]); if (!sig || sig.length !== 64) return null;
    var signed = enc.encode(p.slice(0, p.lastIndexOf(FS) + 1));
    try { if (!await subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, await verifyKey(e.pub), sig, signed)) return null; } catch (x) { return null; }
    return e;
  }

  // ---- state (saved in localStorage) ----
  var S = {
    name: '', friends: [], inc: [], out: [], convs: [], coms: [], notifs: [], seen: [], partyOk: [], favs: [],
    people: {}, pres: {}, presAct: {}, profiles: {}, comMem: {}, comMemName: {}, ratings: {}, myRatings: {}, pending: []
  };
  var SAVE_KEYS = ['friends', 'inc', 'out', 'convs', 'coms', 'notifs', 'seen', 'partyOk', 'favs', 'myRatings'];
  function save() { var o = {}; SAVE_KEYS.forEach(function (k) { o[k] = S[k]; }); try { localStorage.setItem('moq_state_' + K.id, JSON.stringify(o)); } catch (e) {} }
  function load() { try { var o = JSON.parse(localStorage.getItem('moq_state_' + K.id) || 'null'); if (o) SAVE_KEYS.forEach(function (k) { if (o[k]) S[k] = o[k]; }); } catch (e) {} }
  var saveT = 0; function dirty() { clearTimeout(saveT); saveT = setTimeout(save, 600); if (typeof backupSoon === 'function') backupSoon(); emit('change'); }

  var listeners = {};
  function on(ev, f) { (listeners[ev] = listeners[ev] || []).push(f); }
  function emit(ev, a) { (listeners[ev] || []).forEach(function (f) { try { f(a); } catch (e) { console.error(e); } }); }

  function findIn(list, id) { for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i]; return null; }
  function removeFrom(list, id) { for (var i = list.length - 1; i >= 0; i--) if (list[i].id === id) list.splice(i, 1); }
  function pubOf(id) {
    var p = findIn(S.friends, id) || findIn(S.inc, id) || findIn(S.out, id);
    if (p && p.pub) return p.pub;
    return S.people[id] ? S.people[id].pub : '';
  }
  function conv(key) { for (var i = 0; i < S.convs.length; i++) if (S.convs[i].key === key) return S.convs[i]; return null; }
  function com(cid) { for (var i = 0; i < S.coms.length; i++) if (S.coms[i].cid === cid) return S.coms[i]; return null; }
  function isMember(c, id) { return c.members.some(function (m) { return m.id === id; }); }

  function addNotif(kind, fromId, fromName, ref, text) {
    if (kind === 'msg' || kind === 'com')
      for (var i = 0; i < S.notifs.length; i++) { var n = S.notifs[i]; if (n.kind === kind && n.ref === ref && !n.read) { n.fromId = fromId; n.fromName = fromName; n.text = text; n.ts = now(); dirty(); emit('notif', n); return n; } }
    var x = { kind: kind, fromId: fromId, fromName: fromName, ref: ref, text: clean(text, 160), ts: now(), read: false, done: false };
    S.notifs.push(x); if (S.notifs.length > 120) S.notifs.shift();
    dirty(); emit('notif', x); return x;
  }
  function doneNotifs(kind, fromId, ref) { S.notifs.forEach(function (n) { if (n.kind === kind && n.fromId === fromId && (!ref || n.ref === ref)) { n.done = true; n.read = true; } }); dirty(); }

  // ---- link ----
  var M = null, bi = 0, gen = 0, subs = {}, presT = 0, upCb = [];
  function T(t) { return ROOT + t; }
  function sub(f) { subs[f] = 1; if (M && M.up) M.sub(T(f)); }
  function unsub(f) { delete subs[f]; if (M && M.up) M.unsub(T(f)); }
  var outQ = [];
  function pub(t, payload, retain) { if (M && M.up && M.pub(T(t), payload, retain)) return; outQ.push([t, payload, retain]); if (outQ.length > 300) outQ.shift(); }
  function connect() {
    var url = BROKERS[bi % BROKERS.length];
    S.broker = url.replace(/^wss:\/\//, '').replace(/\/.*$/, '');
    M = new Mqtt(url, T('o/' + K.id), {
      up: function () {
        gen++; Object.keys(subs).forEach(function (f) { M.sub(T(f)); });
        while (outQ.length) { var o = outQ.shift(); M.pub(T(o[0]), o[1], o[2]); }
        publishProfile(); publishPresence(); publishRich();
        S.coms.forEach(function (c) { if (c.ownerId === K.id && !c.deleted && c.name) makeEnv('com', '', c.name + RS + (c.desc || '')).then(function (e) { pub('c/' + c.cid + '/i', e, true); }); });
        emit('link', true);
      },
      msg: function (topic, data) { onTopic(topic, data); },
      down: function () { emit('link', false); bi++; setTimeout(connect, 1500); }
    });
  }
  function up() { return !!(M && M.up); }

  // a private message into someone's mailbox
  async function mail(to, kind, body) {
    if (!to) return false;
    var pk = to === K.id ? K.pubHex : pubOf(to);
    if (!pk) { sub('u/' + to); if (S.pending.length < 200) S.pending.push({ to: to, kind: kind, body: body, until: now() + 600 }); return true; }
    var sealed = await seal(pk, body), mid = hex(rnd(8));
    if (to === K.id) { OWN[mid] = 1; S.seen.push(mid); }   // our own copy: the other device reads (and clears) it
    pub('m/' + to + '/' + mid, await makeEnv(kind, to, sealed), true);
    return true;
  }
  var OWN = {};
  function linked() { try { var k = JSON.parse(localStorage.getItem('moq_key') || 'null'); return !!(k && k.linked); } catch (e) { return false; } }
  async function publishProfile() { if (S.name) pub('u/' + K.id, await makeEnv('prof', '', S.name), true); }
  function publishPresence() { var mp = G.MoqProfile ? G.MoqProfile() : null; if (mp && mp.pres === 'invisible') { pub('o/' + K.id, '', true); return; } pub('o/' + K.id, now() + '|Moquil', true); }
  // the rich profile (Moquil + the MP mod 4.13 read it)
  async function publishRich() {
    var p = G.MoqProfile ? G.MoqProfile() : null; if (!p || !S.name) return;
    var j = JSON.stringify(p);
    if (j.length > 56000) { p = Object.assign({}, p, { banner: '' }); j = JSON.stringify(p); }
    if (j.length > 56000) { p.avatar = ''; j = JSON.stringify(p); }
    pub('p/' + K.id, await makeEnv('pf2', '', j), true);
  }

  function membersStr(c) { return c.members.map(function (m) { return m.id + ':' + cleanName(m.name); }).join(','); }
  function groupBody(c) { return c.gid + RS + c.kind + RS + clean(c.title, 40) + RS + c.owner + RS + membersStr(c); }
  function groupCast(c, kind, body, except) { c.members.forEach(function (m) { if (m.id !== K.id && m.id !== except) mail(m.id, kind, body); }); }
  function addMsg(c, fromId, fromName, text, ts, mine) {
    var m = { fromId: fromId, fromName: fromName, text: clean(text, 400), ts: ts || now(), mine: !!mine };
    if (m.ts > now() + 60 || m.ts < now() - 30 * 86400) m.ts = now();
    c.msgs.push(m); if (c.msgs.length > CONV_KEEP) c.msgs.splice(0, c.msgs.length - CONV_KEEP);
    c.last = now(); if (!mine) c.unread = (c.unread || 0) + 1;
    dirty(); emit('msg', { key: c.key, msg: m });
  }
  function makeDm(id, name) {
    var k = 'u' + id, c = conv(k);
    if (c) { if (name) c.title = name; return c; }
    c = { key: k, kind: 0, gid: '', title: name || id, owner: '', members: [{ id: K.id, name: S.name }, { id: id, name: name || id }], msgs: [], unread: 0, last: now() };
    S.convs.push(c); dirty(); return c;
  }
  function removeConv(key) { for (var i = S.convs.length - 1; i >= 0; i--) if (S.convs[i].key === key) S.convs.splice(i, 1); dirty(); }
  function fixOwner(c) { if (!isMember(c, c.owner) && c.members.length) c.owner = c.members[0].id; }
  function comWatched(c) { return c.joined || c.followed || c.ownerId === K.id || c.cid === S.viewCid; }
  function comSubs(c, onn) { var a = ['c/' + c.cid + '/m/+', 'c/' + c.cid + '/p/+']; a.forEach(onn ? sub : unsub); }
  function comCount(c) {
    var mm = S.comMem[c.cid] || {}; c.members = 0; c.followers = 0; c.memberList = [];
    Object.keys(mm).forEach(function (id) { if (mm[id].indexOf('j') >= 0) { c.members++; c.memberList.push({ id: id, name: S.comMemName[id] || id }); } if (mm[id].indexOf('f') >= 0) c.followers++; });
  }
  async function publishMembership(c) {
    var f = (c.joined ? 'j' : '') + (c.followed ? 'f' : '');
    if (!f) pub('c/' + c.cid + '/m/' + K.id, '', true); else pub('c/' + c.cid + '/m/' + K.id, await makeEnv('cm', c.cid, f), true);
    S.comMem[c.cid] = S.comMem[c.cid] || {};
    if (!f) delete S.comMem[c.cid][K.id]; else { S.comMem[c.cid][K.id] = f; S.comMemName[K.id] = S.name; }
    comCount(c);
  }

  // ---- incoming ----
  async function onMail(mid, e) {
    if (e.to !== K.id || S.seen.indexOf(mid) >= 0) return;
    S.seen.push(mid); if (S.seen.length > 300) S.seen.shift(); dirty();
    var fr = findIn(S.friends, e.from);
    if (fr && fr.pub && fr.pub !== e.pub) return;
    var body = await unseal(e.pub, e.body); if (body === null) return;
    if (e.from !== K.id) S.people[e.from] = { id: e.from, name: e.name, pub: e.pub, since: now() };
    var k = e.kind;
    if (e.from === K.id) {   // a copy from your other device (the game or another Moquil)
      if (k === 'dmc') { var f0 = body.split(RS); var c0 = conv(f0[0]) || (f0[0][0] === 'u' ? makeDm(f0[0].slice(1), (findIn(S.friends, f0[0].slice(1)) || {}).name) : null); if (c0) { addMsg(c0, K.id, S.name, f0[1] || '', e.ts, true); } }
      return;
    }
    if (k === 'freq') {
      if (findIn(S.friends, e.from)) { mail(e.from, 'facc', ''); return; }
      if (findIn(S.out, e.from)) { removeFrom(S.out, e.from); S.friends.push({ id: e.from, name: e.name, pub: e.pub, since: now() }); mail(e.from, 'facc', ''); addNotif('facc', e.from, e.name, '', e.name + ' is now your friend'); sub('o/' + e.from); sub('p/' + e.from); return; }
      if (!findIn(S.inc, e.from)) S.inc.push({ id: e.from, name: e.name, pub: e.pub, since: now() });
      doneNotifs('freq', e.from, ''); addNotif('freq', e.from, e.name, '', e.name + ' #' + e.from + ' sent you a friend request');
    } else if (k === 'facc') {
      if (!findIn(S.out, e.from) && !findIn(S.friends, e.from)) return;
      removeFrom(S.out, e.from);
      if (!findIn(S.friends, e.from)) { S.friends.push({ id: e.from, name: e.name, pub: e.pub, since: now() }); addNotif('facc', e.from, e.name, '', e.name + ' accepted your friend request. You can chat now.'); sub('o/' + e.from); sub('p/' + e.from); }
    } else if (k === 'fdec') { if (!findIn(S.out, e.from)) return; removeFrom(S.out, e.from); addNotif('info', e.from, e.name, '', e.name + ' declined your friend request'); }
    else if (k === 'fcan') { removeFrom(S.inc, e.from); doneNotifs('freq', e.from, ''); }
    else if (k === 'frem') { removeFrom(S.friends, e.from); }
    else if (k === 'dm') {
      fr = findIn(S.friends, e.from); if (!fr) return; fr.name = e.name;
      var c = makeDm(e.from, e.name); addMsg(c, e.from, e.name, body, e.ts, false);
      addNotif('msg', e.from, e.name, c.key, e.name + ': ' + clean(body, 80));
    } else if (k === 'ginv' || k === 'gupd') {
      var f = body.split(RS); if (f.length < 5) return;
      var gid = f[0], kind = parseInt(f[1], 10); if (kind !== 1 && kind !== 2) return;
      var key = 'g' + gid, g = conv(key);
      var ok = (g && isMember(g, e.from)) || (!g && kind === 1 && findIn(S.friends, e.from)) || (!g && kind === 2 && S.partyOk.indexOf(gid) >= 0);
      if (!ok) return;
      var mem = f[4].split(',').map(function (m) { var p = m.indexOf(':'); return p > 0 ? { id: m.slice(0, p), name: m.slice(p + 1) } : null; }).filter(Boolean);
      if (!mem.some(function (m) { return m.id === K.id; })) { if (g) { removeConv(key); addNotif('info', e.from, e.name, '', kind === 2 ? 'You left the party' : 'You were removed from ' + f[2]); } return; }
      var fresh = !g;
      if (fresh) { if (kind === 2) { var old = party(); if (old && old.gid !== gid) leaveParty(); } g = { key: key, gid: gid, kind: kind, msgs: [], unread: 0 }; S.convs.push(g); }
      g.title = clean(f[2], 40); g.owner = f[3]; g.members = mem; g.last = now();
      mem.forEach(function (m) { if (m.id !== K.id && !pubOf(m.id)) sub('u/' + m.id); });
      var pi = S.partyOk.indexOf(gid); if (pi >= 0) S.partyOk.splice(pi, 1);
      if (fresh && kind === 1) addNotif('group', e.from, e.name, key, e.name + ' added you to the group ' + g.title);
      if (fresh && kind === 2) addNotif('info', e.from, e.name, key, "You joined " + e.name + "'s party");
      dirty();
    } else if (k === 'gmsg') {
      var f2 = body.split(RS); if (f2.length < 2) return;
      var c2 = conv('g' + f2[0]); if (!c2 || !isMember(c2, e.from)) return;
      addMsg(c2, e.from, e.name, f2[1], e.ts, false);
      if (c2.kind === 1) addNotif('msg', e.from, e.name, c2.key, c2.title + ' - ' + e.name + ': ' + clean(f2[1], 70));
    } else if (k === 'glv') {
      var c3 = conv('g' + body); if (!c3) return;
      c3.members = c3.members.filter(function (m) { return m.id !== e.from; }); fixOwner(c3);
      addMsg(c3, '', '', e.name + (c3.kind === 2 ? ' left the party' : ' left the group'), e.ts, true); c3.unread = 0;
    } else if (k === 'pinv') {
      var f3 = body.split(RS); if (!findIn(S.friends, e.from)) return;
      doneNotifs('pinv', e.from, f3[0]); addNotif('pinv', e.from, e.name, f3[0], e.name + ' invited you to their party');
    } else if (k === 'pacc') {
      var c4 = conv('g' + body); if (!c4 || c4.kind !== 2 || c4.owner !== K.id) return;
      if (!isMember(c4, e.from) && c4.members.length < PARTY_MAX) c4.members.push({ id: e.from, name: e.name });
      else if (!isMember(c4, e.from)) { addNotif('info', e.from, e.name, '', 'The party is full (5): ' + e.name + " couldn't join"); return; }
      addMsg(c4, '', '', e.name + ' joined the party', now(), true); c4.unread = 0;
      addNotif('pacc', e.from, e.name, c4.key, e.name + ' joined your party');
      groupCast(c4, 'gupd', groupBody(c4));
    } else if (k === 'pdec') addNotif('info', e.from, e.name, '', e.name + ' declined your party invite');
    dirty();
  }

  var inQ = [], busy = false;
  function onTopic(topic, data) { inQ.push([topic, data]); if (!busy) drain(); }
  async function drain() {
    busy = true;
    while (inQ.length) { var x = inQ.shift(); try { await handle(x[0], x[1]); } catch (e) { console.error(e); } }
    busy = false;
  }
  async function handle(topic, data) {
    if (topic.indexOf(ROOT) !== 0) return;
    var t = topic.slice(ROOT.length), p = t.split('/');
    if (p.length === 2 && p[0] === 'o') {
      if (!data.length) { delete S.pres[p[1]]; delete S.presAct[p[1]]; }
      else { var s = dec.decode(data), ts = parseInt(s, 10); if (ts > 0) { S.pres[p[1]] = ts; S.presAct[p[1]] = s.indexOf('|') > 0 ? s.slice(s.indexOf('|') + 1) : 'MP'; } }
      emit('presence', p[1]); return;
    }
    if (p[0] === 'v') { emit('voice', { room: p[1], to: p[2], data: data }); return; }
    if (p[0] === 'k') { emit('linkcode', { h: p[1], data: data }); return; }
    if (p[0] === 's' && p.length === 2) { if (p[1] === K.id && data.length) { var eb = await parseEnv(data); if (eb && eb.kind === 'bk') await restoreBackup(eb); } return; }
    if (!data.length) {
      if (p.length === 3 && p[0] === 'c' && p[2] === 'i') { var c = com(p[1]); if (c && (c.ownerId !== K.id || !c.joined)) { c.deleted = true; emit('change'); } }
      else if (p.length === 4 && p[0] === 'c' && p[2] === 'm') { if (S.comMem[p[1]]) delete S.comMem[p[1]][p[3]]; var c1 = com(p[1]); if (c1) comCount(c1); emit('change'); }
      else if (p.length === 4 && p[0] === 'c' && p[2] === 'p') { var c2 = com(p[1]); if (c2) c2.posts = (c2.posts || []).filter(function (x) { return x.id !== p[3]; }); emit('change'); }
      else if (p.length === 3 && p[0] === 'r') { if (S.ratings[p[1]]) delete S.ratings[p[1]][p[2]]; emit('rating', p[1]); }
      return;
    }
    var e = await parseEnv(data);
    if (!e) { if (p.length === 3 && p[0] === 'm' && p[1] === K.id) pub(t, '', true); return; }
    if (p.length === 2 && p[0] === 'u' && e.kind === 'prof' && e.from === p[1]) {
      S.people[e.from] = { id: e.from, name: cleanName(e.body), pub: e.pub, since: e.ts };
      var f = findIn(S.friends, e.from); if (f && f.pub === e.pub && f.name !== cleanName(e.body)) { f.name = cleanName(e.body); var dc = conv('u' + e.from); if (dc) dc.title = f.name; dirty(); }
      var o = findIn(S.out, e.from); if (o && !o.pub) { o.pub = e.pub; dirty(); }
      for (var i = S.pending.length - 1; i >= 0; i--) if (S.pending[i].to === e.from) { var pm = S.pending.splice(i, 1)[0]; mail(pm.to, pm.kind, pm.body); }
      emit('people'); return;
    }
    if (p.length === 2 && p[0] === 'p' && e.kind === 'pf2' && e.from === p[1]) {
      try { var pr = JSON.parse(e.body); pr.ts = e.ts; pr.name = e.name; S.profiles[e.from] = pr; } catch (x) {}
      emit('profile', e.from); return;
    }
    if (p.length === 3 && p[0] === 'r' && e.kind === 'rt' && e.from === p[2] && e.to === p[1]) {
      var v = parseInt(e.body, 10); if (v >= 1 && v <= 5) { S.ratings[p[1]] = S.ratings[p[1]] || {}; S.ratings[p[1]][e.from] = v; emit('rating', p[1]); }
      return;
    }
    if (p.length === 3 && p[0] === 'm' && p[1] === K.id) { if (OWN[p[2]]) return; await onMail(p[2], e); pub(t, '', true); return; }
    if (p.length === 3 && p[0] === 'c' && p[2] === 'i' && e.kind === 'com') {
      if (p[1].indexOf(e.from + '-') !== 0) return;
      var ff = e.body.split(RS), cc = com(p[1]);
      if (!cc) { cc = { cid: p[1], posts: [], seen: 0 }; S.coms.push(cc); }
      cc.name = clean(ff[0], 40); cc.desc = ff.length > 1 ? clean(ff[1], 300) : ''; cc.ownerId = e.from; cc.ownerName = e.name; cc.ts = e.ts; cc.deleted = false;
      if (comWatched(cc)) comSubs(cc, true);
      emit('change'); return;
    }
    if (p.length === 4 && p[0] === 'c' && p[2] === 'm' && e.kind === 'cm' && e.from === p[3] && e.to === p[1]) {
      S.comMem[p[1]] = S.comMem[p[1]] || {}; S.comMem[p[1]][e.from] = e.body.slice(0, 2); S.comMemName[e.from] = e.name;
      var c5 = com(p[1]); if (c5) comCount(c5); emit('change'); return;
    }
    if (p.length === 4 && p[0] === 'c' && p[2] === 'p' && e.kind === 'cp' && e.to === p[1]) {
      var c6 = com(p[1]); if (!c6) return; c6.posts = c6.posts || [];
      if (c6.posts.some(function (x) { return x.id === p[3]; })) return;
      var po = { id: p[3], fromId: e.from, fromName: e.name, text: clean(e.body, 600), ts: e.ts };
      c6.posts.push(po); c6.posts.sort(function (a, b) { return b.ts - a.ts; }); if (c6.posts.length > 200) c6.posts.pop();
      if ((c6.joined || c6.followed) && e.from !== K.id && e.ts > (c6.seen || 0) && c6.cid !== S.viewCid)
        addNotif('com', e.from, e.name, c6.cid, c6.name + ' - ' + e.name + ': ' + clean(e.body, 70));
      emit('post', { cid: c6.cid, post: po }); return;
    }
  }

  // ---- one identity on every device: your social key is kept (encrypted with your MP account) on the broker ----
  // a/<h> retained = hex(iv || AES-GCM(PBKDF2(password, "moquil-acct:"+user), AAD "MA1", "dHex qHex")).
  // h comes from the same secret, so nobody can find or read it without the account password.
  async function acctSecret(user, pass) {
    var base = await subtle.importKey('raw', enc.encode(String(pass)), 'PBKDF2', false, ['deriveBits']);
    var bits = new Uint8Array(await subtle.deriveBits({ name: 'PBKDF2', salt: enc.encode('moquil-acct:' + String(user).toLowerCase()), iterations: 120000, hash: 'SHA-256' }, base, 256));
    return { h: hex(await sha(cat(enc.encode('moquil-acct-id:'), bits))).slice(0, 24), k: await subtle.importKey('raw', bits, 'AES-GCM', false, ['encrypt', 'decrypt']) };
  }
  function oneShot(topic, waitMs, payloadToSend) {
    return new Promise(function (res) {
      var got = null, done = false, c, wasUp = false;
      function fin() { if (done) return; done = true; try { c.close(); } catch (e) {} res({ data: got, up: wasUp }); }
      c = new Mqtt(BROKERS[bi % BROKERS.length], null, {
        up: function () {
          wasUp = true;
          if (payloadToSend !== undefined) { c.pub(T(topic), payloadToSend, true); setTimeout(fin, 600); return; }
          c.sub(T(topic)); setTimeout(fin, waitMs);
        },
        msg: function (t, data) { if (t === T(topic) && data.length) { got = data; fin(); } },
        down: function () { bi++; fin(); }
      });
      setTimeout(fin, waitMs + 6000);
    });
  }
  async function storeAcctKey(sec, d, q) {
    var iv = rnd(12);
    var ct = new Uint8Array(await subtle.encrypt({ name: 'AES-GCM', iv: iv, additionalData: enc.encode('MA1'), tagLength: 128 }, sec.k, enc.encode(d + ' ' + q)));
    if (M && M.up) pub('a/' + sec.h, hex(cat(iv, ct)), true); else await oneShot('a/' + sec.h, 0, hex(cat(iv, ct)));
  }
  var ACCT = null;
  async function useAccount(user, pass) {
    var sec = await acctSecret(user, pass); ACCT = sec;
    var rd = await oneShot('a/' + sec.h, 3500);
    if (!rd.up) rd = await oneShot('a/' + sec.h, 3500);   // the next broker
    var data = rd.data;
    if (data) {
      try {
        var raw = unhex(dec.decode(data));
        var f = dec.decode(await subtle.decrypt({ name: 'AES-GCM', iv: raw.slice(0, 12), additionalData: enc.encode('MA1'), tagLength: 128 }, sec.k, raw.slice(12))).split(' ');
        if (await idFromPub(f[1])) { localStorage.setItem('moq_key', JSON.stringify({ d: f[0], q: f[1], made: Date.now(), acct: String(user).toLowerCase() })); return { restored: true, id: await idFromPub(f[1]) }; }
      } catch (e) {}
    }
    // nothing saved yet (first login): keep this device's key and save it for the next device
    var s = null; try { s = JSON.parse(localStorage.getItem('moq_key') || 'null'); } catch (e) {}
    if (!s || !s.d) {
      var kp = await subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
      var jwk = await subtle.exportKey('jwk', kp.privateKey);
      s = { d: hex(unb64u(jwk.d)), q: '04' + hex(unb64u(jwk.x)) + hex(unb64u(jwk.y)), made: Date.now() };
    }
    s.acct = String(user).toLowerCase(); localStorage.setItem('moq_key', JSON.stringify(s));
    // only save when the broker really answered "nothing saved": never overwrite a saved key because the net was down
    if (rd.up) await storeAcctKey(sec, s.d, s.q);
    return { restored: false, id: await idFromPub(s.q) };
  }

  // ---- backup of friends / chats / communities (sealed to yourself, retained on s/<id>) ----
  var bkT = 0, bkFrom = 0;
  function backupSoon() { clearTimeout(bkT); bkT = setTimeout(backup, 8000); }
  async function backup() {
    if (!up() || !K.id) return;
    var keep = [40, 15, 3, 0], j = '';
    for (var i = 0; i < keep.length; i++) {
      var o = { v: 1, at: Date.now(), friends: S.friends, inc: S.inc, out: S.out, favs: S.favs, myRatings: S.myRatings, partyOk: S.partyOk,
        coms: S.coms.filter(function (c) { return !c.deleted; }).map(function (c) { return { cid: c.cid, name: c.name, desc: c.desc, ownerId: c.ownerId, ownerName: c.ownerName, ts: c.ts, joined: c.joined, followed: c.followed }; }),
        convs: S.convs.map(function (c) { var x = Object.assign({}, c); x.msgs = (c.msgs || []).slice(-keep[i]); return x; }) };
      j = JSON.stringify(o); if (j.length < 40000) break;
    }
    pub('s/' + K.id, await makeEnv('bk', K.id, await seal(K.pubHex, j)), true);
  }
  function mergeById(a, b, k) { var seen = {}; a.forEach(function (x) { seen[x[k]] = 1; }); (b || []).forEach(function (x) { if (x && !seen[x[k]]) { a.push(x); seen[x[k]] = 1; } }); }
  async function restoreBackup(e) {
    if (e.from !== K.id) return;
    var j = await unseal(K.pubHex, e.body); if (!j) return;
    var o; try { o = JSON.parse(j); } catch (x) { return; }
    if (!o || o.at <= bkFrom) return; bkFrom = o.at;
    mergeById(S.friends, o.friends, 'id'); mergeById(S.inc, o.inc, 'id'); mergeById(S.out, o.out, 'id');
    S.inc = S.inc.filter(function (x) { return !findIn(S.friends, x.id); }); S.out = S.out.filter(function (x) { return !findIn(S.friends, x.id); });
    (o.favs || []).forEach(function (f) { if (S.favs.indexOf(f) < 0) S.favs.push(f); });
    (o.partyOk || []).forEach(function (f) { if (S.partyOk.indexOf(f) < 0) S.partyOk.push(f); });
    Object.keys(o.myRatings || {}).forEach(function (m) { if (!S.myRatings[m]) S.myRatings[m] = o.myRatings[m]; });
    (o.coms || []).forEach(function (c) { if (!com(c.cid)) { c.posts = []; c.seen = now(); S.coms.push(c); if (comWatched(c)) comSubs(c, true); } });
    (o.convs || []).forEach(function (c) {
      var mine = conv(c.key);
      if (!mine) { c.unread = 0; S.convs.push(c); return; }
      var sig = {}; (mine.msgs || []).forEach(function (m) { sig[m.ts + '|' + m.fromId + '|' + m.text] = 1; });
      (c.msgs || []).forEach(function (m) { if (!sig[m.ts + '|' + m.fromId + '|' + m.text]) mine.msgs.push(m); });
      mine.msgs.sort(function (a, b) { return a.ts - b.ts; }); if (mine.msgs.length > CONV_KEEP) mine.msgs = mine.msgs.slice(-CONV_KEEP);
      if (c.last > mine.last) mine.last = c.last;
    });
    S.friends.forEach(function (f) { sub('p/' + f.id); sub('o/' + f.id); });
    dirty(); emit('restored');
  }

  // ---- public API ----
  function party() { for (var i = 0; i < S.convs.length; i++) if (S.convs[i].kind === 2) return S.convs[i]; return null; }
  function leaveParty() { var p = party(); if (p) API.leaveGroup(p.key); }
  var API = {
    S: S, RS: RS, on: on, emit: emit, up: up, now: now, clean: clean, cleanName: cleanName, hex: hex, unhex: unhex, rnd: rnd, sha: sha,
    makeEnv: makeEnv, parseEnv: parseEnv, pubRaw: pub, sub: sub, unsub: unsub, publishRich: publishRich,
    get id() { return K.id; }, get pubHex() { return K.pubHex; }, get broker() { return S.broker || ''; },
    start: async function (name) {
      await loadOrMakeKey(); load(); S.name = cleanName(name || '');
      S.coms.forEach(function (c) { c.deleted = false; });
      sub('m/' + K.id + '/#'); sub('s/' + K.id); sub('o/+'); sub('c/+/i'); sub('u/' + K.id);
      S.friends.forEach(function (f) { sub('p/' + f.id); });
      sub('p/' + K.id);
      S.coms.forEach(function (c) { if (comWatched(c)) comSubs(c, true); });
      connect();
      clearInterval(presT); presT = setInterval(function () { if (up()) publishPresence(); var t = now(); S.pending = S.pending.filter(function (x) { return x.until > t; }); }, PRES_EVERY * 1000);
    },
    stop: function () { if (M) M.close(T('o/' + K.id)); M = null; },
    setName: function (n) { n = cleanName(n); if (n === S.name) return; S.name = n; S.convs.forEach(function (c) { (c.members || []).forEach(function (m) { if (m.id === K.id) m.name = n; }); }); publishProfile(); publishRich(); },
    isOnline: function (id) { if (id === K.id) return up(); return S.pres[id] && now() - S.pres[id] < PRES_FRESH; },
    activity: function (id) { return API.isOnline(id) ? (S.presAct[id] || 'MP') : ''; },
    nameOf: function (id) { if (id === K.id) return S.name; var f = findIn(S.friends, id); if (f) return f.name; return S.people[id] ? S.people[id].name : ''; },
    profile: function (id) { return S.profiles[id] || null; },
    watchProfile: function (id) { sub('p/' + id); sub('o/' + id); },
    // friends
    search: function (q) {
      q = String(q || '').trim().replace(/^#/, ''); S.query = q; S.queryAt = now();
      if (/^\d{8}$/.test(q)) sub('u/' + q);
      if (!S.dirSub && q) { S.dirSub = true; sub('u/+'); }
    },
    results: function () {
      var q = (S.query || '').toLowerCase(); if (!q) return [];
      var r = Object.keys(S.people).map(function (k) { return S.people[k]; }).filter(function (p) {
        if (p.id === K.id || !p.name) return false;
        return p.id === q || (q.length >= 3 && p.id.indexOf(q) === 0) || p.name.toLowerCase().indexOf(q) >= 0;
      });
      r.sort(function (a, b) { var oa = API.isOnline(a.id) ? 1 : 0, ob = API.isOnline(b.id) ? 1 : 0; if (oa !== ob) return ob - oa; return a.name.localeCompare(b.name); });
      return r.slice(0, 60);
    },
    searching: function () { return S.query && now() - S.queryAt < 4; },
    isFriend: function (id) { return !!findIn(S.friends, id); },
    isOut: function (id) { return !!findIn(S.out, id); },
    isInc: function (id) { return !!findIn(S.inc, id); },
    addFriend: function (id) {
      if (id === K.id || API.isFriend(id) || API.isOut(id)) return false;
      if (API.isInc(id)) { API.accept(id); return true; }
      S.out.push({ id: id, name: API.nameOf(id) || id, pub: pubOf(id), since: now() });
      mail(id, 'freq', S.name); dirty(); return true;
    },
    accept: function (id) { var p = findIn(S.inc, id); if (!p) return; removeFrom(S.inc, id); if (!API.isFriend(id)) S.friends.push({ id: p.id, name: p.name, pub: p.pub, since: now() }); mail(id, 'facc', ''); doneNotifs('freq', id, ''); sub('o/' + id); sub('p/' + id); dirty(); },
    decline: function (id) { if (!findIn(S.inc, id)) return; removeFrom(S.inc, id); mail(id, 'fdec', ''); doneNotifs('freq', id, ''); dirty(); },
    cancel: function (id) { if (!findIn(S.out, id)) return; removeFrom(S.out, id); mail(id, 'fcan', ''); dirty(); },
    removeFriend: function (id) { if (!API.isFriend(id)) return; mail(id, 'frem', ''); removeFrom(S.friends, id); removeConv('u' + id); dirty(); },
    toggleFav: function (id) { var i = S.favs.indexOf(id); if (i >= 0) S.favs.splice(i, 1); else S.favs.push(id); dirty(); },
    // chats
    convs: function () { return S.convs.slice().sort(function (a, b) { return b.last - a.last; }); },
    conv: conv,
    openDm: function (id) { if (!API.isFriend(id)) return null; return makeDm(id, API.nameOf(id)); },
    markRead: function (key) { var c = conv(key); if (c && c.unread) { c.unread = 0; dirty(); } S.notifs.forEach(function (n) { if (n.kind === 'msg' && n.ref === key) n.read = true; }); },
    send: function (key, text) {
      var c = conv(key); if (!c) return false; text = clean(text, 400).trim(); if (!text) return false;
      if (c.kind === 0) { var id = key.slice(1); if (!API.isFriend(id)) return false; mail(id, 'dm', text); }
      else groupCast(c, 'gmsg', c.gid + RS + text);
      if (linked()) mail(K.id, 'dmc', key + RS + text);   // a copy for your other device (the game linked with a code)
      addMsg(c, K.id, S.name, text, now(), true); return true;
    },
    createGroup: function (name, ids) {
      var c = { gid: K.id + hex(rnd(4)), kind: 1, owner: K.id, last: now(), members: [{ id: K.id, name: S.name }], msgs: [], unread: 0 };
      c.key = 'g' + c.gid; var title = [];
      ids.forEach(function (id) { if (API.isFriend(id) && !isMember(c, id)) { c.members.push({ id: id, name: API.nameOf(id) }); title.push(API.nameOf(id)); } });
      c.title = clean(name, 40) || title.join(', ').slice(0, 30) || 'New group';
      S.convs.push(c); groupCast(c, 'ginv', groupBody(c)); addMsg(c, '', '', 'You made the group ' + c.title, now(), true); return c.key;
    },
    addToGroup: function (key, id) { var c = conv(key); if (!c || c.kind !== 1 || !API.isFriend(id) || isMember(c, id)) return; c.members.push({ id: id, name: API.nameOf(id) }); groupCast(c, 'gupd', groupBody(c), id); mail(id, 'ginv', groupBody(c)); addMsg(c, '', '', 'You added ' + API.nameOf(id), now(), true); },
    leaveGroup: function (key) { var c = conv(key); if (!c) return; if (c.kind !== 0) groupCast(c, 'glv', c.gid); removeConv(key); },
    unreadChats: function () { return S.convs.reduce(function (n, c) { return n + (c.unread || 0); }, 0); },
    // party
    party: party,
    createParty: function () { var p = party(); if (p) return p.key; var c = { gid: K.id + hex(rnd(4)), kind: 2, owner: K.id, last: now(), title: (S.name ? S.name + "'s" : 'My') + ' party', members: [{ id: K.id, name: S.name }], msgs: [], unread: 0 }; c.key = 'g' + c.gid; S.convs.push(c); addMsg(c, '', '', 'Party made. Invite up to 4 friends.', now(), true); return c.key; },
    inviteParty: function (id) { if (!API.isFriend(id)) return false; var p = party(); if (!p) { API.createParty(); p = party(); } if (p.owner !== K.id || p.members.length >= PARTY_MAX || isMember(p, id)) return false; mail(id, 'pinv', p.gid + RS + p.title); addMsg(p, '', '', 'Invited ' + API.nameOf(id), now(), true); return true; },
    acceptParty: function (gid) { var owner = ''; S.notifs.forEach(function (n) { if (n.kind === 'pinv' && n.ref === gid && !n.done) { owner = n.fromId; n.done = n.read = true; } }); if (!owner) return; var old = party(); if (old && old.gid !== gid) leaveParty(); S.partyOk.push(gid); mail(owner, 'pacc', gid); dirty(); },
    declineParty: function (gid) { S.notifs.forEach(function (n) { if (n.kind === 'pinv' && n.ref === gid && !n.done) { n.done = n.read = true; mail(n.fromId, 'pdec', gid); } }); dirty(); },
    leaveParty: leaveParty,
    // communities
    communities: function () { return S.coms.filter(function (c) { return !c.deleted && c.name; }).sort(function (a, b) { return b.ts - a.ts; }); },
    community: com,
    createCommunity: async function (name, desc) {
      name = clean(name, 40); if (!name) return '';
      var c = { cid: K.id + '-' + hex(rnd(3)), name: name, desc: clean(desc, 300), ownerId: K.id, ownerName: S.name, ts: now(), joined: true, followed: true, seen: now(), posts: [] };
      S.coms.push(c); pub('c/' + c.cid + '/i', await makeEnv('com', '', c.name + RS + c.desc), true);
      comSubs(c, true); publishMembership(c); dirty(); return c.cid;
    },
    joinCommunity: function (cid, j) { var c = com(cid); if (!c || !!c.joined === j) return; c.joined = j; if (j) c.followed = true; publishMembership(c); comSubs(c, comWatched(c)); dirty(); },
    followCommunity: function (cid, f) { var c = com(cid); if (!c || !!c.followed === f) return; c.followed = f; publishMembership(c); comSubs(c, comWatched(c)); dirty(); },
    post: async function (cid, text) {
      var c = com(cid); if (!c || !(c.joined || c.ownerId === K.id)) return false;
      text = clean(text, 600).trim(); if (!text) return false;
      var pid = now() + hex(rnd(3));
      pub('c/' + cid + '/p/' + pid, await makeEnv('cp', cid, text), true);
      c.posts = c.posts || []; c.posts.unshift({ id: pid, fromId: K.id, fromName: S.name, text: text, ts: now() }); c.seen = now(); dirty(); return true;
    },
    deleteCommunity: function (cid) { var c = com(cid); if (!c || c.ownerId !== K.id) return; (c.posts || []).forEach(function (p) { pub('c/' + cid + '/p/' + p.id, '', true); }); pub('c/' + cid + '/m/' + K.id, '', true); pub('c/' + cid + '/i', '', true); comSubs(c, false); c.deleted = true; c.joined = c.followed = false; dirty(); },
    viewCommunity: function (cid) {
      var old = com(S.viewCid); S.viewCid = cid; if (old && !comWatched(old)) comSubs(old, false);
      var c = com(cid); if (c) { comSubs(c, true); if (c.posts && c.posts[0]) c.seen = c.posts[0].ts; S.notifs.forEach(function (n) { if (n.kind === 'com' && n.ref === cid) n.read = true; }); dirty(); }
    },
    // inbox
    inbox: function () { return S.notifs.slice().sort(function (a, b) { return b.ts - a.ts; }); },
    unreadInbox: function () { return S.notifs.filter(function (n) { return !n.read; }).length; },
    markInboxRead: function () { S.notifs.forEach(function (n) { if (n.kind !== 'freq' && n.kind !== 'pinv') n.read = true; }); dirty(); },
    clearInbox: function () { S.notifs = S.notifs.filter(function (n) { return (n.kind === 'freq' || n.kind === 'pinv') && !n.done; }); dirty(); },
    // ratings
    watchRatings: function (mod) { sub('r/' + mod + '/+'); },
    rate: async function (mod, v) { S.myRatings[mod] = v; S.ratings[mod] = S.ratings[mod] || {}; S.ratings[mod][K.id] = v; pub('r/' + mod + '/' + K.id, await makeEnv('rt', mod, String(v)), true); dirty(); emit('rating', mod); },
    rating: function (mod) { var r = S.ratings[mod] || {}, k = Object.keys(r), s = 0; k.forEach(function (x) { s += r[x]; }); return { avg: k.length ? s / k.length : 0, n: k.length, mine: S.myRatings[mod] || 0 }; },
    // link with the MP game: the game shows a 10 character code and puts its key (encrypted with the code)
    // on k/<sha256("moquil-link:"+code)[0..24]>. Moquil reads it and becomes the same player ID.
    linkWithCode: function (code) {
      code = String(code || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
      return new Promise(async function (res, rej) {
        if (code.length !== 10) { rej(new Error('The code has 10 letters and digits.')); return; }
        var h = hex(await sha(enc.encode('moquil-link:' + code))).slice(0, 24);
        var kk = await subtle.importKey('raw', await sha(enc.encode('moquil-link-key:' + code)), 'AES-GCM', false, ['decrypt']);
        var done = false, to = setTimeout(function () { if (done) return; done = true; unsub('k/' + h); rej(new Error('No game found with this code. Open MP Launcher > Social > Moquil on the phone with the game and try a new code.')); }, 15000);
        on('linkcode', async function (x) {
          if (done || x.h !== h || !x.data.length) return;
          try {
            var raw = unhex(dec.decode(x.data));
            var plain = dec.decode(await subtle.decrypt({ name: 'AES-GCM', iv: raw.slice(0, 12), additionalData: enc.encode('ML1'), tagLength: 128 }, kk, raw.slice(12)));
            var f = plain.split(' ');
            var test = await idFromPub(f[1]); if (!test) throw new Error('bad key');
            done = true; clearTimeout(to); unsub('k/' + h);
            pub('k/' + h, '', true);   // used: clear it
            var oldState = localStorage.getItem('moq_state_' + K.id);
            localStorage.setItem('moq_key', JSON.stringify({ d: f[0], q: f[1], made: Date.now(), linked: 1 }));
            if (oldState && !localStorage.getItem('moq_state_' + test)) localStorage.setItem('moq_state_' + test, oldState);
            res({ id: test, name: f.slice(2).join(' ') });
          } catch (e) { done = true; clearTimeout(to); rej(new Error('The code is not right.')); }
        });
        sub('k/' + h);
      });
    },
    exportKey: function () { return K.dHex + ' ' + K.pubHex; },
    useAccount: useAccount,
    // after linking with the game: save the game's key under the account too, so the web and other phones follow
    saveToAccount: async function () { if (!ACCT) return false; var s = JSON.parse(localStorage.getItem('moq_key') || 'null'); if (!s) return false; await storeAcctKey(ACCT, s.d, s.q); return true; },
    backupNow: backup,
    restart: async function (name) { shared = {}; if (M) { var m = M; M = null; m.close(T('o/' + K.id)); } subs = {}; outQ.length = 0; S.friends = []; S.inc = []; S.out = []; S.convs = []; S.coms = []; S.notifs = []; S.favs = []; S.partyOk = []; S.myRatings = {}; S.profiles = {}; await API.start(name); }
  };
  G.Soc = API;
})(window);
