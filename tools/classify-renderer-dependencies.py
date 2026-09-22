"""Apply source-reviewed call-path classifications, retaining unreviewed effects.

These annotations describe the hook's control flow, not the callee's full write
contract. The source hash prevents silently reusing line reviews after edits.
"""
import csv,hashlib,json
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'
SOURCE=ROOT/'src/native_graphics/guest_shader_bridge.cpp'
source=SOURCE.read_text(encoding='utf-8-sig'); lines=source.splitlines()
manifest=list(csv.DictReader((OUT/'native-source-manifest.csv').open(encoding='utf-8-sig')))
expected=next(r['sha256'] for r in manifest if r['path'].replace('\\','/')=='src/native_graphics/guest_shader_bridge.cpp')
REVIEWED_HASH='655aeeeae801bfdcc9a1b0ef00a0c074448c87e5428d056961a63c86e8efeafd'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==expected==REVIEWED_HASH,'Re-review changed source before updating REVIEWED_HASH'
reviews={}
def group(numbers,category,observation,batch):
 for n in numbers:
  assert n not in reviews,n
  reviews[n]=(category,observation,batch)
group([3187,3193,4050,4076,4081],'audit oracle',
 'Call occurs inside visibility audit branch; native classification is computed separately. Audit output is compared with native result.','static selection')
group([2896,2896+6],'observational forwarding',
 'Both worker-registration audit branches invoke original; audit records registration inputs.','worker callback registration')
group([2919,2925],'audio observation',
 'Original audio call retained around output-page observations; not a graphics replacement hook.','shared producer dependency')
group([2957,3008,3013,3020,3028,3109,4492,4517],'engine producer forwarding',
 'Original engine/loader call executes with timing or publication around it; native consumer does not replace this call.','producer and scheduling')
group([3145,3280,3800,3818,3823,3834,3852,3863,3877,3887,3902,3918,4262],
 'scene producer/lifetime forwarding','Original runs before or after source, membership, tree, world or motion publication/retirement. Hook metadata is native; original object mutation remains.','scene producers and lifetime')
group([3157,3270,3289,3331,3344,3592,3931,4252],
 'selection/dispatch fallback','Original retained when native feature, queue, descriptor or group route is unavailable. See exact branch in source; this is not an audit-only call.','static selection and execution')
group([3220],'world animation producer',
 'Original world-animation call is unconditional; hook captures owner+356/+364 and publishes tree/animation around the call.','simulation/render separation')
group([3400,3540],'accepted native draw CPU handoff',
 'Indexed CPU tail still runs after accepted native draws or deferred geometry handoff; replacing draw submission did not remove this compatibility obligation.','static pass CPU state')
group([4119,4165],'unported object callback',
 'Gather fallback invokes original object dispatcher and refreshes live view/membership state afterward; source-observation hook also forwards original.','callback closure')
group([4268,4274],'model fallback',
 'Original model path used outside interpolation mode or for invalid/unsupported pose-vector range.','animated content')
group([4330],'interpolated model execution',
 'Even accepted pose interpolation installs a native pose context and invokes original model renderer.','animated content')
group([4344,4364],'pose upload forwarding',
 'Original upload runs first; native interpolation conditionally overwrites its shader scratch destination.','animated content')
group([4420],'scene-begin forwarding','Scene-begin hook invokes original after trace/interpolation setup.','view and pass boundaries')
group([4431,4444,4458],'camera producer forwarding',
 'All camera branches invoke original derived-matrix producer; interpolated branch temporarily installs pose and restores authoritative input afterward.','camera producer')
group([4586,4594,5280,5476,5505,5567,5752,5755,5768,5828,5852,5860,6863,6970,7020,7040,7066,7072],
 'retained extracted adapter','Named extracted helper is still called on selected native paths. Its generated body, calls and writes need a separate contract audit; name alone does not prove CPU-only behavior.','device/resource CPU contracts')
group([4589,4597,5568,5761,5770,5845,5963,6047,6060,6141,6158,6200,6216,6243,6296,6301,6307,6864,8918,10005],
 'native-host disabled fallback','Original call belongs to the non-native-host branch. Native-host path takes a different return or branch.','compatibility mode')
group([4601,6508,6513,6780,6787,7008,7021,7041,7056,7067,7073,7657,7698,7729,7897,8959],
 'shader-bridge disabled fallback','Original call belongs to shader-bridge disabled branch; enabled branch uses native/extracted path.','compatibility mode')
