"""Build a source-pinned planning ledger. No generated assignment closes a gap.

Run after refreshing tools/audit-renderer-inventory.py. The companion validator
checks coverage independently against the input rows and fingerprints.
"""
import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path
from renderer_task_playbooks import PLAYBOOKS
from renderer_task_packet import render_guide

ROOT = Path(__file__).resolve().parents[1]
INV = ROOT / 'out/renderer-inventory'
OUT = ROOT / 'out/renderer-coverage'
OUT.mkdir(exist_ok=True, parents=True)

def read(name):
    with (INV / name).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

TASKS = []
def task(id, title, deps, change, test):
    TASKS.append(dict(id=id, title=title, status='open', dependencies=deps.split(), change=change, completion_test=test))

task('R01.scope', 'Classify the exported closure', '',
     'For each assigned function/site, trace the seed-to-function path and its outgoing calls; label renderer consumer, producer, shared utility or unreachable for a named mode. Expand roots for newly found render targets. Scope labels need instruction/source evidence.',
     'Every census function and indirect site has an evidenced role or an explicit follow-up; no utility is declared renderer work solely from reachability, and exclusions name the mode and proof.')
task('R01.callbacks', 'Close frame receiver populations', 'R01.scope',
     'Resolve the eight 821A5080 sites and bucket slot 4 at 821A3C50 using registration/removal writers, receiver identity and verified vtables; check player/free/motion/base cameras and listeners.',
     'Registration-to-call table covers all eight sites plus bucket dispatch; runtime target traces map to it; unknown targets fail coverage and get a task.')
task('R01.modes', 'Declare supported and compatibility modes', 'R01.callbacks',
     'Inventory startup/loading/gameplay/pause/results/movie, player counts, camera modes, backend choices, ownership toggles and diagnostic branches. Decide support or explicit unsupported behavior for each combination.',
     'Mode matrix includes guards and fallback behavior; every supported branch has a scenario and every unsupported branch has a tested diagnostic.')
task('R02.publication', 'Separate simulation mutations from rendering', 'R01.callbacks R09.effects',
     'Split 820B4250 counter/time mutations and 821A4DE8 publication from render repeats. Publish camera, transforms, animation, effect clocks and resource generation together.',
     'Render a retained generation repeatedly: simulation counters, authoritative poses and resource lifetimes remain unchanged; one producer tick advances once.')
task('R02.camera', 'Own camera derivation and interpolation inputs', 'R02.publication',
     'Replace live camera inputs around 821CDDF8 and 821BE8D0 with retained per-view projection/view/FOV/viewport values; preserve matrix rounding and source-pose restoration.',
     'Locked/unlocked movement, zoom, camera changes and dimension edge cases match reference matrices; simultaneous views never share camera state accidentally.')
task('R03.selection', 'Finish immutable static selection', 'R02.publication R09.effects',
     'Publish hidden/mode flags, duplicate marks, LOD inputs, hierarchy and ordered membership together at 821C61D8/820B4038/821BEE68/821C3BB8; eliminate mixed live-generation selection.',
     'Visibility, LOD, duplicate/order, removal, reinsert and address-reuse fixtures match the reference and audited gameplay without live selection reads.')
task('R03.mutation', 'Resolve mutation and unknown-object fallback', 'R01.callbacks R03.selection',
     'Enumerate 821C0C00 callback implementations and mutation writers; specify republish/defer behavior for every unsupported or mid-selection mutation route.',
     'Adversarial callback/removal tests preserve order and retained lifetimes; fallback counts are attributed to a declared route and no silent live-list restart remains.')
task('R04.pass', 'Submit a complete retained static pass', 'R03.mutation R05.pass R09.effects R09.retirement',
     'Move required CPU setup/activation/indexed-tail effects at 821D96D8, 821B94E8 and 821B8E48 to explicit producer or native owners; submit geometry/material/world from retained inputs.',
     'Static pass succeeds with legacy setup, activation and draw submission disabled; mixed-content ordering and retirement remain correct on D3D11/D3D12.')
task('R05.pass', 'Own view, target and pass transitions', 'R02.camera R09.resources',
     'Specify begin/end, inherited target, clear, viewport, scissor, format, resolve and post-pass contracts, including 821BE8D0/821BE9D8 and tiled-to-untiled compatibility branches.',
     'Nested/offscreen/multiple-view tests verify explicit target and state restoration; loading direct-scene output and normal post output both present.')
task('R05.post', 'Validate post-processing and output transforms', 'R05.pass R09.assets',
     'Enumerate techniques that implement tone mapping, exposure/bloom-like passes, fog/environment and output gamma; document which are actually present, with exact pass identities.',
     'Named pass captures compare scene color and final output across authored presets, MSAA modes and 720p/1080p; no unnamed technique or rejected state remains.')
task('R06.pose', 'Replace model traversal and pose uploads', 'R02.publication R09.effects R09.resources',
     'Migrate 821C9478/821C9C20/821B2C28 and 821A1738/821A17D8 into retained rigid/skinned draw records with owned matrix arrays and upload dirty-state effects.',
     'Rigid and skinned fixtures, attachments, interpolation and LODs render with original model traversal disabled and unchanged authoritative bones.')
