#include "native_parallel_recorder.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace edf::native {
struct NativeParallelRecorder::Impl {
  // Frame ownership keeps packet copies trivial: no per-slot reference counts
  // on the producer. Deque elements retain their addresses across appends.
  using Image=NativeBackendConstantImage;
  using Bytes=const Image*;
  std::deque<Image> constant_images;
  size_t used_constant_images=0;
  // Constant images are interned for the frame: a SetConstants whose bytes
  // equal an image this frame already holds binds that image instead of
  // copying another. Materials are interned, so their constants recur all
  // frame; a draw that re-sends unchanged material constants costs a compare,
  // and the workers see one image they can skip re-binding and the backend
  // one image it can stage once (SetConstantImage). An open-addressed table
  // keyed on a sampled hash; the image bytes are always compared in full, so
  // a collision only costs a copy. Entries of an older epoch are empty.
  struct InternSlot { uint64_t key=0; const Image* image=nullptr; uint32_t epoch=0; };
  std::vector<InternSlot> intern_table=std::vector<InternSlot>(1024);
  uint32_t intern_epoch=1;
  size_t intern_occupied=0;
  // The whole of the first 128 bytes, then one word of every 64-byte block
  // (one row of every float4x4) after them: a matrix per draw lands in the
  // head, and a changed register block past it changes its sampled row. About
  // 70 multiplies for a 4 KB image, in four independent lanes.
  static uint64_t Hash(std::span<const uint8_t> bytes) {
    const auto* p=bytes.data(); const size_t n=bytes.size();
    uint64_t lanes[4]{0x9E3779B97F4A7C15ull^n,0xC2B2AE3D27D4EB4Full,0x165667B19E3779F9ull,0x27D4EB2F165667C5ull};
    auto mix=[](uint64_t h,uint64_t v) { return std::rotl((h^v)*0x9FB21C651E98DF25ull,29); };
    auto word=[&](size_t at) { uint64_t v; std::memcpy(&v,p+at,8); return v; };
    const size_t head=std::min<size_t>(n,128)&~size_t(31);
    size_t at=0;
    for(;at<head;at+=32) {
      lanes[0]=mix(lanes[0],word(at)); lanes[1]=mix(lanes[1],word(at+8));
      lanes[2]=mix(lanes[2],word(at+16)); lanes[3]=mix(lanes[3],word(at+24));
    }
    size_t lane=0;
    for(;at+8<=n;at+=(at<128?8:64)) { lanes[lane]=mix(lanes[lane],word(at)); lane=(lane+1)&3; }
    if(n>=8) lanes[lane]=mix(lanes[lane],word(n-8));
    else for(;at<n;++at) lanes[lane]=mix(lanes[lane],p[at]);
    uint64_t h=lanes[0]^std::rotl(lanes[1],17)^std::rotl(lanes[2],31)^std::rotl(lanes[3],47);
    h^=h>>33; h*=0xFF51AFD7ED558CCDull; h^=h>>33;
    return h;
  }
  // Per binding slot: a slot whose images keep missing the table (a matrix
  // that changes every draw) stops paying for the hash and probes it only
  // once in kProbeEvery binds, until a probe hits again. Skipping a probe
  // only ever means copying bytes an interned image already held.
  struct SlotHistory { uint32_t misses=0,skipped=0; };
  std::array<std::array<SlotHistory,14>,3> slot_history{};
  static constexpr uint32_t kMissRun=64,kProbeEvery=16;
  void ClearInterned() {
    if(++intern_epoch==0) { for(auto& slot:intern_table) slot.epoch=0; intern_epoch=1; }
    intern_occupied=0;
  }
  InternSlot& FindSlot(uint64_t key) {
    const size_t mask=intern_table.size()-1;
    for(size_t index=size_t(key)&mask;;index=(index+1)&mask) {
      auto& slot=intern_table[index];
      if(slot.epoch!=intern_epoch || slot.key==key) return slot;
    }
  }
  void GrowInterned() {
    std::vector<InternSlot> old(intern_table.size()*2);
    old.swap(intern_table);
    for(const auto& slot:old) if(slot.epoch==intern_epoch) FindSlot(slot.key)=slot;
  }
  Image& NewImage() {
    if(used_constant_images==constant_images.size()) constant_images.emplace_back();
    auto& image=constant_images[used_constant_images++];
    // Its storage retired at Reset; whatever a backend staged for its old
    // bytes belongs to them, not to what is written next.
    image.Invalidate();
    return image;
  }
  // The frame's image with these bytes: `current` (the slot's binding) when
  // it already holds them, else an interned one, else a new copy.
  Bytes Intern(std::span<const uint8_t> bytes,Bytes current,SlotHistory& history) {
    auto copy=[&]() -> Image& {
      auto& image=NewImage();
      image.bytes.assign(bytes.begin(),bytes.end());
      stats.constant_snapshot_bytes+=bytes.size();
      return image;
    };
    if(current && current->bytes.size()==bytes.size() &&
       std::memcmp(current->bytes.data(),bytes.data(),bytes.size())==0) {
      ++stats.constant_interned; stats.constant_interned_bytes+=bytes.size();
      return current;
    }
    if(history.misses>=kMissRun && ++history.skipped<kProbeEvery) return &copy();
    history.skipped=0;
    const auto key=Hash(bytes);
    auto* slot=&FindSlot(key);
    if(slot->epoch==intern_epoch) {
      const auto* image=slot->image;
      if(image->bytes.size()==bytes.size() && std::memcmp(image->bytes.data(),bytes.data(),bytes.size())==0) {
        ++stats.constant_interned; stats.constant_interned_bytes+=bytes.size();
        history.misses=0;
        return image;
      }
    } else if(++intern_occupied*2>intern_table.size()) {
      GrowInterned(); slot=&FindSlot(key);
    }
    if(history.misses<kMissRun) ++history.misses;
    auto& image=copy();
    // A colliding key now names the newer image; the older one stays valid
    // for every draw that already holds it.
    *slot={key,&image,intern_epoch};
    return &image;
  }
  // Transient vertex bytes, owned by the frame on the same terms as the
  // constant images: a packet points at one, a worker stages it, Reset reuses
  // the storage once every worker has finished.
  std::deque<std::vector<uint8_t>> vertex_images;
  size_t used_vertex_images=0;
  // Either a buffer or a transient image, never both. Distinct images never
  // compare equal, so a transient draw always re-stages its bytes.
  struct Vertex { NativeBackendBuffer* buffer=nullptr; uint32_t stride=0,offset=0; const std::vector<uint8_t>* transient=nullptr; bool operator==(const Vertex&) const=default; };
  struct DrawState {
    std::array<Vertex,16> vertices{};
    NativeBackendBuffer* indices=nullptr;
    NativeBackendIndexFormat index_format=NativeBackendIndexFormat::Uint16;
    uint32_t index_offset=0;
    std::array<std::array<Bytes,14>,3> constants{};
  };
  struct Bindings {
    NativeBackendPipeline* pipeline=nullptr;
    bool world_instancing=false;
    std::optional<NativeBackendTopology> topology;
    std::optional<std::array<float,4>> blend;
    std::array<std::array<NativeBackendTexture*,16>,3> textures{};
    std::array<std::array<NativeBackendSampler*,16>,3> samplers{};
    std::array<uint32_t,3> texture_counts{},sampler_counts{};
    std::array<NativeBackendRenderTarget*,8> colors{};
    uint32_t color_count=0;
    NativeBackendRenderTarget* depth=nullptr;
    std::optional<NativeBackendViewport> viewport;
    NativeBackendScissor scissor{};
    bool scissor_enabled=false;
  };
  // Mutable producer state is snapshotted only after a binding setter changes
  // it. Draws and saved states retain immutable frame-owned snapshots.
  struct State : DrawState {
    Bindings bindings;
    const Bindings* snapshot=nullptr;
    // Producer-only override: immutable shared constants remain frame-owned.
    // Saved states retain their own matrix; packets always have full snapshots.
    struct World {
      bool active=false;
      uint32_t slot=0,offset=0;
      std::array<uint8_t,64> bytes{};
    } world;
  } state;
  bool reuse_world_constants=true;
  bool transient_batching=false;
  // The vertex image a transient append created for packets.back() and which
  // nothing else references, so the next append may grow it in place. Any
  // other image may also be bound by an earlier packet or by the serial replay
  // state that skips re-staging an image it has already staged, so appending
  // to it would change what that other draw reads.
  const std::vector<uint8_t>* appendable=nullptr;
  struct PacketState : DrawState { const Bindings* bindings=nullptr; };
  std::deque<Bindings> binding_images;
  size_t used_binding_images=0;
  const Bindings* SnapshotBindings() {
    if(!state.snapshot) {
      if(used_binding_images==binding_images.size()) binding_images.emplace_back();
      auto& image=binding_images[used_binding_images++];
      image=state.bindings;
      state.snapshot=&image;
    }
    return state.snapshot;
  }
  struct DrawPacket {
    PacketState state;
    bool indexed=false;
    uint32_t count=0,instances=1,first=0,first_instance=0;
    int32_t base=0;
    std::vector<uint8_t>* worlds=nullptr;
  };
  static_assert(sizeof(DrawPacket)<sizeof(State)/2,
                "draw packets should copy less than half the complete producer state");
  // Profiling timestamps between packets: `before` is the packet they precede
  // (packets.size() for after the last). Replayed by whichever list records
  // that packet, so they keep their place among the draws without a flush.
  struct Marker {
    size_t before=0;
    NativeBackendTimestamps* set=nullptr;
    uint32_t first=0,count=0;
    bool resolve=false;
  };
  // One worker's contiguous range of a batch, owned by the job while it is
  // recorded: the producer hands a prefix of its packets over and keeps
  // appending to its own vector, so nothing a worker reads is ever moved or
  // written. Markers are rebased to the job's packets.
  struct Worker {
    // A streamed range owns its packets; the tail's ranges read the pending
    // packets in place, as the producer is joined while they are recorded.
    std::vector<DrawPacket> owned;
    const DrawPacket* packets=nullptr;
    size_t count=0;
    std::vector<Marker> markers;
    std::exception_ptr error;
    uint64_t elapsed=0;
    bool assigned=false;
  };
  std::vector<State> stack;
  std::optional<PacketState> serial_previous;
  // The batch's packets not yet handed to a worker.
  std::vector<DrawPacket> packets;
  std::vector<NativeBackendRecorder*> recorders;
  std::function<void(bool)> finish;
  std::mutex mutex;
  std::condition_variable done;
  std::vector<std::unique_ptr<std::condition_variable>> wake;
  std::vector<Worker> jobs;
  std::vector<std::jthread> threads;
  size_t outstanding=0;
  bool stopping=false;
  // Streaming: a batch's leading ranges go to workers while the producer is
  // still capturing the rest, so a flush joins only the tail. `dispatched` is
  // how many workers of this batch hold a range and `dispatched_draws` how
  // many packets they hold. A range is handed over once the pending packets
  // exceed `chunk`, sized from this batch in the previous frame (the same
  // flush ordinal); the last worker always waits for the flush, so the tail
  // always has a list. A frame that under-predicts leaves a longer tail on
  // that list; one that over-predicts splits its tail as an unstreamed flush.
  size_t dispatched=0,dispatched_draws=0,chunk=0;
  size_t batch_ordinal=0;
  std::vector<size_t> predicted,observed;
  bool observed_parallel=false;
  std::vector<NativeBackendQuery*> queries;
  std::vector<Marker> markers;
  static void ReplayMarker(NativeBackendRecorder& r,const Marker& m) {
    if(m.resolve) r.ResolveTimestamps(*m.set,m.first,m.count); else r.WriteTimestamp(*m.set,m.first);
  }
  bool Pending() const { return !packets.empty(); }
  // With nothing pending, recorder 0 is already the tail of the stream: any
  // earlier worker lists were submitted by the flush that emptied `packets`.
  // A handed-over range always leaves the packet after it pending.
  void Mark(Marker m) {
    if(failure) std::rethrow_exception(failure);
    // Checked here, on the producer: a bad slot found by a worker would poison
    // the whole recorder instead of failing the one call.
    if(!m.count || m.first>=m.set->capacity() || m.count>m.set->capacity()-m.first)
      throw std::runtime_error("timestamp range outside its set");
    if(!Pending()) { ReplayMarker(*recorders[0],m); return; }
    m.before=packets.size();
    markers.push_back(m);
  }
  std::atomic<uint32_t> active_workers{0},max_concurrent{0};
  Statistics stats;
  std::exception_ptr failure;
  uint32_t minimum_draws;

