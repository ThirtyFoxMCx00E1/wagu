// MP Bot (2.22): the website's management bot. Nobody types: the bot picks the next command from
// the live state (regions, beacons, server list, autopilot reports), types it into the prompt,
// presses Enter by itself and runs it - forever, in Termius and in the web Termux.
// Used as:  MPBot({ out, line, ps, keyEnter, status, run(cmd) -> Promise|void, next() -> {cmd, why} })
(function () {
  'use strict';
  function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  // 3.34: several typers at once. o.lanes = [{ ps: 'bot2@bcw:~$', next() -> job }, ...] adds one more
  // prompt row per lane under the main one; every lane types and runs its own commands in its own
  // loop, forever. Nothing piles up: each lane keeps one job at a time, errors never stop a loop,
  // and the output stays trimmed by the page (print keeps the last 600 lines).
  window.MPBot = function (o) {
    var bots = [one(o)];
    (o.lanes || []).forEach(function (l, i) {
      var row = o.line && o.line.parentNode ? o.line.parentNode : null, line = null;
      if (row && row.parentNode) {
        var r2 = row.cloneNode(true);
        r2.classList.add('lane');
        Array.prototype.forEach.call(r2.querySelectorAll('[id]'), function (e) { e.id = e.id + '-l' + (i + 1); });
        var ps = r2.querySelector('.ps'); if (ps && l.ps) ps.textContent = l.ps;
        line = r2.querySelector('.typed');
        var after = bots.length > 1 && bots[bots.length - 1].row ? bots[bots.length - 1].row : row;
        after.parentNode.insertBefore(r2, after.nextSibling);
        if (line) line.textContent = '';
        var b = one({ out: o.out, line: line || document.createElement('span'), keyEnter: r2.querySelector('.kenter'), status: null, run: l.run || o.run, next: l.next,
          typeMs: l.typeMs || (o.typeMs || 38) + 9 * (i + 1), gapMs: l.gapMs || 9000 + 1700 * i, startMs: (o.startMs || 900) + 1300 * (i + 1) });
        b.row = r2; bots.push(b);
      }
    });
    return { stop: function () { bots.forEach(function (b) { b.stop(); }); }, busy: function () { return bots.some(function (b) { return b.busy(); }); }, lanes: bots.length };
  };
  function one(o) {
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
    // 3.34: the loop never dies - an exception restarts it after a short rest
    (function guard() { loop().catch(function () { busy = false; if (running) setTimeout(guard, 3000); }); })();
    return { stop: function () { running = false; }, busy: function () { return busy; } };
  }

  // 3.31: the Debian 12 one-line 24/7 VPS setup. Every console bot (Termius, Termux, Railway) types it
  // at start-up and again every few checks, going round the regions: asia, europe, na, oceania.
  window.MPDebian = (function () {
    var URL = 'https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/vps-setup.sh';
    var REG = ['asia', 'europe', 'na', 'oceania'];
    var INFO = { europe: ['Europe', 'eusamp', 'Frankfurt'], na: ['NA', 'nasamp', 'Virginia / Ohio'], oceania: ['Oceania', 'ocsamp', 'Sydney'], asia: ['Asia', 'mpsamp', 'Singapore / Manila'] };
    var n = 0, ri = 0;
    function cmd(r) { return 'wget -qO- ' + URL + ' | bash -s ' + r; }
    return {
      cmd: cmd,
      // the region of a typed Debian setup line, or null
      match: function (line) { var m = /^wget -qO- \S*vps-setup\.sh \| bash -s (\w+)\s*$/.exec(String(line || '').trim()); return m && INFO[m[1].toLowerCase()] ? m[1].toLowerCase() : null; },
      // what the command does: [class, text] lines for the page's p()
      lines: function (r) {
        var i = INFO[r] || INFO.europe;
        return [
          ['a', 'Debian 12 / Ubuntu: a new 24/7 ' + i[0] + ' server (HiveMQ + Java 21 + Mega Vil Futuristic) - on the machine as root (su -):'],
          ['g', '  ' + cmd(r)],
          ['m', '  installs curl, git, Java 21, HiveMQ and the 24/7 servers, and uses the passwords from regions.txt (phones connect with no change)'],
          ['m', '  first: duckdns.org -> point ' + i[1] + ' at the machine\'s IP · open TCP 1883 8883 8884 + UDP 1922 1932'],
          ['m', '  Debian 12 installer (677 MB): github.com/ThirtyFoxMCx00E1/HiveMQ/releases/tag/Derbian - on a VPS pick Debian 12 in the image list'],
          ['m', '  low ping for ' + i[0] + ' players: put this machine in or near ' + i[2]]
        ];
      },
      // planner: a job on the first check after start-up, then every 8 checks
      tick: function () {
        n++;
        if (n !== 1 && n % 8 !== 0) return null;
        var r = REG[ri++ % REG.length];
        return { cmd: cmd(r), why: 'Debian 12 one-line setup for ' + INFO[r][0] };
      }
    };
  })();

  // keep the log glued to the bottom unless the reader scrolled up
  window.MPAutoScroll = function (out) {
    var stick = true;
    out.addEventListener('scroll', function () { stick = out.scrollHeight - out.scrollTop - out.clientHeight < 40; });
    return function () { if (stick) out.scrollTop = out.scrollHeight; };
  };
})();
