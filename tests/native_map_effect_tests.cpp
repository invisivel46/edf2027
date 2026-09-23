#include "native_graphics/native_map_effects.h"
#include <emmintrin.h>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <set>
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
  // The map-effect members are named; the Effect layer's classes are not.
  Require(!NativeMapEffectClassName(glass) && !NativeMapEffectClassName(0x82012c78) && !NativeMapEffectClassName(0x82012cec) &&
          std::string(NativeMapEffectClassName(0x8200284c))=="clSky" &&
          std::string(NativeMapEffectClassName(0x82002744))=="clElectricWire" &&
          std::string(NativeMapEffectClassName(0x820124dc))=="clGrassMap" &&
          std::string(NativeMapEffectClassName(building))=="clBuilding" && !NativeMapEffectClassName(0x82001234),
          "class name lookup");
}

// ---- clElectricWire: the native builder against a transcription of the guest ----
// Heap at [0, 0x40000) and the image's .rdata/.data from 0x82000000, big-endian.
struct ImageMemory {
  static constexpr uint32_t heap=0x40000,image=0x82000000u,image_size=0x580000;
  mutable std::vector<uint8_t> bytes=std::vector<uint8_t>(heap+image_size);
  static uint32_t Add(uint32_t at,uint32_t offset) { return at+offset; }
  static size_t Map(uint32_t at,size_t size) {
    if(at<heap && size<=heap-at) return at;
    if(at>=image && at-image<image_size && size<=image_size-(at-image)) return heap+(at-image);
    throw std::runtime_error("image memory range");
  }
  const uint8_t* Bytes(uint32_t at,size_t size) const { return bytes.data()+Map(at,size); }
  uint32_t Word(uint32_t at) const {
    const auto* p=Bytes(at,4);
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
  }
  void StoreWord(uint32_t at,uint32_t value) const {
    auto* p=bytes.data()+Map(at,4); for(uint32_t i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8));
  }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
  void StoreByte(uint32_t at,uint8_t value) const { bytes[Map(at,1)]=value; }
};
// Register helpers in the recompiled bodies' terms: lfs/stfs, and every
// single-precision result rounded through float.
double Lfs(const ImageMemory& m,uint32_t at) { return double(std::bit_cast<float>(m.Word(at))); }
void Stfs(const ImageMemory& m,uint32_t at,double value) { m.StoreFloat(at,float(value)); }
double F(double value) { return double(float(value)); }
void Copy16(const ImageMemory& m,uint32_t from,uint32_t to) {  // ld/ld then std/std
  const uint32_t w[4]{m.Word(from),m.Word(from+4),m.Word(from+8),m.Word(from+12)};
  for(uint32_t i=0;i<4;++i) m.StoreWord(to+i*4,w[i]);
}
// sub_821B0258 (r3 out, r4 point, r5 matrix).
void Guest821B0258(const ImageMemory& m,uint32_t r3,uint32_t r4,uint32_t r5) {
  double f0=Lfs(m,r4+8),f8=Lfs(m,r5+36),f7=Lfs(m,r5+40);
  f8=F(f8*f0); f7=F(f7*f0);
  double f13=Lfs(m,r4+4),f6=Lfs(m,r5+20),f5=Lfs(m,r5+24),f12=Lfs(m,r4+0),f9=Lfs(m,r5+0);
  f9=F(f9*f12);
  double f11=Lfs(m,r5+4),f10=Lfs(m,r5+8),f4=Lfs(m,r5+52),f3=Lfs(m,r5+56);
  f8=F(std::fma(f6,f13,f8));
  f7=F(std::fma(f5,f13,f7));
  f11=F(std::fma(f11,f12,f8));
  f8=Lfs(m,r5+16);
  f10=F(std::fma(f10,f12,f7));
  f13=F(std::fma(f8,f13,f9));
  f9=Lfs(m,r5+48);
  f12=F(f11+f4);
  f11=F(f10+f3);
  f10=Lfs(m,r5+32);
  f0=F(std::fma(f0,f10,f13));
  Stfs(m,r3+4,f12);
  Stfs(m,r3+8,f11);
  f0=F(f0+f9);
  Stfs(m,r3+0,f0);
}
// sub_821C2FD0 (r3 frustum, r4 centre, f1 radius): r3 of the return.
bool Guest821C2FD0(const ImageMemory& m,uint32_t r3,uint32_t r4,double f1) {
  double f13=Lfs(m,r3+96);
  f13=F(f13-f1);
  double f0=Lfs(m,r4+8);
  if(f0<f13) return false;            // bge cr6 (not lt)
  f13=Lfs(m,r3+100);
  f13=F(f13+f1);
  if(f0>f13) return false;
  double f12=Lfs(m,r3+40);
  f12=F(f12*f0);
  f13=Lfs(m,r4+0);
  double f11=Lfs(m,r3+32);
  f12=F(std::fma(f11,f13,f12));
  if(f12>f1) return false;
  f12=Lfs(m,r3+56);
  f12=F(f12*f0);
  f11=Lfs(m,r3+48);
  f13=F(std::fma(f11,f13,f12));
  if(f13>f1) return false;
  f12=Lfs(m,r3+72);
  f12=F(f12*f0);
  f13=Lfs(m,r4+4);
  f11=Lfs(m,r3+68);
  f12=F(std::fma(f11,f13,f12));
  if(f12>f1) return false;
  f12=Lfs(m,r3+88);
  f0=F(f12*f0);
  f12=Lfs(m,r3+84);
  f0=F(std::fma(f12,f13,f0));
  return !(f0>f1);                     // bgtlr with r3 0, else r3 1
}
// sub_821B0320 (r3 vector, f1 length).
void Guest821B0320(const ImageMemory& m,uint32_t r3,double f1) {
  double f12=Lfs(m,r3+4);
  double f0=F(f12*f12);
  double f13=Lfs(m,r3+0),f11=Lfs(m,r3+8),f10=Lfs(m,0x820009A4);
  f0=F(std::fma(f13,f13,f0));
  f0=F(std::fma(f11,f11,f0));
  if(f0==f10) { Stfs(m,r3+8,f10); Stfs(m,r3+4,f10); Stfs(m,r3+0,f10); return; }
  f0=F(std::sqrt(f0));
  f0=F(f1/f0);
  f13=F(f13*f0); Stfs(m,r3+0,f13);
  f13=F(f12*f0); Stfs(m,r3+4,f13);
  f0=F(f11*f0); Stfs(m,r3+8,f0);
}
// What 821A7B58 is called with (r3..r8), and the vertex bytes at r5.
struct GuestColourDraw { uint32_t effect=0,primitive=0,primitives=0,blend=0,depth=0; std::vector<uint8_t> vertices; };
// sub_821A7E08 (r3 effect, r4 points, r5 count, f1 width, r7 colour, r8, r9),
// its frame at `stack` (r1 after stwu -3520).
void Guest821A7E08(const ImageMemory& m,uint32_t r3,uint32_t r4,int32_t r5,double f1,uint32_t r7,uint32_t r8,uint32_t r9,
    uint32_t stack,std::vector<GuestColourDraw>& calls) {
  int32_t r27=r5; const double f27=f1;
  const uint32_t r22=r3,r29=r4,r26=r7,r21=r8,r20=r9;
  if(r27<2) return;
  if(r27>100) r27=100;
  double f13=Lfs(m,r29+0),f12=Lfs(m,r29+4);
  uint32_t r30=r29+8;
  double f11=Lfs(m,r29+16),f10=Lfs(m,r29+20);
  f13=F(f11-f13); f12=F(f10-f12);
  Stfs(m,stack+96,f13);
  uint32_t r11=m.Word(0x8257C02C);
  Stfs(m,stack+100,f12);
  double f0=Lfs(m,0x820009A4);
  int32_t r28=0;
  const uint32_t r10=r11+192;
  f13=Lfs(m,r29+24);
  f12=Lfs(m,r30+0);
  Stfs(m,stack+120,f0); Stfs(m,stack+116,f0);
  f13=F(f13-f12);
  Stfs(m,stack+104,f13);
  Copy16(m,r10,stack+160);
  Stfs(m,stack+88,f0); Stfs(m,stack+84,f0); Stfs(m,stack+80,f0);
  f0=Lfs(m,0x820008CC);
  Stfs(m,stack+92,f0);
  if(r27!=0) {
    const double f30=Lfs(m,stack+168),f29=Lfs(m,stack+164),f28=Lfs(m,stack+160);
    const uint32_t r23=uint32_t(-24)-r29;
    const int32_t r25=r27-1;
    Stfs(m,stack+140,f0);
    uint32_t r31=stack+180;
    Stfs(m,stack+156,f0);
    const uint32_t r24=uint32_t(-8)-r29;
    const double f31=Lfs(m,0x820008D4);
    do {
      r11=r24+r30;
      if(r28==r25) r11=r23+r30;
      r11=r11+r29;
      f11=Lfs(m,stack+96);
      f13=Lfs(m,r11+16); f0=Lfs(m,r11+0);
      f0=F(f13-f0);
      f12=Lfs(m,r11+20); f13=Lfs(m,r11+4);
      Stfs(m,stack+128,f0);
      f10=Lfs(m,r11+24);
      f11=F(f0+f11);
      Stfs(m,stack+144,f11);
      f0=F(f12-f13);
      f12=Lfs(m,stack+100);
      Stfs(m,stack+132,f0);
      f13=Lfs(m,r11+8);
      f11=F(f11*f31);
      f0=F(f0+f12);
      Stfs(m,stack+148,f0);
      f0=F(f10-f13);
      f12=Lfs(m,stack+104);
      Stfs(m,stack+136,f0);
      f13=Lfs(m,r11+0); f10=Lfs(m,r11+8);
      f0=F(f0+f12);
      Stfs(m,stack+152,f0);
      const uint32_t sum[4]{m.Word(stack+144),m.Word(stack+148),m.Word(stack+152),m.Word(stack+156)};
      f12=Lfs(m,r11+4);
      f0=F(f13-f28); f13=F(f12-f29);
      const uint32_t side[4]{m.Word(stack+128),m.Word(stack+132),m.Word(stack+136),m.Word(stack+140)};
      f12=F(f10-f30);
      for(uint32_t i=0;i<4;++i) m.StoreWord(stack+112+i*4,sum[i]);
      double f10b=Lfs(m,stack+116);
      for(uint32_t i=0;i<4;++i) m.StoreWord(stack+96+i*4,side[i]);
      double f9=Lfs(m,stack+120);
      f10b=F(f10b*f31); f9=F(f9*f31);
      Stfs(m,stack+116,f10b); Stfs(m,stack+120,f9);
      const double f6=F(f13*f11),f8=F(f12*f10b),f7=F(f9*f0);
      f0=F(std::fma(f10b,f0,-f6)); Stfs(m,stack+88,f0);
      f13=F(std::fma(f13,f9,-f8)); Stfs(m,stack+80,f13);
      f13=F(std::fma(f12,f11,-f7)); Stfs(m,stack+84,f13);
      Guest821B0320(m,stack+80,f27);
      f0=Lfs(m,r30-8); f11=Lfs(m,stack+80);
      ++r28;
      f10=F(f0+f11); Stfs(m,r31-4,f10);
      f13=Lfs(m,r30-4);
      f0=F(f0-f11);
      f10=Lfs(m,stack+84);
      m.StoreWord(r31+8,r26);
      Stfs(m,r31+0,F(f13+f10));
      f12=Lfs(m,r30+0);
      m.StoreWord(r31+24,r26);
      f9=Lfs(m,stack+88);
      r30+=16;
      Stfs(m,r31+12,f0);
      const double f8b=F(f12+f9);
      f0=F(f13-f10);
      Stfs(m,r31+4,f8b);
      Stfs(m,r31+16,f0);
      f0=F(f12-f9);
      Stfs(m,r31+20,f0);
      r31+=32;
    } while(r28!=r27);
  }
  // 821A7B58(r3 r22, r4 6, r5 stack+176, r6 2*count-2, r7 r21, r8 r20);
  // its 821FD8F8 draws count = 6's table entry (1 per primitive, +2).
  GuestColourDraw call{r22,6,uint32_t(r27*2-2),r21,r20,{}};
  const auto* bytes=m.Bytes(stack+176,size_t(call.primitives+2)*16);
  call.vertices.assign(bytes,bytes+size_t(call.primitives+2)*16);
  calls.push_back(std::move(call));
}
// sub_820B8D28 (r3 wire, r4 context), its frame at `stack` (after stwu -512)
// and 821A7E08's below it at `inner`. sin is sub_821E9558 as
// NativeGuestSin transcribes it (checked bit for bit against the recompiled
// body by the render registry tests).
std::vector<GuestColourDraw> Guest820B8D28(const ImageMemory& m,uint32_t r3,uint32_t r4,uint32_t stack,uint32_t inner) {
  std::vector<GuestColourDraw> calls;
  const auto trap=[] { throw std::runtime_error("twi"); };
  uint32_t r11=m.Word(r4+16);
  const uint32_t r25=r3,r24=r11+96,r26=r25+384,r23=r11+288;
  const double f21=Lfs(m,0x820009A4);
  uint32_t r30=m.Word(r26+4),r6=m.Word(r26+8);
  if(!(r30<=r6)) trap();
  const uint32_t r22=m.Word(r26+8);
  r11=m.Word(r26+4);
  if(!(r11<=r22)) trap();
  const uint32_t r21=m.Word(0x8257C034);
  const double f18=Lfs(m,0x8200271C),f22=Lfs(m,0x82002718),f23=Lfs(m,0x820008D4),f20=Lfs(m,0x82002714),f19=Lfs(m,0x82002710);
  while(r22!=r30) {
    if(!(r30<r6)) trap();
    if(m.Bytes(r30+88,1)[0]) {
      const uint32_t r28=r30+32;
      Guest821B0258(m,stack+80,r28,r24);
      if(!(r30<m.Word(r26+8))) trap();
      const uint32_t r27=r30+48;
      Guest821B0258(m,stack+96,r27,r24);
      double f13=Lfs(m,stack+84);
      double f12=F(f13*f13);
      double f0=Lfs(m,stack+88);
      f13=Lfs(m,stack+80);
      f0=F(std::fma(f0,f0,f12));
      f0=F(std::fma(f13,f13,f0));
      bool skip=false;
      if(f0>f19) {
        f13=Lfs(m,stack+100);
        f12=F(f13*f13);
        f0=Lfs(m,stack+96);
        f13=Lfs(m,stack+104);
        f0=F(std::fma(f0,f0,f12));
        f0=F(std::fma(f13,f13,f0));
        skip=f0>f19;
      }
      if(!skip) {
        Guest821B0258(m,stack+128,r30+64,r24);
        if(Guest821C2FD0(m,r23,stack+128,Lfs(m,r30+80))) {
          Copy16(m,r28,stack+112);
          f13=Lfs(m,r27+0);
          double f31=f21;
          f0=Lfs(m,r28+0);
          uint32_t r31=stack+144;
          f0=F(f13-f0);
          f13=Lfs(m,r28+4);
          f12=Lfs(m,r27+4);
          uint32_t r29=10;
          double f11=Lfs(m,r27+8);
          double f27=Lfs(m,stack+120),f28=Lfs(m,stack+116),f29=Lfs(m,stack+112);
          const double f26=F(f0*f20);
          f0=F(f12-f13);
          f13=Lfs(m,r28+8);
          const double f25=F(f0*f20);
          f0=F(f11-f13);
          const double f24=F(f0*f20);
          do {
            Copy16(m,stack+112,r31);
            double f1=NativeGuestSin(f31);
            const double f30=F(f1);
            f0=Lfs(m,r25+400); f13=Lfs(m,r30+84);
            f1=F(f0+f13);
            f1=NativeGuestSin(f1);
            f0=F(f1);
            const uint32_t r11b=r31+8;
            f13=Lfs(m,r31+0);
            --r29;
            f12=Lfs(m,r31+4);
            f29=F(f26+f29);
            f12=F(f12-f30);
            Stfs(m,r31+4,f12);
            f28=F(f28+f25);
            Stfs(m,stack+112,f29);
            f11=Lfs(m,r11b+0);
            f27=F(f27+f24);
            Stfs(m,stack+116,f28);
            f31=F(f31+f22);
            Stfs(m,stack+120,f27);
            f0=F(f0*f23);
            f0=F(f0*f30);
            f13=F(f13+f0);
            Stfs(m,r31+0,f13);
            f0=F(f0+f11);
            r31+=16;
            Stfs(m,r11b+0,f0);
          } while(r29!=0);
          Guest821A7E08(m,r21,stack+144,10,f18,0xFF404040u,0,1,inner,calls);
        }
      }
    }
    r6=m.Word(r26+8);
    if(!(r30<r6)) trap();
    r30+=96;
  }
  return calls;
}
// A wire's world: a view matrix with rotation and translation, a frustum
// (near 1, far 1000, 45-degree sides), the effect and camera globals, and the
// image constants both sides read.
void BuildWireWorld(const ImageMemory& m,uint32_t context,uint32_t scene,uint32_t effect,uint32_t camera) {
  const float view[16]{0.8f,0,-0.6f,0, 0,1,0,0, 0.6f,0,0.8f,0, 1.5f,-2.25f,10.125f,1};
  for(uint32_t i=0;i<16;++i) m.StoreFloat(scene+96+i*4,view[i]);
  constexpr float side=0.70710678f;
  float frustum[26]{};
  frustum[8]=side; frustum[10]=-side; frustum[12]=-side; frustum[14]=-side;
  frustum[17]=side; frustum[18]=-side; frustum[21]=-side; frustum[22]=-side; frustum[24]=1; frustum[25]=1000;
  for(uint32_t i=0;i<26;++i) m.StoreFloat(scene+288+i*4,frustum[i]);
  m.StoreWord(context+16,scene);
  m.StoreWord(0x8257C034,effect); m.StoreWord(0x8257C02C,camera);
  // The eye 821BE8D0 -> 821A19F0 would store for this view (the helper is
  // checked bit for bit against a transcription of 821A19F0 in
  // native_full_frame_effects_tests); the guest transcription reads it here.
  m.StoreWord(kNativeEffectEyeScale,0xbf800000u);  // -1.0, as guest_image.bin holds it
  std::array<uint32_t,16> words{};
  for(uint32_t i=0;i<16;++i) words[i]=std::bit_cast<uint32_t>(view[i]);
  const auto eye=NativeEffectEyeFromView(words,-1.f);
  for(uint32_t i=0;i<4;++i) m.StoreFloat(camera+192+i*4,eye[i]);
  // lfs constants, as guest_image.bin holds them.
  m.StoreWord(0x820009A4,0x00000000u); m.StoreWord(0x820008CC,0x3f800000u); m.StoreWord(0x820008D4,0x3f000000u);
  m.StoreWord(0x82002710,0x47afc800u); m.StoreWord(0x82002714,0x3de38e39u); m.StoreWord(0x82002718,0x3eb2b8c3u);
  m.StoreWord(0x8200271C,0x3cf5c28fu);
}
// The native inputs for the scene's view: the eye from the pass camera's view
// words (scene+96), never from [8257C02C]+192.
std::array<uint32_t,16> WireView(const ImageMemory& m,uint32_t scene) {
  std::array<uint32_t,16> view{};
  for(uint32_t i=0;i<16;++i) view[i]=m.Word(scene+96+i*4);
  return view;
}
NativeEffectInputs WireInputs(const ImageMemory& m,uint32_t scene) { return ReadNativeEffectInputs(m,WireView(m,scene)); }
// A stale guest eye (what the full-frame renderer leaves at +192): the native
// builders must not see it.
void StaleGuestEye(const ImageMemory& m,uint32_t camera) {
  m.StoreFloat(camera+192,-3.5f); m.StoreFloat(camera+196,7.25f); m.StoreFloat(camera+200,-11.0f); m.StoreFloat(camera+204,1);
}
void WireRecord(const ImageMemory& m,uint32_t record,bool enabled,std::array<float,3> a,std::array<float,3> b,
    std::array<float,3> centre,float radius,float offset) {
  const auto vec=[&](uint32_t at,std::array<float,3> v,float w) { for(uint32_t i=0;i<3;++i) m.StoreFloat(at+i*4,v[i]); m.StoreFloat(at+12,w); };
  vec(record+32,a,1); vec(record+48,b,1); vec(record+64,centre,1);
  m.StoreFloat(record+80,radius); m.StoreFloat(record+84,offset); m.StoreByte(record+88,enabled);
}
void TestElectricWire() {
  ImageMemory m;
  constexpr uint32_t context=0x1000,scene=0x1400,wire=0x2000,records=0x3000,effect=0x4000,camera=0x4800,stack=0xC000,inner=0xD000;
  BuildWireWorld(m,context,scene,effect,camera);
  m.StoreWord(wire,NativeElectricWire::vtable);
  m.StoreFloat(wire+400,1.25f);
  WireRecord(m,records+0*96,true,{3.1f,4.7f,20.3f},{9.9f,5.3f,40.7f},{6.5f,5,30.5f},12,0.37f);   // drawn
  WireRecord(m,records+1*96,false,{3.1f,4.7f,20.3f},{9.9f,5.3f,40.7f},{6.5f,5,30.5f},12,0.1f);   // disabled
  WireRecord(m,records+2*96,true,{400,0,300},{410,0,320},{405,0,310},50,0.2f);                   // both ends past 300
  WireRecord(m,records+3*96,true,{400,0,300},{5,1,25},{10,1,30},50,-2.5f);                       // far A, near B: drawn
  WireRecord(m,records+4*96,true,{1,1,20},{2,2,21},{0,0,-200},5,0.4f);                           // centre behind the near plane
  m.StoreWord(wire+388,records); m.StoreWord(wire+392,records+5*96);
  const auto guest=Guest820B8D28(m,wire,context,stack,inner);
  Require(guest.size()==2,"guest transcription draw count");
  StaleGuestEye(m,camera);  // full-frame mode: +192 left by some other view
  NativeElectricWireStats stats;
  const auto draws=BuildNativeElectricWireDraws(m,wire,scene,WireInputs(m,scene),&stats);
  Require(stats.records==5 && stats.disabled==1 && stats.distant==1 && stats.culled==1 && stats.drawn==2,"native wire statistics");
  Require(draws.size()==guest.size(),"native wire draw count");
  for(size_t i=0;i<draws.size();++i) {
    const auto& draw=draws[i];
    const auto& call=guest[i];
    Require(draw.kind==NativeEffectDraw::Kind::ColourStrip && draw.technique==NativeEffectTechnique::Solid &&
      draw.effect==call.effect && draw.primitive()==call.primitive && draw.vertex_count()==call.primitives+2 &&
      draw.stride()==16 && draw.blend==int32_t(call.blend) && draw.sets_depth_write && draw.depth_write==(call.depth==1) &&
      draw.texture==0 && draw.state_before_activation(),"native wire draw contract");
    const auto calls=NativeEffectDrawCalls(draw);
    Require(calls.size()==1 && calls[0]==std::pair<uint32_t,uint32_t>{0,20},"native wire draw call");
    Require(EncodeNativeEffectVertices(draw,0,20)==call.vertices,"native wire vertices differ from the guest's");
  }
  // The strip itself: the colour word on every vertex, unchanged.
  for(uint32_t v=0;v<20;++v) Require(m.Word(inner+176+v*16+12)==0xFF404040u,"guest colour word");
  // 821A7B58's technique: +160, material [+176], no sampler list.
  Require(NativeEffectTechniqueOffset(NativeEffectTechnique::Solid)==160,"solid technique offset");
  m.StoreWord(effect+176,0x5000);
  Require(NativeEffectTechniqueMaterial(m,effect,NativeEffectTechnique::Solid)==0x5000,"solid technique material");
  const auto before=m.bytes;
  BindNativeEffectTexture(m,effect,NativeEffectTechnique::Solid,0x1234);
  Require(m.bytes==before,"821A7B58 binds no texture");
  // Fewer than two points draw nothing; more than 100 are capped at 100.
  const NativeEffectConstants k{};
  const std::vector<NativeFxVec3> one(1,NativeFxVec3{1,2,3});
  Require(BuildNativeColourStrip(one,1,0.5f,{0,0,0},k).empty(),"one-point strip drew");
  std::vector<NativeFxVec3> many(150);
  for(size_t i=0;i<many.size();++i) many[i]={float(i),float(i%7),float(i%3)+5};
  Require(BuildNativeColourStrip(many,1,0.5f,{0,0,0},k).size()==200,"strip cap");
  // A torn record vector is refused, not walked.
  m.StoreWord(wire+392,records+5*96+4);
  bool refused=false;
  try { BuildNativeElectricWireDraws(m,wire,scene,WireInputs(m,scene)); } catch(const std::exception&) { refused=true; }
  Require(refused,"torn wire vector accepted");
}
// NativeElectricWirePoints as first written: the sag and swing sines taken at
// every point, as 820B8D28 does. The builder now takes the sag from one table
// and the swing once per record; the points must be the same bits.
std::array<NativeFxVec3,NativeElectricWire::points> ReferenceWirePoints(const NativeFxVec3& a,const NativeFxVec3& b,
    float phase,float offset,const NativeEffectConstants& k,const NativeElectricWireConstants& w) {
  const float sx=NativeFxMul(NativeFxSub(b[0],a[0]),w.ninth);
  const float sy=NativeFxMul(NativeFxSub(b[1],a[1]),w.ninth);
  const float sz=NativeFxMul(NativeFxSub(b[2],a[2]),w.ninth);
  float px=a[0],py=a[1],pz=a[2],angle=k.zero;
  std::array<NativeFxVec3,NativeElectricWire::points> out{};
  for(auto& point:out) {
    point={px,py,pz};
    const float s=float(NativeGuestSin(double(angle)));
    float t=float(NativeGuestSin(double(NativeFxAdd(phase,offset))));
    px=NativeFxAdd(sx,px);
    point[1]=NativeFxSub(point[1],s);
    py=NativeFxAdd(py,sy); pz=NativeFxAdd(pz,sz);
    angle=NativeFxAdd(angle,w.angle_step);
    t=NativeFxMul(t,k.half); t=NativeFxMul(t,s);
    point[0]=NativeFxAdd(point[0],t);
    point[2]=NativeFxAdd(t,point[2]);
  }
  return out;
}
// Bit for bit, except that a NaN matches any NaN: NativeGuestSin of a NaN (a
// NaN wire phase, or a NaN angle step) returns a NaN whose sign bit the host's
// fma leaves unfixed - the per-point form itself gives either sign depending on
// the calls before it. A NaN vertex is dropped by the rasterizer whatever its
// sign, and every non-NaN result must be the same bits.
bool SameWirePoints(const std::array<NativeFxVec3,NativeElectricWire::points>& a,
    const std::array<NativeFxVec3,NativeElectricWire::points>& b) {
  for(size_t i=0;i<a.size();++i) for(size_t c=0;c<3;++c) {
    if(std::isnan(a[i][c]) || std::isnan(b[i][c])) { if(!std::isnan(a[i][c]) || !std::isnan(b[i][c])) return false; }
    else if(std::bit_cast<uint32_t>(a[i][c])!=std::bit_cast<uint32_t>(b[i][c])) return false;
  }
  return true;
}
void TestElectricWirePointsEquivalence() {
  uint32_t specials_seen=0;
  std::mt19937 random(0x820B8D28u);
  std::uniform_real_distribution<float> coordinate(-2000.f,2000.f),angle(-1e6f,1e6f),small(-4.f,4.f);
  const NativeEffectConstants k{};
  NativeElectricWireConstants w;
  // The image constants as guest_image.bin holds them, and odd ones.
  const std::array<float,5> steps{std::bit_cast<float>(0x3eb2b8c3u),0.f,-0.349066f,3.5f,std::bit_cast<float>(0x7fc00000u)};
  const std::array<float,6> specials{0.f,-0.f,std::bit_cast<float>(0x7f800000u),std::bit_cast<float>(0xff800000u),
    std::bit_cast<float>(0x7fc00000u),std::bit_cast<float>(0x00000001u)};
  for(uint32_t round=0;round<20000;++round) {
    w.angle_step=steps[round%steps.size()];
    w.ninth=round%7?std::bit_cast<float>(0x3de38e39u):small(random);
    NativeFxVec3 a{coordinate(random),coordinate(random),coordinate(random)},b{coordinate(random),coordinate(random),coordinate(random)};
    float phase=round%3?small(random):angle(random),offset=small(random);
    if(round%97==0) phase=specials[(round/97)%specials.size()];
    if(round%89==0) a[round%3]=specials[(round/89)%specials.size()];
    const auto expected=ReferenceWirePoints(a,b,phase,offset,k,w);
    const auto sines=NativeElectricWireSag(k,w);
    const auto actual=NativeElectricWirePoints(a,b,phase,offset,k,w,sines);
    Require(SameWirePoints(expected,actual),"wire points differ from the per-point sines");
    Require(SameWirePoints(expected,NativeElectricWirePoints(a,b,phase,offset,k,w)),"wire points without a sag table differ");
    if(round%97==0) ++specials_seen;
  }
  Require(specials_seen>=specials.size(),"wire point specials not exercised");
}
// Many records through the builder (block-read records, one sag table per
// wire) against the guest transcription: every record's decision and every
// strip's bytes, across phases and random records around the camera.
void TestElectricWireRandomized() {
  ImageMemory m;
  constexpr uint32_t context=0x1000,scene=0x1400,wire=0x2000,effect=0x4000,camera=0x4800,records=0x5000,stack=0xC000,inner=0xD000;
  constexpr uint32_t count=150;
  static_assert(records+count*96<=stack);
  BuildWireWorld(m,context,scene,effect,camera);
  m.StoreWord(wire,NativeElectricWire::vtable);
  std::mt19937 random(0x821A7E08u);
  std::uniform_real_distribution<float> spread(-80.f,80.f),height(-6.f,12.f),radius(0.5f,40.f),offset(-3.f,3.f),phase(-50.f,50.f);
  uint64_t drawn=0,distant=0,culled=0,disabled=0;
  for(uint32_t round=0;round<12;++round) {
    m.StoreFloat(wire+400,phase(random));
    for(uint32_t i=0;i<count;++i) {
      const std::array<float,3> a{spread(random)*(i%5?1.f:6.f),height(random),spread(random)*(i%5?1.f:6.f)};
      const std::array<float,3> b{a[0]+spread(random)*0.5f,height(random),a[2]+spread(random)*0.5f};
      const std::array<float,3> centre{(a[0]+b[0])*0.5f,(a[1]+b[1])*0.5f,(a[2]+b[2])*0.5f};
      WireRecord(m,records+i*96,random()%8!=0,a,b,centre,radius(random),offset(random));
    }
    m.StoreWord(wire+388,records); m.StoreWord(wire+392,records+count*96);
    const auto guest=Guest820B8D28(m,wire,context,stack,inner);
    NativeElectricWireStats stats;
    const auto draws=BuildNativeElectricWireDraws(m,wire,scene,WireInputs(m,scene),&stats);
    Require(draws.size()==guest.size() && stats.drawn==draws.size() && stats.records==count &&
      stats.disabled+stats.distant+stats.culled+stats.drawn==count,"randomized wire draw count");
    for(size_t d=0;d<draws.size();++d)
      Require(EncodeNativeEffectVertices(draws[d],0,draws[d].vertex_count())==guest[d].vertices,"randomized wire vertices differ from the guest's");
    drawn+=stats.drawn; distant+=stats.distant; culled+=stats.culled; disabled+=stats.disabled;
  }
  Require(drawn && distant && culled && disabled,"randomized wires did not reach every decision");
  // An empty record vector draws nothing and reads no record.
  m.StoreWord(wire+392,records);
  NativeElectricWireStats none;
  Require(BuildNativeElectricWireDraws(m,wire,scene,WireInputs(m,scene),&none).empty() && !none.records,"empty wire drew");
}
// The walk's inputs: the manager on the helper's world list, then its members
// in list order with their route words; nothing written.
void TestMapEffectMembers() {
  ImageMemory m;
  constexpr uint32_t owner=0x0800,world_node=0x0900,other=0x0A00,other_node=0x0940,end_node=0x0980;
  // World list: another object first, then the manager.
  m.StoreWord(owner+44,other_node); m.StoreWord(owner+56,end_node);
  m.StoreWord(other_node,world_node); m.StoreWord(other_node+8,other); m.StoreWord(other,0x820072D4);
  m.StoreWord(world_node,end_node); m.StoreWord(world_node+8,manager); m.StoreWord(manager,kNativeMapEffectManagerVtable);
  Require(FindNativeWorldListObject(m,owner,kNativeMapEffectManagerVtable)==manager &&
    FindNativeWorldListObject(m,owner,0x820072D4)==other && !FindNativeWorldListObject(m,owner,0x82000000),"world list lookup");
  // Four members linked at the head (821A1628), so the walk reads 3..0; the
  // end marker list+12 stays zero. Their vtables' slot 4 as the image holds them.
  for(uint32_t i=0;i<4;++i) {
    const auto node=Node(i),first=m.Word(list);
    m.StoreWord(node+4,list); m.StoreWord(node,first); m.StoreWord(list,node);
    if(first) m.StoreWord(first+4,node);
    m.StoreWord(node+8,Object(i));
  }
  const uint32_t vtables[4]{0x8200284C,0x82002744,0x82002744,0x820124DC};
  for(uint32_t i=0;i<4;++i) m.StoreWord(Object(i),vtables[i]);
  m.StoreWord(0x8200284C+16,0x820BB270); m.StoreWord(0x82002744+16,0x820B8D28); m.StoreWord(0x820124DC+16,0x82172698);
  m.StoreWord(Object(2)+NativeMapEffectObject::mode,1); m.StoreByte(Object(1)+NativeMapEffectObject::hidden+1,1);
  m.StoreWord(Object(3)+NativeMapEffectObject::mode,2);
  const auto before=m.bytes;
  const auto members=CollectNativeMapEffectMembers(m,manager);
  Require(m.bytes==before,"member walk wrote guest memory");
  Require(members.size()==4 && members[0].object==Object(3) && members[3].object==Object(0),"members lost the list order");
  Require(members[0].kind==NativeMapEffectKind::GrassMap && members[1].kind==NativeMapEffectKind::ElectricWire &&
    members[2].kind==NativeMapEffectKind::ElectricWire && members[3].kind==NativeMapEffectKind::Sky,"member classes");
  Require(members[0].render==0x82172698 && members[0].mode==2 && !members[0].hidden &&
    members[2].mode==0 && members[2].hidden && members[1].mode==1 && members[3].render==0x820BB270,"member route words");
  Require(ClassifyNativeMapEffect(0x8200284C)==NativeMapEffectKind::Sky && ClassifyNativeMapEffect(0x82002744)==NativeMapEffectKind::ElectricWire &&
    ClassifyNativeMapEffect(0x820124DC)==NativeMapEffectKind::GrassMap && ClassifyNativeMapEffect(0x82012CC8)==NativeMapEffectKind::Other,
    "map effect classes");
  m.StoreWord(manager,0x82000000);
  bool refused=false;
  try { CollectNativeMapEffectMembers(m,manager); } catch(const std::exception&) { refused=true; }
  Require(refused,"non-manager walked");
}

