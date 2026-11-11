/* Moquil - voice rooms (WebRTC, a mesh of up to 8 people). Signalling goes over the social broker:
   v/<room>/all for "hi" / "bye", v/<room>/<id> for offer / answer / ice. Every signal is a signed envelope. */
(function (G) {
  'use strict';
  var ICE = [{ urls: ['stun:stun.l.google.com:19302', 'stun:stun1.l.google.com:19302'] }];
  var V = { room: '', name: '', peers: {}, stream: null, muted: false, deaf: false, talking: {}, listeners: [] };
  function emit() { V.listeners.forEach(function (f) { try { f(); } catch (e) {} }); }
  async function signal(to, obj) { Soc.pubRaw('v/' + V.room + '/' + to, await Soc.makeEnv('vs', to, JSON.stringify(obj)), false); }
  function peer(id, name) {
    var p = V.peers[id]; if (p) { if (name) p.name = name; return p; }
    var pc = new RTCPeerConnection({ iceServers: ICE });
    p = V.peers[id] = { id: id, name: name || Soc.nameOf(id) || id, pc: pc, audio: new Audio(), state: 'connecting' };
    p.audio.autoplay = true; p.audio.muted = V.deaf;
    if (V.stream) V.stream.getTracks().forEach(function (t) { pc.addTrack(t, V.stream); });
    pc.onicecandidate = function (e) { if (e.candidate) signal(id, { t: 'ice', c: e.candidate }); };
    pc.ontrack = function (e) { p.audio.srcObject = e.streams[0]; meter(id, e.streams[0]); };
    pc.onconnectionstatechange = function () { p.state = pc.connectionState; if (pc.connectionState === 'failed' || pc.connectionState === 'closed') drop(id); emit(); };
    emit(); return p;
  }
  function drop(id) { var p = V.peers[id]; if (!p) return; try { p.pc.close(); } catch (e) {} p.audio.srcObject = null; delete V.peers[id]; delete V.talking[id]; emit(); }
  var actx = null;
  function meter(id, stream) {
    try {
      actx = actx || new (G.AudioContext || G.webkitAudioContext)();
      var src = actx.createMediaStreamSource(stream), an = actx.createAnalyser(); an.fftSize = 256; src.connect(an);
      var buf = new Uint8Array(an.frequencyBinCount);
      var t = setInterval(function () {
        if (id !== Soc.id && !V.peers[id]) { clearInterval(t); return; }
        if (id === Soc.id && !V.stream) { clearInterval(t); return; }
        an.getByteFrequencyData(buf); var s = 0; for (var i = 0; i < buf.length; i++) s += buf[i];
        var on = s / buf.length > 18 && !(id === Soc.id && V.muted);
        if (!!V.talking[id] !== on) { V.talking[id] = on; emit(); }
      }, 150);
    } catch (e) {}
  }
  async function onVoice(x) {
    if (!V.room || x.room !== V.room || !x.data.length) return;
    var e = await Soc.parseEnv(x.data); if (!e || e.kind !== 'vs' || e.from === Soc.id) return;
    if (Date.now() / 1000 - e.ts > 60) return;
    var m; try { m = JSON.parse(e.body); } catch (z) { return; }
    if (m.t === 'hi') { if (x.to === 'all') { peer(e.from, e.name); signal(e.from, { t: 'here' }); if (Soc.id < e.from) offer(e.from); } return; }
    if (x.to !== Soc.id) return;
    if (m.t === 'here') { peer(e.from, e.name); if (Soc.id < e.from) offer(e.from); }
    else if (m.t === 'offer') { var p = peer(e.from, e.name); await p.pc.setRemoteDescription(m.sdp); var a = await p.pc.createAnswer(); await p.pc.setLocalDescription(a); signal(e.from, { t: 'answer', sdp: p.pc.localDescription }); }
    else if (m.t === 'answer') { var p2 = V.peers[e.from]; if (p2 && p2.pc.signalingState === 'have-local-offer') await p2.pc.setRemoteDescription(m.sdp); }
    else if (m.t === 'ice') { var p3 = V.peers[e.from]; if (p3) try { await p3.pc.addIceCandidate(m.c); } catch (z) {} }
    else if (m.t === 'bye') drop(e.from);
  }
  async function offer(id) {
    var p = peer(id); if (p.offered) return; p.offered = true;
    var o = await p.pc.createOffer({ offerToReceiveAudio: true }); await p.pc.setLocalDescription(o);
    signal(id, { t: 'offer', sdp: p.pc.localDescription });
  }
  var hiT = 0;
  G.MoqVoice = {
    state: V,
    onChange: function (f) { V.listeners.push(f); },
    join: async function (room, title) {
      if (V.room === room) return true;
      if (V.room) await G.MoqVoice.leave();
      V.stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true }, video: false });
      V.stream.getAudioTracks().forEach(function (t) { t.enabled = !V.muted; });
      V.room = room; V.title = title || room; meter(Soc.id, V.stream);
      Soc.sub('v/' + room + '/all'); Soc.sub('v/' + room + '/' + Soc.id);
      var hi = async function () { Soc.pubRaw('v/' + room + '/all', await Soc.makeEnv('vs', 'all', JSON.stringify({ t: 'hi' })), false); };
      hi(); clearInterval(hiT); hiT = setInterval(hi, 20000);
      emit(); return true;
    },
    leave: async function () {
      if (!V.room) return; clearInterval(hiT);
      var room = V.room;
      Object.keys(V.peers).forEach(function (id) { signal(id, { t: 'bye' }); drop(id); });
      Soc.unsub('v/' + room + '/all'); Soc.unsub('v/' + room + '/' + Soc.id);
      if (V.stream) V.stream.getTracks().forEach(function (t) { t.stop(); });
      V.stream = null; V.room = ''; V.talking = {}; emit();
    },
    mute: function (on) { V.muted = on == null ? !V.muted : !!on; if (V.stream) V.stream.getAudioTracks().forEach(function (t) { t.enabled = !V.muted; }); emit(); },
    deafen: function (on) { V.deaf = on == null ? !V.deaf : !!on; Object.keys(V.peers).forEach(function (k) { V.peers[k].audio.muted = V.deaf; }); emit(); },
    people: function () { var a = []; if (V.room) a.push({ id: Soc.id, name: Soc.S.name, me: true, talking: !!V.talking[Soc.id], state: 'connected' }); Object.keys(V.peers).forEach(function (k) { var p = V.peers[k]; a.push({ id: k, name: p.name, talking: !!V.talking[k], state: p.state }); }); return a; }
  };
  G.addEventListener('load', function () { if (G.Soc) Soc.on('voice', onVoice); });
})(window);
