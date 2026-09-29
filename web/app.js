"use strict";

const FS = 48000;
const ROLES = ["FL-Tweeter", "FR-Tweeter", "FL-Woofer", "FR-Woofer",
               "FL-Midrange", "FR-Midrange", "L-Subwoofer", "R-Subwoofer"];
const SOURCE_NAMES = {1: "AUX", 2: "Bluetooth", 3: "High level", 7: "USB media"};
const NOISE_CODES = [0,1,2,3,4,5,6,7,8,9,10,11,13,14,16,18,23,29,41,65,103];
const HP_CODES = [40,8,14,20,46,52,58,26,64,42,10,16,22,48,54,60,28,66,44,12,18,24,50,56,62,30,68];
const LP_CODES = [41,9,15,21,47,53,59,27,65,43,11,17,23,49,55,61,29,67,45,13,19,25,51,57,63,31,69];

const S = {
  connected: false,
  reading: false,
  valid: false,
  progress: 0,
  total: 0,
  time: "",
  status: "",
  values: new Array(1934).fill(0),
  ch: 2,
  band: 26,
};

/* ---------- protocol conversions (mirror src/gui) ---------- */

function dspFreq(v) { return v & 0x8000 ? (v & 0x7fff) / 10 : v; }
function freqRaw(f) { f = Math.round(f); return f < 100 ? (f * 10) | 0x8000 : f; }
function dspLevel(raw) {
  let l = (raw >= 5000 ? raw - 5000 : raw) / 10;
  return l > 40 ? l - 40 : l;
}
function levelRaw(level, phase) {
  const n = Math.round(level);
  return (phase ? 0 : 5000) + (n ? (n + 40) * 10 : 0);
}
function dspDelayMs(raw) { return Math.round(Math.round(raw * 48 / 1000) * 1000 / 48) / 1000; }
function delayRaw(ms) { return Math.round(Math.round(ms * 48) * 1000 / 48); }
function gainRaw(db) { return Math.round(db * 10 + 500); }
function qRaw(q) { return Math.round(q * 600 / 19); }
function qOf(raw) { return raw * (19 / 6) / 100; }
function volRaw(v) { return 500 + Math.round(v); }
function volOf(raw) { return raw >= 500 ? raw - 500 : raw; }

function decodeFilter(code, highpass) {
  const tab = highpass ? HP_CODES : LP_CODES;
  for (let i = 0; i < tab.length; i++)
    if (code === tab[i])
      return {family: Math.floor(i / 9), slope: i % 9 === 8 ? 0 : (i % 9 + 1) * 6, known: true};
  return {family: -1, slope: 0, known: false};
}

function filterDb(f, cutoff, freq, hp) {
  if (!f.known || !f.slope || f.family === 1 || cutoff <= 0 || cutoff >= 24000) return 0;
  let ratio = Math.tan(Math.PI * freq / FS) / Math.tan(Math.PI * cutoff / FS);
  if (hp) ratio = 1 / ratio;
  const n = f.slope / 6;
  if (f.family === 2) return -20 * Math.log10(1 + Math.pow(ratio, n));
  return -10 * Math.log10(1 + Math.pow(ratio, 2 * n));
}

function peakingDb(f0, q, gainDb, f) {
  const w = 2 * Math.PI * f0 / FS;
  const cs = Math.cos(w), sn = Math.sin(w);
  const a = Math.pow(10, gainDb / 40);
  const al = sn / (2 * q);
  const b0 = 1 + al * a, b1 = -2 * cs, b2 = 1 - al * a;
  const a0 = 1 + al / a, a1 = -2 * cs, a2 = 1 - al / a;
  const w2 = 2 * Math.PI * f / FS;
  const c1 = Math.cos(w2), s1 = Math.sin(w2);
  const c2 = Math.cos(2 * w2), s2 = Math.sin(2 * w2);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
  const dr = 1 + (a1 / a0) * c1 + (a2 / a0) * c2, di = -((a1 / a0) * s1 + (a2 / a0) * s2);
  const mag = Math.sqrt(nr * nr + ni * ni) / Math.sqrt(dr * dr + di * di);
  return 20 * Math.log10(Math.max(mag, 1e-12));
}