  Impl(std::vector<NativeBackendRecorder*> r,std::function<void(bool)> f,uint32_t minimum)
      :recorders(std::move(r)),finish(std::move(f)),minimum_draws(minimum) {
    if(recorders.size()<2 || recorders.size()>33 || !finish)
      throw std::runtime_error("parallel recorder requires 1..32 workers and a submission callback");
    jobs.resize(recorders.size()-1);
    for(size_t index=0;index<jobs.size();++index) wake.push_back(std::make_unique<std::condition_variable>());
    try {
      for(size_t index=0;index<jobs.size();++index) threads.emplace_back([this,index] {
        for(;;) {
          std::unique_lock lock(mutex);
          wake[index]->wait(lock,[&]{return stopping || jobs[index].assigned;});
          if(stopping) return;
          lock.unlock();
          auto& job=jobs[index];
          const auto begin=std::chrono::steady_clock::now();
          if(job.count) {
            const auto active=active_workers.fetch_add(1)+1;
            auto maximum=max_concurrent.load();
            while(maximum<active && !max_concurrent.compare_exchange_weak(maximum,active)) {}
          }
          try {
            auto& list=*recorders[index+1];
            const PacketState* previous=nullptr;
            auto marker=job.markers.begin();
            for(size_t draw=0;draw<job.count;++draw) {
              for(;marker!=job.markers.end() && marker->before==draw;++marker) ReplayMarker(list,*marker);
              Replay(list,job.packets[draw],previous);
              previous=job.packets[draw].worlds?nullptr:&job.packets[draw].state;
            }
            // Markers after the last packet: only the batch's final range
            // has any, and its list executes last.
            for(;marker!=job.markers.end();++marker) ReplayMarker(list,*marker);
          } catch(...) { job.error=std::current_exception(); }
          if(job.count) active_workers.fetch_sub(1);
          job.elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-begin).count());
          lock.lock();
          job.assigned=false;
          if(--outstanding==0) done.notify_one();
        }
      });
    } catch(...) { Stop(); throw; }
  }
  ~Impl() { Stop(); }
  void Stop() {
    { std::lock_guard lock(mutex); stopping=true; }
    for(auto& cv:wake) cv->notify_all();
    threads.clear();
  }
  static void Replay(NativeBackendRecorder& r,const DrawPacket& draw,const PacketState* previous) {
    const auto& s=draw.state;
    const auto& b=*s.bindings;
    if(draw.worlds) previous=nullptr; // Alternate pipeline/stream are local to this draw.
    const auto* old=previous?previous->bindings:nullptr;
    const bool bindings_changed=old!=s.bindings;
    if(!b.pipeline) throw std::runtime_error("draw packet has no pipeline");
    if(bindings_changed) {
      const bool pipeline_changed=!old || old->pipeline!=b.pipeline;
      if(pipeline_changed) r.SetPipeline(draw.worlds?*b.pipeline->world_instanced:*b.pipeline);
      if(b.topology && (pipeline_changed || old->topology!=b.topology)) r.SetTopology(*b.topology);
      if(b.blend && (pipeline_changed || old->blend!=b.blend)) r.SetBlendFactor(*b.blend);
      if(!old || b.colors!=old->colors || b.color_count!=old->color_count || b.depth!=old->depth)
        r.SetRenderTargets({b.colors.data(),b.color_count},b.depth);
      if(b.viewport && (!old || !old->viewport ||
         std::memcmp(&*b.viewport,&*old->viewport,sizeof(NativeBackendViewport)))) r.SetViewport(*b.viewport);
      if(!old || b.scissor_enabled!=old->scissor_enabled ||
         std::memcmp(&b.scissor,&old->scissor,sizeof(NativeBackendScissor)))
        r.SetScissor(b.scissor,b.scissor_enabled);
    }
    for(uint32_t slot=0;slot<s.vertices.size();++slot) {
      const auto& v=s.vertices[slot];
      if(!v.buffer && !v.transient) continue;
      if(previous && v==previous->vertices[slot]) continue;
      if(v.transient) r.SetTransientVertices(slot,*v.transient,v.stride);
      else r.SetVertexBuffer(slot,*v.buffer,v.stride,v.offset);
    }
    if(s.indices && (!previous || s.indices!=previous->indices || s.index_offset!=previous->index_offset ||
       s.index_format!=previous->index_format)) r.SetIndexBuffer(*s.indices,s.index_format,s.index_offset);
    for(uint32_t stage=0;stage<3;++stage) {
      // Interned images: an unchanged binding is the same pointer, and the
      // backend may bind an image it already staged in this submission.
      for(uint32_t slot=0;slot<14;++slot)
        if(s.constants[stage][slot] && (!previous || previous->constants[stage][slot]!=s.constants[stage][slot]))
          r.SetConstantImage(NativeBackendStage(stage),slot,*s.constants[stage][slot]);
      if(!bindings_changed) continue;
      const auto textures=std::max(b.texture_counts[stage],previous?old->texture_counts[stage]:0u);
      for(uint32_t slot=0;slot<textures;++slot) {
        auto* texture=b.textures[stage][slot];
        if(previous && texture==old->textures[stage][slot] &&
           b.colors==old->colors && b.depth==old->depth) continue;
        // Binding a sampled target for writing invalidates its stale SRV. The
        // previous draw may have used it even when this shader does not.
        for(uint32_t color=0;color<b.color_count;++color)
          if(b.colors[color] && b.colors[color]->texture()==texture) texture=nullptr;
        if(b.depth && b.depth->texture()==texture) texture=nullptr;
        r.SetTexture(NativeBackendStage(stage),slot,texture);
      }
      const auto samplers=std::max(b.sampler_counts[stage],previous?old->sampler_counts[stage]:0u);
      for(uint32_t slot=0;slot<samplers;++slot)
        if(!previous || old->samplers[stage][slot]!=b.samplers[stage][slot])
          r.SetSampler(NativeBackendStage(stage),slot,b.samplers[stage][slot]);
    }
    if(draw.worlds) r.SetTransientVertices(15,*draw.worlds,64);
    if(draw.indexed) r.DrawIndexedInstanced(draw.count,draw.instances,draw.first,draw.base,draw.first_instance);
    else r.Draw(draw.count,draw.first);
  }
  static bool SameBytes(Bytes a,Bytes b) { return a==b || (a && b && a->bytes==b->bytes); }
  void MaterializeWorld() {
    auto& w=state.world;
    if(!w.active) return;
    auto& image=NewImage();
    image.bytes=state.constants[0][w.slot]->bytes;
    std::memcpy(image.bytes.data()+w.offset,w.bytes.data(),64);
    stats.constant_snapshot_bytes+=image.bytes.size();
    state.constants[0][w.slot]=&image;
    w.active=false;
  }
  // Largest vertex image an append may build; a run past it starts a new draw.
  static constexpr size_t kTransientAppendBytes=size_t(1)<<20;
  // Appends a transient list draw to the draw recorded just before it when the
  // two differ in nothing but their vertices: the same bindings snapshot
  // (pipeline, topology, targets, viewport, scissor, blend factor, textures,
  // samplers), equal constants in every stage and slot, both non-indexed from
  // vertex 0 over their whole slot-0 image of whole primitives. A list draw of
  // A's vertices followed by B's then becomes one draw of A's vertices then
  // B's: the same primitives, assembled from the same vertices, rasterized and
  // blended in the same order. Only a pipeline marked transient_batchable is
  // eligible - one reading slot 0 alone, per vertex, and nothing that numbers
  // vertices or primitives, which a merged draw would number differently.
  // Never across a GPU timestamp marker taken after the previous draw: the
  // appended vertices would be drawn before the marker, inside the span
  // before it rather than the one after.
  bool TryAppendTransient(bool indexed,uint32_t count,uint32_t instances,uint32_t first,
                          uint32_t first_instance,const Bindings* bindings) {
    if(!transient_batching || indexed || instances!=1 || first || first_instance ||
       packets.empty() || !queries.empty() || state.world.active ||
       (!markers.empty() && markers.back().before==packets.size())) return false;
    const auto* pipeline=bindings->pipeline;
    if(!pipeline->transient_batchable || !bindings->topology) return false;
    uint32_t primitive=0;
    switch(*bindings->topology) {
      case NativeBackendTopology::TriangleList: primitive=3; break;
      case NativeBackendTopology::LineList: primitive=2; break;
      case NativeBackendTopology::PointList: primitive=1; break;
      default: return false;
    }
    auto& previous=packets.back();
    if(previous.indexed || previous.worlds || previous.instances!=1 || previous.first ||
       previous.first_instance || previous.state.bindings!=bindings) return false;
    const auto& mine=state.vertices[0];
    const auto& theirs=previous.state.vertices[0];
    if(!mine.transient || !theirs.transient || mine.buffer || theirs.buffer || !mine.stride ||
       mine.stride!=theirs.stride || mine.offset || theirs.offset || mine.transient==theirs.transient ||
       !count || count%primitive || !previous.count || previous.count%primitive ||
       size_t(count)*mine.stride!=mine.transient->size() ||
       size_t(previous.count)*theirs.stride!=theirs.transient->size() ||
       theirs.transient->size()+mine.transient->size()>kTransientAppendBytes) return false;
    for(size_t stage=0;stage<state.constants.size();++stage)
      for(size_t slot=0;slot<state.constants[stage].size();++slot)
        if(!SameBytes(previous.state.constants[stage][slot],state.constants[stage][slot])) return false;
    if(theirs.transient!=appendable) {
      if(used_vertex_images==vertex_images.size()) vertex_images.emplace_back();
      auto& image=vertex_images[used_vertex_images++];
      image.assign(theirs.transient->begin(),theirs.transient->end());
      previous.state.vertices[0].transient=&image;
      appendable=&image;
    }
    // Owned by this recorder's deque; const only in the packet's view of it.
    auto& grown=const_cast<std::vector<uint8_t>&>(*appendable);
    grown.insert(grown.end(),mine.transient->begin(),mine.transient->end());
    previous.count+=count;
    ++stats.transient_appends; ++stats.draws;
    return true;
  }
  // Called with the mutex held. The job is idle: the flush that last used it
  // joined it.
  void Assign(size_t index) {
    auto& job=jobs[index];
    job.error=nullptr; job.elapsed=0; job.assigned=true;
    if(job.count) stats.worker_mask|=uint64_t(1)<<index;
    ++outstanding;
    wake[index]->notify_one();
  }
  // Hands every pending packet but the last to the next worker. The last one
  // stays: a following draw may fold into it or append to it, and a marker
  // or flush still has a pending packet to order against.
  void Stream() {
    auto& job=jobs[dispatched];
    const size_t handed=packets.size()-1;
    job.owned.clear();
    job.owned.swap(packets);
    packets.push_back(job.owned.back());
    job.owned.pop_back();
    job.packets=job.owned.data(); job.count=job.owned.size();
    job.markers.clear();
    size_t kept=0;
    for(auto& marker:markers) {
      if(marker.before<handed) job.markers.push_back(marker);
      else { marker.before-=handed; markers[kept++]=marker; }
    }
    markers.resize(kept);
    {
      std::lock_guard lock(mutex);
      Assign(dispatched);
    }
    ++dispatched; dispatched_draws+=handed; ++stats.streamed_jobs;
  }
  void Draw(bool indexed,uint32_t count,uint32_t instances,uint32_t first,int32_t base,uint32_t first_instance) {
    if(failure) std::rethrow_exception(failure);
    if(!state.bindings.pipeline) throw std::runtime_error("draw packet has no pipeline");
    if(state.bindings.pipeline->requires_blend_factor() && !state.bindings.blend)
      throw std::runtime_error("draw packet requires a blend factor");
    const auto* bindings=SnapshotBindings();
    if(TryAppendTransient(indexed,count,instances,first,first_instance,bindings)) return;
    if(queries.empty() && indexed && instances==1 && !first_instance &&
       bindings->world_instancing && bindings->pipeline->world_instanced && !packets.empty() &&
       (markers.empty() || markers.back().before!=packets.size())) {
      auto& previous=packets.back();
      const auto slot=bindings->pipeline->instance_world_slot;
      const auto offset=bindings->pipeline->instance_world_offset;
      const auto current_world=state.constants[0][slot];
      const auto old_world=previous.state.constants[0][slot];
      bool compatible=previous.indexed && (previous.instances==1 || previous.worlds) && previous.instances<256 && !previous.first_instance &&
        previous.count==count && previous.first==first && previous.base==base &&
        previous.state.bindings==bindings && previous.state.vertices==state.vertices &&
        previous.state.indices==state.indices && previous.state.index_format==state.index_format &&
        previous.state.index_offset==state.index_offset && current_world && old_world &&
        current_world->bytes.size()==old_world->bytes.size() && offset+64<=current_world->bytes.size() &&
        !state.vertices[15].buffer && !state.vertices[15].transient;
      for(const auto& vertex:state.vertices) if(vertex.transient) compatible=false;
      for(size_t stage=0;compatible && stage<3;++stage) for(size_t s=0;compatible && s<14;++s) {
        const auto a=previous.state.constants[stage][s],b=state.constants[stage][s];
        if(a==b) continue;
        if(!a || !b || a->bytes.size()!=b->bytes.size()) { compatible=false; break; }
        if(stage==0 && s==slot) compatible=
          std::memcmp(a->bytes.data(),b->bytes.data(),offset)==0 &&
          std::memcmp(a->bytes.data()+offset+64,b->bytes.data()+offset+64,a->bytes.size()-offset-64)==0;
        else compatible=a->bytes==b->bytes;
      }
      if(compatible) {
        if(!previous.worlds) {
          if(used_vertex_images==vertex_images.size()) vertex_images.emplace_back();
          auto& image=vertex_images[used_vertex_images++];
          image.clear(); image.reserve(256*64);
          image.insert(image.end(),old_world->bytes.begin()+offset,old_world->bytes.begin()+offset+64);
          previous.worlds=&image; ++stats.instanced_draws;
        }
        const auto* matrix=state.world.active?state.world.bytes.data():current_world->bytes.data()+offset;
        previous.worlds->insert(previous.worlds->end(),matrix,matrix+64);
        ++previous.instances; ++stats.folded_draws; ++stats.draws;
        return;
      }
    }
    // A batch break (including queries and ordinary draws) needs a complete
    // immutable image. Only these draws pay for copying the shared constants.
    MaterializeWorld();
    if(!queries.empty()) {
      DrawPacket packet{{static_cast<const DrawState&>(state),bindings},
                        indexed,count,instances,first,first_instance,base};
      Replay(*recorders[0],packet,serial_previous?&*serial_previous:nullptr);
      serial_previous=std::move(packet.state);
    } else {
      // Only geometry and constant pointers are copied per draw. Material,
      // target and raster bindings share an immutable snapshot.
      if(packets.empty()) {
        // A new batch: how much of it to hand over at a time.
        const size_t workers=jobs.size();
        // The streamed workers split all but a small tail: they record
        // behind the producer, which captures a draw more slowly than a
        // worker replays one, so what the flush waits for is the tail.
        const size_t expected=batch_ordinal<predicted.size()?predicted[batch_ordinal]:0;
        const size_t tail=expected/(4*workers);
        chunk=workers>1 && expected>=minimum_draws?
          std::max<size_t>({(expected-tail+workers-2)/(workers-1),minimum_draws,kMinimumStream}):0;
      }
      auto& packet=packets.emplace_back();
      static_cast<DrawState&>(packet.state)=state; packet.state.bindings=bindings;
      packet.indexed=indexed; packet.count=count; packet.instances=instances;
      packet.first=first; packet.first_instance=first_instance; packet.base=base;
      packet.worlds=nullptr;
      if(chunk && dispatched+1<jobs.size() && packets.size()>chunk) Stream();
    }
    ++stats.draws;
  }
  // Fewer packets than this per streamed range is not worth a wakeup.
  static constexpr size_t kMinimumStream=16;
  void EndBatch(size_t total) {
    if(observed.size()<=batch_ordinal) observed.resize(batch_ordinal+1,0);
    observed[batch_ordinal++]=total;
  }
  void Flush(bool reopen) {
    if(failure) std::rethrow_exception(failure);
    appendable=nullptr;
    if(packets.empty()) { if(!reopen) { finish(false); serial_previous.reset(); } return; }
    const size_t total=dispatched_draws+packets.size();
    EndBatch(total);
    // Small UI/immediate runs often end at a buffer update. Waking every
    // worker and submitting a GPU frame per such run costs more than recording
    // them inline. Keep them on the ordered prefix list; large geometry runs
    // still follow that prefix on independent worker lists. A batch that has
    // streamed a range to a worker is never small.
    if(!dispatched && packets.size()<minimum_draws) {
      ++stats.serial_flushes; stats.serial_draws+=packets.size();
      const PacketState* previous=serial_previous?&*serial_previous:nullptr;
      size_t marker=0;
      for(size_t draw=0;draw<packets.size();++draw) {
        for(;marker<markers.size() && markers[marker].before==draw;++marker) ReplayMarker(*recorders[0],markers[marker]);
        Replay(*recorders[0],packets[draw],previous);
        previous=packets[draw].worlds?nullptr:&packets[draw].state;
      }
      for(;marker<markers.size();++marker) ReplayMarker(*recorders[0],markers[marker]);
      markers.clear();
      if(packets.back().worlds) serial_previous.reset();
      else serial_previous=std::move(packets.back().state);
      packets.clear();
      if(!reopen) { finish(false); serial_previous.reset(); }
      return;
    }
    // The tail: split what is still pending among the workers that have no
    // range yet, in order, markers with the packets they precede.
    observed_parallel=true;
    const auto begin=std::chrono::steady_clock::now();
    {
      const size_t remaining=jobs.size()-dispatched,count=packets.size();
      for(size_t i=0;i<remaining;++i) {
        auto& job=jobs[dispatched+i];
        const size_t from=count*i/remaining,to=count*(i+1)/remaining;
        job.packets=packets.data()+from; job.count=to-from;
        job.markers.clear();
        for(const auto& marker:markers)
          if(marker.before>=from && (marker.before<to || (i+1==remaining && marker.before>=to))) {
            job.markers.push_back(marker); job.markers.back().before-=from;
          }
      }
      std::unique_lock lock(mutex);
      for(size_t i=dispatched;i<jobs.size();++i) Assign(i);
      done.wait(lock,[&]{return outstanding==0;});
    }
    stats.wait_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-begin).count());
    std::exception_ptr error;
    for(const auto& job:jobs) { stats.record_ns+=job.elapsed; if(job.error && !error) error=job.error; }
    packets.clear(); markers.clear();
    dispatched=0; dispatched_draws=0; chunk=0;
    if(error) std::rethrow_exception(error);
    ++stats.batches;
    finish(reopen); serial_previous.reset();
  }
};
NativeParallelRecorder::NativeParallelRecorder(std::vector<NativeBackendRecorder*> r,std::function<void(bool)> f,uint32_t minimum)
  :impl_(std::make_unique<Impl>(std::move(r),std::move(f),minimum)) {}
