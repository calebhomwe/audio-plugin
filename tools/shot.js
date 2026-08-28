#!/usr/bin/env node
/* Codified gameplay screenshot for survivor-wave.html.
 * Usage: node tools/shot.js [battle|levelup] [out.png]
 * --virtual-time-budget drives timers but NOT rAF, so we inject a script
 * that calls startGame() and fast-forwards update() manually.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const mode = process.argv[2] || 'battle';
const out = path.resolve(process.argv[3] || path.join(process.env.TEMP || '.', 'sw-' + mode + '.png'));
const src = fs.readFileSync(path.join(__dirname, '..', 'games', 'survivor-wave.html'), 'utf8');

const INJ = {
  battle: '<script>window.addEventListener("load",function(){setTimeout(function(){startGame();keys["d"]=true;keys["w"]=true;var n=0;var iv=setInterval(function(){for(var i=0;i<10;i++){if(state==="levelup"&&currentChoices.length)applyChoice(currentChoices[0]);if(state==="chest"){var b=document.getElementById("chestBtn");if(b.onclick)b.onclick();}if(state==="play")update(1/30);}if(++n>40)clearInterval(iv);},50);},300);});</script>',
  levelup: '<script>window.addEventListener("load",function(){setTimeout(function(){startGame();keys["d"]=true;for(var i=0;i<1200&&state==="play";i++)update(1/30);},300);});</script>',
};
if (!INJ[mode]) { console.error('mode must be battle|levelup'); process.exit(2); }

const tmp = path.join(process.env.TEMP || '.', 'sw-shot-' + mode + '.html');
fs.writeFileSync(tmp, src.replace('</body>', INJ[mode] + '</body>'), 'utf8');

const edge = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const url = 'file:///' + tmp.replace(/\\/g, '/');
execFileSync(edge, ['--headless=new', '--disable-gpu', '--screenshot=' + out, '--window-size=960,640', '--virtual-time-budget=6000', url], { stdio: 'ignore' });
console.log(JSON.stringify({ mode, out, bytes: fs.statSync(out).size }));
