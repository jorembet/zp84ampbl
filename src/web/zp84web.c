#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

/* Minimal dependency-free HTTP server exposing the amplifier USB protocol
   (src/hid) as a JSON API so a browser can edit the DSP. Binds to loopback
   only: the write API is unauthenticated, so it must not be exposed. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../hid/zp_hid.h"

#define DSP_READ_BATCH 8
#define MAX_CLIENTS 8
#define REQ_MAX 16384

static zp_conn conn;
static int connected;
static uint16_t values[ZP_PARAM_COUNT];
static uint16_t pending[ZP_PARAM_COUNT];
static int dsp_valid;
static int reading, read_next;
static char status[160];
static char dsp_time[32];
static int device_lock = -1;

/* ===== Embedded frontend (single-file build) ===== */
static const char INDEX_HTML[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"id\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>ZP 8.4 AMP - DSP Editor Web</title>\n"
    "<link rel=\"stylesheet\" href=\"style.css\">\n"
    "</head>\n"
    "<body>\n"
    "<header>\n"
    "  <h1>ZP 8.4 AMP <span>DSP Editor Web</span></h1>\n"
    "  <div class=\"actions\">\n"
    "    <span id=\"status\" class=\"status\">Terputus</span>\n"
    "    <button id=\"btn-connect\" class=\"btn primary\">Connect</button>\n"
    "    <button id=\"btn-read\" class=\"btn\">Baca DSP</button>\n"
    "  </div>\n"
    "</header>\n"
    "\n"
    "<div id=\"progress\" class=\"hidden\">\n"
    "  <div id=\"progress-bar\"></div>\n"
    "  <span id=\"progress-text\"></span>\n"
    "</div>\n"
    "\n"
    "<nav id=\"channels\"></nav>\n"
    "\n"
    "<main>\n"
    "  <section class=\"eq-panel\">\n"
    "    <canvas id=\"eq\"></canvas>\n"
    "    <div id=\"band-info\" class=\"band-info\">Geser titik band untuk mengubah gain</div>\n"
    "  </section>\n"
    "\n"
    "  <aside class=\"side\">\n"
    "    <div class=\"card\">\n"
    "      <h2>Crossover</h2>\n"
    "      <div class=\"row\">\n"
    "        <label>HPF</label>\n"
    "        <input type=\"number\" id=\"hp-freq\" min=\"10\" max=\"23000\" step=\"1\">\n"
    "        <span class=\"unit\">Hz</span>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>LPF</label>\n"
    "        <input type=\"number\" id=\"lp-freq\" min=\"10\" max=\"23000\" step=\"1\">\n"
    "        <span class=\"unit\">Hz</span>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>Tipe</label>\n"
    "        <select id=\"xover-family\">\n"
    "          <option value=\"0\">Butter-W</option>\n"
    "          <option value=\"1\">Bessel</option>\n"
    "          <option value=\"2\">Link_R</option>\n"
    "        </select>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>Slope</label>\n"
    "        <select id=\"xover-slope\">\n"
    "          <option value=\"8\">OFF</option>\n"
    "          <option value=\"0\">6 dB/oct</option>\n"
    "          <option value=\"1\">12 dB/oct</option>\n"
    "          <option value=\"2\">18 dB/oct</option>\n"
    "          <option value=\"3\">24 dB/oct</option>\n"
    "          <option value=\"4\">30 dB/oct</option>\n"
    "          <option value=\"5\">36 dB/oct</option>\n"
    "          <option value=\"6\">42 dB/oct</option>\n"
    "          <option value=\"7\">48 dB/oct</option>\n"
    "        </select>\n"
    "      </div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"card\">\n"
    "      <h2>Channel</h2>\n"
    "      <div class=\"row\">\n"
    "        <label>Level</label>\n"
    "        <input type=\"range\" id=\"ch-level\" min=\"0\" max=\"60\" step=\"0.5\" value=\"0\">\n"
    "        <span id=\"ch-level-val\" class=\"val\">--</span>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>Delay</label>\n"
    "        <input type=\"range\" id=\"ch-delay\" min=\"0\" max=\"20\" step=\"0.01\" value=\"0\">\n"
    "        <span id=\"ch-delay-val\" class=\"val\">--</span>\n"
    "      </div>\n"
    "      <div class=\"row btns\">\n"
    "        <button id=\"ch-phase\" class=\"btn small\">Fase 0'</button>\n"
    "        <button id=\"ch-mute\" class=\"btn small\">Mute</button>\n"
    "        <button id=\"ch-reset-eq\" class=\"btn small\">Reset EQ</button>\n"
    "      </div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"card\">\n"
    "      <h2>Input</h2>\n"
    "      <div class=\"row btns\" id=\"input-btns\">\n"
    "        <button class=\"btn small\" data-src=\"1\">AUX</button>\n"
    "        <button class=\"btn small\" data-src=\"2\">Bluetooth</button>\n"
    "        <button class=\"btn small\" data-src=\"3\">High level</button>\n"
    "        <button class=\"btn small\" data-src=\"7\">USB media</button>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>USB vol</label>\n"
    "        <input type=\"range\" id=\"usb-vol\" min=\"0\" max=\"100\" step=\"1\" value=\"0\">\n"
    "        <span id=\"usb-vol-val\" class=\"val\">--</span>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>BT vol</label>\n"
    "        <input type=\"range\" id=\"bt-vol\" min=\"0\" max=\"100\" step=\"1\" value=\"0\">\n"
    "        <span id=\"bt-vol-val\" class=\"val\">--</span>\n"
    "      </div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"card\">\n"
    "      <h2>Master</h2>\n"
    "      <div class=\"row\">\n"
    "        <label>Volume</label>\n"
    "        <input type=\"range\" id=\"master-vol\" min=\"0\" max=\"60\" step=\"0.5\" value=\"0\">\n"
    "        <span id=\"master-vol-val\" class=\"val\">--</span>\n"
    "      </div>\n"
    "      <div class=\"row\">\n"
    "        <label>Noise gate</label>\n"
    "        <select id=\"noise-gate\"></select>\n"
    "      </div>\n"
    "    </div>\n"
    "\n"
    "    <details class=\"card\">\n"
    "      <summary>Mixer</summary>\n"
    "      <div id=\"mixer\"></div>\n"
    "    </details>\n"
    "  </aside>\n"
    "</main>\n"
    "\n"
    "<section class=\"bands\">\n"
    "  <table id=\"band-table\">\n"
    "    <thead>\n"
    "      <tr><th>Band</th><th>Freq (Hz)</th><th>Q</th><th>Gain (dB)</th></tr>\n"
    "    </thead>\n"
    "    <tbody></tbody>\n"
    "  </table>\n"
    "</section>\n"
    "\n"
    "<div id=\"toast\" class=\"hidden\"></div>\n"
    "<script src=\"app.js\"></script>\n"
    "</body>\n"
    "</html>\n"
    "";

static const char STYLE_CSS[] =
    ":root {\n"
    "  --bg: #101720;\n"
    "  --panel: #1d2936;\n"
    "  --panel2: #182230;\n"
    "  --border: #3a4a5c;\n"
    "  --text: #e8e8ea;\n"
    "  --dim: #a5b4c4;\n"
    "  --accent: #59d8cd;\n"
    "  --sel: #ffbe18;\n"
    "  --good: #4ade80;\n"
    "  --warn: #ef4444;\n"
    "}\n"
    "\n"
    "* { box-sizing: border-box; }\n"
    "\n"
    "body {\n"
    "  margin: 0;\n"
    "  background: var(--bg);\n"
    "  color: var(--text);\n"
    "  font-family: \"Segoe UI\", system-ui, sans-serif;\n"
    "  font-size: 14px;\n"
    "}\n"
    "\n"
    "header {\n"
    "  display: flex;\n"
    "  align-items: center;\n"
    "  justify-content: space-between;\n"
    "  padding: 10px 16px;\n"
    "  background: var(--panel);\n"
    "  border-bottom: 1px solid var(--border);\n"
    "}\n"
    "\n"
    "h1 { font-size: 18px; margin: 0; font-weight: 600; }\n"
    "h1 span { color: var(--dim); font-weight: 400; font-size: 13px; margin-left: 8px; }\n"
    "\n"
    ".actions { display: flex; align-items: center; gap: 8px; }\n"
    "\n"
    ".status {\n"
    "  padding: 4px 10px;\n"
    "  border-radius: 4px;\n"
    "  background: var(--panel2);\n"
    "  color: var(--dim);\n"
    "  max-width: 460px;\n"
    "  overflow: hidden;\n"
    "  text-overflow: ellipsis;\n"
    "  white-space: nowrap;\n"
    "}\n"
    ".status.connected { color: var(--good); }\n"
    ".status.error { color: var(--warn); }\n"
    "\n"
    ".btn {\n"
    "  background: var(--panel2);\n"
    "  color: var(--text);\n"
    "  border: 1px solid var(--border);\n"
    "  border-radius: 4px;\n"
    "  padding: 6px 14px;\n"
    "  cursor: pointer;\n"
    "  font-size: 13px;\n"
    "}\n"
    ".btn:hover { border-color: var(--accent); }\n"
    ".btn.primary { background: var(--accent); color: #10232c; border-color: var(--accent); font-weight: 600; }\n"
    ".btn.small { padding: 4px 10px; font-size: 12px; }\n"
    ".btn.active { background: var(--sel); color: #1a1a10; border-color: var(--sel); }\n"
    ".btn:disabled { opacity: 0.4; cursor: default; }\n"
    "\n"
    "#progress {\n"
    "  display: flex;\n"
    "  align-items: center;\n"
    "  gap: 10px;\n"
    "  padding: 6px 16px;\n"
    "  background: var(--panel2);\n"
    "  border-bottom: 1px solid var(--border);\n"
    "}\n"
    "#progress-bar {\n"
    "  height: 6px;\n"
    "  width: 0%;\n"
    "  background: var(--accent);\n"
    "  border-radius: 3px;\n"
    "  transition: width 0.15s;\n"
    "}\n"
    "#progress-text { color: var(--dim); font-size: 12px; }\n"
    "\n"
    ".hidden { display: none !important; }\n"
    "\n"
    "#channels {\n"
    "  display: flex;\n"
    "  gap: 4px;\n"
    "  padding: 8px 16px 0;\n"
    "}\n"
    "#channels button {\n"
    "  background: var(--panel);\n"
    "  border: 1px solid var(--border);\n"
    "  color: var(--dim);\n"
    "  padding: 6px 14px;\n"
    "  cursor: pointer;\n"
    "  font-size: 13px;\n"
    "}\n"
    "#channels button.active {\n"
    "  background: var(--accent);\n"
    "  color: #10232c;\n"
    "  border-color: var(--accent);\n"
    "  font-weight: 600;\n"
    "}\n"
    "#channels button .role { display: block; font-size: 10px; opacity: 0.75; }\n"
    "\n"
    "main {\n"
    "  display: flex;\n"
    "  gap: 12px;\n"
    "  padding: 12px 16px;\n"
    "  align-items: flex-start;\n"
    "}\n"
    "\n"
    ".eq-panel {\n"
    "  flex: 1;\n"
    "  min-width: 0;\n"
    "  background: var(--panel);\n"
    "  border: 1px solid var(--border);\n"
    "  border-radius: 6px;\n"
    "  padding: 8px;\n"
    "}\n"
    "\n"
    "#eq { width: 100%; height: 460px; display: block; cursor: crosshair; }\n"
    "\n"
    ".band-info {\n"
    "  color: var(--dim);\n"
    "  font-size: 12px;\n"
    "  padding: 6px 4px 2px;\n"
    "  min-height: 20px;\n"
    "}\n"
    "\n"
    ".side {\n"
    "  width: 300px;\n"
    "  flex-shrink: 0;\n"
    "  display: flex;\n"
    "  flex-direction: column;\n"
    "  gap: 10px;\n"
    "}\n"
    "\n"
    ".card {\n"
    "  background: var(--panel);\n"
    "  border: 1px solid var(--border);\n"
    "  border-radius: 6px;\n"
    "  padding: 10px 12px;\n"
    "}\n"
    ".card h2 { margin: 0 0 8px; font-size: 13px; color: var(--dim); text-transform: uppercase; letter-spacing: 0.5px; }\n"
    ".card summary { cursor: pointer; font-size: 13px; color: var(--dim); text-transform: uppercase; letter-spacing: 0.5px; }\n"
    "\n"
    ".row {\n"
    "  display: flex;\n"
    "  align-items: center;\n"
    "  gap: 8px;\n"
    "  margin: 6px 0;\n"
    "}\n"
    ".row label { width: 70px; flex-shrink: 0; color: var(--dim); font-size: 12px; }\n"
    ".row input[type=\"range\"] { flex: 1; accent-color: var(--accent); }\n"
    ".row input[type=\"number\"], .row select {\n"
    "  flex: 1;\n"
    "  background: var(--panel2);\n"
    "  border: 1px solid var(--border);\n"
    "  color: var(--text);\n"
    "  border-radius: 4px;\n"
    "  padding: 4px 6px;\n"
    "  font-size: 13px;\n"
    "}\n"
    ".row .unit { color: var(--dim); font-size: 11px; width: 20px; }\n"
    ".row .val { width: 52px; text-align: right; font-size: 12px; color: var(--text); font-variant-numeric: tabular-nums; }\n"
    ".row.btns { flex-wrap: wrap; }\n"
    "\n"
    ".bands { padding: 0 16px 16px; }\n"
    "#band-table {\n"
    "  width: 100%;\n"
    "  border-collapse: collapse;\n"
    "  background: var(--panel);\n"
    "  border: 1px solid var(--border);\n"
    "  border-radius: 6px;\n"
    "  overflow: hidden;\n"
    "}\n"
    "#band-table th, #band-table td {\n"
    "  padding: 4px 10px;\n"
    "  text-align: left;\n"
    "  border-bottom: 1px solid var(--panel2);\n"
    "  font-size: 12px;\n"
    "}\n"
    "#band-table th { color: var(--dim); font-weight: 600; }\n"
    "#band-table td.editable { cursor: pointer; font-variant-numeric: tabular-nums; }\n"
    "#band-table td.editable:hover { background: var(--panel2); }\n"
    "#band-table td.editing { padding: 0; }\n"
    "#band-table td.editing input {\n"
    "  width: 100%;\n"
    "  background: var(--panel2);\n"
    "  border: 1px solid var(--accent);\n"
    "  color: var(--text);\n"
    "  padding: 3px 8px;\n"
    "  font-size: 12px;\n"
    "}\n"
    "#band-table tr.selected td { background: #233241; }\n"
    "\n"
    "#mixer { margin-top: 8px; }\n"
    "#mixer table { width: 100%; border-collapse: collapse; font-size: 11px; }\n"
    "#mixer th, #mixer td { padding: 3px 4px; text-align: center; }\n"
    "#mixer th { color: var(--dim); font-weight: 600; }\n"
    "#mixer input[type=\"range\"] { width: 100%; accent-color: var(--accent); }\n"
    "#mixer .pwr { cursor: pointer; user-select: none; }\n"
    "#mixer .pwr.on { color: var(--good); }\n"
    "#mixer .pwr.off { color: var(--dim); }\n"
    "\n"
    "#toast {\n"
    "  position: fixed;\n"
    "  bottom: 20px;\n"
    "  left: 50%;\n"
    "  transform: translateX(-50%);\n"
    "  background: var(--warn);\n"
    "  color: #fff;\n"
    "  padding: 10px 18px;\n"
    "  border-radius: 6px;\n"
    "  font-size: 13px;\n"
    "  z-index: 100;\n"
    "  max-width: 80%;\n"
    "}\n"
    "\n"
    "@media (max-width: 1100px) {\n"
    "  main { flex-direction: column; }\n"
    "  .side { width: 100%; }\n"
    "}\n"
    "";

static const char APP_JS[] =
    "\"use strict\";\n"
    "\n"
    "const FS = 48000;\n"
    "const ROLES = [\"FL-Tweeter\", \"FR-Tweeter\", \"FL-Woofer\", \"FR-Woofer\",\n"
    "               \"FL-Midrange\", \"FR-Midrange\", \"L-Subwoofer\", \"R-Subwoofer\"];\n"
    "const SOURCE_NAMES = {1: \"AUX\", 2: \"Bluetooth\", 3: \"High level\", 7: \"USB media\"};\n"
    "const NOISE_CODES = [0,1,2,3,4,5,6,7,8,9,10,11,13,14,16,18,23,29,41,65,103];\n"
    "const HP_CODES = [40,8,14,20,46,52,58,26,64,42,10,16,22,48,54,60,28,66,44,12,18,24,50,56,62,30,68];\n"
    "const LP_CODES = [41,9,15,21,47,53,59,27,65,43,11,17,23,49,55,61,29,67,45,13,19,25,51,57,63,31,69];\n"
    "\n"
    "const S = {\n"
    "  connected: false,\n"
    "  reading: false,\n"
    "  valid: false,\n"
    "  progress: 0,\n"
    "  total: 0,\n"
    "  time: \"\",\n"
    "  status: \"\",\n"
    "  values: new Array(1934).fill(0),\n"
    "  ch: 2,\n"
    "  band: 26,\n"
    "};\n"
    "\n"
    "/* ---------- protocol conversions (mirror src/gui) ---------- */\n"
    "\n"
    "function dspFreq(v) { return v & 0x8000 ? (v & 0x7fff) / 10 : v; }\n"
    "function freqRaw(f) { f = Math.round(f); return f < 100 ? (f * 10) | 0x8000 : f; }\n"
    "function dspLevel(raw) {\n"
    "  let l = (raw >= 5000 ? raw - 5000 : raw) / 10;\n"
    "  return l > 40 ? l - 40 : l;\n"
    "}\n"
    "function levelRaw(level, phase) {\n"
    "  const n = Math.round(level);\n"
    "  return (phase ? 0 : 5000) + (n ? (n + 40) * 10 : 0);\n"
    "}\n"
    "function dspDelayMs(raw) { return Math.round(Math.round(raw * 48 / 1000) * 1000 / 48) / 1000; }\n"
    "function delayRaw(ms) { return Math.round(Math.round(ms * 48) * 1000 / 48); }\n"
    "function gainRaw(db) { return Math.round(db * 10 + 500); }\n"
    "function qRaw(q) { return Math.round(q * 600 / 19); }\n"
    "function qOf(raw) { return raw * (19 / 6) / 100; }\n"
    "function volRaw(v) { return 500 + Math.round(v); }\n"
    "function volOf(raw) { return raw >= 500 ? raw - 500 : raw; }\n"
    "\n"
    "function decodeFilter(code, highpass) {\n"
    "  const tab = highpass ? HP_CODES : LP_CODES;\n"
    "  for (let i = 0; i < tab.length; i++)\n"
    "    if (code === tab[i])\n"
    "      return {family: Math.floor(i / 9), slope: i % 9 === 8 ? 0 : (i % 9 + 1) * 6, known: true};\n"
    "  return {family: -1, slope: 0, known: false};\n"
    "}\n"
    "\n"
    "function filterDb(f, cutoff, freq, hp) {\n"
    "  if (!f.known || !f.slope || f.family === 1 || cutoff <= 0 || cutoff >= 24000) return 0;\n"
    "  let ratio = Math.tan(Math.PI * freq / FS) / Math.tan(Math.PI * cutoff / FS);\n"
    "  if (hp) ratio = 1 / ratio;\n"
    "  const n = f.slope / 6;\n"
    "  if (f.family === 2) return -20 * Math.log10(1 + Math.pow(ratio, n));\n"
    "  return -10 * Math.log10(1 + Math.pow(ratio, 2 * n));\n"
    "}\n"
    "\n"
    "function peakingDb(f0, q, gainDb, f) {\n"
    "  const w = 2 * Math.PI * f0 / FS;\n"
    "  const cs = Math.cos(w), sn = Math.sin(w);\n"
    "  const a = Math.pow(10, gainDb / 40);\n"
    "  const al = sn / (2 * q);\n"
    "  const b0 = 1 + al * a, b1 = -2 * cs, b2 = 1 - al * a;\n"
    "  const a0 = 1 + al / a, a1 = -2 * cs, a2 = 1 - al / a;\n"
    "  const w2 = 2 * Math.PI * f / FS;\n"
    "  const c1 = Math.cos(w2), s1 = Math.sin(w2);\n"
    "  const c2 = Math.cos(2 * w2), s2 = Math.sin(2 * w2);\n"
    "  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);\n"
    "  const dr = 1 + (a1 / a0) * c1 + (a2 / a0) * c2, di = -((a1 / a0) * s1 + (a2 / a0) * s2);\n"
    "  const mag = Math.sqrt(nr * nr + ni * ni) / Math.sqrt(dr * dr + di * di);\n"
    "  return 20 * Math.log10(Math.max(mag, 1e-12));\n"
    "}\n"
    "\n"
    "function bandId(ch, band) { return 136 * ch + 147 + 4 * band; }\n"
    "\n"
    "function curveDb(ch, freq) {\n"
    "  const base = 136 * ch;\n"
    "  let db = 0;\n"
    "  for (let b = 0; b < 31; b++) {\n"
    "    const id = base + 147 + 4 * b;\n"
    "    const f = dspFreq(S.values[id]);\n"
    "    const g = (S.values[id + 1] - 500) / 10;\n"
    "    const q = S.values[id + 2] * (19 / 6) / 100;\n"
    "    if (f >= 10 && f <= 23000 && q > 0 && g >= -30 && g <= 30)\n"
    "      db += peakingDb(f, q, g, freq);\n"
    "  }\n"
    "  const hp = decodeFilter(S.values[base + 138], true);\n"
    "  const lp = decodeFilter(S.values[base + 142], false);\n"
    "  db += filterDb(hp, dspFreq(S.values[base + 139]), freq, true);\n"
    "  db += filterDb(lp, dspFreq(S.values[base + 143]), freq, false);\n"
    "  return db;\n"
    "}\n"
    "\n"
    "/* ---------- API ---------- */\n"
    "\n"
    "async function api(path, body, method) {\n"
    "  const m = method || (body === undefined ? \"GET\" : \"POST\");\n"
    "  const opt = {method: m};\n"
    "  if (body !== undefined) {\n"
    "    opt.headers = {\"Content-Type\": \"application/json\"};\n"
    "    opt.body = JSON.stringify(body);\n"
    "  }\n"
    "  const r = await fetch(path, opt);\n"
    "  return r.json();\n"
    "}\n"
    "\n"
    "async function write(id, value) {\n"
    "  const r = await api(\"/api/write\", {id, value});\n"
    "  if (r.ok) {\n"
    "    S.values[id] = value;\n"
    "    render();\n"
    "  } else {\n"
    "    toast(\"Gagal menulis ID \" + id.toString(16).toUpperCase().padStart(4, \"0\"));\n"
    "  }\n"
    "  return r.ok;\n"
    "}\n"
    "\n"
    "async function poll() {\n"
    "  try {\n"
    "    const st = await api(\"/api/state\");\n"
    "    const changed = st.values.some((v, i) => v !== S.values[i]);\n"
    "    Object.assign(S, st);\n"
    "    if (changed) render();\n"
    "    else renderStatus();\n"
    "  } catch (e) {\n"
    "    S.connected = false;\n"
    "    S.status = \"Server web tidak merespons\";\n"
    "    renderStatus();\n"
    "  }\n"
    "}\n"
    "\n"
    "/* ---------- canvas EQ plot ---------- */\n"
    "\n"
    "const canvas = document.getElementById(\"eq\");\n"
    "const ctx = canvas.getContext(\"2d\");\n"
    "let dragBand = -1;\n"
    "\n"
    "const F_MIN = 20, F_MAX = 20000, DB_TOP = 20, DB_BOT = -20;\n"
    "\n"
    "function xOfF(f, w, h, padL, padR) {\n"
    "  const t = (Math.log10(f) - Math.log10(F_MIN)) / (Math.log10(F_MAX) - Math.log10(F_MIN));\n"
    "  return padL + t * (w - padL - padR);\n"
    "}\n"
    "function fOfX(x, w, h, padL, padR) {\n"
    "  const t = (x - padL) / (w - padL - padR);\n"
    "  return F_MIN * Math.pow(F_MAX / F_MIN, Math.min(1, Math.max(0, t)));\n"
    "}\n"
    "function yOfDb(db, h, padT, padB) {\n"
    "  const t = (DB_TOP - db) / (DB_TOP - DB_BOT);\n"
    "  return padT + t * (h - padT - padB);\n"
    "}\n"
    "function dbOfY(y, h, padT, padB) {\n"
    "  const t = (y - padT) / (h - padT - padB);\n"
    "  return DB_TOP - t * (DB_TOP - DB_BOT);\n"
    "}\n"
    "\n"
    "function resizeCanvas() {\n"
    "  const dpr = window.devicePixelRatio || 1;\n"
    "  const rect = canvas.getBoundingClientRect();\n"
    "  canvas.width = Math.round(rect.width * dpr);\n"
    "  canvas.height = Math.round(rect.height * dpr);\n"
    "  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);\n"
    "  drawEq();\n"
    "}\n"
    "\n"
    "function drawEq() {\n"
    "  const w = canvas.getBoundingClientRect().width;\n"
    "  const h = canvas.getBoundingClientRect().height;\n"
    "  const padL = 46, padR = 14, padT = 12, padB = 26;\n"
    "  ctx.clearRect(0, 0, w, h);\n"
    "\n"
    "  ctx.fillStyle = \"#101720\";\n"
    "  ctx.fillRect(0, 0, w, h);\n"
    "\n"
    "  ctx.font = \"11px sans-serif\";\n"
    "  ctx.textAlign = \"center\";\n"
    "  for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {\n"
    "    const x = xOfF(f, w, h, padL, padR);\n"
    "    ctx.strokeStyle = \"#2d3b4a\";\n"
    "    ctx.beginPath();\n"
    "    ctx.moveTo(x, padT);\n"
    "    ctx.lineTo(x, h - padB);\n"
    "    ctx.stroke();\n"
    "    ctx.fillStyle = \"#a5b4c4\";\n"
    "    ctx.fillText(f >= 1000 ? (f / 1000) + \"K\" : String(f), x, h - padB + 16);\n"
    "  }\n"
    "  ctx.textAlign = \"right\";\n"
    "  for (let d = -20; d <= 20; d += 5) {\n"
    "    const y = yOfDb(d, h, padT, padB);\n"
    "    ctx.strokeStyle = d === 0 ? \"#536579\" : \"#2d3b4a\";\n"
    "    ctx.beginPath();\n"
    "    ctx.moveTo(padL, y);\n"
    "    ctx.lineTo(w - padR, y);\n"
    "    ctx.stroke();\n"
    "    ctx.fillStyle = \"#a5b4c4\";\n"
    "    ctx.fillText((d > 0 ? \"+\" : \"\") + d, padL - 6, y + 4);\n"
    "  }\n"
    "  ctx.textAlign = \"center\";\n"
    "  ctx.fillText(\"Hz\", w - padR - 8, h - 6);\n"
    "  ctx.textAlign = \"left\";\n"
    "  ctx.fillText(\"dB\", 4, padT + 4);\n"
    "\n"
    "  if (!S.valid) {\n"
    "    ctx.fillStyle = \"#a5b4c4\";\n"
    "    ctx.textAlign = \"center\";\n"
    "    ctx.fillText(\"Belum ada data USB - klik Baca DSP\", w / 2, h / 2);\n"
    "    return;\n"
    "  }\n"
    "\n"
    "  const ch = S.ch;\n"
    "  const base = 136 * ch;\n"
    "  const hp = decodeFilter(S.values[base + 138], true);\n"
    "  const lp = decodeFilter(S.values[base + 142], false);\n"
    "\n"
    "  ctx.setLineDash([4, 4]);\n"
    "  ctx.strokeStyle = \"#f4ba63\";\n"
    "  const hpF = dspFreq(S.values[base + 139]);\n"
    "  const lpF = dspFreq(S.values[base + 143]);\n"
    "  if (hp.known && hp.slope && hpF >= 20 && hpF <= 20000) {\n"
    "    const x = xOfF(hpF, w, h, padL, padR);\n"
    "    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h - padB); ctx.stroke();\n"
    "  }\n"
    "  if (lp.known && lp.slope && lpF >= 20 && lpF <= 20000) {\n"
    "    const x = xOfF(lpF, w, h, padL, padR);\n"
    "    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h - padB); ctx.stroke();\n"
    "  }\n"
    "  ctx.setLineDash([]);\n"
    "\n"
    "  ctx.strokeStyle = \"#59d8cd\";\n"
    "  ctx.lineWidth = 1.5;\n"
    "  ctx.beginPath();\n"
    "  let first = true;\n"
    "  for (let x = 0; x <= w - padL - padR; x += 2) {\n"
    "    const f = fOfX(x + padL, w, h, padL, padR);\n"
    "    const db = Math.max(DB_BOT, Math.min(DB_TOP, curveDb(ch, f)));\n"
    "    const y = yOfDb(db, h, padT, padB);\n"
    "    if (first) { ctx.moveTo(padL + x, y); first = false; }\n"
    "    else ctx.lineTo(padL + x, y);\n"
    "  }\n"
    "  ctx.stroke();\n"
    "  ctx.lineWidth = 1;\n"
    "\n"
    "  for (let b = 0; b < 31; b++) {\n"
    "    const id = base + 147 + 4 * b;\n"
    "    const f = dspFreq(S.values[id]);\n"
    "    const g = (S.values[id + 1] - 500) / 10;\n"
    "    if (f < F_MIN || f > F_MAX) continue;\n"
    "    const x = xOfF(f, w, h, padL, padR);\n"
    "    const y = yOfDb(Math.max(DB_BOT, Math.min(DB_TOP, g)), h, padT, padB);\n"
    "    const sel = b === S.band;\n"
    "    ctx.fillStyle = sel ? \"#ffbe18\" : \"#59d8cd\";\n"
    "    ctx.beginPath();\n"
    "    ctx.arc(x, y, sel ? 6 : 4, 0, Math.PI * 2);\n"
    "    ctx.fill();\n"
    "  }\n"
    "}\n"
    "\n"
    "function canvasPos(e) {\n"
    "  const r = canvas.getBoundingClientRect();\n"
    "  return {x: e.clientX - r.left, y: e.clientY - r.top};\n"
    "}\n"
    "\n"
    "function handleAt(pos) {\n"
    "  const w = canvas.getBoundingClientRect().width;\n"
    "  const h = canvas.getBoundingClientRect().height;\n"
    "  const padL = 46, padR = 14, padT = 12, padB = 26;\n"
    "  const base = 136 * S.ch;\n"
    "  let best = -1, bd = 12;\n"
    "  for (let b = 0; b < 31; b++) {\n"
    "    const id = base + 147 + 4 * b;\n"
    "    const f = dspFreq(S.values[id]);\n"
    "    const g = (S.values[id + 1] - 500) / 10;\n"
    "    if (f < F_MIN || f > F_MAX) continue;\n"
    "    const x = xOfF(f, w, h, padL, padR);\n"
    "    const y = yOfDb(Math.max(DB_BOT, Math.min(DB_TOP, g)), h, padT, padB);\n"
    "    const d = Math.hypot(x - pos.x, y - pos.y);\n"
    "    if (d < bd) { bd = d; best = b; }\n"
    "  }\n"
    "  return best;\n"
    "}\n"
    "\n"
    "canvas.addEventListener(\"mousedown\", e => {\n"
    "  if (!S.valid) return;\n"
    "  const b = handleAt(canvasPos(e));\n"
    "  if (b >= 0) {\n"
    "    dragBand = b;\n"
    "    S.band = b;\n"
    "    renderBandInfo();\n"
    "    renderBandTable();\n"
    "  }\n"
    "});\n"
    "canvas.addEventListener(\"mousemove\", e => {\n"
    "  if (!S.valid) return;\n"
    "  const pos = canvasPos(e);\n"
    "  if (dragBand >= 0) {\n"
    "    const h = canvas.getBoundingClientRect().height;\n"
    "    const db = Math.max(-12, Math.min(12, dbOfY(pos.y, h, 12, 26)));\n"
    "    const id = bandId(S.ch, dragBand) + 1;\n"
    "    S.values[id] = gainRaw(db);\n"
    "    drawEq();\n"
    "    renderBandInfo();\n"
    "    renderBandTable();\n"
    "  } else {\n"
    "    canvas.style.cursor = handleAt(pos) >= 0 ? \"pointer\" : \"crosshair\";\n"
    "  }\n"
    "});\n"
    "window.addEventListener(\"mouseup\", () => {\n"
    "  if (dragBand >= 0) {\n"
    "    const id = bandId(S.ch, dragBand) + 1;\n"
    "    write(id, S.values[id]);\n"
    "    dragBand = -1;\n"
    "  }\n"
    "});\n"
    "\n"
    "/* ---------- rendering ---------- */\n"
    "\n"
    "const $ = id => document.getElementById(id);\n"
    "\n"
    "function renderStatus() {\n"
    "  const el = $(\"status\");\n"
    "  el.textContent = S.status;\n"
    "  el.className = \"status\" + (S.connected ? \" connected\" : \"\");\n"
    "  $(\"btn-connect\").textContent = S.connected ? \"Disconnect\" : \"Connect\";\n"
    "  $(\"btn-read\").disabled = !S.connected || S.reading;\n"
    "  const prog = $(\"progress\");\n"
    "  if (S.reading) {\n"
    "    prog.classList.remove(\"hidden\");\n"
    "    const pct = S.total ? (S.progress / S.total) * 100 : 0;\n"
    "    $(\"progress-bar\").style.width = pct + \"%\";\n"
    "    $(\"progress-text\").textContent = S.progress + \"/\" + S.total;\n"
    "  } else {\n"
    "    prog.classList.add(\"hidden\");\n"
    "  }\n"
    "}\n"
    "\n"
    "function renderChannels() {\n"
    "  const nav = $(\"channels\");\n"
    "  nav.innerHTML = \"\";\n"
    "  for (let c = 0; c < 8; c++) {\n"
    "    const b = document.createElement(\"button\");\n"
    "    b.innerHTML = \"CH\" + (c + 1) + '<span class=\"role\">' + ROLES[c] + \"</span>\";\n"
    "    if (c === S.ch) b.classList.add(\"active\");\n"
    "    b.onclick = () => { S.ch = c; render(); };\n"
    "    nav.appendChild(b);\n"
    "  }\n"
    "}\n"
    "\n"
    "function renderBandInfo() {\n"
    "  const id = bandId(S.ch, S.band);\n"
    "  const f = dspFreq(S.values[id]);\n"
    "  const q = qOf(S.values[id + 2]);\n"
    "  const g = (S.values[id + 1] - 500) / 10;\n"
    "  $(\"band-info\").textContent = \"Band \" + (S.band + 1) +\n"
    "    \"  F:\" + f + \" Hz   Q:\" + q.toFixed(2) + \"   G:\" + g.toFixed(1) + \" dB\" +\n"
    "    \"   |   Geser titik band untuk mengubah gain\";\n"
    "}\n"
    "\n"
    "function renderCrossover() {\n"
    "  const base = 136 * S.ch;\n"
    "  const hp = decodeFilter(S.values[base + 138], true);\n"
    "  const lp = decodeFilter(S.values[base + 142], false);\n"
    "  setInputValue(\"hp-freq\", dspFreq(S.values[base + 139]));\n"
    "  setInputValue(\"lp-freq\", dspFreq(S.values[base + 143]));\n"
    "  $(\"xover-family\").value = String(hp.known ? hp.family : 0);\n"
    "  $(\"xover-slope\").value = String(hp.known ? (hp.slope ? hp.slope / 6 - 1 : 8) : 3);\n"
    "}\n"
    "\n"
    "function setInputValue(id, v) {\n"
    "  const el = $(id);\n"
    "  if (document.activeElement !== el) el.value = v;\n"
    "}\n"
    "\n"
    "function renderChannel() {\n"
    "  const c = S.ch;\n"
    "  const level = S.valid ? dspLevel(S.values[26 + c]) : 0;\n"
    "  const phase = S.valid && S.values[26 + c] < 5000;\n"
    "  const mute = S.valid && !S.values[2 + c];\n"
    "  const ms = S.valid ? dspDelayMs(S.values[73 + c]) : 0;\n"
    "  $(\"ch-level\").value = level;\n"
    "  $(\"ch-level-val\").textContent = S.valid ? level.toFixed(1) : \"--\";\n"
    "  $(\"ch-delay\").value = ms;\n"
    "  $(\"ch-delay-val\").textContent = S.valid ? ms.toFixed(3) : \"--\";\n"
    "  $(\"ch-phase\").textContent = \"Fase \" + (phase ? \"180'\" : \"0'\");\n"
    "  $(\"ch-phase\").classList.toggle(\"active\", !!phase);\n"
    "  $(\"ch-mute\").textContent = mute ? \"Unmute\" : \"Mute\";\n"
    "  $(\"ch-mute\").classList.toggle(\"active\", !!mute);\n"
    "}\n"
    "\n"
    "function renderInput() {\n"
    "  const src = S.values[1554];\n"
    "  document.querySelectorAll(\"#input-btns button\").forEach(b => {\n"
    "    b.classList.toggle(\"active\", S.valid && Number(b.dataset.src) === src);\n"
    "  });\n"
    "  const usb = S.valid ? volOf(S.values[1908]) : 0;\n"
    "  const bt = S.valid ? volOf(S.values[1900]) : 0;\n"
    "  $(\"usb-vol\").value = usb;\n"
    "  $(\"usb-vol-val\").textContent = S.valid ? String(usb) : \"--\";\n"
    "  $(\"bt-vol\").value = bt;\n"
    "  $(\"bt-vol-val\").textContent = S.valid ? String(bt) : \"--\";\n"
    "}\n"
    "\n"
    "function renderMaster() {\n"
    "  const v = S.valid ? dspLevel(S.values[12]) : 0;\n"
    "  $(\"master-vol\").value = v;\n"
    "  $(\"master-vol-val\").textContent = S.valid ? v.toFixed(1) : \"--\";\n"
    "  const idx = NOISE_CODES.indexOf(S.values[1565]);\n"
    "  $(\"noise-gate\").value = String(idx >= 0 ? idx : 0);\n"
    "}\n"
    "\n"
    "function renderMixer() {\n"
    "  const box = $(\"mixer\");\n"
    "  if (!S.valid) {\n"
    "    box.innerHTML = \"<p style='color:var(--dim)'>Baca DSP terlebih dahulu.</p>\";\n"
    "    return;\n"
    "  }\n"
    "  const src = S.values[1554];\n"
    "  if (![1, 2, 3].includes(src)) {\n"
    "    box.innerHTML = \"<p style='color:var(--dim)'>Pilih AUX, Bluetooth atau High level.</p>\";\n"
    "    return;\n"
    "  }\n"
    "  const rows = src === 3 ? 4 : 2;\n"
    "  const base = src === 3 ? 1226 : src === 1 ? 1482 : 1360;\n"
    "  let html = \"<table><tr><th></th>\";\n"
    "  for (let c = 0; c < 8; c++) html += \"<th>CH\" + (c + 1) + \"</th>\";\n"
    "  html += \"</tr>\";\n"
    "  for (let i = 0; i < rows; i++) {\n"
    "    html += \"<tr><th>\" + (src === 3 ? \"HiLevel\" + (i + 1) : (src === 1 ? \"AUX-\" : \"BT-\") + (i ? \"R\" : \"L\")) + \"</th>\";\n"
    "    for (let c = 0; c < 8; c++) {\n"
    "      const id = base + c * 8 + i;\n"
    "      const v = S.values[id];\n"
    "      const on = (v & 1) !== 0;\n"
    "      const gain = Math.floor(v / 256);\n"
    "      html += \"<td><span class='pwr \" + (on ? \"on\" : \"off\") + \"' data-id='\" + id + \"'>\" + (on ? \"⏻\" : \"○\") + \"</span>\" +\n"
    "        \"<input type='range' min='0' max='100' value='\" + gain + \"' data-id='\" + id + \"'></td>\";\n"
    "    }\n"
    "    html += \"</tr>\";\n"
    "  }\n"
    "  html += \"</table>\";\n"
    "  box.innerHTML = html;\n"
    "  box.querySelectorAll(\".pwr\").forEach(el => {\n"
    "    el.onclick = () => {\n"
    "      const id = Number(el.dataset.id);\n"
    "      write(id, S.values[id] ^ 1);\n"
    "    };\n"
    "  });\n"
    "  box.querySelectorAll(\"input[type=range]\").forEach(el => {\n"
    "    el.onchange = () => {\n"
    "      const id = Number(el.dataset.id);\n"
    "      write(id, Math.round(Number(el.value)) * 256 + (S.values[id] & 1));\n"
    "    };\n"
    "  });\n"
    "}\n"
    "\n"
    "function renderBandTable() {\n"
    "  const tbody = document.querySelector(\"#band-table tbody\");\n"
    "  if (!tbody.dataset.built) {\n"
    "    let html = \"\";\n"
    "    for (let b = 0; b < 31; b++) {\n"
    "      html += \"<tr data-band='\" + b + \"'>\" +\n"
    "        \"<td>\" + (b + 1) + \"</td>\" +\n"
    "        \"<td class='editable' data-col='f'></td>\" +\n"
    "        \"<td class='editable' data-col='q'></td>\" +\n"
    "        \"<td class='editable' data-col='g'></td></tr>\";\n"
    "    }\n"
    "    tbody.innerHTML = html;\n"
    "    tbody.dataset.built = \"1\";\n"
    "    tbody.querySelectorAll(\"td.editable\").forEach(td => {\n"
    "      td.onclick = () => startEdit(td);\n"
    "    });\n"
    "  }\n"
    "  const base = 136 * S.ch;\n"
    "  tbody.querySelectorAll(\"tr\").forEach(tr => {\n"
    "    const b = Number(tr.dataset.band);\n"
    "    const id = base + 147 + 4 * b;\n"
    "    const f = dspFreq(S.values[id]);\n"
    "    const q = qOf(S.values[id + 2]);\n"
    "    const g = (S.values[id + 1] - 500) / 10;\n"
    "    const cells = tr.children;\n"
    "    if (!cells[1].classList.contains(\"editing\")) {\n"
    "      cells[1].textContent = f;\n"
    "      cells[2].textContent = q.toFixed(2);\n"
    "      cells[3].textContent = g.toFixed(1);\n"
    "    }\n"
    "    tr.classList.toggle(\"selected\", b === S.band);\n"
    "  });\n"
    "}\n"
    "\n"
    "function startEdit(td) {\n"
    "  const tr = td.closest(\"tr\");\n"
    "  const b = Number(tr.dataset.band);\n"
    "  const col = td.dataset.col;\n"
    "  const id = bandId(S.ch, b);\n"
    "  const ranges = {f: [20, 20000], q: [0.1, 30], g: [-12, 12]};\n"
    "  const vals = {f: dspFreq(S.values[id]), q: qOf(S.values[id + 2]), g: (S.values[id + 1] - 500) / 10};\n"
    "  td.classList.add(\"editing\");\n"
    "  td.innerHTML = \"<input type='number' step='any' value='\" + vals[col] + \"'>\";\n"
    "  const input = td.querySelector(\"input\");\n"
    "  input.focus();\n"
    "  input.select();\n"
    "  let done = false;\n"
    "  const commit = ok => {\n"
    "    if (done) return;\n"
    "    done = true;\n"
    "    if (ok) {\n"
    "      let v = Number(input.value);\n"
    "      const [lo, hi] = ranges[col];\n"
    "      v = Math.max(lo, Math.min(hi, v));\n"
    "      if (col === \"f\") write(id, freqRaw(v));\n"
    "      else if (col === \"q\") write(id + 2, qRaw(v));\n"
    "      else write(id + 1, gainRaw(v));\n"
    "    }\n"
    "    renderBandTable();\n"
    "  };\n"
    "  input.onkeydown = e => {\n"
    "    if (e.key === \"Enter\") commit(true);\n"
    "    else if (e.key === \"Escape\") commit(false);\n"
    "    e.stopPropagation();\n"
    "  };\n"
    "  input.onblur = () => commit(true);\n"
    "}\n"
    "\n"
    "function render() {\n"
    "  renderStatus();\n"
    "  renderChannels();\n"
    "  renderBandInfo();\n"
    "  renderCrossover();\n"
    "  renderChannel();\n"
    "  renderInput();\n"
    "  renderMaster();\n"
    "  renderMixer();\n"
    "  renderBandTable();\n"
    "  drawEq();\n"
    "}\n"
    "\n"
    "/* ---------- controls ---------- */\n"
    "\n"
    "$(\"btn-connect\").onclick = async () => {\n"
    "  if (S.connected) {\n"
    "    await api(\"/api/disconnect\", null, \"POST\");\n"
    "    S.connected = false;\n"
    "    S.valid = false;\n"
    "    render();\n"
    "  } else {\n"
    "    const r = await api(\"/api/connect\", null, \"POST\");\n"
    "    if (r.ok) {\n"
    "      S.connected = true;\n"
    "      S.valid = false;\n"
    "      render();\n"
    "    } else {\n"
    "      toast(\"Connect gagal\");\n"
    "    }\n"
    "  }\n"
    "};\n"
    "\n"
    "$(\"btn-read\").onclick = async () => {\n"
    "  const r = await api(\"/api/read\", null, \"POST\");\n"
    "  if (!r.ok) toast(\"Baca DSP gagal\");\n"
    "};\n"
    "\n"
    "function bindNumber(id, fn) {\n"
    "  $(id).onchange = () => fn(Number($(id).value));\n"
    "}\n"
    "bindNumber(\"hp-freq\", v => {\n"
    "  const base = 136 * S.ch;\n"
    "  write(base + 139, freqRaw(v));\n"
    "});\n"
    "bindNumber(\"lp-freq\", v => {\n"
    "  const base = 136 * S.ch;\n"
    "  write(base + 143, freqRaw(v));\n"
    "});\n"
    "$(\"xover-family\").onchange = () => writeXover();\n"
    "$(\"xover-slope\").onchange = () => writeXover();\n"
    "\n"
    "function writeXover() {\n"
    "  const base = 136 * S.ch;\n"
    "  const family = Number($(\"xover-family\").value);\n"
    "  const rate = Number($(\"xover-slope\").value);\n"
    "  write(base + 138, HP_CODES[family * 9 + rate]);\n"
    "  write(base + 142, LP_CODES[family * 9 + rate]);\n"
    "}\n"
    "\n"
    "$(\"ch-level\").onchange = () => {\n"
    "  const c = S.ch;\n"
    "  const phase = S.valid && S.values[26 + c] < 5000;\n"
    "  write(26 + c, levelRaw(Number($(\"ch-level\").value), phase));\n"
    "};\n"
    "$(\"ch-delay\").onchange = () => {\n"
    "  write(73 + S.ch, delayRaw(Number($(\"ch-delay\").value)));\n"
    "};\n"
    "$(\"ch-phase\").onclick = () => {\n"
    "  const c = S.ch;\n"
    "  const phase = !(S.valid && S.values[26 + c] < 5000);\n"
    "  write(26 + c, levelRaw(dspLevel(S.values[26 + c]), phase));\n"
    "};\n"
    "$(\"ch-mute\").onclick = () => {\n"
    "  const c = S.ch;\n"
    "  write(2 + c, S.values[2 + c] ? 0 : 1);\n"
    "};\n"
    "$(\"ch-reset-eq\").onclick = async () => {\n"
    "  const base = 136 * S.ch;\n"
    "  for (let b = 0; b < 31; b++) {\n"
    "    const ok = await write(base + 148 + 4 * b, 500);\n"
    "    if (!ok) break;\n"
    "  }\n"
    "};\n"
    "\n"
    "document.querySelectorAll(\"#input-btns button\").forEach(b => {\n"
    "  b.onclick = () => write(1554, Number(b.dataset.src));\n"
    "});\n"
    "\n"
    "$(\"usb-vol\").onchange = () => write(1908, volRaw(Number($(\"usb-vol\").value)));\n"
    "$(\"bt-vol\").onchange = () => write(1900, volRaw(Number($(\"bt-vol\").value)));\n"
    "$(\"master-vol\").onchange = () => {\n"
    "  write(12, levelRaw(Number($(\"master-vol\").value), false));\n"
    "};\n"
    "\n"
    "{\n"
    "  const sel = $(\"noise-gate\");\n"
    "  for (let i = 0; i < NOISE_CODES.length; i++) {\n"
    "    const o = document.createElement(\"option\");\n"
    "    o.value = String(i);\n"
    "    o.textContent = i === 0 ? \"OFF\" : \"Tingkat \" + i;\n"
    "    sel.appendChild(o);\n"
    "  }\n"
    "  sel.onchange = () => write(1565, NOISE_CODES[Number(sel.value)]);\n"
    "}\n"
    "\n"
    "let toastTimer = null;\n"
    "function toast(msg) {\n"
    "  const el = $(\"toast\");\n"
    "  el.textContent = msg;\n"
    "  el.classList.remove(\"hidden\");\n"
    "  clearTimeout(toastTimer);\n"
    "  toastTimer = setTimeout(() => el.classList.add(\"hidden\"), 3000);\n"
    "}\n"
    "\n"
    "window.addEventListener(\"resize\", resizeCanvas);\n"
    "\n"
    "render();\n"
    "resizeCanvas();\n"
    "setInterval(poll, 1000);\n"
    "poll();\n"
    "";
/* ===== End embedded frontend ===== */


/* Only one app (desktop or web) may own the USB device at a time: concurrent
   hidraw transactions from two processes cross replies and fail. */
static int lock_device(void)
{
    char path[512];
    const char *home = getenv("HOME");
    int fd;
    if (!home)
        return 0;
    snprintf(path, sizeof(path), "%s/.config/zp84-dsp", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.config/zp84-dsp/device.lock", home);
    fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
        return 0;
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }
    device_lock = fd;
    return 1;
}

static void unlock_device(void)
{
    if (device_lock >= 0) {
        flock(device_lock, LOCK_UN);
        close(device_lock);
        device_lock = -1;
    }
}

typedef struct {
    char *data;
    size_t len, cap;
} jbuf;

static void jb_init(jbuf *b)
{
    b->cap = 4096;
    b->len = 0;
    b->data = malloc(b->cap);
    b->data[0] = 0;
}

static void jb_free(jbuf *b)
{
    free(b->data);
    b->data = NULL;
}

static void jb_reserve(jbuf *b, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap)
            b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
}

