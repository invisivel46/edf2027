#include "native_graphics/native_map_effects.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
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
// Heap at [0, 0x10000) and the image's .rdata/.data from 0x82000000, big-endian.
struct ImageMemory {
  static constexpr uint32_t heap=0x10000,image=0x82000000u,image_size=0x580000;
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
NativeEffectInputs WireInputs(const ImageMemory& m,uint32_t scene) {
  std::array<uint32_t,16> view{};
  for(uint32_t i=0;i<16;++i) view[i]=m.Word(scene+96+i*4);
  return ReadNativeEffectInputs(m,view);
}
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
  Require(refused && kNativeGrassMapSupport==NativeGrassMapSupport::Unsupported,"non-manager walked");
}
}
int main() {
  try {
    TestOrder(); TestMutation(); TestCensus(); TestElectricWire(); TestElectricWirePointsEquivalence(); TestElectricWireRandomized();
    TestMapEffectMembers();
  } catch(const std::exception& error) {
    std::cerr<<"native map effect tests failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native map effect tests passed\n";
  return 0;
}