task('R06.families', 'Close model-family render routes', 'R01.callbacks R06.pose',
     'For each assigned renderable method, follow direct and indirect routes to submission, classify per-instance/attachment overrides and migrate the remaining native consumer; keep external class labels provisional.',
     'Soldier/civilian/enemy/large multipart/vehicle/attachment rows each have a targeted scene or evidenced unreachable disposition; no original render callback needed.')
task('R07.effects', 'Own dynamic effects and transparency', 'R02.publication R05.pass R09.dynamic',
     'For each particle, spark, smoke, projectile, muzzle, shell and glass method, retain geometry/material/lifetime/sort inputs and remove per-render simulation changes.',
     'Controlled firing, explosions, trails, overlapping alpha/additive effects and expiry match reference ordering; render repeats do not age effects.')
task('R07.environment', 'Own special world and destruction families', 'R03.selection R04.pass R09.dynamic',
     'Follow sky, wire, rock, tree, grass, broken-object and broken-piece routes; implement their topology, material and lifecycle deviations from the static group path.',
     'Outdoor/environment and destruction scenes exercise each family; broken/removing objects leave no stale retained draws or resource aliases.')
task('R07.shadows', 'Identify and migrate shadow passes', 'R01.callbacks R05.pass R06.pose',
     'Trace shadow-related effect techniques, render-target/depth writes and caster/receiver callbacks from source/assets; associate observed pass identities with static, animated and effect casters. Do not infer shadows from a dark patch.',
     'Document the actual shadow technique and target contract (or evidence of absence per mode), then compare static/moving caster and receiver occlusion in controlled captures.')
task('R07.special', 'Resolve base/no-op/test renderable methods', 'R01.scope R01.callbacks',
     'For shared 8252B718 and test/base-class rows, verify concrete dispatch and overrides; determine whether each family renders elsewhere, is producer-only, or is genuinely unreachable.',
     'Each class/table row has registration and slot evidence; no-op local body alone never closes a whole content family.')
task('R08.ui', 'Own HUD, XUI, fonts and overlays', 'R01.callbacks R05.pass R09.assets',
     'Migrate player overlay 820D3FD0, listener callbacks and UI composition to retained draw records; cover text, radar, menus, pause/results and SDK settings overlay.',
     'Startup/menu/gameplay/pause/results show correct order, text, clip/scissor and authored canvas at 720p/1080p and alternate aspect ratios; original UI callbacks disabled for consumer.')
task('R08.loading', 'Close loading and mission transitions', 'R05.pass R08.ui R09.retirement',
     'Specify no-active-output/direct-scene publication, concurrent loader submissions and menu-to-map/retry/exit handoff; avoid holding submission locks across pacing waits.',
     'Cold and warm load, retry, return to menu and another mission show changing loading frames with bounded queues; old-generation resources drain before reuse.')
task('R08.movie', 'Own movie composition and lifetime', 'R05.pass R09.resources',
     'Trace movie decode producers and retained original calls; retain YUV/RGB textures and conversion parameters until consumption, including skip and end-of-movie.',
     'Supported SD/HD movie paths play, skip and transition with correct color/topology and no stale frame or premature plane retirement.')
task('R08.present', 'Close host presentation and device lifecycle', 'R05.pass R09.sync R09.retirement',
     'Review host files outside native_graphics, 820B0B80 finish callbacks, surface ring ownership, resize/fullscreen/minimize/restore and device recreation policy.',
     'Resize and mode changes preserve frame order and resource completion; host tests pass and captured unique frame IDs match submissions with no unexplained repeats/drops.')
task('R09.effects', 'Classify all retained CPU effects', 'R01.scope',
     'For each assigned explicit/macro/adapter/store boundary, finish local effects if pending and trace transitive writes, readers, aliasing, exceptions and callbacks. Assign simulation, producer lifetime, native consumer or compatibility ownership.',
     'Every assigned source site has a complete effects/owner/ordering table with instruction evidence and differential tests; partial local review is not a completed status.')
task('R09.resources', 'Close resource creation and identity contracts', 'R09.effects',
     'Review texture/declaration/shader/VB/IB create/import/lock/unlock/destruction and physical/virtual alias writers, including ImportTexture function reference; retain versioned identity through frame completion.',
     'Create/failure/update/free/reallocate-same-address and alias tests reject stale handles and preserve in-flight frames, with no unreported guest writes.')
task('R09.retirement', 'Close retirement queue and allocator ownership', 'R09.effects',
     'Trace 82141440 queue growth/consumption, immediate fence tags, allocator providers, reference transitions and resource destruction. Preserve callback ordering and failure semantics.',
     'Queue exhaustion, allocation failure, same-resource rebind, delayed GPU completion and address reuse have explicit expected results and passing lifetime tests.')
task('R09.shader', 'Finish shader-default aliasing and failure parity', 'R09.retirement',
     'After the fixed retirement-before-default-read ordering, investigate payload aliasing during incremental constant writes, concurrent mutation and malformed count/extent behavior; either preserve reachable behavior or prove input preconditions.',
     'Differential fixtures cover pixel/vertex, null/no-default, payload self-alias and failure-after-partial-write; document excluded malformed streams with provider evidence.')
