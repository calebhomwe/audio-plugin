const fs = require('fs');
let s = fs.readFileSync('tools/dbg.js', 'utf8');
const old = 'if (b >= 0 && b < 20) { hpSum[b] += sandbox.__hp; hpCnt[b]++; }';
const add = old + '\n      if (sandbox.__hp > 5000 && !sandbox.__dbg) { sandbox.__dbg = true; console.log("TRACE", vm.runInContext("JSON.stringify({t:gameTime|0,hp:player.hp|0,mx:player.maxHp|0,lv:player.level,w:player.weapons.map(function(x){return x.id+x.lvl;}),p:player.passives})", ctx)); }';
if (!s.includes(old)) { console.error('anchor missing'); process.exit(1); }
s = s.replace(old, add);
fs.writeFileSync('tools/dbg.js', s);
console.log('patched');
