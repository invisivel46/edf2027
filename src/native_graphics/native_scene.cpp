#include "native_scene.h"
#include "native_camera_history.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace edf::native {
namespace {
bool Finite(const NativeSceneMatrix& m) {
  return std::all_of(m.begin(),m.end(),[](float x) { return std::isfinite(x); });
}
void Validate(const NativeSceneObject& object) {
  if(!object.geometry || !object.material || object.geometry->backend()!=object.material->backend())
    throw std::runtime_error("scene object needs geometry/material from the same backend");
  if(!Finite(object.world) || object.world[3]!=0 || object.world[7]!=0 ||
     object.world[11]!=0 || object.world[15]!=1)
    throw std::runtime_error("scene object needs a finite affine world transform");
  if(object.bounds) for(size_t i=0;i<3;++i)
    if(!std::isfinite(object.bounds->minimum[i]) || !std::isfinite(object.bounds->maximum[i]) ||
       object.bounds->minimum[i]>object.bounds->maximum[i])
      throw std::runtime_error("invalid native scene bounds");
}
NativeSceneMatrix Multiply(const NativeSceneMatrix& a,const NativeSceneMatrix& b) {
  NativeSceneMatrix result{};
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col)
    for(size_t k=0;k<4;++k) result[row*4+col]+=a[row*4+k]*b[k*4+col];
  return result;
}
NativeSceneMatrix Interpolate(const NativeSceneMatrix& a,const NativeSceneMatrix& b,float alpha) {
  if(a==b || alpha>=1) return b;
  NativeCameraPose left{},right{};
  left.world=a; right.world=b; left.fov=right.fov=1;
  std::array<float,3> sa{},sb{};
  for(size_t row=0;row<3;++row) {
    double aa=0,bb=0;
    for(size_t col=0;col<3;++col) {
      aa+=double(a[row*4+col])*a[row*4+col];
      bb+=double(b[row*4+col])*b[row*4+col];
    }
    sa[row]=float(std::sqrt(aa)); sb[row]=float(std::sqrt(bb));
    if(!std::isfinite(sa[row]) || !std::isfinite(sb[row]) || sa[row]<1.e-6f || sb[row]<1.e-6f) return b;
    for(size_t col=0;col<3;++col) {
      left.world[row*4+col]/=sa[row]; right.world[row*4+col]/=sb[row];
    }
  }
  // Shears, reflections and discontinuities snap instead of producing an
  // invalid quaternion or sweeping an object across the world.
  if(!NativeCameraHistory::CanInterpolate(left,right)) return b;
  if(alpha<=0) return a;
  NativeCameraHistory history;
  history.Sample(left,0,1);
  auto result=history.Sample(right,1,alpha).world;
  for(size_t row=0;row<3;++row) for(size_t col=0;col<3;++col)
    result[row*4+col]*=sa[row]+(sb[row]-sa[row])*alpha;
  return result;
}
void Pack(uint8_t* destination,const NativeSceneMatrix& value,bool column_major) {
  if(!column_major) { std::memcpy(destination,value.data(),64); return; }
  NativeSceneMatrix transposed;
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) transposed[row*4+col]=value[col*4+row];
  std::memcpy(destination,transposed.data(),64);
}
bool Culled(const NativeSceneBounds& bounds,const NativeSceneMatrix& clip) {
  // Conservative homogeneous D3D clip-volume test. Works with normal/reversed
  // depth and boxes crossing the near plane; no divide by w is performed.
  uint32_t outside=63;
  for(uint32_t corner=0;corner<8;++corner) {
    std::array<float,4> p{},q{};
    for(size_t axis=0;axis<3;++axis) p[axis]=(corner&(1u<<axis))?bounds.maximum[axis]:bounds.minimum[axis];
    p[3]=1;
    for(size_t col=0;col<4;++col) for(size_t row=0;row<4;++row) q[col]+=p[row]*clip[row*4+col];
    if(!std::all_of(q.begin(),q.end(),[](float x) { return std::isfinite(x); })) return false;
    outside&=(q[0]<-q[3]?1u:0u)|(q[0]>q[3]?2u:0u)|
      (q[1]<-q[3]?4u:0u)|(q[1]>q[3]?8u:0u)|(q[2]<0?16u:0u)|(q[2]>q[3]?32u:0u);
  }
  return outside!=0;
}
}

