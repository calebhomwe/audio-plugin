#!/usr/bin/env node
/* Headless balance/playtest gauntlet for games/survivor-wave.html.
 * Usage: node tools/balance.js [sims]   (default 6)
 * Loads the game's inline <script> in a vm sandbox with stubbed DOM
 * (stubs modeled on tools/harness.js), drives it with a simple bot,
 * and prints a one-line aggregate JSON report.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SIMS = Math.max(1, parseInt(process.argv[2], 10) || 6);
const DT = 1 / 60;
const GOAL_CAP = 620;
const MAX_STEPS = GOAL_CAP * 60 + 7200;
const FILE = path.join(__dirname, '..', 'games', 'survivor-wave.html');

const html = fs.readFileSync(FILE, 'utf8');
const GAME_SRC = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)].map(m => m[1]).join('\n');
if (!GAME_SRC) { console.error('no inline <script> found in ' + FILE); process.exit(2); }

function errMsg(e) {
  if (!e) return 'unknown error';
  let s = (e && e.message) ? e.message : String(e);
  if (e && e.name && s.indexOf(e.name) !== 0) s = e.name + ': ' + s;
  return String(s).split('\n')[0].slice(0, 200);
}

function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/* ---- universal callable/readable deep stub (from tools/harness.js) ---- */
function deepStub(label) {
  const fn = function () {};
  const cache = new Map();
  return new Proxy(fn, {
    get(t, p) {
      if (p === Symbol.toPrimitive) return () => 0;
      if (p === Symbol.iterator) return function* () {};
      if (p === 'then') return undefined;
      if (p === 'toString') return () => label;
      if (p === 'valueOf') return () => 0;
      if (typeof p === 'string' && Object.prototype.hasOwnProperty.call(t, p)) return t[p];
      if (!cache.has(p)) cache.set(p, deepStub(label + '.' + String(p)));
      return cache.get(p);
    },
    set(t, p, v) { t[p] = v; return true; },
    apply() { return deepStub(label + '()'); },
    construct() { return deepStub(label + '<new>'); },
  });
}

/* ---- element stub (simplified from tools/harness.js) ---- */
function makeEl(tag, id) {
  const el = {
    tagName: String(tag || 'div').toUpperCase(), id: id || '', className: '',
    style: {}, dataset: {}, children: [], value: '', checked: false,
    textContent: '', innerHTML: '', disabled: false, hidden: false,
    width: 320, height: 320, clientWidth: 390, clientHeight: 844,
    offsetWidth: 390, offsetHeight: 844,
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    addEventListener() {}, removeEventListener() {},
    appendChild(c) { el.children.push(c); return c; },
    removeChild(c) { return c; }, insertBefore(c) { return c; }, replaceChildren() {},
    remove() {}, cloneNode() { return makeEl(tag, id); },
    setAttribute() {}, getAttribute() { return null; }, removeAttribute() {}, hasAttribute() { return false; },
    getBoundingClientRect() { return { left: 0, top: 0, right: 390, bottom: 844, width: 390, height: 844, x: 0, y: 0 }; },
    focus() {}, blur() {}, click() {},
    querySelector() { return makeEl('div'); },
    querySelectorAll() { return []; },
    getContext() { return ctx2d(el); },
    toDataURL() { return 'data:image/png;base64,'; },
    onclick: null,
  };
  return el;
}

function ctx2d(canvas) {
  const base = deepStub('ctx');
  return new Proxy(base, {
    get(t, p) {
      if (p === 'canvas') return canvas;
      if (p === 'measureText') return () => ({ width: 12 });
      if (p === 'createLinearGradient' || p === 'createRadialGradient' || p === 'createPattern')
        return () => ({ addColorStop() {} });
      if (p === 'getImageData') return () => ({ data: new Uint8ClampedArray(4), width: 1, height: 1 });
      return Reflect.get(t, p);
    },
    set(t, p, v) { t[p] = v; return true; },
  });
}

function makeDocument() {
  const els = new Map();
  const byId = (id) => { if (!els.has(id)) els.set(id, makeEl('div', id)); return els.get(id); };
  return {
    getElementById: byId,
    querySelector: (s) => makeEl('div', s),
    querySelectorAll: () => [],
    createElement: (t) => makeEl(t),
    createTextNode: (t) => ({ textContent: t }),
    body: makeEl('body'), documentElement: makeEl('html'), head: makeEl('head'),
    addEventListener() {}, removeEventListener() {},
    visibilityState: 'visible', hidden: false,
  };
}

