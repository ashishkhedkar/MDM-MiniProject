/* VitalStat dashboard. All statistics come from the C++ backend (/api/*).
   This file only handles the UI, chart rendering and API communication. */
(() => {
  'use strict';

  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
  const esc = (v) => String(v).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const f1 = (v) => Number(v).toFixed(1);
  const FIT_COLORS = ['#e76f51', '#f4a261', '#e9c46a', '#5fb49c', '#2a9d8f'];

  const state = {
    A: null,
    records: [],
    modes: { exMode: 'count', fitMode: 'count', stackMode: 'count' },
    exOn: new Set(), fitOn: new Set(),
    heat: 'obs', tab: 'obs',
    charts: {}, chartsPending: false, chartsFailed: false,
    table: { page: 1, size: 25, sort: { key: 'id', dir: 1 }, search: '', ex: 'all', fit: 'all' },
  };

  /* ---------------- icons ---------------- */
  const I = {
    users: '<svg viewBox="0 0 24 24"><circle cx="9" cy="8" r="3.5"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0M16 4.5a3.5 3.5 0 0 1 0 7M21.5 20a6.5 6.5 0 0 0-4-6"/></svg>',
    run: '<svg viewBox="0 0 24 24"><circle cx="14" cy="4.5" r="2"/><path d="M9 21l3-6 3 2v4M12 15l-1-5 4-1 2 4M7 12l3-3"/></svg>',
    heart: '<svg viewBox="0 0 24 24"><path d="M12 20s-8-5.2-8-11a4.5 4.5 0 0 1 8-2.8A4.5 4.5 0 0 1 20 9c0 5.8-8 11-8 11z"/></svg>',
    chi: '<svg viewBox="0 0 24 24"><path d="M6 4l12 16M18 4L6 20"/></svg>',
    link: '<svg viewBox="0 0 24 24"><path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3A4 4 0 0 0 11 18.7l1-1"/></svg>',
    check: '<svg viewBox="0 0 24 24"><path d="M5 12.5l4.5 4.5L19 7"/></svg>',
    minus: '<svg viewBox="0 0 24 24"><path d="M6 12h12"/></svg>',
    warn: '<svg viewBox="0 0 24 24"><path d="M12 3l10 18H2zM12 10v5M12 18.5v.01"/></svg>',
    info: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg>',
    search: '<svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/></svg>',
  };

  /* ---------------- helpers ---------------- */
  async function api(path, opts) {
    let res;
    try { res = await fetch(path, opts); }
    catch (e) { throw new Error('Cannot reach the C++ server. Is it still running? (' + e.message + ')'); }
    const text = await res.text();
    let data = null;
    try { data = JSON.parse(text); } catch (e) { /* not JSON */ }
    if (!res.ok) {
      const err = new Error((data && data.error) || `Request failed (${res.status})`);
      err.issues = (data && data.issues) || [];
      throw err;
    }
    return data;
  }

  let loadingCount = 0;
  function setLoading(on) {
    const bar = $('#loadbar');
    loadingCount += on ? 1 : -1;
    if (loadingCount > 0) { bar.className = 'loadbar'; void bar.offsetWidth; bar.classList.add('active'); }
    else { loadingCount = 0; bar.className = 'loadbar done'; setTimeout(() => { if (!loadingCount) bar.className = 'loadbar'; }, 700); }
  }

  function toast(msg, isErr) {
    const t = document.createElement('div');
    t.className = 'toast' + (isErr ? ' err' : '');
    t.textContent = msg;
    $('#toasts').appendChild(t);
    setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity .3s'; setTimeout(() => t.remove(), 320); }, 3200);
  }

  function animateNumber(el, to) {
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) { el.textContent = to.toLocaleString(); return; }
    const t0 = performance.now(), dur = 700;
    const step = (t) => {
      const p = clamp((t - t0) / dur, 0, 1);
      el.textContent = Math.round(to * (1 - Math.pow(1 - p, 3))).toLocaleString();
      if (p < 1) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }

  const wrapLabel = (l) => (l.includes(' per ') ? l.split(' per ').map((s, i) => (i ? 'per ' + s : s)) : l);

  /* ---------------- theme ---------------- */
  function applyTheme(t) {
    document.documentElement.setAttribute('data-theme', t);
    try { localStorage.setItem('vitalstat-theme', t); } catch (e) { /* ignore */ }
  }
  function initTheme() {
    let t = null;
    try { t = localStorage.getItem('vitalstat-theme'); } catch (e) { /* ignore */ }
    if (!t) t = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    applyTheme(t);
  }
  function themeColors() {
    const cs = getComputedStyle(document.documentElement);
    const g = (n) => cs.getPropertyValue(n).trim();
    return { text: g('--text'), muted: g('--muted'), grid: g('--border'), surface: g('--surface') };
  }

  /* ---------------- data loading ---------------- */
  async function loadAll() {
    setLoading(true);
    $('#kpis').innerHTML = '<div class="skeleton" style="height:120px"></div>'.repeat(5);
    $('#resultsPanel').innerHTML = '<div class="skeleton" style="height:260px"></div>';
    try {
      const [A, R] = await Promise.all([api('/api/analysis'), api('/api/records')]);
      state.A = A;
      state.records = R.records;
      resetViewState();
      $('#banner').dataset.err = '';
      renderAll();
    } catch (e) {
      $('#kpis').innerHTML = '';
      $('#resultsPanel').innerHTML = '';
      $('#banner').innerHTML = `<div class="alert err">${I.warn}<div class="a-body"><b>Could not load the analysis.</b><div>${esc(e.message)}</div><div>Start the server from the project folder with <code>make run</code>, then retry.</div></div><button class="btn btn-ghost" id="retryBtn">Retry</button></div>`;
      $('#retryBtn').onclick = loadAll;
    } finally { setLoading(false); }
  }

  function resetViewState() {
    state.exOn = new Set([0, 1, 2, 3, 4]);
    state.fitOn = new Set([0, 1, 2, 3, 4]);
    state.table.page = 1; state.table.search = ''; state.table.ex = 'all'; state.table.fit = 'all';
    $('#search').value = '';
    const cat = state.A.categories;
    $('#fltEx').innerHTML = '<option value="all">All exercise levels</option>' + cat.exercise.map((l, i) => `<option value="${i}">${esc(l)}</option>`).join('');
    $('#fltFit').innerHTML = '<option value="all">All fitness levels</option>' + cat.fitness.map((l, i) => `<option value="${i}">${esc(l)}</option>`).join('');
  }

  /* ---------------- rendering ---------------- */
  function renderAll() {
    renderBadge(); renderBanner(); renderKPIs(); renderCharts(); renderChips(); renderHeat(); renderResults(); renderTable();
  }

  function renderBadge() {
    const d = state.A.dataset, b = $('#datasetBadge');
    b.hidden = false;
    b.className = 'chip ' + (d.synthetic ? 'synthetic' : 'user');
    b.textContent = d.synthetic ? 'Synthetic data' : d.bundled ? 'CDC BRFSS 2015 · real survey data' : 'Uploaded: ' + (d.name.length > 26 ? d.name.slice(0, 24) + '…' : d.name);
  }

  function renderBanner() {
    const d = state.A.dataset;
    let h = '';
    if (d.synthetic)
      h += `<div class="alert warn">${I.warn}<div class="a-body"><b>Synthetic dataset.</b><div>These ${d.validRows.toLocaleString()} records were computer-generated for demonstration. They are <b>not</b> genuine survey data, and the findings below illustrate the method only.</div></div></div>`;
    else if (d.bundled)
      h += `<div class="alert info">${I.info}<div class="a-body"><b>Dataset: CDC BRFSS 2015 (real survey data).</b><div>${esc(d.citation)}. Exercise frequency is derived from EXERANY2 + EXEROFT1 (activity sessions in the past 30 days); fitness is the respondent’s self-rated general health (GENHLTH), used as a proxy. ${d.validRows.toLocaleString()} records analysed.</div></div></div>`;
    else
      h += `<div class="alert info">${I.info}<div class="a-body"><b>Your dataset: ${esc(d.name)}</b><div>${d.validRows.toLocaleString()} valid records analysed. The data are treated as supplied by you; their origin is not verified.</div></div></div>`;
    if (d.warnings.length)
      h += `<div class="alert warn">${I.warn}<div class="a-body"><b>Data quality notes</b><ul>${d.warnings.map((w) => `<li>${esc(w)}</li>`).join('')}</ul></div></div>`;
    $('#banner').innerHTML = h;
  }

  function meterHTML(v, th) {
    const max = th[2] * 2, pos = (x) => clamp(x / max, 0, 1) * 100;
    return `<div class="meter">${th.map((t) => `<span class="tick" style="left:${pos(t)}%"></span>`).join('')}<span class="pin" style="left:${pos(v)}%"></span></div>
      <div class="meter-lbls"><span>0</span><span>small ${th[0]}</span><span>medium ${th[1]}</span><span>large ${th[2]}+</span></div>`;
  }

  function renderKPIs() {
    const A = state.A, S = A.summary, T = A.test, V = A.cramersV;
    const sigBadge = !T.computable ? '<span class="badge bad">Not applicable</span>'
      : T.significant ? '<span class="badge ok">Significant</span>' : '<span class="badge no">Not significant</span>';
    $('#kpis').innerHTML = `
      <div class="kpi"><div class="k-lbl"><i>${I.users}</i>Participants</div><div class="k-val" id="kN">0</div><div class="k-sub">${A.dataset.validRows.toLocaleString()} valid of ${A.dataset.totalRows.toLocaleString()} rows</div></div>
      <div class="kpi"><div class="k-lbl"><i>${I.run}</i>Most common exercise</div><div class="k-val sm">${esc(S.topExercise)}</div><div class="k-sub">${S.topExerciseCount} participants · ${f1(S.topExercisePct)}%</div></div>
      <div class="kpi"><div class="k-lbl"><i>${I.heart}</i>Most common fitness</div><div class="k-val sm">${esc(S.topFitness)}</div><div class="k-sub">${S.topFitnessCount} participants · ${f1(S.topFitnessPct)}%</div></div>
      <div class="kpi"><div class="k-lbl"><i>${I.chi}</i>Chi-Square test</div>
        <div class="k-val sm">${T.computable ? `χ² = ${T.statistic.toFixed(2)}` : 'n/a'}</div>
        <div class="k-sub">${T.computable ? `df = ${T.df} · p ${T.pDisplay.startsWith('<') ? T.pDisplay : '= ' + T.pDisplay}` : 'Not enough categories'} &nbsp;${sigBadge}</div></div>
      <div class="kpi"><div class="k-lbl"><i>${I.link}</i>Cramér’s V</div>
        <div class="k-val sm">${V.computable ? V.value.toFixed(3) : 'n/a'} ${V.computable ? `<span class="badge ${V.strength === 'negligible' ? 'no' : 'ok'}">${esc(V.strength)}</span>` : ''}</div>
        ${V.computable ? meterHTML(V.value, V.thresholds) : '<div class="k-sub">Not computable</div>'}</div>`;
    animateNumber($('#kN'), S.n);
  }

  /* ---- charts ---- */
  function mk(id, cfg) {
    if (state.charts[id]) state.charts[id].destroy();
    state.charts[id] = new Chart($('#' + id), cfg);
  }

  function renderCharts() {
    if (!window.Chart) {
      if (state.chartsFailed) {
        $$('.chart-box').forEach((b) => (b.innerHTML = `<div class="empty">${I.warn}<div>Chart.js could not be loaded.<br>Connect to the internet once, or run <code>make vendor</code> to save it locally.</div></div>`));
      } else state.chartsPending = true;
      return;
    }
    state.chartsPending = false;
    const th = themeColors();
    Chart.defaults.font.family = getComputedStyle(document.body).fontFamily;
    Chart.defaults.color = th.muted;
    drawDist('exChart', 'exercise', 'exMode', '#0f9d8f', th);
    drawDist('fitChart', 'fitness', 'fitMode', '#3b82c4', th);
    drawStack(th);
  }

  const tooltipBase = { backgroundColor: '#0d2a30', titleColor: '#fff', bodyColor: '#d6ecef', padding: 11, cornerRadius: 10, boxPadding: 4 };

  function drawDist(id, kind, modeKey, color, th) {
    const A = state.A, f = A[kind], cats = A.categories[kind];
    const pct = state.modes[modeKey] === 'pct';
    const data = pct ? f.percents : f.counts;
    mk(id, {
      type: 'bar',
      data: { labels: cats.map(wrapLabel), datasets: [{ data, backgroundColor: data.map((_, i) => (i === f.modeIndex ? color : color + '88')), hoverBackgroundColor: color, borderRadius: 8, maxBarThickness: 72 }] },
      options: {
        responsive: true, maintainAspectRatio: false, animation: { duration: 600 },
        plugins: {
          legend: { display: false },
          tooltip: { ...tooltipBase, callbacks: {
            title: (it) => cats[it[0].dataIndex],
            label: (c) => `${f.counts[c.dataIndex].toLocaleString()} participants (${f1(f.percents[c.dataIndex])}%)`,
            footer: (it) => (it[0].dataIndex === f.modeIndex ? 'Most common category' : ''),
          } },
        },
        scales: {
          x: { grid: { display: false }, ticks: { color: th.muted } },
          y: { beginAtZero: true, grid: { color: th.grid }, ticks: { color: th.muted, callback: (v) => (pct ? v + '%' : v) }, title: { display: true, text: pct ? 'Share of participants' : 'Participants', color: th.muted } },
        },
      },
    });
  }

  function drawStack(th) {
    const A = state.A, C = A.contingency, cat = A.categories;
    const pct = state.modes.stackMode === 'pct';
    const exIdx = [0, 1, 2, 3, 4].filter((i) => state.exOn.has(i));
    const fitIdx = [0, 1, 2, 3, 4].filter((j) => state.fitOn.has(j));
    mk('stackChart', {
      type: 'bar',
      data: {
        labels: exIdx.map((i) => wrapLabel(cat.exercise[i])),
        datasets: fitIdx.map((j) => ({ label: cat.fitness[j], _j: j, backgroundColor: FIT_COLORS[j], data: exIdx.map((i) => (pct ? C.rowPercents[i][j] : C.observed[i][j])), borderWidth: 1, borderColor: th.surface, maxBarThickness: 90 })),
      },
      options: {
        responsive: true, maintainAspectRatio: false, animation: { duration: 600 },
        interaction: { mode: 'nearest', intersect: true },
        plugins: {
          legend: { position: 'bottom', labels: { usePointStyle: true, boxWidth: 8, color: th.text } },
          tooltip: { ...tooltipBase, callbacks: {
            title: (it) => cat.exercise[exIdx[it[0].dataIndex]],
            label: (c) => { const i = exIdx[c.dataIndex], j = c.dataset._j; return `${cat.fitness[j]}: ${C.observed[i][j]} (${f1(C.rowPercents[i][j])}% of group · ${f1(C.cellPercents[i][j])}% of all)`; },
            footer: (it) => `Group total: ${C.rowTotals[exIdx[it[0].dataIndex]]} participants`,
          } },
        },
        scales: {
          x: { stacked: true, grid: { display: false }, ticks: { color: th.muted } },
          y: { stacked: true, beginAtZero: true, max: pct ? 100 : undefined, grid: { color: th.grid }, ticks: { color: th.muted, callback: (v) => (pct ? v + '%' : v) }, title: { display: true, text: pct ? 'Share of exercise group' : 'Participants', color: th.muted } },
        },
      },
    });
  }

  function renderChips() {
    const cat = state.A.categories;
    const build = (el, labels, set, colors) => {
      el.innerHTML = labels.map((l, i) => `<button type="button" class="fchip ${set.has(i) ? '' : 'off'}" data-i="${i}" style="--sw:${colors ? colors[i] : 'var(--primary)'}" aria-pressed="${set.has(i)}">${esc(l)}</button>`).join('');
    };
    build($('#exChips'), cat.exercise, state.exOn, null);
    build($('#fitChips'), cat.fitness, state.fitOn, FIT_COLORS);
  }

  /* ---- heatmap ---- */
  function heatValue(mode, i, j) {
    const C = state.A.contingency;
    return mode === 'obs' ? C.observed[i][j] : mode === 'exp' ? C.expected[i][j] : mode === 'row' ? C.rowPercents[i][j] : C.residuals[i][j];
  }
  function heatText(mode, v) {
    return mode === 'obs' ? String(v) : mode === 'exp' ? v.toFixed(1) : mode === 'row' ? v.toFixed(1) + '%' : (v > 0 ? '+' : '') + v.toFixed(2);
  }
  function renderHeat() {
    const A = state.A, C = A.contingency, cat = A.categories, mode = state.heat;
    let max = 0;
    for (let i = 0; i < 5; i++) for (let j = 0; j < 5; j++) max = Math.max(max, Math.abs(heatValue(mode, i, j)));
    const resMax = 3.5;
    let h = `<table class="heat" aria-label="Contingency heatmap"><thead><tr><th class="corner rowh">Exercise ↓ / Fitness →</th>${cat.fitness.map((l) => `<th>${esc(l)}</th>`).join('')}<th>Total</th></tr></thead><tbody>`;
    for (let i = 0; i < 5; i++) {
      h += `<tr><th class="rowh" scope="row">${esc(cat.exercise[i])}</th>`;
      for (let j = 0; j < 5; j++) {
        const v = heatValue(mode, i, j);
        let bg, fg;
        if (mode === 'res') {
          const t = clamp(v / resMax, -1, 1), a = Math.abs(t) * 0.9;
          bg = t >= 0 ? `rgba(15,157,143,${a.toFixed(3)})` : `rgba(224,96,78,${a.toFixed(3)})`;
          fg = a > 0.5 ? '#fff' : 'var(--text)';
        } else {
          const a = max ? v / max : 0, al = 0.07 + a * 0.88;
          bg = `rgba(15,157,143,${al.toFixed(3)})`;
          fg = al > 0.55 ? '#fff' : 'var(--text)';
        }
        h += `<td class="cell" data-i="${i}" data-j="${j}" style="background:${bg};color:${fg}">${heatText(mode, v)}</td>`;
      }
      h += `<td class="tot">${C.rowTotals[i]}</td></tr>`;
    }
    if (mode === 'obs' || mode === 'exp') {
      h += `<tr><th class="rowh" scope="row">Total</th>${C.colTotals.map((t) => `<td class="tot">${t}</td>`).join('')}<td class="tot">${A.summary.n}</td></tr>`;
    }
    h += '</tbody></table>';
    $('#heatmap').innerHTML = h;
    const leg = $('#heatLegend');
    if (mode === 'res')
      leg.innerHTML = '<span>Fewer than expected</span><span class="bar" style="background:linear-gradient(90deg,rgba(224,96,78,.9),rgba(127,127,127,.1),rgba(15,157,143,.9))"></span><span>More than expected</span><span>· |residual| &gt; 2 suggests a notable cell</span>';
    else
      leg.innerHTML = `<span>${mode === 'row' ? '0%' : '0'}</span><span class="bar" style="background:linear-gradient(90deg,rgba(15,157,143,.07),rgba(15,157,143,.95))"></span><span>${heatText(mode, max)}</span>`;
  }

  function showHeatTip(e) {
    const td = e.target.closest('td.cell'), tip = $('#heatTip');
    if (!td) { tip.classList.remove('show'); return; }
    const A = state.A, C = A.contingency, i = +td.dataset.i, j = +td.dataset.j;
    tip.innerHTML = `<b>${esc(A.categories.exercise[i])}</b> × <b>${esc(A.categories.fitness[j])}</b><br>
      Observed: <b>${C.observed[i][j]}</b> · Expected: <b>${C.expected[i][j].toFixed(1)}</b><br>
      <span class="mut">${f1(C.rowPercents[i][j])}% of exercise group · ${f1(C.colPercents[i][j])}% of fitness level · ${f1(C.cellPercents[i][j])}% of all</span><br>
      Std. residual: <b>${C.residuals[i][j] >= 0 ? '+' : ''}${C.residuals[i][j].toFixed(2)}</b>`;
    const w = tip.offsetWidth, hh = tip.offsetHeight;
    tip.style.left = clamp(e.clientX + 14, 8, innerWidth - w - 8) + 'px';
    tip.style.top = clamp(e.clientY + 14, 8, innerHeight - hh - 8) + 'px';
    tip.classList.add('show');
  }

  /* ---- statistical results ---- */
  const STRENGTH_TXT = {
    negligible: 'The variables are only very weakly related; knowing someone’s exercise frequency tells you almost nothing about their fitness rating.',
    small: 'A small but detectable relationship: exercise frequency explains only a modest part of the variation in fitness ratings.',
    medium: 'A moderate relationship: fitness ratings differ clearly between exercise groups, though much variation remains.',
    large: 'A strong relationship for this table size: fitness ratings differ substantially between exercise groups.',
  };

  function matrixHTML(kind) {
    const A = state.A, C = A.contingency, cat = A.categories;
    const get = { obs: (i, j) => C.observed[i][j], exp: (i, j) => C.expected[i][j], res: (i, j) => C.residuals[i][j], con: (i, j) => C.contributions[i][j] }[kind];
    const fmt = { obs: (v) => v, exp: (v) => v.toFixed(2), res: (v) => (v > 0 ? '+' : '') + v.toFixed(2), con: (v) => v.toFixed(3) }[kind];
    const totals = kind === 'obs' || kind === 'exp';
    let h = `<div class="mx-wrap"><table class="mx"><thead><tr><th>Exercise \\ Fitness</th>${cat.fitness.map((l) => `<th>${esc(l)}</th>`).join('')}${totals ? '<th>Total</th>' : ''}</tr></thead><tbody>`;
    for (let i = 0; i < 5; i++) {
      h += `<tr><td>${esc(cat.exercise[i])}</td>`;
      for (let j = 0; j < 5; j++) {
        const v = get(i, j);
        const cls = kind === 'res' ? (v >= 2 ? 'pos' : v <= -2 ? 'neg' : '') : kind === 'exp' && v < 5 && C.rowTotals[i] > 0 && C.colTotals[j] > 0 ? 'neg' : '';
        h += `<td class="${cls}">${fmt(v)}</td>`;
      }
      if (totals) h += `<td class="totcol">${C.rowTotals[i]}</td>`;
      h += '</tr>';
    }
    if (totals) h += `<tr class="totrow"><td>Total</td>${C.colTotals.map((t) => `<td>${t}</td>`).join('')}<td>${A.summary.n}</td></tr>`;
    h += '</tbody></table></div>';
    const notes = {
      obs: 'Observed counts from the dataset.',
      exp: 'Expected counts if the two variables were independent: (row total × column total) ÷ n. Values below 5 are shown in red.',
      res: 'Standardised residuals (observed − expected) ÷ √expected. Values beyond ±2 (green/red) mark cells that depart most from independence.',
      con: 'Each cell’s contribution (O − E)² / E; the contributions add up to the Chi-Square statistic.',
    };
    return h + `<p class="note">${notes[kind]}</p>`;
  }

  function renderResults() {
    const A = state.A, T = A.test, V = A.cramersV, N = A.interpretation;
    const vcls = !T.computable ? 'na' : T.significant ? 'sig' : 'nosig';
    const vico = !T.computable ? I.warn : T.significant ? I.check : I.minus;
    let h = `<article class="card">
      <div class="res-grid">
        <div class="stat"><div class="s-lbl">Chi-Square statistic (χ²)</div><div class="s-val">${T.computable ? T.statistic.toFixed(3) : 'n/a'}</div><div class="s-sub">Σ (O − E)² / E</div></div>
        <div class="stat"><div class="s-lbl">Degrees of freedom</div><div class="s-val">${T.computable ? T.df : 'n/a'}</div><div class="s-sub">(${T.effectiveRows} − 1) × (${T.effectiveCols} − 1)</div></div>
        <div class="stat"><div class="s-lbl">p-value</div><div class="s-val">${esc(T.pDisplay)}</div><div class="s-sub">${T.computable ? 'P(χ² ≥ observed | H0)' : ''}</div></div>
        <div class="stat"><div class="s-lbl">Significance level (α)</div><div class="s-val">${T.alpha}</div><div class="s-sub">Decision: ${esc(T.decision)}</div></div>
      </div>
      <div class="verdict ${vcls}"><div class="v-ico">${vico}</div><div><h4>${esc(N.headline)}</h4><p>${esc(N.plain)}</p></div></div>
      ${N.caution ? `<div class="alert warn">${I.warn}<div class="a-body">${esc(N.caution)}</div></div>` : ''}
      ${N.synthetic ? `<div class="alert info">${I.info}<div class="a-body">${esc(N.synthetic)}</div></div>` : ''}
    </article>`;

    h += `<div class="res-cols">
      <article class="card"><h4 class="sub">Hypotheses</h4><div class="hyp"><div><b>Null</b> — ${esc(T.hypotheses.null.replace(/^H0:\s*/, ''))}</div><div><b>Alternative</b> — ${esc(T.hypotheses.alternative.replace(/^H1:\s*/, ''))}</div>
        <div><b>Rule</b> — reject H0 if p &lt; ${T.alpha}. ${T.computable ? `Here p ${T.pDisplay.startsWith('<') ? T.pDisplay : '= ' + T.pDisplay}, so: <b>${esc(T.decision)}</b>.` : ''}</div></div></article>
      <article class="card"><h4 class="sub">Assumption checks ${T.computable ? `<span class="badge ${T.assumptionsMet ? 'ok' : 'bad'}">${T.assumptionsMet ? 'Expected counts OK' : 'Assumption violated'}</span>` : ''}</h4>
        <ul class="checks">${T.computable ? T.assumptions.map((a) => `<li class="${esc(a.status)}"><span class="ic">${a.status === 'pass' ? '✓' : a.status === 'fail' ? '!' : 'i'}</span><span>${esc(a.text)}</span></li>`).join('') : `<li class="fail"><span class="ic">!</span><span>${esc(T.reason)}</span></li>`}</ul></article>
    </div>`;

    h += `<article class="card" style="margin-top:18px"><h4 class="sub">Observed and expected frequencies</h4>
      <div class="tabs" id="mxTabs">${[['obs', 'Observed'], ['exp', 'Expected'], ['res', 'Std. residuals'], ['con', 'χ² contributions']].map(([k, l]) => `<button type="button" data-k="${k}" class="${state.tab === k ? 'on' : ''}">${l}</button>`).join('')}</div>
      <div id="mxBody">${matrixHTML(state.tab)}</div></article>`;

    h += `<div class="res-cols">
      <article class="card"><h4 class="sub">Cramér’s V — strength of association</h4>
        ${V.computable ? `<div class="v-box"><div class="v-big">${V.value.toFixed(3)}</div><div><span class="badge ${V.strength === 'negligible' ? 'no' : 'ok'}">${esc(V.strength)} association</span><p class="note" style="margin-top:6px">${esc(STRENGTH_TXT[V.strength])}</p></div></div>
        ${meterHTML(V.value, V.thresholds)}<p class="note">V = √(χ² / (n × ${V.dfStar})), ranging from 0 (no association) to 1 (perfect). Benchmarks (Cohen) for df* = ${V.dfStar}: small ${V.thresholds[0]}, medium ${V.thresholds[1]}, large ${V.thresholds[2]}.</p>` : '<p class="note">Not computable for this dataset.</p>'}</article>
      <article class="card"><h4 class="sub">Where the association comes from</h4>
        ${A.topResiduals.length ? `<ul class="resid-list">${A.topResiduals.map((r) => `<li><b>${esc(r.exercise)}</b> &amp; <b>${esc(r.fitness)}</b>: ${r.observed} observed vs ${r.expected.toFixed(1)} expected — <b>${r.direction} than expected</b> (residual ${r.residual > 0 ? '+' : ''}${r.residual.toFixed(2)})</li>`).join('')}</ul>` : '<p class="note">No cell-level comparison available.</p>'}
        <div class="alert info" style="margin-top:14px">${I.info}<div class="a-body"><b>Association ≠ causation.</b><div>${esc(N.causation)}</div></div></div></article>
    </div>`;
    $('#resultsPanel').innerHTML = h;
  }

  /* ---- dataset explorer ---- */
  function filteredRecords() {
    const t = state.table, A = state.A, q = t.search.trim().toLowerCase();
    let rows = state.records.filter((r) => {
      if (t.ex !== 'all' && r.e !== +t.ex) return false;
      if (t.fit !== 'all' && r.f !== +t.fit) return false;
      if (q && !(r.id.toLowerCase().includes(q) || A.categories.exercise[r.e].toLowerCase().includes(q) || A.categories.fitness[r.f].toLowerCase().includes(q))) return false;
      return true;
    });
    const k = t.sort.key, d = t.sort.dir;
    rows.sort((a, b) => (k === 'id' ? a.id.localeCompare(b.id, undefined, { numeric: true }) : a[k] - b[k] || a.id.localeCompare(b.id, undefined, { numeric: true })) * d);
    return rows;
  }

  function renderTable() {
    const t = state.table, A = state.A, rows = filteredRecords();
    const pages = Math.max(1, Math.ceil(rows.length / t.size));
    t.page = clamp(t.page, 1, pages);
    const slice = rows.slice((t.page - 1) * t.size, t.page * t.size);
    $('#tbl tbody').innerHTML = slice.length
      ? slice.map((r) => `<tr><td>${esc(r.id)}</td><td><span class="tag">${esc(A.categories.exercise[r.e])}</span></td><td><span class="tag" style="border-left:4px solid ${FIT_COLORS[r.f]}">${esc(A.categories.fitness[r.f])}</span></td></tr>`).join('')
      : `<tr><td colspan="3"><div class="empty">${I.search}<div><b>No matching records</b><br>Try a different search or clear the filters.</div></div></td></tr>`;
    $$('#tbl th').forEach((th) => th.setAttribute('aria-sort', th.dataset.sort === t.sort.key ? (t.sort.dir === 1 ? 'ascending' : 'descending') : 'none'));
    const from = rows.length ? (t.page - 1) * t.size + 1 : 0;
    $('#count').textContent = `Showing ${from.toLocaleString()}–${Math.min(t.page * t.size, rows.length).toLocaleString()} of ${rows.length.toLocaleString()} records` + (rows.length !== state.records.length ? ` (filtered from ${state.records.length.toLocaleString()})` : '');
    const nums = [];
    for (let p = 1; p <= pages; p++) if (p === 1 || p === pages || Math.abs(p - t.page) <= 1) nums.push(p); else if (nums[nums.length - 1] !== '…') nums.push('…');
    $('#pager').innerHTML = `<button data-p="${t.page - 1}" ${t.page === 1 ? 'disabled' : ''} aria-label="Previous page">‹</button>` +
      nums.map((n) => (n === '…' ? '<button disabled>…</button>' : `<button data-p="${n}" class="${n === t.page ? 'on' : ''}">${n}</button>`)).join('') +
      `<button data-p="${t.page + 1}" ${t.page === pages ? 'disabled' : ''} aria-label="Next page">›</button>`;
  }

  /* ---------------- upload ---------------- */
  function uploadError(msg, issues) {
    const list = (issues || []).slice(0, 12).map((i) => `<li>Line ${i.line}: ${esc(i.message)}</li>`).join('');
    $('#uploadStatus').innerHTML = `<div class="alert err">${I.warn}<div class="a-body"><b>Upload rejected.</b> ${esc(msg)}${list ? `<ul>${list}</ul>` : ''}<div>Your previous dataset is still loaded. Accepted values — Exercise: Never, Rarely, 1–2 days per week, 3–4 days per week, 5+ days per week. Fitness: Poor, Fair, Good, Very Good, Excellent.</div></div></div>`;
  }

  async function uploadFile(file) {
    if (!file) return;
    const st = $('#uploadStatus');
    if (!/\.csv$/i.test(file.name) && !/csv|text\/plain/.test(file.type)) return uploadError('Please choose a .csv file.');
    if (file.size === 0) return uploadError('The selected file is empty.');
    if (file.size > 8 * 1024 * 1024) return uploadError('The file is larger than the 8 MB limit.');
    st.innerHTML = `<div class="alert info"><span class="spinner"></span><div class="a-body">Validating and analysing <b>${esc(file.name)}</b>…</div></div>`;
    setLoading(true);
    try {
      const text = await file.text();
      const A = await api('/api/upload', { method: 'POST', headers: { 'Content-Type': 'text/csv', 'X-Filename': encodeURIComponent(file.name) }, body: text });
      const R = await api('/api/records');
      state.A = A; state.records = R.records;
      resetViewState(); renderAll();
      const d = A.dataset, skipped = d.missingRows + d.invalidRows;
      st.innerHTML = `<div class="alert ok">${I.check}<div class="a-body"><b>${esc(d.name)}</b> loaded: ${d.validRows.toLocaleString()} valid records${skipped ? `, ${skipped} row(s) excluded` : ''}. All statistics and charts have been recalculated.</div></div>`;
      toast('Dataset analysed — results updated');
    } catch (e) {
      uploadError(e.message, e.issues);
      toast('Upload failed', true);
    } finally { setLoading(false); $('#fileInput').value = ''; }
  }

  async function resetSample() {
    setLoading(true);
    try {
      state.A = await api('/api/reset', { method: 'POST' });
      state.records = (await api('/api/records')).records;
      resetViewState(); renderAll();
      $('#uploadStatus').innerHTML = '';
      toast('BRFSS sample restored');
    } catch (e) { toast(e.message, true); } finally { setLoading(false); }
  }

  /* ---------------- events ---------------- */
  function bind() {
    $('#themeToggle').onclick = () => {
      applyTheme(document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark');
      if (state.A) { renderCharts(); }
    };
    const sb = $('#sidebar'), sc = $('#scrim');
    const closeNav = () => { sb.classList.remove('open'); sc.classList.remove('show'); };
    $('#menuBtn').onclick = () => { sb.classList.add('open'); sc.classList.add('show'); };
    sc.onclick = closeNav;
    $('#nav').addEventListener('click', (e) => { if (e.target.closest('a')) closeNav(); });
    addEventListener('keydown', (e) => { if (e.key === 'Escape') closeNav(); });

    const secs = $$('main section');
    const io = new IntersectionObserver((ents) => {
      ents.forEach((en) => { if (en.isIntersecting) $$('#nav a').forEach((a) => a.classList.toggle('active', a.getAttribute('href') === '#' + en.target.id)); });
    }, { rootMargin: '-35% 0px -60% 0px' });
    secs.forEach((s) => io.observe(s));

    $$('.seg').forEach((seg) => seg.addEventListener('click', (e) => {
      const b = e.target.closest('button'); if (!b) return;
      $$('button', seg).forEach((x) => x.classList.toggle('on', x === b));
      state.modes[seg.dataset.seg] = b.dataset.v;
      if (state.A) renderCharts();
    }));
    const chipClick = (set) => (e) => {
      const b = e.target.closest('.fchip'); if (!b) return;
      const i = +b.dataset.i;
      if (set.has(i)) { if (set.size === 1) { toast('Keep at least one category visible', true); return; } set.delete(i); } else set.add(i);
      renderChips(); renderCharts();
    };
    $('#exChips').addEventListener('click', (e) => chipClick(state.exOn)(e));
    $('#fitChips').addEventListener('click', (e) => chipClick(state.fitOn)(e));

    $('#heatMode').onchange = (e) => { state.heat = e.target.value; renderHeat(); };
    const hm = $('#heatmap');
    hm.addEventListener('mousemove', showHeatTip);
    hm.addEventListener('mouseleave', () => $('#heatTip').classList.remove('show'));

    $('#resultsPanel').addEventListener('click', (e) => {
      const b = e.target.closest('#mxTabs button'); if (!b) return;
      state.tab = b.dataset.k;
      $$('#mxTabs button').forEach((x) => x.classList.toggle('on', x === b));
      $('#mxBody').innerHTML = matrixHTML(state.tab);
    });

    // upload
    const dz = $('#dropzone'), fi = $('#fileInput');
    const pick = () => fi.click();
    dz.onclick = pick;
    dz.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(); } };
    $('#uploadTop').onclick = () => { $('#explorer').scrollIntoView(); setTimeout(pick, 250); };
    fi.onchange = () => uploadFile(fi.files[0]);
    ['dragenter', 'dragover'].forEach((ev) => dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.add('drag'); }));
    ['dragleave', 'drop'].forEach((ev) => dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.remove('drag'); }));
    dz.addEventListener('drop', (e) => uploadFile(e.dataTransfer.files[0]));
    $('#resetBtn').onclick = resetSample;

    // table
    let timer;
    $('#search').oninput = (e) => { clearTimeout(timer); timer = setTimeout(() => { state.table.search = e.target.value; state.table.page = 1; renderTable(); }, 150); };
    $('#fltEx').onchange = (e) => { state.table.ex = e.target.value; state.table.page = 1; renderTable(); };
    $('#fltFit').onchange = (e) => { state.table.fit = e.target.value; state.table.page = 1; renderTable(); };
    $('#pageSize').onchange = (e) => { state.table.size = +e.target.value; state.table.page = 1; renderTable(); };
    $('#tbl thead').addEventListener('click', (e) => {
      const th = e.target.closest('th'); if (!th) return;
      const s = state.table.sort;
      if (s.key === th.dataset.sort) s.dir *= -1; else { s.key = th.dataset.sort; s.dir = 1; }
      renderTable();
    });
    $('#pager').addEventListener('click', (e) => {
      const b = e.target.closest('button[data-p]'); if (!b || b.disabled) return;
      state.table.page = +b.dataset.p; renderTable();
    });

    addEventListener('chartjs-ready', () => { if (state.chartsPending && state.A) renderCharts(); });
    addEventListener('chartjs-failed', () => { state.chartsFailed = true; if (state.A) renderCharts(); });
  }

  initTheme();
  bind();
  loadAll();
})();
