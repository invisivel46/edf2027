"""Group retained call sites and expose adapter closure without inferring purity.

Guest callees are boundaries, not recursively expanded adapter bodies. A plain
sub_* call may enter a native hook; __imp__sub_* explicitly selects the original.
Store counts are syntax counts only and exclude vector/atomic/helper effects.
"""
import csv, hashlib, json, re
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'

def read(name):
    with (OUT/name).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))

def write(name, rows):
    with (OUT/name).open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

def normalize(name):
    return name.removeprefix('__imp__')

# Do not join stale extraction bodies to a current caller inventory.
for row in read('adapter-extraction-manifest.csv'):
    path = OUT/'adapters'/row['output']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'], path
for row in read('native-source-manifest.csv'):
    path = ROOT/row['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'], path

functions = {r['function']: r for r in read('complete-function-inventory.csv')}
adapters = {r['function']: r for r in read('adapter-functions.csv')}
calls = defaultdict(list)
stores = defaultdict(list)
for row in read('adapter-calls.csv'):
    calls[row['function']].append(row)
for row in read('adapter-store-sites.csv'):
    stores[row['function']].append(row)
special = defaultdict(list)
for fn, info in adapters.items():
    filename, line = info['source'].rsplit(':', 1)
    text = (ROOT/filename).read_text(encoding='utf-8-sig')
    start = text.index('{', text.index('('+fn+')')) + 1
    end, depth = start, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    # Deliberately scoped operation families, not a universal write detector.
    for match in re.finditer(r'\b(__sync_bool_compare_and_swap|simde_mm_store\w*|REX_ENTER_GLOBAL_LOCK|REX_LEAVE_GLOBAL_LOCK|REX_CHECK_GLOBAL_LOCK)\s*\(', text[start:end]):
        pos = start + match.start()
        special[fn].append(dict(function=fn, operation=match[1],
            source=f'{filename}:{text.count(chr(10), 0, pos)+1}',
            expression=text[pos:text.find('\n', pos)].strip(),
            review='explicit atomic/vector/lock syntax; address and ownership require review'))

def closure(root):
    seen, pending, terminal = set(), [root], []
    while pending:
        fn = pending.pop()
        if fn in seen:
            continue
        seen.add(fn)
        for edge in calls[fn]:
            target = normalize(edge['callee'])
            if target in adapters:
                pending.append(target)
            else:
                terminal.append(edge)
    return seen, terminal

grouped = defaultdict(list)
callback_reviews={r['source'].replace('\\','/'):r for r in read('retained-callback-review.csv')}
for row in read('native-dependency-classification.csv'):
    grouped[row['callee']].append(row)
rows, edges = [], []
scoped_effects={
 'edf_native_indexed_cpu_tail':
  'local device dirty words+0/+8/+16/+24/+32 cleared; conditional+11568 write; nested shader identity/cache/derived state retained; six-adapter closure has43scalar store sites plus shader-cache atomic/locks',
 'edf_native_counter_reset_cpu_tail':
  'profiling flags/result+15144/+15148; frame counter/timing and query/resource state; three external monitor callback sites mapped; setup exposes profiling-state aliases to monitor',
 'edf_native_worker_audit':
  'worker+0 atomic lock; direct worker writes+16/+20/+24/+28/+32/+36/+48/+52/+80/+84/+88/+108; command81000000 installs callback and argument; job target and nested command-helper effects remain open'}
for entry in read('static-pass-contracts.csv'):
    scoped_effects[entry['function']]=entry['direct_effects']+'; '+entry['retained_route']
scoped_evidence=defaultdict(set)
for target in scoped_effects:
    scoped_evidence[target].add('static-pass-contracts.csv:'+target if target.startswith('sub_') else 'docs/renderer-completion-inventory.md: retained adapter review')

def append_effect(target, effect, evidence):
    scoped_effects[target]='; '.join(filter(None,[scoped_effects.get(target,''),effect]))
    scoped_evidence[target].add(evidence)

