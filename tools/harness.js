#!/usr/bin/env node
/* Headless smoke-test harness for the single-file HTML games.
 * Usage: node tools/harness.js games/foo.html [frames]
 * Stubs DOM/canvas/audio, executes inline <script>, drives rAF + synthetic
 * input, and prints a JSON report. Exit code 1 if a runtime error occurred.
 */
'use strict';
const fs = require('fs');
const vm = require('vm');

const file = process.argv[2];
const FRAMES = parseInt(process.argv[3] || '240', 10);
if (!file) { console.error('usage: node tools/harness.js <game.html> [frames]'); process.exit(2); }
const html = fs.readFileSync(file, 'utf8');
const scripts = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)].map(m => m[1]);

const errors = [];
const notes = [];
function fail(where, e) {
  const msg = (e && (e.stack || e.message)) || String(e);
  errors.push({ where, msg: String(msg).split('\n').slice(0, 4).join(' | ') });
}

// ---- universal callable/ readable deep stub ------------------------------
function deepStub(label) {
  const fn = function () { return deepStub(label + '()'); };
  const cache = new Map();
  return new Proxy(fn, {
    get(t, p) {
      if (p === Symbol.toPrimitive) return () => 0;
      if (p === Symbol.iterator) return function* () {};
      if (p === 'then') return undefined; // not a promise
      if (p === 'width' || p === 'height' || p === 'length') return 10;
      if (p === 'data') return new Uint8ClampedArray(4);
      if (p === 'toString') return () => label;
      if (p === 'valueOf') return () => 0;
      if (!cache.has(p)) cache.set(p, deepStub(label + '.' + String(p)));
      return cache.get(p);
    },
    set() { return true; },
    apply() { return deepStub(label + '()'); },
    construct() { return deepStub(label + '<new>'); },
  });
}

// ---- element stub --------------------------------------------------------
const listeners = new Map(); // key -> {type:[fns]}
function keyOf(o) { return o.__key || (o.__key = 'el' + (++keyOf.n)); }
keyOf.n = 0;

function makeEl(tag, id) {
  const el = {
    tagName: (tag || 'div').toUpperCase(), id: id || '', className: '',
    style: {}, dataset: {}, children: [], value: '', checked: false,
    textContent: '', innerHTML: '', disabled: false, hidden: false,
    width: 320, height: 320, clientWidth: 320, clientHeight: 320,
    offsetWidth: 320, offsetHeight: 320, offsetLeft: 0, offsetTop: 0,
    scrollWidth: 320, scrollHeight: 320, scrollLeft: 0, scrollTop: 0,
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    addEventListener(type, fn) { const k = keyOf(el); if (!listeners.has(k)) listeners.set(k, {}); (listeners.get(k)[type] ||= []).push(fn); },
    removeEventListener() {},
    appendChild(c) { el.children.push(c); return c; },
    removeChild(c) { return c; },     insertBefore(c) { return c; }, replaceChildren() {}, insertAdjacentHTML() { el.children.push(makeEl('div')); },
    remove() {}, replaceWith() {}, cloneNode() { return makeEl(tag, id); },
    setAttribute() {}, getAttribute() { return null; }, removeAttribute() {}, hasAttribute() { return false; },
    getBoundingClientRect() { return { left: 0, top: 0, right: 320, bottom: 320, width: 320, height: 320, x: 0, y: 0 }; },
    focus() {}, blur() {}, click() { dispatch(el, 'click', {}); },
    querySelector() { return makeEl('div'); },
    querySelectorAll() { return []; },
    closest() { return null; }, matches() { return false; },
    getContext() { return ctx2d(el); },
    toDataURL() { return 'data:image/png;base64,'; },
  };
  Object.defineProperty(el, 'firstChild', { configurable: true, get() {
    if (el.children.length) return el.children[0];
    if (!el.__txt) el.__txt = { nodeValue: '', textContent: '' };
    return el.__txt;
  }});
  Object.defineProperty(el, 'lastChild', { configurable: true, get() {
    return el.children.length ? el.children[el.children.length - 1] : null;
  }});
  el.nextSibling = null; el.ownerDocument = null;
  Object.defineProperty(el, 'parentElement', { configurable: true, get() { return getBody(); } });
  Object.defineProperty(el, 'parentNode', { configurable: true, get() { return getBody(); } });
  return el;
}