function bandId(ch, band) { return 136 * ch + 147 + 4 * band; }

function curveDb(ch, freq) {
  const base = 136 * ch;
  let db = 0;
  for (let b = 0; b < 31; b++) {
    const id = base + 147 + 4 * b;
    const f = dspFreq(S.values[id]);
    const g = (S.values[id + 1] - 500) / 10;
    const q = S.values[id + 2] * (19 / 6) / 100;
    if (f >= 10 && f <= 23000 && q > 0 && g >= -30 && g <= 30)
      db += peakingDb(f, q, g, freq);
  }
  const hp = decodeFilter(S.values[base + 138], true);
  const lp = decodeFilter(S.values[base + 142], false);
  db += filterDb(hp, dspFreq(S.values[base + 139]), freq, true);
  db += filterDb(lp, dspFreq(S.values[base + 143]), freq, false);
  return db;
}

/* ---------- API ---------- */

async function api(path, body, method) {
  const m = method || (body === undefined ? "GET" : "POST");
  const opt = {method: m};
  if (body !== undefined) {
    opt.headers = {"Content-Type": "application/json"};
    opt.body = JSON.stringify(body);
  }
  const r = await fetch(path, opt);
  return r.json();
}

async function write(id, value) {
  const r = await api("/api/write", {id, value});
  if (r.ok) {
    S.values[id] = value;
    render();
  } else {
    toast("Gagal menulis ID " + id.toString(16).toUpperCase().padStart(4, "0"));
  }
  return r.ok;
}

async function poll() {
  try {
    const st = await api("/api/state");
    const changed = st.values.some((v, i) => v !== S.values[i]);
    Object.assign(S, st);
    if (changed) render();
    else renderStatus();
  } catch (e) {
    S.connected = false;
    S.status = "Server web tidak merespons";
    renderStatus();
  }
}

/* ---------- canvas EQ plot ---------- */

const canvas = document.getElementById("eq");
const ctx = canvas.getContext("2d");
let dragBand = -1;

const F_MIN = 20, F_MAX = 20000, DB_TOP = 20, DB_BOT = -20;

function xOfF(f, w, h, padL, padR) {
  const t = (Math.log10(f) - Math.log10(F_MIN)) / (Math.log10(F_MAX) - Math.log10(F_MIN));
  return padL + t * (w - padL - padR);
}
function fOfX(x, w, h, padL, padR) {
  const t = (x - padL) / (w - padL - padR);
  return F_MIN * Math.pow(F_MAX / F_MIN, Math.min(1, Math.max(0, t)));
}
function yOfDb(db, h, padT, padB) {
  const t = (DB_TOP - db) / (DB_TOP - DB_BOT);
  return padT + t * (h - padT - padB);
}
function dbOfY(y, h, padT, padB) {
  const t = (y - padT) / (h - padT - padB);
  return DB_TOP - t * (DB_TOP - DB_BOT);
}