append_effect('edf_native_buffer_lock_cpu_tail',
 'Preserves original parent/resource fence wait, off-thread atomic range accumulation, packed128byte dirty bounds at resource20/24, returned alias and atomicadd0x100 at resource0. Removes only original821344E8..8213456C render-thread cache packet block/cursor writes. Header suffix wrapped by begin/complete tracking over28bytes after retained services. Guarded hook routes access10/12 or access14 withLR8213AE24 when shader_bridge enabled;other cases original. Resource lifetime, tracking concurrency and native parity remain open.',
 'tools/extract-native-buffer-lock.cmake | out/renderer-inventory/adapters/native_buffer_lock.cpp | src/native_graphics/guest_shader_bridge.cpp:6827,6964')
for suffix in ('82134958','82134A78'):
 append_effect('edf_native_lock_'+suffix,
  'Hash-gated copy of original lock wrapper '+suffix+'; same arguments/stack stores,callee replaced by edf_native_buffer_lock_cpu_tail. Inherits retained waits, dirty-range/header/atomic effects and tracking; ownership remains open.',
  'tools/extract-native-buffer-lock.cmake | out/renderer-inventory/adapters/native_buffer_lock.cpp')

# These are original-body effects. Do not join native hook replacement behavior
append_effect('edf_native_immediate_cpu_tail',
 'Extracted FD8F8 wrapper retains stack/ABI and calls extracted FD428 prepare with entryr7 asstride; novertexcopy/cursorcommit/flush. Prepare temporarily writesdevice12256=(stride>>2)&255,setsdirty16 bit51 ifdifferentfromdevice11552. Processes/clears five64bit dirtywords0/8/16/24/32; ifdirty24 mask2 setsdevice11568=0xffffffffff000000. Main-state adapter called fordirty16 bits49..52; packet bankhelpers removed,D938 replaced bymask&~0x100. Restoresoldstride12256 andreturns0; novertexallocation/failureflag/GPUscratch path. Nestedmainstate effects and metadata freshness remainopen. Nativehost selectsadapter;otherwiseoriginal.',
 'tools/extract-native-immediate-tail.cmake | tools/native-draw-prefix.cmake | out/renderer-inventory/adapters/native_immediate_cpu_tail.cpp | src/native_graphics/guest_shader_bridge.cpp:10004')
append_effect('edf_native_main_state_cpu_tail',
 'Extracted ECB0 retains boundshader12420/12416 anddeclaration11536 metadata selection. Directnonstack destinations:10452 low3bitmode(5withoutpeer,4onpeerreload),10810 flags3/6,10408 peerword24,12424 effective mode,10400/10404 combinedmetadata. Returnsmodifiedentryr4 dirtymask; localdoesnotstoredevice16. RemovesEAB0 andtwoinlinepacketregions; retainsbit46set,late10808mask0x40 clearingmask8 andbits47/48if48set. Directlycalls uploadadapter twice,cacheadapter twice,derivedadapter once(conditionally); GPRsave/restore onlyothercalls. Input validity/cache atomics/transitiveownership separate. PublichookOwnsMainStatePackets guardelseoriginal; drawadapters directlyinvoke.',
 'tools/extract-native-main-state.cmake | out/renderer-inventory/adapters/native_main_state_cpu_tail.cpp | src/native_graphics/guest_shader_bridge.cpp:10011')
append_effect('edf_native_device_defaults',
 'Extracted EFF8 sets device11544=0x00000000ffffffff; clears low2bits of26words at1024+24*i; sets low2bits=1 for18words at1648+8*i. Writes defaults10476=8,10300=0x20002000,10500/10640/10788=14,10580=0xff000,10584=0xff100,10316=0xffffff,10452/10560=4,10644=16,10696=2; OR10436 mask0x80000 and10440 mask0x10000. Sets five64bit dirtywords0..32 toallones. Packet tail removed,return3650. Hook always invokes adapter then PublishNativeRenderState; native publication/lifetime obligations separate.',
 'tools/extract-native-device-defaults.cmake | out/renderer-inventory/adapters/native_device_defaults.cpp | src/native_graphics/guest_shader_bridge.cpp:5858')