static void jb_put(jbuf *b, const char *s)
{
    size_t n = strlen(s);
    jb_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void jb_putf(jbuf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    jb_put(b, tmp);
}

static void jb_putjson(jbuf *b, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            char e[3] = {'\\', (char)c, 0};
            jb_put(b, e);
        } else if (c < 32) {
            jb_putf(b, "\\u%04x", c);
        } else {
            char ch[2] = {(char)c, 0};
            jb_put(b, ch);
        }
    }
}

static void read_batch(void)
{
    uint16_t ids[DSP_READ_BATCH];
    uint8_t rep[DSP_READ_BATCH * ZP_ID_RECORD_BYTES];
    size_t len = 0;
    int count = ZP_PARAM_COUNT - read_next;
    int result = ZP_OK;
    int i;
    if (count > DSP_READ_BATCH)
        count = DSP_READ_BATCH;
    for (i = 0; i < count; i++)
        ids[i] = (uint16_t)(read_next + i);
    result = zp_id_query(&conn, ids, (size_t)count, rep, sizeof(rep), &len);
    if (result == ZP_OK && len != (size_t)count * ZP_ID_RECORD_BYTES)
        result = ZP_ERR_SHORT;
    if (result == ZP_OK) {
        for (i = 0; i < count; i++) {
            uint16_t id = (uint16_t)((rep[i * 4] << 8) | rep[i * 4 + 1]);
            if (id != ids[i]) {
                result = ZP_ERR_SYNC;
                break;
            }
            pending[id] = (uint16_t)((rep[i * 4 + 2] << 8) | rep[i * 4 + 3]);
        }
    }
    if (result != ZP_OK) {
        snprintf(status, sizeof(status), "Gagal pada ID 0x%04X: %s. Snapshot terakhir dipertahankan.",
                 read_next, zp_strerror((uint32_t)result));
        reading = 0;
        zp_close(&conn);
        connected = 0;
        unlock_device();
        return;
    }
    read_next += count;
    if (read_next >= ZP_PARAM_COUNT) {
        memcpy(values, pending, sizeof(values));
        dsp_valid = 1;
        reading = 0;
        {
            time_t now = time(NULL);
            struct tm local;
            if (localtime_r(&now, &local))
                strftime(dsp_time, sizeof(dsp_time), "%Y-%m-%d %H:%M:%S", &local);
        }
        snprintf(status, sizeof(status), "Berhasil membaca %d parameter dari USB.",
                 ZP_PARAM_COUNT);
    } else {
        snprintf(status, sizeof(status), "Membaca %d/%d parameter...", read_next,
                 ZP_PARAM_COUNT);
    }
}

