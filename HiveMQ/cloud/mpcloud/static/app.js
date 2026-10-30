// MP Cloud console - plain JS, no build step. Every user string goes through textContent (no innerHTML).
(function () {
  'use strict';
  var main = document.getElementById('main');
  var S = { me: null, pub: null, catalog: null, timer: null };

  // ------------------------------------------------------------------ helpers
  function h(tag, attrs) {
    var el = document.createElement(tag);
    if (attrs) for (var k in attrs) {
      var v = attrs[k];
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k === 'style') el.style.cssText = v;
      else if (k.slice(0, 2) === 'on') el.addEventListener(k.slice(2), v);
      else if (v === true) el.setAttribute(k, '');
      else el.setAttribute(k, v);
    }
    for (var i = 2; i < arguments.length; i++) add(el, arguments[i]);
    return el;
  }
  function add(el, c) {
    if (c == null || c === false) return;
    if (Array.isArray(c)) { c.forEach(function (x) { add(el, x); }); return; }
    el.appendChild(typeof c === 'string' || typeof c === 'number' ? document.createTextNode(String(c)) : c);
  }
  function api(method, path, body) {
    var o = { method: method, headers: { 'X-MP-Cloud': '1' }, credentials: 'same-origin' };
    if (body !== undefined) { o.headers['Content-Type'] = 'application/json'; o.body = JSON.stringify(body); }
    return fetch('/api/v1' + path, o).then(function (r) {
      return r.text().then(function (t) {
        var j = null; try { j = t ? JSON.parse(t) : null; } catch (e) { j = null; }
        if (!r.ok) {
          if (r.status === 401 && path !== '/auth/login') { S.me = null; if (location.hash.indexOf('#/sign') !== 0) location.hash = '#/signin'; }
          var msg = j && (j.error || (j.detail && (Array.isArray(j.detail) ? j.detail.map(function (d) { return (d.loc || []).slice(-1)[0] + ': ' + d.msg; }).join('; ') : j.detail))) || ('HTTP ' + r.status);
          var e = new Error(msg); e.status = r.status; throw e;
        }
        return j;
      });
    });
  }
  var toastT;
  function toast(msg, bad) {
    var t = document.getElementById('toast');
    t.textContent = msg; t.className = 'toast on' + (bad ? ' bad' : '');
    clearTimeout(toastT); toastT = setTimeout(function () { t.className = 'toast'; }, 3800);
  }
  function fmtDate(ts) { if (!ts) return '-'; var d = new Date(ts * 1000); return d.toLocaleString(); }
  function stateBadge(s) { return h('span', { class: 'state s-' + s, text: s.replace('_', ' ').toLowerCase().replace(/^./, function (c) { return c.toUpperCase(); }) }); }
  function num(n) { return (Math.round(n * 10) / 10).toString(); }
  function meter(label, used, limit, unit) {
    var pct = limit ? Math.min(100, used / limit * 100) : 0;
    return h('div', { class: 'meter' },
      h('div', { class: 'lbl' }, h('span', { text: label }), h('b', { text: num(used) + ' / ' + num(limit) + (unit ? ' ' + unit : '') })),
      h('div', { class: 'bar', role: 'progressbar', 'aria-valuenow': Math.round(pct), 'aria-valuemin': 0, 'aria-valuemax': 100, 'aria-label': label },
        h('i', { class: pct >= 100 ? 'full' : pct >= 75 ? 'hi' : '', style: 'width:' + pct + '%' })));
  }
  function page(title, sub, crumbs) {
    main.textContent = '';
    if (crumbs) main.appendChild(h('div', { class: 'crumbs' }, crumbs));
    main.appendChild(h('h1', { text: title }));
    if (sub) main.appendChild(h('p', { class: 'sub', text: sub }));
    document.title = title + ' - MP Cloud';
  }
  function skeleton(n) { var w = h('div', { class: 'card' }); for (var i = 0; i < (n || 4); i++) w.appendChild(h('div', { class: 'skel', style: 'width:' + (90 - i * 12) + '%' })); return w; }
  function errBox(e) { return h('div', { class: 'note bad', role: 'alert', text: e.message || String(e) }); }
  function field(label, input, hint) { return h('div', null, h('label', { for: input.id, text: label }), input, hint ? h('p', { class: 'hint', text: hint }) : null); }
  function busy(btn, on, txt) { btn.disabled = on; if (txt) btn.textContent = txt; }

  // ------------------------------------------------------------------ shell
  function shell(on) {
    document.body.classList.toggle('auth', !on);
    document.getElementById('top').hidden = !on;
    document.getElementById('side').hidden = !on;
  }
  function navOn(key) {
    Array.prototype.forEach.call(document.querySelectorAll('[data-nav]'), function (a) { a.classList.toggle('on', a.getAttribute('data-nav') === key); });
    document.getElementById('side').classList.remove('open');
    document.getElementById('menu-btn').setAttribute('aria-expanded', 'false');
  }
  document.getElementById('menu-btn').addEventListener('click', function () {
    var s = document.getElementById('side'); s.classList.toggle('open');
    this.setAttribute('aria-expanded', s.classList.contains('open') ? 'true' : 'false');
  });
  document.getElementById('logout').addEventListener('click', function () {
    api('POST', '/auth/logout').finally(function () { S.me = null; location.hash = '#/signin'; });
  });
  function loadMe() {
    return api('GET', '/me').then(function (m) {
      S.me = m;
      document.getElementById('who').textContent = m.user.name + ' (' + m.user.email + ')';
      document.getElementById('region').textContent = m.region;
      Array.prototype.forEach.call(document.querySelectorAll('.admin-only'), function (x) { x.hidden = !m.user.is_admin; });
      return m;
    });
  }
  function loadCatalog() { return S.catalog ? Promise.resolve(S.catalog) : api('GET', '/catalog').then(function (c) { S.catalog = c; return c; }); }

  // ------------------------------------------------------------------ sign in / sign up
  function authPage(mode) {
    shell(false);
    var pub = S.pub || { free: { ocpus: 2, mem_gb: 12, disk_gb: 100, instances: 2 }, region: '', signup: 'invite' };
    var first = pub.first_run;
    if (first) mode = 'signup';
    var f = pub.free;
    main.textContent = '';
    var err = h('div');
    var email = h('input', { id: 'email', type: 'email', autocomplete: 'email', required: true });
    var pw = h('input', { id: 'pw', type: 'password', autocomplete: mode === 'signup' ? 'new-password' : 'current-password', required: true, minlength: mode === 'signup' ? 8 : null });
    var name = h('input', { id: 'name', type: 'text', autocomplete: 'name', required: true });
    var inv = h('input', { id: 'inv', type: 'text', autocomplete: 'off' });
    var btn = h('button', { class: 'btn', type: 'submit', text: mode === 'signup' ? (first ? 'Create the admin account' : 'Create account') : 'Sign in' });
    var form = h('form', {
      novalidate: true, onsubmit: function (ev) {
        ev.preventDefault(); err.textContent = '';
        busy(btn, true);
        var p = mode === 'signup'
          ? api('POST', '/auth/signup', { email: email.value, name: name.value, password: pw.value, invite: inv.value })
          : api('POST', '/auth/login', { email: email.value, password: pw.value });
        p.then(function () { S.pub = null; location.hash = '#/home'; route(); })
          .catch(function (e) { err.appendChild(errBox(e)); busy(btn, false); });
      }
    },
      h('h1', { text: mode === 'signup' ? (first ? 'Set up MP Cloud' : 'Create your free account') : 'Sign in to MP Cloud' }),
      h('p', { class: 'sub', text: first ? 'The first account becomes the administrator of this cloud.' : (pub.region ? 'Region ' + pub.region : '') }),
      err,
      mode === 'signup' ? field('Name', name) : null,
      field('Email', email),
      field('Password', pw, mode === 'signup' ? 'At least 8 characters.' : null),
      mode === 'signup' && !first && pub.signup === 'invite' ? field('Invite code', inv, 'Ask the admin of this cloud for it.') : null,
      btn,
      first ? null : h('p', { class: 'switch' }, mode === 'signup'
        ? ['Have an account? ', h('a', { href: '#/signin', text: 'Sign in' })]
        : (pub.signup === 'closed' ? 'Accounts are made by the admin.' : ['New here? ', h('a', { href: '#/signup', text: 'Create a free account' })])));
    main.appendChild(h('div', { class: 'auth-wrap' },
      h('section', { class: 'auth-art', 'aria-label': 'About MP Cloud' },
        h('div', null,
          h('div', { class: 'logo', style: 'color:#fff' }, h('span', null, 'MP ', h('b', { style: 'color:#ffd2c9', text: 'Cloud' }))),
        ),
        h('div', null,
          h('h2', { text: 'Your own Always Free cloud.' }),
          h('p', { text: 'Make servers on this machine or straight in Oracle Cloud, with one-click MP servers, Coolify and Dokploy.' }),
          h('ul', null,
            h('li', { text: f.ocpus + ' OCPUs and ' + f.mem_gb + ' GB memory, always free' }),
            h('li', { text: f.disk_gb + ' GB storage, ' + f.instances + ' servers' }),
            h('li', { text: 'Oracle capacity retry: waits for free A1 for you' }),
            h('li', { text: 'REST API keys for scripts and GitHub Actions' }))),
        h('small', { style: 'opacity:.7', text: 'MP Cloud ' + (pub.version || '') })),
      h('section', { class: 'auth-form' }, form)));
    (mode === 'signup' ? name : email).focus();
  }

  // ------------------------------------------------------------------ home
  function home() {
    navOn('home');
    var m = S.me, u = m.user, us = m.usage;
    page('Welcome, ' + u.name, null);
    var oracleNote = null;
    main.appendChild(h('div', { class: 'grid g2' },
      h('section', { class: 'card' },
        h('h2', null, 'Free tier usage', h('span', { class: 'free-tag', text: 'ALWAYS FREE' })),
        h('div', { class: 'grid', style: 'gap:14px' },
          meter('OCPUs', us.ocpus, u.ocpu_limit),
          meter('Memory', us.mem_gb, u.mem_limit, 'GB'),
          meter('Boot volumes', us.disk_gb, u.disk_limit, 'GB'),
          meter('Servers', us.instances, u.inst_limit))),
      h('section', { class: 'card' },
        h('h2', { text: 'Quick actions' }),
        h('div', { class: 'grid', style: 'gap:10px' },
          qa('Create an MP server', 'HiveMQ + Mega Vil + the 24/7 servers in one click', '#/instances/new?preset=mp'),
          qa('Create a server', 'Ubuntu or Debian with SSH', '#/instances/new'),
          qa('Deploy Coolify or Dokploy', 'Your own app platform on top', '#/instances/new?preset=coolify'),
          qa('Make an API key', 'For scripts and GitHub Actions', '#/keys')),
        oracleNote)));
    var recent = h('section', { class: 'card' }, h('h2', { text: 'Your servers' }), skeleton(2));
    main.appendChild(recent);
    api('GET', '/instances').then(function (list) {
      recent.lastChild.remove();
      recent.appendChild(list.length ? instTable(list.slice(0, 5)) :
        h('div', { class: 'empty' }, h('p', { text: 'No servers yet.' }), h('a', { class: 'btn', href: '#/instances/new', text: 'Create instance' })));
    }).catch(function (e) { recent.lastChild.replaceWith(errBox(e)); });
  }
  function qa(title, desc, href) {
    return h('a', { href: href, class: 'card', style: 'margin:0;padding:12px 14px;text-decoration:none;color:inherit;display:block' },
      h('b', { text: title, style: 'color:var(--info)' }), h('div', { class: 'hint', text: desc }));
  }

  // ------------------------------------------------------------------ instances
  function instTable(list, showOwner) {
    return h('div', { class: 'tbl-wrap' }, h('table', null,
      h('thead', null, h('tr', null, h('th', { text: 'Name' }), h('th', { text: 'State' }), h('th', { text: 'Where' }),
        h('th', { class: 'hide-sm', text: 'Shape' }), h('th', { class: 'hide-sm', text: 'OCPU / GB' }), h('th', { text: 'Address' }),
        showOwner ? h('th', { text: 'Owner' }) : null, h('th', { class: 'hide-sm', text: 'Created' }))),
      h('tbody', null, list.map(function (i) {
        return h('tr', null,
          h('td', null, h('a', { href: '#/instances/' + encodeURIComponent(i.id), text: i.name }), h('div', { class: 'hint', text: i.preset_label })),
          h('td', null, stateBadge(i.state)),
          h('td', { text: i.placement === 'oracle' ? 'Oracle Cloud' : 'This machine' }),
          h('td', { class: 'hide-sm mono', text: i.shape }),
          h('td', { class: 'hide-sm num', text: num(i.ocpus) + ' / ' + num(i.mem_gb) }),
          h('td', { class: 'mono', text: i.public_ip || i.private_ip || '-' }),
          showOwner ? h('td', { text: '#' + i.user_id }) : null,
          h('td', { class: 'hide-sm', text: fmtDate(i.created) }));
      }))));
  }
  function instances() {
    navOn('instances');
    page('Instances', 'Servers in this account. Oracle servers live in the connected Oracle tenancy; local ones on this machine.',
      [h('a', { href: '#/home', text: 'Home' }), ' / Compute'] );
    var bar = h('div', { class: 'row', style: 'margin-bottom:12px' }, h('a', { class: 'btn', href: '#/instances/new', text: 'Create instance' }));
    var allBox = null;
    if (S.me.user.is_admin) {
      allBox = h('input', { type: 'checkbox', id: 'allinst', onchange: load });
      bar.appendChild(h('label', { class: 'chk', for: 'allinst', style: 'margin:0 0 0 auto' }, allBox, 'All accounts'));
    }
    var box = h('section', { class: 'card' }, skeleton(3));
    main.appendChild(bar); main.appendChild(box);
    function load() {
      api('GET', '/instances' + (allBox && allBox.checked ? '?all=1' : '')).then(function (list) {
        box.textContent = '';
        box.appendChild(list.length ? instTable(list, allBox && allBox.checked) :
          h('div', { class: 'empty' }, h('p', { text: 'No servers yet. Create your first one - it is free.' }), h('a', { class: 'btn', href: '#/instances/new', text: 'Create instance' })));
        if (list.some(function (i) { return /PROVISIONING|WAITING|TERMINATING/.test(i.state); })) poll(load, 4000);
      }).catch(function (e) { box.textContent = ''; box.appendChild(errBox(e)); });
    }
    load();
  }
  function poll(fn, ms) { clearTimeout(S.timer); S.timer = setTimeout(fn, ms); }

  function createPage(q) {
    navOn('new');
    page('Create compute instance', null, [h('a', { href: '#/instances', text: 'Instances' }), ' / Create']);
    var wrap = h('div', null, skeleton(5)); main.appendChild(wrap);
    Promise.all([loadCatalog(), loadMe()]).then(function (r) {
      var cat = r[0], me = r[1], u = me.user, us = me.usage;
      wrap.textContent = '';
      var freeOc = Math.max(0, u.ocpu_limit - us.ocpus), freeMem = Math.max(0, u.mem_limit - us.mem_gb), freeDisk = Math.max(0, u.disk_limit - us.disk_gb);
      var err = h('div');
      var nameI = h('input', { id: 'iname', type: 'text', maxlength: 40, value: 'instance-' + new Date().toISOString().slice(0, 10).replace(/-/g, ''), required: true });
      function radios(name, items, cur, render) {
        return h('div', { class: 'choice', role: 'radiogroup' }, items.map(function (it) {
          return h('label', null, h('input', { type: 'radio', name: name, value: it.id, checked: it.id === cur, disabled: it.disabled }), render(it));
        }));
      }
      function val(name) { var x = wrap.querySelector('input[name=' + name + ']:checked'); return x ? x.value : ''; }
      var place = radios('place', cat.placements.map(function (p) { return { id: p.id, label: p.label, desc: p.desc, ready: p.ready }; }),
        cat.placements[0].ready ? 'local' : 'oracle',
        function (p) { return h('span', null, h('b', { text: p.label }), h('small', { text: p.ready ? p.desc : (p.id === 'oracle' ? 'Admin: connect the Oracle account first' : 'Incus is not installed on this machine') })); });
      var presetQ = (q.preset && cat.presets.some(function (p) { return p.id === q.preset; })) ? q.preset : 'plain';
      var preset = radios('preset', cat.presets, presetQ, function (p) { return h('span', null, h('b', { text: p.label }), h('small', { text: p.desc })); });
      var region = radios('mpreg', cat.mp_regions, 'asia', function (r) { return h('span', null, h('b', { text: r.label }), h('small', { text: r.domain })); });
      var regionBox = h('div', null, h('label', { text: 'MP region' }), region,
        h('p', { class: 'hint', text: 'Pick where your players are. The DuckDNS name is updated to this server if you add your token.' }),
        field('DuckDNS token (optional)', h('input', { id: 'duck', type: 'password', autocomplete: 'off' })));
      var shapeSel = h('select', { id: 'shape' });
      var imgSel = h('select', { id: 'img' }, cat.images.map(function (i) { return h('option', { value: i.id, text: i.label }); }));
      var ocR = h('input', { type: 'range', id: 'oc', min: 1, max: Math.max(1, Math.floor(u.ocpu_limit)), step: 1, value: Math.min(Math.max(1, Math.floor(freeOc)), 2) });
      var memR = h('input', { type: 'range', id: 'mem', min: 1, max: Math.max(1, u.mem_limit), step: 1, value: Math.min(Math.max(1, Math.floor(freeMem)), 12) });
      var ocO = h('output', { for: 'oc' }), memO = h('output', { for: 'mem' });
      var diskI = h('input', { id: 'disk', type: 'number', min: 10, max: 2000, value: Math.min(50, Math.max(10, freeDisk)) });
      var sshI = h('textarea', { id: 'ssh', rows: 3, placeholder: 'ssh-ed25519 AAAA... you@phone', spellcheck: 'false' });
      var flexBox = h('div', { class: 'shape' },
        h('div', null, h('label', { for: 'oc' }, 'OCPUs: ', ocO), ocR),
        h('div', null, h('label', { for: 'mem' }, 'Memory: ', memO, ' GB'), memR));
      var freeLine = h('p', { class: 'hint' });
      function sync() {
        var pl = val('place'), p = cat.placements.filter(function (x) { return x.id === pl; })[0];
        var cur = shapeSel.value;
        shapeSel.textContent = '';
        p.shapes.forEach(function (s) { shapeSel.appendChild(h('option', { value: s.id, text: s.id + ' - ' + s.label, selected: s.id === cur })); });
        var sh = p.shapes.filter(function (s) { return s.id === shapeSel.value; })[0] || p.shapes[0];
        flexBox.hidden = !sh.flex;
        Array.prototype.forEach.call(imgSel.options, function (o) {
          var im = cat.images.filter(function (i) { return i.id === o.value; })[0];
          o.disabled = pl === 'oracle' && !im.oracle;
        });
        if (imgSel.selectedOptions[0] && imgSel.selectedOptions[0].disabled) imgSel.value = 'ubuntu-24.04';
        if (pl === 'oracle' && +diskI.value < 47) diskI.value = 50;
        diskI.min = pl === 'oracle' ? 47 : 10;
        ocO.textContent = ocR.value; memO.textContent = memR.value;
        regionBox.hidden = val('preset') !== 'mp';
        var oc = sh.flex ? +ocR.value : sh.ocpus, mem = sh.flex ? +memR.value : sh.mem_gb;
        var over = oc > freeOc + 1e-9 || mem > freeMem + 1e-9 || +diskI.value > freeDisk;
        freeLine.textContent = 'Left in your free tier: ' + num(freeOc) + ' OCPU, ' + num(freeMem) + ' GB memory, ' + num(freeDisk) + ' GB storage, ' + (u.inst_limit - us.instances) + ' server(s).';
        freeLine.classList.toggle('over', over);
      }
      wrap.addEventListener('change', sync); wrap.addEventListener('input', sync);
      var btn = h('button', { class: 'btn', type: 'submit', text: 'Create' });
      var form = h('form', {
        onsubmit: function (ev) {
          ev.preventDefault(); err.textContent = '';
          busy(btn, true, 'Creating...');
          var pl = val('place');
          api('POST', '/instances', {
            name: nameI.value, placement: pl, shape: shapeSel.value, image: imgSel.value,
            ocpus: +ocR.value, mem_gb: +memR.value, disk_gb: +diskI.value, ssh_key: sshI.value.trim(),
            preset: val('preset'), mp_region: val('preset') === 'mp' ? val('mpreg') : '',
            duckdns_token: (document.getElementById('duck') || {}).value || ''
          }).then(function (i) {
            toast(pl === 'oracle' ? 'Asked Oracle for the server - it keeps trying until there is capacity.' : 'Creating the server...');
            location.hash = '#/instances/' + encodeURIComponent(i.id);
          }).catch(function (e) { err.appendChild(errBox(e)); busy(btn, false, 'Create'); window.scrollTo(0, 0); });
        }
      },
        err,
        h('section', { class: 'card' }, h('h2', { text: 'Name and placement' }), field('Name', nameI), h('label', { text: 'Placement' }), place),
        h('section', { class: 'card' }, h('h2', { text: 'What to install' }), preset, regionBox),
        h('section', { class: 'card' }, h('h2', null, 'Image and shape', h('span', { class: 'free-tag', text: 'ALWAYS FREE ELIGIBLE' })),
          h('div', { class: 'grid g2' }, field('Image', imgSel), field('Shape', shapeSel)), flexBox,
          field('Boot volume (GB)', diskI, 'Oracle boot volumes are at least 47 GB.'), freeLine),
        h('section', { class: 'card' }, h('h2', { text: 'Add SSH keys' }),
          field('SSH public key', sshI, 'Paste the .pub key (Termius: Keychain -> your key -> Export public key). You log in as opc.')),
        h('div', { class: 'row end' }, h('a', { class: 'btn ghost', href: '#/instances', text: 'Cancel' }), btn));
      wrap.appendChild(form);
      sync();
    }).catch(function (e) { wrap.textContent = ''; wrap.appendChild(errBox(e)); });
  }

  function detail(id) {
    navOn('instances');
    page('Instance', null, [h('a', { href: '#/instances', text: 'Instances' }), ' / Details']);
    var wrap = h('div', null, skeleton(5)); main.appendChild(wrap);
    var logPre = h('pre', { class: 'log', text: 'Loading...' });
    function load(first) {
      api('GET', '/instances/' + encodeURIComponent(id)).then(function (i) {
        main.querySelector('h1').textContent = i.name;
        document.title = i.name + ' - MP Cloud';
        wrap.textContent = '';
        var busyState = /PROVISIONING|WAITING|TERMINATING/.test(i.state);
        function act(a, label, cls) {
          return h('button', {
            class: 'btn sm ' + (cls || 'ghost'), text: label, disabled: busyState || i.state === 'FAILED' ||
              (a === 'start' && i.state === 'RUNNING') || (a !== 'start' && i.state === 'STOPPED'),
            onclick: function () {
              this.disabled = true;
              api('POST', '/instances/' + encodeURIComponent(id) + '/action', { action: a })
                .then(function () { toast(label + ': done'); load(); }).catch(function (e) { toast(e.message, true); load(); });
            }
          });
        }
        var term = h('button', {
          class: 'btn sm danger', text: 'Terminate', disabled: i.state === 'TERMINATING',
          onclick: function () {
            if (!confirm('Terminate "' + i.name + '"? The server and its disk are deleted for good.')) return;
            api('DELETE', '/instances/' + encodeURIComponent(id)).then(function () { toast('Terminating ' + i.name); load(); })
              .catch(function (e) { toast(e.message, true); });
          }
        });
        wrap.appendChild(h('div', { class: 'row', style: 'margin-bottom:14px' }, stateBadge(i.state),
          act('start', 'Start'), act('stop', 'Stop'), act('reboot', 'Reboot'), term));
        if (i.state === 'WAITING_CAPACITY') wrap.appendChild(h('div', { class: 'note warn' },
          h('b', { text: 'Waiting for Oracle capacity. ' }), i.note || '', ' - you can close this page; it keeps trying.'));
        else if (i.note) wrap.appendChild(h('div', { class: 'note info', text: i.note }));
        if (i.error) wrap.appendChild(h('div', { class: 'note bad', role: 'alert', text: i.error }));
        var kv = [
          ['OCID', h('code', { text: i.id })], ['Placement', i.placement === 'oracle' ? 'Oracle Cloud' : 'This machine (Incus)'],
          ['Installs', i.preset_label + (i.mp_region ? ' - ' + i.mp_region : '')], ['Image', i.image_label],
          ['Shape', i.shape], ['OCPUs / memory', num(i.ocpus) + ' OCPU / ' + num(i.mem_gb) + ' GB'], ['Boot volume', i.disk_gb + ' GB'],
          ['Public address', i.public_ip || i.public_host || '-'], ['Private IP', i.private_ip || '-'], ['Created', fmtDate(i.created)]];
        var conn = h('section', { class: 'card' }, h('h2', { text: 'Connect' }));
        if (i.ssh) conn.appendChild(h('div', { class: 'secret' }, h('code', { text: i.ssh }),
          h('button', { class: 'btn sm ghost', text: 'Copy', onclick: function () { navigator.clipboard && navigator.clipboard.writeText(i.ssh).then(function () { toast('Copied'); }); } })));
        else conn.appendChild(h('p', { class: 'hint', text: 'The SSH command shows up once the server has an address.' }));
        if (i.links.length) conn.appendChild(h('div', { class: 'row', style: 'margin-top:10px' }, i.links.map(function (l) {
          return h('a', { class: 'btn sm ghost', href: l.url, target: '_blank', rel: 'noopener', text: l.label });
        })));
        conn.appendChild(h('h2', { text: 'Open ports', style: 'margin-top:16px' }));
        conn.appendChild(h('div', { class: 'tbl-wrap' }, h('table', null,
          h('thead', null, h('tr', null, h('th', { text: 'Protocol' }), h('th', { text: 'Public port' }), h('th', { text: 'Server port' }))),
          h('tbody', null, i.ports.map(function (p) { return h('tr', null, h('td', { text: p.proto.toUpperCase() }), h('td', { class: 'num', text: p.public }), h('td', { class: 'num', text: p.private })); })))));
        wrap.appendChild(h('div', { class: 'grid g2' },
          h('section', { class: 'card' }, h('h2', { text: 'Instance information' }),
            h('dl', { class: 'kv' }, kv.map(function (r) { return [h('dt', { text: r[0] }), h('dd', null, r[1])]; }))),
          conn));
        wrap.appendChild(h('section', { class: 'card' },
          h('div', { class: 'row', style: 'justify-content:space-between' }, h('h2', { text: 'Setup log', style: 'margin:0' }),
            h('button', { class: 'btn sm ghost', text: 'Refresh', onclick: loadLog })), h('div', { style: 'height:10px' }), logPre));
        if (first) loadLog();
        if (busyState) poll(function () { load(); loadLog(); }, 4000);
      }).catch(function (e) {
        wrap.textContent = '';
        wrap.appendChild(e.status === 404 ? h('div', { class: 'empty' }, h('p', { text: 'This server is gone (terminated).' }), h('a', { class: 'btn', href: '#/instances', text: 'Back to instances' })) : errBox(e));
      });
    }
    function loadLog() {
      api('GET', '/instances/' + encodeURIComponent(id) + '/log').then(function (r) { logPre.textContent = r.log || '(empty)'; logPre.scrollTop = logPre.scrollHeight; })
        .catch(function () {});
    }
    load(true);
  }

  // ------------------------------------------------------------------ API keys
  function keys() {
    navOn('keys');
    page('API keys', 'Use a key in scripts or GitHub Actions: Authorization: Bearer <key>. Full API at /api/docs.',
      [h('a', { href: '#/home', text: 'Home' }), ' / Identity']);
    var out = h('div'), box = h('section', { class: 'card' }, skeleton(2));
    var nameI = h('input', { id: 'kname', type: 'text', maxlength: 60, placeholder: 'github-actions' });
    var btn = h('button', { class: 'btn', type: 'submit', text: 'Add API key' });
    main.appendChild(h('form', {
      class: 'card', onsubmit: function (ev) {
        ev.preventDefault(); busy(btn, true);
        api('POST', '/api-keys', { name: nameI.value || 'key' }).then(function (r) {
          out.textContent = '';
          out.appendChild(h('div', { class: 'note ok' }, h('b', { text: 'Copy it now - it is shown only once.' }),
            h('div', { class: 'secret', style: 'margin-top:8px' }, h('code', { text: r.token }),
              h('button', { class: 'btn sm ghost', type: 'button', text: 'Copy', onclick: function () { navigator.clipboard && navigator.clipboard.writeText(r.token).then(function () { toast('Copied'); }); } }))));
          nameI.value = ''; busy(btn, false); load();
        }).catch(function (e) { toast(e.message, true); busy(btn, false); });
      }
    }, h('div', { class: 'row', style: 'align-items:flex-end' }, h('div', { style: 'flex:1;min-width:200px' }, field('Key name', nameI)), btn), out));
    main.appendChild(box);
    function load() {
      api('GET', '/api-keys').then(function (list) {
        box.textContent = '';
        if (!list.length) { box.appendChild(h('div', { class: 'empty', text: 'No API keys.' })); return; }
        box.appendChild(h('div', { class: 'tbl-wrap' }, h('table', null,
          h('thead', null, h('tr', null, h('th', { text: 'Name' }), h('th', { text: 'Starts with' }), h('th', { text: 'Created' }), h('th', { text: 'Last used' }), h('th'))),
          h('tbody', null, list.map(function (k) {
            return h('tr', null, h('td', { text: k.name }), h('td', { class: 'mono', text: k.prefix + '...' }), h('td', { text: fmtDate(k.created) }), h('td', { text: fmtDate(k.last_used) }),
              h('td', null, h('button', { class: 'btn sm danger', text: 'Delete', onclick: function () { if (confirm('Delete key "' + k.name + '"?')) api('DELETE', '/api-keys/' + k.id).then(load); } })));
          })))));
      }).catch(function (e) { box.textContent = ''; box.appendChild(errBox(e)); });
    }
    load();
  }

  // ------------------------------------------------------------------ profile
  function account() {
    navOn('account');
    var u = S.me.user;
    page('My profile', null, [h('a', { href: '#/home', text: 'Home' }), ' / Identity']);
    main.appendChild(h('section', { class: 'card' }, h('dl', { class: 'kv' },
      h('dt', { text: 'Name' }), h('dd', { text: u.name }), h('dt', { text: 'Email' }), h('dd', { text: u.email }),
      h('dt', { text: 'Role' }), h('dd', { text: u.is_admin ? 'Administrator' : 'User' }),
      h('dt', { text: 'Free tier' }), h('dd', { text: u.ocpu_limit + ' OCPU, ' + u.mem_limit + ' GB memory, ' + u.disk_limit + ' GB storage, ' + u.inst_limit + ' servers' }),
      h('dt', { text: 'Member since' }), h('dd', { text: fmtDate(u.created) }))));
    var o = h('input', { id: 'opw', type: 'password', autocomplete: 'current-password' });
    var n = h('input', { id: 'npw', type: 'password', autocomplete: 'new-password', minlength: 8 });
    var btn = h('button', { class: 'btn', type: 'submit', text: 'Change password' });
    main.appendChild(h('form', {
      class: 'card', onsubmit: function (ev) {
        ev.preventDefault(); busy(btn, true);
        api('POST', '/me/password', { old: o.value, new: n.value }).then(function () { toast('Password changed - sign in again'); location.hash = '#/signin'; })
          .catch(function (e) { toast(e.message, true); busy(btn, false); });
      }
    }, h('h2', { text: 'Password' }), h('div', { class: 'grid g2' }, field('Current password', o), field('New password', n, 'At least 8 characters.')),
      h('div', { style: 'margin-top:12px' }, btn)));
  }

  // ------------------------------------------------------------------ audit
  function audit() {
    navOn('audit');
    page('Audit', 'Who did what, newest first (last 200).', [h('a', { href: '#/home', text: 'Home' }), ' / Identity']);
    var box = h('section', { class: 'card' }, skeleton(4)); main.appendChild(box);
    api('GET', '/audit').then(function (list) {
      box.textContent = '';
      box.appendChild(list.length ? h('div', { class: 'tbl-wrap' }, h('table', null,
        h('thead', null, h('tr', null, h('th', { text: 'Time' }), h('th', { text: 'Account' }), h('th', { text: 'Event' }), h('th', { text: 'Target' }), h('th', { class: 'hide-sm', text: 'Detail' }))),
        h('tbody', null, list.map(function (a) {
          return h('tr', null, h('td', { text: fmtDate(a.at) }), h('td', { text: a.email || '-' }), h('td', { class: 'mono', text: a.action }), h('td', { text: a.target || '' }), h('td', { class: 'hide-sm mono', text: (a.detail || '').slice(0, 120) }));
        })))) : h('div', { class: 'empty', text: 'Nothing yet.' }));
    }).catch(function (e) { box.textContent = ''; box.appendChild(errBox(e)); });
  }

  // ------------------------------------------------------------------ admin
  function admin() {
    navOn('admin');
    if (!S.me.user.is_admin) { location.hash = '#/home'; return; }
    page('Tenancy & quotas', 'This machine, the Oracle account, sign-up and every account\'s free tier.', [h('a', { href: '#/home', text: 'Home' }), ' / Administration']);
    var wrap = h('div', null, skeleton(6)); main.appendChild(wrap);
    Promise.all([api('GET', '/admin/overview'), api('GET', '/admin/oracle')]).then(function (r) {
      var o = r[0], oc = r[1], st = o.settings;
      wrap.textContent = '';
      wrap.appendChild(h('div', { class: 'grid g2' },
        h('section', { class: 'card' }, h('h2', { text: 'This machine' }),
          h('dl', { class: 'kv' }, h('dt', { text: 'Engine' }), h('dd', { text: o.driver === 'fake' ? 'Demo (fake servers)' : 'Incus' + (o.driver_ok ? '' : ' - not running, see install.sh') }),
            h('dt', { text: 'CPU / memory' }), h('dd', { text: o.host.cpus + ' cores / ' + o.host.mem_gb + ' GB (' + o.host.arch + ')' }),
            h('dt', { text: 'Free disk' }), h('dd', { text: o.host.disk_free_gb + ' GB' })),
          h('div', { style: 'margin-top:12px' }, meter('Memory given to local + Oracle servers', o.allocated.mem, o.host.mem_gb, 'GB'))),
        oracleCard(oc)));
      wrap.appendChild(settingsCard(st, o.invite_set));
      wrap.appendChild(usersCard(o.users));
    }).catch(function (e) { wrap.textContent = ''; wrap.appendChild(errBox(e)); });
  }
  function oracleCard(oc) {
    var cfg = h('textarea', { id: 'ocfg', rows: 6, spellcheck: 'false', placeholder: '[DEFAULT]\nuser=ocid1.user.oc1..\nfingerprint=12:34:..\ntenancy=ocid1.tenancy.oc1..\nregion=ap-singapore-1\nkey_file=<path>' });
    if (oc.configured) cfg.value = '[DEFAULT]\nuser=' + oc.user + '\nfingerprint=' + oc.fingerprint + '\ntenancy=' + oc.tenancy + '\nregion=' + oc.region + '\n';
    var keyF = h('input', { id: 'okeyf', type: 'file', accept: '.pem,.key,.txt' });
    var keyT = h('textarea', { id: 'okey', rows: 3, spellcheck: 'false', placeholder: oc.key_saved ? '(saved - leave empty to keep it)' : '-----BEGIN PRIVATE KEY-----' });
    keyF.addEventListener('change', function () { var f = keyF.files[0]; if (!f) return; var rd = new FileReader(); rd.onload = function () { keyT.value = String(rd.result); }; rd.readAsText(f); });
    var retry = h('input', { id: 'oretry', type: 'number', min: 30, max: 3600, value: oc.retry_s });
    var res = h('div');
    var save = h('button', { class: 'btn', type: 'submit', text: 'Save' });
    var test = h('button', {
      class: 'btn ghost', type: 'button', text: 'Test connection', disabled: !oc.configured, onclick: function () {
        busy(test, true); res.textContent = '';
        api('POST', '/admin/oracle/test').then(function (t) {
          res.appendChild(h('div', { class: 'note ok' }, h('b', { text: 'Connected. ' }),
            'Home region ' + (t.home_region || '?') + ', ' + t.availability_domains.length + ' availability domain(s), ' + t.instances +
            ' server(s), A1 in use: ' + t.a1_ocpus_used + ' OCPU / ' + t.a1_mem_used + ' GB.' +
            (t.home_region && t.home_region !== t.region ? ' Note: free servers only exist in the home region ' + t.home_region + '.' : '')));
        }).catch(function (e) { res.appendChild(errBox(e)); }).finally(function () { busy(test, false); });
      }
    });
    return h('form', {
      class: 'card', onsubmit: function (ev) {
        ev.preventDefault(); busy(save, true); res.textContent = '';
        api('PUT', '/admin/oracle', { config: cfg.value, key: keyT.value, retry_s: +retry.value }).then(function () {
          keyT.value = ''; keyF.value = ''; S.catalog = null; test.disabled = false;
          toast('Oracle account saved'); test.click();
        }).catch(function (e) { res.appendChild(errBox(e)); }).finally(function () { busy(save, false); });
      }
    }, h('h2', null, 'Oracle account', h('span', { class: 'free-tag', style: oc.configured ? '' : 'color:var(--muted);background:var(--surface2)', text: oc.configured ? 'CONNECTED' : 'NOT SET' })),
      h('p', { class: 'hint', text: 'Oracle website: Profile -> API keys -> Add API key -> download the private key, copy the config text. Servers with placement "Oracle Cloud" are made in this account.' }),
      field('Config text', cfg), h('label', { for: 'okeyf', text: 'Private key (.pem)' }), keyF, keyT,
      field('Retry every (seconds) while Oracle is out of capacity', retry),
      h('div', { class: 'row', style: 'margin-top:12px' }, save, test), res);
  }
  function settingsCard(st, inviteSet) {
    var signup = h('select', { id: 'signup' }, [['open', 'Open - anyone can sign up'], ['invite', 'Invite code needed'], ['closed', 'Closed']].map(function (x) { return h('option', { value: x[0], text: x[1], selected: st.signup === x[0] }); }));
    var inv = h('input', { id: 'invite', type: 'text', autocomplete: 'off', placeholder: inviteSet ? '(set - type to change)' : 'make one up' });
    var rl = h('input', { id: 'rlabel', type: 'text', value: st.region_label });
    var ph = h('input', { id: 'phost', type: 'text', value: st.public_host, placeholder: 'public IP or DuckDNS name of this machine' });
    var fo = h('input', { id: 'fo', type: 'number', min: 0, step: 1, value: st.free_ocpus });
    var fm = h('input', { id: 'fm', type: 'number', min: 0, step: 1, value: st.free_mem_gb });
    var fd = h('input', { id: 'fd', type: 'number', min: 0, step: 1, value: st.free_disk_gb });
    var fi = h('input', { id: 'fi', type: 'number', min: 0, step: 1, value: st.free_instances });
    var btn = h('button', { class: 'btn', type: 'submit', text: 'Save settings' });
    return h('form', {
      class: 'card', onsubmit: function (ev) {
        ev.preventDefault(); busy(btn, true);
        var body = { signup: signup.value, region_label: rl.value, public_host: ph.value, free_ocpus: +fo.value, free_mem_gb: +fm.value, free_disk_gb: +fd.value, free_instances: +fi.value };
        if (inv.value) body.invite_code = inv.value;
        api('PATCH', '/admin/settings', body).then(function () { toast('Saved'); inv.value = ''; return loadMe(); })
          .catch(function (e) { toast(e.message, true); }).finally(function () { busy(btn, false); });
      }
    }, h('h2', { text: 'Cloud settings' }),
      h('div', { class: 'grid g2' }, field('Sign-up', signup), field('Invite code', inv), field('Region name shown in the console', rl), field('Public address of this machine', ph, 'Used in the SSH commands of local servers.')),
      h('h2', { text: 'Always Free quota for new accounts', style: 'margin-top:16px' }),
      h('div', { class: 'grid g4' }, field('OCPUs', fo), field('Memory (GB)', fm), field('Storage (GB)', fd), field('Servers', fi)),
      h('div', { style: 'margin-top:12px' }, btn));
  }
  function usersCard(users) {
    return h('section', { class: 'card' }, h('h2', { text: 'Accounts' }), h('div', { class: 'tbl-wrap' }, h('table', null,
      h('thead', null, h('tr', null, h('th', { text: 'Account' }), h('th', { text: 'Used' }), h('th', { text: 'OCPU' }), h('th', { text: 'GB' }), h('th', { text: 'Disk' }), h('th', { text: 'Servers' }), h('th', { text: 'Admin' }), h('th'))),
      h('tbody', null, users.map(function (x) {
        function n(k, v) { return h('input', { type: 'number', min: 0, step: 1, value: v, 'aria-label': k + ' for ' + x.email, style: 'width:76px', 'data-k': k }); }
        var row = h('tr', null,
          h('td', null, h('b', { text: x.name }), h('div', { class: 'hint', text: x.email })),
          h('td', { class: 'num', text: num(x.usage.ocpus) + ' OCPU, ' + num(x.usage.mem_gb) + ' GB, ' + x.usage.instances + ' srv' }),
          h('td', null, n('ocpu_limit', x.ocpu_limit)), h('td', null, n('mem_limit', x.mem_limit)), h('td', null, n('disk_limit', x.disk_limit)), h('td', null, n('inst_limit', x.inst_limit)),
          h('td', null, h('input', { type: 'checkbox', checked: !!x.is_admin, 'aria-label': 'Admin', 'data-k': 'is_admin', disabled: x.id === S.me.user.id })),
          h('td', null, h('div', { class: 'row' },
            h('button', {
              class: 'btn sm ghost', text: 'Save', onclick: function () {
                var b = {};
                Array.prototype.forEach.call(row.querySelectorAll('[data-k]'), function (i) { b[i.getAttribute('data-k')] = i.type === 'checkbox' ? i.checked : +i.value; });
                api('PATCH', '/admin/users/' + x.id, b).then(function () { toast('Saved ' + x.email); }).catch(function (e) { toast(e.message, true); });
              }
            }),
            x.id === S.me.user.id ? null : h('button', {
              class: 'btn sm danger', text: 'Delete', onclick: function () {
                if (confirm('Delete the account ' + x.email + '?')) api('DELETE', '/admin/users/' + x.id).then(function () { toast('Deleted'); admin(); }).catch(function (e) { toast(e.message, true); });
              }
            }))));
        return row;
      })))));
  }

  // ------------------------------------------------------------------ router
  function route() {
    clearTimeout(S.timer);
    var hash = location.hash || '#/home';
    var path = hash.slice(1).split('?')[0], qs = {};
    (hash.split('?')[1] || '').split('&').forEach(function (p) { if (p) { var kv = p.split('='); qs[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || ''); } });
    if (path === '/signin' || path === '/signup') {
      return (S.pub ? Promise.resolve(S.pub) : api('GET', '/public').then(function (p) { S.pub = p; return p; }))
        .then(function () { authPage(path === '/signup' ? 'signup' : 'signin'); })
        .catch(function () { authPage('signin'); });
    }
    var ready = S.me ? Promise.resolve(S.me) : loadMe();
    ready.then(function () {
      shell(true);
      main.focus({ preventScroll: true });
      if (path === '/home' || path === '/') return home();
      if (path === '/instances') return instances();
      if (path === '/instances/new') return createPage(qs);
      if (path.indexOf('/instances/') === 0) return detail(decodeURIComponent(path.slice(11)));
      if (path === '/keys') return keys();
      if (path === '/account') return account();
      if (path === '/audit') return audit();
      if (path === '/admin') return admin();
      location.hash = '#/home';
    }).catch(function (e) {
      if (e.status === 401) {
        api('GET', '/public').then(function (p) { S.pub = p; location.hash = p.first_run ? '#/signup' : '#/signin'; });
      } else { shell(false); main.textContent = ''; main.appendChild(errBox(e)); }
    });
  }
  window.addEventListener('hashchange', function () { if (location.hash.indexOf('#/sign') !== 0) S.me = S.me; route(); });
  route();
})();
