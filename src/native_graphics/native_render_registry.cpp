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
constexpr NativeRenderClass Rigid(uint32_t vtable,const char* name,uint32_t instance,uint32_t pose,Cadence cadence=Cadence::Tick) {
  return {.vtable=vtable,.name=name,.cadence=cadence,.lod=Lod::None,.instance=instance,.pose=pose};
}
constexpr NativeRenderClass Character(uint32_t vtable,const char* name,uint8_t attachments=0) {
  return {.vtable=vtable,.name=name,.cadence=Cadence::Tick,.lod=Lod::Character,
    .instance=kCharacterInstance,.pose=kCharacterPose,.attachments=attachments};
}
constexpr uint8_t kPeople=kNativeRenderFace,kSoldier=kNativeRenderFace|kNativeRenderWeapons;
constexpr std::array kClasses{
  // Static map parts: drawn from the existing scene sources (LODs at obj+448+44i).
  NativeRenderClass{.vtable=0x82002760u,.name="clFieldParts",.lod=Lod::FieldParts,.scene_source=true},
  Rigid(0x8200284Cu,"clSky",412,384,Cadence::Frame),                 // 820BB270
  Rigid(0x820028F8u,"clTree",416,400,Cadence::Constructed),          // 820BBA48
  Character(0x820042BCu,"clFriendPeople",kPeople),                   // 820D7448 -> 820DB268
  Character(0x8200452Cu,"clFriendSoldier",kSoldier),                 // 820DEA08
  Character(0x8200479Cu,"clPlayerObject",kSoldier),                  // 820DEA08
  Character(0x82004974u,"clSoldierObject",kSoldier),                 // 820DEA08
  Character(0x82005198u,"clGiantAnt"),                               // 8210E6C0
  Character(0x820052C0u,"clUfoSmall01"),                             // 820E7FB8 -> 8210E6C0
  Character(0x820053F0u,"clUfoCarrier01"),                           // 820EA398 -> 8210E6C0
  // 820EC180: +1100 with pose +1144, then +1172 per record (821C9DA8).
  NativeRenderClass{.vtable=0x820054D0u,.name="clUfoMother01_Dummy",.cadence=Cadence::Tick,.lod=Lod::None,
    .instance=1100,.pose=1144,.attachments=kNativeRenderMotherSpheres},
  Character(0x82005678u,"clAlienTank01"),                            // 820ECCC0
  Character(0x820059D0u,"clGiantSpider"),                            // 8210E6C0
  Character(0x82005E14u,"clAlien4LegTank01"),                        // 820F5630
  Character(0x82005EF8u,"clAlien4LegTank01_Bit"),                    // 820F9928 -> 820FC528
  Character(0x82005F70u,"clAlien4LegTank01_Cannon"),
  Character(0x82006098u,"clAlien4LegTank01_GuttlingGun"),
  Character(0x82006128u,"clAlien4LegTank01_Shield"),
  Character(0x82006290u,"clMonster01"),                              // 8210E6C0
  Character(0x820063C4u,"clMonster01Mech"),                          // 820FFFC0 -> 8210E6C0
  Character(0x820065A4u,"clUfoMother01"),                            // 82100D00
  Character(0x820066CCu,"clUfoMother01_BigCannon"),                  // 82108BE8
  Character(0x820068B0u,"clUfoMother01_Cannon"),
  Character(0x820068ECu,"clUfoMother01_Hatch"),
  Character(0x82006A04u,"clUfoMother01_Panel"),
  Character(0x82006A74u,"clUfoMother01_PartsBase"),
  Character(0x82006AC4u,"clAntHill"),                                // 8210E6C0
  Character(0x82007178u,"clSampleObject"),                           // 8210E6C0
  Rigid(0x82007394u,"clBombAmmo01",908,952),                         // 821152D0
  Rigid(0x820073C4u,"clCentryGun01",932,976),                        // 82115AE8
  Rigid(0x8200740Cu,"clGrenadeAmmo01",904,948),                      // 82117298
  Rigid(0x82007478u,"clMissileAmmo01",892,936),                      // 82118648
  Rigid(0x820077D8u,"clBrokenObject",384,428,Cadence::Frame),        // 8211FAA8
  Rigid(0x820077F4u,"clBrokenPiece",384,428),                        // 82120168
  Rigid(0x82014D4Cu,"clShellCase01",544,588),                        // 8218A658
  Character(0x82015E94u,"C_VehicleBase"),                            // 8219A2D0
  Character(0x82015F24u,"C_Bike"),
  Character(0x82016434u,"C_PowerLoader"),
  Character(0x8202032Cu,"C_Helicopter"),                             // 821E2250
  Character(0x82020674u,"C_Tank"),                                   // 821E5810
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