static int api_control_ready(void)
{
    return connected && dsp_valid && !reading;
}

static int api_write(int id, int value)
{
    if (!api_control_ready()) {
        snprintf(status, sizeof(status),
                 "Hubungkan USB lalu klik Baca DSP: data USB lengkap diperlukan.");
        return 0;
    }
    if (id < 0 || id >= ZP_PARAM_COUNT) {
        snprintf(status, sizeof(status), "ID di luar rentang.");
        return 0;
    }
    if (zp_id_write(&conn, (uint16_t)id, (uint16_t)value) != ZP_OK) {
        dsp_valid = 0;
        snprintf(status, sizeof(status), "Gagal verifikasi ID %04X (%s). Baca DSP sebelum mencoba lagi.",
                 id, zp_strerror(conn.last_error));
        return 0;
    }
    values[id] = (uint16_t)value;
    return 1;
}

static int api_connect(void)
{
    char path[256];
    uint8_t req[2] = {0, 0}, rep[64];
    size_t rl = 0;
    if (connected)
        return 1;
    if (zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
        snprintf(status, sizeof(status),
                 "USB tidak ditemukan. Sambungkan amplifier lalu Connect.");
        return 0;
    }
    {
        int lk = lock_device();
        if (lk < 0) {
            snprintf(status, sizeof(status),
                     "Perangkat sedang dipakai aplikasi lain (desktop/web). Tutup aplikasi tersebut lalu coba lagi.");
            return 0;
        }
    }
    if (zp_open(&conn, path) != ZP_OK) {
        int e = conn.last_errno;
        snprintf(status, sizeof(status), "USB gagal dibuka: %s",
                 e ? strerror(e) : "periksa koneksi");
        unlock_device();
        return 0;
    }
    conn.timeout_ms = 400;
    if (zp_xfer(&conn, req, 2, ZP_CMD_ID, NULL, rep, sizeof(rep), &rl) != ZP_OK) {
        zp_close(&conn);
        unlock_device();
        snprintf(status, sizeof(status),
                 "USB tidak merespons. Periksa amplifier lalu Connect kembali.");
        return 0;
    }
    connected = 1;
    dsp_valid = 0;
    snprintf(status, sizeof(status),
             "USB terhubung. Klik Baca DSP untuk mengaktifkan kontrol.");
    return 1;
}

