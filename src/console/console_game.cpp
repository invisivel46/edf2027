// EDF2027 console - game commands: spawn, killall, god, ammo, teleport, pos, destroy,
// effects, timescale, stats. Engine thread only (console.h), through the game's own code.
//
// Everything here drives guest functions the game itself uses; none of it writes a game
// structure the game does not also write. Addresses are the retail image's (title
// 445007D3); docs/console.md has the evidence for each.
//
//   Spawning (mission-script native #300, sub_820CEBD0, which M001 uses for its ants):
//     path   = L"game:\\Object\\" + name + L".Sgo"            (a guest std::wstring)
//     clResourceManager::MakeObjectCache([0x82578648], path, 1) sub_820AFAC0: loads the
//            SGO and its model/texture dependencies once, synchronously; a cached name
//            returns at once
//     obj    = clCore::CreateObject([0x8257C030], &matrix, path, &params)  sub_821A6E08
//     go     = __RTDynamicCast(obj, 0, Sgs::clObject_Base, clGameObject_Base)  sub_821E9348
//     SetScale(go, 1.0)          sub_820D5430
//     SetFaction(go, 1, true)    sub_820D5ED0 (faction 1 = the invaders)
//     Awaken(go)                 sub_820D4A00 (the script's "start awake" argument)
//   Ground placement (natives #251/#301): sweep from +1000 Y by (0,-10000,0) through
//     sub_821AD100([0x82578678], 0, 0, &start, &move, 0, 0x821A3C78, &result); the hit
//     position is result+16.
//   Killing: clGameObject_Base::ApplyDamage(obj, &damage, force) sub_820D59D0 with damage
//     +24 = health damage; returns 2 on a kill (it sends BASEDTRY and leaves the faction list).
//     Removal: scenegraph mark-subtree sub_821C0ED8 (native #201).
//   Objects: faction manager [0x82578720]: 5 records of 40 bytes at [manager]; record+0
//     heads an intrusive list of object+608 nodes (next at +0). Object +604 faction,
//     +592/+596 health/max, +624 dead, +625 invulnerable (ApplyDamage returns at once),
//     +272 position. The player camera (clPlayerCamera, first on the clCore [0x8257C030]
//     list [core+0] -> [core+12], node+0 next, node+8 object) has its world matrix at
//     +416 (row 2 forward). Core bytes +2260..+2262 are nonzero while loading.
//   Teleport: clSoldierObject's accumulated position +1440 (copied to +272 each update).
//   Weapons (clSoldierObject): [player+1824] array of [player+1832] weapons, 1408 bytes
//     each; +800 magazine size, +804 rounds left (ints).
//   Buildings (clBuilding, clMapArtifact_Base): the debug console's "vanish" (sub_820BE088)
//     finds map objects around a point with the grid query sub_821C5AC0([0x82578678],
//     &center, radius, callback, 0); its callback sub_820B3B10 is hooked here to collect
//     them. Destroying one is clMapArtifact_Base::slot5's death branch (sub_820B38D8):
//     health +392 = 0, sub_820B3818(obj, &payload) (MB_Destp to the children), then
//     slot 0 (obj, &MB_Destr [0x82578660], &payload); clBuilding::slot0 (sub_820B81E0)
//     then collapses it (sub_820B2E78 "fallmodel") and plays a collapse sound.
//   Effects: the ordinary explosion sub_8210FF78([0x82578770], &pos, &dir, scale, r7 = shake).
#include "console.h"
#include "console_guest.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <numbers>
#include <random>
#include <unordered_set>
#include "frame_stats.h"
#include "native_graphics/native_console_counters.h"
#include "native_graphics/native_pacing.h"

REXCVAR_DEFINE_INT32(edf_console_spawn_per_tick, 20, "EDF2027",
                     "Console spawn: objects created per simulation tick (larger spawns spread over ticks)")
    .range(1, 1000);

REX_EXTERN(sub_821E9348);  // __RTDynamicCast
REX_EXTERN(sub_820A0D10);  // std::wstring::assign(const wchar_t*, size)
REX_EXTERN(sub_820A0858);  // std::wstring::~wstring
REX_EXTERN(sub_820AFAC0);  // clResourceManager::MakeObjectCache
REX_EXTERN(sub_821A6E08);  // clCore::CreateObject
REX_EXTERN(sub_820D5430);  // clGameObject_Base scale
REX_EXTERN(sub_820D5ED0);  // clGameObject_Base faction
REX_EXTERN(sub_820D4A00);  // clGameObject_Base awaken (BASENCUT)
REX_EXTERN(sub_820D59D0);  // clGameObject_Base damage
REX_EXTERN(sub_821C0ED8);  // scene graph: mark subtree for removal
REX_EXTERN(sub_821AD100);  // collision sweep
REX_EXTERN(sub_821C5AC0);  // grid sphere query with callback
REX_EXTERN(sub_820B3818);  // map artifact: destroy children (MB_Destp)
REX_EXTERN(sub_8210FF78);  // ordinary explosion effect