let _body = null;
function getBody() {
  if (!_body) {
    _body = makeEl('body');
    Object.defineProperty(_body, 'parentElement', { configurable: true, get() { return null; } });
    Object.defineProperty(_body, 'parentNode', { configurable: true, get() { return null; } });
  }
  return _body;
}

function ctx2d(canvas) {
  const base = deepStub('ctx');
  return new Proxy(base, {
    get(t, p) {
      if (p === 'canvas') return canvas;
      if (p === 'measureText') return () => ({ width: 12 });
      if (p === 'createLinearGradient' || p === 'createRadialGradient' || p === 'createPattern')
        return () => ({ addColorStop() {} });
      if (p === 'getImageData') return () => ({ data: new Uint8ClampedArray(4 * 8 * 8), width: 8, height: 8 });
      return Reflect.get(t, p);
    },
    set(t, p, v) { t[p] = v; return true; },
  });
}

// ---- document / window ---------------------------------------------------
const els = new Map();
function byId(id) { if (!els.has(id)) els.set(id, makeEl('div', id)); return els.get(id); }

const docListeners = {};
const documentStub = {
  getElementById: byId,
  querySelector: (sel) => makeEl('div', sel),
  querySelectorAll: () => [],
  createElement: (t) => makeEl(t),
  createTextNode: (t) => ({ textContent: t }),
  body: getBody(), documentElement: makeEl('html'), head: makeEl('head'),
  addEventListener(type, fn) { (docListeners[type] ||= []).push(fn); },
  removeEventListener() {},
  visibilityState: 'visible', hidden: false,
};

const winListeners = {};
const rafQ = [];
let now = 0;
const timeouts = [];

const sandbox = {
  console: { log() {}, warn() {}, error(...a) { notes.push('console.error: ' + a.join(' ')); }, info() {}, debug() {} },
  document: documentStub,
  localStorage: { _s: {}, getItem(k) { return k in this._s ? this._s[k] : null; }, setItem(k, v) { this._s[k] = String(v); }, removeItem(k) { delete this._s[k]; }, clear() { this._s = {}; } },
  sessionStorage: { getItem() { return null; }, setItem() {}, removeItem() {}, clear() {} },
  requestAnimationFrame(cb) { rafQ.push(cb); return rafQ.length; },
  cancelAnimationFrame() {},
  setTimeout(fn, ms) { timeouts.push(fn); return timeouts.length; },
  clearTimeout() {},
  setInterval(fn) { timeouts.push(fn); return timeouts.length; },
  clearInterval() {},
  performance: { now: () => (now += 16.7) },
  Date,
  Math, JSON, Object, Array, Number, String, Boolean, RegExp, Error, TypeError, RangeError, Promise, Symbol, Map, Set, WeakMap, parseInt, parseFloat, isNaN, isFinite,
  navigator: { userAgent: 'harness', maxTouchPoints: 1, vibrate() {}, language: 'en-US' },
  innerWidth: 390, innerHeight: 844, devicePixelRatio: 2,
  screen: { width: 390, height: 844 },
  matchMedia: () => ({ matches: false, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} }),
  getComputedStyle: () => ({ getPropertyValue() { return ''; } }),
  addEventListener(type, fn) { (winListeners[type] ||= []).push(fn); },
  removeEventListener() {},
  alert() {}, confirm() { return true; }, prompt() { return ''; },
  CanvasRenderingContext2D: function () {},
  AudioContext: function () { return deepStub('AudioContext'); },
  webkitAudioContext: function () { return deepStub('AudioContext'); },
  Image: function () { return makeEl('img'); },
  atob: (s) => Buffer.from(s, 'base64').toString('binary'),
  btoa: (s) => Buffer.from(s, 'binary').toString('base64'),
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;
sandbox.self = sandbox;
sandbox.top = sandbox;