function memStorage() {
  const s = {};
  return {
    getItem(k) { return k in s ? s[k] : null; },
    setItem(k, v) { s[k] = String(v); },
    removeItem(k) { delete s[k]; },
    clear() { for (const k in s) delete s[k]; },
  };
}

function makeSandbox(seed) {
  const timers = new Map();
  let nextId = 0;
  let perfNow = 0;
  const winListeners = {};
  const M = Object.create(Math);
  M.random = mulberry32(seed);

  const sandbox = {
    console: { log() {}, warn() {}, error() {}, info() {}, debug() {} },
    document: makeDocument(),
    localStorage: memStorage(),
    sessionStorage: memStorage(),
    requestAnimationFrame() { return ++nextId; }, // collect-and-ignore: render() never runs
    cancelAnimationFrame() {},
    setTimeout(fn) { const id = ++nextId; timers.set(id, fn); return id; },
    clearTimeout(id) { timers.delete(id); },
    setInterval(fn) { const id = ++nextId; timers.set(id, fn); return id; },
    clearInterval(id) { timers.delete(id); },
    performance: { now() { return perfNow += 16.7; } },
    Date, Math: M, JSON, Object, Array, Number, String, Boolean, RegExp,
    Error, TypeError, RangeError, Promise, Symbol, Map, Set, WeakMap,
    parseInt, parseFloat, isNaN, isFinite,
    navigator: { userAgent: 'balance', maxTouchPoints: 1, vibrate() {}, language: 'en-US', getGamepads() { return []; } },
    innerWidth: 390, innerHeight: 844, devicePixelRatio: 2,
    screen: { width: 390, height: 844 },
    matchMedia: () => ({ matches: false, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} }),
    getComputedStyle: () => ({ getPropertyValue() { return ''; } }),
    addEventListener(type, fn) { (winListeners[type] ||= []).push(fn); },
    removeEventListener() {},
    alert() {}, confirm() { return true; }, prompt() { return ''; },
    AudioContext: function () { const d = deepStub('AudioContext'); d.state = 'running'; return d; },
    Image: function () { return { ok: false, width: 0, height: 0, style: {}, addEventListener() {} }; },
    Audio: function () { return { preload: '', volume: 0, currentTime: 0, paused: true, play() { return { catch() {} }; }, pause() {} }; },
  };
  sandbox.webkitAudioContext = sandbox.AudioContext;
  sandbox.window = sandbox;
  sandbox.globalThis = sandbox;
  sandbox.self = sandbox;
  sandbox.top = sandbox;
  return { sandbox, timers, winListeners };
}

function flushTimers(timers) {
  if (!timers.size) return;
  const fns = [...timers.values()];
  timers.clear();
  for (const fn of fns) fn();
}

/* ---- bot, installed into the game context after the game script ---- */
const BOT_SRC = [
  '(function(){',
  'var wanderA=0;',
  'window.__botStep=function(dt){',
  '  if(state==="play"){',
  '    var ne=null,nd=90000;', // nearest enemy within 300px (300^2)
  '    for(var i=0;i<enemies.length;i++){',
  '      var e=enemies[i];if(e.hp<=0)continue;',
  '      var ex=e.x-player.x,ey=e.y-player.y,d2=ex*ex+ey*ey;',
  '      if(d2<nd){nd=d2;ne=e;}',
  '    }',
  '    var dx=0,dy=0;',
  '    if(ne){',
  '      var d=Math.sqrt(nd)||1;',
  '      dx=(player.x-ne.x)/d;dy=(player.y-ne.y)/d;', // steer AWAY
  '    }else{',
  '      var ng=null,ngd=250000;', // nearest gem within 500px (500^2)
  '      for(var j=0;j<gems.length;j++){',
  '        var g=gems[j],gx=g.x-player.x,gy=g.y-player.y,gd=gx*gx+gy*gy;',
  '        if(gd<ngd){ngd=gd;ng=g;}',
  '      }',
  '      if(ng){var d3=Math.sqrt(ngd)||1;dx=(ng.x-player.x)/d3;dy=(ng.y-player.y)/d3;}',
  '      else{wanderA+=0.02;dx=Math.cos(wanderA);dy=Math.sin(wanderA);}',
  '    }',
  '    joy.active=true;joy.sx=0;joy.sy=0;joy.cx=dx*100;joy.cy=dy*100;',
  '    if(ne&&nd<4900&&player.dashCd<=0)tryDash();', // enemy within 70px
  '    if(player.ult>=1)tryUlt();',
  '    update(dt);',
  '  }',
  '  if(player.level>window.__maxLevel)window.__maxLevel=player.level;',
  '  window.__gameTime=gameTime;window.__hp=player.hp;',
  '  return state;',
  '};',
  'window.__pick=function(){',
  '  if(state!=="levelup")return state;',
  '  var idx=currentChoices.findIndex(function(c){return c.t==="evo";});',
  '  if(idx<0)idx=currentChoices.findIndex(function(c){return c.t==="w";});',
  '  if(idx<0)idx=currentChoices.findIndex(function(c){return c.t==="nw";});',
  '  if(idx<0)idx=Math.floor(Math.random()*currentChoices.length);',
  '  applyChoice(currentChoices[idx]);',
  '  return state;',
  '};',
  'window.__chestDone=function(){',
  '  document.getElementById("chestBtn").onclick();',
  '  return state;',
  '};',
  'window.__maxLevel=1;window.__gameTime=0;window.__hp=100;',
  '})();',
].join('\n');

