// MP Oracle (3.32): saves the Oracle API key as encrypted HiveMQ Actions secrets and runs "MP Oracle claim".
// Everything happens between this browser and api.github.com. Secrets are sealed with the repo's
// public key (libsodium crypto_box_seal, vendor/mp-seal-1.0.min.js) before they are sent.
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var API = 'https://api.github.com';
  var WF = 'oracle-claim.yml';
  var cfg = {};

  // ---- small helpers ----------------------------------------------------------------------
  function log(msg, cls) {
    var d = document.createElement('div');
    if (cls) d.className = cls;
    d.textContent = new Date().toTimeString().slice(0, 8) + '  ' + msg;
    $('log').appendChild(d);
    $('log').scrollTop = $('log').scrollHeight;
  }
  function st(id, msg, cls) { var e = $(id); e.textContent = msg; e.className = 'st' + (cls ? ' ' + cls : ''); }
  function live(msg, cls) { $('live-t').textContent = msg; $('live').className = 'live' + (cls ? ' ' + cls : ''); }
  function repo() { return $('repo').value.trim().replace(/^https?:\/\/github\.com\//, '').replace(/\/+$/, ''); }
  function token() { return $('tok').value.trim(); }
  function region() { var r = document.querySelector('input[name=rg]:checked'); return r ? r.value : ''; }
  function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  function b64(u8) { var s = ''; for (var i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i]); return btoa(s); }
  function unb64(s) { var b = atob(s), u = new Uint8Array(b.length); for (var i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; }

  function gh(method, path, body) {
    var h = { 'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28' };
    if (token()) h.Authorization = 'Bearer ' + token();
    if (body) h['Content-Type'] = 'application/json';
    return fetch(API + path, { method: method, headers: h, body: body ? JSON.stringify(body) : undefined })
      .then(function (r) {
        return r.text().then(function (t) {
          var j = null; try { j = t ? JSON.parse(t) : null; } catch (e) { j = null; }
          return { ok: r.ok, status: r.status, data: j };
        });
      });
  }
  function need(res, what) {
    if (res.ok) return res;
    var why = res.data && res.data.message ? res.data.message : 'HTTP ' + res.status;
    if (res.status === 403 || res.status === 404) why += ' - the token needs Secrets, Variables and Actions = Read and write on ' + repo();
    throw new Error(what + ': ' + why);
  }

  // ---- 1. token ---------------------------------------------------------------------------
  try {
    var saved = sessionStorage.getItem('mpOracleTok');
    if (saved) { $('tok').value = saved; $('remember').checked = true; }
  } catch (e) {}
  function keepToken() {
    try { if ($('remember').checked && token()) sessionStorage.setItem('mpOracleTok', token()); else sessionStorage.removeItem('mpOracleTok'); } catch (e) {}
  }
  $('remember').addEventListener('change', keepToken);

  async function checkLogin() {
    if (!token()) { st('s-login', 'Paste the token first.', 'warn'); return false; }
    keepToken();
    st('s-login', 'Checking...');
    var r = await gh('GET', '/repos/' + repo());
    if (!r.ok) { st('s-login', 'Token or repo not accepted (' + (r.data && r.data.message || r.status) + ').', 'bad'); live('token refused', 'bad'); return false; }
    var k = await gh('GET', '/repos/' + repo() + '/actions/secrets/public-key');
    if (!k.ok) { st('s-login', 'Repo found, but the token cannot write Actions secrets (Secrets = Read and write).', 'bad'); live('no secrets access', 'warn'); return false; }
    st('s-login', 'OK: ' + r.data.full_name + ' - secrets can be saved.', 'ok');
    live(r.data.full_name, 'ok');
    return true;
  }
  $('b-login').addEventListener('click', function () { checkLogin().catch(function (e) { st('s-login', e.message, 'bad'); }); });

  // ---- 2. Oracle config + key -------------------------------------------------------------
  var REG_MAP = function (r) {
    r = (r || '').toLowerCase();
    if (/^(eu|uk|me|af|il)-/.test(r)) return 'europe';
    if (/^(us|ca|mx|sa)-/.test(r)) return 'na';
    if (r === 'ap-sydney-1' || r === 'ap-melbourne-1' || r.indexOf('ap-auckland') === 0) return 'oceania';
    return r ? 'asia' : '';
  };
  function parseCfg() {
    cfg = {};
    $('cfg').value.split(/\r?\n/).forEach(function (line) {
      var m = line.match(/^\s*([a-z_]+)\s*=\s*(.+?)\s*$/i);
      if (m) cfg[m[1].toLowerCase()] = m[2];
    });
    var box = $('parsed'); box.textContent = '';
    ['user', 'tenancy', 'fingerprint', 'region'].forEach(function (k) {
      var a = document.createElement('span'), b = document.createElement('span');
      a.textContent = k; b.textContent = cfg[k] ? (k === 'fingerprint' || k === 'region' ? cfg[k] : cfg[k].slice(0, 22) + '...' + cfg[k].slice(-6)) : 'missing';
      if (!cfg[k]) b.style.color = 'var(--r)';
      box.appendChild(a); box.appendChild(b);
    });
    var mp = REG_MAP(cfg.region);
    if (mp && !region()) { var el = document.querySelector('input[name=rg][value=' + mp + ']'); if (el) el.checked = true; regionNote(); }
  }
  $('cfg').addEventListener('input', parseCfg);
  $('keyf').addEventListener('change', function () {
    var f = this.files && this.files[0]; if (!f) return;
    var rd = new FileReader();
    rd.onload = function () { $('key').value = String(rd.result).trim() + '\n'; checkKey(); };
    rd.readAsText(f);
  });
  function checkKey() {
    var k = $('key').value.trim();
    if (!k) { st('s-key', ''); return false; }
    if (/BEGIN (RSA |ENCRYPTED )?PRIVATE KEY/.test(k) && /END (RSA |ENCRYPTED )?PRIVATE KEY/.test(k)) {
      if (/ENCRYPTED/.test(k)) { st('s-key', 'This key has a passphrase - make the API key again without one.', 'bad'); return false; }
      st('s-key', 'Private key looks right (' + k.length + ' characters). It stays in this tab.', 'ok'); return true;
    }
    if (/BEGIN PUBLIC KEY/.test(k)) { st('s-key', 'That is the PUBLIC key. Use the private key file you downloaded.', 'bad'); return false; }
    st('s-key', 'Not a PEM private key.', 'bad'); return false;
  }
  $('key').addEventListener('input', checkKey);

  // ---- 3. region --------------------------------------------------------------------------
  function regionNote() {
    var r = region(), home = REG_MAP(cfg.region);
    if (!r) { st('s-region', ''); return; }
    if (home && home !== r) st('s-region', 'Your Oracle home region ' + cfg.region + ' is a ' + home + ' region: the free server will be there, so ' + r + ' players get a higher ping. Pick ' + home + ' or use an Oracle account made in a ' + r + ' home region.', 'warn');
    else st('s-region', r + ' it is' + (cfg.region ? ' (Oracle ' + cfg.region + ')' : '') + '.', 'ok');
  }
  Array.prototype.forEach.call(document.querySelectorAll('input[name=rg]'), function (el) { el.addEventListener('change', regionNote); });

  // ---- 4. save + run ----------------------------------------------------------------------
  async function putSecrets() {
    var pk = need(await gh('GET', '/repos/' + repo() + '/actions/secrets/public-key'), 'public key').data;
    var key = unb64(pk.key), enc = new TextEncoder();
    var list = [
      ['OCI_USER', cfg.user], ['OCI_TENANCY', cfg.tenancy], ['OCI_FINGERPRINT', cfg.fingerprint],
      ['OCI_REGION', cfg.region], ['OCI_KEY', $('key').value.trim() + '\n'],
      ['DUCKDNS_TOKEN', $('duck').value.trim()], ['DISCORD_WEBHOOK', $('disc').value.trim()], ['SSH_PUBLIC_KEY', $('ssh').value.trim()]
    ];
    for (var i = 0; i < list.length; i++) {
      var name = list[i][0], val = list[i][1];
      if (!val) continue;   // optional ones left empty: keep whatever is there
      var sealed = window.mpSeal(enc.encode(val), key);
      need(await gh('PUT', '/repos/' + repo() + '/actions/secrets/' + name, { encrypted_value: b64(sealed), key_id: pk.key_id }), name);
      log('secret ' + name + ' saved (encrypted)', 'g');
    }
  }
  async function putRegion(r) {
    var u = await gh('PATCH', '/repos/' + repo() + '/actions/variables/MP_REGION', { name: 'MP_REGION', value: r });
    if (u.status === 404) u = await gh('POST', '/repos/' + repo() + '/actions/variables', { name: 'MP_REGION', value: r });
    need(u, 'variable MP_REGION');
    log('variable MP_REGION = ' + r, 'g');
  }
  async function runWorkflow(cmd, r) {
    var since = Date.now() - 5000;
    var info = need(await gh('GET', '/repos/' + repo()), 'repo').data;
    need(await gh('POST', '/repos/' + repo() + '/actions/workflows/' + WF + '/dispatches',
      { ref: info.default_branch || 'main', inputs: { region: r || '', command: cmd } }), 'run workflow');
    log('MP Oracle claim started (' + cmd + (r ? ', ' + r : '') + ') - waiting for GitHub to pick it up...', 'c');
    var run = null;
    for (var i = 0; i < 20 && !run; i++) {
      await sleep(3000);
      var l = await gh('GET', '/repos/' + repo() + '/actions/workflows/' + WF + '/runs?event=workflow_dispatch&per_page=5');
      if (l.ok && l.data && l.data.workflow_runs)
        run = l.data.workflow_runs.filter(function (w) { return Date.parse(w.created_at) >= since; })[0] || null;
    }
    if (!run) { log('Could not see the run yet - open the Actions tab.', 'y'); return; }
    log('run: ' + run.html_url, 'b');
    await follow(run.id);
  }
  async function follow(id) {
    var lastStep = '';
    for (var i = 0; i < 400; i++) {   // up to ~66 min (claim keeps trying for 50)
      var r = await gh('GET', '/repos/' + repo() + '/actions/runs/' + id + '/jobs');
      var job = r.ok && r.data && r.data.jobs && r.data.jobs[0];
      if (job) {
        var cur = (job.steps || []).filter(function (s) { return s.status === 'in_progress'; })[0];
        var name = cur ? cur.name : job.status;
        if (name !== lastStep) { lastStep = name; log('... ' + name, 'm'); live('claim: ' + name, 'warn'); }
        if (job.status === 'completed') {
          log('finished: ' + job.conclusion, job.conclusion === 'success' ? 'g' : 'r');
          await showResult(job.id);
          live(job.conclusion === 'success' ? 'done' : 'failed', job.conclusion === 'success' ? 'ok' : 'bad');
          return;
        }
      }
      await sleep(10000);
    }
    log('Still running - check the Actions tab later (it also retries every hour).', 'y');
  }
  async function showResult(jobId) {
    var a = await gh('GET', '/repos/' + repo() + '/check-runs/' + jobId + '/annotations');
    var found = false;
    if (a.ok && Array.isArray(a.data)) a.data.forEach(function (n) {
      if (/MP Oracle/.test(n.title || '')) { found = true; log(n.message, /\d+\.\d+\.\d+\.\d+/.test(n.message) ? 'g' : 'y'); }
    });
    if (!found) log('No server IP yet: Oracle was out of free capacity (normal) or the login was refused - the run page shows why. It retries every hour by itself.', 'y');
  }

  function lock(on) { ['b-save', 'b-check', 'b-status'].forEach(function (b) { $(b).disabled = on; }); }
  async function go(kind) {
    lock(true);
    try {
      if (!(await checkLogin())) throw new Error('Fix the GitHub token first (step 1).');
      if (kind === 'claim') {
        parseCfg();
        var miss = ['user', 'tenancy', 'fingerprint', 'region'].filter(function (k) { return !cfg[k]; });
        if (miss.length) throw new Error('Config text is missing: ' + miss.join(', '));
        if (!checkKey()) throw new Error('Add the private key (.pem) in step 2.');
        if (!region()) throw new Error('Pick the MP region in step 3.');
        await putSecrets();
        await putRegion(region());
        $('key').value = ''; $('keyf').value = ''; st('s-key', 'Saved to GitHub and cleared from this page.', 'ok');
      }
      await runWorkflow(kind, region());
    } catch (e) { log(e.message, 'r'); live('error', 'bad'); }
    lock(false);
  }
  $('b-save').addEventListener('click', function () { go('claim'); });
  $('b-check').addEventListener('click', function () { go('check'); });
  $('b-status').addEventListener('click', function () { go('status'); });
  if (token()) checkLogin().catch(function () {});
})();