// ---- clGrassMap: the native builder against a transcription of the guest ----
// Registers as the recompiled bodies hold them: GPRs as 64-bit values, rotates
// as the recompiler writes rlwinm/rotlwi, fctiwz with its NaN/INT_MAX guards
// in front of cvttsd2si, divw with its zero/overflow guard.
uint64_t Rot(uint64_t r,int n) { const uint64_t v=uint64_t(uint32_t(r))|(r<<32); return (v<<n)|(v>>(64-n)); }
uint32_t Fctiwz(double f) {
  if(std::isnan(f)) return 0x80000000u;
  if(f>=double(INT_MAX)) return uint32_t(INT_MAX);
  return uint32_t(_mm_cvttsd_si32(_mm_load_sd(&f)));
}
uint32_t Divw(uint32_t a,uint32_t b) {
  return (int32_t(b) && !(int32_t(a)==INT32_MIN && int32_t(b)==-1))?uint32_t(int32_t(a)/int32_t(b)):0;
}
uint32_t Lhz(const ImageMemory& m,uint32_t at) { const auto* p=m.Bytes(at,2); return uint32_t(p[0])<<8|p[1]; }
uint32_t Lbz(const ImageMemory& m,uint32_t at) { return m.Bytes(at,1)[0]; }
[[noreturn]] void Trap() { throw std::runtime_error("twi"); }
// What 8218D440 binds and 8218D3F0 draws: the Utility object (r3), the
// texture (8218D440's r4), 821FD8F8's primitive, vertex count and the bytes at
// its r6 (stride 36).
struct GuestGrassDraw { uint32_t utility=0,texture=0,primitive=0,count=0,vertices=0; std::vector<uint8_t> bytes; };
struct GuestGrassLog {
  std::vector<std::array<int32_t,2>> cells;
  std::vector<GuestGrassDraw> draws;
  uint32_t texture=0,utility=0;
  uint32_t blend_src=0,blend_dst=0,depth_write=2,pushed=0;
};
// sub_821B0198 (r3 out, r4 vec4, r5 matrix).
void Guest821B0198(const ImageMemory& m,uint32_t r3,uint32_t r4,uint32_t r5) {
  double f0=Lfs(m,r4+8),f6=Lfs(m,r5+36);
  f6=F(f6*f0);
  double f13=Lfs(m,r4+4),f3=Lfs(m,r5+20),f12=Lfs(m,r4+12),f31=Lfs(m,r5+52),f11=Lfs(m,r4+0),f10=Lfs(m,r5+4);
  double f5=Lfs(m,r5+40),f4=Lfs(m,r5+44);
  f5=F(f5*f0); f4=F(f4*f0);
  double f2=Lfs(m,r5+24),f7=Lfs(m,r5+0);
  f6=F(std::fma(f3,f13,f6));
  double f1=Lfs(m,r5+28),f30=Lfs(m,r5+56),f29=Lfs(m,r5+60),f9=Lfs(m,r5+8),f8=Lfs(m,r5+12);
  f5=F(std::fma(f2,f13,f5));
  f4=F(std::fma(f1,f13,f4));
  f6=F(std::fma(f31,f12,f6));
  f5=F(std::fma(f30,f12,f5));
  f4=F(std::fma(f29,f12,f4));
  f10=F(std::fma(f10,f11,f6));
  f6=Lfs(m,r5+32);
  f0=F(f0*f6);
  f6=Lfs(m,r5+16);
  f9=F(std::fma(f9,f11,f5));
  f5=Lfs(m,r5+48);
  f8=F(std::fma(f8,f11,f4));
  Stfs(m,r3+4,f10); Stfs(m,r3+8,f9); Stfs(m,r3+12,f8);
  f0=F(std::fma(f11,f7,f0));
  f0=F(std::fma(f6,f13,f0));
  f0=F(std::fma(f5,f12,f0));
  Stfs(m,r3+0,f0);
}
// sub_82171BD8 (r3 grass, f1 limit): r3 of the return.
uint32_t Guest82171BD8(const ImageMemory& m,uint32_t r3,double f1) {
  const uint32_t r31=r3;
  const double f31=f1;
  const uint32_t r4=r31+288;
  uint32_t r11=m.Word(r31+1228);
  const uint32_t r3b=r11+32;
  const uint32_t r30=m.Word(r11+16);
  Guest821B0198(m,r3b,r4,r30+96);
  r11=m.Word(r31+1228);
  const uint32_t r4b=r11+32;
  double f0=Lfs(m,r11+8);
  const double f13=Lfs(m,r4b+8);
  f0=F(f0*f13);
  f0=std::bit_cast<double>(std::bit_cast<uint64_t>(f0)^0x8000000000000000ull);
  if(f0>f31) return 0;
  const bool r3c=Guest821C2FD0(m,r30+288,r4b,Lfs(m,r31+352));
  return uint32_t(r3c?1:0);   // clrlwi; cntlzw; rlwinm 27,31,31; xori 1
}
// sub_821B03C8 (r3 vector): f1.
double Guest821B03C8(const ImageMemory& m,uint32_t r3) {
  double f13=Lfs(m,r3+4);
  f13=F(f13*f13);
  double f0=Lfs(m,r3+0);
  const double f12=Lfs(m,r3+8);
  const double f1=Lfs(m,0x820009A4);
  f0=F(std::fma(f0,f0,f13));
  f0=F(std::fma(f12,f12,f0));
  if(f0==f1) return f1;
  return F(std::sqrt(f0));
}
// sub_8218D440 (r3 Utility, r4 texture): 821BC4C8 and 821B94E8 bind, then
// the declaration traps.
void Guest8218D440(const ImageMemory& m,uint32_t r3,uint32_t r4,GuestGrassLog& log) {
  log.utility=r3; log.texture=r4;
  const uint32_t r11=m.Word(r3+60);
  if(r11==0) Trap();
  const uint32_t r10=m.Word(r3+64);
  if(r10==m.Word(r11+4)) Trap();
}
// sub_8218D3F0 (r3 Utility, r4 primitive, r5 vertices, r6 count): 821FD8F8
// (r4 primitive, r5 = table[r4].a * count + table[r4].b, r6 vertices, r7 36).
void Guest8218D3F0(const ImageMemory& m,uint32_t r3,uint32_t r4,uint32_t r5,uint32_t r6,GuestGrassLog& log) {
  const uint32_t r11=uint32_t(Rot(r4,3)&0xFFFFFFF8u);
  const uint32_t r9=r6,r6b=r5,r7=36;
  const uint32_t r10=0x82010000u-30584u;
  const uint32_t r8=r10+4;
  const uint32_t r5b=m.Word(r11+r10),r10b=m.Word(r11+r8);
  const uint32_t r11b=uint32_t(int64_t(int32_t(r5b))*int64_t(int32_t(r9)));
  const uint32_t r5c=r11b+r10b;
  GuestGrassDraw draw{r3,log.texture,r4,r9,r5c,{}};
  if(log.utility!=r3) Trap();
  const auto* bytes=m.Bytes(r6b,size_t(r5c)*r7);
  draw.bytes.assign(bytes,bytes+size_t(r5c)*r7);
  log.draws.push_back(std::move(draw));
}
// sub_82171F58 (r3 grass, r4 x, r5 z), its frame at `stack` (r1 after stwu -544).
void Guest82171F58(const ImageMemory& m,uint32_t r3,uint32_t r4,uint32_t r5,uint32_t stack,GuestGrassLog& log) {
  log.cells.push_back({int32_t(r4),int32_t(r5)});
  const uint32_t r31=r3;
  uint32_t r29=r5;
  if(int32_t(r4)<0) return;
  uint32_t r11=m.Word(r31+1208);
  if(!(int32_t(r4)<int32_t(r11))) return;
  if(int32_t(r29)<0) return;
  uint32_t r10=m.Word(r31+1212);
  if(!(int32_t(r29)<int32_t(r10))) return;
  r11=uint32_t(int64_t(int32_t(r11))*int64_t(int32_t(r29)));
  r10=m.Word(r31+1176);
  r11=r11+r4;
  r11=uint32_t(Rot(r11,2)&0xFFFFFFFCu);
  uint32_t r30=m.Word(r11+r10);
  if(int32_t(r30)<0) return;
  int64_t r9=int32_t(r29);                              // extsw r9,r29
  r11=m.Word(r31+1216);
  r10=uint32_t(Rot(r30,3)&0xFFFFFFF8u);
  double f12=Lfs(m,r31+1152),f0=Lfs(m,r31+1144);
  r11=r11+r10;
  const double f9=Lfs(m,r31+1128);
  double f11=Lfs(m,r31+1140);
  const int64_t s112=r9;                                // std r9,112(r1)
  const double f7=Lfs(m,r31+1148),f5=Lfs(m,r31+1124);
  double f13=Lfs(m,r11+0);
  const double f25=Lfs(m,0x820008CC);
  const int64_t r10s=int32_t(r4);                       // extsw r10,r4
  double f10=Lfs(m,r11+4);
  Stfs(m,r31+300,f25);
  const int64_t s80=r10s;                               // std r10,80(r1)
  const double f26=Lfs(m,0x820008D4);
  double f8=double(s112);                               // lfd; fcfid
  const double f6=double(s80);
  f8=F(f8);
  const double f16=F(f6);
  f0=F(std::fma(f0,f8,f12));
  f11=F(std::fma(f11,f16,f7));
  f12=F(f0+f9); Stfs(m,r31+296,f12);
  f0=F(f10-f13);
  f12=F(f11+f5); Stfs(m,r31+288,f12);
  f0=F(f0*f26);
  f13=F(f13+f0); Stfs(m,r31+292,f13);
  f12=Lfs(m,r31+1148);
  f10=F(f12*f12);
  f13=Lfs(m,r31+1152);
  Stfs(m,r31+344,f13);
  Stfs(m,r31+324,f0);
  f11=Lfs(m,r31+1156);
  Stfs(m,r31+304,f12);
  f13=F(std::fma(f13,f13,f10));
  f0=F(std::fma(f0,f0,f13));
  f0=F(std::sqrt(f0));
  Stfs(m,r31+352,f0);
  double f1=F(f11+f0);
  if(Guest82171BD8(m,r31,f1)==0) return;
  uint32_t r8=m.Word(r31+1188);
  uint32_t r28=r31+920;
  f0=Lfs(m,r31+1128); f13=Lfs(m,r31+1112);
  uint32_t r7=stack+180;
  m.StoreWord(stack+176,0);
  uint32_t r6=r28;
  r11=m.Word(r31+1200);
  r9=int64_t(int32_t(r11))*int64_t(int32_t(r29));
  r9=int32_t(r9);                                        // extsw
  const uint32_t r4b=uint32_t(int64_t(int32_t(r11))*int64_t(int32_t(r11)));
  const int64_t s80b=r9;
  uint32_t r9u=uint32_t(int64_t(int32_t(r4b))*int64_t(int32_t(r30)));
  r9u=uint32_t(Rot(r9u,2)&0xFFFFFFFCu);
  r29=r9u+r8;
  f12=double(s80b); f12=F(f12);
  double f19=F(std::fma(f12,f13,f0));
  for(uint32_t ctr=15;ctr;--ctr) { m.StoreWord(r7,0); r7+=4; }
  r10=0;
  do { const uint32_t w=m.Word(r6); r6+=12; m.StoreWord(r10+stack+244,w); r10+=4; } while(int32_t(r10)<60);
  uint32_t r25=0;
  if(int32_t(r11)>0) {
    const uint32_t r27=0x82550000u+17536u;
    const double f17=Lfs(m,0x82000000u+8676u),f18=Lfs(m,0x82000000u+9376u),f20=Lfs(m,0x82010000u+9424u),
      f21=Lfs(m,0x82000000u+20472u),f22=Lfs(m,0x82010000u+9420u),f24=Lfs(m,0x82010000u-27052u);
    do {
      r11=m.Word(r31+1200); f12=Lfs(m,r31+1108); f0=Lfs(m,r31+1124);
      uint32_t r26=0;
      const int64_t n80=int32_t(r11);
      f13=double(n80); f13=F(f13); f13=F(f13*f12);
      double f23=F(std::fma(f13,f16,f0));
      if(int32_t(r11)>0) do {
        uint64_t r10v=Lhz(m,r29);
        uint64_t r30v=Rot(r10v,20)&0xFFFFF;
        if(int32_t(r30v)!=0) {
          uint64_t r9v=Rot(r10v,24)&0xF;
          uint64_t r8v=Lhz(m,r29+2);
          uint64_t r11v=Rot(r30v,3)&0xFFFFFFF8u;
          double f11b=Lfs(m,r31+1112);
          r9v=uint32_t(r9v)&0xFFFF;
          r10v=uint64_t(int64_t(int16_t(uint16_t(r10v))));
          double f0b=Lfs(m,uint32_t(r11v)+r27);
          const int64_t s112b=int64_t(r9v);
          r9v=uint64_t(int64_t(int16_t(uint16_t(r8v))));
          const uint32_t r8b=r27+4;
          const int64_t s152=int64_t(r9v);
          double f10b=Lfs(m,uint32_t(r11v)+r8b);
          r11v=Rot(r10v,24)&0xFF000000u;
          r10v=Rot(r10v,28)&0xF0000000u;
          f10b=F(f10b-f0b);
          r11v=uint64_t(int64_t(int32_t(r11v)>>28));
          Stfs(m,r31+300,f25);
          r10v=uint64_t(int64_t(int32_t(r10v)>>28));
          const int64_t s160=int64_t(int32_t(r11v)),s144=int64_t(int32_t(r10v));
          double f13b=double(s112b);
          double f9b=double(s152);
          f13b=F(f13b); f9b=F(f9b);
          double f8b=double(s144);
          f10b=F(f10b*f13b);
          double f28=F(f13b*f22);
          const double f31=F(f9b*f24);
          f9b=double(s160);
          f8b=F(f8b);
          const double f27=F(std::fma(f10b,f21,f0b));
          f9b=F(f9b);
          f13b=F(f8b*f11b);
          f0b=F(f9b*f12);
          const double f29=F(std::fma(f13b,f24,f19)); Stfs(m,r31+296,f29);
          const double f30=F(std::fma(f0b,f24,f23)); Stfs(m,r31+288,f30);
          f0b=F(f27*f26);
          f13b=F(f0b+f31); Stfs(m,r31+292,f13b);
          f13b=F(f0b*f20);
          double f1b=Lfs(m,r31+1156);
          Stfs(m,r31+304,f0b); Stfs(m,r31+324,f0b); Stfs(m,r31+344,f0b); Stfs(m,r31+352,f13b);
          if(Guest82171BD8(m,r31,f1b)!=0) {
            f0b=Lfs(m,r31+1232);
            const uint32_t r3v=stack+128;
            f13b=Lfs(m,r31+1236);
            f0b=F(f30-f0b); Stfs(m,stack+128,f0b);
            f0b=F(f31-f13b);
            double f12b=Lfs(m,r31+1240);
            Stfs(m,stack+132,f0b);
            f0b=F(f29-f12b); Stfs(m,stack+136,f0b);
            Stfs(m,stack+140,f25);
            f1b=Guest821B03C8(m,r3v);
            f0b=Lfs(m,r31+1156);
            f13b=F(f0b*f17);
            f0b=F(-std::fma(f0b,f18,-f1b));
            f0b=F(f0b/f13b);
            if(!(f0b>f25)) {
              f0b=F(f25-f0b);
              if(f0b>f25) f0b=f25;
              uint32_t r10c=uint32_t(Rot(r30v,4)&0xFFFFFFF0u);
              f1b=f28;
              uint32_t r11c=stack+96;
              r10c=r10c+r31; r10c=r10c+396;
              { const uint32_t a=m.Word(r10c),b=m.Word(r10c+4),c=m.Word(r10c+8),d=m.Word(r10c+12);
                m.StoreWord(r11c,a); m.StoreWord(r11c+4,b); m.StoreWord(r11c+8,c); m.StoreWord(r11c+12,d); }
              f13b=Lfs(m,stack+108);
              f0b=F(f13b*f0b); Stfs(m,stack+108,f0b);
              f1b=NativeGuestSin(f1b);
              f0b=f1b; f1b=f28;
              f0b=F(f0b); f0b=F(f0b*f27); f28=F(f0b*f26);
              f1b=NativeGuestCos(f1b);
              f13b=F(f1b);
              uint32_t r11=stack+96;
              const uint32_t r10=uint32_t(Rot(r30v,2)&0xFFFFFFFCu);
              f12b=F(f30-f28);
              const uint32_t r4=stack+240;
              f0b=F(f27+f31);
              uint32_t r9=stack+96;
              f11b=F(f28+f30);
              uint32_t r8=m.Word(r11+0),r7=m.Word(r11+4),r6=m.Word(r11+8),r5=m.Word(r11+12);
              r11=m.Word(r10+r4);
              f13b=F(f13b*f27);
              uint32_t r3=m.Word(r9+0),r30=m.Word(r9+4),r24=m.Word(r9+8),r23=m.Word(r9+12);
              r9=r11+20;
              Stfs(m,r11+0,f12b); Stfs(m,r11+4,f0b);
              m.StoreWord(r9+0,r8); m.StoreWord(r9+4,r7);
              r7=stack+96;
              f13b=F(f13b*f26);
              m.StoreWord(r9+8,r6); m.StoreWord(r9+12,r5);
              r6=stack+96; r5=stack+96;
              f10b=F(f29-f13b); Stfs(m,r11+8,f10b);
              r11+=36;
              f9b=F(f13b+f29);
              r9=r11+20;
              Stfs(m,r11+0,f11b); Stfs(m,r11+8,f9b);
              m.StoreWord(r9+0,r3);
              Stfs(m,r11+4,f0b);
              m.StoreWord(r9+4,r30);
              r11+=36;
              m.StoreWord(r9+8,r24);
              f8b=f10b;
              m.StoreWord(r9+12,r23);
              r9=stack+96;
              r8=r11+20;
              f10b=F(f29-f28);
              Stfs(m,r11+8,f9b);
              f9b=f12b;
              Stfs(m,r11+0,f11b);
              f12b=F(f30-f13b);
              Stfs(m,r11+4,f31);
              r3=m.Word(r9+0);
              r11+=36;
              f11b=F(f28+f29);
              f13b=F(f13b+f30);
              r30=m.Word(r9+4);
              r23=r11+20;
              r24=m.Word(r9+8);
              uint32_t r22=m.Word(r9+12);
              r9=r23;
              m.StoreWord(r8+0,r3);
              r23=m.Word(r7+0);
              m.StoreWord(r8+4,r30); m.StoreWord(r8+8,r24); m.StoreWord(r8+12,r22);
              r8=m.Word(r7+4);
              Stfs(m,r11+0,f9b);
              r3=m.Word(r7+8);
              Stfs(m,r11+4,f31);
              r7=m.Word(r7+12);
              Stfs(m,r11+8,f8b);
              m.StoreWord(r9+0,r23);
              r11+=36;
              r30=m.Word(r6+0);
              m.StoreWord(r9+4,r8); m.StoreWord(r9+8,r3); m.StoreWord(r9+12,r7);
              r9=r11+20;
              r24=m.Word(r6+4); r22=m.Word(r6+8);
              Stfs(m,r11+0,f12b);
              r6=m.Word(r6+12);
              Stfs(m,r11+4,f0b); Stfs(m,r11+8,f11b);
              r11+=36;
              m.StoreWord(r9+0,r30); m.StoreWord(r9+4,r24);
              r8=r11+20;
              m.StoreWord(r9+8,r22); m.StoreWord(r9+12,r6);
              r9=stack+96;
              const uint32_t r21=m.Word(r5+0),r20=m.Word(r5+4);
              Stfs(m,r11+0,f13b); Stfs(m,r11+4,f0b);
              const uint32_t r19=m.Word(r5+8);
              Stfs(m,r11+8,f10b);
              r5=m.Word(r5+12);
              r11+=36;
              r3=m.Word(r9+0);
              m.StoreWord(r8+0,r21);
              r7=r11+20;
              m.StoreWord(r8+4,r20);
              r24=m.Word(r9+4);
              m.StoreWord(r8+8,r19);
              r23=m.Word(r9+8);
              m.StoreWord(r8+12,r5);
              r9=m.Word(r9+12);
              r5=stack+176;
              Stfs(m,r11+0,f13b);
              m.StoreWord(r7+0,r3);
              Stfs(m,r11+4,f31);
              m.StoreWord(r7+4,r24);
              Stfs(m,r11+8,f10b);
              r11+=36;
              m.StoreWord(r7+8,r23);
              r8=stack+96;
              m.StoreWord(r7+12,r9);
              r9=m.Word(r10+r5);
              r30=r11+36;
              r6=r11+20;
              Stfs(m,r11+0,f12b); Stfs(m,r11+4,f31);
              r22=m.Word(r8+0);
              Stfs(m,r11+8,f11b);
              r11=r9+2;
              r3=m.Word(r8+4); r24=m.Word(r8+8); r8=m.Word(r8+12);
              m.StoreWord(r10+r4,r30);
              m.StoreWord(r10+r5,r11);
              m.StoreWord(r6+0,r22); m.StoreWord(r6+4,r3); m.StoreWord(r6+8,r24); m.StoreWord(r6+12,r8);
              m.StoreWord(r31+1168,m.Word(r31+1168)+1);
            }
          }
        }
        r11=m.Word(r31+1200);
        ++r26;
        f12=Lfs(m,r31+1108);
        r29+=4;
        f23=F(f12+f23);
      } while(int32_t(r26)<int32_t(r11));
      r11=m.Word(r31+1200);
      ++r25;
      f0=Lfs(m,r31+1112);
      f19=F(f0+f19);
    } while(int32_t(r25)<int32_t(r11));
  }
  r25=stack+180;
  r29=r31+668;
  uint32_t r26=15;
  do {
    const uint32_t r27=m.Word(r25);
    if(r27!=0) {
      const uint32_t r30b=r31+1256;
      Guest8218D440(m,r30b,m.Word(r29),log);
      Guest8218D3F0(m,r30b,13,m.Word(r28),r27,log);
    }
    --r26; r29+=16; r28+=12; r25+=4;
  } while(r26!=0);
}
// sub_82172698 (r3 grass, r4 context), its frame at `stack` (after stwu -288)
// and 82171F58's at `inner`.
void Guest82172698(const ImageMemory& m,uint32_t r3,uint32_t r4,uint32_t stack,uint32_t inner,GuestGrassLog& log) {
  const uint32_t r31=r3;
  uint32_t r30=0;
  uint32_t r11=Lbz(m,r31+1164);
  m.StoreWord(r31+1168,r30);
  if(r11==0) return;
  r11=Lbz(m,r31+1248);
  if(r11==0) return;
  r11=m.Word(r4+16);
  uint32_t r10=stack+96;
  r11+=416;
  for(uint32_t ctr=8;ctr;--ctr) {
    const uint32_t hi=m.Word(r11),lo=m.Word(r11+4);
    r11+=8; m.StoreWord(r10,hi); m.StoreWord(r10+4,lo); r10+=8;
  }
  double f13=Lfs(m,r31+1112),f11=Lfs(m,r31+1108);
  m.StoreWord(r31+1228,r4);
  double f0=Lfs(m,0x82000000u+2252u);
  double f12=F(f0/f13);
  f13=Lfs(m,r31+1156);
  f0=F(f0/f11);
  Copy16(m,stack+144,r31+1232);
  f11=F(f13*f12);
  f13=F(f0*f13);
  m.StoreWord(stack+84,Fctiwz(f11));
  m.StoreWord(stack+80,Fctiwz(f13));
  r10=m.Word(stack+80);
  r11=m.Word(stack+84);
  if(int32_t(r10)<int32_t(r11)) r10=r11;
  f11=Lfs(m,r31+1124);
  f13=Lfs(m,r31+1232);
  r11=m.Word(r31+1200);
  f13=F(f13-f11);
  const double f10=Lfs(m,r31+1128);
  f11=Lfs(m,r31+1240);
  r10=r11+r10;
  f11=F(f11-f10);
  const uint32_t r19=r31+1256;
  if(r11==0) Trap();                                     // twllei r11,0 (x3)
  f0=F(f13*f0);
  f13=F(f11*f12);
  m.StoreWord(stack+80,Fctiwz(f0));
  uint32_t r9=r10-1;
  const uint32_t f0w=Fctiwz(f13);
  r10=(r9<<1)|(r9>>31);                                  // rotlwi r10,r9,1
  const uint32_t r20=Divw(r9,r11);
  r9=m.Word(stack+80);
  r10=r10-1;
  r10=r11&~r10;
  uint32_t r29=Divw(r9,r11);
  if(r10==0xFFFFFFFFu) Trap();
  r10=(r9<<1)|(r9>>31);
  m.StoreWord(stack+80,f0w);
  r9=m.Word(stack+80);
  r10=r10-1;
  uint32_t r28=Divw(r9,r11);
  r10=r11&~r10;
  if(r10==0xFFFFFFFFu) Trap();
  r10=(r9<<1)|(r9>>31);
  r10=r10-1;
  r11=r11&~r10;
  if(r11==0xFFFFFFFFu) Trap();
  ++log.pushed;                                          // 8218D380
  log.blend_src=6; log.blend_dst=7; log.depth_write=0;   // 8218D398(r19, 0, 0)
  (void)r19;
  Guest82171F58(m,r31,r29,r28,inner,log);
  uint32_t r22=1;
  if(!(int32_t(r20)<1)) {
    uint32_t r25=r29+1;
    const uint32_t r24=r28-1,r23=r29-1,r21=r28-r29,r18=uint32_t(-1)-r29;
    do {
      r11=r30-1;
      if(!(int32_t(r11)>int32_t(r22))) {
        r11=r22-r30;
        const uint32_t r27=r24+r30,r26=r21+r25;
        r29=r23+r30;
        r28=r11+2;
        do {
          Guest82171F58(m,r31,r29,r27,inner,log);
          Guest82171F58(m,r31,r29,r26,inner,log);
          --r28; ++r29;
        } while(r28!=0);
      }
      r10=r18+r25;
      if(!(int32_t(r30)>int32_t(r10))) {
        r11=r24+r30;
        r10=r10-r30;
        const uint32_t r27=r23+r30;
        r29=r11+1;
        r28=r10+1;
        do {
          Guest82171F58(m,r31,r27,r29,inner,log);
          Guest82171F58(m,r31,r25,r29,inner,log);
          --r28; ++r29;
        } while(r28!=0);
      }
      ++r22; --r30; ++r25;
    } while(!(int32_t(r22)>int32_t(r20)));
  }
  --log.pushed;                                          // 8218D430
  r11=m.Word(r31+1168);
  r10=m.Word(r31+1172);
  if(int32_t(r11)>int32_t(r10)) m.StoreWord(r31+1172,r11);
}
// A grass world: the camera `camera_at` looking along +z rotated by `yaw`
// (row-vector view matrix, translation -camera * R), the wire world's frustum
// (near 1, far 1000, 45-degree sides), depth scale -1 (depth = view z), and a
// 7 x 7 grid of 4 x 4 sub-cells of 1 unit from (-14, -14).
struct GrassWorld {
  static constexpr uint32_t context=0x1000,scene=0x1400,grass=0x2000,declarations=0x2800,slots=0x3000,heights=0x3400,
    blades=0x4000,lists=0x20000,list_bytes=0x2000,stack=0xC000,inner=0xD000;
  static constexpr uint32_t width=7,depth=7,n=4;
  static constexpr uint32_t utility() { return grass+NativeGrassMap::utility; }
};
void GrassConstants(const ImageMemory& m) {
  m.StoreWord(0x820009A4,0x00000000u); m.StoreWord(0x820008CC,0x3f800000u); m.StoreWord(0x820008D4,0x3f000000u);
  m.StoreWord(0x820021E4,0x3e800000u); m.StoreWord(0x820024A0,0x3f400000u); m.StoreWord(0x82004FF8,0x3d888889u);
  m.StoreWord(0x82009654,0x3d800000u); m.StoreWord(0x820124CC,0x3ed67750u); m.StoreWord(0x820124D0,0x3fddb3d7u);
  m.StoreWord(0x82019A20,0x47800000u); m.StoreWord(0x82018EB4,0x477fff00u);   // 821C0C00's mode-2 scale and clamp
  m.StoreWord(0x82008888+13*8,4); m.StoreWord(0x82008888+13*8+4,0);   // quad list: 4 per primitive
}
void GrassCamera(const ImageMemory& m,std::array<float,3> at,float yaw) {
  using W=GrassWorld;
  const float c=std::cos(yaw),s=std::sin(yaw);
  const float r[3][3]{{c,0,-s},{0,1,0},{s,0,c}};
  float view[16]{r[0][0],r[0][1],r[0][2],0, r[1][0],r[1][1],r[1][2],0, r[2][0],r[2][1],r[2][2],0, 0,0,0,1};
  for(int j=0;j<3;++j) view[12+j]=-(at[0]*r[0][j]+at[1]*r[1][j]+at[2]*r[2][j]);
  for(uint32_t i=0;i<16;++i) m.StoreFloat(W::scene+96+i*4,view[i]);
  constexpr float side=0.70710678f;
  float frustum[26]{};
  frustum[8]=side; frustum[10]=-side; frustum[12]=-side; frustum[14]=-side;
  frustum[17]=side; frustum[18]=-side; frustum[21]=-side; frustum[22]=-side; frustum[24]=1; frustum[25]=1000;
  for(uint32_t i=0;i<26;++i) m.StoreFloat(W::scene+288+i*4,frustum[i]);
  // The camera world (scene+416): its translation row is what 82172698 copies.
  const float world[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, at[0],at[1],at[2],1};
  for(uint32_t i=0;i<16;++i) m.StoreFloat(W::scene+416+i*4,world[i]);
  m.StoreWord(W::context+16,W::scene);
  m.StoreFloat(W::context+8,-1.0f);
}
void BuildGrassWorld(const ImageMemory& m,std::mt19937& random) {
  using W=GrassWorld; using G=NativeGrassMap;
  GrassConstants(m);
  std::uniform_real_distribution<float> unit(0.f,1.f);
  const uint32_t g=W::grass;
  m.StoreWord(g,G::vtable); m.StoreWord(g+52,2); m.StoreFloat(g+56,1.0f);
  m.StoreByte(g+G::loaded,1); m.StoreByte(g+G::enabled,1);
  m.StoreFloat(g+G::sub_x,1.0f); m.StoreFloat(g+G::sub_z,1.0f);
  m.StoreFloat(g+G::origin_x,-14.f); m.StoreFloat(g+G::origin_z,-14.f);
  m.StoreFloat(g+G::cell_x,4.f); m.StoreFloat(g+G::cell_z,4.f); m.StoreFloat(g+G::half_x,2.f); m.StoreFloat(g+G::half_z,2.f);
  m.StoreFloat(g+G::reach,9.f);
  m.StoreWord(g+G::subcells,W::n); m.StoreWord(g+G::width,W::width); m.StoreWord(g+G::depth,W::depth);
  m.StoreWord(g+G::slots,W::slots); m.StoreWord(g+G::heights,W::heights); m.StoreWord(g+G::blades,W::blades);
  m.StoreWord(g+1168,0); m.StoreWord(g+1172,0);
  // The Utility object's declaration iterator (8218D440's traps pass).
  m.StoreWord(W::utility()+G::declarations,W::declarations); m.StoreWord(W::declarations+4,0x1111);
  m.StoreWord(W::utility()+G::declaration,0x2222);
  // Slots: row-major, a few empty (-1); each with a height pair and n*n blades.
  uint32_t slot=0;
  for(uint32_t z=0;z<W::depth;++z) for(uint32_t x=0;x<W::width;++x) {
    const bool empty=(x*3+z*5)%7==2;
    m.StoreWord(W::slots+(z*W::width+x)*4,empty?0xFFFFFFFFu:slot);
    if(empty) continue;
    const float lo=-1.f+unit(random),hi=lo+0.5f+3*unit(random);
    m.StoreFloat(W::heights+slot*8,lo); m.StoreFloat(W::heights+slot*8+4,hi);
    for(uint32_t b=0;b<W::n*W::n;++b) {
      const uint32_t type=random()%5==0?0:1+random()%15;
      const uint32_t hw=type<<12|(random()%16)<<8|(random()%16)<<4|random()%16;
      const int16_t height=int16_t(int32_t(random()%72)-16);
      const uint32_t at=W::blades+(slot*W::n*W::n+b)*4;
      m.StoreByte(at,uint8_t(hw>>8)); m.StoreByte(at+1,uint8_t(hw));
      m.StoreByte(at+2,uint8_t(uint16_t(height)>>8)); m.StoreByte(at+3,uint8_t(uint16_t(height)));
    }
    ++slot;
  }
  // Per type: colour and (base, top) sizes; per list: texture and a vertex
  // vector whose UV words are arbitrary (they are the vector's, not rebuilt).
  for(uint32_t t=1;t<=G::types;++t) {
    for(uint32_t c=0;c<4;++c) m.StoreFloat(g+G::colours+t*16+c*4,0.1f+0.9f*unit(random));
    const float base=0.2f+unit(random),top=base+unit(random)*2;
    m.StoreFloat(G::blade_table+t*8,base); m.StoreFloat(G::blade_table+t*8+4,top);
  }
  for(uint32_t i=0;i<G::types;++i) {
    m.StoreWord(g+G::textures+i*16,0x9000+i);
    const uint32_t begin=W::lists+i*W::list_bytes;
    m.StoreWord(g+G::lists+i*12,begin); m.StoreWord(g+G::lists+i*12+4,begin); m.StoreWord(g+G::lists+i*12+8,begin+W::list_bytes);
    for(uint32_t v=0;v<W::list_bytes/36;++v) {
      m.StoreFloat(begin+v*36+12,unit(random)); m.StoreFloat(begin+v*36+16,unit(random));
      m.StoreWord(begin+v*36+0,0xDEADBEEF);   // positions and colours the guest overwrites
    }
  }
}
// One grass build, native and guest, compared draw for draw and byte for byte.
NativeGrassMapStats CompareGrass(const ImageMemory& m,std::set<uint32_t>& textures) {
  using W=GrassWorld;
  NativeGrassMapStats stats;
  std::vector<std::array<int32_t,2>> cells;
  const auto before=m.bytes;
  const auto draws=BuildNativeGrassMapDraws(m,W::grass,W::context,&stats,&cells);
  Require(m.bytes==before,"native grass build wrote guest memory");
  GuestGrassLog log;
  Guest82172698(m,W::grass,W::context,W::stack,W::inner,log);
  Require(log.pushed==0 && log.blend_src==6 && log.blend_dst==7 && log.depth_write==0,"guest grass state bracket");
  Require(cells==log.cells,"native grass cells differ from the guest's ring walk");
  Require(draws.size()==log.draws.size() && stats.draws==draws.size(),"native grass draw count");
  Require(stats.drawn==m.Word(W::grass+1168),"native grass blades drawn differ from +1168");
  for(size_t i=0;i<draws.size();++i) {
    const auto& draw=draws[i];
    const auto& call=log.draws[i];
    Require(draw.kind==NativeEffectDraw::Kind::RibbonQuads && draw.technique==NativeEffectTechnique::Utility3DTexA &&
      draw.effect==call.utility && draw.texture==call.texture && draw.primitive()==call.primitive &&
      draw.vertex_count()==call.vertices && call.vertices==call.count*4 && draw.stride()==36 &&
      draw.blend==kNativeEffectBlendAlpha && draw.sets_depth_write && !draw.depth_write && draw.state_before_activation(),
      "native grass draw contract");
    Require(EncodeNativeEffectVertices(draw,0,draw.vertex_count())==call.bytes,"native grass vertices differ from the guest's");
    const auto calls=NativeEffectDrawCalls(draw);
    Require(calls.size()==1 && calls[0]==std::pair<uint32_t,uint32_t>{0,draw.vertex_count()},"native grass draw call");
    textures.insert(draw.texture);
  }
  return stats;
}
void TestGrassMapRings() {
  // Against the ring's geometry: the camera's cell, then per ring the top and
  // bottom rows pairwise (x ascending), then the two columns pairwise; every
  // cell of the (2r+1)^2 square once. Negative and far-off centres included.
  for(const auto& [cx,cz]:std::vector<std::array<int32_t,2>>{{0,0},{3,-2},{-5,7},{100000,-100000}})
    for(int32_t rings=-1;rings<=5;++rings) {
      std::vector<std::array<int32_t,2>> got,expected{{cx,cz}};
      WalkNativeGrassMapRings(cx,cz,rings,[&](int32_t x,int32_t z) { got.push_back({x,z}); });
      for(int32_t ring=1;ring<=rings;++ring) {
        for(int32_t x=cx-ring;x<=cx+ring;++x) { expected.push_back({x,cz-ring}); expected.push_back({x,cz+ring}); }
        for(int32_t z=cz-ring+1;z<=cz+ring-1;++z) { expected.push_back({cx-ring,z}); expected.push_back({cx+ring,z}); }
      }
      Require(got==expected,"grass ring order");
      std::set<std::array<int32_t,2>> unique(got.begin(),got.end());
      const size_t side=size_t(std::max(rings,0))*2+1;
      Require(unique.size()==got.size() && got.size()==side*side,"grass rings visit a cell twice or miss one");
    }
  // The fctiwz/divw edges 82172698 relies on.
  Require(NativeGuestFctiwz(std::nan(""))==INT32_MIN && NativeGuestFctiwz(3e9)==INT32_MAX && NativeGuestFctiwz(-3e9)==INT32_MIN &&
    NativeGuestFctiwz(-2147483648.5)==INT32_MIN && NativeGuestFctiwz(-7.9)==-7 && NativeGuestFctiwz(7.9)==7,"fctiwz edges");
  Require(NativeGrassMapDivw(-7,4)==-1 && NativeGrassMapDivw(7,-4)==-1,"divw truncates toward zero");
  bool zero=false,overflow=false;
  try { NativeGrassMapDivw(5,0); } catch(const std::exception&) { zero=true; }
  try { NativeGrassMapDivw(INT32_MIN,-1); } catch(const std::exception&) { overflow=true; }
  Require(zero && overflow,"divw traps not refused");
}
void TestGrassMap() {
  using W=GrassWorld; using G=NativeGrassMap;
  ImageMemory m;
  std::mt19937 random(0x82171F58u);
  BuildGrassWorld(m,random);
  std::set<uint32_t> textures;
  NativeGrassMapStats total;
  uint32_t visible_cells=0;
  // Cameras: off the grid's edge (the rings leave it), centred, and rotated
  // so the frustum culls cells on either side.
  const std::array<std::array<float,4>,8> cameras{{
    {-7.5f,1.25f,0.3f,0.f},{-7.5f,1.25f,0.3f,1.1f},{0.2f,0.75f,-1.7f,-0.6f},{9.9f,2.5f,11.3f,3.0f},
    {-13.1f,0.5f,-13.4f,0.8f},{3.3f,1.5f,4.4f,-2.2f},{0.f,1.f,0.f,0.3f},{-20.f,1.f,-20.f,0.785f}}};
  for(const auto& camera:cameras) {
    GrassCamera(m,{camera[0],camera[1],camera[2]},camera[3]);
    const auto stats=CompareGrass(m,textures);
    visible_cells+=stats.cells-stats.outside-stats.empty-stats.culled;
    total.cells+=stats.cells; total.outside+=stats.outside; total.empty+=stats.empty; total.culled+=stats.culled;
    total.blades+=stats.blades; total.blade_culled+=stats.blade_culled; total.faded+=stats.faded; total.drawn+=stats.drawn;
    Require(stats.blades==stats.blade_culled+stats.faded+stats.drawn,"grass blade tally");
  }
  Require(total.outside && total.empty && total.culled && visible_cells>=4,"grass cells did not reach every decision");
  Require(total.blade_culled && total.faded && total.drawn,"grass blades did not reach every decision");
  Require(textures.size()==G::types,"grass draws did not cover every texture batch");
  // +1172 keeps the peak across builds.
  Require(m.Word(W::grass+1172)>=m.Word(W::grass+1168),"guest grass peak");
  // Not loaded or not enabled: nothing, and the guest draws nothing either.
  for(const uint32_t flag:{G::loaded,G::enabled}) {
    m.StoreByte(W::grass+flag,0);
    NativeGrassMapStats stats;
    GuestGrassLog log;
    Guest82172698(m,W::grass,W::context,W::stack,W::inner,log);
    Require(BuildNativeGrassMapDraws(m,W::grass,W::context,&stats).empty() && stats.skipped==1 && log.draws.empty() && log.cells.empty(),
      "disabled grass drew");
    m.StoreByte(W::grass+flag,1);
  }
  // A zero sub-cell count traps in both.
  m.StoreWord(W::grass+G::subcells,0);
  bool native=false,guest=false;
  try { BuildNativeGrassMapDraws(m,W::grass,W::context); } catch(const std::exception&) { native=true; }
  try { GuestGrassLog log; Guest82172698(m,W::grass,W::context,W::stack,W::inner,log); } catch(const std::exception&) { guest=true; }
  Require(native && guest,"zero sub-cell count not trapped");
  m.StoreWord(W::grass+G::subcells,W::n);
  // The declaration traps of 8218D440, once something draws.
  GrassCamera(m,{-7.5f,1.25f,0.3f},0.f);
  m.StoreWord(W::utility()+G::declaration,m.Word(W::declarations+4));
  native=false;
  try { BuildNativeGrassMapDraws(m,W::grass,W::context); } catch(const std::exception&) { native=true; }
  Require(native,"grass draw without a declaration");
  m.StoreWord(W::utility()+G::declaration,0x2222);
  // The Utility technique: +172, material [+188], sampler list +200.
  Require(NativeEffectTechniqueOffset(NativeEffectTechnique::Utility3DTexA)==172 &&
    NativeEffectSamplerListOffset(NativeEffectTechnique::Utility3DTexA)==200,"utility technique offsets");
  m.StoreWord(W::utility()+188,0x5000);
  Require(NativeEffectTechniqueMaterial(m,W::utility(),NativeEffectTechnique::Utility3DTexA)==0x5000,"utility technique material");
  m.StoreWord(W::utility()+200+4,0x5100); m.StoreWord(W::utility()+200+12,1); m.StoreWord(0x5100,0x5200);
  BindNativeEffectTexture(m,W::utility(),NativeEffectTechnique::Utility3DTexA,0x9003);
  Require(m.Word(0x5200+4)==0x9003,"utility texture not bound into its sampler list");
  // A call past the immediate limit is split on whole quads.
  NativeEffectDraw big=MakeNativeGrassMapDraw(W::utility(),std::vector<NativeRibbonVertex>(kNativeGrassMapCallVertices+8),1);
  const auto calls=NativeEffectDrawCalls(big);
  Require(calls.size()==2 && calls[0]==std::pair<uint32_t,uint32_t>{0,kNativeGrassMapCallVertices} &&
    calls[1]==std::pair<uint32_t,uint32_t>{kNativeGrassMapCallVertices,8},"grass call split");
}
// The map-effect walk's routes in list order, and where the filed grass map
// lands among the transparent items.
void TestMapEffectPlan() {
  using W=GrassWorld;
  ImageMemory m;
  constexpr uint32_t effect=0x6000,camera=0x6800,wire=0x7000,records=0x7400,grass2=0x8000,sky=0x8800,other_sky=0x8900;
  BuildWireWorld(m,W::context,W::scene,effect,camera);   // effect globals and wire constants
  std::mt19937 random(0x820B35A0u);
  BuildGrassWorld(m,random);
  GrassCamera(m,{-7.5f,1.25f,0.3f},0.f);
  // A wire record in front of the grass camera (view z 3..6).
  m.StoreWord(wire,NativeElectricWire::vtable);
  m.StoreFloat(wire+400,0.5f);
  WireRecord(m,records,true,{-8.5f,1.5f,3.5f},{-6.5f,1.5f,6.5f},{-7.5f,1.5f,5.f},4,0.3f);
  m.StoreWord(wire+388,records); m.StoreWord(wire+392,records+96);
  // A second grass map whose key (+56 * 65536 = 65.5) is below 256: filed, never drawn.
  m.StoreWord(grass2,NativeGrassMap::vtable); m.StoreWord(grass2+52,2); m.StoreFloat(grass2+56,0.001f);
  const auto member=[](uint32_t object,uint32_t vtable,uint32_t render,int32_t mode,bool hidden) {
    NativeMapEffectMember out;
    out.object=object; out.vtable=vtable; out.render=render; out.mode=mode; out.hidden=hidden;
    out.kind=ClassifyNativeMapEffect(vtable);
    return out;
  };
  const std::vector<NativeMapEffectMember> members{
    member(wire,0x82002744,0x820B8D28,0,false),        // A: immediate strip
    member(W::grass,0x820124DC,0x82172698,2,false),    // G1: filed, key 65535
    member(other_sky,0x8200284C,0x820BB270,0,false),   // not the current sky: skipped
    member(sky,0x8200284C,0x820BB270,0,false),         // the sky
    member(wire,0x82002744,0x820B8D28,0,true),         // hidden
    member(W::grass,0x820124DC,0x82172698,0,false),    // G2: mode 0, drawn inside the walk
    member(wire,0x82002744,0x820B8D28,1,false),        // filed wire: unsupported
    member(W::grass,0x820124DC,0x82172698,1,false),    // mode-1 grass: unsupported
    member(grass2,0x820124DC,0x82172698,2,false),      // undrawn key
  };
  std::vector<std::string> failures;
  const auto before=m.bytes;
  const auto plan=PlanNativeMapEffects(m,members,sky,W::scene,W::context,WireView(m,W::scene),[&](const std::string& what) { failures.push_back(what); });
  Require(m.bytes==before && failures.empty(),"map-effect plan wrote memory or failed");
  const auto wire_draws=BuildNativeElectricWireDraws(m,wire,W::scene,WireInputs(m,W::scene));
  const auto grass_draws=BuildNativeGrassMapDraws(m,W::grass,W::context);
  Require(!wire_draws.empty() && !grass_draws.empty(),"plan fixture draws nothing");
  const auto same=[](const std::vector<NativeEffectDraw>& a,const std::vector<NativeEffectDraw>& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i)
      if(a[i].texture!=b[i].texture || a[i].technique!=b[i].technique ||
         EncodeNativeEffectVertices(a[i],0,a[i].vertex_count())!=EncodeNativeEffectVertices(b[i],0,b[i].vertex_count())) return false;
    return true;
  };
  Require(plan.segments.size()==3 && !plan.segments[0].sky && same(plan.segments[0].draws,wire_draws) &&
    plan.segments[1].sky && plan.segments[1].draws.empty() && !plan.segments[2].sky && same(plan.segments[2].draws,grass_draws),
    "map-effect segments: wire strips, then the sky, then the mode-0 grass");
  Require(plan.filings==2 && plan.undrawn_keys==1 && plan.filed.size()==1,"map-effect filings");
  const auto& item=plan.filed[0];
  Require(item.object==W::grass && item.key==0xFFFF && item.order==0 && item.slot4==0x82172698 &&
    item.type==NativeEffectClass::GrassMap && same(item.draws,grass_draws),"filed grass item");
  Require(plan.unsupported.size()==2 && plan.unsupported[0].vtable==0x82002744 && plan.unsupported[0].mode==1 &&
    plan.unsupported[1].vtable==0x820124DC && plan.unsupported[1].mode==1,"unsupported members");
  Require(plan.grass.objects==2 && plan.wires.records==1,"plan statistics");
  // No context: the grass maps fail (reported), the rest is unchanged.
  const auto bare=PlanNativeMapEffects(m,members,sky,W::scene,0,WireView(m,W::scene),[&](const std::string& what) { failures.push_back(what); });
  Require(failures.size()==3 && bare.filed.empty() && bare.segments.size()==2,"grass without a context");
  // The world list decides where the map effects' filings fall.
  constexpr uint32_t owner=0x0800,manager_object=0x0A00,effects_object=0x0A80,n0=0x0900,n1=0x0940,n2=0x0980,end=0x09C0;
  m.StoreWord(manager_object,kNativeMapEffectManagerVtable); m.StoreWord(effects_object,NativeEffectList::manager_vtable);
  const auto world=[&](uint32_t first,uint32_t second) {
    m.StoreWord(owner+44,n0); m.StoreWord(owner+56,end);
    m.StoreWord(n0,n1); m.StoreWord(n0+8,0);
    m.StoreWord(n1,n2); m.StoreWord(n1+8,first);
    m.StoreWord(n2,end); m.StoreWord(n2+8,second);
  };
  world(manager_object,effects_object);
  Require(NativeWorldListIndex(m,owner,kNativeMapEffectManagerVtable)==1 && NativeWorldListIndex(m,owner,NativeEffectList::manager_vtable)==2 &&
    NativeWorldListIndex(m,owner,0x82000000)==-1 && NativeMapEffectsFiledBeforeEffects(m,owner),"map effects first");
  world(effects_object,manager_object);
  Require(!NativeMapEffectsFiledBeforeEffects(m,owner),"effects first");
  world(manager_object,0x0B00);
  m.StoreWord(0x0B00,0x82000000);
  Require(NativeMapEffectsFiledBeforeEffects(m,owner),"no effects manager");
  // Models (3 filings), the map effects (2) and the effects (4) in one sequence.
  const auto first=NativeMapEffectFilingBases(true,3,2,4),second=NativeMapEffectFilingBases(false,3,2,4);
  Require(first.map_effects==3 && first.effects==5 && second.map_effects==7 && second.effects==3,"filing bases");
  // Key 65535 ties go by filing order; the grass item draws before every lower key.
  for(const bool map_first:{true,false}) {
    const auto bases=NativeMapEffectFilingBases(map_first,3,2,4);
    std::map<std::pair<uint16_t,uint32_t>,std::string> tags;
    const auto tagged=[&](uint16_t key,uint32_t order,std::string tag) {
      tags[{key,order}]=std::move(tag);
      return NativeTransparentItem{key,order,{}};
    };
    std::vector<std::vector<NativeTransparentItem>> sources(3);
    sources[0].push_back(tagged(300,1,"model 300")); sources[0].push_back(tagged(0xFFFF,0,"model 65535"));
    sources[1].push_back(tagged(0xFFFF,bases.effects,"effect 65535")); sources[1].push_back(tagged(500,bases.effects+1,"effect 500"));
    sources[2].push_back(tagged(item.key,item.order+bases.map_effects,"grass"));
    sources[2].push_back(tagged(65,1+bases.map_effects,"undrawn"));
    const auto sequence=MergeNativeTransparentItems(std::move(sources));
    std::vector<std::string> drawn;
    for(const auto& entry:sequence) drawn.push_back(tags.at({entry.key,entry.order}));
    const std::vector<std::string> expected=map_first?
      std::vector<std::string>{"model 65535","grass","effect 65535","effect 500","model 300"}:
      std::vector<std::string>{"model 65535","effect 65535","grass","effect 500","model 300"};
    Require(drawn==expected,"transparent order of the grass item");
  }
}
}
int main() {
  try {
    TestOrder(); TestMutation(); TestCensus(); TestElectricWire(); TestElectricWirePointsEquivalence(); TestElectricWireRandomized();
    TestMapEffectMembers(); TestGrassMapRings(); TestGrassMap(); TestMapEffectPlan();
  } catch(const std::exception& error) {
    std::cerr<<"native map effect tests failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native map effect tests passed\n";
  return 0;
}