append_effect('edf_native_derived_cpu_tail',
 'Extracted D750 has no remaining calls. Computes device10432 bits0/1 from device10420,10810,11580 and optional shader12416 relative metadata; preserves otherbits. Conditionally clears/sets device10809 mask4. Only nonstack store destinations are10432 and10809;returnentryr4|0x100. Removes rollover and2wordpacket/cursor emission but retains cursor40/limit48 reads. Publichook guarded byOwnsDerivedStatePackets(device,LR),otherwiseoriginal; native main-state adapter directly invokes thisadapter. Input freshness/equivalence remainopen.',
 'tools/extract-native-derived-tail.cmake | out/renderer-inventory/adapters/native_derived_cpu_tail.cpp | src/native_graphics/guest_shader_bridge.cpp:10053')
append_effect('edf_native_swap_wait_cpu_tail',
 'Extracted512D8 skips wait ifdevice13220==0x80000000 or(device13456&0xfffff000)==0;otherwise calls39148(device,1),native_swap_wait(device),39148(device,0). Normalexit ORsdevice10809 mask0x20. Replaces packet/reservation span withnativehelper;publichook unconditional. Nativehelper accepts modes0/1/2/4,requiresinitializedstate andnullcallback15120;submitsframe underlocks,waitsGPUcredit/completion (10s deadline) andpacing (1sdeadline),writes15136 callbackcountbeforepacingwait and15124/28/32 after. Exceptions canbypass closing39148 andfinalflag. Scheduling/39148/publication equivalence remainsopen.',
 'tools/extract-native-present.cmake | out/renderer-inventory/adapters/native_present.cpp:1267 | src/native_graphics/guest_shader_bridge.cpp:5279,5350')
append_effect('edf_native_shader_upload_cpu_tail',
 'Extracted E950 removes shader allocation/copy/packet span, E070 call and device40 cursor publication; code destination r26=0. If entryr4 nonzero invokes output adapter with entryr6 as output pointer,entryr8 metadata,entryr9 optional peer. Preserves device10810 bit7=(entryr4!=0), other bits unchanged; copies16bytes device12256/12264 to11552/11560 from existing source fields (original E070 also only reads this stride source). Metadata prefix reads remain. Hook gated by OwnsShaderUpload(device,LR), otherwise original. Source freshness and output ownership/equivalence remain open.',
 'tools/extract-native-shader-upload.cmake | out/renderer-inventory/adapters/native_shader_upload_cpu_tail.cpp | src/native_graphics/guest_shader_bridge.cpp:10021')
append_effect('edf_native_shader_output_cpu_tail',
 'Extracted E800 removes E678/E748 calls but preserves metadata traversal/reads. Early exits leave entryr6 output untouched if metadata8 low3bits=7, metadata20 mask0x40000, or optional peer20 mask0x20000. Otherwise replaces output mask0x00f00000 with low4bits of max((peer20&31)-1,0), or0 without peer. Only non-stack direct store is this output word; descriptor traversal and input bounds remain obligations. Hook gated by OwnsShaderOutputPatch(r29,LR); upload adapter invokes this adapter directly. No shader destination dereference in adapter.',
 'tools/extract-native-shader-output.cmake | out/renderer-inventory/adapters/native_shader_output_cpu_tail.cpp | src/native_graphics/guest_shader_bridge.cpp:10031')
append_effect('edf_native_worker_init',
 'Preserves EE50 CPUworker/list/arena records,6selectedaffinitybits,ExCreateThread(start8214EAD0,stack32768) andpriority17,errorreturn0/success1. Replacesdevice13476 ringaliasloadwith0 andomitssetup packetreservation/4stores/cursorpublication. No localrollback ofpreviouslycreatedthreads onfailure. Hook alwaysusesadapter;thread/platform lifetime obligations remain.',
 'tools/extract-native-worker-init.cmake | out/renderer-inventory/adapters/native_worker_init.cpp | small-retained-contracts.csv:sub_8214EE50')