static void api_disconnect(void)
{
    if (!connected)
        return;
    zp_close(&conn);
    connected = 0;
    dsp_valid = 0;
    reading = 0;
    unlock_device();
    snprintf(status, sizeof(status), "Terputus.");
}

static void api_read(void)
{
    if (reading)
        return;
    if (!connected) {
        snprintf(status, sizeof(status), "Connect terlebih dahulu.");
        return;
    }
    read_next = 0;
    reading = 1;
    snprintf(status, sizeof(status), "Membaca 0/%d parameter...", ZP_PARAM_COUNT);
}

static void json_state(jbuf *b)
{
    int i;
    jb_putf(b, "{\"connected\":%s,\"reading\":%s,\"valid\":%s,",
            connected ? "true" : "false", reading ? "true" : "false",
            dsp_valid ? "true" : "false");
    jb_putf(b, "\"progress\":%d,\"total\":%d,\"time\":\"%s\",\"status\":\"",
            reading ? read_next : (dsp_valid ? ZP_PARAM_COUNT : 0), ZP_PARAM_COUNT,
            dsp_time);
    jb_putjson(b, status);
    jb_put(b, "\",\"values\":[");
    for (i = 0; i < ZP_PARAM_COUNT; i++)
        jb_putf(b, i ? ",%u" : "%u", (unsigned)values[i]);
    jb_put(b, "]}");
}

