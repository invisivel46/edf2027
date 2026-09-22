#include "edf2017_funcs.42.h"
#include "edf2017_funcs.10.h"
#include "edf2017_funcs.48.h"
#include "native_graphics/native_constant_ownership.h"
#include "native_graphics/native_pacing.h"
#include "native_graphics/native_model_buffers.h"
#include "native_graphics/native_shader_state.h"
#include "native_graphics/native_declarations.h"
#include "native_graphics/native_render_state_snapshot.h"
#include "native_graphics/native_submission_flush.h"
#include "native_graphics/native_buffer_write_frame.h"
#include "native_graphics/native_cache_flush.h"
#include "native_graphics/native_scene_handoff.h"
#include "native_graphics/native_scene_execution.h"
#include "native_graphics/native_scene_world_restore.h"
#include "native_graphics/native_static_group_eligibility.h"
#include "native_static_group_gpu.h"
REX_EXTERN(__imp__sub_821D9600);

namespace edf::native {
NativeSceneExecution fixture_scene_execution;
void EnterNativeSceneBoundary(NativeSceneBoundary boundary) { fixture_scene_execution.Enter(boundary); }
}
#include "native_graphics/native_scene_geometry_install.h"
#include "native_graphics/native_material_cpu_program.h"
#include "native_graphics/native_shader_binding.h"
#include "native_graphics/native_indexed_completion.h"
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <windows.h>
#include "native_graphics/guest_raw_store_observer.h"
struct FlushReader {
  uint8_t* base;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t address) const { return REX_LOAD_U32(address); }
  const uint8_t* Bytes(uint32_t address,size_t) const { return base+address; }
  void StoreByte(uint32_t address,uint8_t value) const { REX_STORE_U8(address,value); }
  void StoreWord(uint32_t address,uint32_t value) const { REX_STORE_U32(address,value); }
};
struct GeometryFixtureReader {
  uint8_t* base;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t address) const { return REX_LOAD_U32(address); }
  uint64_t DoubleWord(uint32_t address) const { return REX_LOAD_U64(address); }
  const uint8_t* Bytes(uint32_t address,size_t) const { return base+address; }
  const uint8_t* WritableBytes(uint32_t address,size_t,size_t) const { return base+address; }
  void StoreWord(uint32_t address,uint32_t value) const { REX_STORE_U32(address,value); }
  void StoreDoubleWord(uint32_t address,uint64_t value) const { REX_STORE_U64(address,value); }
  void StoreByte(uint32_t address,uint8_t value) const { REX_STORE_U8(address,value); }
};
static void TestObservedVector(uint8_t* base,simde__m128i* destination,simde__m128i value) {
  simde_mm_store_si128(destination,value);
}
static void* TestObservedFill(uint8_t* base,void* destination,int value,size_t bytes) {
  return memset(destination,value,bytes);
}
#undef simde_mm_store_si128
#undef memset
REX_EXTERN(__imp__edf_native_indexed_cpu_tail);
REX_EXTERN(__imp__sub_82137410);
REX_EXTERN(__imp__sub_821375C0);
REX_EXTERN(__imp__sub_821B8E48);
REX_EXTERN(__imp__sub_82149248);
REX_EXTERN(__imp__sub_82149358);
REX_EXTERN(__imp__edf_native_immediate_cpu_tail);
REX_EXTERN(sub_821FE358);
REX_EXTERN(__imp__sub_8213DB60);
REX_EXTERN(__imp__sub_8213DC20);
REX_EXTERN(__imp__sub_8213D938);
REX_EXTERN(__imp__sub_8213EAB0);
REX_EXTERN(__imp__sub_8213D750);
REX_EXTERN(__imp__edf_native_derived_cpu_tail);
REX_EXTERN(__imp__sub_8213E070);
REX_EXTERN(__imp__sub_8213E800);
REX_EXTERN(__imp__sub_8213E678);
REX_EXTERN(__imp__sub_8213E748);
REX_EXTERN(__imp__edf_native_shader_output_cpu_tail);
REX_EXTERN(__imp__sub_82135418);
REX_EXTERN(__imp__sub_8213E950);
REX_EXTERN(__imp__sub_82141AB8);
REX_EXTERN(__imp__edf_native_shader_upload_cpu_tail);
REX_EXTERN(__imp__sub_8213ECB0);
REX_EXTERN(__imp__edf_native_main_state_cpu_tail);
REX_EXTERN(__imp__sub_8213EB68);
REX_EXTERN(__imp__edf_native_shader_cache_cpu_tail);
REX_EXTERN(__imp__sub_8213D298);
REX_EXTERN(__imp__sub_8213D1C8);
REX_EXTERN(__imp__edf_native_device_reset);
REX_EXTERN(__imp__edf_native_device_drain);
REX_EXTERN(__imp__sub_8213C928);
REX_EXTERN(__imp__sub_8213D0F8);
REX_EXTERN(__imp__sub_8213C5F0);
REX_EXTERN(__imp__sub_8213BE68);
REX_EXTERN(__imp__edf_native_cache_range_cpu_tail);
REX_EXTERN(__imp__edf_native_cache_reservation_cpu_tail);
REX_EXTERN(__imp__sub_8213CF60);
REX_EXTERN(__imp__sub_8213C410);
REX_EXTERN(__imp__edf_test_ring_copy_original);
REX_EXTERN(__imp__edf_native_ring_copy_cpu_tail);
REX_EXTERN(__imp__sub_8213C9F0);
REX_EXTERN(__imp__edf_native_worker_signal_cpu_tail);
REX_EXTERN(__imp__edf_native_ring_submit_cpu_tail);
REX_EXTERN(__imp__sub_8214EBA0);
REX_EXTERN(__imp__sub_82151248);
REX_EXTERN(__imp__sub_8214ECD8);
REX_EXTERN(__imp__sub_82134408);
REX_EXTERN(__imp__edf_native_buffer_lock_cpu_tail);
REX_EXTERN(__imp__edf_native_lock_82134958);
REX_EXTERN(__imp__edf_native_lock_82134A78);
REX_EXTERN(__imp__sub_82134958);
REX_EXTERN(__imp__sub_82134A78);
REX_EXTERN(__imp__sub_8213AD70);
REX_EXTERN(__imp__edf_test_native_resource_lock);
REX_EXTERN(__imp__sub_821E8740);
REX_EXTERN(__imp__sub_821EA320);
REX_EXTERN(__imp__edf_test_native_move);
REX_EXTERN(__imp__edf_test_native_fill);
REX_EXTERN(__imp__edf_test_native_file_read);
REX_EXTERN(__imp__edf_test_sub_821E8320);
REX_EXTERN(__imp__edf_test_sub_821E8740);
REX_EXTERN(__imp__edf_test_native_word_fill);
REX_EXTERN(__imp__sub_8214EE50);
REX_EXTERN(__imp__edf_native_worker_init);
REX_EXTERN(__imp__sub_8214EFF8);
REX_EXTERN(__imp__edf_native_device_defaults);
REX_EXTERN(__imp__sub_82149A90);
REX_EXTERN(__imp__sub_8214B0A0);
REX_EXTERN(__imp__edf_test_sub_82149A90);
REX_EXTERN(__imp__edf_test_sub_8214B0A0);
REX_EXTERN(__imp__sub_821498C8);
REX_EXTERN(__imp__sub_82149608);
REX_EXTERN(__imp__edf_test_sub_821498C8);
REX_EXTERN(__imp__edf_test_sub_82149608);
REX_EXTERN(__imp__sub_82147BA0);
REX_EXTERN(__imp__sub_82149AB0);
REX_EXTERN(__imp__edf_test_sub_82147BA0);
REX_EXTERN(__imp__edf_test_sub_82149AB0);
REX_EXTERN(__imp__sub_821BC588);
REX_EXTERN(__imp__sub_821BC430);

