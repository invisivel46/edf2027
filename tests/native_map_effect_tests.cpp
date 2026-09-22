#include "native_graphics/native_map_effects.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace edf::native;
namespace {
void Require(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
// Synthetic big-endian guest memory; every access is range checked.
struct Memory {
  mutable std::array<uint8_t,65536> bytes{};
  uint32_t Add(uint32_t at,uint32_t offset) const { return at+offset; }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if(size>bytes.size() || at>bytes.size()-size) throw std::runtime_error("range");
    return bytes.data()+at;
  }
  uint32_t Word(uint32_t at) const {
    const auto* p=Bytes(at,4);
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
  }
  void StoreWord(uint32_t at,uint32_t value) const {
    Bytes(at,4); for(uint32_t i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(24-i*8));
  }
  void StoreHalf(uint32_t at,uint16_t value) const { Bytes(at,2); bytes[at]=uint8_t(value>>8); bytes[at+1]=uint8_t(value); }
};
constexpr uint32_t manager=0x1000,list=manager+NativeMapEffectList::manager_offset;
constexpr uint32_t node_base=0x2000,object_base=0x4000,table_base=0x8000;
uint32_t Node(uint32_t i) { return node_base+i*12; }
uint32_t Object(uint32_t i) { return object_base+i*0x80; }
// Links nodes 0..count-1 at the head the way sub_821A1628 does, so the list
// reads count-1..0; the end marker stays zero as the constructor leaves it.
void Build(const Memory& m,uint32_t count) {
  m.StoreWord(list,0); m.StoreWord(list+4,0); m.StoreWord(list+8,0); m.StoreWord(list+12,0);
  for(uint32_t i=0;i<count;++i) {
    const auto node=Node(i),first=m.Word(list);
    m.StoreWord(node+4,list); m.StoreWord(node,first); m.StoreWord(list,node);
    if(first) m.StoreWord(first+4,node);
    m.StoreWord(node+8,Object(i));
  }
}
void Class(const Memory& m,uint32_t object,uint32_t table,uint32_t mode,uint32_t render,uint16_t hidden=0) {
  m.StoreWord(object,table); m.StoreWord(object+NativeMapEffectObject::mode,mode);
  m.StoreHalf(object+NativeMapEffectObject::hidden,hidden);
  m.StoreWord(table+NativeMapEffectObject::render_slot,render);
}
std::vector<uint32_t> Walk(const Memory& m,const auto& callback) {
  std::vector<uint32_t> visited;
  const auto count=WalkNativeMapEffects(m,list,[&](uint32_t object) { visited.push_back(object); callback(object); });
  Require(count==visited.size(),"walk count differs from the calls it made");
  return visited;
}
void TestOrder() {
  Memory m;
  Build(m,0);
  Require(Walk(m,[](uint32_t) {}).empty(),"empty list produced calls");
  Build(m,4);
  const std::vector<uint32_t> expected{Object(3),Object(2),Object(1),Object(0)};
  Require(Walk(m,[](uint32_t) {})==expected,"walk left head-first link order");
  // A nonzero end marker stops at that node without visiting it.
  m.StoreWord(list+12,Node(1));
  Require((Walk(m,[](uint32_t) {})==std::vector<uint32_t>{Object(3),Object(2)}),"end marker not honoured");
}
void TestMutation() {
  Memory m;
  // Next link is read after the call: unlinking the successor skips it.
  Build(m,4);
  auto seen=Walk(m,[&](uint32_t object) { if(object==Object(3)) m.StoreWord(Node(3),Node(1)); });
  Require((seen==std::vector<uint32_t>{Object(3),Object(1),Object(0)}),"successor unlink during callback not observed");
  // Unlinking the current node (sub_821A1628 zeroes its links) ends the walk.
  Build(m,4);
  seen=Walk(m,[&](uint32_t object) {
    if(object!=Object(2)) return;
    m.StoreWord(Node(3),Node(1)); m.StoreWord(Node(1)+4,Node(3));
    m.StoreWord(Node(2),0); m.StoreWord(Node(2)+4,0);
  });
  Require((seen==std::vector<uint32_t>{Object(3),Object(2)}),"current-node unlink did not end the walk");
  // A node linked at the head during the walk is not visited, and the end
  // marker is read once before the first call.
  Build(m,3);
  seen=Walk(m,[&](uint32_t object) {
    if(object!=Object(2)) return;
    m.StoreWord(Node(5),m.Word(list)); m.StoreWord(Node(5)+8,Object(5)); m.StoreWord(list,Node(5));
    m.StoreWord(list+12,Node(0));
  });
  Require((seen==std::vector<uint32_t>{Object(2),Object(1),Object(0)}),"head insert or end rewrite changed the walk");
  // The object is read before the call: rewriting the current node's object
  // takes effect only if the node is visited again.
  Build(m,2);
  seen=Walk(m,[&](uint32_t object) { if(object==Object(1)) m.StoreWord(Node(1)+8,Object(7)); });
  Require((seen==std::vector<uint32_t>{Object(1),Object(0)}),"object slot reread after the call");
  // Relinking the current node's next to an earlier node revisits it.
  Build(m,3);
  uint32_t calls=0;
  seen=Walk(m,[&](uint32_t object) {
    if(object==Object(1)) m.StoreWord(Node(1),calls++==0?Node(2):Node(0));
  });
  Require((seen==std::vector<uint32_t>{Object(2),Object(1),Object(2),Object(1),Object(0)}),"relinked next not followed");
}
void TestCensus() {
  Memory m;
  Build(m,6);
  constexpr uint32_t glass=0x82012cc8,building=0x820026bc;
  const uint32_t glass_table=table_base,building_table=table_base+0x100,unknown_table=table_base+0x200;
  Class(m,Object(0),glass_table,0,0x8217D6E0);
  Class(m,Object(1),glass_table,0,0x8217D6E0,1);
  Class(m,Object(2),glass_table,0,0x8217D6E0);
  Class(m,Object(3),building_table,1,0x820B2670);
  Class(m,Object(4),building_table,2,0x820B2670);
  Class(m,Object(5),unknown_table,1,0x82001234);
  const auto before=m.bytes;
  NativeMapEffectCensus census;
  Require(census.Record(m,list)==6 && census.Record(m,list)==6,"census node count");
  Require(m.bytes==before,"census wrote guest memory");
  Require(census.walks()==2 && census.objects()==12 && census.classes()==4 && !census.truncated(),"census totals");
  const auto rows=census.Top(16);
  Require(rows.size()==4,"census row count");
  Require(rows[0].key==NativeMapEffectClass{glass_table,0,0x8217D6E0} && rows[0].count==6 && rows[0].hidden==2,"census top class");
  Require(std::abs(rows[0].percent-50.0)<1e-9,"census share");
  // Equal counts keep (vtable, mode, render) order.
  Require(rows[1].key==NativeMapEffectClass{building_table,1,0x820B2670} && rows[1].count==2,"census tie order 1");
  Require(rows[2].key==NativeMapEffectClass{building_table,2,0x820B2670} && rows[2].count==2,"census tie order 2");
  Require(rows[3].key==NativeMapEffectClass{unknown_table,1,0x82001234},"census tie order 3");
  double total=0; for(const auto& row:rows) total+=row.percent;
  Require(std::abs(total-100.0)<1e-9,"census shares do not sum to 100");
  Require(census.Top(2).size()==2,"census top limit");
  // The walk cap stops a cyclic list and is reported.
  m.StoreWord(Node(0),Node(5));
  NativeMapEffectCensus capped;
  Require(capped.Record(m,list,20)==20 && capped.truncated()==1,"census cap");
  census.Reset();
  Require(!census.walks() && !census.objects() && !census.classes() && census.Top(16).empty(),"census reset");
  Require(std::string(NativeMapEffectClassName(glass))=="clEffectGlass" &&
          std::string(NativeMapEffectClassName(building))=="clBuilding" && !NativeMapEffectClassName(0x82001234),
          "class name lookup");
}
}
int main() {
  try {
    TestOrder(); TestMutation(); TestCensus();
  } catch(const std::exception& error) {
    std::cerr<<"native map effect tests failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native map effect tests passed\n";
  return 0;
}
