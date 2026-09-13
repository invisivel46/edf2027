#include "d3d11_bindings.h"
#include "d3d11_backend.h"
#include "binding_runs.h"
#include <cstring>
#include <stdexcept>

namespace edf::native {
namespace {
void Require(HRESULT result, const char* message) {
  if (FAILED(result)) throw std::runtime_error(message);
}
}
ShaderBindings::ShaderBindings(ID3D11Device& device, NativeShader shader)
    : shader_(std::move(shader)) {
  D3D11_SHADER_DESC description{};
  Require(shader_.reflection->GetDesc(&description), "cannot reflect shader");
  for (UINT i = 0; i < description.BoundResources; ++i) {
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    Require(shader_.reflection->GetResourceBindingDesc(i, &binding), "cannot reflect resource");
    if (binding.BindCount != 1) throw std::runtime_error("resource arrays require an explicit binding adapter");
    if (binding.Type == D3D_SIT_CBUFFER) {
      auto* reflected = shader_.reflection->GetConstantBufferByName(binding.Name);
      D3D11_SHADER_BUFFER_DESC desc{};
      Require(reflected->GetDesc(&desc), "cannot reflect constant buffer");
      Buffer buffer{binding.BindPoint, std::vector<uint8_t>((desc.Size + 15u) & ~15u), {}};
      D3D11_BUFFER_DESC gpu_desc{};
      gpu_desc.ByteWidth = static_cast<UINT>(buffer.bytes.size());
      // Dynamic, so the upload below can discard-and-rename rather than
      // overwrite. That is what makes draws independent of each other: a draw
      // already recorded keeps the values it was given instead of seeing the
      // next material's. It is the prerequisite for recording draws on more
      // than one thread, and it is also what every other backend requires,
      // since neither D3D12 nor Vulkan renames anything on its own.
      gpu_desc.Usage = D3D11_USAGE_DYNAMIC;
      gpu_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      gpu_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      Require(device.CreateBuffer(&gpu_desc, nullptr, &buffer.gpu), "cannot create constant buffer");
      for (UINT v = 0; v < desc.Variables; ++v) {
        D3D11_SHADER_VARIABLE_DESC variable{};
        auto* reflected_variable = reflected->GetVariableByIndex(v);
        Require(reflected_variable->GetDesc(&variable), "cannot reflect constant");
        D3D11_SHADER_TYPE_DESC type{};
        Require(reflected_variable->GetType()->GetDesc(&type), "cannot reflect constant type");
        if (variable.StartOffset > buffer.bytes.size() || variable.Size > buffer.bytes.size() - variable.StartOffset)
          throw std::runtime_error("invalid reflected constant extent");
        if (variable.DefaultValue)
          std::memcpy(buffer.bytes.data() + variable.StartOffset, variable.DefaultValue, variable.Size);
        if (!variables_.emplace(variable.Name, Variable{buffers_.size(), variable.StartOffset, variable.Size, type, (variable.uFlags & D3D_SVF_USED) != 0}).second)
          throw std::runtime_error("ambiguous shader constant name");
      }
      buffers_.push_back(std::move(buffer));
    } else if (binding.Type == D3D_SIT_TEXTURE) {
      textures_.emplace(binding.Name, binding.BindPoint);
      texture_values_[binding.BindPoint] = nullptr;
    } else if (binding.Type == D3D_SIT_SAMPLER) {
      samplers_.emplace(binding.Name, binding.BindPoint);
      sampler_values_[binding.BindPoint] = nullptr;
    } else {
      throw std::runtime_error("unsupported native shader resource type");
    }
  }
  EmitBindingRuns<ID3D11ShaderResourceView*,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>(
    texture_values_,[](const auto&) -> ID3D11ShaderResourceView* { return nullptr; },
    [&](UINT slot,UINT count,auto) { texture_runs_.emplace_back(slot,count); });
  EmitBindingRuns<ID3D11SamplerState*,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT>(
    sampler_values_,[](const auto&) -> ID3D11SamplerState* { return nullptr; },
    [&](UINT slot,UINT count,auto) { sampler_runs_.emplace_back(slot,count); });
}
void ShaderBindings::SetConstant(const std::string& name, std::span<const uint8_t> bytes) {
  const auto found = variables_.find(name);
  if (found == variables_.end()) throw std::runtime_error("unknown constant: " + name);
  const auto& variable = found->second;
  if (bytes.size() != variable.size) throw std::runtime_error("constant layout mismatch: " + name);
  auto& buffer = buffers_.at(variable.buffer);
  auto* destination = buffer.bytes.data() + variable.offset;
  if (std::memcmp(destination, bytes.data(), bytes.size()) != 0) {
    std::memcpy(destination, bytes.data(), bytes.size());
    buffer.dirty = true;
  }
}
size_t ShaderBindings::GuestFloatRegisterBytes(const std::string& name) const {
  const auto found=variables_.find(name);
  if (found==variables_.end()) return 0;
  const auto& type=found->second.type;
  if (type.Type!=D3D_SVT_FLOAT || type.Members) throw std::runtime_error("unsupported guest float type: "+name);
  size_t vectors=1;
  switch (type.Class) {
    case D3D_SVC_SCALAR: case D3D_SVC_VECTOR: break;
    case D3D_SVC_MATRIX_ROWS: vectors=type.Rows; break;
    case D3D_SVC_MATRIX_COLUMNS: vectors=type.Columns; break;
    default: throw std::runtime_error("unsupported guest float class: "+name);
  }
  return vectors*(type.Elements?type.Elements:1)*16;
}
bool ShaderBindings::SetGuestFloatRegisters(const std::string& name, std::span<const uint8_t> registers) {
  return SetGuestFloatRegisters(ResolveFloatRegisters(name),registers);
}
ShaderBindings::FloatRegisterBinding ShaderBindings::ResolveFloatRegisters(const std::string& name) const {
  FloatRegisterBinding result;
  result.owner_=generation_; result.name_=name;
  const auto found = variables_.find(name);
  if (found == variables_.end()) return result;
  const auto& variable = found->second;
  const auto& type = variable.type;
  if (type.Type != D3D_SVT_FLOAT || type.Members)
    throw std::runtime_error("guest float upload has unsupported native type: " + name);
  UINT vectors = 1, components = type.Columns;
  switch (type.Class) {
    case D3D_SVC_SCALAR: components = 1; break;
    case D3D_SVC_VECTOR: break;
    case D3D_SVC_MATRIX_ROWS: vectors = type.Rows; break;
    case D3D_SVC_MATRIX_COLUMNS: vectors = type.Columns; components = type.Rows; break;
    default: throw std::runtime_error("unsupported guest constant class: " + name);
  }
  const size_t elements = type.Elements ? type.Elements : 1;
  const size_t slots = elements * vectors;
  if (!components || components > 4)
    throw std::runtime_error("invalid native constant components: "+name);
  if(slots && (slots-1)*16+components*4>variable.size)
    throw std::runtime_error("native constant packing overflow: " + name);
  result.variable_=variable; result.slots_=slots; result.components_=components;
  return result;
}
bool ShaderBindings::SetGuestFloatRegisters(const FloatRegisterBinding& binding,std::span<const uint8_t> registers) {
  if(!Owns(binding)) throw std::runtime_error("foreign native constant binding");
  if(!binding.slots_) return false;
  const auto& variable=binding.variable_;
  const auto& name=binding.name_;
  const auto slots=binding.slots_,components=binding.components_;
  if (registers.size() != slots * 16)
    throw std::runtime_error("guest constant register count mismatch: " + name + " guest=" +
                             std::to_string(registers.size() / 16) + " native=" + std::to_string(slots));
  // ResolveFloatRegisters already validated this immutable reflected extent.
  // Ownership and the live source size were checked above before mutation.
  auto& buffer = buffers_.at(variable.buffer);
  auto* destination = buffer.bytes.data() + variable.offset;
  for(size_t slot=0;slot<slots;++slot) {
    for(size_t lane=0;lane<components;++lane) {
      const size_t offset=slot*16+lane*4;
      const uint8_t packed[]{registers[offset+3],registers[offset+2],registers[offset+1],registers[offset]};
      if(std::memcmp(destination+offset,packed,4)!=0) {
        std::memcpy(destination+offset,packed,4);
        buffer.dirty=true;
      }
    }
    // Full uploads, unlike partial register patches, zero reflected padding.
    // The final array element need not occupy all16bytes in reflection.
    const size_t end=slot+1==slots?variable.size:(slot+1)*16;
    for(size_t offset=slot*16+components*4;offset<end;++offset) if(destination[offset]) {
      destination[offset]=0;
      buffer.dirty=true;
    }
  }
  return true;
}
bool ShaderBindings::PatchGuestFloatRegisters(const std::string& name, size_t first_slot,
                                             std::span<const uint8_t> registers) {
  return PatchGuestFloatRegisters(ResolveFloatRegisters(name),first_slot,registers);
}
bool ShaderBindings::PatchGuestFloatRegisters(const FloatRegisterBinding& binding,size_t first_slot,
                                             std::span<const uint8_t> registers) {
  if(!Owns(binding)) throw std::runtime_error("foreign native constant patch binding");
  if(!binding.slots_) return false;
  const auto slots=binding.slots_;
  const auto& name=binding.name_;
  if(registers.size()%16 || first_slot>slots || registers.size()/16>slots-first_slot)
    throw std::runtime_error("guest constant patch exceeds register range: "+name);
  const auto& variable=binding.variable_;
  const auto components=binding.components_;
  const size_t count=registers.size()/16;
  // Validate the furthest lane before mutating any bytes. All preceding lanes
  // are bounded by it; scalar/vector array tails need not occupy a full slot.
  if(count && (first_slot+count-1)*16+components*4>variable.size)
    throw std::runtime_error("native constant patch packing overflow");
  auto& buffer=buffers_.at(variable.buffer);
  auto* destination=buffer.bytes.data()+variable.offset;
  for(size_t slot=0;slot<count;++slot) for(size_t lane=0;lane<components;++lane) {
    const auto target=(first_slot+slot)*16+lane*4, source=slot*16+lane*4;
    const uint8_t packed[]{registers[source+3],registers[source+2],registers[source+1],registers[source]};
    if(std::memcmp(destination+target,packed,4)!=0) {
      std::memcpy(destination+target,packed,4);
      buffer.dirty=true;
    }
  }
  return true;
}
void ShaderBindings::SetTexture(const std::string& name, std::shared_ptr<NativeBackendTexture> texture) {
  if (!TrySetTexture(name, std::move(texture))) throw std::runtime_error("unknown texture: " + name);
}
bool ShaderBindings::TrySetTexture(const std::string& name, std::shared_ptr<NativeBackendTexture> texture) {
  const auto found = textures_.find(name);
  if (found == textures_.end()) return false;
  texture_slots_.at(found->second)=texture?NativeD3D11TextureView(*texture):nullptr;
  texture_values_.at(found->second) = std::move(texture);
  return true;
}
void ShaderBindings::ClearTextures() {
  for (auto& [slot, texture] : texture_values_) { texture.reset(); texture_slots_[slot]=nullptr; }
}
NativeBackendTexture* ShaderBindings::ReadTexture(const std::string& name) const {
  const auto found=textures_.find(name);
  return found==textures_.end() ? nullptr : texture_values_.at(found->second).get();
}
NativeBackendSampler* ShaderBindings::ReadSampler(const std::string& name) const {
  const auto found=samplers_.find(name);
  return found==samplers_.end() ? nullptr : sampler_values_.at(found->second);
}
ShaderBindings::ResourceBinding ShaderBindings::ResolveResource(const std::string& name) const {
  ResourceBinding binding; binding.owner_=generation_;
  if(const auto found=textures_.find(name);found!=textures_.end()) binding.texture_=found->second;
  if(const auto found=samplers_.find(name);found!=samplers_.end()) binding.sampler_=found->second;
  return binding;
}
bool ShaderBindings::TrySetTexture(const ResourceBinding& binding,std::shared_ptr<NativeBackendTexture> texture) {
  if(binding.owner_!=generation_) throw std::runtime_error("foreign native texture binding");
  if(!binding.texture_) return false;
  texture_slots_.at(*binding.texture_)=texture?NativeD3D11TextureView(*texture):nullptr;
  texture_values_.at(*binding.texture_)=std::move(texture);
  return true;
}
bool ShaderBindings::TrySetSampler(const ResourceBinding& binding,NativeBackendSampler* sampler) {
  if(binding.owner_!=generation_) throw std::runtime_error("foreign native sampler binding");
  if(!binding.sampler_) return false;
  sampler_values_.at(*binding.sampler_)=sampler;
  sampler_slots_.at(*binding.sampler_)=sampler?NativeD3D11SamplerState(*sampler):nullptr;
  return true;
}
void ShaderBindings::ClearSamplers() {
  for (auto& [slot, sampler] : sampler_values_) { sampler=nullptr; sampler_slots_[slot]=nullptr; }
}
void ShaderBindings::SetSampler(const std::string& name, NativeBackendSampler* sampler) {
  if (!TrySetSampler(name,sampler)) throw std::runtime_error("unknown sampler: " + name);
}
bool ShaderBindings::TrySetSampler(const std::string& name, NativeBackendSampler* sampler) {
  const auto found = samplers_.find(name);
  if (found == samplers_.end()) return false;
  sampler_values_.at(found->second) = sampler;
  sampler_slots_.at(found->second)=sampler?NativeD3D11SamplerState(*sampler):nullptr;
  return true;
}
bool ShaderBindings::HasAllTextureInputs() const {
  for (const auto& [slot,value]:texture_values_) if (!value) return false;
  for (const auto& [slot,value]:sampler_values_) if (!value) return false;
  return true;
}
bool ShaderBindings::UsesTextureResource(ID3D11Resource& resource) const {
  for(const auto& [slot,texture]:texture_values_) if(texture) {
    if(NativeD3D11TextureResource(*texture)==&resource) return true;
  }
  return false;
}
bool ShaderBindings::UsesTexture(const NativeBackendTexture& texture) const {
  for(const auto& [slot,bound]:texture_values_) if(bound.get()==&texture) return true;
  return false;
}
std::vector<ShaderBindings::TextureImage> ShaderBindings::TextureImages() const {
  std::vector<TextureImage> images;
  images.reserve(texture_values_.size());
  for(const auto& [slot,texture]:texture_values_) images.push_back({slot,texture.get()});
  return images;
}
std::vector<ShaderBindings::SamplerImage> ShaderBindings::SamplerImages() const {
  std::vector<SamplerImage> images;
  images.reserve(sampler_values_.size());
  for(const auto& [slot,sampler]:sampler_values_) images.push_back({slot,sampler});
  return images;
}
std::vector<float> ShaderBindings::ReadFloatVector(const std::string& name) const {
  const auto found=variables_.find(name);
  if (found==variables_.end()) return {};
  const auto& v=found->second;
  if (v.type.Type!=D3D_SVT_FLOAT || v.type.Elements || v.type.Members ||
      (v.type.Class!=D3D_SVC_SCALAR && v.type.Class!=D3D_SVC_VECTOR))
    throw std::runtime_error("diagnostic constant is not a float vector: "+name);
  std::vector<float> values(v.type.Columns);
  if (values.size()*sizeof(float)!=v.size) throw std::runtime_error("diagnostic float vector size mismatch");
  std::memcpy(values.data(),buffers_.at(v.buffer).bytes.data()+v.offset,v.size);
  return values;
}
bool ShaderBindings::ConsumesConstant(const std::string& name) const {
  const auto found=variables_.find(name);
  return found!=variables_.end() && found->second.used;
}
std::vector<float> ShaderBindings::ReadFloatArray(const std::string& name) const {
  const auto found=variables_.find(name);
  if (found==variables_.end()) return {};
  const auto& v=found->second;
  if (v.type.Type!=D3D_SVT_FLOAT || !v.type.Elements || v.type.Members || v.type.Rows!=1 ||
      !v.type.Columns || v.type.Columns>4 ||
      (v.type.Class!=D3D_SVC_SCALAR && v.type.Class!=D3D_SVC_VECTOR))
    throw std::runtime_error("diagnostic constant is not a float array: "+name);
  // Array elements are 16-byte aligned; the last element keeps only its used
  // columns. Reject any layout that does not match that exact contract.
  constexpr uint32_t stride=16;
  const uint32_t used=v.type.Columns*uint32_t(sizeof(float));
  if (v.size!=(v.type.Elements-1)*stride+used)
    throw std::runtime_error("diagnostic float array layout mismatch: "+name);
  const auto& bytes=buffers_.at(v.buffer).bytes;
  if (v.offset>bytes.size() || v.size>bytes.size()-v.offset)
    throw std::runtime_error("diagnostic float array outside its buffer: "+name);
  std::vector<float> values(size_t(v.type.Elements)*v.type.Columns);
  for (uint32_t element=0;element<v.type.Elements;++element)
    std::memcpy(values.data()+size_t(element)*v.type.Columns,
                bytes.data()+v.offset+size_t(element)*stride,used);
  return values;
}
std::optional<std::array<float,16>> ShaderBindings::ReadFloat4x4(const std::string& name) const {
  const auto found=variables_.find(name);
  if(found==variables_.end()) return std::nullopt;
  const auto& v=found->second;
  if(v.type.Type!=D3D_SVT_FLOAT || v.type.Elements || v.type.Members ||
      v.type.Rows!=4 || v.type.Columns!=4 || v.size!=64 ||
      (v.type.Class!=D3D_SVC_MATRIX_ROWS && v.type.Class!=D3D_SVC_MATRIX_COLUMNS))
    throw std::runtime_error("diagnostic constant is not a float4x4: "+name);
  std::array<float,16> values{};
  const auto* data=buffers_.at(v.buffer).bytes.data()+v.offset;
  for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column) {
    const auto slot=v.type.Class==D3D_SVC_MATRIX_ROWS ? row*4+column : column*4+row;
    std::memcpy(&values[row*4+column],data+slot*sizeof(float),sizeof(float));
  }
  return values;
}
std::vector<ShaderBindings::ConstantImage> ShaderBindings::ConstantImages() const {
  std::vector<ConstantImage> images;
  images.reserve(buffers_.size());
  for(const auto& buffer:buffers_) images.push_back({buffer.slot,buffer.bytes});
  return images;
}