namespace edf::console {
namespace {
// ---- Guest addresses -------------------------------------------------------------------
constexpr uint32_t kCorePointer = 0x8257C030;        // clCore*
constexpr uint32_t kResourceManagerPointer = 0x82578648;
constexpr uint32_t kCollisionPointer = 0x82578678;   // collision world / map grid
constexpr uint32_t kFactionManagerPointer = 0x82578720;
constexpr uint32_t kEffectManagerPointer = 0x82578770;
constexpr uint32_t kSweepFilter = 0x821A3C78;        // the scatter natives' sweep callback
constexpr uint32_t kVanishCallback = 0x820B3B10;     // hooked below as the collector
constexpr uint32_t kMessageDestroy = 0x82578660;     // "MB_Destr"
constexpr uint32_t kTypeSgsObject = 0x82550D14;      // RTTI Sgs::clObject_Base
constexpr uint32_t kTypeGameObject = 0x82551058;     // RTTI clGameObject_Base
constexpr uint32_t kTypeMapObject = 0x82550CCC;      // RTTI clMapObject_Base
constexpr uint32_t kFactionCount = 5, kFactionRecord = 40, kFactionNode = 608;
constexpr uint32_t kListLimit = 20000;               // walk bound: a corrupt list cannot hang the engine

// ---- Spawn types -----------------------------------------------------------------------
struct SpawnType {
  const char* name;
  const char* sgo;         // Object/<sgo>.Sgo on the disc
  const char* what;
  int faction;             // -1: leave the object's own
  const char* object_class;  // RTTI class, for killall filters
  float altitude = 0;        // above the player instead of on the ground (flyers)
};
// Durabilities from each SGO's go_Durability (the analysis notes' drop table).
constexpr SpawnType kTypes[] = {
    {"ant", "GiantAnt01", "black giant ant (durability 60)", 1, "clGiantAnt"},
    {"ant015", "GiantAnt015", "black giant ant, mission 1 variant (60)", 1, "clGiantAnt"},
    {"redant", "GiantAnt02", "red giant ant (180)", 1, "clGiantAnt"},
    {"queen", "GiantAntQueen", "giant ant queen (2000)", 1, "clGiantAnt"},
    {"spider", "GiantSpider01", "giant spider (45)", 1, "clGiantSpider"},
    {"spiderlord", "GiantSpiderLord", "giant spider lord (2000)", 1, "clGiantSpider"},
    {"ufo", "UfoSmall01", "small UFO gunship (50)", 1, "clUfoSmall01", 30.0f},
    {"ufo2", "UfoSmall02", "small UFO gunship, type 2 (200)", 1, "clUfoSmall01", 30.0f},
    {"ufo3", "UfoSmall03", "small UFO gunship, type 3 (200)", 1, "clUfoSmall01", 30.0f},
    {"carrier", "UfoCarrier01", "UFO carrier / dropship (600)", 1, "clUfoCarrier01", 80.0f},
    {"hector", "AlienTankCC", "Hector walking robot, twin cannon (300)", 1, "clAlienTank01"},
    {"hectorkk", "AlienTankKK", "Hector, KK arms (300)", 1, "clAlienTank01"},
    {"hectormc", "AlienTankMC", "Hector, MC arms (300)", 1, "clAlienTank01"},
    {"hectormm", "AlienTankMM", "Hector, MM arms (300)", 1, "clAlienTank01"},
    {"hectormo", "AlienTankMO", "Hector, MO arms (300)", 1, "clAlienTank01"},
    {"hectoroc", "AlienTankOC", "Hector, OC arms (300)", 1, "clAlienTank01"},
    {"hectoroo", "AlienTankOO", "Hector, OO arms (300)", 1, "clAlienTank01"},
    {"hectorrr", "AlienTankRR", "Hector, RR arms (300)", 1, "clAlienTank01"},
    {"bighector", "AlienTankBCC", "large Hector, twin cannon (1200)", 1, "clAlienTank01"},
    {"bighectormm", "AlienTankBMM", "large Hector, MM arms (1200)", 1, "clAlienTank01"},
    {"bighectorrr", "AlienTankBRR", "large Hector, RR arms (1200)", 1, "clAlienTank01"},
    {"anthill", "AntHill01", "ant hill (800)", 1, "clAntHill"},
    {"monster", "Monster01", "Solomon, the kaiju (8000)", 1, "clMonster01"},
    {"mech", "MonsterMech01", "mechanical kaiju (15000)", 1, "clMonster01Mech"},
    {"walker", "Alien4LegTank01", "four-legged walker boss (5000)", 1, "clAlien4LegTank01"},
    {"civilian", "PeopleM01", "civilian, man (30)", 3, "clFriendPeople"},
    {"civilianf", "PeopleF01", "civilian, woman (30)", 3, "clFriendPeople"},
};
const SpawnType* FindType(std::string_view name) {
  for (const auto& type : kTypes)
    if (Lower(name) == Lower(type.name) || Lower(name) == Lower(type.sgo)) return &type;
  return nullptr;
}
std::vector<std::string> TypeNames(bool with_list) {
  std::vector<std::string> names;
  if (with_list) names.push_back("list");
  for (const auto& type : kTypes) names.push_back(type.name);
  return names;
}

// ---- Objects -----------------------------------------------------------------------------
bool EngineIdle(const GuestMemory& m) {
  const uint32_t core = m.U32(kCorePointer);
  if (!GuestMemory::Heap(core, 2264)) return false;
  return !m.U8(core + 2260) && !m.U8(core + 2261) && !m.U8(core + 2262);
}
// The objects of one faction's list.
std::vector<uint32_t> FactionObjects(const GuestMemory& m, uint32_t faction) {
  std::vector<uint32_t> objects;
  const uint32_t manager = m.U32(kFactionManagerPointer);
  if (!GuestMemory::Heap(manager) || faction >= kFactionCount) return objects;
  const uint32_t records = m.U32(manager);
  if (!GuestMemory::Heap(records, kFactionCount * kFactionRecord)) return objects;
  uint32_t node = m.U32(records + faction * kFactionRecord);
  for (uint32_t n = 0; node && n < kListLimit; ++n) {
    if (!GuestMemory::Heap(node, 8) || node < kFactionNode) break;
    objects.push_back(node - kFactionNode);
    node = m.U32(node);
  }
  return objects;
}
// (The core's lists, [core+0..+12) and [core+44..+56) that the step dispatcher
// sub_821A4BA0 walks, hold the top-level managers -- clPlayerCamera,
// clGameObject_Manager, clMapObjectManager, clEffectObjectManager, ... -- not the
// objects themselves, so object counts come from the faction lists and the map grid.)
struct Vec3 { float x = 0, y = 0, z = 0; };
Vec3 Position(const GuestMemory& m, uint32_t object) { return {m.F32(object + 272), m.F32(object + 276), m.F32(object + 280)}; }
// The view's heading: the player camera's world matrix (+416; row 2 forward), the object
// the retail debug console's "object" command also spawns in front of (sub_820BE738:
// the first object of [core+0], matrix +416, 5 units along row 2). 0 = +Z, pi/2 = +X.
uint32_t FindCamera(const GuestMemory& m) {
  const uint32_t core = m.U32(kCorePointer);
  if (!GuestMemory::Heap(core, 16)) return 0;
  const uint32_t end = m.U32(core + 12);
  uint32_t node = m.U32(core);
  for (uint32_t n = 0; node && node != end && n < 64; ++n) {
    if (!GuestMemory::Heap(node, 12)) break;
    if (const uint32_t object = m.U32(node + 8); m.ClassName(object) == "clPlayerCamera") return object;
    node = m.U32(node);
  }
  return 0;
}
float HeadingRadians(const GuestMemory& m, uint32_t /*player*/) {
  const uint32_t camera = FindCamera(m);
  if (!GuestMemory::Heap(camera, 480)) return 0;
  const float fx = m.F32(camera + 416 + 32), fz = m.F32(camera + 416 + 40);
  if (!std::isfinite(fx) || !std::isfinite(fz) || (fx == 0 && fz == 0)) return 0;
  return std::atan2(fx, fz);
}
uint32_t FindPlayer(const GuestMemory& m) {
  for (const uint32_t object : FactionObjects(m, 0))
    if (m.ClassName(object) == "clPlayerObject") return object;
  return 0;
}
std::string Fmt(const char* format, auto... args) {
  char text[512];
  std::snprintf(text, sizeof(text), format, args...);
  return text;
}

// ---- Guest operations ----------------------------------------------------------------------
float GroundY(GuestCalls& g, float x, float y, float z, bool* hit_out = nullptr) {
  const auto& m = g.memory();
  const uint32_t mark = g.Mark();
  const uint32_t start = g.Alloc(16), move = g.Alloc(16), result = g.Alloc(96);
  m.WF32(start, x); m.WF32(start + 4, y + 1000.0f); m.WF32(start + 8, z); m.WF32(start + 12, 1.0f);
  m.WF32(move + 4, -10000.0f); m.WF32(move + 12, 1.0f);
  m.WF32(result + 28, 1.0f); m.WF32(result + 44, 1.0f);
  const uint32_t hit = g.Call(sub_821AD100, GuestCalls::R({m.U32(kCollisionPointer), 0, 0, start, move, 0, kSweepFilter,
                                                           result})) & 0xFF;
  const float ground = m.F32(result + 20);
  g.Release(mark);
  if (hit_out) *hit_out = hit != 0;
  return hit && std::isfinite(ground) ? ground : y;
}

// Creates one object of `type` at (x, y, z) facing `yaw`; returns the game object (or 0).
uint32_t CreateObject(GuestCalls& g, const SpawnType& type, float x, float y, float z, float yaw, bool awake) {
  const auto& m = g.memory();
  const uint32_t mark = g.Mark();
  const std::string path_text = std::string("game:\\Object\\") + type.sgo + ".Sgo";
  const uint32_t literal = g.WideString(path_text);
  const uint32_t path = g.Alloc(32);  // MSVC std::wstring: +4 buffer, +20 size, +24 capacity
  m.W32(path + 24, 7);
  g.Call(sub_820A0D10, GuestCalls::R({path, literal, uint32_t(path_text.size())}));
  g.Call(sub_820AFAC0, GuestCalls::R({m.U32(kResourceManagerPointer), path, 1}));
  const uint32_t matrix = g.Alloc(64), params = g.Alloc(80);
  const float c = std::cos(yaw), s = std::sin(yaw);
  const float rows[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, y, z, 1};
  for (int i = 0; i < 16; ++i) m.WF32(matrix + uint32_t(i) * 4, rows[i]);
  for (int i = 0; i < 4; ++i) m.WF32(params + uint32_t(i) * 20, 1.0f);  // identity; the rest zero
  const uint32_t object = g.Call(sub_821A6E08, GuestCalls::R({m.U32(kCorePointer), matrix, path, params}));
  uint32_t game_object = 0;
  if (object) {
    game_object = g.Call(sub_821E9348, GuestCalls::R({object, 0, kTypeSgsObject, kTypeGameObject, 0}));
    if (game_object) {
      g.Call(sub_820D5430, GuestCalls::R({game_object}, {1.0}));
      if (type.faction >= 0) g.Call(sub_820D5ED0, GuestCalls::R({game_object, uint32_t(type.faction), 1}));
      if (awake) g.Call(sub_820D4A00, GuestCalls::R({game_object}));
    }
  }
  g.Call(sub_820A0858, GuestCalls::R({path}));
  g.Release(mark);
  return game_object ? game_object : object;
}

// 2 = killed, 1 = damaged but alive, 0/3 = refused (invulnerable or already dead).
uint32_t Kill(GuestCalls& g, uint32_t object) {
  const auto& m = g.memory();
  const uint32_t mark = g.Mark();
  const uint32_t damage = g.Alloc(128);
  m.W32(damage + 20, 0xFFFFFFFFu);  // no faction
  m.WF32(damage + 24, 1.0e9f);
  const uint32_t result = g.Call(sub_820D59D0, GuestCalls::R({object, damage, 1}));
  g.Release(mark);
  return result;
}

// The objects the map grid holds within `radius` of `center` (the vanish query).
thread_local std::vector<uint32_t>* collecting = nullptr;
// The grid's box: origin at grid+80..+88 and [grid+64] holding the cells per axis (+8) and
// the cell size (+12), as its cell lookup sub_821C4BE8 reads them. The query refuses a box
// whose two corners both fall outside (sub_821C4D00), so a very large radius asks for
// the whole grid instead and filters by distance.
struct GridBox { Vec3 center; float half = 0; };
std::optional<GridBox> GridBounds(const GuestMemory& m) {
  const uint32_t grid = m.U32(kCollisionPointer);
  if (!GuestMemory::Heap(grid, 96)) return std::nullopt;
  const uint32_t cells = m.U32(grid + 64);
  if (!GuestMemory::Heap(cells, 16)) return std::nullopt;
  const float size = m.F32(cells + 12) * float(m.U32(cells + 8));
  if (!(size > 0) || !std::isfinite(size)) return std::nullopt;
  const float half = size * 0.5f;
  return GridBox{{m.F32(grid + 80) + half, m.F32(grid + 84) + half, m.F32(grid + 88) + half}, half};
}
std::vector<uint32_t> GridQuery(GuestCalls& g, Vec3 center, float radius);
std::vector<uint32_t> GridObjects(GuestCalls& g, Vec3 center, float radius) {
  const auto box = GridBounds(g.memory());
  if (box && radius < box->half) {
    auto objects = GridQuery(g, center, radius);
    if (!objects.empty()) return objects;
  }
  if (!box) return GridQuery(g, center, radius);
  // Whole grid, then keep those within the radius (horizontally, by their bounds centre).
  auto objects = GridQuery(g, box->center, box->half);
  const auto& m = g.memory();
  std::erase_if(objects, [&](uint32_t object) {
    const float dx = m.F32(object + 160) - center.x, dz = m.F32(object + 168) - center.z;
    return dx * dx + dz * dz > radius * radius;
  });
  return objects;
}
std::vector<uint32_t> GridQuery(GuestCalls& g, Vec3 center, float radius) {
  const auto& m = g.memory();
  std::vector<uint32_t> objects;
  const uint32_t mark = g.Mark();
  const uint32_t point = g.Alloc(16);
  m.WF32(point, center.x); m.WF32(point + 4, center.y); m.WF32(point + 8, center.z); m.WF32(point + 12, 1.0f);
  collecting = &objects;
  try {
    g.Call(sub_821C5AC0, GuestCalls::R({m.U32(kCollisionPointer), point, 0, kVanishCallback, 0}, {double(radius)}));
  } catch (...) { collecting = nullptr; throw; }
  collecting = nullptr;
  g.Release(mark);
  std::sort(objects.begin(), objects.end());
  objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
  return objects;
}
struct MapObject { uint32_t object; Vec3 center; float distance; std::string cls; };
std::vector<MapObject> StandingMapObjects(GuestCalls& g, Vec3 center, float radius) {
  const auto& m = g.memory();
  std::vector<MapObject> out;
  for (const uint32_t object : GridObjects(g, center, radius)) {
    if (!GuestMemory::Heap(object, 400)) continue;
    const uint32_t map_object = g.Call(sub_821E9348, GuestCalls::R({object, 0, kTypeSgsObject, kTypeMapObject, 0}));
    if (!map_object || !(m.F32(map_object + 392) > 0)) continue;  // not a map object, or already down
    MapObject entry{map_object, {m.F32(map_object + 160), m.F32(map_object + 164), m.F32(map_object + 168)}, 0,
                    m.ClassName(map_object)};
    const float dx = entry.center.x - center.x, dz = entry.center.z - center.z;
    entry.distance = std::sqrt(dx * dx + dz * dz);
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(), [](const MapObject& a, const MapObject& b) { return a.distance < b.distance; });
  return out;
}
void DestroyMapObject(GuestCalls& g, const MapObject& target) {
  const auto& m = g.memory();
  const uint32_t mark = g.Mark();
  // The death branch's payload: impact point, direction, and a damage record whose +28 is
  // the collapse power sub_820B2E78 reads.
  const uint32_t payload = g.Alloc(48), record = g.Alloc(32);
  m.WF32(payload, target.center.x); m.WF32(payload + 4, target.center.y); m.WF32(payload + 8, target.center.z);
  m.WF32(payload + 12, 1.0f);
  m.WF32(payload + 20, -1.0f); m.WF32(payload + 28, 1.0f);
  m.W32(payload + 32, record);
  m.WF32(record + 24, 1.0e6f);
  m.WF32(record + 28, 1.0f);
  m.WF32(target.object + 392, 0.0f);
  g.Call(sub_820B3818, GuestCalls::R({target.object, payload}));
  const uint32_t vtable = m.U32(target.object);
  if (GuestMemory::Image(vtable)) g.CallAddress(m.U32(vtable), GuestCalls::R({target.object, kMessageDestroy, payload}));
  g.Release(mark);
}
void Explosion(GuestCalls& g, Vec3 at, float scale, bool shake) {
  const auto& m = g.memory();
  const uint32_t mark = g.Mark();
  const uint32_t position = g.Alloc(16), direction = g.Alloc(16);
  m.WF32(position, at.x); m.WF32(position + 4, at.y); m.WF32(position + 8, at.z); m.WF32(position + 12, 1.0f);
  m.WF32(direction + 4, 1.0f);
  g.Call(sub_8210FF78, GuestCalls::R({m.U32(kEffectManagerPointer), position, direction, 0, shake ? 1u : 0u}, {double(scale)}));
  g.Release(mark);
}

// ---- Per-tick state ------------------------------------------------------------------------
std::atomic<bool> god{false}, god_release{false}, infinite_ammo{false};
struct Job {
  std::string label;
  std::function<bool(GuestCalls&, EngineFrame&)> step;  // true: done
};
std::mutex jobs_mutex;
std::vector<Job> jobs;
void AddJob(Job job) {
  std::lock_guard lock(jobs_mutex);
  jobs.push_back(std::move(job));
}

// Objects the console created, for "killall spawned". Engine thread only. An address the
// game frees and reuses for one of its own objects would be taken for ours; the set is
// pruned against the live faction list on every killall.
std::unordered_set<uint32_t>& Spawned() {
  static std::unordered_set<uint32_t> spawned;
  return spawned;
}

// Ring placements around a centre: `count` points from `first_radius` outwards, about
// `spacing` apart, starting at `yaw0` (in front of the player).
struct Placement { float x, z; };
std::vector<Placement> Rings(float cx, float cz, float first_radius, int count, float yaw0, float spacing = 5.0f) {
  std::vector<Placement> out;
  float radius = first_radius;
  if (radius <= 0.01f) { out.push_back({cx, cz}); radius = spacing; }
  while (int(out.size()) < count) {
    const int capacity = std::max(6, int(2.0f * std::numbers::pi_v<float> * radius / spacing));
    const int here = std::min(capacity, count - int(out.size()));
    for (int i = 0; i < here; ++i) {
      const float angle = yaw0 + 2.0f * std::numbers::pi_v<float> * float(i) / float(here);
      out.push_back({cx + std::sin(angle) * radius, cz + std::cos(angle) * radius});
    }
    radius += spacing * 1.2f;
  }
  return out;
}

void Spawn(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  const GuestMemory m(ctx.frame->base);
  if (Lower(in.str(0)) == "list") {
    in.Print("spawn types (name or SGO name):");
    for (const auto& type : kTypes) in.Print(Fmt("  %-12s %-16s %s", type.name, type.sgo, type.what));
    in.Print("There is no golden ant in EDF 2017's data; the disc's ant SGOs are GiantAnt01/015/02 and the queen.");
    return;
  }
  const SpawnType* type = FindType(in.str(0));
  const uint32_t player = FindPlayer(m);
  if (!player) { in.Error("spawn: no player object (not in a mission)"); return; }
  const int count = in.has(1) ? int(in.integer(1)) : 1;
  const Vec3 origin = Position(m, player);
  const float heading = HeadingRadians(m, player);
  std::vector<Placement> spots;
  float base_y = origin.y;
  if (in.count() >= 5) {
    base_y = float(in.number(3));
    spots = Rings(float(in.number(2)), float(in.number(4)), 0.0f, count, heading);
  } else {
    const float distance = in.has(2) ? float(in.number(2)) : 30.0f;
    if (count == 1) spots.push_back({origin.x + std::sin(heading) * distance, origin.z + std::cos(heading) * distance});
    else spots = Rings(origin.x, origin.z, distance, count, heading);
  }
  in.Print(Fmt("spawn: %d x %s (%s) queued, %d per tick", count, type->name, type->sgo, REXCVAR_GET(edf_console_spawn_per_tick)));
  struct State { size_t next = 0; int created = 0, failed = 0, grounded = 0; };
  auto state = std::make_shared<State>();
  const SpawnType chosen = *type;
  AddJob({std::string("spawn ") + type->name, [=](GuestCalls& g, EngineFrame& frame) {
            const GuestMemory mem(frame.base);
            const uint32_t now_player = FindPlayer(mem);
            if (!now_player || !EngineIdle(mem)) {
              Service::Get().Print(Severity::kWarn, Fmt("spawn %s: mission ended after %d of %zu", chosen.name, state->created, spots.size()));
              return true;
            }
            const Vec3 target = Position(mem, now_player);
            int budget = REXCVAR_GET(edf_console_spawn_per_tick);
            while (budget-- > 0 && state->next < spots.size()) {
              const auto spot = spots[state->next++];
              bool hit = false;
              float y = base_y + chosen.altitude;  // flyers hover above the player's height
              if (chosen.altitude <= 0) {
                y = GroundY(g, spot.x, base_y, spot.z, &hit);
                state->grounded += hit;
              }
              const float yaw = std::atan2(target.x - spot.x, target.z - spot.z);
              if (const uint32_t object = CreateObject(g, chosen, spot.x, y, spot.z, yaw, true)) {
                ++state->created;
                Spawned().insert(object);
              } else {
                ++state->failed;
              }
            }
            if (state->next < spots.size()) return false;
            Service::Get().Print(state->failed ? Severity::kWarn : Severity::kOk,
                                 Fmt("spawn %s: created %d (%d failed, %d placed on the ground by the sweep) at tick %llu",
                                     chosen.name, state->created, state->failed, state->grounded,
                                     static_cast<unsigned long long>(frame.tick)));
            return true;
          }});
}

// "killall [type|class|all|spawned] [remove]"
void KillAll(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  GuestCalls g(*ctx.frame->ctx, ctx.frame->base);
  const auto& m = g.memory();
  std::string filter;
  bool remove = false, spawned_only = false;
  for (size_t i = 0; i < in.count(); ++i) {
    const auto word = Lower(in.str(i));
    if (word == "remove") remove = true;
    else if (word == "spawned") spawned_only = true;
    else if (word != "all") {
      if (const auto* type = FindType(word)) filter = type->object_class;
      else filter = in.str(i);
    }
  }
  std::map<std::string, int> killed, survived;
  const auto enemies = FactionObjects(m, 1);
  for (const uint32_t object : enemies) {
    const auto cls = m.ClassName(object);
    if (!filter.empty() && Lower(cls) != Lower(filter)) continue;
    if (spawned_only && !Spawned().contains(object)) continue;
    if (remove) { g.Call(sub_821C0ED8, GuestCalls::R({object})); ++killed[cls]; continue; }
    const uint32_t result = Kill(g, object);
    if (result == 2) ++killed[cls];
    else ++survived[cls];
  }
  {
    const std::unordered_set<uint32_t> live(enemies.begin(), enemies.end());
    std::erase_if(Spawned(), [&](uint32_t object) { return !live.contains(object); });
  }
  int total = 0;
  std::string detail;
  for (const auto& [cls, n] : killed) { total += n; detail += Fmt(" %s=%d", cls.c_str(), n); }
  in.Ok(Fmt("killall: %s %d of %zu enemies%s", remove ? "removed" : "killed", total, enemies.size(), detail.c_str()));
  for (const auto& [cls, n] : survived) in.Warn(Fmt("killall: %d %s survived (invulnerable or scripted); try 'killall %s remove'", n, cls.c_str(), cls.c_str()));
}

void Destroy(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  GuestCalls g(*ctx.frame->ctx, ctx.frame->base);
  const auto& m = g.memory();
  const uint32_t player = FindPlayer(m);
  const auto mode = Lower(in.str(0));
  float radius = 100.0f;
  size_t first_limit_arg = 1;
  if (mode == "all") radius = 100000.0f;
  else if (mode == "radius") { radius = in.has(1) ? float(in.number(1)) : 100.0f; first_limit_arg = 2; }
  else if (mode == "nearest") first_limit_arg = 2;
  else if (ParseFloat(mode)) radius = float(*ParseFloat(mode));
  else { in.Error("destroy: usage: destroy <radius R | all | nearest [n] | R> [max]"); return; }
  size_t limit = SIZE_MAX;
  if (mode == "nearest") {
    limit = in.has(1) ? size_t(std::max<int64_t>(1, in.integer(1))) : 1;
    radius = 100000.0f;
  } else if (in.has(first_limit_arg)) {
    limit = size_t(std::max<int64_t>(1, in.integer(first_limit_arg)));
  }
  const Vec3 center = Position(m, player);
  auto targets = StandingMapObjects(g, center, radius);
  const size_t found = targets.size();
  if (targets.size() > limit) targets.resize(limit);
  std::map<std::string, int> classes;
  for (const auto& target : targets) {
    DestroyMapObject(g, target);
    ++classes[target.cls];
  }
  std::string detail;
  for (const auto& [cls, n] : classes) detail += Fmt(" %s=%d", cls.c_str(), n);
  in.Ok(Fmt("destroy: %zu of %zu standing map objects within %.0f%s", targets.size(), found, radius, detail.c_str()));
}

void Effects(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  const GuestMemory m(ctx.frame->base);
  const int count = int(in.integer(0));
  const float radius = in.has(1) ? float(in.number(1)) : 15.0f;
  const float scale = in.has(2) ? float(in.number(2)) : 3.0f;
  const int per_tick = in.has(3) ? int(in.integer(3)) : count;
  const uint32_t player = FindPlayer(m);
  const Vec3 center = Position(m, player);
  const float heading = HeadingRadians(m, player);
  auto next = std::make_shared<int>(0);
  auto random = std::make_shared<std::mt19937>(uint32_t(ctx.frame->tick));
  AddJob({"effects", [=](GuestCalls& g, EngineFrame& frame) {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            for (int i = 0; i < per_tick && *next < count; ++i, ++(*next)) {
              const float angle = heading + 2.0f * std::numbers::pi_v<float> * unit(*random);
              const float r = radius * std::sqrt(unit(*random));
              const float x = center.x + std::sin(angle) * r, z = center.z + std::cos(angle) * r;
              const float y = GroundY(g, x, center.y, z) + 1.0f;
              Explosion(g, {x, y, z}, scale, false);
            }
            if (*next < count) return false;
            Service::Get().Print(Severity::kOk, Fmt("effects: %d explosions done at tick %llu", count,
                                                    static_cast<unsigned long long>(frame.tick)));
            return true;
          }});
  in.Print(Fmt("effects: %d explosions within %.0f of the player, scale %.1f, %d per tick", count, radius, scale, per_tick));
}

void Stats(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  const GuestMemory m(ctx.frame->base);
  auto& service = Service::Get();
  const auto& frames = CurrentFrameStats();
  auto& timing = service.timing();
  in.Print(Fmt("frames: %.1f FPS, avg %.2f ms, min %.2f, max %.2f, 1%% low %.2f ms (5 s window, %u frames total)",
               frames.fps.load(), frames.average_ms.load(), frames.minimum_ms.load(), frames.maximum_ms.load(),
               frames.low_1_percent_ms.load(), frames.total_frames.load()));
  in.Print(Fmt("simulation: tick %llu, %.1f steps/s, step dispatch avg %.2f ms, max %.2f ms (last second), timescale %.2f",
               static_cast<unsigned long long>(ctx.frame->tick), timing.steps_per_second.load(),
               timing.dispatch_ms_avg.load(), timing.dispatch_ms_max.load(),
               edf::native::NativeTimeScaleRequest().load() ));
  const auto& renderer = edf::native::ConsoleCounters();
  in.Print(Fmt("effects (last native frame): %u effect objects walked, %u filed for drawing, %u immediate draws "
               "(per-pass GPU times and draw counts: edf_native_gpu_timings / edf_native_frame_times)",
               renderer.effects_visited.load(), renderer.effects_filed.load(), renderer.effects_drawn.load()));
  static const char* kFactionNames[kFactionCount] = {"EDF/player", "invaders", "faction2", "civilians", "faction4"};
  std::string factions;
  for (uint32_t f = 0; f < kFactionCount; ++f) factions += Fmt(" %s=%zu", kFactionNames[f], FactionObjects(m, f).size());
  in.Print("factions (live):" + factions);
  // Objects per class: every faction's live objects, and everything the map grid holds
  // (buildings, props and the moving objects it tracks for collision).
  std::unordered_set<uint32_t> objects;
  for (uint32_t f = 0; f < kFactionCount; ++f)
    for (const uint32_t object : FactionObjects(m, f)) objects.insert(object);
  size_t grid_count = 0;
  // The grid is the mission's; outside one it may be stale, so it is only asked in one.
  if (const auto box = GameInMission(*ctx.frame) ? GridBounds(m) : std::nullopt) {
    GuestCalls g(*ctx.frame->ctx, ctx.frame->base);
    const auto grid = GridQuery(g, box->center, box->half);
    grid_count = grid.size();
    objects.insert(grid.begin(), grid.end());
  }
  std::map<std::string, int> classes;
  for (const uint32_t object : objects) {
    auto cls = m.ClassName(object);
    ++classes[cls.empty() ? "?" : cls];
  }
  std::vector<std::pair<int, std::string>> sorted;
  for (auto& [cls, n] : classes) sorted.push_back({n, cls});
  std::sort(sorted.rbegin(), sorted.rend());
  const size_t shown = in.has(0) && Lower(in.str(0)) == "all" ? sorted.size() : std::min<size_t>(sorted.size(), 16);
  in.Print(Fmt("objects: %zu in %zu classes (faction lists and the %zu in the map grid)%s", objects.size(), sorted.size(),
               grid_count, shown < sorted.size() ? "; 'stats all' lists every class" : ""));
  std::string line;
  for (size_t i = 0; i < shown; ++i) {
    line += Fmt("  %s=%d", sorted[i].second.c_str(), sorted[i].first);
    if ((i + 1) % 4 == 0 || i + 1 == shown) { in.Print(line); line.clear(); }
  }
  in.Print(Fmt("cheats: %s", service.cheats_used() ? "USED" : "none"));
}

void Pos(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  const GuestMemory m(ctx.frame->base);
  const uint32_t player = FindPlayer(m);
  const Vec3 p = Position(m, player);
  const float heading = HeadingRadians(m, player) * 180.0f / std::numbers::pi_v<float>;
  in.Print(Fmt("pos: %.2f %.2f %.2f  heading %.1f deg  health %.0f/%.0f  (object 0x%08X)", p.x, p.y, p.z, heading,
               m.F32(player + 592), m.F32(player + 596), player));
}

void Teleport(Invocation& in) {
  auto& ctx = *static_cast<ExecContext*>(in.host());
  const GuestMemory m(ctx.frame->base);
  const uint32_t player = FindPlayer(m);
  const float x = float(in.number(0)), y = float(in.number(1)), z = float(in.number(2));
  // clSoldierObject integrates its accumulated position +1440 and copies it to the
  // public +272 every update (after clamping to the map bounds +1024/+1040).
  for (uint32_t field : {1440u, 272u}) {
    m.WF32(player + field, x);
    m.WF32(player + field + 4, y);
    m.WF32(player + field + 8, z);
  }
  in.Ok(Fmt("teleport: %.2f %.2f %.2f (the next step moves the player from there)", x, y, z));
}

void RefillAmmo(const GuestMemory& m, uint32_t player) {
  const uint32_t weapons = m.U32(player + 1824), count = m.U32(player + 1832);
  if (count > 16 || !GuestMemory::Heap(weapons, count * 1408)) return;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t weapon = weapons + i * 1408;
    const int32_t capacity = int32_t(m.U32(weapon + 800)), left = int32_t(m.U32(weapon + 804));
    if (capacity > 0 && capacity < 100000 && left < capacity) m.W32(weapon + 804, uint32_t(capacity));
  }
}

void GameTick(EngineFrame& frame) {
  // Spread work (spawns, effects).
  std::vector<Job> pending;
  {
    std::lock_guard lock(jobs_mutex);
    pending.swap(jobs);
  }
  if (!pending.empty()) {
    GuestCalls g(*frame.ctx, frame.base);
    std::vector<Job> keep;
    for (auto& job : pending) {
      bool done = true;
      try { done = job.step(g, frame); }
      catch (const std::exception& error) {
        Service::Get().Print(Severity::kError, job.label + ": " + error.what());
      }
      if (!done) keep.push_back(std::move(job));
    }
    std::lock_guard lock(jobs_mutex);
    jobs.insert(jobs.begin(), std::make_move_iterator(keep.begin()), std::make_move_iterator(keep.end()));
  }
  const bool want_god = god.load(std::memory_order_relaxed), release = god_release.exchange(false);
  const bool want_ammo = infinite_ammo.load(std::memory_order_relaxed);
  if (!want_god && !release && !want_ammo) return;
  const GuestMemory m(frame.base);
  const uint32_t player = FindPlayer(m);
  if (!player) return;
  if (want_god) {
    m.W8(player + 625, 1);
    if (m.F32(player + 592) < m.F32(player + 596)) m.WF32(player + 592, m.F32(player + 596));
  } else if (release) {
    m.W8(player + 625, 0);
  }
  if (want_ammo) RefillAmmo(m, player);
}
}  // namespace