static void write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        off += (size_t)n;
    }
}

static void http_reply(int fd, int code, const char *ctype, const char *body,
                       size_t len)
{
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     code, code == 200 ? "OK" : code == 404 ? "Not Found" : "Error",
                     ctype, len);
    write_all(fd, head, (size_t)n);
    if (len)
        write_all(fd, body, len);
}

static const char *mime_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "application/octet-stream";
    if (!strcmp(dot, ".html"))
        return "text/html; charset=utf-8";
    if (!strcmp(dot, ".js"))
        return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".css"))
        return "text/css; charset=utf-8";
    if (!strcmp(dot, ".svg"))
        return "image/svg+xml";
    if (!strcmp(dot, ".png"))
        return "image/png";
    return "application/octet-stream";
}

static void serve_file(int fd, const char *path, const char *root)
{
    jbuf b;
    (void)root;
    const char *body = NULL;
    if (strstr(path, "..") || path[0] != '/') {
        http_reply(fd, 404, "text/plain", "not found", 9);
        return;
    }
    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        body = INDEX_HTML;
    } else if (!strcmp(path, "/style.css")) {
        body = STYLE_CSS;
    } else if (!strcmp(path, "/app.js")) {
        body = APP_JS;
    }
    if (!body) {
        http_reply(fd, 404, "text/plain", "not found", 9);
        return;
    }
    jb_init(&b);
    jb_put(&b, body);
    http_reply(fd, 200, mime_of(path), b.data, b.len);
    jb_free(&b);
}