NativeSceneMaterial::NativeSceneMaterial(std::shared_ptr<NativeRenderBackend> backend,
    NativeBackendPipeline& pipeline,std::vector<NativeSceneConstant> constants,
    std::vector<NativeSceneTexture> textures,std::vector<NativeSceneSampler> samplers,
    std::optional<std::array<float,4>> blend_factor)
    :backend_(std::move(backend)),pipeline_(&pipeline),constants_(std::move(constants)),
     textures_(std::move(textures)),blend_factor_(blend_factor) {
  if(!backend_) throw std::runtime_error("scene material needs its owning backend");
  if(pipeline.requires_blend_factor() && !blend_factor_)
    throw std::runtime_error("scene material needs a blend factor");
  if(blend_factor_ && !std::all_of(blend_factor_->begin(),blend_factor_->end(),[](float x) { return std::isfinite(x); }))
    throw std::runtime_error("nonfinite scene blend factor");
  uint32_t slots[2]{},worlds=0,world_dependencies=0;
  for(const auto& constant:constants_) {
    const auto stage=uint32_t(constant.stage);
    if(stage>1 || constant.slot>=14 || constant.bytes.empty() || constant.bytes.size()%16 || constant.bytes.size()>65536)
      throw std::runtime_error("invalid scene constant buffer");
    if(slots[stage]&(1u<<constant.slot)) throw std::runtime_error("duplicate scene constant slot");
    slots[stage]|=1u<<constant.slot;
    std::vector<bool> occupied(constant.bytes.size()/16);
    for(const auto& matrix:constant.matrices) {
      if(matrix.offset%16 || matrix.offset>constant.bytes.size() || constant.bytes.size()-matrix.offset<64 ||
         uint32_t(matrix.source)>uint32_t(NativeSceneMatrixSource::ViewTranspose))
        throw std::runtime_error("invalid scene matrix binding");
      for(size_t row=0;row<4;++row) {
        auto at=matrix.offset/16+row;
        if(occupied[at]) throw std::runtime_error("overlapping scene matrix bindings");
        occupied[at]=true;
      }
      if(matrix.source==NativeSceneMatrixSource::WorldViewProjection) ++world_dependencies;
      if(matrix.source==NativeSceneMatrixSource::World) {
        ++worlds;
        if(stage==0 && constant.slot==pipeline.instance_world_slot && matrix.offset==pipeline.instance_world_offset)
          instance_world_=matrix;
      }
    }
  }
  if(worlds!=1 || world_dependencies || !pipeline.world_instanced) instance_world_.reset();
  uint32_t texture_slots=0,sampler_slots=0;
  for(const auto& texture:textures_) {
    if(texture.slot>=16 || texture_slots&(1u<<texture.slot)) throw std::runtime_error("invalid scene texture slot");
    texture_slots|=1u<<texture.slot;
  }
  for(const auto& sampler:samplers) {
    if(sampler.slot>=16 || sampler_slots&(1u<<sampler.slot)) throw std::runtime_error("invalid scene sampler slot");
    sampler_slots|=1u<<sampler.slot;
    samplers_.emplace_back(sampler.slot,sampler.sampler);
  }
}
uint64_t NativeSceneMaterial::fingerprint() const {
  if(const auto cached=fingerprint_.load(std::memory_order_relaxed)) return cached;
  uint64_t fingerprint=1469598103934665603ull;
  const auto mix=[&](uint64_t value) { fingerprint^=value; fingerprint*=1099511628211ull; };
  mix(uintptr_t(backend_.get())); mix(uintptr_t(pipeline_));
  for(const auto& c:constants_) {
    mix(uint32_t(c.stage)); mix(c.slot); mix(c.bytes.size());
    for(auto byte:c.bytes) mix(byte);
    for(const auto& m:c.matrices) { mix(uint32_t(m.source)); mix(m.offset); mix(m.column_major); }
  }
  for(const auto& t:textures_) { mix(t.slot); mix(uintptr_t(t.texture.get())); }
  for(const auto& [slot,sampler]:samplers_) { mix(slot); mix(uintptr_t(sampler)); }
  if(blend_factor_) for(auto value:*blend_factor_) { uint32_t bits; std::memcpy(&bits,&value,4); mix(bits); }
  fingerprint_.store(fingerprint,std::memory_order_relaxed);
  return fingerprint;
}
bool NativeSceneMaterial::Equivalent(const NativeSceneMaterial& other) const {
  return backend_==other.backend_ && pipeline_==other.pipeline_ && constants_==other.constants_ &&
    textures_==other.textures_ && samplers_==other.samplers_ && blend_factor_==other.blend_factor_;
}