append_effect('edf_native_worker_commands_audit',
 'Hash-gated original E640 command traversal retained; adds edf_native_audit_worker_command beforecommandread. Preserves CPUrecord copy,flags/loop/continuationwrites and all submission/tiling/helper dispatch. Diagnostichelper gatedbyhook_timings,readscommand/worker fields,updateshostring/atomicvisitcounters/logs;invalidcursorlogs thenpreservesfailingread. Doesnotreplace guestcommand execution. Helper effects/populations remainopen.',
 'tools/extract-native-worker-audit.cmake | out/renderer-inventory/adapters/native_worker_audit.cpp | src/native_graphics/guest_shader_bridge.cpp:5520 | small-retained-contracts.csv:sub_8214E640')

# These are original-body effects. Do not join native hook replacement behavior
for suffix in ('821349B8','82134AD8'):
 append_effect('edf_native_unlock_'+suffix,
  'Wrapper loadsresource24 asbase (349B8 masks low2bits),sets secondbase0,dispatchesextracted34640. Thatbody atomicallysubtracts0x100 fromresource0;onlyoldcountmask0xf00==0x100 consumesdirtybounds20(and24ifsecondbase),resettingto0xffff0000. CallsNativeCacheFlushCpu in placeof82141AB8;helper preserves scratch/register effects,doesnot notifygeometrywrites. No guestcachehelperdispatch;header/lifetime/concurrency equivalence remainsopen.',
  'tools/extract-native-buffer-unlock.cmake | out/renderer-inventory/adapters/native_buffer_unlock.cpp | src/native_graphics/native_cache_flush.h')
for mode,value in [('table','6434'),('pwl','-1')]:
 append_effect('edf_native_gamma_'+mode+'_cpu_tail',
  'Adapterbody onlysets r3='+value+';no guestmemorywrites orcalls. Nativehosthook firstdecodes1536bytes into nativegamma understatemutex,tracksdevice,rejectschangeddevicewithoutreset. Originalfallback retainedwhen nativehostdisabled. Table decoding/value equivalence remainsseparate from return-register adapter.',
  'out/renderer-inventory/adapters/native_present.cpp:1256 | src/native_graphics/guest_shader_bridge.cpp:4565')
append_effect('edf_native_worker_signal_cpu_tail',
 'ExtractedC9F0 suppresses packetstores and replaces device10772 loadswith0;retains generated(flags&C0FFFFFF)==0 prefixpredicate,register/cursor arithmetic andreturnlastwordaddress r4+92or100 plusGPRsave/restore. Hookcapture startsalias(begin+4),bytes=return-begin;CPU-mask=(flags>>24)&63 or4 matchesoriginal. Nativehosthook accepts onlycallback8214EBA0,validatesnonzero32bitspan,queuesnative(callback,argument,CPU-mask) metadata underlocks with4096pending/deliverycapacity;doesnot executecallback here. NativeSignalQueue rejectsunaligned/empty/wrapped/overlappingunsubmittedranges;SubmitRange rejects partialoverlap,sortscontainedsignals byaddress,markscompletion beforeerasingcaptures. FIFOcompletion transfers intoacknowledgedledger;CPUdelivery tracksremainingbits aftereachsuccessfulwake andblockswhilesubmitting. Fullcallerlock/publication/backendcallback ordering remainsseparate;othernativehostcallbacks throw.',
 'out/renderer-inventory/adapters/native_ring_submit.cpp:336 | src/native_graphics/guest_shader_bridge.cpp:5453')

# These are original-body effects. Do not join native hook replacement behavior
append_effect('edf_native_cache_range_cpu_tail',
 'Preserves original source-presence tests and conditionally consumes device11544 through8213BE68 into stackoutputs;alwayswrites entryr5 count0,leaves entryr4 address untouched. Removes allocation and packetencoding. Nativehost hook selects onlyLR8213CF9C;transitive atomic/lifetime obligations remain.',
 'tools/extract-native-cache-range.cmake | out/renderer-inventory/adapters/native_cache_range.cpp | src/native_graphics/guest_shader_bridge.cpp:5745')