task('R09.dynamic', 'Own dynamic geometry and scratch buffers', 'R09.resources',
     'Enumerate immediate/nonindexed/quads, wire/trail/debris and transient ring producers; snapshot mutable bytes before render and retain through GPU consumption.',
     'Wraparound, append, concurrent publication, removal and large effect bursts never overwrite in-flight data; geometry contracts record all encountered formats.')
task('R09.sync', 'Close worker, signal and synchronization callbacks', 'R01.scope R09.effects',
     'Resolve worker registration modes, 8213C9F0 alternate signals, access10/12/14 and other lock callers, monitor/profiling callbacks, vblank/event delivery and SDK imports.',
     'Each callback population, activation guard and lock/event owner is evidenced; contention, failure and shutdown tests terminate without deadlock or lifetime races.')
task('R09.assets', 'Close shader/texture/declaration/render-state coverage', 'R01.modes',
     'Re-run disc effect/texture coverage, enumerate technique render-state combinations, merge runtime declaration catalogs and add offline replay for immediate/font/movie/XUI/utility contracts.',
     'Zero missing assets, rejected contracts and omitted records across the declared content corpus; catalog growth is reported, not treated as proof of exhaustion.')
task('R09.compat', 'Make compatibility and diagnostic routes explicit', 'R01.modes R09.effects',
     'For every disabled-bridge/host/activation/ownership fallback and diagnostic branch, retain an explicit supported owner or reject the mode; verify macros are forwarding, not replacements.',
     'Toggle matrix exercises all declared paths; no branch is marked native solely because a hook exists and no removed mode silently falls back.')
task('R10.trace', 'Make runtime route coverage attributable', 'R01.callbacks',
     'Add bounded per-scenario counters for callback instruction/target/receiver, content method, pass identity, backend and fallback reason; merge with static ledgers and report new routes.',
     'A scripted run produces per-scenario route IDs and new-target tasks; sampling/overflow and unexercised routes are explicit. Aggregate draw counts alone cannot pass.')
task('R10.capture', 'Capture complete frames in a timed burst', '',
     'Extend the one-shot native_backend_host GPU capture to a bounded timestamped burst with published sequence IDs; retain final composition and label partial scene captures separately. Hidden automated runs have no capturable main window through the existing window script.',
     'Before/during/after gameplay, pause and loading frames are captured with sequence and wall-clock metadata; final UI composition is present and no partial output is mislabeled complete.')
task('R10.input', 'Synchronize scenario controls with gameplay state', 'R10.trace',
     'Replace elapsed-time-only scenario assumptions with a reached-gameplay/camera-control marker; record intended and actual poll time and extend the movement/fire window after cutscene completion. The probe delivered the 180000 ms movement event at 183403 ms.',
     'Each control action has a reached-state precondition, observed input interval and visible/state effect; delayed polling and intro camera motion cannot produce a false movement pass.')
task('R10.scenarios', 'Run the declared content and lifecycle matrix', 'R10.trace R10.capture R10.input',
     'Exercise the scenario table with deterministic controls, timestamped screenshots, route counters and a matched reference. Include all map/environment and enemy/weapon families identified by the asset/mission census.',
     'Every scenario records reached/observed/compared status and artifacts; failures have tasks; no queued input or screenshot is mistaken for proof of its intended effect.')
task('R10.cadence', 'Integrate complete frames at independent cadence', 'R04.pass R06.families R07.effects R07.environment R07.shadows R08.ui R08.loading R08.movie R08.present',
     'Consume retained full-frame publications independently of the simulation clock, interpolate render inputs and retire generations by consumer completion.',
     '60 Hz producer / 120 Hz consumer produces measured unique frames with stable simulation over movement/combat/load and no original consumer helper invocation.')
task('R10.performance', 'Measure non-audit performance and backend parity', 'R10.cadence R10.scenarios',
     'Run D3D12 hardware and declared D3D11 fallback, worker counts 0/1/4, MSAA 1/2/4, native resolutions and frame credits; use a smaller pairwise matrix with rationale for combinations.',
     'Report CPU/GPU/frame-time distributions, unique image cadence, visual comparisons and memory peaks with audit instrumentation off; 120 Hz is measured rather than inferred from a cap.')

# Features are acceptance surfaces, not statements of complete implementation.
FEATURES = []
def feature(id, name, works, original, unknown, tasks, scenarios, evidence):
    FEATURES.append(dict(id=id, name=name, status='partial-or-unverified', works=works,
                         original=original, unknown=unknown, tasks=tasks.split(),
                         scenarios=scenarios.split(), evidence=evidence))