// ---- event dispatch ------------------------------------------------------
function dispatch(target, type, props) {
  const k = keyOf(target);
  const fns = (listeners.get(k) && listeners.get(k)[type]) || [];
  const ev = Object.assign({ type, target, currentTarget: target, preventDefault() {}, stopPropagation() {}, clientX: 160, clientY: 160, pageX: 160, pageY: 160, touches: [], changedTouches: [], key: '', code: '' }, props);
  for (const fn of fns) { try { fn(ev); } catch (e) { fail('event:' + type, e); } }
}
function dispatchWin(type, props) {
  const fns = winListeners[type] || [];
  const ev = Object.assign({ type, preventDefault() {}, stopPropagation() {} }, props);
  for (const fn of fns) { try { fn(ev); } catch (e) { fail('winevent:' + type, e); } }
}
function dispatchDoc(type, props) {
  const fns = docListeners[type] || [];
  const ev = Object.assign({ type, preventDefault() {}, stopPropagation() {} }, props);
  for (const fn of fns) { try { fn(ev); } catch (e) { fail('docevent:' + type, e); } }
}

// ---- run -----------------------------------------------------------------
const ctx = vm.createContext(sandbox);
for (const src of scripts) {
  try { vm.runInContext(src, ctx, { filename: file }); }
  catch (e) { fail('load', e); }
}
dispatchDoc('DOMContentLoaded', {});
dispatchWin('load', {});
dispatchWin('resize', {});

// find a start/play button and click it
let clicked = false;
for (const el of els.values()) {
  const t = String(el.id + ' ' + el.textContent).toLowerCase();
  if (/play|start|begin|go\b/.test(t)) { try { dispatch(el, 'click', {}); clicked = true; break; } catch (e) { fail('click', e); } }
}
if (!clicked) { for (const el of els.values()) { try { dispatch(el, 'click', {}); break; } catch (e) {} } }

// synthetic input
try {
  dispatchWin('keydown', { key: 'ArrowRight', code: 'ArrowRight' });
  dispatchWin('keydown', { key: ' ', code: 'Space' });
  dispatchWin('keyup', { key: ' ', code: 'Space' });
  for (const el of els.values()) {
    dispatch(el, 'pointerdown', { clientX: 160, clientY: 160 });
    dispatch(el, 'touchstart', { touches: [{ clientX: 160, clientY: 160 }], changedTouches: [{ clientX: 160, clientY: 160 }] });
    dispatch(el, 'pointermove', { clientX: 200, clientY: 200 });
    dispatch(el, 'pointerup', { clientX: 200, clientY: 200 });
    dispatch(el, 'touchend', { changedTouches: [{ clientX: 200, clientY: 200 }] });
  }
} catch (e) { fail('input', e); }

// drive frames
let ran = 0;
for (let f = 0; f < FRAMES && rafQ.length; f++) {
  const q = rafQ.splice(0, rafQ.length);
  for (const cb of q) { try { cb(now); ran++; } catch (e) { fail('raf', e); break; } }
  // flush a bounded number of timers each frame
  const t = timeouts.splice(0, 4);
  for (const fn of t) { try { fn(); } catch (e) { fail('timer', e); } }
}

const report = {
  file, ok: errors.length === 0, frames: ran,
  scripts: scripts.length, clickedStart: clicked,
  errors: errors.slice(0, 6), notes: notes.slice(0, 6),
};
console.log(JSON.stringify(report, null, 2));
process.exit(errors.length ? 1 : 0);