bool GameInMission(EngineFrame& frame) {
  const GuestMemory m(frame.base);
  if (!EngineIdle(m)) return false;
  const uint32_t player = FindPlayer(m);
  return player && !m.U8(player + 624);
}

void RegisterGameCommands(Registry& r) {
  static bool ticking = false;
  if (!ticking) { AddEngineTick(GameTick); ticking = true; }
  const auto on_off = [] { return std::vector<std::string>{"on", "off"}; };
  r.Add({.name = "spawn", .usage = "spawn <type|list> [count] [distance | x y z]",
         .help = "Create enemies around the player (a ring at distance, default 30) or at x y z",
         .args = {{.name = "type", .type = ArgType::Choice, .choices = [] { return TypeNames(true); }},
                  {.name = "count", .type = ArgType::Int, .optional = true, .min = 1, .max = 2000},
                  {.name = "distance|x", .type = ArgType::Float, .optional = true, .min = -100000, .max = 100000},
                  {.name = "y", .type = ArgType::Float, .optional = true, .min = -100000, .max = 100000},
                  {.name = "z", .type = ArgType::Float, .optional = true, .min = -100000, .max = 100000}},
         .flags = kEngine | kCheat,
         .validate = [](const std::vector<std::string>& args) -> std::string {
           if (!args.empty() && Lower(args[0]) == "list") return args.size() == 1 ? "" : "spawn list takes no arguments";
           if (args.size() == 4) return "give a distance, or all three of x y z";
           if (args.size() == 3 && *ParseFloat(args[2]) < 0) return "distance must not be negative";
           return {};
         },
         .handler = [](Invocation& in) {
           auto& ctx = *static_cast<ExecContext*>(in.host());
           if (Lower(in.str(0)) != "list" && !GameInMission(*ctx.frame)) {
             in.Error("spawn: not in a mission (start or continue a mission first)");
             return;
           }
           Spawn(in);
         }});
  r.Add({.name = "killall", .usage = "killall [type|class|all|spawned] [remove]",
         .help = "Kill every live invader (faction 1), one type, or only console-spawned ones; 'remove' deletes instead",
         .args = {{.name = "type", .optional = true,
                   .choices = [] { auto names = TypeNames(false); names.push_back("all"); names.push_back("spawned"); names.push_back("remove"); return names; }},
                  {.name = "remove", .optional = true, .choices = [] { return std::vector<std::string>{"remove"}; }}},
         .flags = kEngine | kCheat | kMission, .handler = KillAll});
  r.Add({.name = "god", .usage = "god [on|off]", .help = "Player invulnerable (and kept at full health); no argument toggles",
         .args = {{.name = "state", .type = ArgType::Bool, .optional = true, .choices = on_off}},
         .flags = kEngine | kCheat,
         .handler = [](Invocation& in) {
           const bool on = in.has(0) ? *ParseBool(in.str(0)) : !god.load();
           if (!on && god.load()) god_release = true;
           god = on;
           in.Ok(on ? "god: on" : "god: off");
         }});
  r.Add({.name = "ammo", .usage = "ammo [on|off|refill]", .help = "Infinite ammunition (magazines kept full); 'refill' once",
         .args = {{.name = "state", .optional = true, .choices = [] { return std::vector<std::string>{"on", "off", "refill"}; }}},
         .flags = kEngine | kCheat,
         .validate = [](const std::vector<std::string>& args) -> std::string {
           if (args.empty() || Lower(args[0]) == "refill" || ParseBool(args[0])) return {};
           return "ammo takes on, off or refill";
         },
         .handler = [](Invocation& in) {
           if (in.has(0) && Lower(in.str(0)) == "refill") {
             auto& ctx = *static_cast<ExecContext*>(in.host());
             const GuestMemory m(ctx.frame->base);
             const uint32_t player = FindPlayer(m);
             if (!player) { in.Error("ammo: no player object (not in a mission)"); return; }
             RefillAmmo(m, player);
             in.Ok("ammo: refilled");
             return;
           }
           const bool on = in.has(0) ? *ParseBool(in.str(0)) : !infinite_ammo.load();
           infinite_ammo = on;
           in.Ok(on ? "ammo: infinite" : "ammo: normal");
         }});
  r.Add({.name = "pos", .usage = "pos", .help = "Print the player's position, heading and health",
         .flags = kEngine | kMission, .handler = Pos});
  r.Add({.name = "teleport", .usage = "teleport <x> <y> <z>", .help = "Move the player (see 'pos')",
         .args = {{.name = "x", .type = ArgType::Float, .min = -100000, .max = 100000},
                  {.name = "y", .type = ArgType::Float, .min = -100000, .max = 100000},
                  {.name = "z", .type = ArgType::Float, .min = -100000, .max = 100000}},
         .flags = kEngine | kCheat | kMission, .handler = Teleport});
  r.Add({.name = "destroy", .usage = "destroy <radius R | all | nearest [n] | R> [max]",
         .help = "Collapse buildings around the player (map objects, through their own death branch)",
         .args = {{.name = "mode", .choices = [] { return std::vector<std::string>{"radius", "all", "nearest"}; }},
                  {.name = "value", .type = ArgType::Float, .optional = true, .min = 0, .max = 1e6},
                  {.name = "max", .type = ArgType::Int, .optional = true, .min = 1, .max = 100000}},
         .flags = kEngine | kCheat | kMission,
         .validate = [](const std::vector<std::string>& args) -> std::string {
           const auto mode = Lower(args[0]);
           if (mode == "radius" || mode == "all" || mode == "nearest") return {};
           if (const auto radius = ParseFloat(mode); radius && *radius > 0) return args.size() <= 2 ? "" : "too many arguments";
           return "mode must be radius, all, nearest or a radius";
         },
         .handler = Destroy});
  r.Add({.name = "effects", .usage = "effects <count> [radius] [scale] [per_tick]",
         .help = "Explosion effects around the player (visual only: no damage)",
         .args = {{.name = "count", .type = ArgType::Int, .min = 1, .max = 5000},
                  {.name = "radius", .type = ArgType::Float, .optional = true, .min = 0, .max = 1000},
                  {.name = "scale", .type = ArgType::Float, .optional = true, .min = 0.1, .max = 20},
                  {.name = "per_tick", .type = ArgType::Int, .optional = true, .min = 1, .max = 5000}},
         .flags = kEngine | kCheat | kMission, .handler = Effects});
  r.Add({.name = "timescale", .usage = "timescale [scale]",
         .help = "Simulation speed: engine ticks per second = 60 x scale (0.1 to 4; 1 = normal)",
         .args = {{.name = "scale", .type = ArgType::Float, .optional = true, .min = 0.1, .max = 4.0}},
         .handler = [](Invocation& in) {
           auto& request = edf::native::NativeTimeScaleRequest();
           if (in.has(0)) {
             request.store(in.number(0));
             if (in.number(0) != 1.0) Service::Get().MarkCheat("timescale");
             REXLOG_INFO("Console: timescale {}", in.number(0));
           }
           in.Ok(Fmt("timescale: %.2f (%.0f simulation ticks per second; the game is frame-locked, so this is its speed)",
                     request.load(), 60.0 * request.load()));
         }});
  r.Add({.name = "stats", .usage = "stats [all]",
         .help = "Frame rate and times, simulation step times, object counts per class and faction",
         .args = {{.name = "all", .optional = true, .choices = [] { return std::vector<std::string>{"all"}; }}},
         .flags = kEngine, .handler = Stats});
}

}  // namespace edf::console

// The vanish query's callback, borrowed as the console's collector while "destroy" asks the
// grid for the objects around a point; otherwise the original.
REX_EXTERN(__imp__sub_820B3B10);
REX_HOOK_RAW(sub_820B3B10) {
  if (auto* sink = edf::console::collecting) {
    sink->push_back(ctx.r3.u32);
    return;
  }
  __imp__sub_820B3B10(ctx, base);
}