function runSim(seed, hpSum, hpCnt) {
  const env = makeSandbox(seed);
  const sandbox = env.sandbox;
  const ctx = vm.createContext(sandbox);
  try {
    vm.runInContext(GAME_SRC, ctx, { filename: 'survivor-wave.html' });
  } catch (e) {
    return { win: false, death: false, survived: 0, kills: 0, level: 1, maxLevel: 1, error: 'load: ' + errMsg(e) };
  }
  for (const fn of env.winListeners['load'] || []) { try { fn({ type: 'load' }); } catch (e) { /* none expected */ } }
  try {
    vm.runInContext(BOT_SRC, ctx, { filename: 'balance-bot.js' });
    vm.runInContext('startGame()', ctx, { filename: 'balance-start.js' });
  } catch (e) {
    return { win: false, death: false, survived: 0, kills: 0, level: 1, maxLevel: 1, error: 'start: ' + errMsg(e) };
  }

  let err = null;
  let st = 'play';
  let steps = 0;
  while (!err) {
    try {
      flushTimers(env.timers);
      st = sandbox.__botStep(DT);
      let guard = 0;
      while ((st === 'levelup' || st === 'chest') && guard++ < 200) {
        if (st === 'levelup') st = sandbox.__pick();
        else st = sandbox.__chestDone();
      }
      if (st === 'levelup' || st === 'chest') { err = 'stuck in state ' + st; break; }
      if (st === 'over' || st === 'win') break;
      if (st !== 'play') { err = 'unexpected state: ' + st; break; }
      const gt = sandbox.__gameTime;
      if (gt >= GOAL_CAP) { err = 'time cap reached without end state'; break; }
      if (++steps > MAX_STEPS) { err = 'step cap reached'; break; }
      const b = Math.floor(gt / 30);
      if (b >= 0 && b < 20) { hpSum[b] += sandbox.__hp; hpCnt[b]++; }
    } catch (e) {
      err = errMsg(e);
    }
  }

  let survived = 0, kills = 0, level = 1;
  try {
    survived = vm.runInContext('gameTime', ctx);
    const fin = vm.runInContext('({kills:player.kills,level:player.level})', ctx);
    kills = fin.kills; level = fin.level;
  } catch (_) { /* sim already recorded an error */ }
  return {
    win: st === 'win',
    death: st === 'over',
    survived, kills, level,
    maxLevel: Math.max(sandbox.__maxLevel || 1, level),
    error: err,
  };
}

const r2 = (x) => Math.round(x * 100) / 100;
function avg(a) { return a.length ? a.reduce((s, x) => s + x, 0) / a.length : 0; }
function median(a) {
  if (!a.length) return 0;
  const s = [...a].sort((x, y) => x - y);
  const m = s.length >> 1;
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}

const hpSum = new Array(20).fill(0);
const hpCnt = new Array(20).fill(0);
const results = [];
for (let i = 0; i < SIMS; i++) results.push(runSim((0x9E3779B9 ^ (i * 0x85EBCA6B)) >>> 0, hpSum, hpCnt));

const errors = [];
results.forEach((r, i) => { if (r.error) errors.push('sim ' + (i + 1) + ': ' + r.error); });

const report = {
  sims: SIMS,
  wins: results.filter(r => r.win).length,
  deaths: results.filter(r => r.death).length,
  avgSurvived: r2(avg(results.map(r => r.survived))),
  medianSurvived: r2(median(results.map(r => r.survived))),
  avgKills: r2(avg(results.map(r => r.kills))),
  avgLevel: r2(avg(results.map(r => r.level))),
  avgMaxLevel: r2(avg(results.map(r => r.maxLevel))),
  hpSamples: hpSum.map((s, b) => hpCnt[b] ? r2(s / hpCnt[b]) : null),
  errors,
};
console.log(JSON.stringify(report));