typedef struct {
    int fd;
    char buf[REQ_MAX];
    size_t len;
} client;

static int json_int(const char *body, const char *key, int *out)
{
    char pat[32];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(body, pat);
    if (!p)
        return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    *out = (int)strtol(p, NULL, 10);
    return 1;
}

static void reply_json(int fd, int code, const char *json)
{
    http_reply(fd, code, "application/json; charset=utf-8", json, strlen(json));
}

static void dispatch(client *c, const char *root)
{
    char method[8] = {0}, path[1024] = {0};
    char *hdr_end = strstr(c->buf, "\r\n\r\n");
    size_t hdr_len = hdr_end ? (size_t)(hdr_end - c->buf) + 4 : c->len;
    const char *body = c->buf + hdr_len;
    size_t body_len = c->len - hdr_len;
    jbuf b;
    long want = 0;
    char *cl;
    if (sscanf(c->buf, "%7s %1023s", method, path) != 2) {
        http_reply(c->fd, 400, "text/plain", "bad request", 11);
        return;
    }
    {
        char *q = strchr(path, '?');
        if (q)
            *q = 0;
    }
    cl = strcasestr(c->buf, "content-length:");
    if (cl)
        want = strtol(cl + 15, NULL, 10);
    if (want < 0 || (size_t)want > REQ_MAX) {
        http_reply(c->fd, 400, "text/plain", "bad length", 10);
        return;
    }
    if (body_len < (size_t)want) {
        http_reply(c->fd, 400, "text/plain", "short body", 10);
        return;
    }

    jb_init(&b);
    if (!strcmp(method, "GET")) {
        if (!strcmp(path, "/api/state")) {
            json_state(&b);
            reply_json(c->fd, 200, b.data);
        } else if (!strcmp(path, "/") || !strcmp(path, "/index.html") ||
                   !strcmp(path, "/app.js") || !strcmp(path, "/style.css")) {
            if (!strcmp(path, "/"))
                serve_file(c->fd, "/index.html", root);
            else
                serve_file(c->fd, path, root);
        } else {
            reply_json(c->fd, 404, "{\"error\":\"not found\"}");
        }
    } else if (!strcmp(method, "POST")) {
        if (!strcmp(path, "/api/connect")) {
            int ok = api_connect();
            jb_putf(&b, "{\"ok\":%s}", ok ? "true" : "false");
            reply_json(c->fd, ok ? 200 : 500, b.data);
        } else if (!strcmp(path, "/api/disconnect")) {
            api_disconnect();
            jb_put(&b, "{\"ok\":true}");
            reply_json(c->fd, 200, b.data);
        } else if (!strcmp(path, "/api/read")) {
            api_read();
            jb_putf(&b, "{\"ok\":%s}", reading ? "true" : "false");
            reply_json(c->fd, reading ? 200 : 500, b.data);
        } else if (!strcmp(path, "/api/write")) {
            int id = -1, value = 0;
            int ok;
            if (!json_int(body, "id", &id) || !json_int(body, "value", &value)) {
                reply_json(c->fd, 400, "{\"error\":\"expected {\\\"id\\\":N,\\\"value\\\":N}\"}");
            } else {
                ok = api_write(id, value);
                jb_putf(&b, "{\"ok\":%s}", ok ? "true" : "false");
                reply_json(c->fd, ok ? 200 : 500, b.data);
            }
        } else {
            reply_json(c->fd, 404, "{\"error\":\"not found\"}");
        }
    } else {
        http_reply(c->fd, 405, "text/plain", "method not allowed", 18);
    }
    jb_free(&b);
}