function resizeCanvas() {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.round(rect.width * dpr);
  canvas.height = Math.round(rect.height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawEq();
}

function drawEq() {
  const w = canvas.getBoundingClientRect().width;
  const h = canvas.getBoundingClientRect().height;
  const padL = 46, padR = 14, padT = 12, padB = 26;
  ctx.clearRect(0, 0, w, h);

  ctx.fillStyle = "#101720";
  ctx.fillRect(0, 0, w, h);

  ctx.font = "11px sans-serif";
  ctx.textAlign = "center";
  for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
    const x = xOfF(f, w, h, padL, padR);
    ctx.strokeStyle = "#2d3b4a";
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, h - padB);
    ctx.stroke();
    ctx.fillStyle = "#a5b4c4";
    ctx.fillText(f >= 1000 ? (f / 1000) + "K" : String(f), x, h - padB + 16);
  }
  ctx.textAlign = "right";
  for (let d = -20; d <= 20; d += 5) {
    const y = yOfDb(d, h, padT, padB);
    ctx.strokeStyle = d === 0 ? "#536579" : "#2d3b4a";
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(w - padR, y);
    ctx.stroke();
    ctx.fillStyle = "#a5b4c4";
    ctx.fillText((d > 0 ? "+" : "") + d, padL - 6, y + 4);
  }
  ctx.textAlign = "center";
  ctx.fillText("Hz", w - padR - 8, h - 6);
  ctx.textAlign = "left";
  ctx.fillText("dB", 4, padT + 4);

  if (!S.valid) {
    ctx.fillStyle = "#a5b4c4";
    ctx.textAlign = "center";
    ctx.fillText("Belum ada data USB - klik Baca DSP", w / 2, h / 2);
    return;
  }

  const ch = S.ch;
  const base = 136 * ch;
  const hp = decodeFilter(S.values[base + 138], true);
  const lp = decodeFilter(S.values[base + 142], false);

  ctx.setLineDash([4, 4]);
  ctx.strokeStyle = "#f4ba63";
  const hpF = dspFreq(S.values[base + 139]);
  const lpF = dspFreq(S.values[base + 143]);
  if (hp.known && hp.slope && hpF >= 20 && hpF <= 20000) {
    const x = xOfF(hpF, w, h, padL, padR);
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h - padB); ctx.stroke();
  }
  if (lp.known && lp.slope && lpF >= 20 && lpF <= 20000) {
    const x = xOfF(lpF, w, h, padL, padR);
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h - padB); ctx.stroke();
  }
  ctx.setLineDash([]);

  ctx.strokeStyle = "#59d8cd";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  let first = true;
  for (let x = 0; x <= w - padL - padR; x += 2) {
    const f = fOfX(x + padL, w, h, padL, padR);
    const db = Math.max(DB_BOT, Math.min(DB_TOP, curveDb(ch, f)));
    const y = yOfDb(db, h, padT, padB);
    if (first) { ctx.moveTo(padL + x, y); first = false; }
    else ctx.lineTo(padL + x, y);
  }
  ctx.stroke();
  ctx.lineWidth = 1;

  for (let b = 0; b < 31; b++) {
    const id = base + 147 + 4 * b;
    const f = dspFreq(S.values[id]);
    const g = (S.values[id + 1] - 500) / 10;
    if (f < F_MIN || f > F_MAX) continue;
    const x = xOfF(f, w, h, padL, padR);
    const y = yOfDb(Math.max(DB_BOT, Math.min(DB_TOP, g)), h, padT, padB);
    const sel = b === S.band;
    ctx.fillStyle = sel ? "#ffbe18" : "#59d8cd";
    ctx.beginPath();
    ctx.arc(x, y, sel ? 6 : 4, 0, Math.PI * 2);
    ctx.fill();
  }
}

function canvasPos(e) {
  const r = canvas.getBoundingClientRect();
  return {x: e.clientX - r.left, y: e.clientY - r.top};
}

function handleAt(pos) {
  const w = canvas.getBoundingClientRect().width;
  const h = canvas.getBoundingClientRect().height;
  const padL = 46, padR = 14, padT = 12, padB = 26;
  const base = 136 * S.ch;
  let best = -1, bd = 12;
  for (let b = 0; b < 31; b++) {
    const id = base + 147 + 4 * b;
    const f = dspFreq(S.values[id]);
    const g = (S.values[id + 1] - 500) / 10;
    if (f < F_MIN || f > F_MAX) continue;
    const x = xOfF(f, w, h, padL, padR);
    const y = yOfDb(Math.max(DB_BOT, Math.min(DB_TOP, g)), h, padT, padB);
    const d = Math.hypot(x - pos.x, y - pos.y);
    if (d < bd) { bd = d; best = b; }
  }
  return best;
}

canvas.addEventListener("mousedown", e => {
  if (!S.valid) return;
  const b = handleAt(canvasPos(e));
  if (b >= 0) {
    dragBand = b;
    S.band = b;
    renderBandInfo();
    renderBandTable();
  }
});
canvas.addEventListener("mousemove", e => {
  if (!S.valid) return;
  const pos = canvasPos(e);
  if (dragBand >= 0) {
    const h = canvas.getBoundingClientRect().height;
    const db = Math.max(-12, Math.min(12, dbOfY(pos.y, h, 12, 26)));
    const id = bandId(S.ch, dragBand) + 1;
    S.values[id] = gainRaw(db);
    drawEq();
    renderBandInfo();
    renderBandTable();
  } else {
    canvas.style.cursor = handleAt(pos) >= 0 ? "pointer" : "crosshair";
  }
});
window.addEventListener("mouseup", () => {
  if (dragBand >= 0) {
    const id = bandId(S.ch, dragBand) + 1;
    write(id, S.values[id]);
    dragBand = -1;
  }
});

