// Shared result plumbing for the 0.2.0 pages.
//
// Deliberately ES5, Promise-free, and dependency-free. The point is to test the
// browser: a harness that needs async/await to even start would hide an engine
// failure behind a syntax error, and a device with no console gives you nothing
// to debug that with.

(function (global) {
  var state = { pass: 0, fail: 0, skip: 0, list: null, summary: null, pending: 0, sealed: false };

  function ensureDom() {
    if (state.list) return;
    state.summary = document.getElementById('summary');
    state.list = document.getElementById('results');
  }

  function repaint() {
    if (!state.summary) return;
    var parts = [];
    if (state.fail) parts.push(state.fail + ' FAILED');
    parts.push(state.pass + ' passed');
    if (state.skip) parts.push(state.skip + ' skipped');
    if (state.pending) parts.push(state.pending + ' running');
    state.summary.textContent = parts.join(', ');
    state.summary.className = state.fail ? 'fail' : (state.pending ? '' : 'pass');
  }

  // ok: true = PASS, false = FAIL, null = SKIP (a check that could not run here,
  // which is NOT the same as one that ran and was fine — keep them apart).
  function record(name, ok, note) {
    ensureDom();
    var li = document.createElement('li');
    var tag = document.createElement('span');
    var label = ok === null ? 'SKIP' : (ok ? 'PASS' : 'FAIL');
    tag.className = 'tag ' + label;
    tag.textContent = label;
    var nm = document.createElement('span');
    nm.className = 'name';
    nm.textContent = name;
    if (note) {
      var n = document.createElement('div');
      n.className = 'note';
      n.textContent = note;
      nm.appendChild(n);
    }
    li.appendChild(tag);
    li.appendChild(nm);
    state.list.appendChild(li);
    if (ok === true) state.pass++;
    else if (ok === false) state.fail++;
    else state.skip++;
    repaint();
  }

  // Async checks announce themselves so the summary can say "still running"
  // instead of claiming everything passed while three requests are in flight.
  function begin() { state.pending++; repaint(); }
  function end() { if (state.pending > 0) state.pending--; repaint(); }

  // One-shot callback wrapper: a failed request can fire both readystatechange
  // and onerror, and a check that records itself twice makes the tally lie.
  function once(fn) {
    var done = false;
    return function () {
      if (done) return;
      done = true;
      fn.apply(null, arguments);
    };
  }

  function get(url, cb) {
    var finish = once(cb);
    var x = new XMLHttpRequest();
    try { x.open('GET', url, true); } catch (e) { return finish(0, '' + e, null); }
    x.onreadystatechange = function () { if (x.readyState === 4) finish(x.status, x.responseText, x); };
    x.onerror = function () { finish(0, '', x); };
    x.ontimeout = function () { finish(0, 'timeout', x); };
    x.timeout = 15000;
    try { x.send(null); } catch (e) { finish(0, '' + e, null); }
  }

  function getJSON(url, cb) {
    get(url, function (status, text) {
      var obj = null;
      try { obj = JSON.parse(text); } catch (e) { obj = null; }
      cb(status, obj, text);
    });
  }

  // Where the "other origin" lives. Default: same host, alt port. Override with
  // ?alt=<host:port> to get a genuinely cross-SITE origin (a second port is only
  // cross-origin — same site — which is not enough for SameSite).
  function altOrigin() {
    var m = /[?&]alt=([^&]+)/.exec(location.search);
    if (m) return location.protocol + '//' + decodeURIComponent(m[1]);
    var port = (location.port === '8099') ? '8100' : '8099';
    return location.protocol + '//' + location.hostname + ':' + port;
  }

  function altIsCrossSite() {
    var a = document.createElement('a');
    a.href = altOrigin();
    return a.hostname !== location.hostname;
  }

  global.T = {
    record: record, begin: begin, end: end, once: once,
    get: get, getJSON: getJSON,
    altOrigin: altOrigin, altIsCrossSite: altIsCrossSite
  };
})(window);