/* Returns 1 to keep the client, 0 to close it. */
static int client_step(client *c, const char *root)
{
    char buf[4096];
    for (;;) {
        ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (c->len + (size_t)n > REQ_MAX) {
                http_reply(c->fd, 413, "text/plain", "request too large", 17);
                return 0;
            }
            memcpy(c->buf + c->len, buf, n);
            c->len += (size_t)n;
        } else if (n == 0) {
            return 0;
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return 0;
        }
    }
    if (!strstr(c->buf, "\r\n\r\n"))
        return 1;
    dispatch(c, root);
    return 0;
}

int main(int argc, char **argv)
{
    int port = 8085;
    const char *root = "web";
    int lfd, i, maxfd, n;
    client clients[MAX_CLIENTS];
    struct sockaddr_in sa;
    int one = 1;

    for (i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--port"))
            port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--root"))
            root = argv[i + 1];
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "zp84web: bad port\n");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    conn.fd = -1;
    snprintf(status, sizeof(status), "Terputus.");

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(lfd, 4) < 0) {
        fprintf(stderr, "zp84web: cannot listen on 127.0.0.1:%d: %s\n", port,
                strerror(errno));
        return 1;
    }
    printf("zp84web: http://127.0.0.1:%d  (root: %s)\n", port, root);
    fflush(stdout);

    memset(clients, 0, sizeof(clients));
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        maxfd = lfd;
        for (i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].fd > 0) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > maxfd)
                    maxfd = clients[i].fd;
            }
        tv.tv_sec = 0;
        tv.tv_usec = reading ? 5000 : 100000;
        n = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (FD_ISSET(lfd, &rfds)) {
            int cfd = accept(lfd, NULL, NULL);
            int slot = -1;
            if (cfd >= 0) {
                for (i = 0; i < MAX_CLIENTS; i++)
                    if (clients[i].fd <= 0) {
                        slot = i;
                        break;
                    }
                if (slot < 0) {
                    close(cfd);
                } else {
                    int flags;
                    memset(&clients[slot], 0, sizeof(client));
                    clients[slot].fd = cfd;
                    flags = fcntl(cfd, F_GETFL, 0);
                    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
                }
            }
        }
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0 && FD_ISSET(clients[i].fd, &rfds)) {
                if (!client_step(&clients[i], root)) {
                    close(clients[i].fd);
                    clients[i].fd = 0;
                }
            }
        }
        if (reading)
            read_batch();
    }
    return 0;
}