uint64_t NativeSceneDatabase::Create(NativeSceneObject object) {
  Validate(object);
  if(next_id_==(std::numeric_limits<uint64_t>::max)()) throw std::runtime_error("native scene identity exhausted");
  const auto id=next_id_;
  changed_.insert(id);
  objects_.emplace(id,Entry{std::move(object)});
  ++next_id_;
  return id;
}
void NativeSceneDatabase::Update(uint64_t id,NativeSceneObject object) {
  Validate(object);
  auto& entry=objects_.at(id);
  if(entry.object==object) return;
  changed_.insert(id);
  entry.object=std::move(object); entry.dirty=true;
}
bool NativeSceneDatabase::Remove(uint64_t id) {
  const auto found=objects_.find(id);
  if(found==objects_.end()) return false;
  if(by_id_.Find(id,NativeSceneInstanceId{})) changed_.insert(id); else changed_.erase(id);
  objects_.erase(found); return true;
}
void NativeSceneDatabase::Clear() {
  for(const auto& [id,entry]:objects_)
    if(by_id_.Find(id,NativeSceneInstanceId{})) changed_.insert(id); else changed_.erase(id);
  objects_.clear();
}
NativeSceneSnapshot NativeSceneDatabase::Select(std::span<const uint64_t> ids) {
  NativeSceneSnapshot result;
  for(auto id:ids) result.instances.push_back(SelectOne(id));
  return result;
}
std::shared_ptr<const NativeSceneInstance> NativeSceneDatabase::SelectOne(uint64_t id) {
  auto& entry=objects_.at(id);
  if(!entry.selected || entry.selected->object!=entry.object) {
    auto instance=std::make_shared<NativeSceneInstance>();
    instance->id=id; instance->object=entry.object; instance->previous=entry.object.world;
    entry.selected=std::move(instance);
  }
  return entry.selected;
}
std::shared_ptr<const NativeSceneSnapshot> NativeSceneDatabase::Publish(uint64_t tick) {
  const auto old=Acquire();
  if(old && tick<=old->tick) throw std::runtime_error("native scene ticks must increase");
  auto snapshot=std::make_shared<NativeSceneSnapshot>(); snapshot->tick=tick;
  // Work on copies: they share storage with the last publication, and writes
  // clone only the chunks holding changed ids.
  auto ordered=ordered_,by_id=by_id_;
  const NativeSceneInstanceOrder order{}; const NativeSceneInstanceId identity{};
  std::vector<std::pair<Entry*,std::shared_ptr<const NativeSceneInstance>>> changed;
  for(const auto id:changed_) {
    const auto* prior=by_id.Find(id,identity);
    const auto prior_order=prior?std::optional(order(*prior)):std::nullopt;
    const auto found=objects_.find(id);
    if(found==objects_.end()) {
      if(prior_order) { ordered.Erase(*prior_order,order); by_id.Erase(id,identity); }
      continue;
    }
    auto& entry=found->second;
    auto instance=entry.published;
    if(entry.dirty) {
      auto replacement=std::make_shared<NativeSceneInstance>();
      replacement->id=id; replacement->changed_tick=tick; replacement->object=entry.object;
      replacement->previous=entry.object.world;
      if(old && tick-old->tick==1 && instance && instance->object.geometry==entry.object.geometry &&
         instance->object.material==entry.object.material && instance->object.visible && entry.object.visible)
        replacement->previous=instance->object.world;
      instance=std::move(replacement);
      changed.emplace_back(&entry,instance);
    }
    if(!instance) continue;
    if(prior_order && *prior_order!=order(instance)) ordered.Erase(*prior_order,order);
    ordered.Assign(instance,order); by_id.Assign(instance,identity);
  }
  snapshot->instances=ordered;
  // No publication changes until all allocations and ordering have succeeded.
  for(auto& [entry,instance]:changed) { entry->published=std::move(instance); entry->dirty=false; }
  ordered_=std::move(ordered); by_id_=std::move(by_id); changed_.clear();
  published_.store(snapshot);
  return snapshot;
}

