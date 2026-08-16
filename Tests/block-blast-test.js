global.localStorage = { getItem: () => '0', setItem: () => {} };

const fs = require('fs');
const html = fs.readFileSync('games/block-blast.html', 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

// Extract SHAPES+GAME STATE and GAME LOGIC sections (skip CANVAS which uses DOM)
const shapesStart = script.indexOf('// ==================== BLOCK SHAPES');
const canvasStart = script.indexOf('// ==================== CANVAS');
const logicStart = script.indexOf('// ==================== GAME LOGIC');
const drawingStart = script.indexOf('// ==================== DRAWING');

const shapesCode = script.substring(shapesStart, canvasStart);
const logicCode = script.substring(logicStart, drawingStart);
const combined = shapesCode + '\n' + logicCode;

const tf = new Function(combined + '; return { GRID, SHAPES, COLORS, canPlace, placePiece, findFullLines, clearLines, hasAnyMoves, generatePieces };');
const m = tf();

let pass = 0, fail = 0;
function t(n, c) { if (c) pass++; else { fail++; console.log('FAIL: ' + n); } }

const empty = new Array(64).fill(0);
t('Grid is 8', m.GRID === 8);
t('Can place 1x1', m.canPlace(empty, [[1]], 0, 0));
t('Can place 2x2', m.canPlace(empty, [[1,1],[1,1]], 0, 0));
t('Cannot place off-grid', !m.canPlace(empty, [[1,1,1],[1,1,1]], 6, 6));

const occ = new Array(64).fill(0); occ[0] = 1;
t('Cannot place on occupied', !m.canPlace(occ, [[1]], 0, 0));
t('Can place adjacent', m.canPlace(occ, [[1]], 0, 1));

const placed = m.placePiece(empty, [[1,1],[1,1]], 0, 0, 0);
t('Place fills 4 cells', placed[0] && placed[1] && placed[8] && placed[9]);
t('Place leaves rest empty', placed[2] === 0);

const fr = new Array(64).fill(0);
for (let c = 0; c < 8; c++) fr[c] = 1;
t('Full row detected', m.findFullLines(fr).rows.length === 1);

const fc = new Array(64).fill(0);
for (let r = 0; r < 8; r++) fc[r * 8] = 1;
t('Full col detected', m.findFullLines(fc).cols.length === 1);
t('Clear removes row', m.clearLines(fr, [0], [])[0] === 0);

const pieces = m.generatePieces();
t('3 pieces generated', pieces.length === 3);
t('Shapes valid', pieces.every(p => p.shape && p.shape.length > 0));
t('Colors valid', pieces.every(p => p.color));
t('Has moves on empty', m.hasAnyMoves(empty, pieces));
t('No moves on full', !m.hasAnyMoves(new Array(64).fill(1), pieces));
t('20+ shapes', m.SHAPES.length >= 20);
t('12+ colors', m.COLORS.length >= 12);

console.log('Block Blast: ' + pass + 'P ' + fail + 'F');