void ShaderBindings::BindConstants(ID3D11DeviceContext& context) {
  for (auto& buffer : buffers_) {
    if (buffer.dirty) {
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (SUCCEEDED(context.Map(buffer.gpu.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, buffer.bytes.data(), buffer.bytes.size());
        context.Unmap(buffer.gpu.Get(), 0);
      }
      buffer.dirty = false;
    }
    auto* value = buffer.gpu.Get();
    if (shader_.entry.pixel) context.PSSetConstantBuffers(buffer.slot, 1, &value);
    else context.VSSetConstantBuffers(buffer.slot, 1, &value);
  }
}

void ShaderBindings::Bind(ID3D11DeviceContext& context) {
  if (shader_.entry.pixel) context.PSSetShader(shader_.pixel.Get(), nullptr, 0);
  else context.VSSetShader(shader_.vertex.Get(), nullptr, 0);
  for (auto& buffer : buffers_) {
    if (buffer.dirty) {
      // WRITE_DISCARD hands back a fresh version of the buffer; the one the
      // previous draw was given stays intact until that draw has executed.
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (SUCCEEDED(context.Map(buffer.gpu.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, buffer.bytes.data(), buffer.bytes.size());
        context.Unmap(buffer.gpu.Get(), 0);
      }
      buffer.dirty = false;
    }
    auto* value = buffer.gpu.Get();
    if (shader_.entry.pixel) context.PSSetConstantBuffers(buffer.slot, 1, &value);
    else context.VSSetConstantBuffers(buffer.slot, 1, &value);
  }
  // Bind nulls too, to avoid inheriting a previous material's resource.
  for(const auto& [slot,count]:texture_runs_) {
      auto* values=texture_slots_.data()+slot;
      if(shader_.entry.pixel) context.PSSetShaderResources(slot,count,values);
      else context.VSSetShaderResources(slot,count,values);
  }
  for(const auto& [slot,count]:sampler_runs_) {
      auto* values=sampler_slots_.data()+slot;
      if(shader_.entry.pixel) context.PSSetSamplers(slot,count,values);
      else context.VSSetSamplers(slot,count,values);
  }
}
}  // namespace edf::native