append_effect('edf_native_cache_reservation_cpu_tail',
 'Preserves source tests and8213C328(device,11*enabledsources,32) allocation/failure. Successconditionallyconsumes11544 viaBE68 andwrites convertedallocation address throughentryr4;allpaths writeentryr5 count0. No packetencoding. Zero-work/failure leavesaddressuntouched anddoesnotconsume range. Nativehost hook selectsLR8214ED04;reservedbufferlifetime remainsopen.',
 'tools/extract-native-cache-range.cmake | out/renderer-inventory/adapters/native_cache_range.cpp | src/native_graphics/guest_shader_bridge.cpp:5745')
append_effect('edf_native_device_drain',
 'Removes original drain packetreservation/encoding/cursorwrite;preserves8213C928(device,current,4,0) thenloopuntildevice10868zero. Eachiterationcallsedf_native_drain_worker_signals:pollnativeworker signals,checkqueue/deliverypending underlocks,throwonunsubmitted,sleep1mswhilepending;also sleep1msifdevicecountnonzero. Delivery/callback ownership and progress guarantees remainopen.',
 'tools/extract-native-device-reset.cmake | out/renderer-inventory/adapters/native_device_reset.cpp | src/native_graphics/guest_shader_bridge.cpp:5802')
append_effect('edf_native_device_reset',
 'Preserves conditionaloldworkdrain,oldbufferflush/free,commandstorage and96/32bytewriteback allocation/zeroing,devicecursor/counterinitialization,nullconfigsuccess andallocationfailurestatus. Afterdrain/beforefree callsnative completion retirement under submission/state/delivery locks;rejectspendingworker deliveries/signals andunsubmittedfences,erasesqueues/clocks/publishedcompletions/faults,retirescursor,andconditionallyinvalidatesgamma/presentation. RemovesGPUringallocation/borrowedpointer(set0),ringtranslation/exports andstartup packetspanD560..D680;retains emptycommandcursor. Hook initializesnativecursor onlynonnullconfig/success. Allocationfailure rollback and concurrency/lifetime parity remainopen.',
 'tools/extract-native-device-reset.cmake | out/renderer-inventory/adapters/native_device_reset.cpp | src/native_graphics/guest_shader_bridge.cpp:5772,5825')

# These are original-body effects. Do not join native hook replacement behavior
# as the effect of an explicit __imp__ call, which bypasses that hook.
for entry in read('scalar-store-function-summary.csv'):
    if entry['destinations']:
        append_effect(entry['function'],'local store provenance '+entry['owner_classes']+': '+entry['destinations'],
                      'scalar-store-owners.csv:'+entry['function'])
for filename,field in [('sampler-contracts.csv','effects'),('pass-format-contracts.csv','effects'),('helper-callback-review.csv','effects'),('small-retained-contracts.csv','effects')]:
    for entry in read(filename):
        append_effect(entry['function'],entry[field],filename+':'+entry['function'])
for entry in read('device-state-getters.csv'):
    append_effect(entry['function'],entry['result']+'; stores: '+entry['stores'],'device-state-getters.csv:'+entry['function'])
