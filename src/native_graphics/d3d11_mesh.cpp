#include "d3d11_mesh.h"
#include <chrono>
#include "d3d11_backend.h"
#include "native_input_layout.h"
#include "native_declarations.h"
#include "native_generated_indices.h"
#include "native_reuse.h"
#include "native_transient_batching.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>

namespace edf::native {
namespace {
uint32_t BigWord(std::span<const uint8_t> bytes,size_t at) {
  return (uint32_t(bytes[at])<<24)|(uint32_t(bytes[at+1])<<16)|(uint32_t(bytes[at+2])<<8)|bytes[at+3];
}
const char* Semantic(uint8_t usage) {
  switch (usage) {
    case 0: return "POSITION";
    case 1: return "BLENDWEIGHT";
    case 2: return "BLENDINDICES";
    case 3: return "NORMAL";
    case 4: return "PSIZE";
    case 5: return "TEXCOORD";
    case 6: return "TANGENT";
    case 7: return "BINORMAL";
    case 10: return "COLOR";
    default: throw std::runtime_error("unsupported mesh vertex semantic");
  }
}
}
NativeIndexBuffer::NativeIndexBuffer(NativeRenderBackend& backend,std::span<const uint8_t> bytes,uint32_t width,
    std::shared_ptr<const std::vector<uint8_t>> contents)
    : backend_(&backend),width_(width),format_(width==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT) {
  if ((width!=2 && width!=4) || bytes.empty() || bytes.size()%width || bytes.size()>128*1024*1024)
    throw std::runtime_error("invalid native index resource");
  if(contents && (contents->size()!=bytes.size() || contents->data()!=bytes.data()))
    throw std::runtime_error("native index contents do not own supplied bytes");
  source_=contents?std::move(contents):std::make_shared<const std::vector<uint8_t>>(bytes.begin(),bytes.end());
  bytes=*source_; // Convert the owned snapshot, never reread mutable guest bytes.
  std::vector<uint8_t> converted(bytes.size());
  values_.reserve(bytes.size()/width);
  for(size_t at=0;at<bytes.size();at+=width) {
    uint32_t value=0;
    for(uint32_t lane=0;lane<width;++lane) {
      value=(value<<8)|bytes[at+lane];
      converted[at+lane]=bytes[at+width-1-lane];
    }
    values_.push_back(value);
    minimum_=(std::min)(minimum_,value); maximum_=(std::max)(maximum_,value);
  }
  NativeBackendBufferDesc desc{};
  desc.bytes=converted.size();
  desc.index=true;
  storage_=backend.CreateBuffer(desc,converted);
  if(!storage_) throw std::runtime_error("native index buffer creation failed");
  // The same buffer, for the draw, which is still D3D11. Null on a backend
  // that is not, which BindAndDraw refuses rather than binding nothing.
  buffer_=NativeD3D11Buffer(*storage_);
}
bool NativeIndexBuffer::Matches(NativeRenderBackend& backend,std::span<const uint8_t> bytes,uint32_t width) const {
  return OwnsSource(backend,bytes,width) || (backend_==&backend && width_==width && source_->size()==bytes.size() &&
    std::equal(source_->begin(),source_->end(),bytes.begin()));
}
NativeIndexedMesh::NativeIndexedMesh(NativeRenderBackend& backend,const NativeShader& shader,
  std::span<const uint8_t> declaration,uint32_t stride,std::span<const uint8_t> vertices,
  std::span<const uint8_t> indices,uint32_t index_bytes,bool dynamic_vertices,std::shared_ptr<const NativeIndexBuffer> index_storage,
  std::shared_ptr<NativeVertexBuffer> vertex_storage,IndexReuse index_reuse,
  std::shared_ptr<const std::vector<uint8_t>> vertex_contents,size_t contents_offset,
  std::shared_ptr<const std::vector<uint8_t>> index_contents) : stride_(stride) {
  if(index_contents && (index_contents->size()!=indices.size() || index_contents->data()!=indices.data()))
    throw std::runtime_error("native index contents do not own supplied bytes");
  if (shader.entry.pixel || !shader.bytecode || declaration.empty() ||
      declaration.size()%12 || declaration.size()>64*12 || !stride || stride>2048 ||
      vertices.empty() || vertices.size()%stride || vertices.size()>128*1024*1024 ||
      (index_bytes!=2 && index_bytes!=4) || indices.empty() || indices.size()%index_bytes || indices.size()>128*1024*1024)
    throw std::runtime_error("invalid native indexed mesh input");
  // Built neutrally and converted just below, so the same layout can describe
  // a pipeline on any backend instead of only a D3D11 input layout.
  NativeOwnedInputLayout layout;
  std::vector<NativeVertexAttribute> attributes;
  UINT native_stride=0;
  std::vector<bool> occupied(stride,false);
  std::set<std::pair<uint8_t,uint8_t>> semantics;
  for (size_t at=0;at<declaration.size();at+=12) {
    const auto stream_offset=BigWord(declaration,at),type=BigWord(declaration,at+4);
    const auto offset=stream_offset&65535;
    if ((stream_offset>>16)!=0 || declaration[at+8]!=0 || offset%4)
      throw std::runtime_error("unsupported mesh stream/method/alignment");
    UINT components;
    DXGI_FORMAT format;
    switch (type) {
      case 0x2c83a4: components=1; format=DXGI_FORMAT_R32_FLOAT; break;
      case 0x2c23a5: components=2; format=DXGI_FORMAT_R32G32_FLOAT; break;
      case 0x2a23b9: components=3; format=DXGI_FORMAT_R32G32B32_FLOAT; break;
      case 0x1a23a6: components=4; format=DXGI_FORMAT_R32G32B32A32_FLOAT; break;
      case 0x1a2286: case 0x182886: // UBYTE4 and normalized D3DCOLOR.
        components=4; format=DXGI_FORMAT_R32G32B32A32_FLOAT; break;
      default: throw std::runtime_error("packed/non-float mesh attributes not implemented");
    }
    const bool packed=type==0x1a2286 || type==0x182886;
    const UINT guest_bytes=packed?4:components*4;
    if (offset>stride || guest_bytes>stride-offset) throw std::runtime_error("mesh attribute exceeds vertex stride");
    for (UINT b=0;b<guest_bytes;++b) {
      if (occupied[offset+b]) throw std::runtime_error("overlapping mesh attributes");
      occupied[offset+b]=true;
    }
    const auto usage=declaration[at+9],index=declaration[at+10];
    if (!semantics.emplace(usage,index).second) throw std::runtime_error("duplicate mesh vertex semantic");
    bool integer=false;
    if (type==0x1a2286) {
      D3D11_SHADER_DESC description{};
      if (!shader.reflection || FAILED(shader.reflection->GetDesc(&description)))
        throw std::runtime_error("missing mesh input reflection");
      for (UINT i=0;i<description.InputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if (FAILED(shader.reflection->GetInputParameterDesc(i,&input))) throw std::runtime_error("cannot reflect packed mesh input");
        if (_stricmp(input.SemanticName,Semantic(usage)) || input.SemanticIndex!=index) continue;
        if (input.ComponentType==D3D_REGISTER_COMPONENT_SINT32) {
          format=DXGI_FORMAT_R32G32B32A32_SINT; integer=true;
        } else if (input.ComponentType==D3D_REGISTER_COMPONENT_UINT32) {
          format=DXGI_FORMAT_R32G32B32A32_UINT; integer=true;
        }
      }
    }
    layout.Add(Semantic(usage),index,format,0,native_stride);
    attributes.push_back({offset,native_stride,components,type,integer});
    native_stride+=components*4;
  }
  // sub_8213E070 searches declaration usage/index for each shader fetch. Its
  // missing-semantic branch (8213E140) uses 0x9250 and the table at 82009964
  // to select constant (0,0,0,1). Preserve that rule without guest GPU fetches.
  D3D11_SHADER_DESC input_description{};
  if (!shader.reflection || FAILED(shader.reflection->GetDesc(&input_description)))
    throw std::runtime_error("missing mesh input reflection");
  for (UINT i=0;i<input_description.InputParameters;++i) {
    D3D11_SIGNATURE_PARAMETER_DESC input{};
    if (FAILED(shader.reflection->GetInputParameterDesc(i,&input))) throw std::runtime_error("cannot reflect mesh input");
    if (input.SystemValueType!=D3D_NAME_UNDEFINED) continue;
    bool supplied=false;
    for (const auto& element:layout.elements())
      if (!_stricmp(element.semantic,input.SemanticName) && element.semantic_index==input.SemanticIndex) supplied=true;
    if (supplied) continue;
    bool known=false;
    for (uint8_t usage : {0,1,2,3,4,5,6,7,10})
      if (!_stricmp(Semantic(usage),input.SemanticName)) known=true;
    if (!known || input.SemanticIndex>15) throw std::runtime_error("unsupported missing vertex semantic");
    DXGI_FORMAT format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    const bool integer=input.ComponentType!=D3D_REGISTER_COMPONENT_FLOAT32;
    if (input.ComponentType==D3D_REGISTER_COMPONENT_SINT32) format=DXGI_FORMAT_R32G32B32A32_SINT;
    else if (input.ComponentType==D3D_REGISTER_COMPONENT_UINT32) format=DXGI_FORMAT_R32G32B32A32_UINT;
    else if (integer) throw std::runtime_error("unsupported default vertex component type");
    layout.Add(input.SemanticName,input.SemanticIndex,format,0,native_stride);
    attributes.push_back({0,native_stride,4,0,integer,true});
    native_stride+=16;
  }
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  elements.reserve(layout.size());
  for(const auto& element:layout.elements())
    elements.push_back({element.semantic,element.semantic_index,
                        static_cast<DXGI_FORMAT>(element.format),element.slot,element.offset,
                        element.per_instance?D3D11_INPUT_PER_INSTANCE_DATA:D3D11_INPUT_PER_VERTEX_DATA,
                        element.step_rate});
  // A D3D11 input layout, for the direct draw. On a backend with no device to
  // make one the layout travels in the pipeline instead, and a direct draw is
  // already refused there for want of D3D11 buffers - so its absence is the
  // normal state, not a failure.
  //
  // This is also the only place the layout is checked against the shader's
  // input signature, which is why the error below is worth as much as it is.
  // The other backends check the same thing when they build the pipeline.
  auto* d3d11_device=NativeD3D11BackendDevice(backend);
  const auto layout_result=d3d11_device
    ? d3d11_device->CreateInputLayout(elements.data(),static_cast<UINT>(elements.size()),
        shader.bytecode->GetBufferPointer(),shader.bytecode->GetBufferSize(),&layout_)
    : S_OK;
  if (FAILED(layout_result)) {
    std::ostringstream message;
    message << "native mesh shader input layout mismatch: entry=" << shader.entry.name
            << " HRESULT=0x" << std::hex << uint32_t(layout_result) << std::dec
            << " guest_stride=" << stride << " supplied=[";
    for (size_t i=0;i<layout.size();++i) {
      const auto& e=layout.elements()[i]; const auto& a=attributes[i];
      message << e.semantic << e.semantic_index << ":format=" << e.format
              << ":guest_offset=" << a.guest_offset << ":type=0x" << std::hex << a.type << std::dec << ' ';
    }
    message << "] required=[";
    D3D11_SHADER_DESC description{};
    if (shader.reflection && SUCCEEDED(shader.reflection->GetDesc(&description))) {
      for (UINT i=0;i<description.InputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if (FAILED(shader.reflection->GetInputParameterDesc(i,&input))) continue;
        message << input.SemanticName << input.SemanticIndex << ":type=" << input.ComponentType
                << ":mask=" << unsigned(input.Mask) << ":used=" << unsigned(input.ReadWriteMask) << ' ';
      }
    }
    message << ']';
    throw std::runtime_error(message.str());
  }
  input_layout_=std::move(layout);
  vertex_count_=static_cast<uint32_t>(vertices.size()/stride);
  stride_=native_stride;
  if(index_storage && !index_storage->Matches(backend,indices,index_bytes)) {
    if(index_reuse==IndexReuse::RequireMatch)
      throw std::runtime_error("native index resource generation mismatch");
    index_storage.reset();
  }
  index_storage_=index_storage?std::move(index_storage):std::make_shared<NativeIndexBuffer>(backend,indices,index_bytes,std::move(index_contents));
  // Sharing is allowed only for exactly the same conversion contract and bytes.
  // In particular integer blend indices and default semantics affect packing.
  std::shared_ptr<const std::vector<uint8_t>> source_snapshot;
  size_t snapshot_offset=0;
  if(vertex_storage) {
    const bool shareable=!dynamic_vertices && !vertex_storage->dynamic_vertices_;
    // Validate live bytes once even if the GPU conversion layout differs. A
    // layout mismatch need not duplicate the immutable CPU content generation.
    if(shareable && vertex_storage->MatchesSource(vertices)) {
      source_snapshot=vertex_storage->SourceSnapshot();
      snapshot_offset=vertex_storage->SourceOffset();
    }
    if(!source_snapshot || vertex_storage->backend_!=&backend ||
       vertex_storage->guest_stride_!=stride || vertex_storage->stride_!=native_stride ||
       vertex_storage->attributes_!=attributes) vertex_storage.reset();
  }
  if(!source_snapshot && !dynamic_vertices && vertex_contents &&
     contents_offset<=vertex_contents->size() &&
     vertices.size()<=vertex_contents->size()-contents_offset &&
     (vertices.data()==vertex_contents->data()+contents_offset ||
      std::equal(vertices.begin(),vertices.end(),vertex_contents->begin()+contents_offset))) {
    source_snapshot=std::move(vertex_contents);
    snapshot_offset=contents_offset;
  }
  vertex_storage_=vertex_storage?std::move(vertex_storage):std::shared_ptr<NativeVertexBuffer>(
    new NativeVertexBuffer(backend,std::move(attributes),stride,native_stride,vertices,dynamic_vertices,std::move(source_snapshot),snapshot_offset));
}
NativeVertexBuffer::NativeVertexBuffer(NativeRenderBackend& backend,std::vector<NativeVertexAttribute> attributes,
    uint32_t guest_stride,uint32_t native_stride,std::span<const uint8_t> source,bool dynamic,
    std::shared_ptr<const std::vector<uint8_t>> validated_snapshot,size_t snapshot_offset)
    : backend_(&backend),attributes_(std::move(attributes)),source_(validated_snapshot?std::move(validated_snapshot):
        std::make_shared<const std::vector<uint8_t>>(source.begin(),source.end())),
      source_offset_(snapshot_offset),source_bytes_(source.size()),
      guest_stride_(guest_stride),stride_(native_stride),vertex_count_(static_cast<uint32_t>(source.size()/guest_stride)),
      dynamic_vertices_(dynamic) {
  if(source_offset_>source_->size() || source_bytes_>source_->size()-source_offset_)
    throw std::runtime_error("invalid native vertex snapshot range");
  const auto converted=ConvertVertices(std::span<const uint8_t>(*source_).subspan(source_offset_,source_bytes_));
  NativeBackendBufferDesc desc{};
  desc.bytes=converted.size();
  desc.vertex=true;
  // Immediate geometry is rewritten whole every frame while earlier draws are
  // still queued against it, which is exactly what the seam's dynamic flag
  // means; the backend decides how to stage that.
  desc.dynamic=dynamic;
  storage_=backend.CreateBuffer(desc,converted);
  if(!storage_) throw std::runtime_error("native vertex buffer creation failed");
  buffer_=NativeD3D11Buffer(*storage_);
}
bool NativeVertexBuffer::MatchesSource(std::span<const uint8_t> bytes) const {
  return OwnsSource(bytes) || (source_bytes_==bytes.size() &&
    std::equal(bytes.begin(),bytes.end(),source_->begin()+source_offset_));
}
std::vector<uint8_t> NativeVertexBuffer::ConvertVertices(std::span<const uint8_t> vertices) const {
  std::vector<uint8_t> native_vertices;
  ConvertVerticesInto(vertices,native_vertices);
  return native_vertices;
}
void NativeVertexBuffer::ConvertVerticesInto(std::span<const uint8_t> vertices,std::vector<uint8_t>& native_vertices) const {
  if(vertices.size()!=size_t(vertex_count_)*guest_stride_)
    throw std::runtime_error("native mesh vertex update size mismatch");
  ConvertVertexPrefixInto(vertices,vertex_count_,native_vertices);
}
void NativeVertexBuffer::ConvertVertexPrefixInto(std::span<const uint8_t> vertices,uint32_t vertex_count,
                                                 std::vector<uint8_t>& native_vertices) const {
  if(vertex_count>vertex_count_ || vertices.size()!=size_t(vertex_count)*guest_stride_)
    throw std::runtime_error("native mesh vertex prefix size mismatch");
  const auto native_stride=stride_;
  const size_t native_size=size_t(vertex_count)*native_stride;
  if (native_size>128*1024*1024) throw std::runtime_error("expanded native mesh exceeds size limit");
  native_vertices.assign(native_size,0);
  for (size_t vertex=0;vertex<vertex_count;++vertex) for (const auto& attribute:attributes_) {
    const auto source=vertex*guest_stride_+attribute.guest_offset;
    const auto target=vertex*native_stride+attribute.host_offset;
    for (UINT c=0;c<attribute.components;++c) {
      uint32_t value;
      if (attribute.defaulted) {
        value=attribute.integer ? uint32_t(c==3) : std::bit_cast<uint32_t>(c==3?1.0f:0.0f);
      } else if (attribute.type==0x1a2286 || attribute.type==0x182886) {
        // Both verified declarations specify 8-in-32 endian conversion.
        // UBYTE4 swizzles 0,1,2,3; D3DCOLOR swizzles 2,1,0,3 and normalizes.
        const auto channel=attribute.type==0x182886 && c<3 ? 2-c : c;
        const auto byte=(BigWord(vertices,source)>>(channel*8))&255;
        value=attribute.integer ? byte : std::bit_cast<uint32_t>(float(byte)/(attribute.type==0x182886?255.0f:1.0f));
      } else value=BigWord(vertices,source+c*4);
      // Host order, which is what the byte-at-a-time store this replaces
      // produced on the little-endian hosts this runs on.
      std::memcpy(native_vertices.data()+target+c*4,&value,4);
    }
  }
}
void NativeIndexedMesh::UpdateVertices(ID3D11DeviceContext& context,std::span<const uint8_t> vertices) {
  vertex_storage_->Update(context,vertices);
}
void NativeIndexedMesh::UpdateVertices(NativeBackendRecorder& recorder,std::span<const uint8_t> vertices) {
  vertex_storage_->Update(recorder,vertices);
}
void NativeVertexBuffer::Update(NativeBackendRecorder& recorder,std::span<const uint8_t> vertices) {
  if(!dynamic_vertices_) throw std::runtime_error("cannot update immutable native mesh");
  auto snapshot=std::make_shared<const std::vector<uint8_t>>(vertices.begin(),vertices.end());
  const auto converted=ConvertVertices(*snapshot);
  // Whole-buffer, which is what a dynamic buffer takes: the backend renames the
  // storage and the draws already recorded keep the vertices they were given.
  recorder.UpdateBuffer(*storage_,0,converted);
  source_=std::move(snapshot);
  source_offset_=0; source_bytes_=vertices.size();
}
void NativeVertexBuffer::Update(ID3D11DeviceContext& context,std::span<const uint8_t> vertices) {
  if(!dynamic_vertices_) throw std::runtime_error("cannot update immutable native mesh");
  // Still the immediate context, not the recorder: the recorder needs an open
  // frame, and the scene backend does not drive frames yet. The storage is the
  // seam's; only the write is D3D11's.
  if(!buffer_) throw std::runtime_error("this mesh's vertices are not on a D3D11 backend and cannot be updated through a context");
  Microsoft::WRL::ComPtr<ID3D11Device> buffer_device,context_device;
  buffer_->GetDevice(&buffer_device); context.GetDevice(&context_device);
  if(buffer_device.Get()!=context_device.Get() || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("native mesh update requires owning immediate context");
  auto snapshot=std::make_shared<const std::vector<uint8_t>>(vertices.begin(),vertices.end());
  const auto converted=ConvertVertices(*snapshot);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context.Map(buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
    throw std::runtime_error("native mesh vertex update map failed");
  std::memcpy(mapped.pData,converted.data(),converted.size());
  context.Unmap(buffer_.Get(),0);
  source_=std::move(snapshot);
  source_offset_=0; source_bytes_=vertices.size();
}
void NativeIndexedMesh::ValidateDraw(uint32_t first,uint32_t count,int32_t base) const {
  ValidateRange(first,count,base,3);
}
const NativeIndexedMesh::PreparedDraw* NativeIndexedMesh::FindPreparedDraw(
    uint32_t first,uint32_t count,int32_t base) const {
  const auto* draw=prepared_draw_?&*prepared_draw_:nullptr;
  return draw && draw->first_==first && draw->count_==count && draw->base_==base?draw:nullptr;
}
const NativeIndexedMesh::PreparedDraw& NativeIndexedMesh::PrepareDraw(uint32_t first,uint32_t count,int32_t base) {
  if(const auto* draw=FindPreparedDraw(first,count,base)) return *draw;
  ValidateDraw(first,count,base);
  prepared_draw_=PreparedDraw(this,first,count,base);
  return *prepared_draw_;
}
void NativeIndexedMesh::PreparedDraw::Draw(NativeBackendRecorder& recorder) const {
  mesh_->BindAndDraw(recorder,first_,count_,base_,NativeBackendTopology::TriangleList);
}
NativeIndexedMesh::RetainedDraw NativeIndexedMesh::RetainDraw(
    std::shared_ptr<NativeRenderBackend> backend,uint32_t first,uint32_t count,int32_t base) const {
  if(!backend || vertex_storage_->backend_!=backend.get() || index_storage_->backend_!=backend.get())
    throw std::runtime_error("retained scene geometry requires its owning backend");
  if(vertex_storage_->dynamic_vertices_)
    throw std::runtime_error("retained scene geometry requires immutable vertices");
  ValidateDraw(first,count,base);
  return RetainedDraw(std::move(backend),*this,first,count,base);
}
NativeIndexedMesh::RetainedDraw::RetainedDraw(std::shared_ptr<NativeRenderBackend> backend,
    const NativeIndexedMesh& mesh,uint32_t first,uint32_t count,int32_t base)
    :backend_(std::move(backend)),vertices_(mesh.vertex_storage_),indices_(mesh.index_storage_),
     stride_(mesh.stride_),first_(first),count_(count),base_(base) {
  auto layout=std::make_shared<NativeOwnedInputLayout>();
  for(const auto& e:mesh.input_layout().elements())
    layout->Add(e.semantic,e.semantic_index,e.format,e.slot,e.offset,e.per_instance,e.step_rate);
  layout_=std::move(layout);
}
void NativeIndexedMesh::RetainedDraw::Bind(NativeBackendRecorder& recorder) const {
  recorder.SetVertexBuffer(0,*vertices_->storage_,stride_,0);
  recorder.SetIndexBuffer(*indices_->storage_,
    indices_->width_==2?NativeBackendIndexFormat::Uint16:NativeBackendIndexFormat::Uint32,0);
  recorder.SetTopology(NativeBackendTopology::TriangleList);
}
void NativeIndexedMesh::RetainedDraw::Draw(NativeBackendRecorder& recorder) const {
  Bind(recorder);
  recorder.DrawIndexed(count_,first_,base_);
}
void NativeIndexedMesh::RetainedDraw::DrawInstanced(NativeBackendRecorder& recorder,uint32_t instances) const {
  if(!instances) return;
  Bind(recorder);
  recorder.DrawIndexedInstanced(count_,instances,first_,base_,0);
}
std::vector<std::array<float,3>> NativeIndexedMesh::CaptureSourceFloat3(
    uint32_t first,uint32_t count,int32_t base,uint32_t offset) const {
  ValidateRange(first,count,base,1); // Attribute samples need not end on a triangle boundary.
  const auto source_stride=vertex_storage_->guest_stride_;
  if(count>65536 || offset>source_stride || source_stride-offset<12)
    throw std::runtime_error("invalid native mesh float3 probe range");
  const auto source=std::span<const uint8_t>(*vertex_storage_->SourceSnapshot()).subspan(
    vertex_storage_->SourceOffset(),vertex_storage_->SourceBytes());
  std::vector<std::array<float,3>> result;
  result.reserve(count);
  for(uint32_t i=0;i<count;++i) {
    const auto vertex=int64_t(index_storage_->values_[size_t(first)+i])+base;
    const auto at=size_t(vertex)*source_stride+offset; // ValidateDraw checked signed base and extent.
    result.push_back({std::bit_cast<float>(BigWord(source,at)),
      std::bit_cast<float>(BigWord(source,at+4)),std::bit_cast<float>(BigWord(source,at+8))});
  }
  return result;
}
void NativeIndexedMesh::ValidateLineDraw(uint32_t first,uint32_t count,int32_t base) const {
  ValidateRange(first,count,base,2);
}
void NativeIndexedMesh::ValidateRange(uint32_t first,uint32_t count,int32_t base,uint32_t primitive_width) const {
  const auto& index_values_=index_storage_->values_;
  const auto minimum_index_=index_storage_->minimum_,maximum_index_=index_storage_->maximum_;
  if (!count || count%primitive_width || first>index_values_.size() || count>index_values_.size()-first)
    throw std::runtime_error("invalid native mesh index range/topology");
  // An immutable mesh's full index extent can prove any legal subrange safe.
  // If it cannot, retain the exact per-index check: unrelated invalid indices
  // must not reject a valid subrange, and signed base offsets must not wrap.
  if(int64_t(minimum_index_)+base>=0 && int64_t(maximum_index_)+base<vertex_count_) return;
  for (size_t i=first;i<size_t(first)+count;++i) {
    const int64_t vertex=int64_t(index_values_[i])+base;
    if (vertex<0 || vertex>=vertex_count_) throw std::runtime_error("native mesh index outside vertex buffer: index="+
      std::to_string(index_values_[i])+" base="+std::to_string(base)+" vertices="+std::to_string(vertex_count_)+
      " at="+std::to_string(i));
  }
}
void NativeIndexedMesh::Draw(ID3D11DeviceContext& context,uint32_t first,uint32_t count,int32_t base) const {
  ValidateDraw(first,count,base);
  BindAndDraw(context,first,count,base,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
void NativeIndexedMesh::Draw(NativeBackendRecorder& recorder,uint32_t first,uint32_t count,int32_t base) const {
  ValidateDraw(first,count,base);
  BindAndDraw(recorder,first,count,base,NativeBackendTopology::TriangleList);
}
void NativeIndexedMesh::DrawLines(NativeBackendRecorder& recorder,uint32_t first,uint32_t count,int32_t base) const {
  ValidateLineDraw(first,count,base);
  BindAndDraw(recorder,first,count,base,NativeBackendTopology::LineList);
}
void NativeIndexedMesh::BindAndDraw(NativeBackendRecorder& recorder,uint32_t first,uint32_t count,
                                    int32_t base,NativeBackendTopology topology) const {
  recorder.SetVertexBuffer(0,*vertex_storage_->storage_,stride_,0);
  recorder.SetIndexBuffer(*index_storage_->storage_,
    index_storage_->width_==2?NativeBackendIndexFormat::Uint16:NativeBackendIndexFormat::Uint32,0);
  recorder.SetTopology(topology);
  recorder.DrawIndexed(count,first,base);
}
void NativeIndexedMesh::DrawTransient(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                                      uint32_t first,uint32_t count,int32_t base) const {
  ValidateDraw(first,count,base);
  BindTransientAndDraw(recorder,guest_vertices,first,count,base,NativeBackendTopology::TriangleList);
}
void NativeIndexedMesh::DrawLinesTransient(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                                           uint32_t first,uint32_t count,int32_t base) const {
  ValidateLineDraw(first,count,base);
  BindTransientAndDraw(recorder,guest_vertices,first,count,base,NativeBackendTopology::LineList);
}
bool NativeIndexedMesh::DrawTransientExpanded(NativeBackendRecorder& recorder,const NativeBackendPipeline& pipeline,
                                              std::span<const uint8_t> guest_vertices,
                                              uint32_t first,uint32_t count,NativeBackendTopology topology,
                                              int32_t base) const {
  if(topology==NativeBackendTopology::TriangleList) ValidateDraw(first,count,base);
  else if(topology==NativeBackendTopology::LineList) ValidateLineDraw(first,count,base);
  else throw std::runtime_error("an expanded transient draw is for list topologies");
  // The precondition is the pipeline's, not the caller's: a vertex stage
  // reading SV_VertexID (or a second stream) would number or fetch the
  // expanded vertices differently, so such a pipeline draws indexed.
  if(!pipeline.transient_batchable_known || !pipeline.transient_batchable) {
    BindTransientAndDraw(recorder,guest_vertices,first,count,base,topology);
    return false;
  }
  if(!vertex_storage_->dynamic_vertices_)
    throw std::runtime_error("transient vertices are for a dynamic mesh; an immutable one draws its own buffer");
  static thread_local std::vector<uint8_t> converted,expanded;
  vertex_storage_->ConvertVerticesInto(guest_vertices,converted);
  ExpandIndexedVertices(converted,stride_,index_storage_->values_,first,count,base,expanded);
  recorder.SetTransientVerticesOwned(0,expanded,stride_);
  recorder.SetTopology(topology);
  recorder.Draw(count,0);
  return true;
}
void NativeIndexedMesh::DrawTransientPrefix(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                                            uint32_t first,uint32_t count) const {
  ValidateDraw(first,count,0);
  const auto& storage=*vertex_storage_;
  if(!storage.dynamic_vertices_)
    throw std::runtime_error("transient vertices are for a dynamic mesh; an immutable one draws its own buffer");
  if(!storage.guest_stride_ || guest_vertices.size()%storage.guest_stride_)
    throw std::runtime_error("native mesh prefix vertices are not whole vertices");
  const auto vertices=uint32_t(guest_vertices.size()/storage.guest_stride_);
  // Every index drawn names one of the vertices given, not only one of the
  // mesh's: the rest of the mesh's extent has no vertices in this draw.
  const auto& values=index_storage_->values_;
  for(size_t i=first;i<size_t(first)+count;++i)
    if(values[i]>=vertices) throw std::runtime_error("native mesh prefix index outside its vertices: index="+
      std::to_string(values[i])+" vertices="+std::to_string(vertices));
  static thread_local std::vector<uint8_t> converted;
  storage.ConvertVertexPrefixInto(guest_vertices,vertices,converted);
  recorder.SetTransientVerticesOwned(0,converted,stride_);
  recorder.SetIndexBuffer(*index_storage_->storage_,
    index_storage_->width_==2?NativeBackendIndexFormat::Uint16:NativeBackendIndexFormat::Uint32,0);
  recorder.SetTopology(NativeBackendTopology::TriangleList);
  recorder.DrawIndexed(count,first,0);
}
void NativeIndexedMesh::BindTransientAndDraw(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                                             uint32_t first,uint32_t count,int32_t base,
                                             NativeBackendTopology topology) const {
  if(!vertex_storage_->dynamic_vertices_)
    throw std::runtime_error("transient vertices are for a dynamic mesh; an immutable one draws its own buffer");
  // One scratch conversion per thread, reused: this runs a few hundred times a
  // frame. Queued recorders take ownership and return retired scratch storage.
  static thread_local std::vector<uint8_t> converted;
  vertex_storage_->ConvertVerticesInto(guest_vertices,converted);
  recorder.SetTransientVerticesOwned(0,converted,stride_);
  recorder.SetIndexBuffer(*index_storage_->storage_,
    index_storage_->width_==2?NativeBackendIndexFormat::Uint16:NativeBackendIndexFormat::Uint32,0);
  recorder.SetTopology(topology);
  recorder.DrawIndexed(count,first,base);
}
void NativeIndexedMesh::DrawLines(ID3D11DeviceContext& context,uint32_t first,uint32_t count,int32_t base) const {
  ValidateLineDraw(first,count,base);
  BindAndDraw(context,first,count,base,D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
}
void NativeIndexedMesh::BindAndDraw(ID3D11DeviceContext& context,uint32_t first,uint32_t count,int32_t base,D3D11_PRIMITIVE_TOPOLOGY topology) const {
  auto* buffer=vertex_storage_->buffer_.Get(); const UINT offset=0;
  if(!buffer || !index_storage_->buffer_ || !layout_)
    throw std::runtime_error("this mesh is not on a D3D11 backend and cannot be drawn through a context");
  context.IASetInputLayout(layout_.Get());
  context.IASetVertexBuffers(0,1,&buffer,&stride_,&offset);
  context.IASetIndexBuffer(index_storage_->buffer_.Get(),index_storage_->format_,0);
  context.IASetPrimitiveTopology(topology);
  context.DrawIndexed(count,first,base);
}
std::vector<std::array<float,4>> NativeIndexedMesh::CaptureClipPositions(
    ID3D11DeviceContext& context,const NativeShader& shader,uint32_t first,uint32_t count,int32_t base) const {
  ValidateDraw(first,count,base);
  if(count>96 || shader.entry.pixel || !shader.bytecode || !shader.reflection)
    throw std::runtime_error("invalid native clip capture request");
  Microsoft::WRL::ComPtr<ID3D11VertexShader> active_vs;
  Microsoft::WRL::ComPtr<ID3D11GeometryShader> active_gs;
  context.VSGetShader(&active_vs,nullptr,nullptr); context.GSGetShader(&active_gs,nullptr,nullptr);
  if(active_vs.Get()!=shader.vertex.Get() || active_gs)
    throw std::runtime_error("native clip capture requires the bound VS and no GS");
  std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> active_targets{};
  context.SOGetTargets(UINT(active_targets.size()),active_targets.data());
  bool has_target=false;
  for(auto* target:active_targets) if(target) {has_target=true; target->Release();}
  if(has_target) throw std::runtime_error("native clip capture requires no SO targets");
  D3D11_SHADER_DESC shader_desc{};
  if(FAILED(shader.reflection->GetDesc(&shader_desc))) throw std::runtime_error("clip capture reflection failed");
  D3D11_SIGNATURE_PARAMETER_DESC position{}; bool found_position=false;
  for(UINT i=0;i<shader_desc.OutputParameters;++i) {
    D3D11_SIGNATURE_PARAMETER_DESC output{};
    if(FAILED(shader.reflection->GetOutputParameterDesc(i,&output))) throw std::runtime_error("clip capture signature failed");
    if(output.SystemValueType==D3D_NAME_POSITION) {position=output; found_position=true; break;}
  }
  if(!found_position || position.Mask!=15 || position.ComponentType!=D3D_REGISTER_COMPONENT_FLOAT32)
    throw std::runtime_error("clip capture requires float4 SV_POSITION");
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11GeometryShader> stream_shader;
  const D3D11_SO_DECLARATION_ENTRY entry{0,position.SemanticName,position.SemanticIndex,0,4,0};
  const UINT stride=16;
  // D3D11 accepts prior-stage bytecode to capture its outputs without a GS
  // program. NO_RASTERIZED_STREAM prevents this replay from touching the RT.
  if(FAILED(device->CreateGeometryShaderWithStreamOutput(shader.bytecode->GetBufferPointer(),
      shader.bytecode->GetBufferSize(),&entry,1,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&stream_shader)))
    throw std::runtime_error("clip capture stream shader creation failed");
  D3D11_BUFFER_DESC desc{}; desc.ByteWidth=count*stride; desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
  Microsoft::WRL::ComPtr<ID3D11Buffer> stream,staging;
  if(FAILED(device->CreateBuffer(&desc,nullptr,&stream))) throw std::runtime_error("clip capture stream allocation failed");
  desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  if(FAILED(device->CreateBuffer(&desc,nullptr,&staging))) throw std::runtime_error("clip capture staging allocation failed");
  {
    struct Restore {
      ID3D11DeviceContext& context;
      ~Restore() {context.SOSetTargets(0,nullptr,nullptr); context.GSSetShader(nullptr,nullptr,0);}
    } restore{context};
    context.GSSetShader(stream_shader.Get(),nullptr,0);
    auto* buffer=stream.Get(); const UINT offset=0; context.SOSetTargets(1,&buffer,&offset);
    Draw(context,first,count,base);
  }
  context.CopyResource(staging.Get(),stream.Get());
  std::vector<std::array<float,4>> result(count);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("clip capture readback failed");
  std::memcpy(result.data(),mapped.pData,size_t(count)*stride);
  context.Unmap(staging.Get(),0);
  return result;
}
void NativeMeshCache::Clear() {
  last_entry_=nullptr;
  recent_={};
  entries_.clear(); transient_.reset(); index_resources_.clear(); vertex_resources_.clear(); bytes_=0;
}
void NativeMeshCache::Invalidate(uint32_t resource) {
  last_entry_=nullptr;
  recent_={};
  index_resources_.erase(resource);
  vertex_resources_.erase(resource);
  for (auto it=entries_.begin();it!=entries_.end();) {
    // The final key field is a shader variant, not a resource handle.
    if (std::find(it->first.begin(),it->first.begin()+4,resource)!=it->first.begin()+4) {
      bytes_-=it->second.bytes; it=entries_.erase(it);
    } else ++it;
  }
  transient_.reset();
}
NativeIndexedMesh* NativeMeshCache::TryAcquireOwned(NativeRenderBackend& backend,const NativeShader& shader,
    const Key& key,const NativeDeclaration& declaration,uint32_t stride,
    std::span<const uint8_t> vertices,std::span<const uint8_t> indices,uint32_t index_bytes) {
  const auto start=timings_enabled_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
  auto* entry=last_entry_;
  if(dynamic_vertices_ || backend_!=&backend || !entry || last_key_!=key ||
     shader.entry.pixel || !shader.bytecode || !shader.reflection ||
     entry->shader.Get()!=shader.bytecode.Get() || entry->owned_declaration.get()!=&declaration ||
     entry->stride!=stride || entry->index_bytes!=index_bytes || entry->owned_indices ||
     !entry->mesh->IndexStorage()->OwnsSource(backend,indices,index_bytes) ||
     !entry->mesh->VertexStorage()->OwnsSource(vertices)) return nullptr;
  // Identity here means the exact byte span owned by the cached resource, not
  // a guest address or a content hash. Source observation and draw bounds are
  // still checked by the caller, and normal invalidation clears last_entry_.
  transient_.reset();
  entry->used=++tick_;
  ++hits_; ++owned_hits_; ++spend_.calls;
  ++source_checks_.index_identity_hits; ++source_checks_.vertex_identity_hits;
  if(timings_enabled_) spend_.lookup_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now()-start).count());
  return entry->mesh.get();
}
NativeIndexedMesh& NativeMeshCache::Acquire(NativeRenderBackend& backend,const NativeShader& shader,const Key& key,
    std::span<const uint8_t> declaration,uint32_t stride,std::span<const uint8_t> vertices,
    std::span<const uint8_t> indices,uint32_t index_bytes,
    std::shared_ptr<const NativeDeclaration> owned_declaration,
    std::shared_ptr<const NativeGeneratedIndices> owned_indices,
    std::shared_ptr<const NativeIndexBuffer> index_storage,
    std::shared_ptr<NativeVertexBuffer> vertex_storage,
    NativeSnapshotObserver before_snapshot,
    std::shared_ptr<const std::vector<uint8_t>> vertex_contents,size_t contents_offset,
    std::shared_ptr<const std::vector<uint8_t>> index_contents,
    NativeBackendRecorder* recorder) {
  if(index_contents && (index_contents->size()!=indices.size() || index_contents->data()!=indices.data()))
    throw std::runtime_error("native index contents do not own supplied bytes");
  if(owned_indices && (index_bytes!=2 || owned_indices->bytes().data()!=indices.data() ||
      owned_indices->bytes().size()!=indices.size()))
    throw std::runtime_error("generated index identity does not own supplied uint16 bytes");
  if(owned_declaration && (owned_declaration->bytes().data()!=declaration.data() ||
      owned_declaration->bytes().size()!=declaration.size()))
    throw std::runtime_error("native declaration identity does not own supplied bytes");
  if (shader.entry.pixel || !shader.bytecode || !shader.reflection)
    throw std::runtime_error("invalid cached mesh shader");
  // A different backend means different resources; nothing cached here can be
  // bound by it, so the cache starts again rather than handing back a buffer
  // the new backend has never seen.
  const auto clock_now=[&] { return timings_enabled_?std::chrono::steady_clock::now():
    std::chrono::steady_clock::time_point{}; };
  const auto spend_start=clock_now();
  ++spend_.calls;
  if (backend_!=&backend) { Clear(); backend_=&backend; }
  transient_.reset();
  ++tick_;
  const auto after_prologue=clock_now();
  spend_.prologue_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
    after_prologue-spend_start).count());
  // Charged to the lookup until something takes the tail; every return below
  // the hit either goes through the tail marker or is the hit itself.
  auto charge_lookup=[&] {
    spend_.lookup_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      clock_now()-after_prologue).count());
  };
  auto equal=[](const std::vector<uint8_t>& owned,std::span<const uint8_t> guest) {
    return owned.size()==guest.size() && std::equal(owned.begin(),owned.end(),guest.begin());
  };
  const bool published_index=bool(index_storage);
  const auto* retained_vertex=vertex_storage.get();
  // Keep rejected storage alive until candidate selection is complete. A cache
  // mismatch already disproved reuse for this draw; construction must not scan
  // the same rejected source again, including through the weak resource cache.
  std::shared_ptr<const NativeIndexBuffer> rejected_index;
  std::shared_ptr<NativeVertexBuffer> rejected_vertex;
  auto found=entries_.end();
  // Reuse off (native_reuse.h): the map is searched, not the lookup memos.
  Entry* candidate=last_entry_ && last_key_==key && NativeReuseAllowed()?last_entry_:nullptr;
  if(!candidate && NativeReuseAllowed()) {
    const auto& recent=recent_[RecentSlot(key)];
    if(recent.entry && recent.key==key) candidate=recent.entry;
  }
  if(!candidate) {
    found=entries_.find(key);
    if(found!=entries_.end()) candidate=&found->second;
  }
  if (candidate) {
    auto& e=*candidate;
    const bool layout_matches=e.shader.Get()==shader.bytecode.Get() && e.stride==stride && e.index_bytes==index_bytes &&
        (owned_declaration ? e.owned_declaration==owned_declaration : equal(e.declaration,declaration));
    bool indices_match=false;
    if(layout_matches) {
      if(owned_indices) indices_match=e.owned_indices==owned_indices;
      else {
        if(e.mesh->IndexStorage()->OwnsSource(backend,indices,index_bytes)) {
          ++source_checks_.index_identity_hits;
          indices_match=true;
        }
        else {
          ++source_checks_.index_checks;
          source_checks_.index_candidate_bytes+=indices.size();
          indices_match=e.mesh->IndexStorage()->Matches(backend,indices,index_bytes);
        }
      }
    }
    if(layout_matches && !indices_match && !owned_indices) {
      ++index_mismatches_; last_index_mismatch_=key;
      rejected_index=e.mesh->IndexStorage();
    }
    if (indices_match && dynamic_vertices_ && recorder &&
        e.mesh->VertexStorage()->SourceBytes()==vertices.size()) {
      // A dynamic mesh drawn through a recorder takes its vertices per draw
      // (DrawTransient), so the cached buffer's contents are never consulted:
      // nothing to compare against and nothing to rewrite. The bytes it was
      // built from only fixed its declaration and vertex count.
      if(last_entry_!=&e) Remember(key,e);
      e.used=tick_; ++hits_; charge_lookup(); return *e.mesh;
    }
    if (indices_match) {
      bool vertices_match=e.mesh->VertexStorage()->OwnsSource(vertices);
      if(vertices_match) ++source_checks_.vertex_identity_hits;
      else {
        ++source_checks_.vertex_checks;
        source_checks_.vertex_candidate_bytes+=vertices.size();
        vertices_match=e.mesh->VertexStorage()->MatchesSource(vertices);
      }
      if(vertices_match) {
        if(last_entry_!=&e) Remember(key,e);
        e.used=tick_; ++hits_; charge_lookup(); return *e.mesh;
      }
      ++vertex_mismatches_;
      last_vertex_mismatch_=key;
      rejected_vertex=e.mesh->VertexStorage();
      if(dynamic_vertices_ && e.mesh->VertexStorage()->SourceBytes()==vertices.size()) {
        if(found==entries_.end()) found=entries_.find(key);
        ForgetLookup(key);
        try {
          if(before_snapshot) before_snapshot();
          if(recorder) e.mesh->UpdateVertices(*recorder,vertices);
          else {
            // No recorder means the caller is still on the direct path, which
            // writes through the immediate context.
            auto* update_device=NativeD3D11BackendDevice(backend);
            if(!update_device) throw std::runtime_error("dynamic mesh vertices on the "+
              std::string(backend.name())+" backend need a recorder to update them");
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> context; update_device->GetImmediateContext(&context);
            e.mesh->UpdateVertices(*context.Get(),vertices);
          }
          e.used=tick_; ++updates_; return *e.mesh;
        } catch(...) { bytes_-=e.bytes; entries_.erase(found); throw; }
      }
    }
    // Invalidate before construction: malformed updates cannot revive old data.
    if(!index_storage) index_storage=e.mesh->IndexStorage();
    if(!vertex_storage) vertex_storage=e.mesh->VertexStorage();
    ForgetLookup(key);
    if(found==entries_.end()) found=entries_.find(key);
    bytes_-=e.bytes; entries_.erase(found);
  }
  charge_lookup();
  const auto tail_start=clock_now();
  struct TailCharge {
    NativeMeshCache::Spend* spend;
    std::chrono::steady_clock::time_point start;
    ~TailCharge() {
      if(!spend) return;
      spend->tail_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count());
    }
  } tail_charge{timings_enabled_?&spend_:nullptr,tail_start};
  // Only live meshes own these generations. Pruning weak entries keeps resource
  // address churn bounded independently of mesh eviction and transient uploads.
  std::erase_if(index_resources_,[](const auto& item) { return item.second.expired(); });
  std::erase_if(vertex_resources_,[](const auto& item) { return item.second.expired(); });
  if(!dynamic_vertices_ && !vertex_storage) {
    if(const auto resource=vertex_resources_.find(key[0]);resource!=vertex_resources_.end())
      vertex_storage=resource->second.lock();
  }
  if(!index_storage) {
    if(const auto resource=index_resources_.find(key[1]);resource!=index_resources_.end())
      index_storage=resource->second.lock();
  }
  if(index_storage==rejected_index) index_storage.reset();
  if(vertex_storage==rejected_vertex) vertex_storage.reset();
  // Construction validates the candidate's device, format and live source once.
  // Do not repeat that full guest-memory scan here immediately beforehand.
  if(before_snapshot) before_snapshot();
  auto mesh=std::make_unique<NativeIndexedMesh>(backend,shader,declaration,stride,vertices,indices,index_bytes,dynamic_vertices_,index_storage,vertex_storage,
    NativeIndexedMesh::IndexReuse::ReplaceStale,std::move(vertex_contents),contents_offset,std::move(index_contents));
  if(published_index) {
    if(mesh->IndexStorage()==index_storage) ++published_index_reuses_;
    else ++published_index_rejections_;
  }
  if(retained_vertex) {
    if(mesh->VertexStorage().get()==retained_vertex) ++retained_vertex_reuses_;
    else ++retained_vertex_replacements_;
  }
  index_resources_.insert_or_assign(key[1],mesh->IndexStorage());
  if(!dynamic_vertices_) vertex_resources_.insert_or_assign(key[0],mesh->VertexStorage());
  ++builds_;
  const size_t size=mesh->StorageBytes()+declaration.size();
  if (size>budget_ || !entry_limit_) { transient_=std::move(mesh); return *transient_; }
  while (!entries_.empty() && (entries_.size()>=entry_limit_ || bytes_>budget_-size)) {
    if(entries_.size()>=entry_limit_) ++entry_evictions_;
    else ++budget_evictions_;
    const auto oldest=std::min_element(entries_.begin(),entries_.end(),
      [](const auto& a,const auto& b) { return a.second.used<b.second.used; });
    ForgetLookup(oldest->first);
    bytes_-=oldest->second.bytes; entries_.erase(oldest);
  }
  Entry entry{shader.bytecode,owned_declaration?std::vector<uint8_t>{}:std::vector<uint8_t>{declaration.begin(),declaration.end()},
    stride,index_bytes,std::move(mesh),size,tick_,std::move(owned_declaration),std::move(owned_indices)};
  auto inserted=entries_.emplace(key,std::move(entry)).first;
  bytes_+=size;
  Remember(key,inserted->second);
  return *inserted->second.mesh;
}
}