NativeParallelRecorder::~NativeParallelRecorder()=default;
void NativeParallelRecorder::Reset() {
  if(!impl_->packets.empty()) throw std::runtime_error("reset would discard unsubmitted geometry");
  impl_->state={}; impl_->stack.clear(); impl_->serial_previous.reset(); impl_->queries.clear(); impl_->markers.clear();
  // Flush joins the recording workers. Their SetConstants calls have copied
  // the data into backend-owned upload storage before these images retire.
  // Reuse the high-water storage next frame rather than allocating a vector
  // for every changed binding. No old packet or saved state survives Reset.
  impl_->used_constant_images=0;
  impl_->ClearInterned();
  impl_->used_vertex_images=0;
  impl_->appendable=nullptr;
  impl_->used_binding_images=0;
  // This frame's batch sizes predict the next frame's. A frame that sent
  // nothing to the workers (a readback, a loading screen) keeps the last
  // prediction rather than forgetting it.
  if(impl_->observed_parallel) impl_->predicted.swap(impl_->observed);
  impl_->observed.clear();
  impl_->observed_parallel=false;
  impl_->batch_ordinal=0;
}
void NativeParallelRecorder::Flush(bool reopen) {
  try { impl_->Flush(reopen); }
  catch(...) { impl_->failure=std::current_exception(); throw; }
}
NativeParallelRecorder::Statistics NativeParallelRecorder::statistics() const {
  auto result=impl_->stats; result.max_concurrent=impl_->max_concurrent.load(); return result;
}
void NativeParallelRecorder::Forget(const void* resource) {
  Flush();
  auto forget=[&](Impl::State& s) {
    for(auto& v:s.vertices) if(v.buffer==resource) v={};
    if(s.indices==resource) s.indices=nullptr;
    if(s.bindings.depth==resource) s.bindings.depth=nullptr;
    for(auto& c:s.bindings.colors) if(c==resource) c=nullptr;
    for(auto& stage:s.bindings.textures) for(auto& texture:stage) if(texture==resource) texture=nullptr;
    s.snapshot=nullptr;
  };
  forget(impl_->state); for(auto& s:impl_->stack) forget(s);
  // Previous snapshots are immutable and may contain the retired resource.
  // Rebind the next serial draw instead of editing a shared snapshot.
  impl_->serial_previous.reset();
}
void NativeParallelRecorder::SetPipeline(NativeBackendPipeline& p) {
  auto& s=impl_->state;
  if(s.bindings.pipeline!=&p) { impl_->MaterializeWorld(); s.bindings.pipeline=&p; s.snapshot=nullptr; }
}
void NativeParallelRecorder::SetWorldInstancing(bool enabled,bool reuse_constants) {
  auto& s=impl_->state;
  impl_->reuse_world_constants=reuse_constants;
  if(s.bindings.world_instancing!=enabled) { s.bindings.world_instancing=enabled; s.snapshot=nullptr; }
}
void NativeParallelRecorder::SetTransientBatching(bool enabled) { impl_->transient_batching=enabled; }
void NativeParallelRecorder::SetVertexBuffer(uint32_t slot,NativeBackendBuffer& b,uint32_t stride,uint32_t offset) {
  impl_->state.vertices.at(slot)={&b,stride,offset,nullptr};
}
void NativeParallelRecorder::SetTransientVertices(uint32_t slot,std::span<const uint8_t> bytes,uint32_t stride) {
  auto& binding=impl_->state.vertices.at(slot);
  if(impl_->used_vertex_images==impl_->vertex_images.size()) impl_->vertex_images.emplace_back();
  auto& image=impl_->vertex_images[impl_->used_vertex_images];
  image.assign(bytes.begin(),bytes.end());
  binding={nullptr,stride,0,&image};
  ++impl_->used_vertex_images;
}
void NativeParallelRecorder::SetIndexBuffer(NativeBackendBuffer& b,NativeBackendIndexFormat f,uint32_t offset) {
  impl_->state.indices=&b; impl_->state.index_format=f; impl_->state.index_offset=offset;
}
void NativeParallelRecorder::SetTopology(NativeBackendTopology t) {
  auto& s=impl_->state;
  if(s.bindings.topology!=t) { s.bindings.topology=t; s.snapshot=nullptr; }
}
void NativeParallelRecorder::SetTransientVerticesOwned(uint32_t slot,std::vector<uint8_t>& bytes,uint32_t stride) {
  auto& binding=impl_->state.vertices.at(slot);
  if(impl_->used_vertex_images==impl_->vertex_images.size()) impl_->vertex_images.emplace_back();
  auto& image=impl_->vertex_images[impl_->used_vertex_images];
  // This slot has not been published in the current frame. Its old storage
  // retired at Reset after workers staged every previous-frame reference.
  // Swap preserves the new bytes until replay without a producer-side memcpy.
  image.swap(bytes);
  binding={nullptr,stride,0,&image};
  ++impl_->used_vertex_images;
}
void NativeParallelRecorder::SetBlendFactor(const std::array<float,4>& f) {
  auto& s=impl_->state;
  if(s.bindings.blend!=f) { s.bindings.blend=f; s.snapshot=nullptr; }
}
void NativeParallelRecorder::SetConstants(NativeBackendStage stage,uint32_t slot,std::span<const uint8_t> bytes) {
  auto& state=impl_->state;
  auto& binding=state.constants.at(uint32_t(stage)).at(slot);
  const auto* pipeline=state.bindings.pipeline;
  if(impl_->reuse_world_constants && stage==NativeBackendStage::Vertex &&
     state.bindings.world_instancing && pipeline && pipeline->world_instanced &&
     slot==pipeline->instance_world_slot && binding && binding->bytes.size()==bytes.size()) {
    const auto offset=pipeline->instance_world_offset;
    if(offset+64<=bytes.size() &&
       std::memcmp(binding->bytes.data(),bytes.data(),offset)==0 &&
       std::memcmp(binding->bytes.data()+offset+64,bytes.data()+offset+64,bytes.size()-offset-64)==0) {
      state.world.active=true; state.world.slot=slot; state.world.offset=offset;
      std::memcpy(state.world.bytes.data(),bytes.data()+offset,64);
      ++impl_->stats.world_constant_reuses;
      return;
    }
  }
  if(stage==NativeBackendStage::Vertex && state.world.active && state.world.slot==slot)
    state.world.active=false; // Full replacement already includes the current matrix.
  binding=impl_->Intern(bytes,binding,impl_->slot_history[uint32_t(stage)][slot]);
}
void NativeParallelRecorder::SetTexture(NativeBackendStage stage,uint32_t slot,NativeBackendTexture* t) {
  auto& s=impl_->state; const auto i=uint32_t(stage);
  auto& b=s.bindings;
  auto& texture=b.textures.at(i).at(slot);
  if(texture!=t || b.texture_counts.at(i)<slot+1) {
    texture=t; b.texture_counts.at(i)=std::max(b.texture_counts.at(i),slot+1); s.snapshot=nullptr;
  }
}
void NativeParallelRecorder::SetSampler(NativeBackendStage stage,uint32_t slot,NativeBackendSampler* sampler) {
  auto& s=impl_->state; const auto i=uint32_t(stage);
  auto& b=s.bindings;
  auto& value=b.samplers.at(i).at(slot);
  if(value!=sampler || b.sampler_counts.at(i)<slot+1) {
    value=sampler; b.sampler_counts.at(i)=std::max(b.sampler_counts.at(i),slot+1); s.snapshot=nullptr;
  }
}
void NativeParallelRecorder::SetRenderTargets(std::span<NativeBackendRenderTarget* const> colors,NativeBackendRenderTarget* depth) {
  if(colors.size()>8) throw std::runtime_error("too many render targets in a draw packet");
  auto& s=impl_->state; auto& b=s.bindings;
  if(b.color_count==colors.size() && b.depth==depth &&
     std::equal(colors.begin(),colors.end(),b.colors.begin())) return;
  b.colors={}; std::copy(colors.begin(),colors.end(),b.colors.begin());
  b.color_count=uint32_t(colors.size()); b.depth=depth; s.snapshot=nullptr;
}
void NativeParallelRecorder::SetViewport(const NativeBackendViewport& v) {
  auto& s=impl_->state;
  if(!s.bindings.viewport || std::memcmp(&*s.bindings.viewport,&v,sizeof(v))) {
    s.bindings.viewport=v; s.snapshot=nullptr;
  }
}
void NativeParallelRecorder::SetScissor(const NativeBackendScissor& value,bool enabled) {
  auto& s=impl_->state;
  if(s.bindings.scissor_enabled!=enabled || std::memcmp(&s.bindings.scissor,&value,sizeof(value))) {
    s.bindings.scissor=value; s.bindings.scissor_enabled=enabled; s.snapshot=nullptr;
  }
}
void NativeParallelRecorder::Draw(uint32_t n,uint32_t first) { impl_->Draw(false,n,1,first,0,0); }
void NativeParallelRecorder::DrawIndexed(uint32_t n,uint32_t first,int32_t base) { impl_->Draw(true,n,1,first,base,0); }
void NativeParallelRecorder::DrawIndexedInstanced(uint32_t n,uint32_t instances,uint32_t first,int32_t base,uint32_t initial) { impl_->Draw(true,n,instances,first,base,initial); }
void NativeParallelRecorder::ClearColor(NativeBackendRenderTarget& t,const std::array<float,4>& color) { Flush(); impl_->recorders[0]->ClearColor(t,color); impl_->serial_previous.reset(); }
void NativeParallelRecorder::ClearDepthStencil(NativeBackendRenderTarget& t,bool depth,bool stencil,float v,uint8_t s) { Flush(); impl_->recorders[0]->ClearDepthStencil(t,depth,stencil,v,s); impl_->serial_previous.reset(); }
void NativeParallelRecorder::CopyTexture(NativeBackendTexture& to,NativeBackendTexture& from) { Flush(); impl_->recorders[0]->CopyTexture(to,from); impl_->serial_previous.reset(); }
void NativeParallelRecorder::ReleaseSharedTexture(NativeBackendTexture& t) { Flush(); impl_->recorders[0]->ReleaseSharedTexture(t); impl_->serial_previous.reset(); }
void NativeParallelRecorder::CopyToShared(NativeBackendSharedSurface& to,NativeBackendRenderTarget& from) { Flush(); impl_->recorders[0]->CopyToShared(to,from); impl_->serial_previous.reset(); }
void NativeParallelRecorder::ResolveTarget(NativeBackendTexture& to,NativeBackendRenderTarget& from) { Flush(); impl_->recorders[0]->ResolveTarget(to,from); impl_->serial_previous.reset(); }
void NativeParallelRecorder::UpdateBuffer(NativeBackendBuffer& b,uint32_t offset,std::span<const uint8_t> bytes) {
  Flush(); impl_->recorders[0]->UpdateBuffer(b,offset,bytes);
  // Copying changes the buffer's resource state even though its identity is
  // stable. Force the next draw to re-establish its vertex/index transition.
  if(auto& previous=impl_->serial_previous) {
    for(auto& vertex:previous->vertices) if(vertex.buffer==&b) vertex={};
    if(previous->indices==&b) previous->indices=nullptr;
  }
}
void NativeParallelRecorder::UpdateTexture(NativeBackendTexture& t,std::span<const uint8_t> bytes) { Flush(); impl_->recorders[0]->UpdateTexture(t,bytes); impl_->serial_previous.reset(); }
void NativeParallelRecorder::BeginQuery(NativeBackendQuery& q) { Flush(); impl_->queries.push_back(&q); impl_->recorders[0]->BeginQuery(q); }
void NativeParallelRecorder::EndQuery(NativeBackendQuery& q) { Flush(); impl_->recorders[0]->EndQuery(q); std::erase(impl_->queries,&q); }
void NativeParallelRecorder::WriteTimestamp(NativeBackendTimestamps& set,uint32_t slot) {
  impl_->Mark({0,&set,slot,1,false});
}
void NativeParallelRecorder::ResolveTimestamps(NativeBackendTimestamps& set,uint32_t first,uint32_t count) {
  impl_->Mark({0,&set,first,count,true});
}
void NativeParallelRecorder::PushState() { impl_->stack.push_back(impl_->state); }
void NativeParallelRecorder::PopState() {
  if(impl_->stack.empty()) throw std::runtime_error("parallel recorder state stack underflow");
  impl_->state=std::move(impl_->stack.back()); impl_->stack.pop_back();
}
}