feature('F01','Static world geometry/material/world matrices','Retained geometry/material preload and published draw path exist','Setup/activation/indexed CPU tails remain','Complete pass ownership and unseen/mutating inputs','R03.selection R03.mutation R04.pass','S03 S06','src/native_graphics/guest_shader_bridge.cpp:3331; docs/native-scene-renderer-handoff.md')
feature('F02','Visibility, hierarchy, LOD, hidden and duplicate state','Native hierarchy/membership/LOD implementations exist','Unknown callback and live-selection fallbacks','Coherent generation for all flags and mutations','R03.selection R03.mutation','S03 S06','src/native_graphics/native_scene_visibility.h; native_scene_membership.h; native_scene_tree_publication.h')
feature('F03','Rigid models, skinning, animation and attachments','Native pose interpolation and GPU submission exist','821C9478/821C9C20/821B2C28 and matrix uploads','Complete model consumer independence','R06.pose R06.families','S03 S04 S08','out/renderer-inventory/content-boundary-contracts.csv')
feature('F04','Soldiers, civilians, insects, bosses and multipart enemies','47 method census identifies candidate families','Per-family original callbacks remain','Runtime receiver populations and per-family parity','R06.families','S03 S08','out/renderer-inventory/renderable-method-review.csv')
feature('F05','Vehicles, mounted weapons and vehicle cameras','Vehicle-family methods are exported','Vehicle/model/camera callbacks','Entry/exit, attachments and view differences','R06.families R02.camera','S08','renderable-method-review.csv:sub_8219A2D0,sub_821E2250,sub_821E5810')
feature('F06','Projectiles, particles, sparks, smoke and trails','Native draw infrastructure; method census','Effect-family producers/consumers not separated','Sorting, dynamic geometry and effect time ownership','R07.effects R09.dynamic','S04 S08','out/renderer-inventory/renderable-method-review.csv')
feature('F07','Muzzle flash, shells, transparent glass and additive effects','Distinct candidate methods exported','Original family callbacks','Blend/depth/order and expiry parity','R07.effects','S04 S06','renderable-method-review.csv:sub_821897A8,sub_8218A658,sub_8217D6E0')
feature('F08','Sky, terrain decorations, wires, trees and grass','Static and special-family paths identified','Special renderable methods','All environment and topology variants','R07.environment','S03 S06 S08','out/renderer-inventory/renderable-method-review.csv')
feature('F09','Destruction, debris, removal and respawn','Membership/resource invalidation infrastructure','Broken-object/piece callbacks and lifetime helpers','Retained removal, address reuse and debris variants','R07.environment R09.resources','S06 S07','renderable-method-review.csv:sub_8211FAA8,sub_82120168')
feature('F10','Shadows and caster/receiver passes','Generic target/depth/material support exists','No complete shadow consumer ownership demonstrated','Actual technique/receiver/caster population still needs identification','R07.shadows','S03 S08','docs/renderer-remaining-work.md:R07; src/native_graphics/guest_shader_bridge.cpp')
feature('F11','Camera, projection, zoom and interpolation','Published cameras and scoped interpolation','821CDDF8 derivation and 821BE8D0 forward','Other camera modes, multi-view and rounding parity','R02.camera','S03 S04 S09','out/renderer-inventory/small-retained-contracts.csv')
feature('F12','Single/multiple views, split-screen, free/motion cameras','Outer-frame camera candidates mapped','Eight indirect outer calls and bucket dispatch','Supported mode population; split-screen not validated','R01.callbacks R01.modes R05.pass','S09','docs/renderer-completion-inventory.md:Concrete callback and forwarding boundaries')
feature('F13','Targets, viewport, scissor, clear, resolve and offscreen passes','Native pass/target state exists','Begin/end and compatibility CPU effects','Nested/inherited state and recreation closure','R05.pass','S01 S02 S09 S10','out/renderer-inventory/pass-format-contracts.csv')
feature('F14','Post-processing, environment/fog, tone mapping and gamma','Native effect and output infrastructure','Guest-authored pass orchestration/constants','Technique census and matched reference output','R05.post','S03 S08 S10','src/native_graphics/effect.cpp; docs/native-renderer-migration.md')
feature('F15','HUD, radar, crosshair, fonts and in-game overlays','Native XUI/font/utility paths; prior city/HUD captures','820D3FD0 and listener callbacks','Full composition ownership and replay coverage','R08.ui R09.assets','S03 S04 S05','src/native_graphics/xui_effect.cpp; font_effect.cpp; content-boundary-contracts.csv')
feature('F16','Title, menus, pause, mission results and SDK settings','Native UI/backend paths','Menu/UI production and listeners','Every menu/results branch and scale/clip parity','R08.ui','S01 S05 S07 S10','docs/native-renderer-migration.md; README.md')
feature('F17','Loading, direct-scene output and concurrent loader','Native direct-scene publication path documented','Loader and end-frame CPU contracts','Cold/warm/retry/exit and queue bounds','R08.loading','S02 S07','docs/native-backend-migration.md:Loading-transition classification and longer validation')
feature('F18','Movies, conversion, skipping and movie-to-menu transition','Native movie effects and binding paths','Decode/texture production originals','SD/HD variants and plane lifetimes','R08.movie','S01 S11','src/native_graphics/movie_effect.cpp; native_movie_bindings.h')
feature('F19','Presentation, VSync/VRR, pacing and surface ring','D3D12 host and ordered native presentation','Finish callbacks and helper-dependent frame generation','Independent unique-image cadence','R08.present R10.cadence','S03 S10 S12','README.md; docs/native-backend-migration.md')
feature('F20','D3D12/D3D11 hardware and WARP/direct/recorded modes','Backend implementations and scene tests exist','Direct/compatibility branches remain','Per-mode gameplay parity and support policy','R01.modes R09.compat R10.performance','S12','src/native_graphics/guest_shader_bridge.cpp:210')
feature('F21','Geometry workers, frame credits and submission budgets','0/1/parallel workers and credits exposed','Worker callbacks and synchronization tails','Stress, cancellation and independent scheduling','R09.sync R10.performance','S07 S12','src/native_graphics/guest_shader_bridge.cpp:216')
feature('F22','Render size, aspect, MSAA and sampler quality','Native size/MSAA and sampler controls','Authored canvas and some inherited state','All accepted combinations and restart behavior','R05.post R08.ui R10.performance','S10 S12','src/native_graphics/guest_shader_bridge.cpp:157')
feature('F23','Texture, shader and declaration assets/contracts','Native import/reflection and coverage tools','Creation/import and setter callbacks','Catalog completeness and unsupported states','R09.assets R09.resources','S01 S08','docs/native-coverage.md')
feature('F24','Shader bindings/default streams and material state','Retirement/default ordering fix built with regression','Retirement allocator and CPU compatibility','Payload aliasing and malformed stream parity','R09.shader R09.effects','S03 S04 S13','src/native_graphics/native_shader_binding.h; tests/native_shader_binding_tests.cpp')
feature('F25','VB/IB/dynamic buffers, locks and physical aliases','Versioned model buffers and guest write tracking','Allocation/lock/unlock/free providers','All dynamic writers and failure semantics','R09.resources R09.dynamic','S04 S06 S13','src/native_graphics/native_model_buffers.h; docs/geometry-decoder-branch-status.md')
feature('F26','Retirement, allocator failure, destruction and reuse','Native retained generations and retirement hooks','82141440 and provider/queue callbacks','Queue consumption and cross-thread/in-flight reuse','R09.retirement','S07 S13','out/renderer-inventory/retirement-owner-provenance.csv')
feature('F27','Worker signals, events, locks, vblank and profiling','Native/extracted CPU routes','Alternate callbacks and SDK imports','Complete registration/monitor populations','R09.sync','S12 S13','out/renderer-inventory/retained-callback-review.csv')
feature('F28','Bridge-disabled, ownership fallback and audit modes','Explicit route classifications exist','Original fallback is intentional in some modes','Supported-mode policy and no hidden reentry','R09.compat','S12 S13','out/renderer-inventory/native-dependency-classification.csv')
feature('F29','Resize, fullscreen, minimize/restore and shutdown','Native host lifecycle implementation/tests exist','Host integration outside graphics census','Device-loss policy and completion on transitions','R08.present','S10 S13','src/native_graphics/native_backend_host.h; tests/native_host_lifetime_tests.cpp')
feature('F30','Independent frame scheduling and performance','Pacing infrastructure; prior sampled gameplay','Original content/frame helpers still required','Sustained 120 unique frames with stable simulation','R02.publication R10.cadence R10.performance','S12','docs/native-scene-renderer-handoff.md')
feature('F31','Base/shared no-op and debug/test object families','Shared method 8252B718 and test methods in census','Alternate render paths not excluded','Concrete dispatch and actual reachability','R07.special','S08 S09','out/renderer-inventory/renderable-method-review.csv')
feature('F32','All maps, weapons and runtime-only declaration combinations','Mission 1 fixture; disc coverage tooling','Unvisited render routes remain possible','Full content corpus and newly discovered paths','R09.assets R10.trace R10.scenarios','S08','docs/native-coverage.md:100')

