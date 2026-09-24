# The in-game console

A Quake-style drop-down console for EDF2027: type commands to spawn enemies, collapse
buildings, set off explosions, read performance numbers and change any setting, and
run the same commands from script files, so a stress case can be written down once and
replayed unattended against any build.

- Source: `src/console/` (the command language in `console_logic.h`, the executor in
  `console.cpp`, the game commands in `console_game.cpp`, the window in
  `console_dialog.h`).
- Tests: `tests/console_tests.cpp` (ctest `edf_console_tests`) and
  `tests/test_renderer_scenarios.py` (the stress scenarios' scripts).

## Opening it

**`` ` ``** (the key left of 1, `bind_edf_console`) opens and closes the console.
**Escape** also closes it; it never quits the game while the console is open.

The ReXGlue SDK also has a console on `` ` `` (`bind_console`): a log viewer that can set
cvars. EDF2027 moves that one to **F9** at startup when it is still on `` ` ``, so each
key opens one window. **F4** (the SDK's advanced settings) and **F3** (its debug
overlay) are unchanged. To put the SDK console back, set `bind_console` and
`bind_edf_console` to different keys.

While the console is open, keyboard, mouse and controller input do not reach the game,
exactly as with the F1 menu: the menu input gate (`pause_menu.h`) closes, the mouse is
released and the cursor shown. After it closes each source is withheld until it has
been idle, so the key that closed it does not act in the game. The console and the F1
menu never own input at the same time: F1 closes the console first, and the console
key does nothing while the menu is open.

The game keeps running while the console is open. With `edf_console_pause true` the
simulation holds instead, as under the F1 menu; typed game commands then wait and run
at the first simulation step after the console closes.

| key | |
|---|---|
| Enter | run the line |
| Tab | complete the command, cvar or argument; with several candidates, list them |
| Up / Down | history |
| PageUp / PageDown | scroll the output |
| Ctrl+L | clear the output |

The header shows the simulation tick, whether a mission is running, how many commands
are queued, and a CHEATS USED badge once a game-changing command has run.

## The language

```
line     := command ( ';' command )*
command  := name argument*          names are case-insensitive
argument := word | "quoted text"    \" and \\ escape inside quotes
```

`//` or `#` at the start of a word ends the line. A name is looked up as a command, then
as an alias, then as a cvar: `edf_fps_cap` prints a cvar, `edf_fps_cap 144` sets it.
Every argument is checked before anything runs (type, range, known names), so a typo
prints an error and changes nothing.

Durations are simulation ticks (60 per second): `600`, `600t`, `10s`, `1.5s` or
`500ms`. A fraction of a tick rounds up, so a wait never ends early.

## Commands

Game commands (marked *engine*) run on the engine thread at the start of the next
simulation step, never on the UI thread; typed ones are queued and run in order.
*mission* commands refuse to run without a mission in progress (a live player object
and no loading screen). *cheat* commands change the game and mark the run (below).

### General

| command | |
|---|---|
| `help [command]` | list the commands, or describe one (or a cvar) |
| `echo <text...>` | print text to the console and the log |
| `mark <label...>` | write `Console: mark '<label>' tick=N script_tick=M` to the log |
| `exec <file>` | run a script (see Scripts) |
| `wait <duration>` | pause this script |
| `waitmission [timeout] [settle]` | pause this script until a mission is running, then `settle` more (default 2 s) |
| `at <time> <command...>` | run a command at a time on the scripted-pad game clock |
| `alias [name] [line...]` | list aliases, show one, or define one |
| `unalias <name>` | remove an alias |
| `set <cvar> <value...>` / `get <cvar>` / `reset <cvar>` | cvars (also `name` / `name value`) |
| `cvarlist [filter]` | list cvars whose name contains the filter |
| `runners` / `stop` | list / stop running scripts and pending `at` commands |
| `cheats` | say whether this run used cheat commands |
| `console <open\|close\|toggle>` | show or hide the console window (for scripted screenshots) |
| `clear` | clear the output |
| `quit` | close the game |

### Game

| command | flags | |
|---|---|---|
| `spawn <type\|list> [count] [distance \| x y z]` | engine, cheat | create enemies (below) |
| `killall [type\|class\|all\|spawned] [remove]` | engine, mission, cheat | kill invaders |
| `god [on\|off]` | engine, cheat | player invulnerable, kept at full health |
| `ammo [on\|off\|refill]` | engine, cheat | magazines kept full, or refilled once |
| `pos` | engine, mission | player position, view heading, health |
| `teleport <x> <y> <z>` | engine, mission, cheat | move the player |
| `destroy <radius R \| all \| nearest [n] \| R> [max]` | engine, mission, cheat | collapse buildings |
| `effects <count> [radius] [scale] [per_tick]` | engine, mission, cheat | explosion effects |
| `timescale [scale]` | cheat when not 1 | simulation speed, 0.1 to 4 |
| `stats [all]` | engine | performance and object counts |

**spawn.** `spawn list` prints the types. With a count and a distance (default 1 and 30)
the objects stand on a ring of that radius around the player, the first straight ahead
(in the camera's direction), further rings outwards when one is full; with `x y z` they
ring that point. Ground types are dropped onto whatever is below them (the scatter
natives' sweep, from 1000 units up: a spot inside a building puts them on its roof);
UFOs appear 30 units and carriers 80 units above the player's height. Each faces the
player, belongs to the invaders (civilians to faction 3) and starts awake, so it attacks
at once. Large spawns are spread over ticks, `edf_console_spawn_per_tick` (default 20)
per tick. The first spawn of a type the mission has not loaded reads its files from
disc on the engine thread, a visible hitch.

| type | SGO | what (durability) |
|---|---|---|
| `ant` | GiantAnt01 | black giant ant (60) |
| `ant015` | GiantAnt015 | black giant ant, Mission 1's variant (60) |
| `redant` | GiantAnt02 | red giant ant (180) |
| `queen` | GiantAntQueen | giant ant queen (2000) |
| `spider` | GiantSpider01 | giant spider (45) |
| `spiderlord` | GiantSpiderLord | giant spider lord (2000) |
| `ufo`, `ufo2`, `ufo3` | UfoSmall01/02/03 | small UFO gunships (50, 200, 200) |
| `carrier` | UfoCarrier01 | UFO carrier / dropship (600) |
| `hector`, `hectorkk`, `hectormc`, `hectormm`, `hectormo`, `hectoroc`, `hectoroo`, `hectorrr` | AlienTankCC/KK/MC/MM/MO/OC/OO/RR | Hector walking robots (300) |
| `bighector`, `bighectormm`, `bighectorrr` | AlienTankBCC/BMM/BRR | large Hectors (1200) |
| `anthill` | AntHill01 | ant hill (800) |
| `monster` | Monster01 | Solomon, the kaiju (8000) |
| `mech` | MonsterMech01 | mechanical kaiju (15000) |
| `walker` | Alien4LegTank01 | four-legged walker boss (5000) |
| `civilian`, `civilianf` | PeopleM01, PeopleF01 | civilians (30) |

EDF 2017 has no golden ant: the disc's ant definitions are GiantAnt01, GiantAnt015,
GiantAnt02 and the queen. The mothership (UfoMother01), EDF soldiers (they need a
weapon from the script that creates them) and the vehicles are not offered.

**killall** kills every live object of faction 1 (the invaders) through the game's own
damage function, so each dies as it would to a weapon (death animation, BASEDTRY, the
mission's counters). `killall spider` or `killall clGiantSpider` limits it to one class
(`ant`, `redant` and `queen` are all `clGiantAnt`). `killall spawned` kills only what
the console created. `remove` deletes the objects instead (no death). Killing a
mission's own enemies advances it: in Mission 1, killing every wave ends the mission.

**destroy** collapses buildings and other map objects around the player through their
own death branch: debris, the fall model, the collapse sound. `destroy 100` or
`destroy radius 100`, `destroy all` (the whole map), `destroy nearest 3`; a last number
caps how many go at once, nearest first.

**effects** sets off the game's ordinary explosion effect (smoke and fire, no damage) at
random points within `radius` (default 15) of the player, `scale` (default 3), and
`per_tick` of them per tick (default all at once).

**timescale** sets how many simulation ticks the engine runs per second (60 × scale).
The simulation is frame-locked (a tick is a fixed step), so this is the game's speed:
0.5 is half speed, 2 is double. It rebases the engine's pacing clock, so nothing jumps.
Audio is not stretched.

**stats** prints the frame rate and frame-time percentiles of the last 5 s window, the
simulation step rate and the step dispatcher's time, the effect objects the native
renderer walked and drew in the last frame, the live objects per faction, and objects
per class over the faction lists and the map grid (the 16 largest; `stats all` for
every class), and whether cheats were used. Per-pass GPU times and draw counts come from `edf_native_gpu_timings`
and `edf_native_frame_times` (see Stress scenarios).

## Scripts

A script is a text file of commands, one line each (a line may hold several, split by
`;`). Scripts run on the engine thread, in order; `wait` pauses one for a number of
ticks without holding anything else up. Several scripts can run at once, each at its
own pace.

```
// stress-ants.cfg
waitmission 900s 5s     // until the player exists, then 5 s more
god on
mark baseline
stats
wait 20s
spawn ant 300 40
wait 30s
stats
killall
```

- `exec file` from the console starts the file as its own script; from inside a
  script it runs the file in place, so the caller continues after it.
- `--edf_exec=<file>` runs a script at startup; `--edf_exec_mission=<file>` runs one
  each time a mission starts. Relative names are tried against the working directory,
  the executable's folder and its `console/` folder, each also with `.cfg` added.
- `waitmission` is how a startup script waits for gameplay, independently of how long
  menus and loading take.
- An alias used in a script expands in place, so a `wait` inside it paces the script.

### The game clock

Every command is logged with the simulation tick it ran on:

```
Console: tick=5471 script_tick=5470 thread=engine src=edf_exec:verify-types.cfg > spawn ant 3 25
```

`tick` is the engine heartbeat's count (`SimulationTicks()`); `script_tick` is the
scripted pad's game clock: ticks since the pad's first poll, the clock a `clock game`
input script (`EDF_INPUT_SCRIPT`) measures its milliseconds on (ms = tick × 1000 / 60).
`at` schedules on that clock, so console commands and pad input share one timeline:

```
at 600 spawn ant 200 40        // 10 s after the pad's first poll
at 15000ms destroy radius 100  // the time an input script would write as 15000
```

Without a scripted pad, `script_tick` equals `tick`. Mission timing in the game itself
is not on either clock's absolute value (loading screens vary), so for gameplay prefer
`waitmission` then `wait`.

## Stress scenarios

`tools/renderer-scenarios.json` has four scenarios driven by console scripts
(`tools/console/stress-*.cfg`) on Mission 1 from a fresh profile, the intro skipped and
the player standing still (`tools/native-scenario-stress-input.txt`):

| scenario | script |
|---|---|
| `stress-ants` | 300 ants in rings from 40 units, 40 s of swarming under god mode, then `killall` |
| `stress-collapse` | every building within 150 units collapses, then every building on the map |
| `stress-effects` | 20 waves of 240 explosions within 30 units, one every 2 s |
| `stress-mixed` | 150 ants, 60 spiders, 20 UFOs, buildings within 120 collapsing, 720 explosions |

A scenario may name a console script (`"console"`, passed as `--edf_exec`) and its own
flags (`"args"`, replacing the variant's flag of the same name); the stress scenarios add
`--edf_native_hook_timings=true` and `--edf_fps_cap=0` to the variant's frame times and
GPU timings. Run one and read it with the usual reports:

```
powershell -File tools/run-renderer-scenario.ps1 -Scenario stress-ants -Variant unlocked -Executable out/build/win-amd64-release/edf2027.exe
python tools/frame-time-report.py <log>
python tools/renderer-runtime-gate.py <log> --baseline <baseline game.log> --expect-mission M202
```

With hook timings on, a run writes more than the log's 5 MB rotation size, so the
console lines of the early phases may be in `game.1.log` beside `game.log`
(`log_max_file_size_mb`). Each script brackets its phases with `mark` lines (`mark stress-ants swarm-begin`) and
prints `stats` in each, so the log says when the load was applied and what the game
held at the time. Run the same scenario against two builds for before/after numbers;
nothing in the scenario depends on the build.

First runs (variant `unlocked`, uncapped, 1280x720, Core i7-14700 / RTX 4070 Ti SUPER,
build 5de8cba; gameplay frame times from `frame-time-report.py`, the rest from the
scripts' `stats`):

| scenario | load | gameplay p50 / p99 / max (ms) | during the load |
|---|---|---|---|
| (baseline, all four) | Mission 1 street, 44 ants | | 224 FPS, step dispatch 2.6 ms |
| stress-ants | 300 ants spawned, 336 live | 12.3 / 18.0 / 37.0 | 50-73 FPS; step dispatch 11-19 ms (CPU-bound in the simulation) |
| stress-collapse | 657 map objects within 150 (54 buildings), then 10,688 (1,243 buildings) at once | 2.5 / 12.0 / 681.5 | 81 FPS after the near collapse; the whole-map collapse is one 0.7 s frame, then 69 ms 1% lows |
| stress-effects | 20 waves of 240 explosions | 4.5 / 9.5 / 26.3 | 120-130 FPS; ~190 effect objects walked, ~130 drawn |
| stress-mixed | 150 ants, 60 spiders, 20 UFOs, 400 map objects, 720 explosions | 8.5 / 30.3 / 311.3 | 21 FPS at the peak (step dispatch 48 ms), then 70-74 FPS |

To add one: write `tools/console/<name>.cfg` (start with `waitmission`, bracket phases
with `mark`, add `stats`), add a scenario entry with `"console"` pointing at it, and run
`python -m unittest tests.test_renderer_scenarios` (it checks every console script
parses, waits for the mission before its first game command, and marks its phases).

## Adding a command

Commands live in one registry. Add an entry where the others are registered
(`Service::RegisterBuiltins` in `console.cpp`, or `RegisterGameCommands` in
`console_game.cpp`):

```cpp
r.Add({.name = "heal", .usage = "heal [amount]", .help = "Restore the player's health",
       .args = {{.name = "amount", .type = ArgType::Float, .optional = true, .min = 0, .max = 100000}},
       .flags = kEngine | kMission | kCheat,
       .handler = [](Invocation& in) {
         auto& ctx = *static_cast<ExecContext*>(in.host());   // engine: ctx.frame is set
         const GuestMemory m(ctx.frame->base);
         ...
         in.Ok("healed");
       }});
```

- `args` describes each argument (`Int`, `Float`, `Bool`, `Duration`, `Choice`,
  `String`; optional; range; `choices` for validation and Tab). `validate` adds checks
  across arguments; `max_args` allows a variadic tail. Invalid input never reaches the
  handler.
- `kEngine` runs the handler on the engine thread with `ctx.frame` set (the guest
  context and memory); without it the handler may run on the UI thread and must not
  touch game state. `kMission` refuses without a mission; `kCheat` marks the run.
- Guest code is called through `GuestCalls` (`console_guest.h`): scratch memory on the
  guest stack, a fresh context per call, pointers range-checked before they are
  followed. Work spread over ticks goes through `AddJob` (see `Spawn`), per-tick upkeep
  through `AddEngineTick` (see `god`).
- Add the name to `CONSOLE_COMMANDS` in `tests/test_renderer_scenarios.py` if scripts
  may use it, and a line to this file.

## Cheats and logging

Game-changing commands (`spawn`, `killall`, `god`, `ammo`, `teleport`, `destroy`,
`effects`, a `timescale` other than 1) are allowed in single player. The first one logs
`Console: CHEATS USED from simulation tick N (first: <command>)`, the console header
shows a badge, and `stats` and `cheats` say so; a run's log therefore tells whether its
numbers came from an unmodified game. Every command and its output is written to the log
with its tick (`edf_console_log`, on by default).

The console must not take the game down on bad input: arguments are validated before
anything runs, game commands refuse outside a mission and re-check the mission on every
tick of spread-out work (a spawn stops if the mission ends), guest pointers are checked
against the heap and the image before use, list walks are bounded, and a handler's
exception is printed rather than propagated into guest code.

## How it works (the game side)

All addresses are the retail executable's; the evidence is in the analysis workspace
(`D:/roms2/edf2027-analysis`), the recompiled code and the disc's own debug console.

**The disc's debug console.** The retail build still registers the developers' debug
console: `sub_820C02B0` adds 51 commands (`object`, `objectgenerate`, `objectteam`,
`vanish`, `armor`, `player`, `mission`, ...) and the disc ships 56 `.bat` scripts for it
(`Bat/`). Its input side is unreachable, but its handlers show how the engine is meant
to be driven: `object` (`sub_820BE738`) creates an object 5 units in front of the first
object of the core's list (the player camera), `vanish` (`sub_820BE088`) finds map
objects around a point, `objectteam` (`sub_820BDEE8`) sets a faction.

**Spawning** follows mission-script native #300 (`sub_820CEBD0`, the one Mission 1 uses
for its ants), with the marker lookup replaced by a computed transform:

1. `L"game:\Object\<name>.Sgo"` as a guest `std::wstring` (assign `sub_820A0D10`,
   destroy `sub_820A0858`).
2. `clResourceManager::MakeObjectCache([0x82578648], path, 1)` (`sub_820AFAC0`): loads
   the SGO and its model/texture dependencies once; later calls return at once.
3. `clCore::CreateObject([0x8257C030], &matrix, path, &params)` (`sub_821A6E08`):
   matrix rows right, up, forward, position; params a 4×4 identity and zeros.
4. `__RTDynamicCast(obj, 0, Sgs::clObject_Base, clGameObject_Base)` (`sub_821E9348`,
   type descriptors `0x82550D14`, `0x82551058`), then scale (`sub_820D5430`, 1.0),
   faction (`sub_820D5ED0`, 1 = invaders, notify) and awaken (`sub_820D4A00`, the
   script's "start awake" argument).

The ground height is the scatter natives' (#251 `sub_820CE100`, #301, #302) sweep:
`sub_821AD100([0x82578678], 0, 0, &start, &move, 0, 0x821A3C78, &result)` from 1000
units above by (0, −10000, 0); the hit position is `result+16`.

**Objects.** The faction manager `[0x82578720]` holds five 40-byte records; record+0
heads an intrusive list of the objects' +608 nodes (`sub_820D6D68` inserts), so
`object = node − 608`. Faction 0 is the EDF and the player, 1 the invaders, 3 civilians.
Game-object fields: +272 position, +592/+596 health and maximum, +600 damage scale,
+604 faction, +624 dead, +625 invulnerable. The player is the `clPlayerObject` on
faction 0's list. Class names come from RTTI: vtable−4 → complete-object locator,
+12 → type descriptor, +8 → `.?AV<name>@@`. The core's two lists (`[core+0..+12)` and
`[core+44..+56)`, node+0 next, node+8 object, walked by the step dispatcher
`sub_821A4BA0`) hold only the top-level managers (clPlayerCamera, clGameObject_Manager,
clMapObjectManager, clEffectObjectManager, ...), so `stats` counts objects from the
faction lists and the map grid. Core bytes +2260..+2262 are nonzero while loading.

**Killing** calls `clGameObject_Base::ApplyDamage(obj, &damage, force=1)`
(`sub_820D59D0`) with damage+24 = 1e9: it returns at once for +625 (invulnerable),
subtracts +600 × damage from +592, and on zero sets +624, sends BASEDTRY and leaves the
faction list; it returns 2 for a kill. `remove` uses the scene graph's mark-for-removal
`sub_821C0ED8` (mission native #201).

**God** keeps the player's +625 set (the same early return) and +592 at +596.
**Ammo**: a soldier's weapons are `[player+1824]`, `[player+1832]` of them, 1408 bytes
each; +800 is the magazine size and +804 the rounds left (ints; `sub_820E28B8` fires,
`clWeapon01::slot3` reloads at zero), kept equal.
**Teleport** writes clSoldierObject's accumulated position +1440 (which its update
clamps to the map bounds +1024/+1040 and copies to +272) and +272.
**pos** reads +272, and the heading from the player camera's world matrix (+416, row 2),
the first object of `[core+0]`.

**Buildings.** `sub_821C5AC0(grid [0x82578678], &center, radius, callback, data)` calls
the callback for each object whose bounds (+160 centre, +176 half extents) come within
the radius; the retail `vanish` passes `sub_820B3B10`. The query gives up when both
corners of its box fall outside the grid (`sub_821C4D00`, `sub_821C4BE8`: origin at
grid+80, `[grid+64]` holding the cells per axis at +8 and the cell size at +12), so a
radius larger than half the grid asks for the whole grid and filters by distance. The
console hooks that callback
to collect the objects instead while it asks, casts each to `clMapObject_Base`
(`0x82550CCC`) and keeps those with health (+392) left. A building is destroyed the way
its damage handler does it on a lethal hit (`clMapArtifact_Base::slot5`,
`sub_820B38D8`): +392 = 0, `sub_820B3818(obj, &payload)` sends MB_Destp to its children,
then its slot 0 receives MB_Destr (`0x82578660`) with the payload (impact point,
direction, and a damage record whose +28 is the collapse power). `clBuilding::slot0`
(`sub_820B81E0`) then runs the collapse (`sub_820B2E78`, the "fallmodel") and plays one
of the four collapse sounds (ビル倒壊A–D).

**Explosions** are `sub_8210FF78([0x82578770], &position, &direction, scale, r7)`, the
ordinary explosion the UFOs and rockets use (three billboards and a 60-frame fireball,
smoke_01/explosion_01); r7 = 1 would add a camera shake, the console passes 0.

**Time scale.** The native engine heartbeat (`sub_821BEAB0`'s hook) grants simulation
ticks from `NativePacingClock`; the console's scale sets that clock's rate
(`native_pacing.h`), rebasing so the tick count stays continuous.

**Where commands run.** The step dispatcher's hook (`sub_821A4BA0`, frame.cpp) calls
`console::EngineStepBegin` before the iteration's steps and `EngineStepEnd` after. When
nothing is queued the cost is a lock and a few loads per engine iteration.

## Limitations

- No draw counts in `stats`: the renderer's per-pass numbers come from its timing logs.
- A spawn inside a building's footprint lands on its roof; spawns do not avoid each
  other beyond the ring spacing.
- `timescale` does not stretch audio or movies; above about 3 the engine may not keep up
  (it runs at most 4 ticks per iteration).
- `killall spawned` recognises objects by address; an address the game frees and reuses
  could be misattributed, and an object whose game object is not the one CreateObject
  returned (seen once with the ant hill) is missed; plain `killall` still gets it.
- Killing a mission's own enemies advances its script like any other kill: in Mission 1,
  repeated `killall` clears the waves and ends the mission.
- The first spawn of a type the mission did not preload loads it synchronously on the
  engine thread (tens to hundreds of milliseconds).
- Soldiers, vehicles and the mothership cannot be spawned.