template<class Geometry,class Material,class World>
void NativeSceneRenderer::Record(NativeRenderBackend& backend,size_t count,const Geometry& geometry_of,
    const Material& material_of,const World& world_of,const NativeSceneView& view,const NativeSceneMatrix& vp,
    NativeSceneRenderStatistics& statistics) {
  auto& recorder=backend.Recorder();
  recorder.SetWorldInstancing(false);
  recorder.SetViewport(view.viewport); recorder.SetScissor(view.scissor,view.scissor_enabled);
  for(size_t first=0;first<count;) {
    const auto* geometry=geometry_of(first);
    const auto* material_pointer=material_of(first);
    const auto& material=*material_pointer;
    size_t end=first+1;
    if(material.instance_world_) while(end<count && end-first<256 &&
      geometry_of(end)==geometry && material_of(end)==material_pointer) ++end;
    const bool instanced=end-first>1;
    recorder.SetPipeline(instanced?*material.pipeline_->world_instanced:*material.pipeline_);
    if(material.blend_factor_) recorder.SetBlendFactor(*material.blend_factor_);
    for(const auto& texture:material.textures_) recorder.SetTexture(NativeBackendStage::Pixel,texture.slot,texture.texture.get());
    for(const auto& [slot,sampler]:material.samplers_) recorder.SetSampler(NativeBackendStage::Pixel,slot,sampler);
    for(const auto& constant:material.constants_) {
      if(constant.matrices.empty()) { recorder.SetConstants(constant.stage,constant.slot,constant.bytes); continue; }
      constants_scratch_=constant.bytes;
      for(const auto& matrix:constant.matrices) {
        auto value=kNativeSceneIdentity;
        switch(matrix.source) {
          case NativeSceneMatrixSource::World: value=world_of(first); break;
          case NativeSceneMatrixSource::View: value=view.view; break;
          case NativeSceneMatrixSource::Projection: value=view.projection; break;
          case NativeSceneMatrixSource::ViewProjection: value=vp; break;
          case NativeSceneMatrixSource::WorldViewProjection: value=Multiply(world_of(first),vp); break;
          case NativeSceneMatrixSource::ViewTranspose: value=NativeSceneTranspose(view.view); break;
        }
        Pack(constants_scratch_.data()+matrix.offset,value,matrix.column_major);
      }
      recorder.SetConstants(constant.stage,constant.slot,constants_scratch_);
    }
    if(instanced) {
      instances_scratch_.resize((end-first)*64);
      for(size_t i=first;i<end;++i)
        Pack(instances_scratch_.data()+(i-first)*64,world_of(i),material.instance_world_->column_major);
      recorder.SetTransientVertices(15,instances_scratch_,64);
      geometry->DrawInstanced(recorder,uint32_t(end-first));
      ++statistics.instanced_draws;
    } else geometry->Draw(recorder);
    ++statistics.draws; first=end;
  }
}
NativeSceneRenderStatistics NativeSceneRenderer::Render(NativeRenderBackend& backend,
    const NativeSceneSnapshot& snapshot,const NativeSceneView& view,float fraction) {
  if(!Finite(view.view) || !Finite(view.projection) || (view.view_projection && !Finite(*view.view_projection)))
    throw std::runtime_error("nonfinite native scene camera");
  fraction=std::isfinite(fraction)?std::clamp(fraction,0.f,1.f):1.f;
  const auto vp=view.view_projection?*view.view_projection:Multiply(view.view,view.projection);
  NativeSceneRenderStatistics statistics;
  visible_.clear(); visible_.reserve(snapshot.instances.size());
  for(const auto& instance:snapshot.instances) {
    if(!instance || !instance->object.geometry || !instance->object.material ||
       instance->object.geometry->backend()!=&backend || instance->object.material->backend()!=&backend)
      throw std::runtime_error("scene snapshot belongs to another backend");
    const auto& object=instance->object;
    if(!object.visible) continue;
    const auto world=instance->changed_tick==snapshot.tick?Interpolate(instance->previous,object.world,fraction):object.world;
    if(object.bounds && Culled(*object.bounds,Multiply(world,vp))) { ++statistics.culled; continue; }
    visible_.push_back({instance.get(),world});
  }
  statistics.visible=visible_.size();
  Record(backend,visible_.size(),[&](size_t i) { return visible_[i].instance->object.geometry.get(); },
    [&](size_t i) { return visible_[i].instance->object.material.get(); },
    [&](size_t i)->const NativeSceneMatrix& { return visible_[i].world; },view,vp,statistics);
  return statistics;
}
NativeSceneRenderStatistics NativeSceneRenderer::RenderUniform(NativeRenderBackend& backend,
    const NativeIndexedMesh::RetainedDraw& geometry,const NativeSceneMaterial& material,
    std::span<const NativeSceneMatrix> worlds,const NativeSceneView& view) {
  if(!Finite(view.view) || !Finite(view.projection) || (view.view_projection && !Finite(*view.view_projection)))
    throw std::runtime_error("nonfinite native scene camera");
  const auto vp=view.view_projection?*view.view_projection:Multiply(view.view,view.projection);
  NativeSceneRenderStatistics statistics;
  if(!worlds.empty() && (geometry.backend()!=&backend || material.backend()!=&backend))
    throw std::runtime_error("scene snapshot belongs to another backend");
  statistics.visible=worlds.size();
  Record(backend,worlds.size(),[&](size_t) { return &geometry; },[&](size_t) { return &material; },
    [&](size_t i)->const NativeSceneMatrix& { return worlds[i]; },view,vp,statistics);
  return statistics;
}
}