group([4610,5871,5895,6400,6451,6527,6563,6612,6630,6643,7089,7320,7621,7627,7662,7821,7828,7834,7839,7842,7848,7903,8927,8936],
 'resource producer/lifetime forwarding','Original remains alongside native registration, publication, invalidation or destruction bookkeeping. Do not count this as an unimplemented draw solely from original-call presence.','resource production and retirement')
group([4825,7855,7872,7879],'material activation disabled fallback',
 'Native material activation and shader-bridge flags select native implementation; otherwise original runs.','material compatibility mode')
group([5193],'instance execution forwarding','Original instance routine executes before native instance synchronization.','static instance state')
group([5255],'completion forwarding','Original fence call runs before optional native completion probe.','completion contract')
group([5477],'native-host disabled signal fallback','Original runs only with native host disabled: the earlier guard throws for any other native-host callback; recognized callback 0x8214EBA0 uses extracted CPU tail.','worker callback closure')
group([5636,5640],'platform synchronization import','Native submission explicitly acquires/releases kernel spin lock at device+10872; this is a platform import, not a CPU-tail extraction.','platform contract')
group([5654,6134,6348],'submission/wait fallback','Original retained in alternative branch of native submission or wait path; exact full guard must be included in runtime coverage.','submission and waits')
group([5901],'render-state producer forwarding','Original setter runs before native state publication.','render state production')
group([5985],'profiling-mode fallback','Original runs when host disabled OR profiling mode is not 6; mode6 is replaced.','profiling mode coverage')
group([6016],'profiling-control forwarding','Original control routine runs before native profiler invalidation.','profiling control')
group([6205],'native pacing bookkeeping','Native clock value is written to 0x8257C300 then original pacing bookkeeping runs even in native-host mode.','scheduling')
group([6410,6415,6420],'texture production forwarding','Texture loader stages invoke original with scoped timing.','texture production')
group([6859],'PIX idle fallback','Original runs if host disabled or RunNativePixIdle returns false; callback path requires closure.','profiling callback closure')
group([6877,6886,6898,6911,6922],'shared memory writer forwarding','Original memory writer executes within native write-tracking scope, with completion notification.','shared producer dependency')
group([6975],'unsupported resource-lock fallback','Original runs outside supported shader-bridge access codes10/12 or code14 at LR8213AE24.','resource lock coverage')
group([7058],'inline-index producer','Original executes inside write-frame tracking even with shader bridge enabled.','dynamic geometry production')
group([7180,7257,7263,7328,7449,7607,7636,7650],'view/target execution forwarding',
 'Original clear, target, scene setup/end or target scope runs alongside native rendering and publication.','view and pass boundaries')
group([7238],'viewport alternative path','Original viewport call remains outside early-return native replacement path; audit occurs afterward.','viewport coverage')
group([8914,10004],'draw CPU compatibility execution','Indexed/immediate native-host path still invokes extracted CPU helper even when draw unsupported or rejected.','draw CPU state')
group([8948,8968],'movie producer forwarding','Original texture lock/decode executes; native code captures plane metadata or publishes successful decoded result.','movie content production')
group([10013,10023,10033,10044,10055,10066,10078,10090,10109,10124,10139,10149],
 'ownership-predicate fallback','Original executes when exact NativeConstantOwnership predicate fails. Host enabled alone is not sufficient to bypass it.','CPU state and packet coverage')
group([10015,10025,10037,10059],'owned-path CPU extraction','After ownership predicate succeeds extracted helper executes; callee CPU writes and nested hooks remain to be audited.','CPU state contracts')

rows=list(csv.DictReader((OUT/'native-original-dependencies.csv').open(encoding='utf-8-sig')))
for row in rows:
 n=int(row['source'].rsplit(':',1)[1])
 assert row['callee'] in lines[n-1],f'stale locator {row}'
 review=reviews.get(n,('unreviewed','No source-reviewed classification yet.','pending'))
 row.update(zip(['path_class','observed_control_flow','work_batch'],review))
 row['callee_effect_contract']='pending; call-path review does not establish callee writes or complete reachability'
 row['reviewed_source_sha256']=expected
with (OUT/'native-dependency-classification.csv').open('w',newline='',encoding='utf-8') as stream:
 writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
summary=dict(sites=len(rows),reviewed=sum(r['path_class']!='unreviewed' for r in rows),
 unreviewed=[r['source'] for r in rows if r['path_class']=='unreviewed'],categories=dict(Counter(r['path_class'] for r in rows)))
(OUT/'dependency-classification-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