# Runtime results are kept separate from semantic completion. Rebuilding the
# catalog preserves this reviewed execution record when it exists.

SCENARIOS = [
    ('S01','Cold startup, title, menu and story skip','Capture menu/movie route identities, UI text and final menu output','R08.ui R08.movie'),
    ('S02','Menu to Mission 1 loading','Observe direct-scene output, loading UI, progress and first gameplay frame','R08.loading'),
    ('S03','Stationary and moving city/player/NPC view','Capture before/after, camera/world change, static groups, model and HUD counters','R03.selection R06.families R02.camera'),
    ('S04','Aim, fire, change weapon, zoom and effect expiry','Verify ammo/projectile/effect changes, transparent order and camera motion','R07.effects R06.pose'),
    ('S05','Pause and resume','Capture pause composition, frozen simulation and resumed gameplay','R08.ui R02.publication'),
    ('S06','Destroy objects, approach/cull/LOD, remove and reinsert','Verify topology/visibility/lifecycle transitions with route identities','R03.mutation R07.environment'),
    ('S07','Retry, exit, reload and switch mission; results','Track generation drain, resource reuse, queue peak and loading output','R08.loading R09.retirement'),
    ('S08','Mission/content census: environments, enemy types, vehicles and weapon families','Choose missions from asset/script evidence; exercise every family, record untouched families','R06.families R07.effects R09.assets'),
    ('S09','Multiple views and player/free/motion cameras','First establish supported activation, then inspect view isolation and pass order','R01.modes R05.pass'),
    ('S10','720p/1080p, aspect, resize, fullscreen and minimize/restore','Inspect canvas/clip, targets, surface ring and recreation','R08.present R08.ui R05.post'),
    ('S11','SD/HD movie playback, skip and end','Inspect plane conversion and movie-to-menu resource transition','R08.movie'),
    ('S12','Backend/worker/MSAA/credit/VSync/cadence matrix','Pairwise supported settings; matched output and non-audit timing','R10.performance R09.compat'),
    ('S13','Resource/CPU contract failure, reuse and shutdown fixtures','Differential alias/queue/failure cases and in-flight GPU completion','R09.shader R09.resources R09.sync'),
]
SCENARIOS = [dict(id=i, name=n, completion_test=t, tasks=ts.split(), status='not-exercised-in-this-audit') for i,n,t,ts in SCENARIOS]
runtime_status_path=ROOT/'docs/renderer-coverage-scenario-results.json'
if runtime_status_path.exists():
    runtime_status=json.loads(runtime_status_path.read_text(encoding='utf-8'))
    for scenario in SCENARIOS:
        if scenario['id'] in runtime_status:
            scenario['execution']=runtime_status[scenario['id']]
            scenario['status']=runtime_status[scenario['id']]['status']