namespace {
uint32_t converted_declaration=0;
uint8_t* declaration_fixture_base=nullptr;
std::map<uint32_t,uint32_t> captured_declarations;
edf::native::NativeShaderState captured_shaders;
bool declaration_allocation=false,declaration_allocation_fail=false;
bool material_copy_fail=false;
uint32_t material_copy_count=0;
uint32_t declaration_content_handle=0;
edf::native::NativeDeclarations captured_contents;
uint32_t file_read_status=0,file_read_length=0;
uint32_t file_read_status_address=0;
bool file_read_finished=false;
bool file_read_throw=false;
bool file_scope_probe=false;
bool file_scope_exact=false;
bool word_fill_finished=false;
uint32_t word_fill_bytes=0,word_fill_notifications=0;
bool worker_init_fixture=false;
unsigned worker_init_failure=0;
std::vector<std::array<uint64_t,7>> worker_init_calls;
std::vector<std::array<uint64_t,2>> worker_init_priorities;
const uint8_t* move_expected=nullptr;
uint32_t move_destination=0,move_bytes=0;
unsigned move_notifications=0;
bool scalar_observer_test=false;
uint32_t scalar_address=0,scalar_bytes=0;
uint64_t scalar_value=0;
unsigned scalar_notifications=0;
std::span<const uint8_t> raw_store_expected;
edf::native::NativeBufferWrites file_writes;
}
REX_EXTERN(__imp__edf_test_inline_unlock);
REX_EXTERN(__imp__edf_test_chunk_free);
REX_EXTERN(__imp__edf_test_generic_free_original);
REX_EXTERN(__imp__edf_test_generic_free);
static unsigned generic_free_route=0;
static bool generic_retirement_expected=false;
static std::vector<unsigned> chunk_free_events;
static std::array<uint64_t,4> generic_free_arguments{};
static edf::native::NativeBufferWrites backing_release_writes;
static void CheckBackingReleaseExclusion(bool expected) {
  // Synthetic physical extent; mapping itself is not part of this fixture.
  backing_release_writes.Subscribe(1,0x1000,16);
  const std::array<uint8_t,16> bytes{};
  const auto snapshot=backing_release_writes.CopyObserved(1,0x1000,bytes);
  backing_release_writes.Unsubscribe(1);
  if(snapshot.has_value()==expected)
    throw std::runtime_error("backing free scope lifetime does not match provider execution");
}
REX_EXTERN(edf_test_generic_physical_free) {
  if(chunk_free_events!=(generic_retirement_expected?std::vector<unsigned>{1}:std::vector<unsigned>{}))
    throw std::runtime_error("generic physical free preceded expected retirement");
  CheckBackingReleaseExclusion(generic_retirement_expected);
  generic_free_route=1;
  generic_free_arguments={ctx.r3.u64,ctx.r4.u64,ctx.r5.u64,ctx.lr};
  ctx.r3.u64=0x123456789abcdef0ull;
}
REX_EXTERN(edf_test_generic_cpu_free) {
  CheckBackingReleaseExclusion(false);
  if(!chunk_free_events.empty()) throw std::runtime_error("CPU free unexpectedly retired physical geometry");
  generic_free_route=2;
  generic_free_arguments={ctx.r3.u64,ctx.r4.u64,ctx.r5.u64,ctx.lr};
  ctx.r3.u64=0x123456789abcdef0ull;
}
static uint32_t chunk_free_address=0;
static bool chunk_retirement_throws=false;
edf::native::NativeBufferWrites::ReleaseScopes edf_test_retire_pool_backings(uint8_t*,std::span<const uint32_t> backings) {
  if(backings.size()!=1 || backings[0]!=chunk_free_address || !chunk_free_events.empty())
    throw std::runtime_error("chunk retirement lost actual free argument/order");
  chunk_free_events.push_back(1);
  auto scopes=backing_release_writes.BeginReleaseSet(
    std::array<edf::native::NativeBufferWrites::Range,1>{{{0x1000,16}}});
  if(chunk_retirement_throws) throw std::runtime_error("injected chunk retirement failure");
  return scopes;
}
REX_EXTERN(__imp__MmFreePhysicalMemory) {
  if(ctx.r3.u32!=0 || ctx.r4.u32!=chunk_free_address)
    throw std::runtime_error("chunk free changed original physical free ABI");
  CheckBackingReleaseExclusion(!chunk_free_events.empty());
  chunk_free_events.push_back(2);
  ctx.r3.u64=0xdeadbeef; ctx.r4.u64=0xfeed1234;
}
REX_EXTERN(__imp__edf_test_sub_821349B8);
REX_EXTERN(__imp__sub_821349B8);
REX_EXTERN(__imp__sub_82134AD8);
REX_EXTERN(__imp__edf_native_unlock_82134AD8);
static bool index_unlock_retail=false;
static bool index_unlock_native=false;
static uint32_t vertex_unlock_owner=0;
static unsigned vertex_unlock_updates=0;
void edf_test_vertex_unlock_update(uint8_t*,uint32_t owner,bool index) {
  if(owner!=vertex_unlock_owner || index || !file_writes.Version(1))
    throw std::runtime_error("vertex unlock changed owner or retained guard through renderer update");
  ++vertex_unlock_updates;
}
REX_EXTERN(__imp__edf_test_sub_8213BDF8);
static bool inline_expected=false;
static bool inline_header_expected=false;
static bool inline_payload_active=true;
static unsigned inline_order=0;
REX_EXTERN(edf_test_inline_unlock_original) {
  if(index_unlock_retail) {
    if(file_writes.Version(1).has_value()==index_unlock_native)
      throw std::runtime_error("index unlock lost header guard before retail provider");
    if(index_unlock_native) __imp__edf_native_unlock_82134AD8(ctx,base);
    else __imp__sub_82134AD8(ctx,base);
    return;
  }
  if(inline_order++!=0 || file_writes.Version(1).has_value()==inline_header_expected)
    throw std::runtime_error("inline unlock provider ordering changed");
  std::array<uint8_t,24> header{};
  const std::array<edf::native::NativeBufferWrites::SnapshotSource,1> source{{{2,1,header}}};
  edf::native::NativeBufferWrites::SnapshotFailure failure;
  if(file_writes.CopyObservedSet(source,&failure).has_value()==inline_header_expected ||
    (inline_header_expected && (failure.overlapping_writers!=1 || failure.unknown_writers!=0)))
    throw std::runtime_error("inline unlock lost exact independent header exclusion");
  ctx.r3.u64=0xdeadbeef;
}
void edf_test_inline_completed(uint8_t*,uint32_t destination,uint32_t bytes) {
  if(index_unlock_retail) {
    if(!index_unlock_native || file_writes.Version(1) || destination!=vertex_unlock_owner || bytes!=24)
      throw std::runtime_error("index unlock lost exact header completion inside guard");
    file_writes.Record(destination,bytes);
    return;
  }
  const bool header=bytes==24;
  if(!(header?inline_header_expected:inline_expected) || inline_order++!=(header?1u:2u) ||
    file_writes.Version(1).has_value()!=(!header && !inline_payload_active) ||
    (header?destination!=1:bytes!=12))
    throw std::runtime_error("inline completed notification escaped scope");
  file_writes.Record(destination,bytes);
}
void edf_test_inline_renderer_update(uint8_t*,uint32_t owner,bool index) {
  if(index_unlock_retail) {
    if(owner!=vertex_unlock_owner || !index || !file_writes.Version(1))
      throw std::runtime_error("index unlock retained guard through renderer update or lost owner");
    ++vertex_unlock_updates;
    return;
  }
  if(inline_order++!=(inline_expected?3u:inline_header_expected?2u:1u) || !file_writes.Version(1) || owner!=1 || !index)
    throw std::runtime_error("inline scope retained through renderer update or owner clobbered");
}
namespace {
using Ownership=edf::native::NativeConstantOwnership;
constexpr uint32_t device=0x1000,vertices=0x20000,copy=0x30000,cursor=0x40000;
struct alignas(32) Arena { std::array<uint8_t,1024*1024> bytes{}; };
// E070 reads two fixed guest lookup tables above 2GB. Reserve guest address
// space sparsely; commit only the fixture arena, lookup-table page and a shader
// source page in the GPU-address alias range used by E950.
struct ShaderPatchArena {
  uint8_t* bytes=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));
  ShaderPatchArena() {
    if(!bytes) throw std::runtime_error("shader fixture reservation failed");
    if(!VirtualAlloc(bytes,1024*1024,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+0x8200b000,8192,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+0x82009000,4096,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+0x82000000,4096,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+0x82578000,4096,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+((uint64_t(REX_IMAGE_BASE)+REX_IMAGE_SIZE)&~uint64_t(4095)),8192,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+0xc0070000,4096,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(bytes+0xe0070000,8192,MEM_COMMIT,PAGE_READWRITE)) {
      VirtualFree(bytes,0,MEM_RELEASE); bytes=nullptr;
      throw std::runtime_error("shader fixture commit failed");
    }
  }
  ~ShaderPatchArena() { if(bytes) VirtualFree(bytes,0,MEM_RELEASE); }
  ShaderPatchArena(const ShaderPatchArena&)=delete;
  ShaderPatchArena& operator=(const ShaderPatchArena&)=delete;
};
unsigned allocations,copies,constants,render,derived,rollovers,output_skips,output_patches;
uint32_t expected_stride;
bool real_state_packets=false;
bool real_vector_packets=false;
bool real_special_packets=false;
bool main_state_fixture=false,shader_cache_hit=false;
bool real_shader_cache=false;
bool indexed_bank_fixture=false;
bool material_reference=false;
unsigned shader_cache_hits=0,shader_cache_refreshes=0,shader_cache_busy=0;
uint64_t helper_trace=0;
uint64_t cache_trace=0;
bool reset_fixture=false,reset_native=false,reset_drained=false,reset_boundary=false,reset_needs_drain=false;
unsigned reset_tracking=0,reset_allocations=0,reset_failure=0,reset_frees=0,reset_exports=0;
unsigned reset_packets=0;
bool real_drain_wait=false;
unsigned drain_polls=0,drain_wait_objects=0,drain_wait_frees=0;
uint32_t drain_target=0;
bool cache_range_fixture=false,cache_range_failure=false;
unsigned cache_range_exchanges=0;
bool enclosing_fixture=false,enclosing_native=false;
unsigned enclosing_failures=0,enclosing_restores=0,enclosing_clears=0;
std::vector<std::array<uint32_t,4>> enclosing_lists,enclosing_signals,enclosing_allocations;
bool flush_fixture=false;
unsigned flush_submits=0,flush_waits=0,flush_cache_calls=0;
bool ring_fixture=false;
unsigned ring_reservations=0,ring_doorbells=0;
std::vector<std::array<uint64_t,6>> ring_callbacks;
bool signal_fixture=false;
uint32_t signal_cpu=0,signal_argument=0,signal_line=0;
unsigned signal_events=0,signal_reads=0;
bool buffer_lock_fixture=false;
bool native_lock_tracking=false;
unsigned native_lock_completions=0;
bool texture_lock_fixture=false,texture_native=false;
uint32_t texture_block_bytes=0,texture_rows=0,texture_offset=0;
unsigned texture_layout_calls=0,texture_format_calls=0,texture_lock_calls=0;
std::vector<std::array<uint32_t,4>> buffer_lock_waits;
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void PrepareDrawCpuState(uint8_t* base) {
  constexpr uint32_t program=0x60000,decl=0x65000;
  REX_STORE_U32(device+12420,program); REX_STORE_U32(device+11536,decl);
  REX_STORE_U32(device+10452,0x12345005);
  REX_STORE_U32(program+28,0x70000); REX_STORE_U32(program+872,0x20);
  REX_STORE_U32(decl+48,1);
  REX_STORE_U64(decl+32,UINT64_MAX); REX_STORE_U64(decl+40,UINT64_MAX);
  REX_STORE_U32(device+10768,0x67000); REX_STORE_U32(0x67000,100);
  REX_STORE_U32(device+10780,200);
  for(uint32_t variant:{0u,1u}) {
    const auto offset=0x1000+variant*0x200,record=program+variant*416;
    REX_STORE_U32(program+896+variant*8,offset);
    REX_STORE_U32(program+offset+876,12);
    REX_STORE_U32(record+40,1);
    REX_STORE_U64(record+48,REX_LOAD_U64(device+12256));
    REX_STORE_U64(record+56,REX_LOAD_U64(device+12264));
  }
}
}
// Dependencies are controlled; the wrapper/state-flush/control flow and all
// register save/restore routines are extracted unchanged from generated code.
bool edf_test_texture_native() { return texture_native; }
extern "C" void edf_native_observe_guest_scalar_store(uint8_t* base,uint32_t address,uint32_t bytes,const char* file,uint32_t line) {
  if(!scalar_observer_test) return;
  Require(file && *file && line,"generated writer source attribution missing");
  Require(address==scalar_address && bytes==scalar_bytes,"scalar observer extent changed");
  const auto* data=base+address+REX_PHYS_HOST_OFFSET(address);
  if(!raw_store_expected.empty()) {
    Require(raw_store_expected.size()==bytes && std::equal(raw_store_expected.begin(),raw_store_expected.end(),data),
      "raw store observer preceded write or changed contents");
    ++scalar_notifications;
    return;
  }
  for(uint32_t i=0;i<bytes;++i)
    Require(data[i]==uint8_t(scalar_value>>((bytes-1-i)*8)),"scalar observer preceded store or changed byte order");
  ++scalar_notifications;
}
edf::native::NativeBufferWrites::WriterScope BeginNativeBufferWrite(uint8_t*,uint32_t destination,uint32_t bytes,bool exact,
    edf::native::NativeBufferWrites::WriterKind kind) {
  // Controlled physical mapping; use the real queue and RAII protocol.
  const auto range=exact?std::optional<edf::native::NativeBufferWrites::Range>{{destination,bytes}}:std::nullopt;
  return edf::native::NativeBufferWrites::WriterScope(bytes?&file_writes:nullptr,range,kind);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_lock_header_write(uint8_t*,uint32_t owner) {
  if(native_lock_tracking) Require(owner==0x60000 && file_writes.Version(4).has_value(),
    "lock header guard began at wrong owner or inside another guard");
  return edf::native::NativeBufferWrites::WriterScope(native_lock_tracking?&file_writes:nullptr,
    edf::native::NativeBufferWrites::Range{owner,28});
}
void edf_native_complete_lock_header_write(uint8_t* base,uint32_t owner) {
  if(!native_lock_tracking) return;
  Require(owner==0x60000 && !file_writes.Version(4) && (REX_LOAD_U32(owner)&0x100),
    "lock header completion missed atomic update or escaped guard");
  file_writes.Record(owner,28); ++native_lock_completions;
}
void edf_test_completed_copy(uint8_t*,uint32_t destination,uint32_t bytes,bool notify_versions,bool generated,
    edf::native::NativeBufferWrites::WriterSite site) {
  Require(!notify_versions && !generated,"copy provider changed notification classification");
  Require(file_writes.Version(1).has_value()==(bytes==0),"copy scope ended before notification");
  file_writes.Record(destination,bytes,false,site);
}
void edf_test_completed_move(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions,bool generated,
    edf::native::NativeBufferWrites::WriterSite site) {
  Require(!notify_versions && !generated,"move/fill changed notification classification");
  Require(!site.file && !site.line &&
    ((site.provider==0x821ea320 && site.caller==0x82123464) ||
     (site.provider==0x821e9ba0 && site.caller==0x12345678)),"move/fill lost provider or entry caller");
  Require(file_writes.Version(1).has_value()==(bytes==0),"move/fill scope ended before notification");
  Require(move_expected && destination==move_destination && bytes==move_bytes,
    "backward-copy notification lost entry extent");
  Require(!std::memcmp(base+copy,move_expected,512),"backward-copy notification preceded completed writes");
  file_writes.Record(destination,bytes,false,site);
  ++move_notifications;
}
REX_EXTERN(sub_8213A8B0) {
  Require(texture_lock_fixture && ctx.lr==0x8213adc4 && ctx.r3.u32==0x60000,
    "texture lock layout caller changed");
  ++texture_layout_calls;
  // Controlled layout outputs; AD70's argument routing/arithmetic stays retail.
  REX_STORE_U32(ctx.r8.u32,texture_rows);
  REX_STORE_U32(ctx.r10.u32,0x1234);
  REX_STORE_U32(ctx.r9.u32,texture_offset);
}
REX_EXTERN(sub_82139C30) {
  Require(texture_lock_fixture && ctx.lr==0x8213add8 && ctx.r3.u32==7,
    "texture lock format caller changed");
  ++texture_format_calls;
  REX_STORE_U32(ctx.r5.u32,texture_block_bytes);
}
REX_EXTERN(sub_82134408) {
  if(buffer_lock_fixture && !texture_lock_fixture) {
    Require((ctx.lr==0x821349a8 && ctx.r4.u32==10) || (ctx.lr==0x82134ac4 && ctx.r4.u32==12),
      "buffer wrapper changed shared-helper contract");
    __imp__sub_82134408(ctx,base); return;
  }
  Require(texture_lock_fixture && ctx.lr==0x8213ae24 && ctx.r4.u32==14,
    "texture lock shared-helper contract changed");
  ++texture_lock_calls;
  __imp__edf_test_native_resource_lock(ctx,base);
}
REX_EXTERN(sub_8213DF00) { ++constants; }
REX_EXTERN(sub_8213DB60) {
  ++render;
  if(real_state_packets && !Ownership::OwnsRenderWords(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r4.u64))
    __imp__sub_8213DB60(ctx,base);
}
REX_EXTERN(sub_8213D698) { throw std::runtime_error("unexpected state packet overflow"); }
REX_EXTERN(sub_8213ECB0) {
  ++derived;
  Require(REX_LOAD_U8(ctx.r3.u32+12256)==expected_stride/4,"derived helper did not see temporary stride");
  __imp__edf_native_main_state_cpu_tail(ctx,base);
}
REX_EXTERN(sub_8213D938) {
  if(indexed_bank_fixture) {
    Require(ctx.r6.u32==0,"indexed special encoder mode changed");
    ++render; ctx.r3.u64=ctx.r4.u64 & ~uint64_t(0x100); return;
  }
  Require(real_special_packets,"unexpected special render helper");
  if(Ownership::OwnsSpecialRenderPacket(ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u64,ctx.r6.u32))
    ctx.r3.u64=ctx.r4.u64 & ~uint64_t(0x100);
  else __imp__sub_8213D938(ctx,base);
}
REX_EXTERN(sub_8213DDA0) {
  if(indexed_bank_fixture) { ++render; return; } // Audited packet-only encoder.
  throw std::runtime_error("unexpected fetch helper");
}
REX_EXTERN(sub_8213DC20) {
  if(indexed_bank_fixture) { ++render; return; } // Audited packet-only encoder.
  Require(real_vector_packets,"unexpected vector-state helper");
  if(!Ownership::OwnsVectorStatePackets(ctx.r3.u32,ctx.r4.u64)) __imp__sub_8213DC20(ctx,base);
}
REX_EXTERN(sub_8213CB30) { throw std::runtime_error("unexpected query allocation"); }
REX_EXTERN(sub_8213CF60) {
  ++rollovers;
  if(real_drain_wait) {
    Require(ctx.r3.u32==device,"drain flush device changed");
    REX_STORE_U32(device+10780,REX_LOAD_U32(device+10780)+2);
  }
  ctx.r3.u64=reset_fixture && !real_drain_wait?0x90400:cursor+1024;
}
REX_EXTERN(sub_8213C328) {
  if(enclosing_fixture) {
    const auto caller=uint32_t(ctx.lr);
    unsigned bit=0; uint32_t result=0;
    if(caller==0x8213C65C) { bit=1; result=cursor; }
    else if(caller==0x8214ED14) { bit=2; result=cursor+256; }
    else if(caller==0x8214ED80) { bit=4; result=cursor+512; }
    else throw std::runtime_error("unexpected enclosing allocation caller");
    enclosing_allocations.push_back({caller,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32});
    ctx.r3.u64=(enclosing_failures&bit)?0:result; return;
  }
  if(cache_range_fixture) {
    Require(ctx.r3.u32==device && ctx.r5.u32==32 && (ctx.r4.u32==11 || ctx.r4.u32==22),
      "unexpected cache-range allocation");
    ++allocations; ctx.r3.u64=cache_range_failure?0:cursor; return;
  }
  Require(REX_LOAD_U8(device+12256)==7,"temporary stride not restored before allocation");
  for(uint32_t at:{0,8,16,24,32}) Require(REX_LOAD_U64(device+at)==0,"dirty bank not cleared before allocation");
  if(Ownership::OwnsImmediateAllocation(ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u32,ctx.r5.u32)) {
    ctx.r3.u64=0; return;
  }
  ++allocations; ctx.r3.u64=copy;
}
REX_EXTERN(sub_821E8320) {
  ++copies;
  std::memcpy(base+ctx.r3.u32,base+ctx.r4.u32,ctx.r5.u32);
}
REX_EXTERN(edf_test_copy_provider) {
  Require(file_writes.Version(1).has_value()==(ctx.r5.u32==0),"main copy scope did not precede provider");
  sub_821E8320(ctx,base);
  ctx.lr=0x87654321; // Expose accidental post-provider caller attribution.
}
REX_EXTERN(sub_8252B718) {} // Retail routine is itself a bare return.
REX_EXTERN(sub_8213EAB0) {
  if(!Ownership::OwnsShaderLoadPackets(ctx.r3.u32,uint32_t(ctx.lr))) __imp__sub_8213EAB0(ctx,base);
}
REX_EXTERN(sub_8213E070) {
  if(!Ownership::OwnsShaderMicrocodePatch(ctx.r6.u32,uint32_t(ctx.lr))) __imp__sub_8213E070(ctx,base);
}
REX_EXTERN(sub_8213E678) { ++output_skips; __imp__sub_8213E678(ctx,base); }
REX_EXTERN(sub_8213E748) { ++output_patches; __imp__sub_8213E748(ctx,base); }
REX_EXTERN(sub_8213E800) { __imp__sub_8213E800(ctx,base); }
static uint64_t retail_cache_flush_calls=0;
REX_EXTERN(sub_82141AB8) { ++retail_cache_flush_calls; __imp__sub_82141AB8(ctx,base); }
REX_EXTERN(sub_8213D160) { ++allocations; ctx.r3.u64=cursor; }
REX_EXTERN(sub_8213EB68) {
  Require(main_state_fixture,"unexpected shader cache helper");
  helper_trace=helper_trace*17+1+ctx.r6.u32;
  cache_trace=cache_trace*17+1+ctx.r6.u32;
  if(real_shader_cache) {
    const uint32_t record=ctx.r4.u32+ctx.r6.u32*416;
    const bool matched=REX_LOAD_U32(record+40)==REX_LOAD_U32(ctx.r5.u32+48);
    __imp__sub_8213EB68(ctx,base);
    if(!ctx.r3.u32) ++shader_cache_busy;
    else if(matched) ++shader_cache_hits;
    else ++shader_cache_refreshes;
  } else ctx.r3.u64=shader_cache_hit?1:0;
}
REX_EXTERN(sub_8213E950) {
  Require(main_state_fixture,"unexpected shader upload helper");
  helper_trace=helper_trace*17+4+ctx.r4.u32;
  __imp__edf_native_shader_upload_cpu_tail(ctx,base);
}
REX_EXTERN(sub_8213D750) {
  Require(main_state_fixture,"unexpected derived-state helper");
  helper_trace=helper_trace*17+9;
  __imp__edf_native_derived_cpu_tail(ctx,base);
}
REX_EXTERN(edf_native_reset_completion_tracking) {
  Require(reset_fixture && reset_native && ctx.r31.u32==device,"unexpected completion reset boundary");
  Require(!reset_needs_drain || reset_drained,"completion tracking retired before old-work drain");
  Require(!reset_frees,"completion tracking retired after storage was freed");
  reset_tracking=0; reset_boundary=true;
}
REX_EXTERN(edf_native_drain_worker_signals) {
  Require(reset_fixture && reset_native && reset_drained && ctx.r31.u32==device,
    "native worker drain called before fence wait or with wrong device");
}
REX_EXTERN(sub_8213D1C8) {
  Require(reset_fixture && reset_tracking==1,"old completion tracking unavailable at drain entry");
  if(reset_native) __imp__edf_native_device_drain(ctx,base);
  else __imp__sub_8213D1C8(ctx,base);
}
REX_EXTERN(sub_8213C928) {
  if(buffer_lock_fixture) {
    if(native_lock_tracking) Require(file_writes.Version(4).has_value(),
      "lock header guard spans retained fence service");
    Require(ctx.lr==0x82134498 && ctx.r3.u32==device && ctx.r6.u32==0x60000,
      "buffer lock changed wait caller/resource");
    Require((REX_LOAD_U32(ctx.r6.u32)&0x100)==0 && REX_LOAD_U32(device+40)==cursor,
      "buffer lock modified resource or submitted packets before wait");
    buffer_lock_waits.push_back({ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32});
    return;
  }
  if(flush_fixture) {
    Require(flush_submits==1 && ctx.r3.u32==device && ctx.r4.u32==7 && ctx.r5.u32==0 &&
      ctx.r6.u32==0 && ctx.lr==0x8213D020,"flush wait arguments/order changed");
    ++flush_waits; return;
  }
  Require(reset_fixture && reset_tracking==1 && !reset_frees,"old completion tracking unavailable during wait");
  Require(ctx.r3.u32==device && ctx.r4.u32==REX_LOAD_U32(device+10780) &&
    ctx.r5.u64==4 && ctx.r6.u64==0 && ctx.lr==0x8213D21C,"drain changed wait arguments");
  if(real_drain_wait) __imp__sub_8213C928(ctx,base);
  reset_drained=true;
}
REX_EXTERN(sub_821394D8) {
  Require(real_drain_wait && ctx.r4.u32==device && ctx.r5.u32==4,"unexpected drain wait creation");
  ++drain_wait_objects;
  REX_STORE_U32(ctx.r3.u32,device);
}
REX_EXTERN(sub_82139688) {
  Require(real_drain_wait && REX_LOAD_U32(ctx.r3.u32)==device,"unexpected drain poll");
  // Controlled asynchronous completion, not a D3D query or fabricated runtime fence.
  if(++drain_polls==3) REX_STORE_U32(REX_LOAD_U32(device+10768),drain_target);
  Require(drain_polls<=3,"drain failed to observe completion");
  ctx.r3.u64=1;
}
REX_EXTERN(sub_82139508) {
  Require(real_drain_wait,"unexpected drain wait cleanup");
  ++drain_wait_frees;
}
REX_EXTERN(sub_8213BE68) {
  Require(cache_range_fixture && ctx.r3.u32==device+11544,"unexpected dirty-range exchange");
  ++cache_range_exchanges;
  __imp__sub_8213BE68(ctx,base);
}
REX_EXTERN(sub_8213C5F0) {
  if(enclosing_fixture) {
    Require(ctx.lr==0x8214ED04,"unexpected enclosing cache caller");
    if(enclosing_native) __imp__edf_native_cache_reservation_cpu_tail(ctx,base);
    else __imp__sub_8213C5F0(ctx,base);
    return;
  }
  Require(flush_fixture && ctx.lr==0x8213CF9C,"unexpected flush cache-range call");
  ++flush_cache_calls;
  __imp__edf_native_cache_range_cpu_tail(ctx,base);
}
REX_EXTERN(sub_8213CDC0) {
  Require(flush_fixture && ctx.r3.u32==device && ctx.lr==0x8213CFE0,"flush submission changed");
  ++flush_submits;
  // Controlled submission boundary: actual C788/D3D query is tested elsewhere.
  REX_STORE_U32(device+10780,9);
}
REX_EXTERN(sub_8213C868) {
  Require(enclosing_fixture,"native flush enqueued cache packet");
  enclosing_lists.push_back({ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32});
  if(ctx.r7.u32) REX_STORE_U32(device+10868,REX_LOAD_U32(device+10868)+1);
  ctx.r3.u64=ctx.r4.u32+24;
}
REX_EXTERN(sub_8213C9F0) {
  Require(enclosing_fixture,"unexpected enclosing worker signal");
  enclosing_signals.push_back({ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32});
  // Reservation endpoint only. Actual native signal encoding/submission is
  // tested separately; no callback is dispatched by this controlled dependency.
  ctx.r3.u64=ctx.r4.u32+96;
}
REX_EXTERN(sub_8213C0E0) { Require(enclosing_fixture,"unexpected CPU restore"); ++enclosing_restores; }
REX_EXTERN(sub_8213BFD8) { Require(enclosing_fixture,"unexpected CPU list clear"); ++enclosing_clears; }
REX_EXTERN(sub_8213BCE0) {
  Require(ring_fixture && ctx.r3.u32==device,"unexpected ring reservation");
  ++ring_reservations;
  ctx.r3.u64=(ctx.r4.u32+ctx.r5.u32)&REX_LOAD_U32(device+13480);
}
REX_EXTERN(edf_test_ring_doorbell) {
  Require(ring_fixture && ctx.r11.u32+1812==0x7fc80714 &&
    ctx.r29.u32==REX_LOAD_U32(device+10820),"original doorbell address/cursor differs");
  ++ring_doorbells;
}
REX_EXTERN(edf_test_ring_callback) {
  Require(ring_fixture,"unexpected ring callback");
  ring_callbacks.push_back({ctx.ctr.u64,ctx.lr,ctx.r3.u64,ctx.r4.u64,ctx.r5.u64,ctx.r6.u64});
}
REX_EXTERN(__imp__KeSetEvent) {
  Require(signal_fixture && ctx.r3.u32==device+11228+signal_cpu*56 && ctx.r4.u32==1 && ctx.r5.u32==0,
    "signal callback selected wrong CPU event/arguments");
  Require(REX_LOAD_U32(device+10900)==signal_argument,"signal event preceded CPU result publication");
  ++signal_events; ctx.r3.u64=0;
}
uint32_t edf_test_signal_mmio(uint32_t address) {
  Require(signal_fixture && (address==0x7fc86530 || address==0x7fc86584),"unexpected signal timing register");
  ++signal_reads;
  return address==0x7fc86530?signal_line:100;
}
REX_EXTERN(sub_8212F4B8) {
  Require(reset_fixture,"unexpected fixture free");
  if(reset_native) Require(reset_boundary && reset_tracking==0,"writeback freed before completion retirement");
  ++reset_frees;
}
REX_EXTERN(sub_8212F420) {
  if(declaration_allocation) {
    Require(ctx.r3.u32<=56+64*12,"invalid declaration allocation extent");
    ctx.r3.u64=declaration_allocation_fail?0:copy;
    return;
  }
  Require(reset_fixture,"unexpected fixture allocation");
  const uint32_t bytes=ctx.r3.u32;
  ++reset_allocations;
  if(reset_allocations==reset_failure) { ctx.r3.u64=0; return; }
  const uint32_t address=bytes==1024?0x80000:bytes==8192?0x90000:bytes==96?0x70000:bytes==32?0x70100:0;
  Require(address!=0,"unexpected reset allocation size");
  std::memset(base+address,0xa5,bytes); ctx.r3.u64=address;
}
REX_EXTERN(sub_821E9BA0) { std::memset(base+ctx.r3.u32,ctx.r4.u8,ctx.r5.u32); }
REX_EXTERN(sub_8213BD90) {
  Require(reset_fixture,"unexpected reset packet append");
  ++reset_packets;
  const auto begin=REX_LOAD_U32(ctx.r3.u32+40);
  std::memcpy(base+begin+4,base+ctx.r4.u32,ctx.r5.u32*4);
  REX_STORE_U32(ctx.r3.u32+40,begin+ctx.r5.u32*4);
}
REX_EXTERN(sub_8213D0F8) {
  Require(reset_fixture && !reset_native,"native initializer called packet append");
  ++reset_packets;
  __imp__sub_8213D0F8(ctx,base);
}
REX_EXTERN(__imp__MmGetPhysicalAddress) {} // Identity mapping; only consumed by the ring-export fixture.
REX_EXTERN(__imp__KiApcNormalRoutineNop) {}
REX_EXTERN(__imp__VdInitializeRingBuffer) { ++reset_exports; }
REX_EXTERN(__imp__VdEnableRingBufferRPtrWriteBack) { ++reset_exports; }
REX_EXTERN(__imp__VdSetSystemCommandBufferGpuIdentifierAddress) { ++reset_exports; }

REX_EXTERN(sub_821F9FC8) { ctx.r3.u64=7; }
REX_EXTERN(sub_8213BDF8) {
  // Controlled dirty-range provider; both original/native must call it with
  // the same resource range. This fixture does not test its atomic implementation.
  REX_STORE_U32(ctx.r3.u32,ctx.r4.u32);
  REX_STORE_U32(ctx.r3.u32+4,ctx.r5.u32);
}
REX_EXTERN(__imp__NtReadFile) {
  file_read_status_address=ctx.r7.u32;
  Require(file_writes.Version(1).has_value()==(ctx.r9.u32==0 && !file_read_status_address),"file scope did not precede provider");
  if(file_scope_probe) {
    std::array<uint8_t,8> unrelated{};
    const std::array<edf::native::NativeBufferWrites::SnapshotSource,1> requested{{{3,copy+16384,unrelated}}};
    edf::native::NativeBufferWrites::SnapshotFailure failure;
    const auto snapshot=file_writes.CopyObservedSet(requested,&failure);
    Require(bool(snapshot)==file_scope_exact,"file APC policy narrowed or blocked the wrong provider");
    if(!file_scope_exact) Require(failure.unknown_by_kind[size_t(edf::native::NativeBufferWrites::WriterKind::FileRead)]==2,
      "APC-capable read lost conservative data/status scopes");
  }
  // A deliberately adversarial synchronous provider: even failure may mutate
  // part of the destination. It clobbers volatile argument registers on return.
  if(ctx.r9.u32) REX_STORE_U8(ctx.r8.u32,0x4f);
  if(file_read_status_address) {
    REX_STORE_U32(file_read_status_address,file_read_status);
    REX_STORE_U32(file_read_status_address+4,ctx.r9.u32?1u:0u);
  }
  file_read_length=ctx.r9.u32;
  if(file_read_throw) throw std::runtime_error("injected file provider failure");
  ctx.r8.u64=0xdeadbeef; ctx.r9.u64=0xbadf00d;
  ctx.r7.u64=0xfeedface;
  ctx.r3.u64=file_read_status;
  file_read_finished=true;
}
REX_EXTERN(__imp__RtlFillMemoryUlong) {
  word_fill_bytes=ctx.r4.u32&~3u;
  Require(file_writes.Version(1).has_value()==(word_fill_bytes==0),"word fill scope did not precede provider");
  using Writes=edf::native::NativeBufferWrites;
  const std::array<Writes::SnapshotSource,2> unrelated{{
    {3,copy+16384,std::span<const uint8_t>(base+copy+16384,8)},
    {4,copy+word_fill_bytes,std::span<const uint8_t>(base+copy+word_fill_bytes,1)}}};
  Require(file_writes.CopyObservedSet(unrelated).has_value(),
    "word fill blocks unrelated geometry or its unwritten trailing byte");
  const std::array<Writes::SnapshotSource,1> overlapping{{
    {1,copy,std::span<const uint8_t>(base+copy,4096)}}};
  Writes::SnapshotFailure failure;
  Require(file_writes.CopyObservedSet(overlapping,&failure).has_value()==(word_fill_bytes==0),
    "word fill admitted overlapping geometry or blocked an empty fill");
  if(word_fill_bytes) Require(failure.reason==Writes::SnapshotRejection::ActiveWriter &&
    failure.overlapping_writers==1 && failure.unknown_writers==0,
    "word fill did not retain exact overlapping exclusion");
  for(uint32_t offset=0;offset<word_fill_bytes;offset+=4)
    REX_STORE_U32(ctx.r3.u32+offset,ctx.r5.u32);
  ctx.r3.u64=0x12345678; ctx.r4.u64=0xdeadbeef;
  word_fill_finished=true;
}
REX_EXTERN(__imp__ExCreateThread) {
  Require(worker_init_fixture,"unexpected worker thread creation");
  worker_init_calls.push_back({ctx.r3.u64,ctx.r4.u64,ctx.r5.u64,ctx.r6.u64,ctx.r7.u64,ctx.r8.u64,ctx.r9.u64});
  Require(ctx.r4.u32==32768 && ctx.r6.u32==0,"worker stack/flags changed");
  REX_STORE_U32(ctx.r3.u32,0x100+uint32_t(worker_init_calls.size()));
  ctx.r3.u64=worker_init_failure==worker_init_calls.size()?0xc0000001u:0;
  ctx.r10.u64=0xabababab; ctx.r11.u64=0xcdcdcdcd;
}
REX_EXTERN(__imp__KeSetBasePriorityThread) {
  Require(worker_init_fixture && ctx.r4.u32==17,"worker priority changed");
  worker_init_priorities.push_back({ctx.r3.u64,ctx.r4.u64});
  ctx.r3.u64=8;
}
void edf_test_completed_word_fill(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions) {
  Require(file_writes.Version(1).has_value()==(bytes==0),"word fill scope ended before notification");
  Require(word_fill_finished && notify_versions,"word fill omitted completed provider notification");
  Require(destination==copy && bytes==word_fill_bytes,"word fill lost original extent or included partial word");
  for(uint32_t offset=0;offset<bytes;offset+=4)
    Require(REX_LOAD_U32(destination+offset)==0x12345678,"word fill notification preceded provider write");
  ++word_fill_notifications;
  file_writes.Record(destination,bytes);
}
void edf_test_completed_file_write(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions) {
  Require(file_writes.Version(1).has_value()==(bytes==0 && !file_read_status_address),"file scope ended before notification");
  Require(notify_versions,"file read omitted version-tracker notification request");
  Require(file_read_finished,"file notification ran before provider completion");
  if(file_read_status_address && destination==file_read_status_address) {
    Require(bytes==8 && REX_LOAD_U32(destination)==file_read_status &&
      REX_LOAD_U32(destination+4)==(file_read_length?1u:0u),"file status notification lost original pointer or preceded write");
  } else {
    Require(bytes==file_read_length && destination==copy,"file notification lost original arguments");
    if(bytes) Require(base[destination]==0x4f,"file notification preceded destination mutation");
  }
  // Physical-coordinate stand-in: extent translation is tested separately.
  file_writes.Record(destination,bytes);
}
REX_EXTERN(sub_82147BA0) {
  // Controlled FVF converter: deliberately do not return the input FVF.
  ctx.r3.u64=0x1234;
  ctx.r4.u64=converted_declaration;
}
REX_EXTERN(sub_82141440) {
  throw std::runtime_error("unexpected shader retirement allocation in binding fixture");
}
REX_EXTERN(sub_821498C8) {
  Require(material_reference,"native material called retail vertex shader binding");
  __imp__sub_821498C8(ctx,base);
}
REX_EXTERN(sub_82149608) {
  Require(material_reference,"native material called retail pixel shader binding");
  __imp__sub_82149608(ctx,base);
}
REX_EXTERN(sub_82149248) {
  Require(material_reference,"native material called retail vertex constant setter");
  __imp__sub_82149248(ctx,base);
}
REX_EXTERN(sub_82149358) {
  Require(material_reference,"native material called retail pixel constant setter");
  __imp__sub_82149358(ctx,base);
}
REX_EXTERN(sub_8213BA98) { throw std::runtime_error("unconfigured material texture reference"); }
REX_EXTERN(sub_82136888) { throw std::runtime_error("unconfigured material sampler reference"); }
REX_EXTERN(sub_82136700) { throw std::runtime_error("unconfigured material sampler reference"); }
REX_EXTERN(sub_82136C20) { throw std::runtime_error("unconfigured material sampler reference"); }
void edf_test_shader_binding(uint32_t owner,uint32_t shader,bool pixel) {
  auto* base=declaration_fixture_base;
  Require(base && REX_LOAD_U32(owner+(pixel?12416:12420))==shader,
    "shader binding published before original CPU update");
  captured_shaders.Set(owner,shader,pixel);
}
void edf_test_unexpected_native_shader_binding(PPCContext&,uint8_t*,bool) {
  throw std::runtime_error("publication fixture unexpectedly selected native shader activation");
}
void edf_test_declaration_contents(uint8_t* base,uint32_t handle) {
  declaration_content_handle=handle;
  if(!handle) return;
  const auto count=REX_LOAD_U32(handle+24);
  Require(count<=64,"constructor published invalid declaration count");
  captured_contents.Publish(handle,{base+handle+52,size_t(count)*12});
}
void edf_test_declaration_binding(uint32_t owner,uint32_t declaration) {
  auto* base=declaration_fixture_base;
  Require(base && REX_LOAD_U32(owner+11536)==declaration,
    "declaration published before original CPU state update");
  captured_declarations.insert_or_assign(owner,declaration);
}
REX_EXTERN(sub_821BC2D8) {
  material_copy_count=ctx.r4.u32;
  if(!material_copy_fail) {
    const auto destination=ctx.r3.u32;
    REX_STORE_U32(destination,copy);
    REX_STORE_U32(destination+4,material_copy_count);
    REX_STORE_U32(destination+8,material_copy_count);
  }
  ctx.r3.u64=material_copy_fail?0:1;
}
int main() {
  {
    constexpr uint32_t test_device=0x1000;
    Ownership legacy(test_device,64);
    {
      auto native=Ownership::ForCpuOnlyTail(test_device,true);
      Require(Ownership::Owns(test_device,16384,test_device+1792) &&
        !Ownership::OwnsImmediateAllocation(test_device,0x821fd6e8,16,16),
        "CPU-only packet routing inherited legacy immediate allocation extent");
      {
        auto nested=Ownership::ForCpuOnlyTail(test_device,false);
        Require(!Ownership::Owns(test_device,16384,test_device+1792),
          "nonnative tail inherited native packet routing");
      }
      Require(Ownership::Owns(test_device,16384,test_device+1792),
        "nested tail did not restore native packet routing");
    }
    Require(Ownership::OwnsImmediateAllocation(test_device,0x821fd6e8,16,16),
      "CPU-only tail did not restore outer legacy allocation scope");
  }
  try {
  {
    auto expected_arena=std::make_unique<Arena>(),actual_arena=std::make_unique<Arena>();
    for(uint32_t address:{0u,0x1001u,0x7f80u,0x7f81u,0x7f801234u,0x7f7fffffu,
        0x7f800000u,0x7f0fffffu,0x7f100000u,0x86ffffffu,0x87000000u,0xa0001000u,0xfffff001u})
    for(uint32_t length:{0u,1u,127u,128u,129u,895u,896u,1023u,1024u,1025u,4097u})
    for(unsigned flags=0;flags<4;++flags) {
      expected_arena->bytes.fill(0x5a); actual_arena->bytes=expected_arena->bytes;
      PPCContext expected{}; expected.r1.u64=0x90000;
      expected.r3.u64=uint64_t(address)|(flags&1?0xffffffff00000000ull:0);
      expected.r4.u64=uint64_t(uint32_t(address+length))|(flags&1?0xffffffff00000000ull:0);
      expected.r8.u64=0x1122334455667788ull; expected.r9.u64=expected.r8.u64;
      expected.r10.u64=expected.r8.u64; expected.r11.u64=expected.r8.u64;
      expected.xer.ca=flags&1; expected.xer.so=(flags>>1)&1;
      expected.lr=0x12345678; expected.r31.u64=0xabcdef0123456789ull;
      auto actual=expected;
      __imp__sub_82141AB8(expected,expected_arena->bytes.data());
      edf::native::NativeCacheFlushCpu(actual,FlushReader{actual_arena->bytes.data()});
      Require(std::memcmp(&actual,&expected,sizeof(actual))==0 && actual_arena->bytes==expected_arena->bytes,
        "constant-time cache flush differs from original CPU state or scratch");
    }
  }
  {
    ShaderPatchArena arena; auto* base=arena.bytes;
    constexpr uint32_t blend_device=0x1000,stack=0x90000;
    edf::native::NativeRenderStateSnapshots owned;
    // Exercise actual generated conversion/stores. The scale is a controlled
    // fixture input, not an assertion about the value in the shipped image.
    for(float scale:{0.f,.5f,1.f,1.f/255.f}) {
      REX_STORE_U32(0x8200964c,std::bit_cast<uint32_t>(scale));
      for(unsigned shift:{0u,8u,16u,24u}) for(uint32_t value=0;value<256;++value) {
        const uint32_t packed=(0x7f35c9a6u&~(255u<<shift))|(value<<shift);
        std::array<uint8_t,20480> expected_device;
        expected_device.fill(0x5a);
        std::memcpy(base+blend_device,expected_device.data(),expected_device.size());
        const auto put=[&](uint32_t offset,uint32_t word) {
          for(unsigned i=0;i<4;++i) expected_device[offset+i]=uint8_t(word>>(24-8*i));
        };
        edf::native::NativeRenderStateSnapshots::BlendWords expected{};
        const std::array<uint32_t,4> channels{(packed>>16)&255,(packed>>8)&255,packed&255,packed>>24};
        for(size_t channel=0;channel<4;++channel) {
          expected[channel]=std::bit_cast<uint32_t>(float(double(channels[channel])*double(scale)));
          put(10336+uint32_t(channel)*4,expected[channel]);
        }
        // Big-endian 64-bit dirty mask at +24: OR low word with 0x3c000.
        put(28,0x5a5a5a5au|0x3c000u);
        PPCContext ctx{}; ctx.r1.u64=stack; ctx.r3.u64=blend_device; ctx.r4.u64=packed;
        ctx.lr=0x12345678; ctx.r31.u64=0x1122334455667788ull;
        __imp__sub_82135418(ctx,base);
        Require(std::memcmp(base+blend_device,expected_device.data(),expected_device.size())==0,
          "retail blend setter changed channel conversion, dirty mask or unrelated device bytes");
        Require(ctx.r1.u64==stack && ctx.r3.u64==blend_device && ctx.r4.u64==packed &&
          ctx.lr==0x12345678 && ctx.r31.u64==0x1122334455667788ull,"blend setter ABI changed");
        Require(REX_LOAD_U64(stack-32)==channels[0] && REX_LOAD_U64(stack-24)==channels[1] &&
          REX_LOAD_U64(stack-16)==channels[2] && REX_LOAD_U64(stack-8)==channels[3],
          "blend setter stack scratch channel order changed");
        edf::native::NativeRenderStateSnapshots::BlendWords completed{};
        for(size_t channel=0;channel<4;++channel)
          completed[channel]=REX_LOAD_U32(blend_device+10336+uint32_t(channel)*4);
        owned.PublishBlend(blend_device,completed,0x82135418);
        Require(owned.RequireBlend(blend_device)==expected,"published retail blend words differ");
      }
    }
  }
  // Actual retail record-copy helper: the allocator is controlled, but all
  // sixteen-byte record loads/stores and pointer semantics are extracted.
  for(bool fail:{false,true}) for(uint32_t count:{0u,1u,3u}) {
    auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
    constexpr uint32_t source=0x1000,destination=0x1100,records=0x2000;
    REX_STORE_U32(source,records); REX_STORE_U32(source+8,count);
    for(uint32_t i=0;i<3;++i) {
      REX_STORE_U32(records+i*16,0x5000+i*32);
      REX_STORE_U32(records+i*16+4,0x6000+i*64);
      REX_STORE_U32(records+i*16+8,i+1);
      REX_STORE_U32(records+i*16+12,i*4);
    }
    std::memset(base+copy,0xcd,64);
    material_copy_fail=fail; material_copy_count=~0u;
    PPCContext context{}; context.r1.u64=0x90000;
    context.r3.u64=destination; context.r4.u64=source;
    context.r29.u64=0x1122334455667788ull;
    context.r30.u64=0x2233445566778899ull; context.r31.u64=0x33445566778899aaull;
    context.lr=0x12345678;
    __imp__sub_821BC588(context,base);
    Require(material_copy_count==count,"material record copy resize count");
    Require(context.r3.u32==destination && context.r1.u32==0x90000 &&
      context.lr==0x12345678 && context.r29.u64==0x1122334455667788ull &&
      context.r30.u64==0x2233445566778899ull && context.r31.u64==0x33445566778899aaull,
      "material record copy ABI");
    for(uint32_t i=0;i<64;++i)
      Require(base[copy+i]==(!fail && i<count*16?base[records+i]:0xcd),
        "material record copy bytes/guard");
    // Exact copied bytes include name/data pointers: there is no rebasing.
    if(!fail && count) Require(REX_LOAD_U32(copy+4)==0x6000,
      "material copy must preserve source data pointer");
  }
  // Actual local-value update: count clipping, multiple targets, zero-sized
  // targets, empty lists and unmodified trailing bytes. No memcpy hook involved.
  for(uint32_t targets:{0u,3u}) for(uint32_t requested:{0u,1u,2u,4u}) {
    auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
    constexpr uint32_t list=0x1000,pointers=0x2000,records=0x3000,input=0x4000;
    REX_STORE_U32(list+4,pointers); REX_STORE_U32(list+12,targets);
    for(uint32_t i=0;i<64;++i) base[input+i]=uint8_t(i*7+3);
    for(uint32_t i=0;i<3;++i) {
      REX_STORE_U32(pointers+i*4,records+i*16);
      REX_STORE_U32(records+i*16+4,0x5000+i*128);
      REX_STORE_U32(records+i*16+8,i);
      std::memset(base+0x5000+i*128,0xcd,80);
    }
    PPCContext context{}; context.r1.u64=0x90000; context.r4.u64=list;
    context.r5.u64=input; context.r6.u64=requested;
    context.r31.u64=0xaabbccddeeff0011ull; context.lr=0x12345678;
    __imp__sub_821BC430(context,base);
    Require(context.r1.u32==0x90000 && context.r31.u64==0xaabbccddeeff0011ull &&
      context.lr==0x12345678,"material value update ABI");
    for(uint32_t i=0;i<3;++i) for(uint32_t b=0;b<80;++b) {
      const auto written=targets?16*(std::min)(requested,i):0u;
      Require(base[0x5000+i*128+b]==(b<written?base[input+b]:0xcd),
        "material value update clipping/guard");
    }
  }
  for(bool fvf:{false,true}) for(uint32_t value: {0u,2u,4u,6u,0x102u,0x112u,0x144u,0x204u}) {
    auto original=std::make_unique<ShaderPatchArena>(),native=std::make_unique<ShaderPatchArena>();
    auto* base=original->bytes;
    // Controlled FVF size/type lookup at the retail table address. The fixture
    // tests producer ABI and publication, not the disc's format lookup contents.
    std::memset(base+0x8200bff8,8,4);
    REX_STORE_U32(0x8200c000,0x2c23a5);
    std::memcpy(native->bytes+0x8200b000,base+0x8200b000,8192);
    const auto count=value%4;
    for(uint32_t i=0;i<count;++i) {
      REX_STORE_U16(vertices+i*12,0);
      REX_STORE_U16(vertices+i*12+2,uint16_t(i*8));
      REX_STORE_U32(vertices+i*12+4,0x2c23a5);
    }
    REX_STORE_U16(vertices+count*12,255);
    std::memcpy(native->bytes,original->bytes,1024*1024);
    PPCContext expected{},actual{};
    expected.r1.u64=0x90000; expected.r3.u64=fvf?value:vertices;
    expected.r4.u64=copy; expected.r31.u64=0x12345678; expected.lr=0xabcdef;
    actual=expected;
    declaration_allocation=!fvf; declaration_allocation_fail=false;
    (fvf?__imp__sub_82147BA0:__imp__sub_82149AB0)(expected,original->bytes);
    (fvf?__imp__edf_test_sub_82147BA0:__imp__edf_test_sub_82149AB0)(actual,native->bytes);
    Require(declaration_content_handle==copy,"constructor published wrong declaration address");
    const auto owned=captured_contents.Get(copy);
    base=native->bytes;
    Require(owned->bytes().size()==size_t(REX_LOAD_U32(copy+24))*12 &&
      std::equal(owned->bytes().begin(),owned->bytes().end(),base+copy+52),
      "constructor published incomplete declaration bytes");
    Require(std::memcmp(original->bytes,native->bytes,1024*1024)==0 && actual.r3.u64==expected.r3.u64 &&
      actual.r4.u64==expected.r4.u64 && actual.r1.u64==expected.r1.u64 && actual.lr==expected.lr &&
      actual.r31.u64==expected.r31.u64,"declaration content hook changed original CPU behavior");
    if(!fvf) {
      declaration_allocation_fail=true; actual.r1.u64=0x90000; actual.r3.u64=vertices;
      __imp__edf_test_sub_82149AB0(actual,base);
      Require(actual.r3.u32==0 && declaration_content_handle==0 && captured_contents.Get(copy)==owned,
        "failed declaration constructor replaced published content");
    }
    declaration_allocation=false;
  }
  {
    edf::native::NativeShaderState state;
    bool rejected=false;
    try { state.Pair(device); } catch(const std::exception&) { rejected=true; }
    Require(rejected,"missing shader producers accepted");
    state.Set(device,0,false); rejected=false;
    try { state.Pair(device); } catch(const std::exception&) { rejected=true; }
    Require(rejected && state.Vertex(device)==0,"missing pixel producer or explicit vertex unbind mishandled");
    state.Set(device,0,true);
    Require(state.Pair(device).pixel==0 && state.Pair(device).vertex==0,"explicit null pair rejected");
  }
  for(bool pixel:{false,true}) for(uint32_t owner:{device,device+0x4000})
    for(uint32_t shader:{0x31000u,0u,0x32000u}) for(bool old_bound:{false,true}) {
      auto original=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      auto* base=original->bytes.data();
      REX_STORE_U32(owner+(pixel?12416:12420),old_bound?0x33000:0);
      REX_STORE_U32(owner+10780,0x123); // Old-resource fence path, not a GPU command.
      *native=*original;
      PPCContext expected{},actual{};
      expected.r1.u64=0x90000; expected.r3.u64=owner; expected.r4.u64=shader;
      expected.r31.u64=0x98765432; expected.lr=0xabcdef; actual=expected;
      (pixel?__imp__sub_82149608:__imp__sub_821498C8)(expected,original->bytes.data());
      declaration_fixture_base=native->bytes.data();
      (pixel?__imp__edf_test_sub_82149608:__imp__edf_test_sub_821498C8)(actual,native->bytes.data());
      // Supply the other stage to distinguish published null from missing state.
      captured_shaders.Set(owner,0,!pixel);
      const auto pair=captured_shaders.Pair(owner);
      Require((pixel?pair.pixel:pair.vertex)==shader,"wrong native shader identity");
      Require(original->bytes==native->bytes,"shader hook changed CPU memory behavior");
      Require(actual.r1.u64==expected.r1.u64 && actual.r3.u64==expected.r3.u64 &&
        actual.r4.u64==expected.r4.u64 && actual.r31.u64==expected.r31.u64 && actual.lr==expected.lr,
        "shader hook changed CPU return state");
    }
  for(bool fvf:{false,true}) for(uint32_t owner:{device,device+0x4000})
    for(uint32_t declaration:{0x31000u,0u,0x32000u}) {
      auto original=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      PPCContext expected{},actual{};
      expected.r1.u64=0x90000; expected.r3.u64=owner;
      expected.r4.u64=fvf?0x112u:declaration;
      expected.r31.u64=0x98765432; expected.lr=0xabcdef;
      actual=expected; converted_declaration=declaration;
      (fvf?__imp__sub_8214B0A0:__imp__sub_82149A90)(expected,original->bytes.data());
      declaration_fixture_base=native->bytes.data();
      (fvf?__imp__edf_test_sub_8214B0A0:__imp__edf_test_sub_82149A90)(actual,native->bytes.data());
      Require(captured_declarations.at(owner)==declaration,"wrong native declaration identity");
      Require(original->bytes==native->bytes,"declaration hook changed CPU memory behavior");
      Require(actual.r1.u64==expected.r1.u64 && actual.r3.u64==expected.r3.u64 &&
        actual.r4.u64==expected.r4.u64 && actual.r31.u64==expected.r31.u64 && actual.lr==expected.lr,
        "declaration hook changed CPU return state");
    }
  declaration_fixture_base=nullptr;
  for(uint32_t pattern:{0u,0xffffffffu,0xdeadbeefu,0x55555555u})
    for(bool overflow:{false,true}) for(bool optional_packet:{false,true}) {
      auto original=std::make_unique<ShaderPatchArena>();
      auto native=std::make_unique<Arena>();
      auto* base=original->bytes;
      std::memset(base,0,1024*1024);
      for(uint32_t offset=0;offset<20480;offset+=4) REX_STORE_U32(device+offset,pattern);
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,overflow?cursor-4:cursor+2048);
      REX_STORE_U32(0x82578e28,optional_packet?0x12345678:0);
      REX_STORE_U32(0x82578e2c,0x1234); REX_STORE_U32(0x82578e30,0x5678);
      std::fill_n(base+cursor,2048,0xa5);
      std::copy_n(base,native->bytes.size(),native->bytes.data());
      PPCContext expected{}; expected.r1.u64=0x90000; expected.r3.u64=device; expected.lr=0x11223344;
      expected.r27.u64=27; expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected;
      rollovers=0;
      __imp__sub_8214EFF8(expected,base);
      Require(rollovers==unsigned(overflow),"default-state retail rollover negative control failed");
      const uint32_t expected_cursor=cursor+(overflow?1024:0)+(optional_packet?80:72);
      Require(REX_LOAD_U32(device+40)==expected_cursor,"default-state retail packet extent changed");
      rollovers=0;
      // Native arena has no mapping for the Xbox-only fixed packet globals.
      __imp__edf_native_device_defaults(actual,native->bytes.data());
      Require(rollovers==0,"native defaults retained command rollover");
      Require(actual.r3.u64==expected.r3.u64 && actual.r3.u32==3650 && actual.r1.u64==expected.r1.u64 && actual.lr==expected.lr,
        "native defaults changed CPU return/stack/LR");
      Require(actual.r27.u64==expected.r27.u64 && actual.r28.u64==expected.r28.u64 && actual.r29.u64==expected.r29.u64 &&
        actual.r30.u64==expected.r30.u64 && actual.r31.u64==expected.r31.u64,"native defaults changed nonvolatile registers");
      auto* native_base=native->bytes.data();
      Require(std::all_of(native_base+cursor,native_base+cursor+2048,[](uint8_t b){return b==0xa5;}),"native defaults wrote command packets");
      Require(!std::equal(base+cursor,base+cursor+2048,native_base+cursor),"retail defaults emitted no packet");
      // Preserve CPU10788 and all descriptor/default words; normalize only
      // the intentionally omitted command buffer/cursor in the retail arena.
      std::copy_n(native_base+cursor,2048,base+cursor);
      REX_STORE_U32(device+40,cursor);
      Require(std::equal(base,base+native->bytes.size(),native_base),"native defaults changed CPU state or command cursor");
    }
  // Differential actual worker initializer: CPU records, thread arguments,
  // partial failure and ABI survive; packet bytes and rollover do not.
  for(uint32_t mask:{0u,1u,3u,21u,42u,63u}) for(unsigned failure:{0u,1u,3u,6u})
    for(bool overflow:{false,true}) for(bool disabled:{false,true}) {
      auto original=std::make_unique<Arena>();
      auto native=std::make_unique<Arena>();
      auto* base=original->bytes.data();
      REX_STORE_U32(device+20416,(mask<<24)|(disabled?0x100:0));
      REX_STORE_U32(device+13476,0x1234); REX_STORE_U32(device+13480,0x5678);
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,overflow?cursor-4:cursor+2048);
      std::fill_n(base+cursor,2048,0xa5);
      native->bytes=original->bytes;
      PPCContext expected{}; expected.r1.u64=0x90000; expected.r3.u64=device; expected.lr=0x11223344;
      expected.r21.u64=21; expected.r22.u64=22; expected.r23.u64=23; expected.r24.u64=24;
      expected.r25.u64=25; expected.r26.u64=26; expected.r27.u64=27; expected.r28.u64=28;
      expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected;
      worker_init_fixture=true; worker_init_failure=failure;
      worker_init_calls.clear(); worker_init_priorities.clear(); rollovers=0;
      __imp__sub_8214EE50(expected,base);
      const auto calls=worker_init_calls;
      const auto priorities=worker_init_priorities;
      const auto original_rollovers=rollovers;
      const unsigned active=disabled?0u:std::popcount(mask);
      const bool failed=failure && failure<=active;
      Require(calls.size()==(failed?failure:active) && priorities.size()==calls.size()-unsigned(failed),
        "worker mask/failure fixture did not exercise expected calls");
      Require(expected.r3.u32==unsigned(!failed),"worker initialization failure result changed");
      worker_init_calls.clear(); worker_init_priorities.clear(); rollovers=0;
      __imp__edf_native_worker_init(actual,native->bytes.data());
      Require(worker_init_calls==calls && worker_init_priorities==priorities,"native worker creation/priority arguments changed");
      Require(rollovers==0 && original_rollovers==unsigned(overflow?calls.size():0),"native worker initialization retained packet rollover");
      Require(actual.r1.u64==expected.r1.u64 && actual.lr==expected.lr && actual.r3.u64==expected.r3.u64,"native worker initialization changed return/stack/LR");
#define CHECK_WORKER_REGISTER(n) Require(actual.r##n.u64==expected.r##n.u64,"native worker initialization changed nonvolatile register")
      CHECK_WORKER_REGISTER(21); CHECK_WORKER_REGISTER(22); CHECK_WORKER_REGISTER(23); CHECK_WORKER_REGISTER(24);
      CHECK_WORKER_REGISTER(25); CHECK_WORKER_REGISTER(26); CHECK_WORKER_REGISTER(27); CHECK_WORKER_REGISTER(28);
      CHECK_WORKER_REGISTER(29); CHECK_WORKER_REGISTER(30); CHECK_WORKER_REGISTER(31);
#undef CHECK_WORKER_REGISTER
      const auto* native_base=native->bytes.data();
      Require(std::all_of(native_base+cursor,native_base+cursor+2048,[](uint8_t b){return b==0xa5;}),"native worker initialization wrote Xbox packet bytes");
      if(!calls.empty()) Require(!std::equal(base+cursor,base+cursor+2048,native_base+cursor),"retail negative control emitted no packet");
      Require(native_base[device+40]==uint8_t(cursor>>24) && native_base[device+41]==uint8_t(cursor>>16) &&
        native_base[device+42]==uint8_t(cursor>>8) && native_base[device+43]==uint8_t(cursor),"native worker initialization advanced packet cursor");
      // Normalize only intentionally removed GPU side effects.
      std::copy_n(native_base+cursor,2048,base+cursor);
      std::copy_n(native_base+device+40,4,base+device+40);
      Require(disabled || std::all_of(native_base+device+10816,native_base+device+10820,
        [](uint8_t b){return b==0;}),"native worker retained GPU ring alias");
      std::copy_n(native_base+device+10816,4,base+device+10816);
      Require(original->bytes==native->bytes,"native worker initialization changed CPU records or stack");
      worker_init_fixture=false;
    }
  file_writes.Subscribe(1,copy,4096);
  // The generic selector tail-dispatches without replacing LR. High 64-bit
  // flag bits cannot stand in for the guest's 32-bit physical-memory flag.
  for(uint64_t flags:{0ull,4ull,0x7fffffffull,0x80000000ull,0xb1800000ull,
                      0xffffffffull,0x8000000000000000ull,0xffffffff80000004ull})
    for(uint32_t address:{0u,0xa0004000u,0xe0008000u})
      for(uint64_t caller:{0ull,0x82145ab0ull}) for(bool native_mode:{false,true}) {
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r3.u64=address; context.r4.u64=flags;
    context.r5.u64=0xfedcba9876543210ull; context.lr=caller;
    generic_free_route=0; generic_free_arguments={};
    const bool prior_native=texture_native; texture_native=native_mode;
    chunk_free_events.clear(); chunk_free_address=address;
    generic_retirement_expected=native_mode && (uint32_t(flags)&0x80000000u) && address;
    __imp__edf_test_generic_free(context,arena->bytes.data());
    CheckBackingReleaseExclusion(false);
    texture_native=prior_native;
    const bool physical=(uint32_t(flags)&0x80000000u)!=0;
    const unsigned expected=physical?(address?1u:0u):2u;
    Require(chunk_free_events==(native_mode && expected==1?std::vector<unsigned>{1}:std::vector<unsigned>{}),
      "generic release retirement policy changed");
    Require(generic_free_route==expected,"generic release selected wrong heap or null policy");
    if(expected) Require(generic_free_arguments==std::array<uint64_t,4>{address,flags,0xfedcba9876543210ull,caller},
      "generic release changed arguments or inherited LR");
    Require(context.r3.u64==(expected?0x123456789abcdef0ull:address) && context.lr==caller &&
      context.r4.u64==flags && context.r5.u64==0xfedcba9876543210ull,
      "generic release lost provider return or caller state");
  }
  {
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r3.u32=0xa0004000; context.r4.u32=0x80000000;
    const bool prior_native=texture_native; texture_native=true;
    generic_free_route=0; chunk_free_events.clear(); chunk_free_address=context.r3.u32;
    chunk_retirement_throws=true;
    bool rejected=false;
    try { __imp__edf_test_generic_free(context,arena->bytes.data()); }
    catch(const std::runtime_error&) { rejected=true; }
    chunk_retirement_throws=false; texture_native=prior_native;
    CheckBackingReleaseExclusion(false);
    Require(rejected && generic_free_route==0 && chunk_free_events==std::vector<unsigned>{1},
      "generic release forwarded after failed retirement");
  }
  for(bool native_mode:{false,true}) for(uint32_t caller:{0x821D3BECu,0x821D18B4u,0x821D190Cu,0x821D3A90u,0x821D4404u,0u})
    for(uint32_t address:{0xa0004000u,0xe0008000u}) {
    const bool prior_native=texture_native; texture_native=native_mode;
    chunk_free_events.clear(); chunk_free_address=address;
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r3.u64=address; context.lr=caller;
    context.r5.u64=0x123456789abcdef0ull;
    __imp__edf_test_chunk_free(context,arena->bytes.data());
    CheckBackingReleaseExclusion(false);
    Require(chunk_free_events==(native_mode && (caller==0x821D3BEC || caller==0x821D18B4 || caller==0x821D190C || caller==0x821D4404)?
      std::vector<unsigned>{1,2}:std::vector<unsigned>{2}) &&
      context.r3.u32==0xdeadbeef && context.r4.u32==0xfeed1234 && context.lr==caller &&
      context.r5.u64==0x123456789abcdef0ull,"chunk free retirement policy or return ABI changed");
    texture_native=prior_native;
  }
  for(uint32_t caller:{0x821D3BECu,0x821D18B4u,0x821D190Cu,0x821D4404u}) {
    const bool prior_native=texture_native; texture_native=true;
    chunk_free_events.clear(); chunk_free_address=0xa0004000; chunk_retirement_throws=true;
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r3.u64=chunk_free_address; context.lr=caller;
    bool rejected=false;
    try { __imp__edf_test_chunk_free(context,arena->bytes.data()); }
    catch(const std::runtime_error&) { rejected=true; }
    chunk_retirement_throws=false; texture_native=prior_native;
    CheckBackingReleaseExclusion(false);
    Require(rejected && chunk_free_events==std::vector<unsigned>{1} && context.r3.u32==chunk_free_address,
      "failed native retirement still freed physical backing");
  }
  for(bool index:{false,true}) for(bool native_mode:{false,true}) for(uint32_t locks:{0u,0x100u,0x200u})
    for(uint32_t dirty:{0xffff0000u,0x00010000u,0x00000009u}) for(uint32_t physical_flag:{0u,0x200000u})
    for(uint32_t address:{0xc0030000u,0xc0030003u}) {
    const bool prior_native=texture_native; texture_native=native_mode;
    auto original=std::make_unique<Arena>();
    auto actual=std::make_unique<Arena>();
    auto* base=original->bytes.data();
    REX_STORE_U32(copy,locks|physical_flag);
    REX_STORE_U32(copy+20,dirty);
    REX_STORE_U32(copy+24,address);
    actual->bytes=original->bytes;
    PPCContext expected{}; expected.r1.u64=0x80000; expected.r3.u64=copy;
    expected.lr=0x8242D4BC; expected.r28.u64=0x123456789abcdef0ull;
    auto context=expected;
    const auto reference_flush_begin=retail_cache_flush_calls;
    if(index) __imp__sub_82134AD8(expected,base);
    else __imp__sub_821349B8(expected,base);
    const auto reference_flush_count=retail_cache_flush_calls-reference_flush_begin;
    file_writes.Drain();
    const auto before=*file_writes.Version(1);
    vertex_unlock_owner=copy; vertex_unlock_updates=0;
    index_unlock_retail=index; index_unlock_native=native_mode;
    const auto native_flush_begin=retail_cache_flush_calls;
    if(index) __imp__edf_test_inline_unlock(context,actual->bytes.data());
    else __imp__edf_test_sub_821349B8(context,actual->bytes.data());
    index_unlock_retail=false;
    Require(retail_cache_flush_calls-native_flush_begin==(native_mode?0:reference_flush_count),
      "native unlock dispatched retail cache helper or changed legacy dispatch");
    const auto batch=file_writes.Drain();
    Require(original->bytes==actual->bytes && std::memcmp(&expected,&context,sizeof(context))==0,
      "vertex unlock guard changed retail memory or CPU context");
    Require(vertex_unlock_updates==1 && batch.count==(native_mode?1u:0u) &&
      file_writes.Version(1).has_value(),"vertex unlock notification or scope release changed");
    if(native_mode) Require(batch.ranges[0].address==copy && batch.ranges[0].bytes==24 &&
      !file_writes.CommitObserved(1,before,[] {}),"vertex unlock failed to invalidate exact header extent");
    texture_native=prior_native;
  }
  {
    auto arena=std::make_unique<Arena>();
    auto* base=arena->bytes.data();
    for(uint32_t low:{0u,10u,0x80000000u,UINT32_MAX})
      for(uint32_t high:{0u,20u,0x80000000u,UINT32_MAX})
      for(uint32_t begin:{0u,5u,15u,UINT32_MAX})
      for(uint32_t end:{0u,15u,25u,UINT32_MAX}) {
      file_writes.Drain();
      const auto before=*file_writes.Version(1);
      REX_STORE_U64(copy,(uint64_t(high)<<32)|low);
      PPCContext context{}; context.r3.u64=copy; context.r4.u64=begin;
      context.r5.u64=end; context.lr=0x12345678;
      __imp__edf_test_sub_8213BDF8(context,base);
      Require(REX_LOAD_U64(copy)==((uint64_t(std::max(high,end))<<32)|std::min(low,begin)),
        "atomic dirty range changed packed min/max semantics");
      Require(context.r3.u32==copy && context.r4.u32==begin && context.r5.u32==end && context.lr==0x12345678,
        "dirty-range hook changed argument/return ABI");
      const auto batch=file_writes.Drain();
      Require(batch.count==1 && batch.ranges[0].address==copy && batch.ranges[0].bytes==8 &&
        file_writes.Version(1).has_value() && !file_writes.CommitObserved(1,before,[] {}),
        "dirty-range hook lost completion extent, stale invalidation or scope release");
    }
  }
  file_writes.Subscribe(2,1,24);
  for(bool tracked:{false,true}) for(bool payload_active:{false,true}) {
    const bool prior_native=texture_native; texture_native=true;
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r3.u64=1; context.lr=tracked?0x8242D3DC:0x8242D3E0;
    inline_expected=tracked; inline_header_expected=true; inline_payload_active=payload_active; inline_order=0;
    edf::native::NativeBufferWriteFrame frame;
    if(tracked) frame.Begin(1,payload_active?&file_writes:nullptr,copy,
      edf::native::NativeBufferWrites::Range{copy,12});
    __imp__edf_test_inline_unlock(context,arena->bytes.data());
    texture_native=prior_native;
    frame.RequireFinished();
    const auto batch=file_writes.Drain();
    Require(inline_order==(tracked?4u:3u) && context.r3.u32==0xdeadbeef && batch.count==(tracked?2u:1u),
      "inline hook changed ABI or notification sequence");
    if(tracked) Require(batch.ranges[0].address==1 && batch.ranges[0].bytes==24 &&
      batch.ranges[1].address==copy && batch.ranges[1].bytes==12,
      "inline hook did not notify original header and locked destination");
  }
  file_writes.Unsubscribe(2);
  file_writes.Subscribe(3,copy+16384,8);
  for(uint32_t routine:{0u,1u,0x1234u,0x1235u}) for(uint32_t apc_context:{0u,0x4000u}) {
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r5.u64=routine; context.r6.u64=apc_context;
    context.r7.u64=copy+8192; context.r8.u64=copy; context.r9.u64=16;
    file_scope_probe=true; file_scope_exact=!((routine&~1u) && apc_context);
    file_read_status=0; file_read_finished=false;
    __imp__edf_test_native_file_read(context,arena->bytes.data());
    file_scope_probe=false;
    Require(file_writes.Drain().count==2 && file_writes.Version(1).has_value(),
      "file APC selection changed completion coverage or leaked scope");
  }
  file_writes.Unsubscribe(3);
  for(uint32_t status:{0u,0x103u,0xc0000011u}) for(uint32_t length:{0u,16u}) {
    auto arena=std::make_unique<Arena>();
    constexpr uint32_t status_address=copy+8192;
    file_writes.Subscribe(2,status_address,8);
    const auto before=*file_writes.Version(2);
    PPCContext context{}; context.r7.u64=status_address; context.r8.u64=copy; context.r9.u64=length;
    file_read_status=status; file_read_finished=false;
    __imp__edf_test_native_file_read(context,arena->bytes.data());
    const auto batch=file_writes.Drain();
    Require(file_read_finished && context.r3.u32==status && context.r7.u32==0xfeedface &&
      batch.count==(length?2u:1u) && file_writes.Version(2)->revision==before.revision+1,
      "file status-only/failure write escaped invalidation or changed return ABI");
    const auto& status_range=batch.ranges[batch.count-1];
    Require(status_range.address==status_address && status_range.bytes==8 &&
      !file_writes.CommitObserved(2,before,[] {}),"status block notification lost extent or accepted stale storage");
    file_writes.Unsubscribe(2);
  }
  for(uint32_t status:{0u,0x103u,0xc0000011u}) for(uint32_t length:{0u,1u,16u}) {
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r8.u64=copy; context.r9.u64=length;
    context.r31.u64=0x12345678;
    file_read_status=status; file_read_finished=false;
    __imp__edf_test_native_file_read(context,arena->bytes.data());
    Require(file_writes.Version(1).has_value(),"file scope leaked after return");
    Require(context.r3.u32==status && context.r8.u32==0xdeadbeef && context.r9.u32==0xbadf00d &&
      context.r31.u32==0x12345678,"file adapter changed original return/register state");
    edf::native::NativeModelBuffers owners;
    owners.Publish(0x1000,edf::native::NativeModelBuffers::Kind::Vertex,copy,4,4,copy);
    unsigned affected=0;
    owners.ApplyWrites(file_writes.Drain(),[&](uint32_t owner) {
      Require(owner==0x1000,"file write invalidated unrelated owner"); ++affected;
    });
    Require(affected==unsigned(length!=0),"file return status incorrectly suppressed owner invalidation");
  }
  {
    auto arena=std::make_unique<Arena>();
    PPCContext context{}; context.r8.u64=copy; context.r9.u64=16;
    const auto before=*file_writes.Version(1);
    file_read_throw=true; file_read_finished=false; bool caught=false;
    try { __imp__edf_test_native_file_read(context,arena->bytes.data()); }
    catch(const std::runtime_error&) { caught=true; }
    file_read_throw=false;
    Require(caught && !file_read_finished && arena->bytes[copy]==0x4f && file_writes.Version(1).has_value() &&
      !file_writes.CommitObserved(1,before,[] {}),"throwing file provider escaped transaction or leaked scope");
    const auto aborted=file_writes.Drain();
    Require(aborted.all && aborted.aborted_writers==1 && aborted.provider_calls==0,
      "partial file write did not queue conservative invalidation");
    edf::native::NativeModelBuffers owners;
    owners.Publish(0x1000,edf::native::NativeModelBuffers::Kind::Vertex,copy,4,4,copy);
    unsigned affected=0;
    owners.ApplyWrites(aborted,[&](uint32_t owner) {
      Require(owner==0x1000,"aborted write returned wrong owner"); ++affected;
    });
    Require(affected==1,"partial file write failed to invalidate retained geometry");
  }
  for(uint32_t length:{0u,1u,3u,4u,5u,7u,8u,31u,32u}) {
    auto arena=std::make_unique<Arena>();
    std::fill_n(arena->bytes.data()+copy,40,0xa5);
    PPCContext context{}; context.r3.u64=copy; context.r4.u64=length; context.r5.u64=0x12345678;
    word_fill_finished=false; word_fill_notifications=0;
    file_writes.Subscribe(3,copy+16384,8);
    file_writes.Subscribe(4,copy+(length&~3u),1);
    __imp__edf_test_native_word_fill(context,arena->bytes.data());
    file_writes.Unsubscribe(3);
    file_writes.Unsubscribe(4);
    Require(file_writes.Version(1).has_value(),"word fill scope leaked after return");
    Require(word_fill_notifications==1 && context.r3.u32==0x12345678 && context.r4.u32==0xdeadbeef,
      "word fill adapter changed provider state or notification count");
    for(uint32_t offset=length&~3u;offset<40;++offset)
      Require(arena->bytes[copy+offset]==0xa5,"word fill changed unwritten trailing bytes");
    const auto batch=file_writes.Drain();
    Require(batch.count==unsigned(length>=4),"word fill notification has incorrect empty extent");
    if(batch.count) Require(batch.ranges[0].address==copy && batch.ranges[0].bytes==(length&~3u),"word fill queued wrong extent");
  }
  // Verify the independently called forward-copy ABI from the actual generated
  // routine, including its byte-prefix and unaligned-source word paths. The
  // renderer hook must retain entry r3/r5: r5 is consumed by prefix copying.
  {
    auto arena=std::make_unique<Arena>();
    auto* base=arena->bytes.data();
    for(uint32_t destination_alignment=0;destination_alignment<4;++destination_alignment)
      for(uint32_t source_alignment=0;source_alignment<4;++source_alignment)
        for(uint32_t bytes=0;bytes<=129;++bytes) {
          const uint32_t destination=copy+destination_alignment,source=vertices+source_alignment;
          std::fill_n(base+copy-4,144,0xa5);
          for(uint32_t i=0;i<bytes;++i) base[source+i]=uint8_t(i*37+11);
          PPCContext context{};
          context.r3.u64=destination; context.r4.u64=source; context.r5.u64=bytes;
          context.lr=0x82123456;
          __imp__edf_test_sub_821E8740(context,base);
          Require(file_writes.Version(1).has_value(),"forward copy scope leaked after return");
          const auto forward_site=file_writes.Drain();
          Require(forward_site.writer_hit_count==unsigned(bytes!=0) &&
            (!bytes || (forward_site.writer_hits[0].site.provider==0x821e8740 &&
              forward_site.writer_hits[0].site.caller==0x82123456)),"forward-copy caller attribution changed");
          Require(context.r3.u32==destination,"forward copy changed destination return value");
          Require(std::memcmp(base+destination,base+source,bytes)==0,"forward copy extent/contents mismatch");
          context.r3.u64=destination; context.r4.u64=source; context.r5.u64=bytes;
          context.lr=0x82123460;
          __imp__edf_test_sub_821E8320(context,base);
          Require(file_writes.Version(1).has_value() && std::memcmp(base+destination,base+source,bytes)==0,
            "main copy scope leaked or changed contents");
          const auto main_site=file_writes.Drain();
          Require(context.lr==0x87654321 && main_site.writer_hit_count==unsigned(bytes!=0) &&
            (!bytes || (main_site.writer_hits[0].site.provider==0x821e8320 &&
              main_site.writer_hits[0].site.caller==0x82123460)),"main-copy entry caller or callee output changed");
          for(uint32_t i=copy-4;i<destination;++i)
            Require(base[i]==0xa5,"forward copy overwrote leading guard");
          for(uint32_t i=destination+bytes;i<copy+140;++i)
            Require(base[i]==0xa5,"forward copy overwrote trailing guard");
        }
  }
  // Actual backward memmove: overlapping and disjoint ranges, every alignment,
  // short tails and aligned/unaligned word loops. Entry r5 is not a return extent.
  {
    auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
    for(uint32_t alignment=0;alignment<4;++alignment)
      for(uint32_t displacement:{0u,1u,2u,3u,4u,7u,32u,160u})
        for(uint32_t bytes=0;bytes<=129;++bytes) {
          constexpr size_t extent=512;
          for(size_t i=0;i<extent;++i) base[copy+i]=uint8_t(i*37+11);
          std::array<uint8_t,extent> expected{};
          std::memcpy(expected.data(),base+copy,extent);
          std::memmove(expected.data()+alignment+displacement,expected.data()+alignment,bytes);
          PPCContext ctx{}; ctx.r3.u64=copy+alignment+displacement;
          ctx.r4.u64=copy+alignment; ctx.r5.u64=bytes;
          ctx.lr=0x82123464;
          const auto destination=ctx.r3.u32;
          move_expected=expected.data(); move_destination=destination; move_bytes=bytes; move_notifications=0;
          __imp__edf_test_native_move(ctx,base);
          Require(move_notifications==unsigned(displacement!=0),"backward-copy notification count/self-copy changed");
          const auto batch=file_writes.Drain();
          Require(batch.writer_hit_count==unsigned(displacement!=0 && bytes!=0) &&
            (!batch.writer_hit_count || batch.writer_hits[0].site.provider==0x821ea320),
            "backward-copy inventory count or provider changed");
          move_expected=nullptr;
          Require(!std::memcmp(expected.data(),base+copy,extent),"backward move extent/contents or guards changed");
          Require(ctx.r3.u32==destination,"backward move changed destination return");
        }
    for(uint32_t bytes:{0u,1u,129u}) {
      for(size_t i=0;i<512;++i) base[copy+i]=uint8_t(i*37+11);
      std::array<uint8_t,512> expected{}; std::memcpy(expected.data(),base+copy,512);
      std::memmove(expected.data()+4,expected.data()+320,bytes);
      PPCContext ctx{}; ctx.r3.u64=copy+4; ctx.r4.u64=copy+320; ctx.r5.u64=bytes;
      const auto old_copies=copies; move_notifications=0;
      __imp__edf_test_native_move(ctx,base);
      Require(copies==old_copies+1 && move_notifications==0,
        "forward move failed to delegate or duplicated completion notification");
      Require(!std::memcmp(expected.data(),base+copy,512),"forward adapter changed copied contents");
    }
  }
  {
    ShaderPatchArena arena; auto* base=arena.bytes;
    scalar_observer_test=true;
    for(bool mmio_helper:{false,true})
    for(uint32_t alias:{0xc0070000u,0xe0070000u})
    for(uint32_t width:{1u,2u,4u,8u}) for(uint32_t offset:{0u,1u,7u,16u}) {
      scalar_address=alias+offset; scalar_bytes=width;
      if(alias==0xe0070000u) std::memset(base+alias,0x5a,4096);
      scalar_value=0x123456789abcdef0ull; scalar_notifications=0;
      unsigned evaluations=0;
      auto address=[&] { ++evaluations; return scalar_address; };
      if(mmio_helper) switch(width) {
        case 1: REX_MM_STORE_U8(address(),scalar_value); break;
        case 2: REX_MM_STORE_U16(address(),scalar_value); break;
        case 4: REX_MM_STORE_U32(address(),scalar_value); break;
        case 8: REX_MM_STORE_U64(address(),scalar_value); break;
      }
      else switch(width) {
        case 1: REX_STORE_U8(address(),scalar_value); break;
        case 2: REX_STORE_U16(address(),scalar_value); break;
        case 4: REX_STORE_U32(address(),scalar_value); break;
        case 8: REX_STORE_U64(address(),scalar_value); break;
      }
      Require(evaluations==1 && scalar_notifications==1,"scalar adapter repeated address or lost completion");
      if(alias==0xe0070000u) {
        for(uint32_t i=0;i<4096;++i)
          Require(base[alias+i]==0x5a,"E-alias scalar store missed Windows host offset");
        for(uint32_t i=0;i<width;++i)
          Require(base[uint64_t(scalar_address)+0x1000+i]==uint8_t(scalar_value>>((width-1-i)*8)),
            "E-alias scalar bytes at independent host offset");
      }
    }
    scalar_notifications=0;
    for(uint32_t alias:{0xc0070000u,0xe0070000u}) for(uint32_t width:{4u,8u}) {
      scalar_address=alias; scalar_bytes=width; scalar_value=0x123456789abcdef0ull;
      auto* destination=base+alias+(alias==0xe0070000u?0x1000:0);
      std::memset(destination,0,width); scalar_notifications=0;
      bool succeeded=false,failed=false;
      if(width==4) {
        auto* word=reinterpret_cast<uint32_t*>(destination);
        succeeded=__sync_bool_compare_and_swap(word,0,__builtin_bswap32(uint32_t(scalar_value)));
        failed=__sync_bool_compare_and_swap(word,0,1);
      } else {
        auto* word=reinterpret_cast<uint64_t*>(destination);
        succeeded=__sync_bool_compare_and_swap(word,0,__builtin_bswap64(scalar_value));
        failed=__sync_bool_compare_and_swap(word,0,1);
      }
      Require(succeeded && !failed && scalar_notifications==1,"atomic completion/success notification");
    }
    scalar_notifications=0;
    uint32_t host_atomic=0;
    Require(__sync_bool_compare_and_swap(&host_atomic,0,7) && host_atomic==7,
      "host atomic store changed semantics");
    Require(scalar_notifications==0,"host atomic store entered guest observer");
    edf::native::ObserveRawPhysicalStore(base,base+0xe0000000u,4);
    edf::native::ObserveRawPhysicalStore(base,base+0xe0000fffu,4);
    Require(scalar_notifications==0,"E-alias host gap entered guest observer");
    for(uint32_t alias:{0xc0070000u,0xe0070000u}) {
      auto* destination=base+alias+(alias==0xe0070000u?0x1000:0);
      std::array<uint8_t,128> expected{};
      scalar_address=alias;
      for(uint32_t extent:{16u,32u,128u}) {
        scalar_bytes=extent; scalar_notifications=0;
        std::fill_n(destination,extent+16,0x5a);
        raw_store_expected=std::span(expected).first(extent);
        if(extent==16) {
          for(size_t i=0;i<16;++i) expected[i]=uint8_t(i*13);
          TestObservedVector(base,reinterpret_cast<simde__m128i*>(destination),
            simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(expected.data())));
        } else {
          expected.fill(0);
          Require(TestObservedFill(base,destination,0,extent)==destination,"inline fill return changed");
        }
        Require(scalar_notifications==1,"raw store notification count");
        for(size_t i=extent;i<extent+16;++i) Require(destination[i]==0x5a,"raw store crossed extent");
      }
    }
    raw_store_expected={}; scalar_notifications=0;
    Require(TestObservedFill(base,base+0xc0070000u,0,0)==base+0xc0070000u && scalar_notifications==0,
      "empty inline fill emitted a write");
    alignas(16) std::array<uint8_t,128> host_store{};
    TestObservedVector(base,reinterpret_cast<simde__m128i*>(host_store.data()),simde_mm_setzero_si128());
    TestObservedFill(base,host_store.data(),0,128);
    Require(scalar_notifications==0,"host vector/fill entered guest observer");
    REX_STORE_U32(copy,0x12345678);
    Require(scalar_notifications==0 && REX_LOAD_U32(copy)==0x12345678,
      "ordinary virtual scalar store entered physical observer or changed value");
    REX_MM_STORE_U32(copy,0xabcdef01);
    Require(scalar_notifications==0 && REX_LOAD_U32(copy)==0xabcdef01,
      "ordinary virtual MMIO-helper store entered physical observer or changed value");
    Require(!rex::runtime::MMIOHandler::global_handler(),"MMIO test requires isolated handler");
    auto handler=rex::runtime::MMIOHandler::Install(base,nullptr,base+(size_t(1)<<32),
      nullptr,nullptr,nullptr,nullptr);
    Require(bool(handler),"MMIO test handler installation");
    std::vector<std::pair<uint32_t,uint32_t>> mmio_writes;
    handler->RegisterRange(0x7f000000,0xff000000,0x1000000,&mmio_writes,nullptr,
      [](void*,void* context,uint32_t address,uint32_t value) {
        static_cast<std::vector<std::pair<uint32_t,uint32_t>>*>(context)->emplace_back(address,value);
      });
    REX_MM_STORE_U8(0x7f001000,0x12345678u);
    REX_MM_STORE_U16(0x7f001004,0x89abcdefu);
    REX_MM_STORE_U32(0x7f001008,0xfedcba98u);
    REX_MM_STORE_U64(0x7f001010,0x0123456789abcdefull);
    const std::vector<std::pair<uint32_t,uint32_t>> expected_mmio{
      {0x7f001000,0x12345678},{0x7f001004,0x89abcdef},{0x7f001008,0xfedcba98},
      {0x7f001010,0x01234567},{0x7f001014,0x89abcdef}};
    Require(mmio_writes==expected_mmio,"MMIO observer changed dispatch values/order");
    Require(scalar_notifications==0,"MMIO dispatch entered physical store observer");
    handler.reset();
    scalar_observer_test=false;
  }
  // Bulk fill's actual production hook must report the entry extent only after
  // all stores complete, including alignment prefixes that consume r5.
  {
    auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
    for(uint32_t alignment=0;alignment<4;++alignment)
      for(uint32_t fill:{0u,1u,0x7fu,0xffu,0x12345678u,0xffffffffu})
        for(uint32_t bytes=0;bytes<=257;++bytes) {
          std::fill_n(base+copy,512,0xa5);
          std::array<uint8_t,512> expected{}; expected.fill(0xa5);
          std::fill_n(expected.data()+alignment,bytes,uint8_t(fill));
          PPCContext ctx{}; ctx.r3.u64=copy+alignment; ctx.r4.u64=fill; ctx.r5.u64=bytes;
          ctx.lr=0x12345678;
          move_expected=expected.data(); move_destination=ctx.r3.u32; move_bytes=bytes; move_notifications=0;
          __imp__edf_test_native_fill(ctx,base);
          Require(move_notifications==1,"fill adapter lost completed extent notification");
          const auto batch=file_writes.Drain();
          Require(batch.writer_hit_count==unsigned(bytes!=0) &&
            (!batch.writer_hit_count || batch.writer_hits[0].site.provider==0x821e9ba0),
            "fill inventory count or provider changed");
          Require(ctx.r3.u32==copy+alignment && ctx.lr==0x12345678,"fill adapter changed return ABI");
          move_expected=nullptr;
        }
  }
  // Include texture resources, optional surface parent fences, and the second
  // subresource dirty-range slot used by AD70's nonzero level/base arguments.
  for(const auto contract:std::array<std::array<uint32_t,3>,6>{{
      {1,10,0},{2,12,0},{3,14,0},{4,14,0},
      {0x40000004,14,0},{0x40000004,14,0x61000}}})
    for(bool subresource:{false,true}) for(bool same_thread:{false,true}) for(bool cached:{false,true})
    for(bool fenced:{false,true}) for(uint32_t flags:{0u,1u,2u,16u,17u,4096u})
    for(unsigned wrapper:{0u,1u,2u}) {
      if(wrapper && contract[1]!=10 && contract[1]!=12) continue;
      ShaderPatchArena reference,native;
      auto prepare=[&](uint8_t* base) {
        constexpr uint32_t resource=0x60000;
        REX_STORE_U32(0x82000720,0x800);
        REX_STORE_U32(0x800,device);
        REX_STORE_U32(device+10760,same_thread?7:8);
        REX_STORE_U32(device+40,cursor);
        REX_STORE_U32(device+48,cursor+4096);
        REX_STORE_U32(resource,contract[0]|(cached?0x200000:0));
        REX_STORE_U32(resource+8,fenced?9:0);
        REX_STORE_U32(resource+12,fenced?11:0);
        REX_STORE_U32(resource+20,0xffff0000);
        REX_STORE_U32(resource+24,contract[2]);
        if(contract[2]) {
          REX_STORE_U32(contract[2]+8,fenced?19:0);
          REX_STORE_U32(contract[2]+12,fenced?21:0);
        }
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r22.u64=22; ctx.r23.u64=23; ctx.r24.u64=24; ctx.r25.u64=25;
        ctx.r26.u64=26; ctx.r27.u64=27; ctx.r28.u64=28; ctx.r29.u64=29;
        ctx.r30.u64=30; ctx.r31.u64=31;
        ctx.r3.u64=resource; ctx.r4.u64=contract[1]; ctx.r6.u64=vertices;
        ctx.r5.u64=subresource?1:0; ctx.r7.u64=subresource?vertices:0;
        ctx.r8.u64=vertices+16; ctx.r9.u64=120; ctx.r10.u64=flags;
        if(wrapper) {
          REX_STORE_U32(resource+24,vertices+(contract[1]==10?3:0));
          REX_STORE_U32(resource+28,contract[1]==10?0x10000078u:120u);
          ctx.r4.u64=16; ctx.r5.u64=wrapper==1?0:64; ctx.r6.u64=flags;
        }
        return ctx;
      };
      auto expected=prepare(reference.bytes),actual=prepare(native.bytes);
      buffer_lock_fixture=true; buffer_lock_waits.clear();
      if(!wrapper) __imp__sub_82134408(expected,reference.bytes);
      else if(contract[1]==10) __imp__sub_82134958(expected,reference.bytes);
      else __imp__sub_82134A78(expected,reference.bytes);
      const auto expected_waits=buffer_lock_waits; buffer_lock_waits.clear();
      file_writes.Subscribe(4,0x60000,28);
      file_writes.Drain(); native_lock_tracking=true; native_lock_completions=0;
      if(!wrapper) __imp__edf_native_buffer_lock_cpu_tail(actual,native.bytes);
      else if(contract[1]==10) __imp__edf_native_lock_82134958(actual,native.bytes);
      else __imp__edf_native_lock_82134A78(actual,native.bytes);
      native_lock_tracking=false;
      const auto header_writes=file_writes.Drain();
      Require(native_lock_completions==1 && file_writes.Version(4).has_value() && header_writes.count==1 &&
        header_writes.ranges[0].address==0x60000 && header_writes.ranges[0].bytes==28,
        "lock tail lost completed header notification or retained guard");
      file_writes.Unsubscribe(4);
      Require(buffer_lock_waits==expected_waits && buffer_lock_waits.size()==size_t(fenced),
        "native buffer lock changed resource wait count/arguments");
      if(fenced) Require(buffer_lock_waits[0][1]==((flags&4112)?11u:9u)+(contract[2]?10u:0u) &&
        buffer_lock_waits[0][2]==contract[1],"native buffer lock chose wrong fence/access type");
      buffer_lock_fixture=false;
      Require(actual.r3.u64==expected.r3.u64 && actual.r1.u64==expected.r1.u64 && actual.lr==expected.lr,
        "native buffer lock return ABI changed");
      Require(actual.r22.u64==22 && actual.r23.u64==23 && actual.r24.u64==24 && actual.r25.u64==25 &&
        actual.r26.u64==26 && actual.r27.u64==27 && actual.r28.u64==28 && actual.r29.u64==29 &&
        actual.r30.u64==30 && actual.r31.u64==31,"native buffer lock clobbered nonvolatile state");
      Require(!std::memcmp(reference.bytes+0x60000,native.bytes+0x60000,32),"native buffer lock resource state changed");
      Require(!std::memcmp(reference.bytes+0x61000,native.bytes+0x61000,32),"native texture lock parent state changed");
      Require(!std::memcmp(reference.bytes+device+11544,native.bytes+device+11544,8),"native buffer lock lost dirty range");
      auto* base=native.bytes;
      Require(REX_LOAD_U32(device+40)==cursor,"native lock advanced packet cursor");
      for(size_t i=0;i<64;++i) Require(native.bytes[cursor+i]==0,"native lock encoded cache packet");
    }
  for(uint32_t level:{0u,1u}) for(uint32_t flags:{0u,1u,2u,16u,17u,4096u})
    for(uint32_t block:{1u,4u,16u}) for(uint32_t offset:{0u,32u,128u}) {
      ShaderPatchArena reference,native;
      texture_block_bytes=block; texture_rows=7; texture_offset=offset;
      auto prepare=[&](uint8_t* base) {
        REX_STORE_U32(0x82000720,0x800); REX_STORE_U32(0x800,device);
        REX_STORE_U32(device+10760,7);
        REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor+4096);
        REX_STORE_U32(0x60000,3|0x200000);
        REX_STORE_U32(0x60008,9); REX_STORE_U32(0x6000c,11);
        REX_STORE_U32(0x60014,0xffff0000); REX_STORE_U32(0x60018,0xffff0000);
        REX_STORE_U32(0x60020,vertices|7); REX_STORE_U32(0x60030,vertices-4096);
        REX_STORE_U32(0x62000,vertices+offset); REX_STORE_U32(0x62004,9);
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r26.u64=26; ctx.r27.u64=27; ctx.r28.u64=28;
        ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
        ctx.r3.u64=0x60000; ctx.r4.u64=2; ctx.r5.u64=level; ctx.r6.u64=flags;
        ctx.r7.u64=0x62000; ctx.r8.u64=0x62004; ctx.r9.u64=0; ctx.r10.u64=0x62008;
        return ctx;
      };
      auto expected=prepare(reference.bytes),actual=prepare(native.bytes);
      texture_lock_fixture=buffer_lock_fixture=true;
      texture_layout_calls=texture_format_calls=texture_lock_calls=0;
      texture_native=false; buffer_lock_waits.clear();
      __imp__sub_8213AD70(expected,reference.bytes);
      const auto waits=buffer_lock_waits; buffer_lock_waits.clear();
      texture_native=true;
      __imp__sub_8213AD70(actual,native.bytes);
      Require(texture_layout_calls==2 && texture_format_calls==2 && texture_lock_calls==2,
        "texture lock enclosing helper sequence changed");
      Require(buffer_lock_waits==waits && waits.size()==1,"texture lock enclosing waits changed");
      texture_lock_fixture=buffer_lock_fixture=texture_native=false;
      Require(actual.r3.u64==expected.r3.u64 && actual.r1.u64==expected.r1.u64 && actual.lr==expected.lr,
        "texture lock enclosing return ABI changed");
      Require(actual.r26.u64==26 && actual.r27.u64==27 && actual.r28.u64==28 &&
        actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,
        "texture lock enclosing nonvolatile registers changed");
      Require(!std::memcmp(reference.bytes+0x60000,native.bytes+0x60000,64) &&
        !std::memcmp(reference.bytes+0x62000,native.bytes+0x62000,12),
        "texture lock enclosing resource/output state changed");
      auto* base=native.bytes;
      Require(REX_LOAD_U32(0x62004)==9*block,"texture lock lost pitch conversion");
      Require(REX_LOAD_U32(0x62000)==vertices+offset+((flags&16)?0xc0000000u:0u),
        "texture lock lost returned alias/subresource offset");
      Require(REX_LOAD_U32(device+40)==cursor,"texture lock enclosing native cursor advanced");
      for(size_t i=0;i<64;++i) Require(base[cursor+i]==0,"texture lock enclosing emitted cache packets");
      base=reference.bytes;
      Require(REX_LOAD_U32(device+40)==cursor+((flags&18)?0u:44u),
        "texture lock reference did not exercise expected cache packet path");
    }
    for(bool cpu_only:{false,true}) for(bool owned:{false,true}) for(bool wrap:{false,true})
      for(uint32_t stride:{8,12,16,20,36,44}) for(uint32_t count:{4,20}) {
      expected_stride=stride;
      const auto payload=count*stride;
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      allocations=copies=constants=render=derived=rollovers=0;
      for(uint32_t at:{0,8,16,32}) REX_STORE_U64(device+at,1);
      REX_STORE_U64(device+24,4);
      REX_STORE_U32(device+40,cursor);
      REX_STORE_U32(device+48,wrap?cursor-4:cursor+2048);
      REX_STORE_U8(device+11552,0);
      REX_STORE_U8(device+12256,7);
      REX_STORE_U8(device+10809,0x12);
      PrepareDrawCpuState(base);
      for(uint32_t at:{13076,13080,13088}) REX_STORE_U32(device+at,0x12345678);
      for(uint32_t at=0;at<payload;++at) base[vertices+at]=uint8_t(at);
      PPCContext ctx{};
      ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r22.u64=22; ctx.r23.u64=23; ctx.r24.u64=24; ctx.r25.u64=25;
      ctx.r26.u64=26; ctx.r27.u64=27; ctx.r28.u64=28; ctx.r29.u64=29;
      ctx.r30.u64=30; ctx.r31.u64=31;
      ctx.r3.u64=device; ctx.r4.u64=13; ctx.r5.u64=count; ctx.r6.u64=vertices; ctx.r7.u64=stride;
      {
        auto scope=cpu_only?Ownership::ForCpuOnlyTail(device,owned):Ownership(owned?device:0,payload);
        if(cpu_only) {
          ctx.r6.u32=0xfffffffcu; // CPU-only wrapper must never copy guest vertices.
          __imp__edf_native_immediate_cpu_tail(ctx,base);
        } else sub_821FD8F8(ctx,base);
      }
      Require(ctx.r1.u64==0xf0000 && ctx.lr==0x12345678,"wrapper stack/link register changed");
      Require(ctx.r22.u64==22 && ctx.r23.u64==23 && ctx.r24.u64==24 && ctx.r25.u64==25 &&
        ctx.r26.u64==26 && ctx.r27.u64==27 && ctx.r28.u64==28 && ctx.r29.u64==29 &&
        ctx.r30.u64==30 && ctx.r31.u64==31,"wrapper nonvolatile registers changed");
      if(!(constants==(cpu_only?0u:2u) && render==(cpu_only?0u:4u) && derived==unsigned(!cpu_only) && rollovers==unsigned(wrap && !cpu_only)))
        std::cerr << "immediate helpers: native=" << cpu_only << " owned=" << owned << " wrap=" << wrap
          << " stride=" << stride << " constants=" << constants << " render=" << render << " derived=" << derived << " rollovers=" << rollovers << '\n';
      Require(constants==(cpu_only?0u:2u) && render==(cpu_only?0u:4u) && derived==unsigned(!cpu_only) && rollovers==unsigned(wrap && !cpu_only),"CPU helper sequence changed");
      Require(REX_LOAD_U8(device+12256)==7 && REX_LOAD_U8(device+10809)==0x12,"stride/failure flags changed");
      if(owned || cpu_only) {
        Require(allocations==0 && copies==0 && ctx.r3.u64==0,"owned tail allocated or copied guest vertices");
        Require(REX_LOAD_U32(device+40)==cursor+(wrap && !cpu_only?1024:0),"owned tail lost current command cursor");
        for(uint32_t at:{0,8,16,24,32}) Require(REX_LOAD_U64(device+at)==0,"owned tail left dirty state");
        for(uint32_t at:{13076,13080,13088}) Require(REX_LOAD_U32(device+at)==0x12345678,"owned tail touched GPU scratch");
        for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"owned tail emitted draw packets");
      } else {
        Require(allocations==1 && copies==1,"unowned tail did not retain original allocation/copy");
        Require(std::memcmp(base+copy,base+vertices,payload)==0,"unowned vertex copy differs");
        Require(REX_LOAD_U64(device+16)==4096 && REX_LOAD_U32(device+13080)==copy &&
          REX_LOAD_U32(device+13088)==payload/4,"unowned GPU scratch/restore state missing");
        Require(REX_LOAD_U32(device+40)>cursor+(wrap?1024:0),"unowned tail emitted no packets");
      }
    }
    // Production orchestration: entry snapshots, store order and partial failure.
    for(bool initially_dirty:{false,true}) for(bool failure:{false,true}) {
      struct Memory {
        mutable std::array<uint64_t,2048> words{};
        mutable std::vector<uint32_t> writes;
        uint64_t Wide(uint32_t address) const { return words.at(address/8); }
        void StoreWide(uint32_t address,uint64_t value) const { writes.push_back(address); words.at(address/8)=value; }
      } memory;
      memory.words[0]=1; memory.words[1]=1; memory.words[2]=uint64_t(1)<<49;
      memory.words[3]=initially_dirty?2:0; memory.words[4]=initially_dirty?1:0;
      bool caught=false;
      try {
        edf::native::CompleteNativeIndexedState(memory,0,[&](uint64_t dirty) {
          Require(dirty==(uint64_t(1)<<49) && memory.writes==std::vector<uint32_t>{0,8},
            "indexed main-state call lost entry mask or ordered constant consumption");
          memory.words[3]=4; memory.words[4]=2;
          if(failure) throw std::runtime_error("injected main-state failure");
        });
      } catch(const std::runtime_error& error) {
        if(std::string(error.what())!="injected main-state failure") throw;
        caught=true;
      }
      Require(caught==failure,"indexed main-state failure swallowed");
      if(failure) {
        Require(memory.writes==std::vector<uint32_t>{0,8} && memory.words[2]==(uint64_t(1)<<49) &&
          memory.words[3]==4 && memory.words[4]==2,"indexed failure consumed unfinished state");
        edf::native::CompleteNativeIndexedState(memory,0,[](uint64_t) {});
        Require(memory.writes==std::vector<uint32_t>({0,8,16,24,32}),"indexed retry repeated completed constant work");
      } else {
        const auto expected=initially_dirty?std::vector<uint32_t>{0,8,16,11568,24,32}:std::vector<uint32_t>{0,8,16};
        Require(memory.writes==expected,"indexed completion store order changed");
        Require(memory.words[3]==(initially_dirty?0:4) && memory.words[4]==(initially_dirty?0:2),
          "indexed completion reread helper-modified banks instead of entry snapshots");
        Require(memory.words[11568/8]==(initially_dirty?0xffffffffff000000ull:0),"indexed render signature changed");
      }
    }
    // Every dirty-bank branch in the native prefixes must be packet-free even
    // without ownership scope, including the real mixed CPU-state helper chain.
    for(bool immediate:{false,true}) {
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      for(uint32_t at:{0,8,16,24,32}) REX_STORE_U64(device+at,UINT64_MAX);
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor-4);
      REX_STORE_U8(device+12256,7); REX_STORE_U8(device+10809,0x12);
      REX_STORE_U32(device+12164,0xfffffffcu);
      PrepareDrawCpuState(base);
      for(uint32_t at:{13076,13080,13088}) REX_STORE_U32(device+at,0x12345678);
      allocations=copies=constants=render=derived=rollovers=0;
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=4;
      if(immediate) {
        expected_stride=44;
        ctx.r5.u64=4; ctx.r6.u64=0xfffffffcu; ctx.r7.u64=44;
        __imp__edf_native_immediate_cpu_tail(ctx,base);
      } else {
        expected_stride=28;
        ctx.r5.u64=0; ctx.r6.u64=0; ctx.r7.u64=3;
        __imp__edf_native_indexed_cpu_tail(ctx,base);
      }
      Require(allocations==0 && copies==0 && constants==0 && render==0 && derived==0 && rollovers==0,
        "fully dirty native prefix reached retail packet work");
      for(uint32_t at:{0,8,16,24,32}) Require(REX_LOAD_U64(device+at)==0,"native prefix lost dirty-bank clear");
      Require(REX_LOAD_U8(device+12256)==7 && REX_LOAD_U8(device+10809)==0x12 &&
        REX_LOAD_U32(device+40)==cursor && ctx.r1.u64==0xf0000 && ctx.lr==0x12345678,
        "fully dirty native prefix changed stride/failure/cursor/ABI state");
      for(uint32_t at:{13076,13080,13088}) Require(REX_LOAD_U32(device+at)==0x12345678,"native prefix changed GPU scratch");
      for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native prefix wrote Xbox packet bytes");
    }
    // Compare the actual indexed routine and its extracted CPU-only prefix.
    // Compare device and shader/cache memory through the real CPU helper chain,
    // excluding only command bytes/cursor40 and the scratch stack area.
    for(uint32_t primitive:{1,2,4,13}) for(uint32_t count:{0,3,6,6000}) for(bool dirty:{false,true})
      for(bool index32:{false,true}) for(bool wrap:{false,true}) {
      auto original=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      auto* base=original->bytes.data();
      if(dirty) {
        for(uint32_t at:{0,8,32}) REX_STORE_U64(device+at,1);
        REX_STORE_U64(device+16,(uint64_t(1)<<51)|1);
        REX_STORE_U64(device+24,4);
      }
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,wrap?cursor-4:cursor+2048);
      REX_STORE_U32(device+12164,vertices);
      REX_STORE_U32(vertices,index32?0x80000000:0);
      REX_STORE_U32(vertices+24,copy);
      REX_STORE_U8(device+12256,7); expected_stride=28;
      PrepareDrawCpuState(base);
      native->bytes=original->bytes;
      auto run=[&](bool cpu_only,uint8_t* memory) {
        constants=render=derived=rollovers=0;
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r14.u64=14; ctx.r31.u64=31;
        ctx.r3.u64=device; ctx.r4.u64=primitive; ctx.r5.s64=-1; ctx.r6.u64=2; ctx.r7.u64=count;
        if(cpu_only) {
          // A failed guarded acquisition may refer to a retired index handle.
          // The CPU tail must not dereference it, even without native constant
          // ownership. Restore the binding solely for the device-state comparison.
          auto* base=memory;
          const auto saved_index=REX_LOAD_U32(device+12164);
          REX_STORE_U32(device+12164,0xfffffffcu);
          __imp__edf_native_indexed_cpu_tail(ctx,memory);
          Require(REX_LOAD_U32(device+12164)==0xfffffffcu,"CPU tail modified index binding");
          REX_STORE_U32(device+12164,saved_index);
        }
        else sub_821FE358(ctx,memory);
        Require(constants==(dirty && !cpu_only?2u:0u) && render==(dirty && !cpu_only?4u:0u) && derived==unsigned(dirty && !cpu_only) &&
          rollovers==unsigned(wrap && !cpu_only),"indexed CPU helper sequence differs");
        Require(ctx.r1.u64==0xf0000 && ctx.lr==0x12345678 && ctx.r14.u64==14 && ctx.r31.u64==31,
          "indexed CPU tail failed ABI restoration");
      };
      run(false,original->bytes.data()); run(true,native->bytes.data());
      for(uint32_t at=0;at<0xe0000;++at) {
        if((at>=device+40 && at<device+44) || (at>=cursor && at<cursor+2048)) continue;
        Require(original->bytes[at]==native->bytes[at],"indexed CPU/cache state differs from original");
      }
      Require(REX_LOAD_U32(device+40)>cursor,"original indexed routine emitted no packets");
      base=native->bytes.data();
      Require(REX_LOAD_U32(device+40)==cursor,"native indexed tail advanced GPU cursor");
      for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native indexed tail emitted draw packets");
    }
    // Every individual dirty bit against the original indexed routine, followed
    // by a second native completion. This includes bank24 bit1's CPU signature.
    for(uint32_t bank:{0,8,16,24,32}) for(unsigned bit=0;bit<64;++bit) {
      auto reference=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      auto* base=reference->bytes.data();
      REX_STORE_U8(device+12256,7); expected_stride=28;
      PrepareDrawCpuState(base);
      REX_STORE_U64(device+bank,uint64_t(1)<<bit);
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor+2048);
      REX_STORE_U32(device+12164,vertices); REX_STORE_U32(vertices+24,copy);
      *native=*reference;
      const auto context=[] {
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r3.u64=device; ctx.r4.u64=4; ctx.r7.u64=3; return ctx;
      };
      auto original=context();
      // Isolate audited packet encoders, including D938's documented mask
      // return. This comparison does not validate their packet byte encodings.
      indexed_bank_fixture=true;
      sub_821FE358(original,reference->bytes.data());
      indexed_bank_fixture=false;
      constants=render=derived=rollovers=allocations=copies=0; helper_trace=cache_trace=0;
      auto actual=context(); __imp__edf_native_indexed_cpu_tail(actual,native->bytes.data());
      Require(!(constants || render || derived || rollovers || allocations || copies || helper_trace || cache_trace),
        "native indexed completion reached a retail packet/helper dispatch");
      for(uint32_t at=0;at<0xe0000;++at) {
        if((at>=device+40 && at<device+44) || (at>=cursor && at<cursor+2048)) continue;
        Require(reference->bytes[at]==native->bytes[at],"single dirty-bit indexed CPU projection differs");
      }
      const auto completed=std::make_unique<Arena>(*native);
      actual=context(); __imp__edf_native_indexed_cpu_tail(actual,native->bytes.data());
      Require(!std::memcmp(completed->bytes.data(),native->bytes.data(),0xe0000),"repeated indexed completion changed CPU state");
      base=native->bytes.data();
      Require(REX_LOAD_U32(device+40)==cursor && actual.r1.u64==0xf0000 && actual.lr==0x12345678,
        "indexed completion changed cursor or caller ABI");
    }
    std::cout << "Native indexed completion: 320 single-bit/repetition comparisons and 4 snapshot/failure cases passed\n";
    // Execute production group orchestration, real CPU geometry/material/tail
    // helpers, and retained WARP output from the same guest-derived inputs.
    for(unsigned draws:{0u,1u,3u}) for(bool changed_material:{false,true})
      for(bool fallback:{false,true}) for(bool fail_tail:{false,true}) {
      using namespace edf::native;
      auto reference=std::make_unique<Arena>(),actual=std::make_unique<Arena>();
      auto* base=reference->bytes.data();
      REX_STORE_U8(device+12256,7); expected_stride=28;
      PrepareDrawCpuState(base);
      REX_STORE_U64(device,1);
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor+2048);
      REX_STORE_U32(device+12164,vertices); REX_STORE_U32(vertices+24,copy);
      *actual=*reference;
      NativeSceneGeometryHandoff geometry;
      NativeSceneGeometryInstallState geometry_progress;
      NativeSceneMaterialHandoff material;
      NativeSceneGeometrySource setup{}; setup.declaration=0x65000;
      setup.vertex=vertices; setup.index=vertices+128; setup.stride=28; setup.count=6;
      REX_STORE_U32(vertices+28,0x1000); REX_STORE_U32(setup.index+24,copy);
      REX_STORE_U32(device+12188,fallback?vertices+256:0);
      constexpr uint32_t material_instance=0x80000,pass=0x81000,pixel_shader=0x68000;
      setup.material=material_instance; setup.shader=0x60000;
      REX_STORE_U32(material_instance+108,pass);
      REX_STORE_U32(pass,pass+32); REX_STORE_U32(pass+4,pass+64);
      REX_STORE_U32(pass+32,0x60000); REX_STORE_U32(pass+68,pixel_shader);
      REX_STORE_U32(pixel_shader+64,128);
      unsigned block=0;
      for(uint32_t offset:{0,24,36,60}) {
        const uint32_t record=0x82000+block*32,data=0x84000+block*32,indirect=0x83000+block*4;
        const bool global=offset==24 || offset==60;
        const uint32_t first=global?4:0;
        REX_STORE_U32(material_instance+offset,record);
        REX_STORE_U32(material_instance+offset+8,1);
        REX_STORE_U32(indirect,data);
        REX_STORE_U32(record,global?indirect:first);
        REX_STORE_U32(record+4,data);
        REX_STORE_U32(record+8,1);
        REX_STORE_U32(record+12,global?first:1);
        for(unsigned word=0;word<4;++word) REX_STORE_U32(data+word*4,0x3f000000+block*16+word);
        ++block;
      }
      // One declared opaque fixture: position-only quad, no textures or explicit
      // state lists, disjoint constants, fence retirement, identity world. The
      // pixel constant changes red -> blue; its oracle lives in the GPU helper.
      constexpr uint32_t gpu_vertices=0x90000,gpu_indices=0x91000;
      REX_STORE_U32(device+10424,0x10001); REX_STORE_U32(device+10332,15);
      REX_STORE_U32(vertices+24,gpu_vertices);
      REX_STORE_U32(setup.index+24,gpu_indices);
      REX_STORE_U32(setup.declaration,0); REX_STORE_U32(setup.declaration+4,0x2a23b9);
      REX_STORE_U32(setup.declaration+8,0);
      const float points[]{-.5f,-.5f,.5f, -.5f,.5f,.5f, .5f,.5f,.5f, .5f,-.5f,.5f};
      for(unsigned vertex=0;vertex<4;++vertex) for(unsigned component=0;component<3;++component)
        REX_STORE_U32(gpu_vertices+vertex*setup.stride+component*4,std::bit_cast<uint32_t>(points[vertex*3+component]));
      const uint16_t indices[]{0,1,2,0,2,3};
      for(unsigned i=0;i<6;++i) REX_STORE_U16(gpu_indices+i*2,indices[i]);
      for(unsigned word=0;word<4;++word) {
        REX_STORE_U32(device+368*16+word*4,(word==0 || word==3)?0x3f800000:0);
        REX_STORE_U32(0x84040+word*4,(word==2 || word==3)?0x3f800000:0);
      }
      constexpr uint32_t fallback_instance=0x92000,fallback_record=0x93000,fallback_data=0x94000;
      REX_STORE_U32(fallback_instance+16,fallback_record);
      REX_STORE_U32(fallback_instance+20,fallback_record+12);
      REX_STORE_U32(fallback_record,fallback_data);
      REX_STORE_U32(fallback_record+4,8); REX_STORE_U32(fallback_record+8,1);
      for(unsigned word=0;word<4;++word) REX_STORE_U32(fallback_data+word*4,0x42280000+word);
      std::array<uint8_t,64> world_bytes{};
      for(unsigned word:{0u,5u,10u,15u}) { world_bytes[word*4]=0x3f; world_bytes[word*4+1]=0x80; }
      *actual=*reference;
      NativeStaticGroupGpuInputs gpu;
      {
        const GeometryFixtureReader reader{actual->bytes.data()};
        Require(AssessNativeStaticGroup(reader,device,0xf0000,setup)==NativeStaticGroupEligibility::Supported,
          "integrated static group no longer satisfies production eligibility");
        const auto capture=[&](uint32_t address,size_t bytes) {
          const auto* begin=reader.Bytes(address,bytes); return std::vector<uint8_t>(begin,begin+bytes);
        };
        gpu.declaration=capture(setup.declaration,12);
        gpu.vertices=capture(reader.Word(setup.vertex+24),setup.stride*4);
        gpu.indices=capture(reader.Word(setup.index+24),12); gpu.stride=setup.stride;
        const std::array<uint32_t,6> state_offsets{10424,10420,10440,10428,10332,11584};
        for(size_t i=0;i<state_offsets.size();++i) gpu.state[i]=reader.Word(device+state_offsets[i]);
        uint32_t tint=device+368*16;
        if(changed_material) {
          const auto program=ReadNativeMaterialCpuProgram(reader,material_instance,device);
          const auto operation=std::find_if(program.constants.begin(),program.constants.end(),
            [](const auto& value) { return value.pixel && value.first==0; });
          Require(operation!=program.constants.end(),"group publication lost pixel constant input");
          tint=operation->data;
        }
        for(unsigned word=0;word<4;++word) gpu.tint[word]=std::bit_cast<float>(reader.Word(tint+word*4));
        for(unsigned word=0;word<16;++word) {
          uint32_t bits=0; for(unsigned byte=0;byte<4;++byte) bits=(bits<<8)|world_bytes[word*4+byte];
          gpu.world[word]=std::bit_cast<float>(bits);
        }
      }
      geometry.Defer(setup);
      if(changed_material) material.Defer();
      for(unsigned i=0;i<draws;++i) geometry.DrawAccepted();
      std::vector<int> trace;
      unsigned installations=0,activations=0,tails=0,submissions=0;
      bool clean=true,throw_once=fail_tail && draws,gpu_submitted=false;
      std::unique_ptr<Arena> before_tail;
      const auto context=[] {
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r3.u64=device; ctx.r4.u64=4; ctx.r7.u64=6; return ctx;
      };
      const auto install=[&](uint8_t* memory,bool native) {
        if(!native) {
          auto ctx=context(); ctx.r4.u64=0; ctx.r5.u64=setup.vertex;
          ctx.r6.u64=0; ctx.r7.u64=setup.stride; ctx.r8.u64=4096;
          __imp__sub_82137410(ctx,memory);
          ctx.r3.u64=device; ctx.r4.u64=setup.declaration;
          __imp__sub_82149A90(ctx,memory);
          ctx.r3.u64=device; ctx.r4.u64=setup.index;
          __imp__sub_821375C0(ctx,memory);
          return;
        }
        const GeometryFixtureReader reader{memory};
        std::vector<NativeGeometryBinding> published;
        InstallNativeSceneGeometry(reader,device,setup,
          [](bool)->uint32_t { throw std::runtime_error("unexpected group geometry allocator"); },
          [](bool)->uint32_t { throw std::runtime_error("unexpected group geometry legacy tag"); },
          [](bool,auto,auto,auto,auto) {},[&](auto binding) {
            if(binding==NativeGeometryBinding::Stream)
              Require(reader.Word(device+12188)==setup.vertex,"stream publication preceded CPU binding");
            if(binding==NativeGeometryBinding::Declaration)
              Require(reader.Word(device+11536)==setup.declaration,"declaration publication preceded CPU binding");
            if(binding==NativeGeometryBinding::Index)
              Require(reader.Word(device+12164)==setup.index,"index publication preceded CPU binding");
            published.push_back(binding);
          },&geometry_progress);
        Require(published==std::vector<NativeGeometryBinding>{NativeGeometryBinding::Stream,
          NativeGeometryBinding::Declaration,NativeGeometryBinding::Index},"group geometry publication order");
      };
      // Reference is a direct full-matrix copy. Native uses the exact production
      // restoration helper; publication observes all 64 restored bytes.
      const auto world=[&](uint8_t* memory,bool native) {
        if(!native) { std::memcpy(memory+device+1792,world_bytes.data(),64); return; }
        RestoreNativeSceneWorld(GeometryFixtureReader{memory},device,0,world_bytes,[&](const auto& bytes) {
          Require(!std::memcmp(memory+device+1792,bytes.data(),64),"world mirror published before CPU restoration");
        });
      };
      const auto activate=[&](uint8_t* base,bool native) {
        if(!native) {
          auto ctx=context(); ctx.r3.u64=material_instance; ctx.r4.u64=device;
          material_reference=true; __imp__sub_821B8E48(ctx,base); material_reference=false;
          return;
        }
        const GeometryFixtureReader reader{base};
        const auto program=ReadNativeMaterialCpuProgram(reader,material_instance,device);
        ExecuteNativeMaterialCpuProgram(program,[&](uint32_t shader,bool pixel) {
          SetNativeShaderResource(reader,device,shader,pixel,
            []()->uint32_t { throw std::runtime_error("native material allocation not expected"); },
            []()->uint32_t { throw std::runtime_error("native material retirement tag not expected"); });
        },[&](const auto& operation) {
          const auto first=operation.first/4,last=(operation.first+operation.count-1)/4;
          const auto mask=(UINT64_MAX>>first)&(UINT64_MAX<<(63-last));
          Require(UploadNativeMaterialConstant(reader,device,operation,mask),"supported material required legacy constant setter");
        },[](const auto&) { throw std::runtime_error("unconfigured native material texture"); },
          [](const auto&) { throw std::runtime_error("unconfigured native material state"); });
      };
      world(base,false); install(base,false);
      if(changed_material) { activate(base,false); world(base,false); }
      if(draws) {
        auto ctx=context(); indexed_bank_fixture=true;
        sub_821FE358(ctx,base); indexed_bank_fixture=false;
      }
      const auto finish=[&] {
        auto* base=actual->bytes.data();
        FinishNativeSceneGroupHandoff(geometry,
          [&] { trace.push_back(1); world(base,true); },
          [&] {
            trace.push_back(2);
            if(!gpu_submitted) { VerifyNativeStaticGroupGpu(gpu,draws,changed_material); gpu_submitted=true; submissions=draws; }
          },
          [&](const auto& value) { Require(value==setup,"group setup changed"); trace.push_back(3); ++installations; install(base,true); },
          [&] { material.Finish([&] { trace.push_back(4); ++activations; activate(base,true); },
                                [&] { trace.push_back(5); world(base,true); }); },
          [&] {
            trace.push_back(6);
            if(throw_once) { throw_once=false; throw std::runtime_error("injected tail failure"); }
            before_tail=std::make_unique<Arena>(*actual);
            ++tails; auto ctx=context(); __imp__edf_native_indexed_cpu_tail(ctx,base);
          },[&] { trace.push_back(7); clean=false; });
      };
      fixture_scene_execution=NativeSceneExecution{};
      if(throw_once) {
        bool caught=false;
        try { finish(); } catch(const std::runtime_error&) { caught=true; }
        Require(caught && geometry.pending() && clean,"tail failure lost pending group");
      }
      finish(); finish();
      Require(fixture_scene_execution.Total()==0,"native group differential entered a compatibility binding");
      if(fallback) trace.push_back(8); // The real compatibility draw below follows this boundary.
      std::vector<int> expected{1,2,3};
      if(changed_material) { expected.push_back(4); expected.push_back(5); }
      if(draws) {
        expected.push_back(6);
        if(fail_tail) expected.insert(expected.end(),{1,2,6});
      }
      expected.push_back(7); if(fallback) expected.push_back(8);
      Require(trace==expected,"production group handoff order changed");
      auto wrong_order=expected; std::swap(wrong_order[1],wrong_order[2]);
      Require(trace!=wrong_order,"ordering negative control was not detected");
      Require(installations==1 && activations==unsigned(changed_material) &&
        tails==unsigned(draws!=0) && submissions==draws && !clean && !geometry.pending() && !material.pending(),
        "group repeated or omitted owed work");
      const auto same_projection=[&] {
        for(uint32_t at=0;at<0xe0000;++at) {
          if((at>=device+40 && at<device+44) || (at>=cursor && at<cursor+2048)) continue;
          if(reference->bytes[at]!=actual->bytes[at]) return false;
        }
        return true;
      };
      Require(same_projection(),"group CPU projection differs from original indexed execution");
      actual->bytes[device+1792]^=1;
      Require(!same_projection(),"CPU word negative control was not detected");
      actual->bytes[device+1792]^=1;
      base=actual->bytes.data();
      const uint64_t owed_constants=1|(changed_material?(uint64_t(3)<<62):0);
      Require(REX_LOAD_U64(device)==(draws?0:owed_constants),"no-draw group consumed dirty state");
      // Minimal cutover blocker: retained submission alone does not own these
      // CPU effects. Compare the actual state immediately before consumption.
      if(draws) {
        Require(before_tail && before_tail->bytes!=actual->bytes,"indexed chain had no observable CPU effects");
        if(draws==1 && !changed_material && !fallback && !fail_tail) {
          std::cout << "Static group indexed CPU writes (4-byte addresses, excluding packets/stack):";
          for(uint32_t at=0;at<0xe0000;at+=4) {
            if((at>=device+40 && at<device+44) || (at>=cursor && at<cursor+2048)) continue;
            if(std::memcmp(before_tail->bytes.data()+at,actual->bytes.data()+at,4))
              std::cout << " 0x" << std::hex << at << std::dec;
          }
          std::cout << '\n';
        }
        REX_STORE_U64(device,1);
        Require(!same_projection(),"omitting indexed dirty-state completion was not detected");
        REX_STORE_U64(device,0);
      }
      Require(REX_LOAD_U32(device+40)==cursor,"group handoff emitted packets");
      if(fallback) {
        // The unsupported draw really executes the original instance upload and
        // indexed consumer, before/after 0/1/3 accepted native draws. Compare its
        // CPU effects against an independent original sequence after handoff.
        const auto fallback_draw=[&](uint8_t* memory,bool counted) {
          if(counted) fixture_scene_execution.Enter(NativeSceneBoundary::InstanceSetup);
          auto ctx=context(); ctx.r3.u64=fallback_instance; ctx.r4.u64=device;
          material_reference=true; __imp__sub_821D9600(ctx,memory); material_reference=false;
          if(counted) fixture_scene_execution.Enter(NativeSceneBoundary::IndexedDraw);
          ctx=context(); indexed_bank_fixture=true; sub_821FE358(ctx,memory); indexed_bank_fixture=false;
        };
        fallback_draw(reference->bytes.data(),false); fallback_draw(actual->bytes.data(),true);
        Require(same_projection(),"compatibility draw after native group changed CPU effects");
        Require(fixture_scene_execution.Calls(NativeSceneBoundary::InstanceSetup)==1 &&
          fixture_scene_execution.Calls(NativeSceneBoundary::IndexedDraw)==1,
          "unsupported group draw was not counted");
      }
    }
    FinishNativeStaticGroupGpu();
    std::cout << "Static group handoff: 24 cases; ordering/CPU/omitted-tail/pixel controls detected\n";
    real_state_packets=true;
    struct Bank { uint32_t number,offset,words; };
    constexpr Bank banks[]{{18688,9984,40},{8192,10240,16},{8448,10316,21},{8576,10400,5},
      {8704,10420,12},{8832,10468,21},{8960,10552,38},{9088,10704,8}};
    for(const auto& bank:banks) for(bool owned:{false,true}) {
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+44,cursor+2048);
      for(uint32_t i=0;i<bank.words;++i) REX_STORE_U32(device+bank.offset+i*4,0xabcdef00+i);
      const auto before=std::make_unique<Arena>(*arena);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=UINT64_MAX<<(64-bank.words);
      ctx.r5.u64=bank.number; ctx.r6.u64=device+bank.offset;
      { Ownership scope(owned?device:0); sub_8213DB60(ctx,base); }
      Require(ctx.r1.u64==0xf0000 && ctx.lr==0x12345678,"state encoder ABI changed");
      for(uint32_t at=0;at<13520;++at) if(at<40 || at>=44)
        Require(base[device+at]==before->bytes[device+at],"state encoder modified CPU source state");
      if(owned) {
        Require(REX_LOAD_U32(device+40)==cursor,"native state encoder advanced cursor");
        for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native state encoder wrote packet bytes");
      } else {
        Require(REX_LOAD_U32(device+40)==cursor+4*(bank.words+1),"original state packet extent");
        Require(REX_LOAD_U32(cursor+4)==((bank.words-1)<<16|bank.number),"original state packet header");
        for(uint32_t i=0;i<bank.words;++i)
          Require(REX_LOAD_U32(cursor+8+i*4)==0xabcdef00+i,"original state packet payload");
      }
    }
    real_vector_packets=true;
    for(uint64_t mask:{0x8000000000000000ull,0x0400000000000000ull,0xa800000000000000ull,0xfc00000000000000ull})
      for(bool owned:{false,true}) {
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+44,cursor+2048);
      REX_STORE_U32(device+48,cursor+2048); REX_STORE_U32(device+10268,0x1234);
      for(uint32_t at=0;at<96;at+=4) REX_STORE_U32(device+10144+at,0x3f800000+at);
      const auto before=std::make_unique<Arena>(*arena);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=mask;
      { Ownership scope(owned?device:0); sub_8213DC20(ctx,base); }
      Require(ctx.r1.u64==0xf0000 && ctx.lr==0x12345678,"vector-state encoder ABI");
      for(uint32_t at=0;at<13520;++at) if(at<40 || at>=44)
        Require(base[device+at]==before->bytes[device+at],"vector-state encoder modified CPU source");
      if(owned) {
        Require(REX_LOAD_U32(device+40)==cursor,"native vector-state cursor changed");
        for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native vector-state emitted packets");
      } else Require(REX_LOAD_U32(device+40)>cursor && REX_LOAD_U32(cursor+4)==8199 &&
        REX_LOAD_U32(cursor+8)==0x1234,"original vector-state packet preamble missing");
    }
    real_special_packets=true;
    for(uint32_t variant:{0,1,2,3,4}) for(bool owned:{false,true}) for(uint32_t mode:{0,1}) {
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor+2048);
      REX_STORE_U8(device+10811,variant?0x20:0);
      REX_STORE_U8(device+10808,variant==1?0x10:variant==2?0x50:0x20);
      if(variant==4) REX_STORE_U32(device+12168,1); // Mismatched target selects simple path.
      REX_STORE_U32(device+12472,2);
      REX_STORE_U32(device+12836,7); REX_STORE_U32(device+12840,11);
      const auto before=std::make_unique<Arena>(*arena);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x821fe424;
      constexpr uint64_t dirty=0xabcdef123456ffffull;
      ctx.r3.u64=device; ctx.r4.u64=dirty; ctx.r5.u64=0x12345; ctx.r6.u64=mode;
      { Ownership scope(owned?device:0); sub_8213D938(ctx,base); }
      Require(ctx.r1.u64==0xf0000 && ctx.lr==0x821fe424 && ctx.r3.u64==(dirty&~uint64_t(0x100)),
        "special render ABI/dirty-mask return differs");
      for(uint32_t at=0;at<13520;++at) if(at<40 || at>=44)
        Require(base[device+at]==before->bytes[device+at],"special render modified CPU source state");
      if(owned && mode==0) {
        Require(REX_LOAD_U32(device+40)==cursor,"native special render advanced packet cursor");
        for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native special render emitted packets");
      } else Require(REX_LOAD_U32(device+40)>cursor,"original special render emitted no packets");
    }
    for(uint32_t variant:{0,1,2,3,4}) for(bool owned:{false,true})
      for(uint32_t caller:{0x8213edc8u,0x8213ef3cu,0x8213f04cu,0x12345678u})
      for(uint32_t address_base:{0x70000u,0xe0070000u}) {
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      constexpr uint32_t program=0x60000;
      const uint32_t records=variant<2?0:variant==2?1:3;
      const uint32_t emitted=variant==4?1:records;
      const bool omitted=owned && caller!=0x12345678u;
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor+2048);
      REX_STORE_U32(device+12440,0x80);
      REX_STORE_U32(program+20,variant?32:0);
      REX_STORE_U32(program+32+24,records*8);
      for(uint32_t i=0;i<records;++i) {
        REX_STORE_U16(program+64+i*8,uint16_t(4+i));
        REX_STORE_U16(program+66+i*8,uint16_t(variant==4 && i==1?0:8+i));
        REX_STORE_U32(program+68+i*8,16+i*32);
      }
      const auto before=std::make_unique<Arena>(*arena);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=caller;
      ctx.r25.u64=25; ctx.r26.u64=26; ctx.r27.u64=27; ctx.r28.u64=28;
      ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
      ctx.r3.u64=device; ctx.r4.u64=program; ctx.r5.u64=address_base;
      { Ownership scope(owned?device:0); sub_8213EAB0(ctx,base); }
      Require(ctx.r1.u64==0xf0000 && ctx.lr==caller && ctx.r25.u64==25 &&
        ctx.r26.u64==26 && ctx.r27.u64==27 && ctx.r28.u64==28 &&
        ctx.r29.u64==29 && ctx.r30.u64==30 && ctx.r31.u64==31,"shader-load ABI changed");
      for(uint32_t at=0;at<13520;++at) if(at<40 || at>=44)
        Require(base[device+at]==before->bytes[device+at],"shader-load changed CPU device state");
      Require(std::memcmp(base+program,before->bytes.data()+program,128)==0,"shader-load changed relocation table");
      Require(REX_LOAD_U32(device+40)==cursor+(omitted?0:emitted*16),"shader-load packet extent differs");
      if(omitted || !emitted) {
        for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native/empty shader load emitted packets");
      } else for(uint32_t i=0;i<emitted;++i) {
        const uint32_t relocated=(address_base==0x70000u?0x70000u:0x71000u)+16+i*32;
        Require(REX_LOAD_U32(cursor+4+i*16)==0xc0022f80 &&
          REX_LOAD_U32(cursor+8+i*16)==relocated &&
          REX_LOAD_U32(cursor+12+i*16)==(4+i)*4 && REX_LOAD_U32(cursor+16+i*16)==8+i,
          "original shader-load packet header/register/count");
      }
    }
    {
      ShaderPatchArena arena;
      auto* base=arena.bytes;
      // Synthetic fixed tables exercise the actual instruction-rewrite path;
      // they are not a claim of equivalence to retail shader machine code.
      std::memset(base+0x82009964,0xff,16);
      for(uint32_t records:{0,1,3}) for(bool declared:{false,true})
        for(uint32_t variant:{0,1}) for(bool owned:{false,true})
        for(uint32_t caller:{0x8213ec30u,0x8213ea8cu,0x12345678u}) {
        std::memset(base,0,1024*1024);
        constexpr uint32_t program=0x60000,decl=0x65000,code=0x70000,header=program+0x1000+872;
        REX_STORE_U32(program+896+variant*8,0x1000);
        REX_STORE_U32(header+24,0); REX_STORE_U32(header+28,records);
        REX_STORE_U32(decl+24,declared?1:0);
        REX_STORE_U8(device+12256,7);
        for(uint32_t i=0;i<records;++i) {
          REX_STORE_U32(header+36+i*4,i);
          REX_STORE_U32(program+variant*416+68+i*12,0x80000000);
        }
        std::memset(base+code,0xcd,128);
        auto before=std::make_unique<Arena>(); std::memcpy(before->bytes.data(),base,1024*1024);
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=caller;
        ctx.r20.u64=20; ctx.r21.u64=21; ctx.r22.u64=22; ctx.r23.u64=23;
        ctx.r24.u64=24; ctx.r25.u64=25; ctx.r26.u64=26; ctx.r27.u64=27;
        ctx.r28.u64=28; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
        ctx.r3.u64=program; ctx.r4.u64=code; ctx.r5.u64=decl;
        ctx.r6.u64=device+12256; ctx.r7.u64=variant; copies=0;
        { Ownership scope(owned?device:0); sub_8213E070(ctx,base); }
        const bool omitted=owned && caller!=0x12345678u;
        Require(copies==(omitted?0:records),"shader patch copy count differs");
        Require(ctx.r1.u64==0xf0000 && ctx.lr==caller &&
          ctx.r20.u64==20 && ctx.r21.u64==21 && ctx.r22.u64==22 && ctx.r23.u64==23 &&
          ctx.r24.u64==24 && ctx.r25.u64==25 && ctx.r26.u64==26 && ctx.r27.u64==27 &&
          ctx.r28.u64==28 && ctx.r29.u64==29 && ctx.r30.u64==30 && ctx.r31.u64==31,
          "shader patch ABI changed");
        Require(std::memcmp(base,before->bytes.data(),code)==0 &&
          std::memcmp(base+code+128,before->bytes.data()+code+128,0xe0000-code-128)==0,
          "shader patch changed non-code CPU state");
        const bool unchanged=std::memcmp(base+code,before->bytes.data()+code,128)==0;
        Require(unchanged==(omitted || !records),"shader patch destination ownership differs");
      }
    }
    output_skips=output_patches=0;
    for(uint32_t records:{0,1,3}) for(uint32_t scenario=0;scenario<8;++scenario) {
      auto original=std::make_unique<Arena>(); auto* base=original->bytes.data();
      constexpr uint32_t header=0x60000,cpu_output=0x65000,other=0x66000,code=0x70000;
      const bool has_other=scenario!=0;
      const bool early=scenario>=5;
      const uint32_t other_count=records && scenario!=4?1:0;
      REX_STORE_U32(header+20,(records<<5)|(scenario==6?0x40000:0));
      REX_STORE_U8(header+8,scenario==5?7:0);
      REX_STORE_U32(other+20,(other_count<<5)|3|(scenario==7?0x20000:0));
      for(uint32_t i=0;i<records;++i) {
        REX_STORE_U32(header+36+i*4,(i<<16)|0x100|(1+i*2));
        REX_STORE_U32(header+(records+9)*4+i*4,0x1000|i);
      }
      REX_STORE_U32(other+32,(scenario==2?0x100:0x200)|(scenario==3 && records>1?3:1));
      REX_STORE_U32(cpu_output,0xdeadbeef);
      std::memset(base+code,0xcd,128);
      auto native=std::make_unique<Arena>(*original);
      PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x8213ea50;
      expected.r3.u64=0x80000; expected.r4.u64=header; expected.r5.u64=code;
      expected.r6.u64=cpu_output; expected.r7.u64=has_other?other:0;
      expected.r22.u64=22; expected.r23.u64=23; expected.r24.u64=24; expected.r25.u64=25;
      expected.r26.u64=26; expected.r27.u64=27; expected.r28.u64=28;
      expected.r29.u64=device; expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected;
      __imp__sub_8213E800(expected,base);
      const auto original_calls=output_skips+output_patches;
      __imp__edf_native_shader_output_cpu_tail(actual,native->bytes.data());
      Require(original_calls==output_skips+output_patches,"native shader-output tail patched microcode");
      Require(std::memcmp(original->bytes.data(),native->bytes.data(),code)==0 &&
        std::memcmp(original->bytes.data()+code+128,native->bytes.data()+code+128,0xe0000-code-128)==0,
        "shader-output CPU data differs");
      const uint32_t output=early?0xdeadbeef:(0xdeadbeef&~0xf00000u)|(has_other?0x200000:0);
      Require(REX_LOAD_U32(cpu_output)==output,"shader-output CPU result differs");
      Require(actual.r1.u64==0xf0000 && actual.lr==0x8213ea50 &&
        actual.r22.u64==22 && actual.r23.u64==23 && actual.r24.u64==24 && actual.r25.u64==25 &&
        actual.r26.u64==26 && actual.r27.u64==27 && actual.r28.u64==28 &&
        actual.r29.u64==device && actual.r30.u64==30 && actual.r31.u64==31,
        "shader-output native ABI changed");
      for(uint32_t at=0;at<128;++at) Require(native->bytes[code+at]==0xcd,"native shader output rewrote instruction bytes");
    }
    Require(output_skips && output_patches,"shader-output fixture missed a microcode helper");
    {
      ShaderPatchArena arena;
      for(uint32_t bytes:{12,24,48}) for(uint32_t mode:{0,1,3})
        for(uint32_t variant:{0,1}) for(uint32_t flags:{0x35,0xb5}) for(bool has_other:{false,true}) {
        auto* base=arena.bytes; std::memset(base,0,1024*1024);
        constexpr uint32_t program=0x60000,metadata=program+0x1000+872;
        constexpr uint32_t header=0x63000,decl=0x64000,cpu_output=0x65000,other=0x66000;
        REX_STORE_U32(device+40,cursor); REX_STORE_U8(device+10810,uint8_t(flags));
        REX_STORE_U64(device+12256,0x1122334455667788ull);
        REX_STORE_U64(device+12264,0x8877665544332211ull);
        REX_STORE_U32(program+896+variant*8,0x1000);
        REX_STORE_U32(program+28,0x70000); REX_STORE_U32(metadata+4,bytes);
        REX_STORE_U32(header+20,32); REX_STORE_U32(header+36,0x101);
        REX_STORE_U32(header+40,0x1000); REX_STORE_U32(other+20,35);
        REX_STORE_U32(other+32,0x201); REX_STORE_U32(cpu_output,0xdeadbeef);
        std::memset(base+0xc0070000,0xcd,4096);
        auto native=std::make_unique<Arena>(); std::memcpy(native->bytes.data(),base,1024*1024);
        PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x8213f070;
        expected.r3.u64=device; expected.r4.u64=mode; expected.r5.u64=program;
        expected.r6.u64=cpu_output; expected.r7.u64=decl; expected.r8.u64=header;
        expected.r9.u64=has_other?other:0; expected.r10.u64=variant;
        expected.r19.u64=19; expected.r20.u64=20; expected.r21.u64=21; expected.r22.u64=22;
        expected.r23.u64=23; expected.r24.u64=24; expected.r25.u64=25; expected.r26.u64=26;
        expected.r27.u64=27; expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
        auto actual=expected; allocations=copies=0;
        __imp__sub_8213E950(expected,base);
        Require(allocations==1 && copies>0,"original shader upload did not allocate/copy");
        Require(REX_LOAD_U32(device+40)==cursor+20+bytes && REX_LOAD_U32(cursor+4)==0xc0003b00,
          "original shader upload packet extent/header differs");
        allocations=copies=0;
        __imp__edf_native_shader_upload_cpu_tail(actual,native->bytes.data());
        Require(!allocations && !copies,"native shader upload allocated/copied guest shader bytes");
        for(uint32_t at=0;at<0xe0000;++at) {
          if((at>=device+40 && at<device+44) || (at>=cursor && at<cursor+2048)) continue;
          Require(base[at]==native->bytes[at],"shader upload CPU metadata differs");
        }
        Require(REX_LOAD_U8(device+10810)==((flags&0x7f)|(mode?0x80:0)) &&
          REX_LOAD_U64(device+11552)==0x1122334455667788ull &&
          REX_LOAD_U64(device+11560)==0x8877665544332211ull,"shader upload CPU flags/stride snapshots differ");
        Require(actual.r1.u64==0xf0000 && actual.lr==0x8213f070 && actual.r19.u64==19 &&
          actual.r20.u64==20 && actual.r21.u64==21 && actual.r22.u64==22 && actual.r23.u64==23 &&
          actual.r24.u64==24 && actual.r25.u64==25 && actual.r26.u64==26 && actual.r27.u64==27 &&
          actual.r28.u64==28 && actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,
          "native shader upload ABI changed");
        for(uint32_t at=0;at<bytes;++at) Require(base[0xc0070000+at]==0xcd,"shader upload modified source microcode");
        base=native->bytes.data();
        Require(REX_LOAD_U32(device+40)==cursor,"native shader upload advanced command cursor");
        for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native shader upload emitted packets");
      }
    }
    // ID allocation is a CPU side effect even when no Xbox shader is patched.
    for(uint32_t initial:{0u,7u,0xfffffffeu,0xffffffffu}) for(uint32_t variant:{0u,1u}) {
      ShaderPatchArena original,native;
      auto* base=original.bytes;
      constexpr uint32_t program=0x60000,decl=0x65000,id_counter=0x82578d0c;
      const auto record=program+variant*416;
      REX_STORE_U32(decl+48,0);
      REX_STORE_U32(record+40,1); // Force refresh and the zero-ID allocation path.
      REX_STORE_U32(program+28,0xfffffff0u); // No readable Xbox code destination.
      REX_STORE_U32(program+896+variant*8,0x1000);
      REX_STORE_U32(program+0x1000+872,0);
      REX_STORE_U32(device+10780,200);
      REX_STORE_U64(device+12256,0x0102030405060708ull);
      REX_STORE_U64(device+12264,0x1112131415161718ull);
      REX_STORE_U32(id_counter,initial);
      std::memcpy(native.bytes,original.bytes,1024*1024);
      std::memcpy(native.bytes+id_counter,original.bytes+id_counter,4);
      PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x12345678;
      expected.r3.u64=device; expected.r4.u64=program; expected.r5.u64=decl; expected.r6.u64=variant;
      expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected;
      { Ownership scope(device); __imp__sub_8213EB68(expected,original.bytes); }
      __imp__edf_native_shader_cache_cpu_tail(actual,native.bytes); // No ownership hook scope.
      Require(!std::memcmp(original.bytes,native.bytes,0xe0000) &&
        !std::memcmp(original.bytes+id_counter,native.bytes+id_counter,4),
        "native shader cache changed ID allocation or CPU metadata");
      Require(actual.r3.u64==1 && actual.r1.u64==0xf0000 && actual.lr==0x12345678 &&
        actual.r28.u64==28 && actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,
        "native shader cache failed return/ABI restoration");
      const auto allocated=REX_LOAD_U32(decl+48);
      Require(allocated==(initial==7?8u:1u) && REX_LOAD_U32(id_counter)==allocated,
        "shader declaration ID allocation failed zero/wraparound handling");
    }
    // The authored native cache implementation shares metadata with fallback.
    // Exercise modular fences, masked signatures, repetition and address reuse;
    // changing either the fence tag or either signature must fail the oracle.
    for(uint32_t variant:{0u,1u}) for(unsigned mode=0;mode<8;++mode) {
      auto reference=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      constexpr uint32_t program=0x60000,decl=0x65000,completed_address=0x67000;
      const auto record=program+variant*416;
      const auto initialize=[&](uint8_t* base,bool reused) {
        std::memset(base,0,0xe0000);
        PrepareDrawCpuState(base);
        REX_STORE_U32(decl+48,reused?9:1);
        REX_STORE_U32(record+40,(!reused && mode<3)?1:2);
        REX_STORE_U64(device+12256,reused?0x12345678abcdef01ull:0x0102030405060708ull);
        REX_STORE_U64(device+12264,0x1112131415161718ull);
        REX_STORE_U64(record+48,REX_LOAD_U64(device+12256)^(mode==1 || mode==2?0x100000000ull:0));
        REX_STORE_U64(record+56,REX_LOAD_U64(device+12264)^(mode==1?0x8000000000000000ull:0));
        REX_STORE_U64(decl+32,mode==1?0:UINT64_MAX);
        REX_STORE_U64(decl+40,mode==1?0:UINT64_MAX);
        const uint32_t current=mode==5 || mode==6?5:200;
        const uint32_t completed=mode==5 || mode==6?0xfffffffeu:100;
        const uint32_t used=reused?0:mode==3?150:mode==4?100:mode==5?2:mode==6?0xfffffffeu:0;
        REX_STORE_U32(record+64,used);
        REX_STORE_U32(device+10780,current);
        REX_STORE_U32(device+10768,completed_address);
        REX_STORE_U32(completed_address,completed);
      };
      initialize(reference->bytes.data(),false); initialize(native->bytes.data(),false);
      for(unsigned step=0;step<4;++step) {
        if(step==3) { // Retired metadata reinitialized at the same guest address.
          initialize(reference->bytes.data(),true); initialize(native->bytes.data(),true);
        }
        PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x12345678;
        expected.r3.u64=device; expected.r4.u64=program; expected.r5.u64=decl; expected.r6.u64=variant;
        expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
        auto actual=expected;
        const auto before=std::make_unique<Arena>(*native);
        { Ownership scope(device); __imp__sub_8213EB68(expected,reference->bytes.data()); }
        if(step==2) { Ownership scope(device); __imp__sub_8213EB68(actual,native->bytes.data()); }
        else __imp__edf_native_shader_cache_cpu_tail(actual,native->bytes.data());
        const auto matches=[&] { return !std::memcmp(reference->bytes.data(),native->bytes.data(),0xe0000); };
        Require(matches() && expected.r3.u64==actual.r3.u64,"native cache lifecycle CPU projection differs");
        Require(actual.r1.u64==0xf0000 && actual.lr==0x12345678 && actual.r28.u64==28 &&
          actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,"native cache lifecycle ABI differs");
        const bool busy=(mode==3 || mode==5) && step!=3;
        Require(actual.r3.u64==unsigned(!busy),"cache fence boundary result differs");
        if(busy) Require(!std::memcmp(before->bytes.data(),native->bytes.data(),0xe0000),"busy cache changed CPU metadata");
        for(uint32_t word:{record+64,device+11552,device+11560}) {
          native->bytes[word]^=1;
          Require(!matches(),"cache tag/signature negative control escaped projection");
          native->bytes[word]^=1;
        }
      }
    }
    std::cout << "Native shader cache: 64 lifecycle comparisons and 192 tag/signature negative controls passed\n";
    main_state_fixture=true;
    unsigned main_packets=0,main_uploads=0;
    for(uint64_t dirty:{uint64_t(0),uint64_t(8),uint64_t(1)<<47,uint64_t(1)<<48,
        uint64_t(1)<<51,uint64_t(1)<<52,UINT64_MAX})
      for(uint32_t flags:{0,0x40,0x80,0xc0}) for(bool linked:{false,true})
      for(uint32_t shader_flags:{0,0x40,0x80}) for(uint32_t cache_mode:{2,3,4}) for(bool wrap:{false,true}) {
      const bool cached=cache_mode==1 || cache_mode==2;
      auto original=std::make_unique<Arena>(); auto* base=original->bytes.data();
      constexpr uint32_t program=0x60000,decl=0x65000,other=0x66000;
      constexpr uint32_t metadata=program+0x1000+872,second=program+0x1200+872;
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,wrap?cursor-4:cursor+2048);
      REX_STORE_U32(device+12420,program); REX_STORE_U32(device+12416,linked?other:0);
      REX_STORE_U32(device+11536,decl); REX_STORE_U8(device+10808,uint8_t(flags));
      REX_STORE_U8(device+10810,uint8_t(shader_flags));
      REX_STORE_U8(device+12256,7);
      // Include already-current declarations so a lone bit47 stays lone;
      // otherwise the initial CPU update would force bits51/48 in every case.
      REX_STORE_U32(device+10452,0x12345000|(cached?(linked?4:5):0));
      REX_STORE_U32(program+896,0x1000); REX_STORE_U32(program+904,0x1200);
      REX_STORE_U32(program+28,0x70000); REX_STORE_U32(program+872,0x20);
      REX_STORE_U32(metadata+4,12); REX_STORE_U32(second+4,12);
      REX_STORE_U32(metadata+12,0x123400); REX_STORE_U32(second+12,0x567800);
      REX_STORE_U32(metadata+8,0x01000000); REX_STORE_U32(second+8,0x02000000);
      REX_STORE_U32(other+64,128); REX_STORE_U32(other+24,0x70000);
      // Exercise the linked shader feature threshold and both render-mode
      // selection gates; these were previously zero in the matrix.
      REX_STORE_U32(other+128+40+8,flags==0x40?0x20000000:flags==0x80?0x10000000:0);
      REX_STORE_U32(other+128+40+12,0x100);
      REX_STORE_U32(other+128+40+20,cached?0:3);
      REX_STORE_U32(other+128+40+28,(flags&0x80)?0x10:0);
      REX_STORE_U32(device+10428,flags==0x40?8:0);
      REX_STORE_U32(other+128+40+24,0x12345678);
      real_shader_cache=cache_mode>=2;
      if(real_shader_cache) {
        // Nonzero declaration ID avoids the separate global-ID allocation
        // path. Exercise actual cache hit, refresh and in-flight rejection.
        REX_STORE_U32(decl+48,1);
        REX_STORE_U64(decl+32,UINT64_MAX); REX_STORE_U64(decl+40,UINT64_MAX);
        REX_STORE_U32(device+10768,0x67000); REX_STORE_U32(0x67000,100);
        REX_STORE_U32(device+10780,200);
        for(uint32_t variant:{0,1}) {
          const auto record=program+variant*416;
          REX_STORE_U32(record+40,cache_mode==2?1:2);
          REX_STORE_U64(record+48,REX_LOAD_U64(device+12256));
          REX_STORE_U64(record+56,REX_LOAD_U64(device+12264));
          REX_STORE_U32(record+64,cache_mode==4?150:0);
        }
      }
      auto native=std::make_unique<Arena>(*original);
      PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x821fe3f4;
      expected.r3.u64=device; expected.r4.u64=dirty;
      expected.r14.u64=14; expected.r15.u64=15; expected.r16.u64=16; expected.r17.u64=17;
      expected.r18.u64=18; expected.r19.u64=19; expected.r20.u64=20; expected.r21.u64=21;
      expected.r22.u64=22; expected.r23.u64=23; expected.r24.u64=24; expected.r25.u64=25;
      expected.r26.u64=26; expected.r27.u64=27; expected.r28.u64=28; expected.r29.u64=29;
      expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected; shader_cache_hit=cached; helper_trace=cache_trace=0; rollovers=0;
      {
        // Compare the inline regions while retaining the same CPU-only helper
        // implementations in both paths. All modes run the real shader cache:
        // hit, refresh and in-flight rejection. Native execution needs no scope.
        { Ownership scope(device); __imp__sub_8213ECB0(expected,base); }
        const auto original_trace=helper_trace;
        if(original_trace) ++main_uploads;
        helper_trace=cache_trace=0; const auto original_rollovers=rollovers;
        __imp__edf_native_main_state_cpu_tail(actual,native->bytes.data());
        Require(cache_trace==0,"native main-state used retail shader-cache dispatch");
        // Native uploads/derived state now bypass retail dispatch wrappers.
        // Full CPU memory and return-state comparisons below still verify their
        // effects; none of the retail tracing wrappers should be reached.
        Require(helper_trace==0,"native main-state used retail upload/derived dispatch");
        Require(rollovers==original_rollovers,"native main-state called GPU rollover");
      }
      if(REX_LOAD_U32(device+40)!=cursor) ++main_packets;
      for(uint32_t at=0;at<0xe0000;++at) {
        if((at>=device+40 && at<device+44) || (at>=cursor && at<cursor+2048)) continue;
        Require(base[at]==native->bytes[at],"main-state CPU data differs");
      }
      Require(actual.r3.u64==expected.r3.u64,"main-state 64-bit dirty-mask return differs");
      Require(actual.r1.u64==0xf0000 && actual.lr==0x821fe3f4 && actual.r14.u64==14 &&
        actual.r15.u64==15 && actual.r16.u64==16 && actual.r17.u64==17 && actual.r18.u64==18 &&
        actual.r19.u64==19 && actual.r20.u64==20 && actual.r21.u64==21 && actual.r22.u64==22 &&
        actual.r23.u64==23 && actual.r24.u64==24 && actual.r25.u64==25 && actual.r26.u64==26 &&
        actual.r27.u64==27 && actual.r28.u64==28 && actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,
        "main-state native ABI changed");
      base=native->bytes.data();
      Require(REX_LOAD_U32(device+40)==cursor,"native main-state advanced packet cursor");
      for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native main-state emitted packets");
    }
    Require(main_packets && main_uploads,"main-state fixture missed packet/helper branches");
    Require(shader_cache_hits && shader_cache_refreshes && shader_cache_busy,
      "main-state fixture missed real shader-cache hit/refresh/busy branches");
    real_shader_cache=false;
    main_state_fixture=false;
    unsigned derived_packets=0,derived_sets=0,derived_clears=0;
    for(uint32_t control:{0u,0x100000u,0x200000u,0x300000u})
      for(uint32_t mode=0;mode<8;++mode) for(uint32_t low:{0u,2u,3u,0x87u})
      for(bool flagged:{false,true}) for(uint32_t shader:{0,1,2}) for(bool wrap:{false,true}) {
      auto original=std::make_unique<Arena>(); auto* base=original->bytes.data();
      constexpr uint32_t program=0x60000;
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,wrap?cursor-4:cursor+2048);
      REX_STORE_U32(device+11580,control);
      REX_STORE_U32(device+10420,(mode<<4)|low);
      REX_STORE_U32(device+10432,flagged?0xdeadbe10:0xabcdef00);
      REX_STORE_U8(device+10810,flagged?0xff:0x20);
      REX_STORE_U8(device+10809,flagged?0xa5:0xa1);
      REX_STORE_U32(device+12416,shader?program:0);
      REX_STORE_U32(program+64,128);
      REX_STORE_U32(program+128+48,shader==2?0x08000000:0);
      auto native=std::make_unique<Arena>(*original);
      PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x8213f368;
      constexpr uint64_t dirty=0xabcdef1234560000ull;
      expected.r3.u64=device; expected.r4.u64=dirty; expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected;
      rollovers=0;
      __imp__sub_8213D750(expected,base);
      const unsigned original_rollovers=rollovers;
      const bool packet=REX_LOAD_U32(device+40)!=cursor;
      if(packet) {
        ++derived_packets;
        const uint32_t packet_cursor=cursor+(wrap?1024:0);
        Require(REX_LOAD_U32(device+40)==packet_cursor+8 &&
          REX_LOAD_U32(packet_cursor+4)==0xc0004600 && REX_LOAD_U32(packet_cursor+8)==15,
          "original derived packet header/payload differs");
      }
      Require(original_rollovers==unsigned(packet && wrap),"derived rollover condition differs");
      if((REX_LOAD_U8(device+10809)&4) && !flagged) ++derived_sets;
      if(!(REX_LOAD_U8(device+10809)&4) && flagged) ++derived_clears;
      __imp__edf_native_derived_cpu_tail(actual,native->bytes.data());
      Require(rollovers==original_rollovers,"native derived tail called GPU rollover");
      Require(actual.r3.u64==(dirty|0x100) && expected.r3.u64==actual.r3.u64 &&
        actual.r1.u64==expected.r1.u64 && actual.r1.u64==0xf0000 &&
        actual.lr==expected.lr && actual.lr==0x8213f368 &&
        actual.r30.u64==30 && actual.r31.u64==31,"derived CPU tail return/ABI differs");
      for(uint32_t at=0;at<13520;++at) if(at<40 || at>=44)
        Require(original->bytes[device+at]==native->bytes[device+at],"derived CPU state differs");
      Require(std::memcmp(original->bytes.data()+program,native->bytes.data()+program,256)==0,
        "derived shader metadata differs");
      base=native->bytes.data();
      Require(REX_LOAD_U32(device+40)==cursor,"native derived packet cursor advanced");
      for(uint32_t at=0;at<2048;++at) Require(base[cursor+at]==0,"native derived tail emitted packets");
    }
    Require(derived_packets && derived_sets && derived_clears,"derived fixture missed packet/CPU flag branches");
    {
      ShaderPatchArena arena; auto* base=arena.bytes;
      REX_STORE_U32(0x82000720,0x60200);
      signal_fixture=true;
      for(uint32_t cpu=0;cpu<6;++cpu) for(uint32_t argument:{0u,0x12345678u,0xffffffffu}) {
        std::memset(base,0,1024*1024); REX_STORE_U32(0x60200,device);
        REX_STORE_U8(0x65000+268,cpu);
        signal_cpu=cpu; signal_argument=argument; signal_events=0;
        PPCContext ctx{}; ctx.r13.u64=0x65000; ctx.r3.u64=argument; ctx.lr=0x12345678;
        __imp__sub_8214EBA0(ctx,base);
        Require(signal_events==1 && ctx.r13.u64==0x65000 && ctx.lr==0x12345678,
          "signal callback context/event contract differs");
      }
      for(uint32_t previous:{0u,3u,0xffffffffu}) for(uint32_t step:{0u,2u,65535u})
        for(uint32_t carry:{0u,3u}) for(uint32_t threshold:{0u,50u,100u}) for(uint32_t line:{0u,50u,99u}) {
        std::memset(base,0,1024*1024); REX_STORE_U32(0x60200,device);
        REX_STORE_U32(device+15124,previous); REX_STORE_U32(device+15128,carry);
        REX_STORE_U32(device+15136,0xffffffffu); REX_STORE_U32(device+10772,0x66000);
        REX_STORE_U32(0x66004,0xdeadbeef);
        signal_line=line; signal_reads=0;
        PPCContext ctx{}; ctx.r3.u64=(step<<16)|threshold; ctx.lr=0x12345678;
        __imp__sub_82151248(ctx,base);
        const uint32_t pending=step-previous+carry;
        const bool checks_timing=int32_t(pending)<=0;
        const bool acknowledges=checks_timing && line+1<=threshold;
        edf::native::NativeSwapPacingState native_pacing{previous,carry,0,0xffffffffu};
        const auto native_ack=native_pacing.Complete((step<<16)|threshold,line+1);
        Require(native_ack==acknowledges && native_pacing.ticks==REX_LOAD_U32(device+15124) &&
          native_pacing.acknowledged==REX_LOAD_U32(device+15128) && native_pacing.pending==REX_LOAD_U32(device+15132) &&
          native_pacing.callbacks==REX_LOAD_U32(device+15136),"native swap pacing differs from retail CPU callback");
        Require(signal_reads==(checks_timing?2u:0u),"signal timing branch differs");
        Require(REX_LOAD_U32(device+15136)==0 &&
          REX_LOAD_U32(device+15132)==(checks_timing && !acknowledges?1u:pending) &&
          REX_LOAD_U32(device+15128)==(acknowledges?previous:carry) &&
          REX_LOAD_U32(0x66004)==(acknowledges?0u:0xdeadbeef) && ctx.lr==0x12345678,
          "signal callback CPU pacing/writeback contract differs");
      }
      signal_fixture=false;
    }
    {
      ShaderPatchArena original,native;
      for(auto* base:{original.bytes,native.bytes}) {
        REX_LOOKUP_FUNC(base,REX_CODE_BASE)=edf_test_ring_callback;
        REX_LOOKUP_FUNC(base,REX_CODE_BASE+4)=edf_test_ring_callback;
        REX_LOOKUP_FUNC(base,REX_CODE_BASE+8)=edf_test_ring_callback;
      }
      for(uint32_t flags:{0u,1u,0x02000000u,0x02000001u,0xc0000000u,0x3f000000u,0xffffffffu})
        for(uint32_t argument:{0u,0x12345678u,0xffffffffu}) {
        auto* base=original.bytes; std::memset(base,0,1024*1024);
        REX_STORE_U32(device+10772,0xffca3000);
        std::memcpy(native.bytes,base,1024*1024);
        PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x12345678;
        expected.r3.u64=device; expected.r4.u64=cursor; expected.r5.u64=flags;
        expected.r6.u64=0x8214eba0; expected.r7.u64=argument;
        expected.r27.u64=27; expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
        auto actual=expected;
        __imp__sub_8213C9F0(expected,base);
        __imp__edf_native_worker_signal_cpu_tail(actual,native.bytes);
        const auto bytes=(flags&0xc0ffffffu)?92u:100u;
        Require(actual.r3.u32==expected.r3.u32 && actual.r3.u32==cursor+bytes,"native worker signal reservation differs");
        Require(std::memcmp(base,native.bytes,cursor+4)==0 &&
          std::memcmp(base+cursor+104,native.bytes+cursor+104,0xe0000-cursor-104)==0,"native worker signal CPU state differs");
        for(unsigned at=4;at<104;++at) Require(native.bytes[cursor+at]==0,"native worker signal emitted GPU bytes");
        Require(std::memcmp(base+cursor+4,native.bytes+cursor+4,bytes)!=0,"retail worker signal packets missing from fixture");
        Require(actual.r1.u64==0xf0000 && actual.lr==0x12345678 && actual.r27.u64==27 && actual.r28.u64==28 &&
          actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,"native worker signal ABI differs");
        // Reservation must not dereference a device/GPU writeback page. The
        // bridge supplies host signal ownership separately from this ABI tail.
        auto detached=actual;
        detached.r3.u64=0xfffffffcu;
        detached.r4.u64=cursor;
        detached.r5.u64=flags;
        detached.r6.u64=0x8214eba0;
        detached.r7.u64=argument;
        __imp__edf_native_worker_signal_cpu_tail(detached,native.bytes);
        Require(detached.r3.u32==cursor+bytes && detached.r1.u64==0xf0000 && detached.lr==0x12345678,
          "native worker signal requires a legacy device/writeback page");
      }
      for(uint32_t count:{0u,1u,2u,9u}) for(uint32_t start:{0u,6u,7u}) {
        auto* base=original.bytes; std::memset(base,0,1024*1024);
        REX_STORE_U32(device+13476,cursor); REX_STORE_U32(device+13480,7);
        REX_STORE_U32(device+10820,start);
        for(uint32_t i=0;i<count;++i) REX_STORE_U32(0x60000+i*4,0xabc00000+i);
        std::memcpy(native.bytes,base,1024*1024);
        PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x12345678;
        expected.r3.u64=device; expected.r4.u64=0x60000; expected.r5.u64=count;
        expected.r26.u64=26; expected.r27.u64=27; expected.r28.u64=28;
        expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
        auto actual=expected;
        ring_fixture=true; ring_reservations=0;
        __imp__edf_test_ring_copy_original(expected,base);
        Require(ring_reservations==1,"original ring copy reservation missing");
        ring_reservations=0;
        __imp__edf_native_ring_copy_cpu_tail(actual,native.bytes);
        Require(!ring_reservations,"native ring copy waited for Xbox capacity");
        Require(actual.r3.u32==expected.r3.u32 && actual.r3.u32==((start+count)&7),"native ring copy return differs");
        Require(std::memcmp(base,native.bytes,cursor)==0 &&
          std::memcmp(base+cursor+32,native.bytes+cursor+32,0xe0000-cursor-32)==0,"native ring copy CPU state differs");
        for(unsigned at=0;at<32;++at) Require(native.bytes[cursor+at]==0,"native ring copy emitted packets");
        Require(actual.r1.u64==0xf0000 && actual.lr==0x12345678 && actual.r26.u64==26 && actual.r27.u64==27 &&
          actual.r28.u64==28 && actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,"native ring copy ABI differs");
        ring_fixture=false;
      }
      for(uint32_t count:{0u,1u,3u}) for(bool wrap:{false,true}) for(bool wait_bit:{false,true})
        for(bool observer:{false,true}) for(bool callback:{false,true}) {
        auto* base=original.bytes; std::memset(base,0,1024*1024);
        REX_STORE_U32(device+13476,cursor); REX_STORE_U32(device+13480,7);
        REX_STORE_U32(device+10820,wrap?7:0); REX_STORE_U8(device+10809,wait_bit?2:0);
        REX_STORE_U32(device+19956,observer?0x61000:0);
        REX_STORE_U32(device+20100,callback?REX_CODE_BASE+8:0);
        REX_STORE_U32(0x61000,0x62000);
        REX_STORE_U32(0x62000+24,REX_CODE_BASE); REX_STORE_U32(0x62000+28,REX_CODE_BASE+4);
        for(uint32_t i=0;i<count;++i) {
          REX_STORE_U32(0x60000+i*8,4+i);
          REX_STORE_U32(0x60004+i*8,(i%2?0x21000000:0x10000000)+i*32);
        }
        std::memcpy(native.bytes,base,1024*1024);
        PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x12345678;
        expected.r3.u64=device; expected.r4.u64=0x60000; expected.r5.u64=count;
        expected.r21.u64=21; expected.r22.u64=22; expected.r23.u64=23; expected.r24.u64=24;
        expected.r25.u64=25; expected.r26.u64=26; expected.r27.u64=27; expected.r28.u64=28;
        expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
        auto actual=expected;
        ring_fixture=true; ring_reservations=ring_doorbells=0; ring_callbacks.clear();
        __imp__sub_8213C410(expected,base);
        Require(ring_reservations==unsigned(!wait_bit) && ring_doorbells==unsigned(!wait_bit),
          "original ring reserve/doorbell branches missing");
        const auto expected_callbacks=ring_callbacks;
        Require(expected_callbacks.size()==(wait_bit?(observer?2*count:0):(observer+callback)*(count+1)),
          "original ring callback branches missing");
        ring_reservations=ring_doorbells=0; ring_callbacks.clear();
        __imp__edf_native_ring_submit_cpu_tail(actual,native.bytes);
        Require(!ring_reservations && !ring_doorbells,"native ring reserved or wrote doorbell");
        Require(ring_callbacks==expected_callbacks,"native ring callback order/arguments differ");
        Require(std::memcmp(base,native.bytes,cursor)==0 &&
          std::memcmp(base+cursor+32,native.bytes+cursor+32,0xe0000-cursor-32)==0,
          "native ring CPU state differs");
        for(unsigned at=0;at<32;++at) Require(native.bytes[cursor+at]==0,"native ring emitted packet words");
        if(count && !wait_bit) Require(std::memcmp(base+cursor,native.bytes+cursor,32)!=0,
          "original ring packet stores missing");
        Require(actual.r1.u64==0xf0000 && actual.lr==0x12345678 && actual.r21.u64==21 &&
          actual.r22.u64==22 && actual.r23.u64==23 && actual.r24.u64==24 && actual.r25.u64==25 &&
          actual.r26.u64==26 && actual.r27.u64==27 && actual.r28.u64==28 && actual.r29.u64==29 &&
          actual.r30.u64==30 && actual.r31.u64==31,"native ring return/ABI differs");
        ring_fixture=false;
      }
    }
    for(bool recording:{false,true}) for(bool special:{false,true}) for(bool force:{false,true})
      for(bool dirty:{false,true}) for(unsigned failures=0;failures<8;++failures) {
      auto seed=std::make_unique<Arena>(),retail=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      auto* base=seed->bytes.data();
      REX_STORE_U8(device+10809,(recording?0x40:0)|(special?2:0));
      REX_STORE_U8(device+10810,4);
      REX_STORE_U64(device+11544,dirty?0x100000004000ull:0xffffffffull);
      REX_STORE_U32(device+19956,force?1:0);
      REX_STORE_U32(device+13000,0x30000);
      REX_STORE_U32(device+12944,0x12345678); REX_STORE_U32(device+12948,0xabcdef01);
      REX_STORE_U32(0xf0000-144+84,0x70000); // Defined legacy allocation-failure fallback.
      struct Result {
        std::vector<std::array<uint32_t,4>> lists,signals,allocations;
        unsigned restores,clears,exchanges;
        PPCContext context;
      };
      auto run=[&](Arena& arena,bool native_mode) {
        arena.bytes=seed->bytes;
        enclosing_fixture=cache_range_fixture=true; enclosing_native=native_mode;
        enclosing_failures=failures; enclosing_restores=enclosing_clears=cache_range_exchanges=0;
        enclosing_lists.clear(); enclosing_signals.clear(); enclosing_allocations.clear();
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678; ctx.r3.u64=device;
        ctx.r5.u64=0x02000000; ctx.r26.u64=26; ctx.r27.u64=27; ctx.r28.u64=28;
        ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
        __imp__sub_8214ECD8(ctx,arena.bytes.data());
        enclosing_fixture=cache_range_fixture=false;
        return Result{enclosing_lists,enclosing_signals,enclosing_allocations,
          enclosing_restores,enclosing_clears,cache_range_exchanges,ctx};
      };
      const auto original=run(*retail,false),actual=run(*native,true);
      auto worker_lists=original.lists;
      std::erase_if(worker_lists,[](const auto& item) { return item[2]==0; });
      Require(actual.lists==worker_lists && actual.signals==original.signals,
        "cache removal changed enclosing worker signal/range submission");
      Require(actual.allocations==original.allocations && actual.restores==original.restores &&
        actual.clears==original.clears && actual.exchanges==original.exchanges,
        "cache removal changed enclosing allocation/CPU helper behavior");
      Require(std::memcmp(retail->bytes.data()+device,native->bytes.data()+device,20000)==0,
        "cache removal changed enclosing device CPU state/busy count");
      Require(actual.context.r3.u64==original.context.r3.u64 && actual.context.r1.u64==0xf0000 &&
        actual.context.lr==0x12345678 && actual.context.r26.u64==26 && actual.context.r27.u64==27 &&
        actual.context.r28.u64==28 && actual.context.r29.u64==29 && actual.context.r30.u64==30 &&
        actual.context.r31.u64==31,"enclosing submission return/ABI changed");
    }
    for(uint64_t range:{0xffffffffull,0ull,0x100000004000ull,0xfffffffe00000008ull})
      for(bool command_range:{false,true}) for(bool alias:{false,true}) for(bool fail:{false,true}) {
      auto original=std::make_unique<Arena>(),native=std::make_unique<Arena>();
      auto* base=original->bytes.data();
      REX_STORE_U64(device+11544,range); REX_STORE_U8(device+10810,command_range?4:0);
      REX_STORE_U32(device+13484,alias?0xc0030000:0x30000);
      REX_STORE_U32(device+13488,alias?0xc0031ffc:0x31ffc);
      REX_STORE_U32(0x60000,0xaabbccdd); REX_STORE_U32(0x60004,0xdeadbeef);
      native->bytes=original->bytes;
      PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x8213CF9C;
      expected.r3.u64=device; expected.r4.u64=0x60000; expected.r5.u64=0x60004;
      expected.r19.u64=19; expected.r20.u64=20; expected.r21.u64=21; expected.r22.u64=22;
      expected.r23.u64=23; expected.r24.u64=24; expected.r25.u64=25; expected.r26.u64=26;
      expected.r27.u64=27; expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
      auto actual=expected;
      const bool dirty=range!=0xffffffffull, packets=dirty || command_range;
      cache_range_fixture=true; cache_range_failure=fail; allocations=cache_range_exchanges=0;
      __imp__sub_8213C5F0(expected,base);
      Require(allocations==unsigned(packets) && cache_range_exchanges==unsigned(dirty && !fail),
        "original cache-range branch coverage differs");
      Require(REX_LOAD_U32(0x60004)==(fail?0u:11u*(unsigned(dirty)+unsigned(command_range))),
        "original cache-range packet word count differs");
      if(packets && !fail) Require(REX_LOAD_U32(cursor)==2609,"original cache packet header missing");
      {
        auto reserved=std::make_unique<Arena>(); reserved->bytes=native->bytes;
        auto reservation_context=actual;
        allocations=cache_range_exchanges=0;
        __imp__edf_native_cache_reservation_cpu_tail(reservation_context,reserved->bytes.data());
        Require(allocations==unsigned(packets) && cache_range_exchanges==unsigned(dirty && !fail),
          "native fallback reservation changed allocation/failure/atomic exchange");
        base=reserved->bytes.data();
        Require(REX_LOAD_U32(0x60004)==0,"native reservation exposed cache packet words");
        Require(std::memcmp(original->bytes.data()+0x60000,base+0x60000,4)==0 &&
          std::memcmp(original->bytes.data()+device,base+device,20000)==0,
          "native fallback address or device CPU state differs from retail");
        for(unsigned at=0;at<256;++at) Require(base[cursor+at]==0,"native fallback storage contains cache packets");
        Require(reservation_context.r1.u64==expected.r1.u64 && reservation_context.lr==expected.lr,
          "native reservation stack/LR differs");
        Require(reservation_context.r19.u64==19 && reservation_context.r20.u64==20 &&
          reservation_context.r21.u64==21 && reservation_context.r22.u64==22 &&
          reservation_context.r23.u64==23 && reservation_context.r24.u64==24 &&
          reservation_context.r25.u64==25 && reservation_context.r26.u64==26 &&
          reservation_context.r27.u64==27 && reservation_context.r28.u64==28 &&
          reservation_context.r29.u64==29 && reservation_context.r30.u64==30 &&
          reservation_context.r31.u64==31,"native reservation nonvolatile register differs");
      }
      allocations=cache_range_exchanges=0;
      __imp__edf_native_cache_range_cpu_tail(actual,native->bytes.data());
      Require(!allocations && cache_range_exchanges==unsigned(dirty),"native cache range allocated or skipped CPU exchange");
      base=native->bytes.data();
      Require(REX_LOAD_U64(device+11544)==0xffffffffull && REX_LOAD_U32(0x60004)==0 &&
        REX_LOAD_U32(0x60000)==0xaabbccdd,"native cache-range CPU/output contract differs");
      for(unsigned at=0;at<256;++at) Require(base[cursor+at]==0,"native cache-range packet emitted");
      // The removed allocator cannot fail: unlike original allocation failure,
      // native mode consumes the dirty range without allocating packet storage.
      if(!fail) Require(std::memcmp(original->bytes.data()+device,base+device,20000)==0,
        "native cache-range device CPU state differs");
      Require(actual.r1.u64==0xf0000 && actual.lr==0x8213CF9C && actual.r19.u64==19 &&
        actual.r20.u64==20 && actual.r21.u64==21 && actual.r22.u64==22 && actual.r23.u64==23 &&
        actual.r24.u64==24 && actual.r25.u64==25 && actual.r26.u64==26 && actual.r27.u64==27 &&
        actual.r28.u64==28 && actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,
        "native cache-range return/ABI differs");
      cache_range_fixture=false;
    }
    {
      ShaderPatchArena arena;
      for(bool recording:{false,true}) for(bool special:{false,true}) for(bool wait_enabled:{false,true})
        for(bool already_waited:{false,true}) for(bool dirty:{false,true}) {
        auto* base=arena.bytes; std::memset(base,0,1024*1024);
        REX_STORE_U32(0x82578d08,wait_enabled?1:0);
        REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+10780,7);
        REX_STORE_U32(device+12944,recording?1:0);
        REX_STORE_U8(device+10808,special?0x80:0);
        REX_STORE_U8(device+10809,already_waited?2:0);
        REX_STORE_U8(device+10810,4);
        REX_STORE_U64(device+11544,dirty?0x100000004000ull:0xffffffffull);
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r3.u64=device; ctx.r30.u64=30; ctx.r31.u64=31;
        flush_fixture=cache_range_fixture=true;
        flush_submits=flush_waits=flush_cache_calls=allocations=cache_range_exchanges=0;
        __imp__sub_8213CF60(ctx,base);
        const bool cache_called=!recording && !special;
        const bool waits=!special && wait_enabled && !already_waited;
        Require(flush_submits==1 && flush_waits==unsigned(waits) && flush_cache_calls==unsigned(cache_called),
          "native flush lost submission/wait or changed cache gating");
        Require(!allocations && cache_range_exchanges==unsigned(cache_called && dirty),
          "native flush allocated cache packets or changed CPU exchange");
        Require(REX_LOAD_U8(device+10809)==((already_waited || waits)?2:0),"native flush wait bit differs");
        Require(ctx.r3.u32==cursor && ctx.r1.u64==0xf0000 && ctx.lr==0x12345678 &&
          ctx.r30.u64==30 && ctx.r31.u64==31,"native flush return/ABI differs");
        for(unsigned at=0;at<256;++at) Require(base[cursor+at]==0,"native flush wrote cache packets");
        // Pair native orchestration with the above original flush using the
        // same native cache service and controlled submission/wait dependencies.
        REX_STORE_U32(device+10780,7); REX_STORE_U8(device+10809,already_waited?2:0);
        REX_STORE_U64(device+11544,dirty?0x100000004000ull:0xffffffffull);
        flush_submits=flush_waits=flush_cache_calls=allocations=cache_range_exchanges=0;
        PPCContext work{}; work.r1.u64=0xf0000-112;
        const auto result=edf::native::FlushNativeSubmission(FlushReader{base},device,
          [&] {
            work.r3.u64=device; work.r4.u64=work.r1.u32+84; work.r5.u64=work.r1.u32+80;
            work.lr=0x8213CF9C; sub_8213C5F0(work,base);
            Require(REX_LOAD_U32(work.r1.u32+80)==0,"native flush cache contract changed");
          },
          [&] { work.r3.u64=device; work.lr=0x8213CFE0; sub_8213CDC0(work,base); },
          [&] { return REX_LOAD_U32(0x82578d08)!=0; },
          [&](uint32_t target) {
            work.r3.u64=device; work.r4.u64=target; work.r5.u64=0; work.r6.u64=0;
            work.lr=0x8213D020; sub_8213C928(work,base);
          });
        Require(result==cursor && flush_submits==1 && flush_waits==unsigned(waits) &&
          flush_cache_calls==unsigned(cache_called) && !allocations &&
          cache_range_exchanges==unsigned(cache_called && dirty),"native flush differs from original CPU effects");
        Require(REX_LOAD_U8(device+10809)==((already_waited || waits)?2:0),"native flush changed post-wait flag");
        flush_fixture=cache_range_fixture=false;
      }
    }
    for(uint32_t target:{0u,1u,7u,0xffffffffu}) for(bool pending:{false,true})
      for(bool recording:{false,true}) for(bool wrap:{false,true}) {
      auto arena=std::make_unique<Arena>(); auto* base=arena->bytes.data();
      REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,wrap?cursor-4:cursor+2048);
      REX_STORE_U32(device+10780,target); REX_STORE_U32(device+10768,0x70000);
      REX_STORE_U32(0x70000,pending?target-2:target);
      REX_STORE_U32(device+12944,recording?1:0);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r31.u64=0xabcdef;
      reset_fixture=reset_native=real_drain_wait=true; reset_tracking=1;
      reset_frees=rollovers=drain_polls=drain_wait_objects=drain_wait_frees=0;
      drain_target=target;
      __imp__edf_native_device_drain(ctx,base);
      const bool waits=target && pending && !recording;
      Require(rollovers==unsigned(waits) && drain_polls==(waits?3u:0u) &&
        drain_wait_objects==unsigned(waits) && drain_wait_frees==unsigned(waits),
        "native drain changed pending/recording/zero-fence wait behavior");
      Require(REX_LOAD_U32(device+40)==cursor,"native drain changed packet cursor");
      for(unsigned at=0;at<2048;++at) Require(base[cursor+at]==0,"native drain emitted packet bytes");
      Require(ctx.r1.u64==0xf0000 && ctx.lr==0x12345678 && ctx.r31.u64==0xabcdef,
        "native drain return/ABI changed");
      reset_fixture=reset_native=real_drain_wait=false;
    }
    {
      ShaderPatchArena original,native;
      reset_fixture=true;
      for(bool issued:{false,true}) for(bool recreate:{false,true}) for(bool borrowed:{false,true})
        for(unsigned failure=0;failure<5;++failure) for(uint32_t divisor:{16u,32u}) {
        auto* base=original.bytes; std::memset(base,0,1024*1024);
        constexpr uint32_t config=0x60000;
        REX_STORE_U32(device+40,cursor); REX_STORE_U32(device+48,cursor+2048);
        REX_STORE_U32(device+10780,issued?7:0);
        REX_STORE_U32(device+10768,0x77000); REX_STORE_U32(device+10772,0x77100);
        REX_STORE_U32(device+13416,0x75000); REX_STORE_U32(device+13420,0x76000);
        // Retail reference borrows a ring so allocation-failure ordinals match
        // the remaining native command/writeback allocations, not a removed GPU pool.
        REX_STORE_U32(config+4,1024); REX_STORE_U32(config+8,0x80000);
        REX_STORE_U32(config+12,8192); REX_STORE_U32(config+16,borrowed?0x90000:0);
        REX_STORE_U32(config+20,divisor);
        std::memcpy(native.bytes,base,1024*1024);
        PPCContext expected{}; expected.r1.u64=0xf0000; expected.lr=0x12345678;
        expected.r3.u64=device; expected.r4.u64=recreate?config:0;
        expected.r24.u64=24; expected.r25.u64=25; expected.r26.u64=26; expected.r27.u64=27;
        expected.r28.u64=28; expected.r29.u64=29; expected.r30.u64=30; expected.r31.u64=31;
        auto actual=expected;
        reset_needs_drain=issued; reset_failure=failure;
        reset_native=false; reset_tracking=1; reset_drained=reset_boundary=false;
        reset_allocations=reset_frees=reset_exports=reset_packets=rollovers=0;
        __imp__sub_8213D298(expected,base);
        const auto original_allocations=reset_allocations,original_frees=reset_frees;
        Require(reset_exports>0,"original reset did not call GPU setup export");
        const bool initialized=recreate && expected.r3.u32==0;
        const bool packet_wrap=initialized && divisor==32;
        Require(rollovers==unsigned(packet_wrap),"original initializer rollover not exercised");
        Require(reset_packets==(initialized?2u:0u),"original initializer packet paths not exercised");
        reset_native=true; reset_tracking=1; reset_drained=reset_boundary=false;
        reset_allocations=reset_frees=reset_exports=reset_packets=rollovers=0;
        { auto* base=native.bytes; REX_STORE_U32(config+8,borrowed?0x80000:0); }
        __imp__edf_native_device_reset(actual,native.bytes);
        { auto* base=native.bytes;
          Require(REX_LOAD_U32(device+13416)==0 && REX_LOAD_U32(device+13476)==0,"native reset retained GPU ring storage");
          REX_STORE_U32(config+8,0x80000); // Normalize only the intentionally ignored ring input.
        }
        REX_STORE_U32(device+13476,0); // Retail borrowed-ring publication is intentionally absent.
        Require(reset_boundary && reset_drained==issued,"native reset drain/retirement boundary missing");
        Require(!reset_exports,"native reset called GPU ring export");
        Require(reset_allocations==original_allocations && reset_frees==original_frees,"native reset changed allocation/free behavior");
        Require(reset_tracking==0 && reset_packets==0 && rollovers==0,"native reset retained tracking or emitted startup packets");
        Require(std::memcmp(base,native.bytes,device+40)==0 &&
          std::memcmp(base+device+44,native.bytes+device+44,cursor-device-44)==0 &&
          std::memcmp(base+cursor+12,native.bytes+cursor+12,0x90000-cursor-12)==0 &&
          std::memcmp(base+0x90088,native.bytes+0x90088,0x90404-0x90088)==0 &&
          std::memcmp(base+0x90414,native.bytes+0x90414,0xe0000-0x90414)==0,
          "native reset changed CPU storage/initialization");
        if(initialized) {
          const uint32_t final_packet=packet_wrap?0x90404:0x90078;
          Require(REX_LOAD_U32(device+40)==final_packet+12,"original initializer packet extent differs");
          Require(REX_LOAD_U32(0x90000)==0xc0114800 && REX_LOAD_U32(0x9004c)==3330 &&
            REX_LOAD_U32(final_packet)==1480 && REX_LOAD_U32(final_packet+8)==3332,
            "original startup packet headers differ");
          for(unsigned at=0;at<136;++at) Require(native.bytes[0x90000+at]==(borrowed?0:0xa5),
            "native initializer wrote packet storage");
          for(unsigned at=0;at<16;++at) Require(native.bytes[0x90404+at]==(borrowed?0:0xa5),
            "native initializer wrote rollover packet storage");
        } else Require(std::memcmp(base+0x90000,native.bytes+0x90000,136)==0,
          "failed/teardown reset changed unused packet storage");
        if(!packet_wrap) Require(std::memcmp(base+0x90404,native.bytes+0x90404,16)==0,
          "reset changed unused rollover storage");
        auto* original_base=base; base=native.bytes;
        Require(REX_LOAD_U32(device+40)==(initialized?0x8fffc:0),"native initializer cursor not empty");
        base=original_base;
        Require(std::memcmp(native.bytes+cursor, std::array<uint8_t,12>{}.data(),12)==0,
          "native reset drain emitted packets");
        if(issued) Require(REX_LOAD_U32(cursor+4)==1480 && REX_LOAD_U32(cursor+8)==131072,
          "original drain packet not exercised");
        Require(actual.r3.u64==expected.r3.u64 && actual.r1.u64==0xf0000 && actual.lr==0x12345678 &&
          actual.r24.u64==24 && actual.r25.u64==25 && actual.r26.u64==26 && actual.r27.u64==27 &&
          actual.r28.u64==28 && actual.r29.u64==29 && actual.r30.u64==30 && actual.r31.u64==31,
          "native reset return/ABI differs");
      }
      reset_fixture=false;
    }
    std::cout<<"Recompiled draw tails/state encoders: CPU state and native packet omission passed\n";
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
