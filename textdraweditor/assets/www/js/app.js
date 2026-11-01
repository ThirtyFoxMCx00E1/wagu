/* MP Textdraw Editor - account pages (login / register / forgot / reset) */
(function (G) {
  'use strict';
  var App = G.MPApp = {};
  App.SRV = 'mp-textdraw-editor';
  App.$ = function (id) { return document.getElementById(id); };
  App.user = function () { try { return localStorage.getItem('mptde_user') || ''; } catch (e) { return ''; } };
  App.setUser = function (u) { try { if (u) localStorage.setItem('mptde_user', u); else localStorage.removeItem('mptde_user'); } catch (e) {} };
  App.status = function (txt, cls) { var s = App.$('status'); if (!s) return; s.textContent = txt; s.className = 'status mt-2 ' + (cls || ''); };
  App.busy = function (on) { document.querySelectorAll('button[type=submit]').forEach(function (b) { b.disabled = !!on; }); };
  App.openUrl = function (u) { if (G.MPTDE && G.MPTDE.openUrl) G.MPTDE.openUrl(u); else G.open(u, '_blank'); };
  // bootstrap validation, like js/login.js of the template
  App.forms = function (onOk) {
    document.querySelectorAll('.needs-validation').forEach(function (form) {
      form.addEventListener('submit', function (ev) {
        ev.preventDefault(); ev.stopPropagation();
        form.classList.add('was-validated');
        if (form.checkValidity()) onOk(form);
      }, false);
    });
  };
  // one bot connection per page; handlers by op
  App.connect = function (handlers) {
    var c = new MPAuth.Client(function (a) {
      if (a.op === 'link') App.status('Connected to the MP account server (' + a.broker + ').', 'ok');
      else if (a.op === 'down') App.status('Connection lost, trying another server...', 'bad');
      else if (a.op === 'slow') App.status('Still connecting (' + a.broker + ')...', '');
      var h = handlers[a.op]; if (h) h(a);
    });
    App.status('Connecting to the MP account server...');
    c.start();
    return c;
  };
  // Discord code flow shared by register.html and forgot.html
  App.codeFlow = function (c, onVerified) {
    var rid = '', code = '';
    function gen() { code = MPAuth.Client.code(); App.$('code').textContent = '/gen ' + code; rid = c.send({ op: 'gen', code: code }); }
    gen();
    return {
      gen: function (a) {
        if (a.rid !== rid) return;
        if (a.ok === '1') { App.status('Waiting for /gen in Discord... (the code works 10 minutes)', 'ok'); return; }
        if (a.err === 'dup') { gen(); return; }
        App.status(a.err || 'The code was not accepted.', 'bad');
      },
      verified: function (a) { if (a.vtoken) onVerified(a); },
      again: gen
    };
  };
})(window);
