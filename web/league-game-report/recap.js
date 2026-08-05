const $ = selector => document.querySelector(selector);
const fmt = seconds => `${Math.floor(seconds / 60)}:${String(Math.round(seconds % 60)).padStart(2, '0')}`;
const n = value => Number(value || 0).toLocaleString();
const avg = values => values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : 0;
const query = new URLSearchParams(location.search);
const id = query.get('report');
const gameId = query.get('game');
const url = id ? `/api/report/${encodeURIComponent(id)}` : gameId ? `/api/game/${encodeURIComponent(gameId)}` : '/api/latest';
const icon = (kind, version, value) => version && value ? `<img loading="lazy" src="/assets/ddragon/${kind}/${encodeURIComponent(version)}/${encodeURIComponent(value)}" onerror="this.style.visibility='hidden'">` : '';
const abilityIcon = (version, champion, slot) => version && champion ? `<img loading="lazy" src="/assets/ddragon/ability/${encodeURIComponent(version)}/${encodeURIComponent(champion)}/${slot}" onerror="this.style.visibility='hidden'">` : '';

function metric(label, value) { return `<div class="metric"><h3>${label}</h3><strong>${value}</strong></div>`; }
function rollingAverage(samples, value) {
  let total = 0;
  return samples.map((sample, index) => {
    total += value(sample);
    if (index >= 3) total -= value(samples[index - 3]);
    return total / Math.min(index + 1, 3);
  });
}
function series(report, key) {
  if (key === 'apm') {
    const values = rollingAverage(report.input_samples, sample => sample.actions * 60);
    return report.input_samples.map((sample, index) => ({ x: sample.seconds, y: values[index] }));
  }
  if (key === 'velocity') {
    const values = rollingAverage(report.input_samples, sample => sample.mouse_distance_pixels);
    return report.input_samples.map((sample, index) => ({ x: sample.seconds, y: values[index] }));
  }
  return report.samples.map(sample => ({ x: sample.seconds, y: key === 'gold' ? (sample.estimated_gold || sample.gold) : sample[key] }));
}
function normalized(points, mode) {
  const values = points.map(point => point.y);
  const low = Math.min(...values), high = Math.max(...values), mean = avg(values);
  return points.map(point => ({ x: point.x, y: mode === 'average' ? point.y / (mean || 1) : (point.y - low) / ((high - low) || 1) }));
}
function reportTools(report) {
  const tools = document.createElement('section');
  tools.className = 'section';
  tools.innerHTML = `<h2>Report data</h2><div class="controls"><label>Game ID <input id="game-id" value="${report.game_id || ''}" placeholder="e.g. NA1_123456789"></label><button id="load-game">Load game</button><button id="save-json">Save source JSON</button></div><p class="notice">A new Game ID is loaded from Riot Match-v5 for your most recently recorded Riot ID, then retained locally.</p>`;
  $('#app').prepend(tools);
  const load = () => { const value = $('#game-id').value.trim(); if (value) location.search = `game=${encodeURIComponent(value)}`; };
  $('#load-game').onclick = load;
  $('#game-id').onkeydown = event => { if (event.key === 'Enter') load(); };
  $('#save-json').onclick = () => {
    const link = document.createElement('a');
    const name = (report.game_id || report.id || 'league-game-report').replace(/[^a-z0-9_-]/gi, '_');
    const jsonUrl = URL.createObjectURL(new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' }));
    link.href = jsonUrl; link.download = `league-game-report-${name}.json`; link.click();
    setTimeout(() => URL.revokeObjectURL(jsonUrl), 0);
  };
}
function render(report) {
  const final = report.samples.at(-1) || {}, input = report.input_samples || [];
  const actions = input.reduce((sum, sample) => sum + sample.actions, 0);
  const distance = input.reduce((sum, sample) => sum + sample.mouse_distance_pixels, 0);
  const apm = rollingAverage(input, sample => sample.actions * 60);
  const velocity = rollingAverage(input, sample => sample.mouse_distance_pixels);
  const minutes = (report.duration_seconds || 1) / 60;
  const hasFinalGold = Boolean(report.enrichment?.riot_match_v5);
  const hasMatchData = hasFinalGold;
  const gold = hasFinalGold ? final.estimated_gold : final.gold;
  const averageApm = avg(apm);
  const version = report.assets?.ddragon_version || '';
  const champion = icon('champion', version, report.champion);
const heatmapNote = report.hexbin_estimated ? 'Legacy movement-density approximation; older reports did not record elapsed dwell time.' : `Mouse dwell time in ${n(report.hex_radius_percent || 4)}%-of-frame-width hexbins.`;
$('#app').innerHTML = `<section class="hero"><div class="split"><div><h1>${champion} ${report.champion || 'League Game Report'}</h1><p class="sub">${report.completed_at?.slice(0, 16).replace('T', ' ')} · ${report.game_mode || 'Unavailable'} · ${report.map || ''}</p><p class="outcome ${report.outcome}">${report.outcome || 'Outcome unavailable'} · ${fmt(report.duration_seconds || 0)}</p></div><div class="grid">${metric('Team gold', report.team_gold ? `${n(report.team_gold)} / ${n(report.enemy_team_gold)}` : 'Pending enrichment')}${metric('Team kills', report.team_kills ? `${n(report.team_kills)} / ${n(report.enemy_team_kills)}` : 'Pending enrichment')}${metric('Riot ID', report.player || 'Unavailable')}${metric('Game ID', report.game_id || 'Pending enrichment')}</div></div><div class="grid">${metric('K / D / A', hasMatchData ? `${final.kills || 0} / ${final.deaths || 0} / ${final.assists || 0}` : 'Pending enrichment')}${metric('CS / CSPM', hasMatchData ? `${n(final.cs)} / ${(final.cs / minutes).toFixed(1)}` : 'Pending enrichment')}${metric(hasFinalGold ? 'Gold / GPM' : 'Current gold', hasFinalGold ? `${n(gold)} / ${(gold / minutes).toFixed(0)}` : 'Pending enrichment')}${metric('Role', report.role || 'Pending enrichment')}</div></section><section class="section"><h2>Performance data</h2><div class="split"><div><div class="heatmap" id="heatmap"></div><p class="notice">${heatmapNote} Positions are limited to the configured League game frame; DPI snapshot: ${n(report.dpi || 800)}.</p></div><div class="grid">${metric('Total actions', n(actions))}${metric('Mouse distance', `${n(Math.round(distance))} px`)}${metric('Peak / avg 3-sec APM', `${n(Math.round(Math.max(...apm, 0)))} / ${n(Math.round(averageApm))}`)}${metric('Peak / avg 3-sec velocity', `${n(Math.round(Math.max(...velocity, 0)))} / ${n(Math.round(avg(velocity)))} px/s`)}</div></div></section><section class="section"><h2>Game data</h2><div class="controls"><label>Normalization <select id="norm"><option value="minmax">Min–max</option><option value="average">Game average ratio</option></select></label>${['apm', 'velocity', 'level'].map(key => `<button data-series="${key}" class="active">${key === 'level' ? 'level progression' : key}</button>`).join('')}</div><div class="chart"><canvas id="chart"></canvas></div><h3>Game events</h3><div class="controls">${['kill', 'objective', 'tower', 'level', 'item'].map(key => `<button data-event="${key}" class="active">${key}</button>`).join('')}</div><div class="timeline" id="events"></div><p class="notice">APM and mouse velocity use one-second samples with a fixed three-second rolling average. Player level is saved only when it changes; the local game API does not provide fractional XP history.</p></section><section class="section"><h2>Ability leveling path</h2><div class="abilities">${(report.abilities || []).map(ability => `<div class="ability">${abilityIcon(version, report.champion, ability.ability)}<b>${ability.ability}${ability.level}</b>${fmt(ability.seconds)}</div>`).join('') || '<p class="muted">Unavailable for this report.</p>'}</div><h2 style="margin-top:28px">Player build</h2><div class="build">${(report.item_events || []).map(item => `<div class="build-item">${icon('item', version, item.item_id)}<div>${item.item || `Item ${item.item_id}`}</div><small>${fmt(item.seconds)}</small></div>`).join('') || '<p class="muted">Unavailable for this report.</p>'}</div></section><section class="section"><h2>Timeseries insights</h2><div id="insights"></div></section><footer class="footer">Self-only local data. Riot Games is not endorsing or sponsoring this source.</footer>`;
  const heatmap = $('#heatmap'), grid = { radius: Number(report.hex_radius_percent || 4), aspect: Number(report.frame_aspect_ratio || 16 / 9) };
  heatmap.style.aspectRatio = `1 / ${grid.aspect}`;
  const bins = report.hexbins || [], values = bins.map(bin => Number(bin.dwell_ms || 0)).sort((a, b) => a - b);
  const palette = ['#3b82f6', '#06b6d4', '#facc15', '#ef4444'];
  for (const bin of bins) {
    const offset = bin.row & 1 ? .5 : 0, x = Math.sqrt(3) * grid.radius * (bin.column + offset), y = grid.radius * (1 + 1.5 * bin.row);
    const rank = values.findLastIndex(value => value <= bin.dwell_ms), band = Math.min(3, Math.floor(Math.max(0, rank) * 4 / Math.max(1, values.length)));
    const hex = document.createElement('i'); hex.className = 'hexbin';
    hex.style.left = `${x}%`; hex.style.top = `${y / grid.aspect}%`; hex.style.width = `${2 * grid.radius}%`; hex.style.height = `${2 * grid.radius / grid.aspect}%`; hex.style.background = palette[band]; heatmap.append(hex);
  }
  const insights = [], first = (report.item_events || [])[0];
  if (first) insights.push(`First build event: <b>${first.item || `Item ${first.item_id}`}</b> at ${fmt(first.seconds)}.`);
  for (const ability of report.abilities || []) if ([6, 11, 16].includes(ability.level)) insights.push(`Level ${ability.level} milestone at ${fmt(ability.seconds)}.`);
  if (!insights.length) insights.push('More insights will appear as local game samples are collected.');
  $('#insights').innerHTML = insights.map(value => `<div class="insight">${value}</div>`).join('');
  let chart;
  const update = () => {
    const keys = [...document.querySelectorAll('[data-series].active')].map(button => button.dataset.series), mode = $('#norm').value;
    if (!window.Chart) { $('.chart').innerHTML = '<p class="notice">Charts need the configured Chart.js CDN connection. Metrics and event data remain available.</p>'; return; }
    Chart.defaults.font.family = 'Inter, ui-sans-serif, system-ui, sans-serif';
    chart?.destroy();
    chart = new Chart($('#chart'), { type: 'line', data: { datasets: keys.map((key, index) => ({ label: key === 'gold' ? 'estimated gold' : key, data: normalized(series(report, key), mode), borderColor: ['#86c5ff', '#e8bb5a', '#61d59b', '#f07886', '#af8cf5'][index], pointRadius: 0, tension: .25 })) }, options: { responsive: true, maintainAspectRatio: false, parsing: false, scales: { x: { type: 'linear', title: { display: true, text: 'Game time (seconds)' } }, y: { title: { display: true, text: mode === 'average' ? 'ratio to game average' : 'normalized value' } } }, plugins: { tooltip: { callbacks: { title: values => fmt(values[0].parsed.x) } } } } });
  };
  document.querySelectorAll('[data-series]').forEach(button => button.onclick = () => { button.classList.toggle('active'); update(); }); $('#norm').onchange = update; update();
  const drawEvents = () => { const enabled = new Set([...document.querySelectorAll('[data-event].active')].map(button => button.dataset.event)); $('#events').innerHTML = (report.events || []).filter(event => enabled.has(event.category)).map(event => `<i class="event ${event.category}" style="left:${Math.min(100, event.seconds / (report.duration_seconds || 1) * 100)}%"><span>${fmt(event.seconds)} · ${event.detail || event.type}</span></i>`).join(''); };
  document.querySelectorAll('[data-event]').forEach(button => button.onclick = () => { button.classList.toggle('active'); drawEvents(); }); drawEvents();
}
fetch(url).then(async response => { const body = await response.json().catch(() => ({})); return response.ok ? body : Promise.reject(body.error || 'This report is no longer stored by the local plugin.'); }).then(report => {
  render(report); reportTools(report);
  if (!report.enrichment?.riot_match_v5) {
    let attempts = 0;
    const refresh = setInterval(async () => {
      const response = await fetch(url); const updated = await response.json().catch(() => ({}));
      if (response.ok && updated.enrichment?.riot_match_v5) { clearInterval(refresh); location.reload(); }
      if (++attempts >= 20) clearInterval(refresh);
    }, 3000);
  }
}).catch(error => $('#app').innerHTML = `<section class="hero"><h1>Report unavailable</h1><p class="sub">${error}</p></section>`);
