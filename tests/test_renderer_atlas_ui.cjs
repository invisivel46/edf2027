// Tests the generated atlas's filtering/pagination logic without a browser.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('out/renderer-atlas/index.html','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
function element(){return {value:'',checked:false,textContent:'',children:[],events:{},
  append(x){this.children.push(x)},replaceChildren(){this.children=[]},
  addEventListener(type,fn){this.events[type]=fn}}}
const ids={};for(const id of ['q','lane','status','gaps','count','rows','prev','next'])ids[id]=element();
const context={document:{getElementById:id=>ids[id],createElement:()=>element()}};
vm.createContext(context);vm.runInContext(script,context);
assert.match(ids.count.textContent,/9216 functions/);assert.equal(ids.rows.children.length,100);
const first=ids.rows.children[0].children[0].children[0].textContent;
ids.next.onclick();assert.notEqual(ids.rows.children[0].children[0].children[0].textContent,first);
ids.prev.onclick();assert.equal(ids.rows.children[0].children[0].children[0].textContent,first);
ids.q.value='sub_821A5080';ids.q.events.input();assert.equal(ids.rows.children.length,1);
assert.equal(ids.rows.children[0].children[0].children[0].href,'functions/sub_821A5080.html');
ids.q.value='does-not-exist';ids.q.events.input();assert.equal(ids.rows.children.length,0);
ids.q.value='';ids.lane.value='straight-line';ids.lane.events.input();
assert.ok(ids.rows.children.length>0);assert.ok(ids.rows.children.every(r=>r.children[1].textContent==='straight-line'));
ids.lane.value='';ids.status.value='partial';ids.status.events.input();assert.match(ids.count.textContent,/511 functions/);
ids.status.value='';ids.gaps.checked=true;ids.gaps.events.input();assert.ok(ids.rows.children.length>0);
console.log('Atlas UI logic: census, pagination, symbol search, empty results, lane/status/gap filters passed.');
