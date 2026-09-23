#include "native_graphics/native_coverage_census.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures=0;
void Check(bool ok,const std::string& what) {
  if(!ok) { ++failures; std::cerr<<"FAILED: "<<what<<"\n"; }
}
using edf::native::NativeCoverageCensus;
using edf::native::NativeCoverageItem;
using edf::native::NativeCoverageMark;
using edf::native::NativeCoverageOwner;
using edf::native::NativeCoverageStatus;
const NativeCoverageItem* Find(const std::vector<NativeCoverageItem>& items,std::string_view name,std::string_view reason,
    NativeCoverageStatus status) {
  for(const auto& item:items) if(item.name==name && item.reason==reason && item.status==status) return &item;
  return nullptr;
}

// Observations fold per frame: frames counts the frames an item appeared in,
// objects the sum, peak the most in one frame, first/last seen the frame times.
void TestAggregation() {
  NativeCoverageCensus census;
  census.Add(NativeCoverageStatus::Uncovered,0x82001000,"clThing","no_builder",2);
  census.Add(NativeCoverageStatus::Uncovered,0x82001000,"clThing","no_builder",3);
  census.Add(NativeCoverageStatus::Covered,0x82005198,{},"models",10);
  census.Add(NativeCoverageStatus::Covered,0x82005198,{},"models",0);  // zero: not an observation
  census.EndFrame(100.0);
  census.Add(NativeCoverageStatus::Uncovered,0x82001000,"clThing","no_builder",1);
  census.EndFrame(101.5);
  census.EndFrame(102.0);  // Nothing observed.
  census.Add(NativeCoverageStatus::Covered,0x82005198,{},"models",4);
  census.EndFrame(103.0);
  const auto items=census.Items();
  Check(items.size()==2,"two distinct items");
  const auto* thing=Find(items,"clThing","no_builder",NativeCoverageStatus::Uncovered);
  Check(thing && thing->frames==2 && thing->objects==6 && thing->peak==5,"uncovered item frames/objects/peak");
  Check(thing && thing->first_seen==0.0 && thing->last_seen==1.5,"first and last seen from the origin");
  Check(thing && thing->vtable==0x82001000,"vtable kept");
  // An empty name resolves through the class tables.
  const auto* ant=Find(items,"clGiantAnt","models",NativeCoverageStatus::Covered);
  Check(ant && ant->frames==2 && ant->objects==14 && ant->peak==10 && ant->first_seen==0.0 && ant->last_seen==3.0,"covered item");
  const auto totals=census.Totals();
  Check(totals.frames==4 && totals.items==2 && totals.covered_items==1 && totals.uncovered_items==1,"totals: items");
  Check(totals.covered_objects==14 && totals.uncovered_objects==6 && totals.seconds==3.0,"totals: objects and seconds");
  Check(totals.coverage()>69.9999 && totals.coverage()<70.0001,"coverage is covered over covered plus uncovered");
}
// The same class under different statuses or reasons are different items;
// parity is left out of the coverage figure.
void TestKeysAndParity() {
  NativeCoverageCensus census;
  census.Add(NativeCoverageStatus::Covered,0x82000010,"clX","effects",3);
  census.Add(NativeCoverageStatus::Parity,0x82000010,"clX","undrawn_key",100);
  census.Add(NativeCoverageStatus::Uncovered,0x82000010,"clX","declined",1);
  census.Add(NativeCoverageStatus::Uncovered,0x82000010,"clX","other_reason",1);
  census.EndFrame(0);
  const auto totals=census.Totals();
  Check(totals.items==4 && totals.parity_items==1 && totals.parity_objects==100,"four keys, one parity");
  Check(totals.coverage()==60.0,"parity excluded from coverage");
  NativeCoverageCensus empty;
  Check(empty.Totals().coverage()==100.0,"no objects: full coverage");
}
// Marks aggregate by identity; a nonzero slot is the item's detail.
void TestMarks() {
  NativeCoverageCensus census;
  std::vector<NativeCoverageMark> marks;
  for(int i=0;i<5;++i) marks.push_back({NativeCoverageStatus::Covered,0x82005198,nullptr,"models"});
  marks.push_back({NativeCoverageStatus::Uncovered,0x82001234,nullptr,"world_list_unhandled",0x820D4850});
  marks.push_back({NativeCoverageStatus::Uncovered,0x82001234,nullptr,"world_list_unhandled",0x820D4850});
  marks.push_back({NativeCoverageStatus::Uncovered,0,"pass:models","stale"});
  census.Add(marks);
  census.EndFrame(0);
  census.Add(marks);
  census.EndFrame(1);
  const auto items=census.Items();
  const auto* ant=Find(items,"clGiantAnt","models",NativeCoverageStatus::Covered);
  Check(ant && ant->objects==10 && ant->peak==5 && ant->frames==2,"marks counted per frame");
  const auto* manager=Find(items,"unknown","world_list_unhandled",NativeCoverageStatus::Uncovered);
  Check(manager && manager->objects==4 && manager->detail=="slot=0x820D4850","slot detail");
  const auto* stale=Find(items,"pass:models","stale",NativeCoverageStatus::Uncovered);
  Check(stale && stale->objects==2 && stale->detail.empty(),"named mark without slot");
}
// A population counts into every frame while it is set.
void TestPopulation() {
  NativeCoverageCensus census;
  census.SetPopulation("registry",{{NativeCoverageStatus::Uncovered,0x82009990,{},"registry_unknown_class","slot=0x82123456",7}});
  census.EndFrame(0);
  census.EndFrame(1);
  census.SetPopulation("registry",{{NativeCoverageStatus::Uncovered,0x82009990,{},"registry_unknown_class","slot=0x82123456",2}});
  census.EndFrame(2);
  census.SetPopulation("registry",{});
  census.EndFrame(3);
  const auto items=census.Items();
  const auto* item=Find(items,"unknown","registry_unknown_class",NativeCoverageStatus::Uncovered);
  Check(item && item->frames==3 && item->objects==16 && item->peak==7 && item->last_seen==2.0,"population per frame");
  Check(item && item->detail=="slot=0x82123456","population detail");
  // Two sources add up in one frame.
  NativeCoverageCensus two;
  two.SetPopulation("a",{{NativeCoverageStatus::Parity,0x1,"x","r","",1}});
  two.SetPopulation("b",{{NativeCoverageStatus::Parity,0x1,"x","r","",2}});
  two.Add(NativeCoverageStatus::Parity,0x1,"x","r",4);
  two.EndFrame(0);
  const auto merged=two.Items();
  Check(merged.size()==1 && merged[0].objects==7 && merged[0].peak==7,"populations and adds merge per frame");
}
// Order: uncovered, parity, covered; each by objects descending.
void TestOrderAndSummary() {
  NativeCoverageCensus census;
  census.Add(NativeCoverageStatus::Covered,0x82005198,{},"models",50);
  census.Add(NativeCoverageStatus::Uncovered,0,"pass:sky","declined",1,"sky pass has no program");
  census.Add(NativeCoverageStatus::Uncovered,0x82001000,"clThing","effect_no_builder",9,"slot=0x82000000");
  census.Add(NativeCoverageStatus::Parity,0,"effect_object","undrawn_key",3);
  census.EndFrame(10.0);
  const auto items=census.Items();
  Check(items.size()==4 && items[0].name=="clThing" && items[1].name=="pass:sky" && items[2].status==NativeCoverageStatus::Parity &&
        items[3].status==NativeCoverageStatus::Covered,"item order");
  const auto lines=census.Summary("final");
  Check(lines.size()==5,"summary: totals plus one line per item");
  Check(lines[0]=="Native coverage summary: kind=final seconds=0.0 frames=1 items=4 covered_items=1 uncovered_items=2 parity_items=1 "
        "covered_objects=50 uncovered_objects=10 parity_objects=3 coverage=83.333%","totals line: "+lines[0]);
  Check(lines[1]=="Native coverage: uncovered class=clThing vtable=0x82001000 reason=effect_no_builder frames=1 objects=9 peak=9 "
        "first_seen=0.0s last_seen=0.0s detail=slot=0x82000000","item line: "+lines[1]);
  // Details are tokens: whitespace becomes '_'.
  Check(lines[2]=="Native coverage: uncovered class=pass:sky vtable=0x00000000 reason=declined frames=1 objects=1 peak=1 "
        "first_seen=0.0s last_seen=0.0s detail=sky_pass_has_no_program","decline line: "+lines[2]);
  Check(lines[4].find("Native coverage: covered class=clGiantAnt vtable=0x82005198 reason=models frames=1 objects=50")==0,"covered line");
  Check(lines[3].find("detail=-")!=std::string::npos,"no detail prints '-'");
}
// Poll: nothing before the first frame or within the interval.
void TestPoll() {
  NativeCoverageCensus census;
  Check(census.Poll(0,30).empty(),"no summary before the first frame");
  census.Add(NativeCoverageStatus::Covered,0x82005198,{},"models");
  census.EndFrame(1000.0);
  Check(census.Poll(1010.0,30).empty(),"within the interval");
  const auto lines=census.Poll(1030.0,30);
  Check(!lines.empty() && lines[0].find("Native coverage summary: kind=window ")==0,"window summary at the interval");
  Check(census.Poll(1040.0,30).empty(),"interval restarts at the summary");
  Check(!census.Poll(1060.5,30).empty(),"next interval");
  census.Reset();
  Check(census.Items().empty() && census.Totals().frames==0 && census.Poll(5000,1).empty(),"reset");
}
void TestNamesAndOwners() {
  using namespace edf::native;
  Check(NativeCoverageClassName(0x82005198)=="clGiantAnt","registry class name");
  Check(NativeCoverageClassName(0x82002744)=="clElectricWire","map-effect class name");
  Check(NativeCoverageClassName(0x82012C78)=="clEffectEtc02","effect class name");
  Check(NativeCoverageClassName(0x8200427C)=="clGameObject_Manager","world-list manager name");
  Check(NativeCoverageClassName(0x82FFFFF0)=="unknown","unknown class");
  Check(NativeCoverageToken("a b\tc")=="a_b_c" && NativeCoverageToken("")=="-","tokens");
  Check(NativeCoverageSlotOwner(0x82005198,0x8210E6C0)==NativeCoverageOwner::Models,"registry class: models");
  Check(NativeCoverageSlotOwner(0x8200284C,0x820BB270)==NativeCoverageOwner::MapEffects,"clSky: the sky pass, not the models");
  Check(NativeCoverageSlotOwner(0x82002760,0x820B2670)==NativeCoverageOwner::StaticWorld,"clFieldParts: the static world, not the models");
  Check(NativeCoverageSlotOwner(0x82002744,0x820B8D28)==NativeCoverageOwner::MapEffects,"wire: map effects");
  Check(NativeCoverageSlotOwner(0x820124DC,0)==NativeCoverageOwner::MapEffects,"grass: map effects");
  Check(NativeCoverageSlotOwner(0x820026BC,0x820B2670)==NativeCoverageOwner::StaticWorld,"LOD slot 4: static world");
  Check(NativeCoverageSlotOwner(0x82002830,0x820BAF90)==NativeCoverageOwner::StaticWorld,"fixed slot 4: static world");
  Check(NativeCoverageSlotOwner(0x82012CC8,0x8217D6E0)==NativeCoverageOwner::Effects,"effect builder: effects");
  Check(NativeCoverageSlotOwner(0x82000000,0x8252B718)==NativeCoverageOwner::Empty,"bare blr: empty");
  Check(NativeCoverageSlotOwner(0x82000000,0x82123456)==NativeCoverageOwner::None,"anything else: none");
  Check(std::string(NativeCoverageWorldListPass(0x820072D4))=="effects" && std::string(NativeCoverageWorldListPass(0x82002624))=="static_world" &&
        std::string(NativeCoverageWorldListPass(0x8200427C))=="models" && std::string(NativeCoverageWorldListPass(0x82003FBC))=="models" &&
        std::string(NativeCoverageWorldListPass(0x82002214))=="sky" && !NativeCoverageWorldListPass(0x82001234),"world-list passes");
  Check(std::string(NativeCoverageStatusName(NativeCoverageStatus::Parity))=="parity","status names");
}
// world+372 (NativeCoverageMapListMarks): the members of the untracked map
// list, read live, named by class. Models' classes are the models pass's to
// count; a bare-blr slot 4 is parity; any other class is uncovered with its
// slot 4. An empty list marks nothing; a list that never reaches its end throws.
void TestMapList() {
  using edf::native::NativeCoverageMapListMarks;
  struct Reader {
    std::map<uint32_t,uint32_t> words;
    uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
    uint32_t Word(uint32_t address) const {
      const auto found=words.find(address);
      if(found==words.end()) throw std::runtime_error("unmapped");
      return found->second;
    }
  };
  constexpr uint32_t world=0x40001000,list=world+edf::native::kNativeCoverageMapList,end=0x40002000;
  const std::map<uint32_t,std::pair<uint32_t,uint32_t>> classes={
    {0x5000,{0x82005198,0x8210E6C0}},  // clGiantAnt: models
    {0x5100,{0x82004474,0x8252B718}},  // bare blr: parity
    {0x5200,{0x8200284C,0x820BB270}},  // clSky: map effects, not walked from here
    {0x5300,{0x82000000,0x82123456}}}; // no pass
  const auto class_of=[&](uint32_t object) { return classes.at(object); };
  Reader reader;
  reader.words[list]=end; reader.words[list+12]=end;
  std::vector<NativeCoverageMark> marks;
  NativeCoverageMapListMarks(reader,list,class_of,marks);
  Check(marks.empty(),"map list: an empty list marks nothing");
  // Nodes {+0 next, +8 object}, in list order.
  const uint32_t nodes[]={0x6000,0x6100,0x6200,0x6300};
  reader.words[list]=nodes[0];
  uint32_t object=0x5000;
  for(size_t i=0;i<4;++i,object+=0x100) {
    reader.words[nodes[i]]=i+1<4?nodes[i+1]:end;
    reader.words[nodes[i]+8]=object;
  }
  NativeCoverageMapListMarks(reader,list,class_of,marks);
  Check(marks.size()==3,"map list: one mark per member other passes do not count");
  if(marks.size()==3) {
    Check(marks[0].status==NativeCoverageStatus::Parity && marks[0].vtable==0x82004474 &&
      std::string(marks[0].reason)=="empty_slot4" && marks[0].slot==0x8252B718,"map list: bare blr member is parity");
    Check(marks[1].status==NativeCoverageStatus::Uncovered && marks[1].vtable==0x8200284C &&
      std::string(marks[1].reason)=="static_map_list_member" && marks[1].slot==0x820BB270,"map list: clSky member is uncovered");
    Check(marks[2].status==NativeCoverageStatus::Uncovered && marks[2].vtable==0x82000000 &&
      std::string(marks[2].reason)=="static_map_list_member","map list: unowned member is uncovered");
  }
  // A cycle, and a null link, never reach the end.
  reader.words[nodes[3]]=nodes[0];
  bool threw=false;
  try { marks.clear(); NativeCoverageMapListMarks(reader,list,class_of,marks,16); } catch(const std::exception&) { threw=true; }
  Check(threw,"map list: a cycle throws");
  reader.words[nodes[3]]=0;
  threw=false;
  try { marks.clear(); NativeCoverageMapListMarks(reader,list,class_of,marks); } catch(const std::exception&) { threw=true; }
  Check(threw,"map list: a null link throws");
}
// Adds from several threads land in the frame whole.
void TestThreads() {
  NativeCoverageCensus census;
  std::vector<std::thread> threads;
  for(int t=0;t<4;++t) threads.emplace_back([&] {
    for(int i=0;i<1000;++i) census.Add(NativeCoverageStatus::Covered,0x82005198,{},"models");
  });
  for(auto& thread:threads) thread.join();
  census.EndFrame(0);
  const auto items=census.Items();
  Check(items.size()==1 && items[0].objects==4000 && items[0].peak==4000,"concurrent adds");
}
}

int main() {
  TestAggregation();
  TestKeysAndParity();
  TestMarks();
  TestPopulation();
  TestOrderAndSummary();
  TestPoll();
  TestNamesAndOwners();
  TestMapList();
  TestThreads();
  if(failures) { std::cerr<<failures<<" failure(s)\n"; return 1; }
  std::cout<<"native coverage census tests passed\n";
  return 0;
}
