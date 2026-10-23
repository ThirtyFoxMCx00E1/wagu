// MP Bot (2.22): the website's management bot. Nobody types: the bot picks the next command from
// the live state (regions, beacons, server list, autopilot reports), types it into the prompt,
// presses Enter by itself and runs it - forever, in Termius and in the web Termux.
// Used as:  MPBot({ out, line, ps, keyEnter, status, run(cmd) -> Promise|void, next() -> {cmd, why} })
(function () {
  'use strict';
  function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  window.MPBot = function (o) {
    var typeMs = o.typeMs || 38, gapMs = o.gapMs || 5200, running = true, busy = false;
    function setStatus(t) { if (o.status) o.status.textContent = t; }
    function typeInto(cmd) {
      var i = 0;
      return new Promise(function (res) {
        (function step() {
          if (i > cmd.length) return res();
          o.line.textContent = cmd.slice(0, i++);
          setTimeout(step, typeMs + Math.random() * 30);
        })();
      });
    }
    function pressEnter() {
      if (!o.keyEnter) return;
      o.keyEnter.classList.add('hit');
      setTimeout(function () { o.keyEnter.classList.remove('hit'); }, 260);
    }
    async function loop() {
      await sleep(o.startMs || 900);
      while (running) {
        var job = null;
        try { job = o.next(); } catch (e) { job = null; }
        if (!job || !job.cmd) { await sleep(1500); continue; }
        busy = true;
        setStatus('bot: ' + (job.why || 'routine check'));
        await typeInto(job.cmd);
        await sleep(260);
        pressEnter();
        o.line.textContent = '';
        try { await Promise.resolve(o.run(job.cmd, job)); } catch (e) { /* the command printed its own error */ }
        busy = false;
        setStatus('bot: waiting · next check soon');
        // hidden tab: slow down (the browser throttles timers anyway)
        await sleep(document.hidden ? gapMs * 4 : (job.wait || gapMs) + Math.random() * 1500);
      }
    }
    loop();
    return { stop: function () { running = false; }, busy: function () { return busy; } };
  };

  // keep the log glued to the bottom unless the reader scrolled up
  window.MPAutoScroll = function (out) {
    var stick = true;
    out.addEventListener('scroll', function () { stick = out.scrollHeight - out.scrollTop - out.clientHeight < 40; });
    return function () { if (stick) out.scrollTop = out.scrollHeight; };
  };
})();
