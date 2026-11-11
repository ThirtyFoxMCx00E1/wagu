// Moquil: global_click.mp3 on every tap (same click as the MP mod and MP Textdraw Editor). WebAudio, so fast taps don't cut it.
(function () {
  var ctx = null, buf = null, loading = false;
  function load() {
    if (loading) return; loading = true;
    try {
      ctx = new (window.AudioContext || window.webkitAudioContext)();
      var x = new XMLHttpRequest();   // XHR, not fetch: the app loads file:///android_asset/www
      x.open('GET', 'global_click.mp3'); x.responseType = 'arraybuffer';
      x.onload = function () { if (x.response) ctx.decodeAudioData(x.response, function (b) { buf = b; }, function () {}); };
      x.send();
    } catch (e) {}
  }
  var fallback = null;
  function play() {
    if (ctx && buf) {
      if (ctx.state === 'suspended') ctx.resume();
      var s = ctx.createBufferSource(), g = ctx.createGain(); g.gain.value = 0.8;
      s.buffer = buf; s.connect(g); g.connect(ctx.destination); s.start(0); return;
    }
    try { if (!fallback) fallback = new Audio('global_click.mp3'); fallback.currentTime = 0; fallback.play(); } catch (e) {}
  }
  var SEL = 'button, .btn, a, .ibtn, .li, .tab, .ni, .nav-i, .mini, .tile, .guild, .sw, .sw-opt, .sitem, .ch, select, input[type=checkbox], input[type=radio], [data-click]';
  document.addEventListener('pointerdown', function (e) {
    load();
    var t = e.target && e.target.closest ? e.target.closest(SEL) : null;
    var st = null; try { st = JSON.parse(localStorage.getItem('moq_settings') || 'null'); } catch (x) {}
    if (st && st.sounds === false) return;
    if (t && !t.disabled) play();
  }, true);
})();
