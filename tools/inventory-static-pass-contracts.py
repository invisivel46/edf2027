"""Record bounded static-pass effects, distinguishing tail expansion from entry bodies."""
import csv,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
with (OUT/'contract-callback-instructions.csv').open(encoding='utf-8',newline='') as stream:
    instructions=list(csv.DictReader(stream))
by_function={fn:[r for r in instructions if r['function']==fn] for fn in ['821D96D8','821D9600','821B94E8','821B8E48','82149248','82137410','821375C0','82149A90','82141440','82141AB8','8213C328','821415A8']}
wrapper=by_function['821B94E8']
assert [r['raw'] for r in wrapper]==['3D608258','816BBFB4','808B0008','4BFFF954']
assert wrapper[-1]['target']=='821B8E48'
stores={fn:[r for r in rows if r['mnemonic'].startswith('st') and r['a']!='1'] for fn,rows in by_function.items()}
assert [r['site'] for r in stores['821D96D8']]==['821D9810']
assert not stores['821D9600'] and not stores['821B94E8']
assert [r['site'] for r in stores['821B8E48']]==['821B90D4','821B90E0','821B91A0','821B91AC']
vector_stores=[r for r in by_function['82149248'] if int(r['raw'],16)>>26==31 and (int(r['raw'],16)>>1)&1023==231]
assert [r['site'] for r in vector_stores]==['821492E8','821492F0','821492F8','82149300','82149330']
assert next(r for r in by_function['821415A8'] if r['site']=='82141608')['raw']=='4E800421'
rows=[dict(function='sub_821D96D8',
    inputs='group+4 head,+8 end,+12 geometry/material descriptor; global8257BFB4->device+8',
    direct_effects='after nonempty traversal stores zero to group+4 at821D9810; stack frame store',
    retained_route='geometry setters82137410/82149A90/821375C0; material wrapper821B94E8; each node821D9600 and indexed821FE358',
    remaining='native queued path has geometry/material handoffs; classify nested setter/tail effects and group queue lifetime'),
 dict(function='sub_821B94E8',inputs='material in r3; global8257BFB4 points to owner with device at+8',
    direct_effects='four instructions; loads device into r4; no direct stores',
    retained_route='unconditional tail branch821B94F4 to821B8E48; generated call is hookable sub_821B8E48',
    remaining='shared material activation boundary; do not count expanded Ghidra body as a second unported implementation'),
 dict(function='sub_821B8E48',inputs='material r3,device r4; constant/sampler/state operation tables',
    direct_effects='sampler wordsdevice+1036+24*slot updated at821B90D4/821B91A0; device64bitdirtyword+16 ORed at821B90E0/821B91AC; stack frame store',
    retained_route='native hook selects ActivateNativeMaterial when shader bridge AND material activation enabled; otherwise original; nested shaders/constants/textures/setters remain scoped separately',
    remaining='retain alias-upload, texture-retirement and scissor contracts; original dynamic setter table is not unconditionally entered on native path'),
 dict(function='sub_821D9600',inputs='instance+16 begin,+20 end; device r4; 12byte constant records',
    direct_effects='no non-stack explicit stores in entry-owned instructions; original calls82149248 per record',
    retained_route='native instance hook forwards original then synchronizes native instance; constant uploads are transitive writes',
    remaining='absence of direct stores is not purity; classify constant upload alias/dirty-state consumers before removing replay'),
 dict(function='sub_82149248',inputs='device r3,start register r4,source r5,16byte record count r6,dirty mask r7',
    direct_effects='vector-copy records into device+16*(start+112), aligned destination; OR dirty mask into device64bitword+0; five vector store sites plus finaldirty write',
    retained_route='plain generated helper; Ghidra truncates VMX path; raw words and generated SIMD implementation retain copy order',
    remaining='preserve overlapping source/destination behavior and guest bank consumers; do not infer memcpy equivalence or purity from truncated decompilation'),
 dict(function='sub_82137410',inputs='device,stream,resource,offset,stride,dirty mask; previous resource atdevice12188+4stream',
    direct_effects='native helper writes fetchdescriptor1788-8stream,address1784-8stream,dirty16,binding12188+4stream,stridebyte12256+stream; previous-resource fence or deferred retirement',
    retained_route='shader bridge enabled uses SetNativeStreamResource then native binding publication; reserve callback82141440 and stacktagSP-64 retained; disabled calls original',
    remaining='retirement allocation/consumer lifetime and residual fetch/dirty consumers remain; binding publication alone is not full setter replacement'),
 dict(function='sub_821375C0',inputs='device,newresource; previous resource atdevice12164',
    direct_effects='previous resource+8=fence when device10780 nonzero; otherwise masked resource appends8byte retirement record andadvances device13148; storesnewresource todevice12164',
    retained_route='shader bridge enabled uses SetNativeIndexResource then native binding publication; reserve callback82141440 and stacktagSP-48 retained; disabled calls original',
    remaining='preserve retirement queue allocation/consumer lifetime; same-resource rebinding is not automatically side-effect-free'),
 dict(function='sub_82149A90',inputs='device r3,declaration r4',
    direct_effects='device11536=declaration; device64bitdirtyword16 OR bit51',
    retained_route='native bridge branch reproduces stores and publishes native declaration; disabled original branch then publishes',
    remaining='direct guest writes identified; retained dirty-state consumers still constrain removing compatibility storage'),
 dict(function='sub_82141440',inputs='device; current block13144,cursor13148,end13152,owner13140,scratch15152,flag10809bit40',
    direct_effects='normal path links new encoded block through owner+112 or previousblock+0; previousblock+4 gets used count preservingbit31; cursor=newblock+8,end=newblock+2008; fallback usesdevice15152scratch withoutlinking',
    retained_route='calls8213C328(device,502,128); normal path calls hooked82141AB8 on encoded2008byte range; returnscursor for250eightbyte records',
    remaining='this allocates and links bookkeeping; consuming record tags and actual resource release are separate unclosed obligations'),
 dict(function='sub_82141AB8',inputs='begin/end cache range r3/r4 and caller stack',
    direct_effects='original cache-line flush/sync instructions plusstack scratch; native helper preserves volatile-register andstack outputs',
    retained_route='shader bridge enabled calls NativeCacheFlushCpu; disabled uses original; no geometry write notification in this hook',
    remaining='cache flush is not a resource-retirement consumer or evidence of completion; allocator/queue consumers must be traced separately'),
 dict(function='sub_8213C328',inputs='device,word count,alignment; owner=device13140,owner152,device10809bit40',
    direct_effects='wordcount*4 request; allocation accountingdevice13512 and limit/pointer adjustments; zeroresultsetsdevice10809bit40',
    retained_route='noowner->8213C120; owner152zero->821415A8(owner,1,sizeptr,alignment); owner152nonzero->82141340; native immediate guard onlycaller821FD6E8 alignment16,so retirementcaller82141460 alignment128 forwardsoriginal',
    remaining='metadata allocation callback population and storage lifetime open; immediate allocation suppression does not eliminate retirement allocation'),
 dict(function='sub_821415A8',inputs='owner r3,flags r4,sizepointer r5,alignment r6; globaldevice via82000720',
    direct_effects='raise requested bytes to owner168 minimum; enter/leave criticalsection globaldevice13552 around callback',
    retained_route='bctrl82141608 callsWord(owner172) with r3=Word(owner164),flags,sizepointer,alignment; no target allowlist observed',
    remaining='resolve owner164/168/172 setup and callback implementations; local wrapper alone does not establish allocation/lifetime effects')]
for row in rows:
    row['evidence']='contract-callback-instructions.csv:'+row['function'][4:]+'; out/ghidra/renderer-inventory/contracts/'+row['function'][4:].lower()+'.c'
    row['status']='scoped local effects and hook routing reviewed; transitive ownership remains open'
with (OUT/'static-pass-contracts.csv').open('w',encoding='utf-8',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
print(json.dumps(dict(contracts=len(rows),entry_instruction_counts={fn:len(value) for fn,value in by_function.items()},
    non_stack_store_sites={fn:[r['site'] for r in value] for fn,value in stores.items()}),indent=2))
