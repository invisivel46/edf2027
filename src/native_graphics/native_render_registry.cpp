#include "native_render_registry.h"
#include <algorithm>
#include <array>

namespace edf::native {
namespace {
using Cadence=NativeRenderPoseCadence;
using Lod=NativeRenderLodKind;
constexpr uint32_t kCharacterInstance=1168,kCharacterPose=1088;
// Classes whose slot 4 reaches 821C9C20 (directly, through 8210AE48, or through
// 820DB268 / 820DE790 for the people), from the edfdb vtables/classes tables.
// Offsets are the r3/r4 each slot-4 body passes to 821C9C20; every 8210AE48
// call site passes r3=this. Cadence: Constructed = pose built once in the
// constructor (clTree 820BB460), Frame = built inside slot 4 itself (never in
// full-frame mode), Tick = built by slot 2 from the 821A4DE8 scene+100 walk.
// A Frame class with a root (Posed) has that slot-4 pose built by the
// registry from the tick's object fields; clSky's is the sky pass's.
// Audited against every vtable whose slot 4 reaches 821C9C20 / 821C9DA8 /
// 821B2C28 through direct calls (these 39 plus clFieldParts; clRock's
// 820BAF90 publishes a static group, drawn by the static world pass): each
// row also carries what its slot 4 stores through 821A1730 before a draw
// (constants) and the models it draws after the LOD model (weapon groups,
// parts), in guest order.
constexpr NativeRenderClass Rigid(uint32_t vtable,const char* name,uint32_t instance,uint32_t pose,Cadence cadence=Cadence::Tick) {
  return {.vtable=vtable,.name=name,.cadence=cadence,.lod=Lod::None,.instance=instance,.pose=pose};
}
constexpr NativeRenderClass Posed(uint32_t vtable,const char* name,uint32_t instance,uint32_t pose,uint32_t root) {
  return {.vtable=vtable,.name=name,.cadence=Cadence::Frame,.lod=Lod::None,.instance=instance,.pose=pose,.frame_root=root};
}
constexpr NativeRenderClass Character(uint32_t vtable,const char* name,uint8_t attachments=0) {
  return {.vtable=vtable,.name=name,.cadence=Cadence::Tick,.lod=Lod::Character,
    .instance=kCharacterInstance,.pose=kCharacterPose,.attachments=attachments};
}
constexpr uint8_t kPeople=kNativeRenderFace,kSoldier=kNativeRenderFace|kNativeRenderWeapons;
using Source=NativeRenderConstantSource;
// A slot 4 that stores two pool constants (g_Highlight, then g_Time: the
// handles its constructor looked up, the second at handle+4) from two object
// float4s before 8210AE48; the offsets are its r4/r5 loads.
constexpr NativeRenderClass Highlighted(uint32_t vtable,const char* name,uint32_t handle,uint32_t highlight,uint32_t time) {
  auto type=Character(vtable,name);
  type.constants={Source{handle,highlight},Source{handle+4,time}};
  return type;
}
// 8219A2D0 (C_VehicleBase and the classes sharing it): 8210AE48, then
// 82199DD8(obj+1824): elements at +36, count +44.
// None of them stores a pool constant: C_PowerLoader's powerloader.Dxm is
// c_Mech01 and reads g_Highlight/g_Time as the previous writer left them
// (the models pass's pool carry).
constexpr NativeRenderClass Vehicle(uint32_t vtable,const char* name) {
  auto type=Character(vtable,name);
  type.weapon_groups[0]={1824,36,44};
  return type;
}
constexpr std::array kClasses{
  // Static map parts: drawn from the existing scene sources (LODs at obj+448+44i).
  NativeRenderClass{.vtable=0x82002760u,.name="clFieldParts",.lod=Lod::FieldParts,.scene_source=true},
  // 820BB270; drawn by the sky pass (native_full_frame_sky.h), never by the models pass.
  NativeRenderClass{.vtable=0x8200284Cu,.name="clSky",.cadence=Cadence::Frame,.lod=Lod::None,.instance=412,.pose=384,.other_pass=true},
  Rigid(0x820028F8u,"clTree",416,400,Cadence::Constructed),          // 820BBA48
  Character(0x820042BCu,"clFriendPeople",kPeople),                   // 820D7448 -> 820DB268
  Character(0x8200452Cu,"clFriendSoldier",kSoldier),                 // 820DEA08
  Character(0x8200479Cu,"clPlayerObject",kSoldier),                  // 820DEA08
  Character(0x82004974u,"clSoldierObject",kSoldier),                 // 820DEA08
  Character(0x82005198u,"clGiantAnt"),                               // 8210E6C0
  Highlighted(0x820052C0u,"clUfoSmall01",2288,2336,2352),            // 820E7FB8 -> 8210E6C0
  Highlighted(0x820053F0u,"clUfoCarrier01",1648,1696,1712),          // 820EA398 -> 8210E6C0
  // 820EC180: +1100 with pose +1144, then +1172 per record (821C9DA8).
  NativeRenderClass{.vtable=0x820054D0u,.name="clUfoMother01_Dummy",.cadence=Cadence::Tick,.lod=Lod::None,
    .instance=1100,.pose=1144,.attachments=kNativeRenderMotherSpheres},
  // 820ECCC0: constants, 8210AE48, then two 820F01E8 turrets.
  NativeRenderClass{.vtable=0x82005678u,.name="clAlienTank01",.cadence=Cadence::Tick,.lod=Lod::Character,
    .instance=kCharacterInstance,.pose=kCharacterPose,.constants={Source{5024,5072},Source{5028,5088}},
    .parts={.base=1920,.count=2,.stride=1488,.instance=108,.pose=152,.constants={Source{1120,1168},Source{1124,1184}}}},
  Character(0x820059D0u,"clGiantSpider"),                            // 8210E6C0
  Highlighted(0x82005E14u,"clAlien4LegTank01",2384,2432,2448),       // 820F5630
  // 820F9928 -> 820FC528: g_Highlight +1296 <- +1264, g_Time +1300 <- +1280.
  Highlighted(0x82005EF8u,"clAlien4LegTank01_Bit",1296,1264,1280),
  Highlighted(0x82005F70u,"clAlien4LegTank01_Cannon",1296,1264,1280),
  Highlighted(0x82006098u,"clAlien4LegTank01_GuttlingGun",1296,1264,1280),
  Highlighted(0x82006128u,"clAlien4LegTank01_Shield",1296,1264,1280),
  Character(0x82006290u,"clMonster01"),                              // 8210E6C0
  Highlighted(0x820063C4u,"clMonster01Mech",1984,2032,2048),         // 820FFFC0 -> 8210E6C0
  Highlighted(0x820065A4u,"clUfoMother01",1248,1296,1312),           // 82100D00
  Highlighted(0x820066CCu,"clUfoMother01_BigCannon",1360,1408,1424), // 82108BE8
  Highlighted(0x820068B0u,"clUfoMother01_Cannon",1360,1408,1424),
  Highlighted(0x820068ECu,"clUfoMother01_Hatch",1360,1408,1424),
  Highlighted(0x82006A04u,"clUfoMother01_Panel",1360,1408,1424),
  Highlighted(0x82006A74u,"clUfoMother01_PartsBase",1360,1408,1424),
  Character(0x82006AC4u,"clAntHill"),                                // 8210E6C0
  Character(0x82007178u,"clSampleObject"),                           // 8210E6C0
  Rigid(0x82007394u,"clBombAmmo01",908,952),                         // 821152D0
  Rigid(0x820073C4u,"clCentryGun01",932,976),                        // 82115AE8
  Rigid(0x8200740Cu,"clGrenadeAmmo01",904,948),                      // 82117298
  Rigid(0x82007478u,"clMissileAmmo01",892,936),                      // 82118648
  // 8211FAA8: +712 = +708, 821C8C58(+400,+640), 821C9478(+400,+428), 821C9C20(+384,+428).
  Posed(0x820077D8u,"clBrokenObject",384,428,640),
  Rigid(0x820077F4u,"clBrokenPiece",384,428),                        // 82120168
  Rigid(0x82014D4Cu,"clShellCase01",544,588),                        // 8218A658
  Vehicle(0x82015E94u,"C_VehicleBase"),                              // 8219A2D0
  Vehicle(0x82015F24u,"C_Bike"),
  Vehicle(0x82016434u,"C_PowerLoader"),
  // 821E2250: 8210AE48, then 821E4E90(obj+1536) and 821E4E90(obj+1580).
  NativeRenderClass{.vtable=0x8202032Cu,.name="C_Helicopter",.cadence=Cadence::Tick,.lod=Lod::Character,
    .instance=kCharacterInstance,.pose=kCharacterPose,.weapon_groups={NativeRenderWeaponGroup{1536,28,36},NativeRenderWeaponGroup{1580,28,36}}},
  // 821E5810: 8210AE48, 821E4E90(obj+2208), then 821E7C50 on the treads +4216, +4308.
  NativeRenderClass{.vtable=0x82020674u,.name="C_Tank",.cadence=Cadence::Tick,.lod=Lod::Character,
    .instance=kCharacterInstance,.pose=kCharacterPose,.weapon_groups={NativeRenderWeaponGroup{2208,28,36}},
    .parts={.base=4216,.count=2,.stride=92,.instance=0,.pose=72,.constants={Source{88,60,true}}}},
};
static_assert(std::is_sorted(kClasses.begin(),kClasses.end(),[](const auto& a,const auto& b) { return a.vtable<b.vtable; }),
  "native render class table must stay sorted by vtable");
static_assert(std::adjacent_find(kClasses.begin(),kClasses.end(),[](const auto& a,const auto& b) { return a.vtable==b.vtable; })==kClasses.end(),
  "native render class table has a duplicate vtable");
}
std::span<const NativeRenderClass> NativeRenderClasses() { return kClasses; }
const NativeRenderClass* FindNativeRenderClass(uint32_t vtable) {
  const auto found=std::lower_bound(kClasses.begin(),kClasses.end(),vtable,[](const NativeRenderClass& type,uint32_t key) { return type.vtable<key; });
  return found!=kClasses.end() && found->vtable==vtable?&*found:nullptr;
}
// Leaked: destructor hooks may still arrive during static destruction.
NativeRenderRegistry& RenderRegistry() { static auto* value=new NativeRenderRegistry; return *value; }
}