/* ---------- rendering ---------- */

const $ = id => document.getElementById(id);

function renderStatus() {
  const el = $("status");
  el.textContent = S.status;
  el.className = "status" + (S.connected ? " connected" : "");
  $("btn-connect").textContent = S.connected ? "Disconnect" : "Connect";
  $("btn-read").disabled = !S.connected || S.reading;
  const prog = $("progress");
  if (S.reading) {
    prog.classList.remove("hidden");
    const pct = S.total ? (S.progress / S.total) * 100 : 0;
    $("progress-bar").style.width = pct + "%";
    $("progress-text").textContent = S.progress + "/" + S.total;
  } else {
    prog.classList.add("hidden");
  }
}

function renderChannels() {
  const nav = $("channels");
  nav.innerHTML = "";
  for (let c = 0; c < 8; c++) {
    const b = document.createElement("button");
    b.innerHTML = "CH" + (c + 1) + '<span class="role">' + ROLES[c] + "</span>";
    if (c === S.ch) b.classList.add("active");
    b.onclick = () => { S.ch = c; render(); };
    nav.appendChild(b);
  }
}

function renderBandInfo() {
  const id = bandId(S.ch, S.band);
  const f = dspFreq(S.values[id]);
  const q = qOf(S.values[id + 2]);
  const g = (S.values[id + 1] - 500) / 10;
  $("band-info").textContent = "Band " + (S.band + 1) +
    "  F:" + f + " Hz   Q:" + q.toFixed(2) + "   G:" + g.toFixed(1) + " dB" +
    "   |   Geser titik band untuk mengubah gain";
}

function renderCrossover() {
  const base = 136 * S.ch;
  const hp = decodeFilter(S.values[base + 138], true);
  const lp = decodeFilter(S.values[base + 142], false);
  setInputValue("hp-freq", dspFreq(S.values[base + 139]));
  setInputValue("lp-freq", dspFreq(S.values[base + 143]));
  $("xover-family").value = String(hp.known ? hp.family : 0);
  $("xover-slope").value = String(hp.known ? (hp.slope ? hp.slope / 6 - 1 : 8) : 3);
}

function setInputValue(id, v) {
  const el = $(id);
  if (document.activeElement !== el) el.value = v;
}

function renderChannel() {
  const c = S.ch;
  const level = S.valid ? dspLevel(S.values[26 + c]) : 0;
  const phase = S.valid && S.values[26 + c] < 5000;
  const mute = S.valid && !S.values[2 + c];
  const ms = S.valid ? dspDelayMs(S.values[73 + c]) : 0;
  $("ch-level").value = level;
  $("ch-level-val").textContent = S.valid ? level.toFixed(1) : "--";
  $("ch-delay").value = ms;
  $("ch-delay-val").textContent = S.valid ? ms.toFixed(3) : "--";
  $("ch-phase").textContent = "Fase " + (phase ? "180'" : "0'");
  $("ch-phase").classList.toggle("active", !!phase);
  $("ch-mute").textContent = mute ? "Unmute" : "Mute";
  $("ch-mute").classList.toggle("active", !!mute);
}

function renderInput() {
  const src = S.values[1554];
  document.querySelectorAll("#input-btns button").forEach(b => {
    b.classList.toggle("active", S.valid && Number(b.dataset.src) === src);
  });
  const usb = S.valid ? volOf(S.values[1908]) : 0;
  const bt = S.valid ? volOf(S.values[1900]) : 0;
  $("usb-vol").value = usb;
  $("usb-vol-val").textContent = S.valid ? String(usb) : "--";
  $("bt-vol").value = bt;
  $("bt-vol-val").textContent = S.valid ? String(bt) : "--";
}