# Source work_batch is a reviewed control-flow classification, not a conclusion
# about callee effects. Preserve that evidence on every assignment.
BATCH = {
 'compatibility mode':'R09.compat','material compatibility mode':'R09.compat',
 'resource production and retirement':'R09.resources','texture production':'R09.resources',
 'device/resource CPU contracts':'R09.effects','CPU state and packet coverage':'R09.effects',
 'CPU state contracts':'R09.effects','render state production':'R09.effects',
 'scene producers and lifetime':'R02.publication','producer and scheduling':'R02.publication',
 'shared producer dependency':'R01.scope','simulation/render separation':'R02.publication',
 'camera producer':'R02.camera','static selection':'R03.selection',
 'static selection and execution':'R04.pass','static pass CPU state':'R04.pass',
 'static instance state':'R04.pass','view and pass boundaries':'R05.pass','viewport coverage':'R05.pass',
 'animated content':'R06.pose','movie content production':'R08.movie',
 'submission and waits':'R09.sync','worker callback registration':'R09.sync',
 'callback closure':'R01.callbacks','platform contract':'R09.sync','draw CPU state':'R09.effects',
 'completion contract':'R09.sync','worker callback closure':'R09.sync',
 'profiling mode coverage':'R09.sync','profiling control':'R09.sync','scheduling':'R09.sync',
 'profiling callback closure':'R09.sync','resource lock coverage':'R09.resources',
 'dynamic geometry production':'R09.dynamic',
}

def family_task(row):
    name=row.get('classes','')
    if row.get('method')=='sub_8252B718' or 'Test' in name: return 'R07.special'
    if row.get('method')=='sub_820B2670': return 'R04.pass'
    if any(s in name for s in ['ElectricWire','clRock','clSky','clTree','GrassMap','Broken']): return 'R07.environment'
    if any(s in name for s in ['Ammo','Particle','Spark','Smoke','Effect','Muzzle','Shell','Centry']): return 'R07.effects'
    return 'R06.families'

SOURCES = {
 'complete-function-inventory.csv':'R01.scope',
 'native-boundary-routes.csv':'R09.effects',
 'retained-call-effect-reviews.csv':None,
 'macro-original-dependencies.csv':None,
 'native-original-references.csv':'R09.resources',
 'native-reentry-sites.csv':'R09.effects',
 'indirect-site-ledger.csv':'R01.scope',
 'renderable-method-review.csv':None,
 'renderable-content-routes.csv':None,
 'retained-adapter-terminal-edges.csv':'R09.effects',
 'retained-callback-review.csv':'R09.sync',
 'core-callback-candidates.csv':'R01.callbacks',
 'state-write-ledger.csv':'R09.effects',
 'adapter-nonscalar-operations.csv':'R09.effects',
 'ghidra-unresolved-direct-targets.csv':'R01.scope',
 'decompiler-quality.csv':'R01.scope',
}
# Import all other explicit remaining-obligation tables. Instruction dumps are
# witnesses to these tasks, not tens of thousands of invented implementation tasks.
for path in sorted(INV.glob('*.csv')):
    rows=read(path.name)
    if rows and 'remaining' in rows[0] and path.name not in SOURCES:
        SOURCES[path.name]='R09.effects'

