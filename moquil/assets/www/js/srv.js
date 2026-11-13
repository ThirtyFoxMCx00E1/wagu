/* Moquil 1.4 (MP 4.13.18) - server features, modeled on Discord.Net 3.19 (research/v3.19.0 source code.zip):
   roles + permissions (GuildPermission: Administrator, ManageGuild, ManageChannels, ManageRoles, KickMembers,
   BanMembers, ModerateMembers, ManageMessages, PinMessages, BypassSlowmode, SendPolls, MentionEveryone,
   ManageEvents, SendMessages, AddReactions), channels (text / announcement / voice, topic, slowmode),
   scheduled events with RSVP, polls, reactions, replies, edits, pins, timeouts, kicks, bans, AutoMod keywords,
   role member counts and members grouped by hoisted role.

   Everything is signed (soc.js envelopes) and checked on every phone:
     c/<cid>/cfg     only the owner's signature counts
     c/<cid>/x/<id>  a moderator's actions; each action only counts when that member's role has the permission
     c/<cid>/e/<id>  that member's own reactions / votes / RSVPs
   There is no central server, so moderation hides things for everybody running Moquil (like Discord's clients do). */
(function (G) {
  'use strict';
  var PERMS = [
    ['admin', 'Administrator', 'Every permission and every channel. Give it with care.'],
    ['manage_server', 'Manage Server', 'Change the server name, description, rules and AutoMod.'],
    ['manage_channels', 'Manage Channels', 'Create, edit and delete channels, set slowmode.'],
    ['manage_roles', 'Manage Roles', 'Create and edit roles below their own, give them to members.'],
    ['manage_events', 'Manage Events', 'Create, edit and cancel scheduled events.'],
    ['kick', 'Kick Members', 'Remove a member (they can join again with an invite).'],
    ['ban', 'Ban Members', 'Remove a member for good and hide their posts.'],
    ['moderate', 'Timeout Members', 'Stop a member from posting, reacting and voting for a while.'],
    ['manage_messages', 'Manage Messages', 'Delete other members\' posts and post in announcement channels.'],
    ['pin', 'Pin Messages', 'Pin and unpin posts (Discord 3.19: its own permission).'],
    ['bypass_slow', 'Bypass Slowmode', 'Not slowed down by channel slowmode (Discord 3.19).'],
    ['mention_everyone', 'Mention @everyone', '@everyone and @here ping every member.'],
    ['send', 'Send Messages', 'Post in text channels.'],
    ['polls', 'Create Polls', 'Post polls.'],
    ['react', 'Add Reactions', 'React to posts.']
  ];
  var DEF_EVERYONE = ['send', 'polls', 'react'];
  var ROLE_COLORS = ['#f23f42', '#f0b232', '#23a55a', '#00a8fc', '#5865f2', '#eb459e', '#9b59b6', '#1abc9c', '#e67e22', '#95a5a6'];
  var REACTS = ['\uD83D\uDC4D', '\u2764\uFE0F', '\uD83D\uDE02', '\uD83D\uDD25', '\uD83D\uDE2E', '\uD83D\uDE22', '\uD83C\uDF89', '\uD83D\uDC4E'];
  var DAY = 86400;

  var X = {};   // cid -> { cfg, cfgTs, mods: {id: {d, ts}}, me: {id: {d, ts}} }
  function st(cid) { return X[cid] || (X[cid] = { cfg: null, cfgTs: 0, mods: {}, rx: {} }); }
  function S() { return G.Soc; }
  function now() { return Math.floor(Date.now() / 1000); }
  function clean(s, n) { return S().clean(String(s == null ? '' : s), n); }
  function cleanML(s, n) { return String(s == null ? '' : s).replace(/\r/g, '').split('\n').slice(0, 40).map(function (l) { return clean(l, n); }).join('\n').slice(0, n); }   // keeps line breaks (rules, event text)
  function rid() { return S().hex(S().rnd(4)); }
  function com(cid) { return S().community(cid); }

  function defaultCfg() {
    return { v: 1, roles: [{ id: 'everyone', name: '@everyone', color: '', perms: DEF_EVERYONE.slice(), hoist: false }],
      assign: {}, chans: [{ id: 'general', name: 'general', kind: 'text', topic: '', slow: 0 }, { id: 'lounge', name: 'Lounge', kind: 'voice' }],
      events: [], rules: '', automod: [] };
  }
  function sanitize(o) {
    var d = defaultCfg(); if (!o || typeof o !== 'object') return d;
    var roles = Array.isArray(o.roles) ? o.roles.slice(0, 30) : [];
    var ok = {}; PERMS.forEach(function (p) { ok[p[0]] = 1; });
    d.roles = [];
    roles.forEach(function (r) {
      if (!r || !/^[a-z0-9]{1,12}$/.test(r.id || '') || d.roles.some(function (x) { return x.id === r.id; })) return;
      d.roles.push({ id: r.id, name: r.id === 'everyone' ? '@everyone' : clean(r.name, 32) || 'role', color: /^#[0-9a-f]{6}$/i.test(r.color || '') ? r.color : '',
        perms: (Array.isArray(r.perms) ? r.perms : []).filter(function (p) { return ok[p]; }), hoist: !!r.hoist });
    });
    if (!d.roles.some(function (r) { return r.id === 'everyone'; })) d.roles.push({ id: 'everyone', name: '@everyone', color: '', perms: DEF_EVERYONE.slice(), hoist: false });
    d.assign = {};
    if (o.assign && typeof o.assign === 'object') Object.keys(o.assign).slice(0, 500).forEach(function (id) {
      if (/^\d{8}$/.test(id) && Array.isArray(o.assign[id])) d.assign[id] = o.assign[id].filter(function (r) { return r !== 'everyone' && d.roles.some(function (x) { return x.id === r; }); }).slice(0, 10);
    });
    var ch = Array.isArray(o.chans) ? o.chans.slice(0, 40) : [];
    var chans = [];
    ch.forEach(function (c) {
      if (!c || !/^[a-z0-9-]{1,24}$/.test(c.id || '') || chans.some(function (x) { return x.id === c.id; })) return;
      var kind = c.kind === 'voice' ? 'voice' : c.kind === 'news' ? 'news' : 'text';
      chans.push({ id: c.id, name: kind === 'voice' ? clean(c.name, 24) || c.id : chanName(c.name || c.id), kind: kind, topic: clean(c.topic, 300), slow: Math.max(0, Math.min(21600, +c.slow || 0)) });
    });
    if (!chans.some(function (c) { return c.id === 'general'; })) chans.unshift({ id: 'general', name: 'general', kind: 'text', topic: '', slow: 0 });
    if (!chans.some(function (c) { return c.kind === 'voice'; })) chans.push({ id: 'lounge', name: 'Lounge', kind: 'voice' });
    d.chans = chans;
    d.events = (Array.isArray(o.events) ? o.events.slice(0, 20) : []).filter(function (e) { return e && /^[0-9a-f]{8}$/.test(e.id || '') && +e.at > 0; }).map(function (e) {
      return { id: e.id, title: clean(e.title, 100) || 'Event', desc: cleanML(e.desc, 600), at: +e.at, dur: Math.max(0, Math.min(7 * DAY, +e.dur || 3600)), where: clean(e.where, 60), by: /^\d{8}$/.test(e.by || '') ? e.by : '' };
    });
    d.rules = cleanML(o.rules, 1500);
    d.automod = (Array.isArray(o.automod) ? o.automod : []).map(function (w) { return clean(w, 40).toLowerCase().trim(); }).filter(Boolean).slice(0, 60);
    return d;
  }
  function chanName(n) { return String(n || '').toLowerCase().trim().replace(/\s+/g, '-').replace(/[^a-z0-9-]/g, '').slice(0, 24) || 'channel'; }

  function cfg(cid) { var s = st(cid); return s.cfg || defaultCfg(); }
  function isOwner(cid, id) { var c = com(cid); return !!c && c.ownerId === id; }
  function rolesOf(cid, id) { var c = cfg(cid); return ['everyone'].concat(c.assign[id] || []); }
  function permsOf(cid, id) {
    if (isOwner(cid, id)) return { all: true };
    var c = cfg(cid), set = {};
    rolesOf(cid, id).forEach(function (rid) { c.roles.forEach(function (r) { if (r.id === rid) r.perms.forEach(function (p) { set[p] = 1; }); }); });
    if (set.admin) set.all = true;
    return set;
  }
  function can(cid, id, perm) { var p = permsOf(cid, id); return !!(p.all || p[perm]); }
  // role hierarchy: the order in cfg.roles (top = highest); the owner is above everybody
  function rank(cid, id) {
    if (isOwner(cid, id)) return -1;
    var c = cfg(cid), best = 999, mine = c.assign[id] || [];
    c.roles.forEach(function (r, i) { if (mine.indexOf(r.id) >= 0 && i < best) best = i; });
    return best;
  }
  function above(cid, a, b) { return rank(cid, a) < rank(cid, b); }
  function topRole(cid, id) {
    var c = cfg(cid), mine = c.assign[id] || [];
    for (var i = 0; i < c.roles.length; i++) if (mine.indexOf(c.roles[i].id) >= 0) return c.roles[i];
    return null;
  }
  function colorOf(cid, id) { var c = cfg(cid), mine = c.assign[id] || []; for (var i = 0; i < c.roles.length; i++) if (mine.indexOf(c.roles[i].id) >= 0 && c.roles[i].color) return c.roles[i].color; return ''; }

  // ---- moderation state: newest valid action per target wins ----
  // a mod record: { ban: {id: [on, ts, reason]}, kick: {id: ts}, to: {id: until}, del: {pid: ts}, pin: {pid: [on, ts]} }
  function modState(cid) {
    var s = st(cid), out = { ban: {}, kick: {}, to: {}, del: {}, pin: {} };
    var pick = function (map, key, val, ts) { if (!map[key] || map[key].ts < ts) map[key] = { v: val, ts: ts }; };
    Object.keys(s.mods).forEach(function (mid) {
      var r = s.mods[mid].d || {};
      var ok = function (perm) { return can(cid, mid, perm); };
      if (ok('ban') && r.ban) Object.keys(r.ban).forEach(function (t) { var b = r.ban[t]; if (Array.isArray(b) && !isOwner(cid, t) && above(cid, mid, t)) pick(out.ban, t, { on: !!b[0], why: clean(b[2], 120), by: mid }, +b[1] || 0); });
      if (ok('kick') && r.kick) Object.keys(r.kick).forEach(function (t) { if (!isOwner(cid, t) && above(cid, mid, t)) pick(out.kick, t, { by: mid }, +r.kick[t] || 0); });
      if (ok('moderate') && r.to) Object.keys(r.to).forEach(function (t) { var v = r.to[t]; if (Array.isArray(v) && !isOwner(cid, t) && above(cid, mid, t)) pick(out.to, t, { until: +v[0] || 0, by: mid }, +v[1] || 0); });
      if (ok('manage_messages') && r.del) Object.keys(r.del).forEach(function (pid) { pick(out.del, pid, { by: mid }, +r.del[pid] || 0); });
      if (ok('pin') && r.pin) Object.keys(r.pin).forEach(function (pid) { var v = r.pin[pid]; if (Array.isArray(v)) pick(out.pin, pid, { on: !!v[0], by: mid }, +v[1] || 0); });
    });
    return out;
  }
  function banned(cid, id) { var b = modState(cid).ban[id]; return !!(b && b.v.on); }
  function timedOut(cid, id) { var t = modState(cid).to[id]; return t && t.v.until > now() ? t.v.until : 0; }
  function kickedAt(cid, id) { var k = modState(cid).kick[id]; return k ? k.ts : 0; }
  function automodHit(cid, text) {
    var w = cfg(cid).automod, t = String(text || '').toLowerCase();
    for (var i = 0; i < w.length; i++) if (w[i] && t.indexOf(w[i]) >= 0) return w[i];
    return '';
  }
  function hidden(cid, po) {
    if (!po) return true;
    var m = modState(cid);
    if (m.del[po.id]) return true;
    if (m.ban[po.fromId] && m.ban[po.fromId].v.on) return true;
    var to = m.to[po.fromId]; if (to && po.ts >= to.ts && po.ts < to.v.until) return true;   // posted while timed out
    if (!isOwner(cid, po.fromId) && !can(cid, po.fromId, 'manage_messages') && automodHit(cid, po.text + ' ' + (po.poll ? po.poll.q + ' ' + po.poll.o.join(' ') : ''))) return true;
    return false;
  }
  function pinned(cid, pid) { var p = modState(cid).pin[pid]; return !!(p && p.v.on); }

  // ---- members: joined, not banned, not kicked after they joined; grouped by hoisted role ----
  function members(cid) {
    var c = com(cid); if (!c) return [];
    var ts = (S().S.comMemTs || {})[cid] || {};
    return (c.memberList || []).filter(function (m) {
      if (banned(cid, m.id)) return false;
      var k = kickedAt(cid, m.id); if (k && (ts[m.id] || 0) <= k) return false;
      return true;
    });
  }
  function memberGroups(cid) {
    var c = cfg(cid), list = members(cid), groups = [], used = {};
    c.roles.forEach(function (r) {
      if (!r.hoist || r.id === 'everyone') return;
      var g = list.filter(function (m) { return !used[m.id] && (c.assign[m.id] || []).indexOf(r.id) >= 0; });
      g.forEach(function (m) { used[m.id] = 1; });
      if (g.length) groups.push({ role: r, list: g });
    });
    var rest = list.filter(function (m) { return !used[m.id]; });
    if (rest.length) groups.push({ role: { id: 'everyone', name: 'Members', color: '' }, list: rest });
    return groups;
  }
  function roleCounts(cid) {   // Discord.Net 3.19: GetRoleUserCounts
    var c = cfg(cid), list = members(cid), out = {};
    c.roles.forEach(function (r) { out[r.id] = r.id === 'everyone' ? list.length : list.filter(function (m) { return (c.assign[m.id] || []).indexOf(r.id) >= 0; }).length; });
    return out;
  }

  // ---- reactions, votes, RSVPs (each member's own record) ----
  function rx(cid) { return st(cid).rx; }
  function reactions(cid, pid) {
    var out = {}, R = rx(cid);
    Object.keys(R).forEach(function (id) {
      if (banned(cid, id)) return;
      var r = (R[id].d.r || {})[pid]; if (!Array.isArray(r)) return;
      r.slice(0, 8).forEach(function (e) { if (REACTS.indexOf(e) < 0) return; (out[e] = out[e] || []).push(id); });
    });
    return out;
  }
  function votes(cid, pid) {
    var out = {}, R = rx(cid);
    Object.keys(R).forEach(function (id) {
      if (banned(cid, id)) return;
      var v = (R[id].d.v || {})[pid]; if (!Array.isArray(v)) return;
      v.slice(0, 10).forEach(function (i) { i = +i; if (i >= 0 && i < 10) (out[i] = out[i] || []).push(id); });
    });
    return out;
  }
  function rsvps(cid, eid) { var R = rx(cid); return Object.keys(R).filter(function (id) { return (R[id].d.ev || {})[eid] && !banned(cid, id); }); }
  function myRec(cid) { var r = rx(cid)[S().id]; return r ? JSON.parse(JSON.stringify(r.d)) : { r: {}, v: {}, ev: {} }; }
  var pubT = {};
  function saveMyRec(cid, d) {
    d.r = d.r || {}; d.v = d.v || {}; d.ev = d.ev || {};
    ['r', 'v'].forEach(function (k) { var keys = Object.keys(d[k]).sort(); while (keys.length > 150) delete d[k][keys.shift()]; });
    rx(cid)[S().id] = { d: d, ts: now() };
    clearTimeout(pubT[cid]);
    pubT[cid] = setTimeout(async function () { S().pubRaw('c/' + cid + '/e/' + S().id, await S().makeEnv('cre', cid, JSON.stringify(d)), true); }, 600);
    S().emit('change');
  }

  // ---- my moderator record ----
  function myMod(cid) { var r = st(cid).mods[S().id]; return r ? JSON.parse(JSON.stringify(r.d)) : {}; }
  async function saveMyMod(cid, d) {
    ['ban', 'kick', 'to', 'del', 'pin'].forEach(function (k) { if (!d[k]) return; var keys = Object.keys(d[k]); if (keys.length > 200) keys.slice(0, keys.length - 200).forEach(function (x) { delete d[k][x]; }); });
    st(cid).mods[S().id] = { d: d, ts: now() };
    S().pubRaw('c/' + cid + '/x/' + S().id, await S().makeEnv('cmx', cid, JSON.stringify(d)), true);
    S().emit('change');
  }

  async function saveCfg(cid, c) {
    if (!isOwner(cid, S().id)) return false;
    c = sanitize(c); var s = st(cid); s.cfg = c; s.cfgTs = now();
    var body = JSON.stringify(c);
    if (body.length > 48000) return false;
    S().pubRaw('c/' + cid + '/cfg', await S().makeEnv('ccf', cid, body), true);
    S().emit('change'); return true;
  }

  var API = {
    PERMS: PERMS, REACTS: REACTS, ROLE_COLORS: ROLE_COLORS,
    onTopic: function (p, e) {
      var cid = p[1], s = st(cid);
      if (p[2] === 'cfg') {
        if (!e) { s.cfg = null; s.cfgTs = 0; return; }
        if (e.kind !== 'ccf' || e.to !== cid || cid.indexOf(e.from + '-') !== 0 || e.ts < s.cfgTs) return;
        try { s.cfg = sanitize(JSON.parse(e.body)); s.cfgTs = e.ts; } catch (x) {}
        return;
      }
      var who = p[3];
      if (p[2] === 'x') {
        if (!e) { delete s.mods[who]; return; }
        if (e.kind !== 'cmx' || e.to !== cid || e.from !== who) return;
        try { var d = JSON.parse(e.body); if (d && typeof d === 'object' && (!s.mods[who] || s.mods[who].ts <= e.ts)) s.mods[who] = { d: d, ts: e.ts }; } catch (x) {}
        return;
      }
      if (p[2] === 'e') {
        if (!e) { delete s.rx[who]; return; }
        if (e.kind !== 'cre' || e.to !== cid || e.from !== who) return;
        try { var d2 = JSON.parse(e.body); if (d2 && typeof d2 === 'object' && (!s.rx[who] || s.rx[who].ts <= e.ts)) s.rx[who] = { d: d2, ts: e.ts }; } catch (x) {}
      }
    },
    cfg: cfg, sanitize: sanitize, defaultCfg: defaultCfg, chanName: chanName, saveCfg: saveCfg, rid: rid,
    can: function (cid, perm, id) { return can(cid, id || S().id, perm); }, permsOf: permsOf, isOwner: isOwner, above: above,
    rolesOf: rolesOf, topRole: topRole, colorOf: colorOf, members: members, memberGroups: memberGroups, roleCounts: roleCounts,
    hidden: hidden, pinned: pinned, banned: banned, timedOut: timedOut, automodHit: automodHit,
    bans: function (cid) { var b = modState(cid).ban; return Object.keys(b).filter(function (id) { return b[id].v.on; }).map(function (id) { return { id: id, why: b[id].v.why, by: b[id].v.by, ts: b[id].ts }; }); },
    textChans: function (cid) { return cfg(cid).chans.filter(function (c) { return c.kind !== 'voice'; }); },
    voiceChans: function (cid) { return cfg(cid).chans.filter(function (c) { return c.kind === 'voice'; }); },
    chan: function (cid, ch) { return cfg(cid).chans.filter(function (c) { return c.id === ch; })[0] || cfg(cid).chans[0]; },
    voiceRoom: function (cid, ch) { return ch === 'lounge' ? 'c' + cid : 'c' + cid + '~' + ch; },   // Lounge keeps the old room (the game uses it)
    posts: function (cid, ch) { var c = com(cid); if (!c) return []; return (c.posts || []).filter(function (p) { return (p.ch || 'general') === ch && !hidden(cid, p); }); },
    post: function (cid, pid) { var c = com(cid); return c ? (c.posts || []).filter(function (p) { return p.id === pid; })[0] || null : null; },
    pins: function (cid, ch) { return API.posts(cid, ch).filter(function (p) { return pinned(cid, p.id); }); },
    // why I can't post right now in this channel ('' = I can)
    blockReason: function (cid, ch) {
      var me = S().id, c = com(cid); if (!c) return 'No server';
      if (!(c.joined || c.ownerId === me)) return 'Join the server to post.';
      if (banned(cid, me)) return 'You are banned from this server.';
      var to = timedOut(cid, me); if (to) return 'You are timed out until ' + new Date(to * 1000).toLocaleString() + '.';
      var k = API.chan(cid, ch);
      if (k.kind === 'news' && !can(cid, me, 'manage_messages')) return 'Only moderators post in this announcement channel. You can react.';
      if (!can(cid, me, 'send')) return 'Your roles cannot send messages here.';
      return '';
    },
    slowWait: function (cid, ch) {
      var k = API.chan(cid, ch), me = S().id; if (!k.slow || can(cid, me, 'bypass_slow') || can(cid, me, 'manage_channels')) return 0;
      var mine = API.posts(cid, ch).filter(function (p) { return p.fromId === me; })[0];
      return mine ? Math.max(0, mine.ts + k.slow - now()) : 0;
    },
    reactions: reactions, votes: votes, rsvps: rsvps,
    react: function (cid, pid, emo) {
      if (!can(cid, S().id, 'react') || timedOut(cid, S().id) || REACTS.indexOf(emo) < 0) return false;
      var d = myRec(cid), r = d.r[pid] = d.r[pid] || [], i = r.indexOf(emo);
      if (i >= 0) r.splice(i, 1); else r.push(emo);
      if (!r.length) delete d.r[pid];
      saveMyRec(cid, d); return true;
    },
    vote: function (cid, pid, idx) {
      var po = API.post(cid, pid); if (!po || !po.poll || timedOut(cid, S().id)) return false;
      if (po.poll.end && po.poll.end < now()) return false;
      var d = myRec(cid), v = d.v[pid] = d.v[pid] || [], i = v.indexOf(idx);
      if (i >= 0) v.splice(i, 1); else if (po.poll.multi) v.push(idx); else d.v[pid] = [idx];
      if (!d.v[pid].length) delete d.v[pid];
      saveMyRec(cid, d); return true;
    },
    myVotes: function (cid, pid) { return (myRec(cid).v[pid] || []).slice(); },
    rsvp: function (cid, eid) { var d = myRec(cid); if (d.ev[eid]) delete d.ev[eid]; else d.ev[eid] = 1; saveMyRec(cid, d); },
    going: function (cid, eid) { return !!myRec(cid).ev[eid]; },
    // moderator actions (each checks the permission and the role hierarchy here too)
    mod: async function (cid, act, target, a2) {
      var me = S().id, d = myMod(cid), t = now();
      if (act === 'del') { if (!can(cid, me, 'manage_messages')) return false; d.del = d.del || {}; d.del[target] = t; }
      else if (act === 'pin' || act === 'unpin') { if (!can(cid, me, 'pin')) return false; d.pin = d.pin || {}; d.pin[target] = [act === 'pin' ? 1 : 0, t]; }
      else {
        if (target === me || isOwner(cid, target) || !above(cid, me, target)) return false;
        if (act === 'kick') { if (!can(cid, me, 'kick')) return false; d.kick = d.kick || {}; d.kick[target] = t; }
        else if (act === 'ban' || act === 'unban') { if (!can(cid, me, 'ban')) return false; d.ban = d.ban || {}; d.ban[target] = [act === 'ban' ? 1 : 0, t, a2 || '']; }
        else if (act === 'timeout') { if (!can(cid, me, 'moderate')) return false; d.to = d.to || {}; d.to[target] = [a2 > 0 ? t + a2 : 0, t]; }
        else return false;
      }
      await saveMyMod(cid, d); return true;
    },
    // roles: owner (or Manage Roles below their own top role, applied through the owner's next save is not possible
    // without the owner's key, so role edits are owner-only; Manage Roles members can give / take lower roles)
    setRoles: async function (cid, uid, roles) {
      var c = JSON.parse(JSON.stringify(cfg(cid)));
      c.assign[uid] = roles.filter(function (r) { return r !== 'everyone'; });
      if (!c.assign[uid].length) delete c.assign[uid];
      return saveCfg(cid, c);
    },
    events: function (cid) { return cfg(cid).events.filter(function (e) { return e.at + e.dur > now() - 3600; }).sort(function (a, b) { return a.at - b.at; }); },
    eventLive: function (e) { var t = now(); return e.at <= t && t < e.at + e.dur; },
    deleteServerData: function (cid) { var me = S().id; S().pubRaw('c/' + cid + '/cfg', '', true); S().pubRaw('c/' + cid + '/x/' + me, '', true); S().pubRaw('c/' + cid + '/e/' + me, '', true); delete X[cid]; }
  };
  G.MoqSrv = API;
})(window);