function renderMaster() {
  const v = S.valid ? dspLevel(S.values[12]) : 0;
  $("master-vol").value = v;
  $("master-vol-val").textContent = S.valid ? v.toFixed(1) : "--";
  const idx = NOISE_CODES.indexOf(S.values[1565]);
  $("noise-gate").value = String(idx >= 0 ? idx : 0);
}

function renderMixer() {
  const box = $("mixer");
  if (!S.valid) {
    box.innerHTML = "<p style='color:var(--dim)'>Baca DSP terlebih dahulu.</p>";
    return;
  }
  const src = S.values[1554];
  if (![1, 2, 3].includes(src)) {
    box.innerHTML = "<p style='color:var(--dim)'>Pilih AUX, Bluetooth atau High level.</p>";
    return;
  }
  const rows = src === 3 ? 4 : 2;
  const base = src === 3 ? 1226 : src === 1 ? 1482 : 1360;
  let html = "<table><tr><th></th>";
  for (let c = 0; c < 8; c++) html += "<th>CH" + (c + 1) + "</th>";
  html += "</tr>";
  for (let i = 0; i < rows; i++) {
    html += "<tr><th>" + (src === 3 ? "HiLevel" + (i + 1) : (src === 1 ? "AUX-" : "BT-") + (i ? "R" : "L")) + "</th>";
    for (let c = 0; c < 8; c++) {
      const id = base + c * 8 + i;
      const v = S.values[id];
      const on = (v & 1) !== 0;
      const gain = Math.floor(v / 256);
      html += "<td><span class='pwr " + (on ? "on" : "off") + "' data-id='" + id + "'>" + (on ? "⏻" : "○") + "</span>" +
        "<input type='range' min='0' max='100' value='" + gain + "' data-id='" + id + "'></td>";
    }
    html += "</tr>";
  }
  html += "</table>";
  box.innerHTML = html;
  box.querySelectorAll(".pwr").forEach(el => {
    el.onclick = () => {
      const id = Number(el.dataset.id);
      write(id, S.values[id] ^ 1);
    };
  });
  box.querySelectorAll("input[type=range]").forEach(el => {
    el.onchange = () => {
      const id = Number(el.dataset.id);
      write(id, Math.round(Number(el.value)) * 256 + (S.values[id] & 1));
    };
  });
}

function renderBandTable() {
  const tbody = document.querySelector("#band-table tbody");
  if (!tbody.dataset.built) {
    let html = "";
    for (let b = 0; b < 31; b++) {
      html += "<tr data-band='" + b + "'>" +
        "<td>" + (b + 1) + "</td>" +
        "<td class='editable' data-col='f'></td>" +
        "<td class='editable' data-col='q'></td>" +
        "<td class='editable' data-col='g'></td></tr>";
    }
    tbody.innerHTML = html;
    tbody.dataset.built = "1";
    tbody.querySelectorAll("td.editable").forEach(td => {
      td.onclick = () => startEdit(td);
    });
  }
  const base = 136 * S.ch;
  tbody.querySelectorAll("tr").forEach(tr => {
    const b = Number(tr.dataset.band);
    const id = base + 147 + 4 * b;
    const f = dspFreq(S.values[id]);
    const q = qOf(S.values[id + 2]);
    const g = (S.values[id + 1] - 500) / 10;
    const cells = tr.children;
    if (!cells[1].classList.contains("editing")) {
      cells[1].textContent = f;
      cells[2].textContent = q.toFixed(2);
      cells[3].textContent = g.toFixed(1);
    }
    tr.classList.toggle("selected", b === S.band);
  });
}