ASSIGNMENTS=[]
MANIFEST=[]
for name, default in SOURCES.items():
    rows=read(name)
    MANIFEST.append(dict(path='out/renderer-inventory/'+name, sha256=sha(INV/name), rows=len(rows)))
    for index,row in enumerate(rows,2):
        tid=default
        if name=='retained-call-effect-reviews.csv': tid=BATCH[row['work_batch']]
        elif name=='macro-original-dependencies.csv':
            tid={'EDF_MAP_TIMED_HOOK':'R08.loading','EDF_TREE_MUTATION':'R03.mutation',
                 'EDF_RENDER_STATE_SETTER':'R09.effects','EDF_RENDER_PHASE':'R01.callbacks'}[row['macro']]
        elif name in ('renderable-method-review.csv','renderable-content-routes.csv'): tid=family_task(row)
        if name=='native-reentry-sites.csv' and row.get('kind')=='indirect resolver call': tid='R01.callbacks'
        locator=row.get('source') or row.get('site') or row.get('address') or row.get('function') or row.get('method') or row.get('target') or f'row {index}'
        subject=' / '.join(str(row[k]) for k in ['function','hook','callee','method','address','site','classes'] if row.get(k)) or str(locator)
        if name=='indirect-site-ledger.csv':
            action=f"At {row['function']}:{row['address']}, resolve {row['classification']} from {row.get('load_chain') or 'register provenance'}; trace registration/removal and mode guards. First establish renderer relevance; add new target roots."
            test='Receiver/target/guard table backed by instruction and registration evidence, plus observed target comparison; otherwise retain an explicitly bounded unresolved follow-up.'
        elif name=='complete-function-inventory.csv':
            action=f"Slice seed reachability and outgoing edges for {row['function']}; classify renderer/producer/utility role using {row.get('root_reasons') or 'incoming closure edges'}."
            test='Evidenced scope decision with mode and path; renderer-facing unresolved callees are linked to explicit boundary tasks.'
        elif name=='state-write-ledger.csv':
            action=f"For {subject}, resolve destination aliases and readers of {row.get('instruction','')}; distinguish stack-only, producer mutation, lifetime or consumer state."
            test='Exact write has an owner/ordering or evidenced stack-only disposition; callback/alias escape and non-stack consumers addressed.'
        else:
            action=f"Review {subject} at {locator}: {row.get('remaining') or row.get('callee_effect_contract') or row.get('contract') or row.get('status') or row.get('review') or 'close branch activation, transitive effects and owner'}"
            test='Source-pinned local/transitive contract assigns every effect and callback an owner; branch-specific differential test or justified non-render/producer-only disposition attached.'
        ASSIGNMENTS.append(dict(id=f"B{len(ASSIGNMENTS)+1:06d}", source=name, row=index,
            locator=str(locator), subject=subject, task=tid, status='open',
            action=action, completion_test=test,
            evidence_status=row.get('joined_callee_effect_status') or row.get('resolution') or row.get('review') or row.get('status') or 'source row; assignment does not establish completion'))

def write_csv(name, rows):
    with (OUT/name).open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

write_csv('boundary-tasks.csv',ASSIGNMENTS)
mode_rows=[]
mode_sources=[]
for path in sorted((ROOT/'src').rglob('*')):
    if path.suffix not in ('.h','.cpp'): continue
    body=path.read_text(encoding='utf-8-sig')
    found=list(re.finditer(r'REXCVAR_DEFINE_\w+\s*\(\s*(edf_native_\w+|edf_fps_cap)\s*,\s*([^,\n]+)',body))
    if not found: continue
    mode_sources.append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path)))
    for m in found:
        name=m[1]
        mode_rows.append(dict(name=name,default=m[2].strip(),source=path.relative_to(ROOT).as_posix()+':'+str(body.count('\n',0,m.start())+1),
                              task='R09.compat' if any(x in name for x in ('audit','trace','capture','probe')) else 'R01.modes',
                              status='open',completion_test='Enumerate accepted values and guards from this definition and consumers; assign support policy and scenario/fixture for each branch. Default alone is not coverage.'))
write_csv('configuration-modes.csv',mode_rows)
if set(PLAYBOOKS)!={t['id'] for t in TASKS}:
    raise ValueError('Task/playbook IDs differ; every task needs exactly one authored playbook.')
for item in TASKS:
    item['execution']=PLAYBOOKS[item['id']]
catalog=dict(version=2, scope='6e9c94b+worktree-2026-09-22-coverage', tasks=TASKS,
             features=FEATURES, scenarios=SCENARIOS, sources=MANIFEST,
             configuration_modes=mode_rows,configuration_sources=mode_sources,
             limitation='Planning assignments cover the declared exported census, not all possible runtime paths. All generated boundary dispositions remain open; no implementation percentage is inferred.')