for symbol, sites in sorted(grouped.items()):
    target = normalize(symbol)
    kind = 'extracted adapter' if target in adapters else 'guest original' if target.startswith('sub_') else 'platform import'
    reachable, terminal = closure(target) if target in adapters else (set(), [])
    for edge in terminal:
        callee = normalize(edge['callee'])
        route = ('indirect unresolved' if callee == 'REX_CALL_INDIRECT_FUNC' else
                 'explicit guest original' if edge['callee'].startswith('__imp__sub_') else
                 'hookable guest entry' if callee.startswith('sub_') else
                 'platform import' if edge['callee'].startswith('__imp__') else 'native helper')
        edges.append(dict(retained_callee=symbol, adapter=edge['function'],
            callee=edge['callee'], route=route, source=edge['source'],
            native_boundary=functions.get(callee, {}).get('native_boundary', ''),
            target_provenance=callback_reviews.get(edge['source'].replace('\\','/'),{}).get('target_expression',''),
            activation=callback_reviews.get(edge['source'].replace('\\','/'),{}).get('activation',''),
            contract='boundary exposed; transitive effects not inferred'))
    own = functions.get(target, {})
    syntax = [r for fn in reachable for r in stores[fn]]
    rows.append(dict(callee=symbol, target_kind=kind, call_sites=len(sites),
        callers=' | '.join(sorted({r['hook'] for r in sites})),
        source_sites=' | '.join(sorted(r['source'] for r in sites)),
        path_classes=' | '.join(sorted({r['path_class'] for r in sites})),
        work_batches=' | '.join(sorted({r['work_batch'] for r in sites})),
        guest_direct_sites=own.get('direct_sites', ''), guest_indirect_sites=own.get('indirect_sites', ''),
        ghidra_definition_present=own.get('ghidra_function_present', ''),
        adapter_closure=' | '.join(sorted(reachable)),
        adapter_scalar_store_sites=len(syntax) if reachable else '',
        adapter_atomic_vector_lock_sites=sum(len(special[fn]) for fn in reachable) if reachable else '',
        adapter_terminal_sites=len(terminal) if reachable else '',
        adapter_terminal_targets=' | '.join(sorted({r['callee'] for r in terminal})),
        scoped_effects=scoped_effects.get(target,''),
        scoped_evidence=' | '.join(sorted(scoped_evidence[target])),
        effect_status='partial local effect review; transitive ownership and runtime population remain open' if target in scoped_effects else
            'pending ownership review; call graph and scalar store syntax attached' if reachable else
            'pending callee effect review; call-path classification attached',
        review_requirement='assign direct and transitive writes, callbacks and lifetime effects before removing or rescheduling this call'))
assert sum(r['call_sites'] for r in rows) == sum(map(len, grouped.values()))
assert all(r['ghidra_definition_present'] == 'True' for r in rows if r['target_kind'] == 'guest original')
write('retained-callee-contracts.csv', rows)
write('retained-adapter-terminal-edges.csv', edges)
write('adapter-nonscalar-operations.csv', [r for values in special.values() for r in values])
by_callee={r['callee']:r for r in rows}
call_reviews=[]
for site in read('native-dependency-classification.csv'):
    callee=by_callee[site['callee']]
    call_reviews.append(dict(**site,joined_callee_effect_status=callee['effect_status'],
                             joined_callee_effects=callee['scoped_effects'],joined_callee_evidence=callee['scoped_evidence']))
write('retained-call-effect-reviews.csv',call_reviews)
macro_reviews=[]
for site in read('macro-original-dependencies.csv'):
    target=normalize(site['callee'])
    macro_reviews.append(dict(**site,joined_callee_effects=scoped_effects.get(target,''),
      joined_callee_evidence=' | '.join(sorted(scoped_evidence[target])),
      joined_callee_effect_status='partial local effect review; ownership and value equivalence open' if target in scoped_effects else 'pending callee effect review; macro forwarding verified'))
write('macro-call-effect-reviews.csv',macro_reviews)
summary = dict(call_sites=sum(r['call_sites'] for r in rows), unique_callees=len(rows),
    target_kinds=dict(Counter(r['target_kind'] for r in rows)),
    scoped_effect_review_callees=sum(bool(r['scoped_effects']) for r in rows),
    scoped_effect_review_call_sites=sum(int(r['call_sites']) for r in rows if r['scoped_effects']),
    joined_call_statuses=dict(Counter(r['joined_callee_effect_status'] for r in call_reviews)),
    macro_call_sites=len(macro_reviews),macro_call_sites_with_scoped_effects=sum(bool(r['joined_callee_effects']) for r in macro_reviews),
    terminal_route_counts=dict(Counter(r['route'] for r in edges)),
    terminal_edge_rows=len(edges),
    unique_terminal_sites=len({r['source'] for r in edges}),
    explicit_atomic_vector_lock_operations=dict(Counter(r['operation'] for values in special.values() for r in values)),
    limitation='Terminal rows repeat across retained roots. Counts describe syntax and conservative reachability, not runtime activation, purity or native completion.')
(OUT/'retained-contract-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