function startEdit(td) {
  const tr = td.closest("tr");
  const b = Number(tr.dataset.band);
  const col = td.dataset.col;
  const id = bandId(S.ch, b);
  const ranges = {f: [20, 20000], q: [0.1, 30], g: [-12, 12]};
  const vals = {f: dspFreq(S.values[id]), q: qOf(S.values[id + 2]), g: (S.values[id + 1] - 500) / 10};
  td.classList.add("editing");
  td.innerHTML = "<input type='number' step='any' value='" + vals[col] + "'>";
  const input = td.querySelector("input");
  input.focus();
  input.select();
  let done = false;
  const commit = ok => {
    if (done) return;
    done = true;
    if (ok) {
      let v = Number(input.value);
      const [lo, hi] = ranges[col];
      v = Math.max(lo, Math.min(hi, v));
      if (col === "f") write(id, freqRaw(v));
      else if (col === "q") write(id + 2, qRaw(v));
      else write(id + 1, gainRaw(v));
    }
    renderBandTable();
  };
  input.onkeydown = e => {
    if (e.key === "Enter") commit(true);
    else if (e.key === "Escape") commit(false);
    e.stopPropagation();
  };
  input.onblur = () => commit(true);
}

function render() {
  renderStatus();
  renderChannels();
  renderBandInfo();
  renderCrossover();
  renderChannel();
  renderInput();
  renderMaster();
  renderMixer();
  renderBandTable();
  drawEq();
}

/* ---------- controls ---------- */

$("btn-connect").onclick = async () => {
  if (S.connected) {
    await api("/api/disconnect", null, "POST");
    S.connected = false;
    S.valid = false;
    render();
  } else {
    const r = await api("/api/connect", null, "POST");
    if (r.ok) {
      S.connected = true;
      S.valid = false;
      render();
    } else {
      toast("Connect gagal");
    }
  }
};

$("btn-read").onclick = async () => {
  const r = await api("/api/read", null, "POST");
  if (!r.ok) toast("Baca DSP gagal");
};

function bindNumber(id, fn) {
  $(id).onchange = () => fn(Number($(id).value));
}
bindNumber("hp-freq", v => {
  const base = 136 * S.ch;
  write(base + 139, freqRaw(v));
});
bindNumber("lp-freq", v => {
  const base = 136 * S.ch;
  write(base + 143, freqRaw(v));
});
$("xover-family").onchange = () => writeXover();
$("xover-slope").onchange = () => writeXover();

function writeXover() {
  const base = 136 * S.ch;
  const family = Number($("xover-family").value);
  const rate = Number($("xover-slope").value);
  write(base + 138, HP_CODES[family * 9 + rate]);
  write(base + 142, LP_CODES[family * 9 + rate]);
}

$("ch-level").onchange = () => {
  const c = S.ch;
  const phase = S.valid && S.values[26 + c] < 5000;
  write(26 + c, levelRaw(Number($("ch-level").value), phase));
};
$("ch-delay").onchange = () => {
  write(73 + S.ch, delayRaw(Number($("ch-delay").value)));
};
$("ch-phase").onclick = () => {
  const c = S.ch;
  const phase = !(S.valid && S.values[26 + c] < 5000);
  write(26 + c, levelRaw(dspLevel(S.values[26 + c]), phase));
};
$("ch-mute").onclick = () => {
  const c = S.ch;
  write(2 + c, S.values[2 + c] ? 0 : 1);
};
$("ch-reset-eq").onclick = async () => {
  const base = 136 * S.ch;
  for (let b = 0; b < 31; b++) {
    const ok = await write(base + 148 + 4 * b, 500);
    if (!ok) break;
  }
};

document.querySelectorAll("#input-btns button").forEach(b => {
  b.onclick = () => write(1554, Number(b.dataset.src));
});

$("usb-vol").onchange = () => write(1908, volRaw(Number($("usb-vol").value)));
$("bt-vol").onchange = () => write(1900, volRaw(Number($("bt-vol").value)));
$("master-vol").onchange = () => {
  write(12, levelRaw(Number($("master-vol").value), false));
};

{
  const sel = $("noise-gate");
  for (let i = 0; i < NOISE_CODES.length; i++) {
    const o = document.createElement("option");
    o.value = String(i);
    o.textContent = i === 0 ? "OFF" : "Tingkat " + i;
    sel.appendChild(o);
  }
  sel.onchange = () => write(1565, NOISE_CODES[Number(sel.value)]);
}

let toastTimer = null;
function toast(msg) {
  const el = $("toast");
  el.textContent = msg;
  el.classList.remove("hidden");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.add("hidden"), 3000);
}

window.addEventListener("resize", resizeCanvas);

render();
resizeCanvas();
setInterval(poll, 1000);
poll();