(ROOT/'docs/renderer-coverage.json').write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
(ROOT/'docs/renderer-task-guide.md').write_text(render_guide(TASKS),encoding='utf-8')
counts=Counter(a['task'] for a in ASSIGNMENTS)
lines=['# Renderer coverage audit','',
 '2026-09-22, `6e9c94b` plus the existing renderer worktree. This supersedes the provisional coverage checklist in `renderer-remaining-work.md`; R01–R10 remain package identifiers.','',
 '**Assessment:** native GPU submission and substantial retained static-world infrastructure exist. Complete independent frame generation is unfinished. Original model, effect/UI orchestration, compatibility CPU work and callback populations remain. No full renderer feature is marked complete by this audit.','',
 'This is a finite, source-pinned planning scope: known features/modes, every exported boundary, all 47 renderable methods, the broad function/indirect census and every exported store candidate. Broad closure includes shared utilities. A scope investigation is not a missing-port assertion. External class names remain provisional. New runtime routes must extend this ledger.','',
 'Machine-readable catalog: [renderer-coverage.json](renderer-coverage.json). Exact boundary investigations: `out/renderer-coverage/boundary-tasks.csv`, regenerated by `python tools/build-renderer-coverage.py`. Each row includes its source row, locator, specific investigation, task dependencies through its parent, and completion test. Raw store candidates are grouped under R09.effects rather than represented as implementation requests.','',
 'Step-by-step execution instructions for every task: [renderer-task-guide.md](renderer-task-guide.md). Export a self-contained packet with `python tools/show-renderer-task.py R10.capture --output out/R10.capture-task.md`. These instructions preserve existing scope, dependencies and open statuses.','',
 '## Feature and mode disposition','',
 '| ID | Feature/mode | Existing capability | Original dependency | Unknown/gap | Tasks | Scenarios |',
 '|---|---|---|---|---|---|---|']
for f in FEATURES:
    lines.append('| '+' | '.join([f['id'],f['name'],f['works'],f['original'],f['unknown'],', '.join(f['tasks']),', '.join(f['scenarios'])])+' |')
lines += ['', '## Renderable-family coverage', '',
          'All 47 method rows are assigned below. Names are external class-label evidence; receiver registration and runtime reachability remain tasks. Every listed class inherits the method task, including shared no-op/base classes.', '',
          '| Method | Candidate classes | Task |', '|---|---|---|']
for r in read('renderable-method-review.csv'):
    lines.append('| '+r['method']+' | '+r['classes'].replace(' | ',', ')+' | '+family_task(r)+' |')
lines += ['', '## Configuration-mode census', '',
          f'{len(mode_rows)} renderer/cadence setting definitions are source-pinned in `out/renderer-coverage/configuration-modes.csv` and the JSON catalog. Each has its literal default, exact definition locator, support-policy task and completion test. This includes diagnostic/ownership toggles, not only backend names. Accepted ranges and combinations remain R01.modes/R09.compat work; this audit does not certify every possible combination.', '',
          'Declared backend choices in the bridge: D3D12, D3D12-WARP, D3D11, D3D11-WARP and empty/no backend. Recorded/direct submission, workers 0/1/2–32, frame credits 1/2, MSAA default/1/2/4, locked/unlocked cadence, VSync, native render size, aspect/canvas, and capture/audit/ownership branches require explicit mode dispositions. Some settings are restart-only. Unsupported combinations must be rejected visibly, not counted as tested.']
lines += ['', '## Concrete tasks', '',
 'All tasks below are open. Dependencies are completion dependencies; bounded investigation can start earlier. Per-boundary rows inherit their parent dependencies and retain the more specific source question. Local effect review, successful native draw submission, and scenario execution each prove different things.', '',
 '| Task | Change or bounded investigation | Depends on | Completion test |', '|---|---|---|---|']
for t in TASKS:
    task_link='['+t['id']+'](renderer-task-guide.md#'+t['id'].replace('.','-').lower()+')'
    lines.append('| '+' | '.join([task_link+' — '+t['title'],t['change'],', '.join(t['dependencies']) or 'None; source census entry point',t['completion_test']])+' |')
lines += ['', '## Scenario acceptance matrix', '',
 'The runtime report records actual execution separately. A planned input is not a reached scenario; a reached scenario is not visual equivalence. Tests do not close unrelated content families.', '',
 '| ID | Scenario | Required evidence | Gap owners |', '|---|---|---|---|']
for s in SCENARIOS:
    lines.append('| '+' | '.join([s['id'],s['name'],s['completion_test'],', '.join(s['tasks'])])+' |')
lines += ['', '## Coverage verification', '',
 'Run `python tools/audit-renderer-coverage.py`. It checks exact source-row coverage, fingerprints, identifiers, task/scenario references, completion fields and dependency cycles, and requires all boundary dispositions to remain open. Run the existing `python tools/audit-renderer-inventory.py` as the upstream source-freshness/structural gate. Neither passing audit establishes semantic completion.', '',
 '| Source | Rows assigned |', '|---|---:|']
for s in MANIFEST: lines.append(f"| `{Path(s['path']).name}` | {s['rows']} |")
lines += ['', 'Priority: R01.callbacks/R09.effects unblock R02–R05 complete static ownership; pursue resource and shader-contract gaps alongside it. Then complete model/effect/shadow/UI families before R10.cadence. R10.trace is the immediate acceptance-instrumentation gap: current logs do not attribute every callback/family to a scenario.', '',
 'Runtime findings and limitations: [renderer-coverage-runtime.md](renderer-coverage-runtime.md). Existing baseline evidence remains historical and is not silently promoted to current validation.']
(ROOT/'docs/renderer-coverage-audit.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(json.dumps(dict(features=len(FEATURES),tasks=len(TASKS),scenarios=len(SCENARIOS),boundary_rows=len(ASSIGNMENTS),sources=len(MANIFEST),assignments_by_task=counts),indent=2))
