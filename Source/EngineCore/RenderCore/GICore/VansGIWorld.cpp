#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansGIWorld.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansJobSystem.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansShaderManager.h"
#include "../VansMaterial.h"
#include "VansGIVoxelSource.h"
#include "../GeometryCore/VansMeshGeometryReadback.h"
#include "../TerrainCore/VansTerrain.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <atomic>
#include <set>
#include <cfloat>
#include <climits>
#include "../GeometryCore/VansSceneGeometrySnapshot.h"
#include "../../AssetCore/Importers/Shader/VansShaderArtifactCache.h"

namespace VansGraphics
{
    namespace
    {
        std::atomic<uint64_t> WorldGeneration{0};
        void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
        glm::vec4 SamplePixels(const std::vector<glm::vec4>& pixels,uint32_t w,uint32_t h,glm::vec2 uv)
        {if(pixels.empty())return glm::vec4(1);uv=glm::fract(uv);return pixels[std::min(uint32_t(uv.y*h),h-1)*w+std::min(uint32_t(uv.x*w),w-1)];}
    }
    VansGIWorld::~VansGIWorld()
    {
        // 引擎约定 JobSystem 最后关闭；销毁时等待本对象已经提交的 CPU 工作，工作线程不访问 GPU。
        if(m_BuildFuture.valid())m_BuildFuture.wait();
        if(!m_Device)return;
        m_Trace.reset();m_Lighting.reset();m_Visibility.reset();m_Bias.reset();m_Atlas.reset();m_State.reset();
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        descriptors->DestroyDescriptorSet(m_Sets);descriptors->ReleaseDescriptorSetLayout(m_Layout);
        for(auto& b:m_Buffers)b.DestroyVulkanBuffer(m_Device->GetLogicDevice());
    }
    uint64_t VansGIWorld::AllocatedBytes() const
    {uint64_t bytes=0;for(const auto& b:m_Buffers)bytes+=b.GetBufferSize();return bytes;}
    std::unique_ptr<VansComputeShader> VansGIWorld::LoadShader(const std::string& file,uint32_t pushBytes)
    {
        auto shader=std::make_unique<VansComputeShader>();
        VansPipelineProgramDesc desc{};desc.name="GIWorld/"+file;desc.shaderPath=m_ShaderFolder;desc.kind=VansPipelineProgramKind::Compute;desc.pushConstantSize=pushBytes;
        shader->SetName(desc.name);shader->SetPipelineProgramDesc(desc);
        Require(shader->InitShader(m_Device->GetLogicDevice(),m_ShaderFolder,{{VK_SHADER_STAGE_COMPUTE_BIT,file+".comp"}}),"GI world shader load failed");
        // InitShader 会重置接口；与 ShaderManager 一样，在加载成功后发布计算管线和常量范围。
        shader->SetPipelineProgramDesc(desc);shader->SetPushConstant(pushBytes);
        return shader;
    }
    void VansGIWorld::MakeBuffer(VansVKBuffer& buffer,const void* data,size_t bytes,VkBufferUsageFlags usage)
    {
        const size_t actual=std::max<size_t>(bytes,16);
        Require(actual<=m_Device->GetDeviceProperties().limits.maxStorageBufferRange,"GI world buffer exceeds device range");
        Require(buffer.CreatVulkanBuffer(m_Device->GetLogicDevice(),actual,VK_FORMAT_UNDEFINED,usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            data?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),"GI world buffer allocation failed");
        if(data&&bytes)Require(buffer.SetBufferData(data,0,bytes),"GI world buffer upload failed");
    }
    std::vector<glm::vec4> VansGIWorld::CaptureTexture(VansTexture* texture,uint32_t& width,uint32_t& height,uint32_t maximum,bool footprint)
    {
        width=height=1;if(!texture)return {glm::vec4(1)};
        width=footprint?maximum:std::min(uint32_t(texture->GetWidth()),maximum);
        height=footprint?maximum:std::min(uint32_t(texture->GetHeight()),maximum);
        Require(width&&height,"GI material has an invalid texture extent");
        auto shader=LoadShader("GIWorldTexture",16);
        auto* manager=VansVKDescriptorManager::GetInstance();
        VkDescriptorSetLayout layout=VK_NULL_HANDLE;std::vector<VkDescriptorSet> sets;VansVKBuffer buffer;
        const size_t samples=size_t(width)*height*(footprint?4u:1u),bytes=samples*sizeof(glm::vec4);
        auto release=[&](){manager->DestroyDescriptorSet(sets);manager->ReleaseDescriptorSetLayout(layout);buffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());};
        try
        {
            Require(buffer.CreatVulkanBuffer(m_Device->GetLogicDevice(),bytes,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&buffer.PersistentMap(),"GI texture snapshot allocation failed");
            Require(manager->CreateDesciptorSetLayout({{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
                {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},layout)&&manager->AllocateDescriptorSet({layout},sets),"GI texture snapshot descriptors failed");
            manager->BeginDescriptorUpdate();
            manager->WriteImageDescriptor(sets[0],0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{texture->GetImage().GetSampler(),texture->GetImage().GetImageView(),texture->GetImage().GetImageLayout()}});
            manager->WriteBufferDescriptor(sets[0],1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{{buffer.GetNativeBuffer(),0,bytes}});manager->UpdateDescriptorSets();
            auto& command=m_Device->GetImmediateGraphicsCommandBuffer();
            Require(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"GI snapshot recording failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            command.EnsureComputeShader(*shader,{layout});glm::uvec4 extent(width,height,footprint?4u:0u,0);
            command.DispatchCompute(*shader,(width+7)/8,(height+7)/8,1,sets,&extent,sizeof(extent));
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});
            Require(command.EndCommandBufferRecord()&&VansVKCommandBuffer::SubmitCommands(m_Device->GetGraphicsQueue(),m_Device->GetLogicDevice(),
                {command.GetVKCommandBuffer()},{},{},command.m_CommandBufferFinishSubmitFence)&&command.ResetCommandBuffer(false),"GI texture snapshot submission failed");
            buffer.InvalidateMappedRange(0,bytes);std::vector<glm::vec4> pixels(samples);std::memcpy(pixels.data(),buffer.GetMappedPtr(),bytes);release();return pixels;
        }
        catch(...){release();throw;}
    }
    std::shared_ptr<const GIWorldTemplate> VansGIWorld::FindTemplate(const GIVoxelSource& source) const
    {
        const auto found=std::find_if(m_SourceTemplates.begin(),m_SourceTemplates.end(),[&](const auto& item)
            {return item.identity==source.modelIdentity && item.parts==source.parts;});
        return found==m_SourceTemplates.end()?nullptr:found->model;
    }
    void VansGIWorld::CaptureGeometry(const std::vector<GIVoxelSource>& sources,std::vector<GIWorldInstance>& instances)
    {
        size_t templateCells=0;
        for(const auto& source:sources)
        {
            // 缺少流送资源时不建立空几何，也不把它解释为删除；查询保持既有 unknown 语义。
            if(source.availability!=GIVoxelSourceAvailability::Available)continue;
            Require(!source.key.empty() && source.modelIdentity && !m_SourceChunks.count(source.key),"GI source requires a unique key and immutable model identity");
            auto model=FindTemplate(source);
            if(!model)
            {
                std::vector<GIWorldTriangle> triangles;
                for(const auto& part:source.parts)
                {
                    auto* root=part.mesh;
                    auto* material=part.material;
                    Require(root&&material,"GI geometry voxelization requires a loaded mesh and PBR material");
                    std::vector<VansMesh*> meshes;
                    if(root->m_IsMultiMesh){for(size_t i=0;i<root->m_SubMeshes.size();++i)if(part.submesh<0||i==size_t(part.submesh))meshes.push_back(root->m_SubMeshes[i]);}
                    else if(part.submesh<=0)meshes.push_back(root);
                    std::vector<VansMeshGeometryData> geometry;std::string error;
                    if(!VansMeshGeometryReadback::Read(*m_Device,meshes,geometry,error,true))throw std::runtime_error(error);
                    uint32_t w,h;auto pixels=std::make_shared<std::vector<glm::vec4>>(CaptureTexture(material->m_BaseColorTexture,w,h));
                    glm::vec3 color(0);float weight=0;
                    for(auto value:*pixels){float a=material->m_AlphaTestEnabled?(value.a>=material->m_AlphaCutoff?1.f:0.f):1.f;color+=glm::vec3(value)*a;weight+=a;}
                    color=(weight>0?color/weight:glm::vec3(0))*material->m_BasePBRParam.m_albedo;
                    Require(m_Materials.size()<65536,"GI voxel material table overflow");uint32_t id=uint32_t(m_Materials.size());
                    m_Materials.push_back(glm::vec4(color*(1-material->m_BasePBRParam.m_metallic),material->m_BasePBRParam.m_roughness));
                    for(const auto& mesh:geometry)for(size_t i=0;i<mesh.indices.size();i+=3)
                    {
                        GIWorldTriangle t;t.material=id;t.porous=part.porous||material->m_AlphaTestEnabled;
                        for(uint32_t j=0;j<3;++j){auto index=mesh.indices[i+j];t.positions[j]=mesh.positions[index];t.uvs[j]=mesh.texcoords[index];}
                        if(material->m_AlphaTestEnabled){t.alphaCutoff=material->m_AlphaCutoff;t.alpha=[pixels,w,h](glm::vec2 uv){return SamplePixels(*pixels,w,h,uv).a;};}
                        triangles.push_back(std::move(t));
                    }
                }
                auto baked=std::make_shared<GIWorldTemplate>();std::string error;
                if(!BakeGIWorldTemplate(triangles,m_Settings.voxelSize,1048576,*baked,error))throw std::runtime_error(error);
                templateCells+=baked->CellCount();Require(templateCells<=1048576,"GI world template cache exceeds one million occupied cells; increase voxel size");
                model=std::move(baked);m_SourceTemplates.push_back({source.modelIdentity,source.parts,model});
            }
            auto request=std::make_shared<InstanceSource>();request->model=model;request->transforms=source.instances;
            auto captured=std::make_shared<CapturedSource>();captured->source=request;
            if(!model->cells.empty())for(const auto& transform:source.instances)captured->instances.push_back(MakeGIWorldInstance(model,transform));
            m_SourceChunks.emplace(source.key,std::move(captured));
        }
        // 初始捕获和后续区块发布都按稳定键排列，编辑不能改变未修改实例的累加顺序。
        for(const auto& item:m_SourceChunks)instances.insert(instances.end(),item.second->instances.begin(),item.second->instances.end());
    }
    void VansGIWorld::Initialize(VansVKDevice& device,VansScene& scene,const GIWorldSettings& settings,bool hardware,uint32_t rayCapacity)
    {
        Require(settings.enabled,"Disabled GI world must not allocate resources");m_Device=&device;m_Settings=settings;m_Revision=(++WorldGeneration)<<32;
        auto* base=VansShaderManager::Get().FindComputeShader("GIPointLight");Require(base,"GI base shader is missing");
        m_ShaderFolder=(std::filesystem::path(base->GetShaderFolder()).parent_path()/"GIWorld").string();
        m_Materials.push_back(glm::vec4(.5f,.5f,.5f,1));
        std::vector<GIVoxelSource> sources;scene.CollectGIVoxelSources(sources);
        std::vector<GIWorldInstance> instances;CaptureGeometry(sources,instances);
        m_LayoutIndex=BuildLayoutIndex(instances);
        glm::vec3 center=scene.GetCamera()?glm::vec3(scene.GetCamera()->GetPosition()):glm::vec3(0);
        m_Builder.Reset(settings,std::move(instances),center);
        m_PendingBricks=m_Builder.PendingCount();
        Require((m_Builder.AvailableLevels()&(1u<<(m_Builder.LevelCount()-1)))!=0,"GI world exceeds the supported hierarchical coordinate range");
        m_ViewCenter=center;
        uint32_t capacity=1;while(capacity<std::max(settings.maxBricks*4,64u))capacity*=2;
        m_Pages.resize(capacity);
        m_PendingCoarseBricks=0;
        for(uint32_t index=0;index<m_Builder.Pages().size();++index)
        {
            const auto key=m_Builder.Pages()[index];auto slot=GIWorldHash(key)&(capacity-1);
            while(m_Pages[slot].data.x!=~0u)slot=(slot+1)&(capacity-1);
            m_Pages[slot].key={key.x,key.y,key.z,key.level};m_Pages[slot].data={index*512,0,0,0};m_PageSlots.emplace(key,slot);
            if(key.level==int(m_Builder.LevelCount())-1)++m_PendingCoarseBricks;
        }
        std::vector<glm::vec4> terrainColors(1,glm::vec4(.5f,.5f,.5f,1));
        if(auto* node=dynamic_cast<VansTerrainRenderNode*>(scene.GetTerrainRenderNode()))if(auto* terrain=node->GetTerrain())
        {
            auto asset=*terrain->GetAssetSnapshot();
            // Height textures retain edits made while GI was disabled; read the current image at this safe point.
            uint32_t hw,hh;auto heightPixels=CaptureTexture(terrain->GetHeightMap(),hw,hh,16384);
            Require(hw==asset.width&&hh==asset.height,"GI terrain snapshot extent differs from the live heightfield");
            for(size_t i=0;i<asset.heights.size();++i)asset.heights[i]=uint16_t(glm::clamp(heightPixels[i].r,0.f,1.f)*65535.f+.5f);
            std::string error;if(!m_Height.Build(asset,error))throw std::runtime_error(error);
            std::vector<glm::vec4> colors;for(const auto& layer:asset.layers)
            {
                uint32_t w,h;auto pixels=CaptureTexture(static_cast<VansTexture*>(scene.FindTextureAssetByGuid(layer.albedo.ToString())),w,h);
                glm::vec4 average(0);for(auto p:pixels)average+=p;average/=float(pixels.size());average.a=1;colors.push_back(average);
            }
            std::array<std::vector<uint32_t>,2> corners;
            for(uint32_t map=0;map<2;++map)
            {
                auto* texture=terrain->GetSplatmap(map);
                Require(texture && texture->GetWidth()==asset.width && texture->GetHeight()==asset.height,
                    "GI splat extent differs from the live heightfield");
                uint32_t w,h;auto pixels=CaptureTexture(texture,w,h,GIWorldTerrainColorData::Resolution,true);
                corners[map].reserve(pixels.size());
                for(auto pixel:pixels)corners[map].push_back(glm::packUnorm4x8(pixel));
            }
            if(!m_TerrainColors.Build(asset.width,asset.height,std::move(corners),std::move(colors),error))throw std::runtime_error(error);
            terrainColors=m_TerrainColors.colors;
        }
        m_Parameters.terrain=m_Height.parameters;m_Parameters.terrain.w=m_Height.width?128.f:1.f;m_Parameters.height={m_Height.width,m_Height.height,uint32_t(m_Height.levels.size()),m_Height.width?1u:0u};
        m_Parameters.table={capacity,uint32_t(m_Builder.Pages().size()),settings.maxTraceSteps,hardware?1u:0u};m_Parameters.center=glm::vec4(center,settings.extinctionScale);
        m_Parameters.grid={settings.voxelSize,settings.coverageDistance,float(m_Builder.LevelCount()),float(m_Builder.AvailableLevels())};
        glm::vec3 voxelMin(FLT_MAX),voxelMax(-FLT_MAX);
        const float coarseBrickSize=settings.voxelSize*float(1u<<(m_Builder.LevelCount()-1))*8;
        for(const auto& page:m_Builder.Pages())if(page.level==int(m_Builder.LevelCount())-1)
        {
            const glm::vec3 lo=glm::vec3(page.x,page.y,page.z)*coarseBrickSize;
            voxelMin=glm::min(voxelMin,lo);voxelMax=glm::max(voxelMax,lo+coarseBrickSize);
        }
        if(m_Builder.Instances().empty())voxelMin=voxelMax=glm::vec3(0);
        m_Parameters.voxelMin=glm::vec4(voxelMin,0);m_Parameters.voxelMax=glm::vec4(voxelMax,0);
        VANS_LOG("[GIWorld] global voxel instances="<<m_Builder.Instances().size()<<" height="<<m_Height.width<<"x"<<m_Height.height
            <<" sourceChunks="<<m_SourceChunks.size()<<" templates="<<m_SourceTemplates.size()
            <<" coarseVoxel="<<settings.voxelSize*float(1u<<(m_Builder.LevelCount()-1))
            <<" levels="<<m_Builder.LevelCount()<<" pages="<<m_Builder.Pages().size()<<" capacity="<<settings.maxBricks
            <<" bounds=("<<voxelMin.x<<","<<voxelMin.y<<","<<voxelMin.z<<")..("
            <<voxelMax.x<<","<<voxelMax.y<<","<<voxelMax.z<<")");
        Require(m_Height.levels.size()<=m_Parameters.heightLevels.size(),"GI height hierarchy exceeds supported depth");
        std::copy(m_Height.levels.begin(),m_Height.levels.end(),m_Parameters.heightLevels.begin());
        MakeBuffer(m_Buffers[0],&m_Parameters,sizeof(m_Parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        float zero=0;glm::vec2 empty(0);
        MakeBuffer(m_Buffers[1],m_Height.heights.empty()?&zero:m_Height.heights.data(),std::max<size_t>(1,m_Height.heights.size())*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[2],m_Height.ranges.empty()?&empty:m_Height.ranges.data(),std::max<size_t>(1,m_Height.ranges.size())*8,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[3],m_Pages.data(),m_Pages.size()*sizeof(GIWorldPageGPU),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[4],nullptr,size_t(settings.maxBricks)*512*sizeof(GIWorldVoxel),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[5],m_Materials.data(),m_Materials.size()*sizeof(glm::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[6],terrainColors.data(),terrainColors.size()*sizeof(glm::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[7],nullptr,32+size_t(std::max(rayCapacity,1u))*sizeof(glm::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        auto* manager=VansVKDescriptorManager::GetInstance();std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(uint32_t i=0;i<m_Buffers.size();++i)bindings.push_back({i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        Require(manager->CreateDesciptorSetLayout(bindings,m_Layout)&&manager->AllocateDescriptorSet({m_Layout},m_Sets),"GI world descriptors failed");
        manager->BeginDescriptorUpdate();for(uint32_t i=0;i<m_Buffers.size();++i)manager->WriteBufferDescriptor(m_Sets[0],i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{m_Buffers[i].GetNativeBuffer(),0,m_Buffers[i].GetBufferSize()}});manager->UpdateDescriptorSets();
        m_Trace=LoadShader("GIWorldTrace",96);m_Lighting=LoadShader("GIWorldLighting",96);
        m_Visibility=LoadShader("GIWorldReceiverVisibility");m_Bias=LoadShader("GIWorldReceiverBias");
        m_Atlas=LoadShader("GIWorldAtlas",96);m_State=LoadShader("GIWorldState",96);
    }
    void VansGIWorld::RecordScatterRanges(VansVKCommandBuffer& command,const std::array<uint32_t,8>& rayCounts)
    {
        std::array<glm::uvec4,2> offsets{};uint64_t total=0;
        for(size_t i=0;i<rayCounts.size();++i)
        {
            Require(total<=UINT32_MAX,"GI scatter offset overflow");
            offsets[i/4][i%4]=uint32_t(total);total+=rayCounts[i];
        }
        Require(total<=(m_Buffers[7].GetBufferSize()-sizeof(offsets))/sizeof(glm::vec4),"GI scatter work exceeds shared ray capacity");
        command.UpdateBuffer(m_Buffers[7].GetNativeBuffer(),0,sizeof(offsets),offsets.data());
    }
    bool VansGIWorld::ApplyHeightPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,
        const std::vector<uint8_t>& pixels,GIWorldHeightData::Patch& changed)
    {
        std::string error;
        if(!m_Height.ApplyPatch(x,z,w,h,pixels,changed,error))return false;
        m_PendingHeightSpans.insert(m_PendingHeightSpans.end(),changed.heights.begin(),changed.heights.end());
        m_PendingRangeSpans.insert(m_PendingRangeSpans.end(),changed.ranges.begin(),changed.ranges.end());
        ++m_Revision;return true;
    }
    bool VansGIWorld::ApplyColorPatch(uint32_t map,uint32_t x,uint32_t y,uint32_t w,uint32_t h,const std::vector<uint8_t>& pixels)
    {
        GIWorldTerrainColorData::Patch changed;std::string error;
        if(!m_TerrainColors.ApplyPatch(map,x,y,w,h,pixels,changed,error))return false;
        if(changed.spans.empty())return true;
        m_PendingColorSpans.insert(m_PendingColorSpans.end(),changed.spans.begin(),changed.spans.end());
        const auto lo=(glm::vec2(changed.first)/float(GIWorldTerrainColorData::Resolution)-.5f)*m_Height.parameters.x;
        const auto hi=(glm::vec2(changed.last+1u)/float(GIWorldTerrainColorData::Resolution)-.5f)*m_Height.parameters.x;
        const auto height=m_Height.ranges.back();
        GIWorldBounds bounds{{lo.x,height.x,lo.y},{hi.x,height.y,hi.y}};
        if(m_PendingLightingBounds)
        {bounds.minimum=glm::min(bounds.minimum,m_PendingLightingBounds->minimum);bounds.maximum=glm::max(bounds.maximum,m_PendingLightingBounds->maximum);}
        m_PendingLightingBounds=bounds;
        // 颜色变化不改变几何可见性版本，也不清除 relocation 或已发布状态。
        return true;
    }
    void VansGIWorld::RecordTerrainUpdates(VansVKCommandBuffer& command)
    {
        if(m_PendingHeightSpans.empty() && m_PendingRangeSpans.empty() && m_PendingColorSpans.empty())return;
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        auto upload=[&](uint32_t buffer,const void* data,uint32_t stride,std::vector<glm::uvec2>& spans)
        {
            const auto* bytes=static_cast<const uint8_t*>(data);
            std::sort(spans.begin(),spans.end(),[](auto a,auto b){return a.x<b.x;});
            for(size_t first=0;first<spans.size();)
            {
                auto span=spans[first++];
                while(first<spans.size() && spans[first].x<=span.x+span.y)
                {span.y=std::max(span.x+span.y,spans[first].x+spans[first].y)-span.x;++first;}
                size_t offset=size_t(span.x)*stride,end=offset+size_t(span.y)*stride;
                while(offset<end)
                {const size_t length=std::min<size_t>(end-offset,65536);command.UpdateBuffer(m_Buffers[buffer].GetNativeBuffer(),offset,length,bytes+offset);offset+=length;}
            }
            spans.clear();
        };
        upload(1,m_Height.heights.data(),sizeof(float),m_PendingHeightSpans);
        upload(2,m_Height.ranges.data(),sizeof(glm::vec2),m_PendingRangeSpans);
        upload(6,m_TerrainColors.colors.data(),sizeof(glm::vec4),m_PendingColorSpans);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
    }
    void VansGIWorld::AddLayoutQueries(VansSceneGeometrySnapshot& geometry) const
    {
        geometry.additionalPositionValid=[this](glm::vec3 p,float clearance)
        {
            if(m_Height.width&&std::abs(p.x)<=m_Height.parameters.x*.5f&&std::abs(p.z)<=m_Height.parameters.x*.5f&&p.y<=m_Height.Sample({p.x,p.z})+clearance)return false;
            glm::ivec3 key(glm::floor(p/32.f));auto bucket=m_LayoutIndex.find({key.x,key.y,key.z,0});if(bucket==m_LayoutIndex.end())return true;
            for(auto index:bucket->second)
            {
                const auto& i=m_Builder.Instances()[index];if(glm::any(glm::lessThan(p,i.minimum))||glm::any(glm::greaterThan(p,i.maximum)))continue;
                glm::ivec3 c(glm::floor(glm::vec3(i.inverse*glm::vec4(p,1))/i.model->voxelSize));
                auto voxel=i.model->cells.find({c.x,c.y,c.z,0});if(voxel!=i.model->cells.end()&&(voxel->second.optical&0x10000u))return false;
            }
            return true;
        };
        geometry.additionalSurface=[this](glm::vec3 lo,glm::vec3 hi)
        {
            VansGeometrySurfaceMeasure measure;
            if(m_Height.width)
            {
                const float size=m_Height.parameters.x;glm::vec2 a=glm::max(glm::vec2(lo.x,lo.z),glm::vec2(-size*.5f)),b=glm::min(glm::vec2(hi.x,hi.z),glm::vec2(size*.5f));
                if(glm::all(glm::lessThan(a,b)))
                {
                    uint32_t level=0;float cells=std::max((b.x-a.x)*m_Height.width/size,(b.y-a.y)*m_Height.height/size);while(level+1<m_Height.levels.size()&&float(1u<<level)<cells)++level;
                    auto info=m_Height.levels[level];float stride=float(1u<<level);glm::vec2 dims(m_Height.width,m_Height.height);
                    glm::uvec2 first(glm::clamp(glm::floor(((a/size+.5f)*dims+.5f)/stride),glm::vec2(0),glm::vec2(info.y-1,info.z-1)));
                    glm::uvec2 last(glm::clamp(glm::floor(((b/size+.5f)*dims+.5f)/stride),glm::vec2(0),glm::vec2(info.y-1,info.z-1)));
                    float minimum=FLT_MAX,maximum=-FLT_MAX;
                    for(uint32_t z=first.y;z<=last.y;++z)for(uint32_t x=first.x;x<=last.x;++x){auto r=m_Height.ranges[info.x+z*info.y+x];minimum=std::min(minimum,r.x);maximum=std::max(maximum,r.y);}
                    if(minimum<=hi.y&&maximum>=lo.y){measure.area=(b.x-a.x)*(b.y-a.y);measure.areaNormal={0,measure.area,0};}
                }
            }
            // AABBs only select demand candidates; they never enter the occlusion query.
            glm::ivec3 first(glm::floor(lo/32.f)),last(glm::floor(hi/32.f));std::set<uint32_t> seen;
            for(auto bucket=m_LayoutIndex.lower_bound({first.x,INT_MIN,INT_MIN,0});bucket!=m_LayoutIndex.end()&&bucket->first.x<=last.x;++bucket)
            {
                auto key=bucket->first;if(key.y<first.y||key.y>last.y||key.z<first.z||key.z>last.z)continue;
                for(auto index:bucket->second)if(seen.insert(index).second)
                {
                    const auto& i=m_Builder.Instances()[index];if(i.model->cells.empty())continue;
                    glm::vec3 extent=glm::min(hi,i.maximum)-glm::max(lo,i.minimum);
                    if(glm::all(glm::greaterThanEqual(extent,glm::vec3(0))))measure.area+=2.0*double(extent.x*extent.y+extent.x*extent.z+extent.y*extent.z);
                }
            }
            return measure;
        };
    }
    bool VansGIWorld::CookShaders(const std::string& shaderRoot,std::vector<Vans::VansShaderCookProgram>& programs,std::string& error)
    {
        for(const char* name:{"GIWorldTexture","GIWorldTrace","GIWorldLighting","GIWorldReceiverVisibility","GIWorldReceiverBias","GIWorldAtlas","GIWorldState"})
        {
            Vans::VansShaderCompileRequest request;request.programId=std::string("GIWorld/")+name;
            request.sourceFolder=std::filesystem::path(shaderRoot)/"GIWorld";request.includeRoots.push_back(shaderRoot);
            request.stages.push_back({"comp",request.sourceFolder/(std::string(name)+".comp")});
            auto prepared=Vans::VansShaderArtifactCache::Get().Prepare(request,false);
            if(!prepared.success||!Vans::VansShaderArtifactCache::Get().CommitActive(prepared)){error="Failed to cook "+request.programId;return false;}
            programs.push_back({request.programId,prepared.artifactRoot});
        }
        return true;
    }
    VansGIWorld::LayoutIndex VansGIWorld::BuildLayoutIndex(const std::vector<GIWorldInstance>& instances)
    {
        LayoutIndex result;
        for(uint32_t index=0;index<instances.size();++index)
        {
            const auto& item=instances[index];glm::ivec3 lo(glm::floor(item.minimum/32.f)),hi(glm::floor(item.maximum/32.f));
            Require(glm::all(glm::lessThanEqual(hi-lo,glm::ivec3(64))),"GI geometry extent exceeds spatial index budget");
            for(int x=lo.x;x<=hi.x;++x)for(int y=lo.y;y<=hi.y;++y)for(int z=lo.z;z<=hi.z;++z)result[{x,y,z,0}].push_back(index);
        }
        return result;
    }
    bool VansGIWorld::QueueSourceChanges(GIVoxelSourceChanges changes)
    {
        if(m_SourceDirty)return false;
        if(changes.Empty())return true;
        std::map<std::string,std::shared_ptr<const InstanceSource>> prepared;
        uint64_t submittedInstances=0;
        std::set<std::string> keys;
        for(const auto& key:changes.removed)Require(!key.empty() && keys.insert(key).second,"Invalid or duplicate GI source removal");
        for(auto& source:changes.updated)
        {
            if(source.availability!=GIVoxelSourceAvailability::Available)continue;
            Require(!source.key.empty() && source.modelIdentity && keys.insert(source.key).second,"Invalid or duplicate GI source update");
            auto model=FindTemplate(source);if(!model)return false;
            submittedInstances+=source.instances.size();
            auto request=std::make_shared<InstanceSource>();request->model=std::move(model);request->transforms=std::move(source.instances);
            prepared.emplace(std::move(source.key),std::move(request));
        }
        // 请求表包含所有尚未发布的修改；A 被 B 合并时，不能丢掉 A 修改过的其他区块。
        for(const auto& key:changes.removed)m_SourceChanges[key]=nullptr;
        for(auto& item:prepared)m_SourceChanges[item.first]=std::move(item.second);
        m_SourceUpdateStats.updatedChunks=prepared.size();m_SourceUpdateStats.removedChunks=changes.removed.size();
        m_SourceUpdateStats.submittedInstances=submittedInstances;
        if(prepared.empty() && changes.removed.empty())return true;
        ++m_InstanceGeneration;return true;
    }
    void VansGIWorld::PlanPages(BuildResult& result) const
    {
        // 与砖规划同一后台事务；发布前旧页表只读，旧场仍可被 GPU 查询。
        const auto& builder=*result.replacement;
        result.pages.assign(m_Pages.size(),GIWorldPageGPU{});
        auto& pages=result.pages;auto& slots=result.pageSlots;
        std::vector<bool> used(m_Settings.maxBricks,false);
        for(const auto& key:builder.Pages())
        {auto found=m_PageSlots.find(key);if(found!=m_PageSlots.end())used[m_Pages[found->second].data.x/512]=true;}
        uint32_t next=0;
        for(const auto& key:builder.Pages())
        {
            GIWorldPageGPU page;page.key={key.x,key.y,key.z,key.level};auto found=m_PageSlots.find(key);
            if(found!=m_PageSlots.end())page=m_Pages[found->second];
            else
            {
                while(next<used.size() && used[next])++next;
                Require(next<used.size(),"GI page transaction exceeds physical brick capacity");
                page.data={next*512,0,0,0};used[next++]=true;
            }
            if(result.changed.IntersectsPage(key,m_Settings.voxelSize))page.data.y=0;
            auto slot=GIWorldHash(key)&(uint32_t(pages.size())-1);while(pages[slot].data.x!=~0u)slot=(slot+1)&(uint32_t(pages.size())-1);
            pages[slot]=page;slots.emplace(key,slot);
            if(key.level==int(builder.LevelCount())-1 && !page.data.y)++result.pendingCoarseBricks;
        }
        auto& parameters=result.parameters;parameters=m_Parameters;
        parameters.center=glm::vec4(builder.Center(),m_Settings.extinctionScale);
        parameters.grid.z=float(builder.LevelCount());parameters.grid.w=float(builder.AvailableLevels());
        parameters.table.y=uint32_t(builder.Pages().size());
        glm::vec3 voxelMin(FLT_MAX),voxelMax(-FLT_MAX);
        const float coarseSize=m_Settings.voxelSize*float(1u<<(builder.LevelCount()-1))*8;
        for(const auto& key:builder.Pages())if(key.level==int(builder.LevelCount())-1)
        {
            const auto lo=glm::vec3(key.x,key.y,key.z)*coarseSize;
            voxelMin=glm::min(voxelMin,lo);voxelMax=glm::max(voxelMax,lo+coarseSize);
        }
        if(builder.Instances().empty())voxelMin=voxelMax=glm::vec3(0);
        parameters.voxelMin=glm::vec4(voxelMin,0);parameters.voxelMax=glm::vec4(voxelMax,0);
    }
    void VansGIWorld::QueueBuild()
    {
        if(m_BuildFuture.valid() || m_ReadyBuild)return;
        const float scroll=m_Settings.voxelSize*32.f;
        const bool moved=glm::any(glm::greaterThan(glm::abs(m_ViewCenter-glm::vec3(m_Parameters.center)),glm::vec3(scroll)));
        const bool changedSources=m_InstanceGeneration!=m_PublishedInstanceGeneration;
        const bool needsWork=moved || m_Builder.PendingCount() || changedSources;
        if(!needsWork && !m_RetiredBuild)return;
        const auto center=glm::floor(m_ViewCenter/scroll)*scroll;
        std::optional<decltype(m_SourceChanges)> requests;if(changedSources)requests=m_SourceChanges;
        const auto instanceGeneration=m_InstanceGeneration;
        const auto submittingThread=std::this_thread::get_id();
        auto task=std::make_shared<std::packaged_task<BuildResult()>>([this,moved,needsWork,center,instanceGeneration,submittingThread,
            requests=std::move(requests),retired=std::move(m_RetiredBuild)]() mutable
        {
            BuildResult result;const bool onSubmittingThread=std::this_thread::get_id()==submittingThread;
            if(retired)
            {
                const auto start=std::chrono::steady_clock::now();retired.reset();
                result.cpu.retirementJobs=1;result.cpu.submissionThreadRetirements=onSubmittingThread?1:0;
                result.cpu.retirementMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            }
            if(!needsWork)return result;
            if(requests)
            {
                result.sourcesChanged=true;
                result.instanceGeneration=instanceGeneration;
                const std::vector<GIWorldInstance> empty;
                std::vector<GIWorldBounds> dirtyRegions;
                auto include=[&](const auto& before,const auto& after)
                {
                    const auto bounds=GIWorldChangedInstanceRegions(before,after);
                    dirtyRegions.insert(dirtyRegions.end(),bounds.begin(),bounds.end());
                };
                // 全区块引用表复制也属于后台规划；主线程只传递未发布的少量改动。
                result.chunks=m_SourceChunks;
                for(const auto& item:*requests)
                {
                    auto old=m_SourceChunks.find(item.first);
                    if(!item.second)
                    {
                        if(old!=m_SourceChunks.end())include(old->second->instances,empty);
                        result.chunks.erase(item.first);
                    }
                    else
                    {
                        auto chunk=std::make_shared<CapturedSource>();chunk->source=item.second;
                        if(!item.second->model->cells.empty())for(const auto& transform:item.second->transforms)
                            chunk->instances.push_back(MakeGIWorldInstance(item.second->model,transform));
                        result.preparedInstances+=chunk->instances.size();
                        include(old==m_SourceChunks.end()?empty:old->second->instances,chunk->instances);
                        result.chunks[item.first]=std::move(chunk);
                    }
                }
                result.changed=GIWorldDirtyRegions(std::move(dirtyRegions));
                size_t count=0;for(const auto& item:result.chunks)count+=item.second->instances.size();
                std::vector<GIWorldInstance> captured;captured.reserve(count);
                for(const auto& item:result.chunks)captured.insert(captured.end(),item.second->instances.begin(),item.second->instances.end());
                result.layoutIndex=BuildLayoutIndex(captured);
                result.replacement=std::make_unique<VansGIWorldBuilder>();
                result.replacement->Reset(m_Settings,std::move(captured),center);
                Require((result.replacement->AvailableLevels()&(1u<<(result.replacement->LevelCount()-1)))!=0,
                    "GI edited world exceeds the supported hierarchical coordinate range");
            }
            else if(moved)
            {
                // 共享不可变来源索引，后台只查询相机邻域；旧场继续服务。
                result.replacement=std::make_unique<VansGIWorldBuilder>();
                result.replacement->Recenter(m_Builder,center);
            }
            if(result.replacement)
            {
                const auto start=std::chrono::steady_clock::now();PlanPages(result);
                result.cpu.pagePlanJobs=1;result.cpu.pagesPlanned=result.pageSlots.size();
                result.cpu.submissionThreadPagePlans=onSubmittingThread?1:0;
                result.cpu.pagePlanMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            }
            auto& builder=result.replacement?*result.replacement:m_Builder;
            result.bricks.reserve(m_Settings.bricksPerFrame);
            const auto start=std::chrono::steady_clock::now();
            for(uint32_t i=0;i<m_Settings.bricksPerFrame;++i)
            {
                while(builder.PendingCount())
                {
                    const auto found=m_PageSlots.find(builder.NextPage());
                    if(found==m_PageSlots.end() || !m_Pages[found->second].data.y)break;
                    if(result.changed.IntersectsPage(builder.NextPage(),m_Settings.voxelSize))break;
                    builder.SkipNext();
                }
                if(i&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>2.0)break;
                GIWorldBrick brick;if(!builder.BuildNext(brick,2048))break;
                result.bricks.push_back(std::move(brick));
            }
            return result;
        });
        m_BuildFuture=task->get_future();
        // QueueJobGroup 在未启动线程池的原生小测试中同步执行；正式运行使用已有线程池。
        Vans::VansJobSystem::Get().QueueJobGroup({[task](){(*task)();}});
    }
    const GIWorldDirtyRegions* VansGIWorld::PrepareUpdates()
    {
        if(m_ReadyBuild || !m_BuildFuture.valid() ||
            m_BuildFuture.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
        m_ReadyBuild=m_BuildFuture.get();auto& completed=*m_ReadyBuild;
        m_CpuWorkStats.pagePlanJobs+=completed.cpu.pagePlanJobs;m_CpuWorkStats.pagesPlanned+=completed.cpu.pagesPlanned;
        m_CpuWorkStats.retirementJobs+=completed.cpu.retirementJobs;m_CpuWorkStats.submissionThreadPagePlans+=completed.cpu.submissionThreadPagePlans;
        m_CpuWorkStats.submissionThreadRetirements+=completed.cpu.submissionThreadRetirements;
        m_CpuWorkStats.pagePlanMilliseconds+=completed.cpu.pagePlanMilliseconds;m_CpuWorkStats.retirementMilliseconds+=completed.cpu.retirementMilliseconds;
        // 连续刷绘期间旧事务只持有候选索引，不能覆盖较新的来源请求。
        if(completed.sourcesChanged && completed.instanceGeneration!=m_InstanceGeneration)
        {m_RetiredBuild=std::make_unique<BuildResult>(std::move(completed));m_ReadyBuild.reset();return {};}
        if(completed.replacement)
        {
            // 只交换容器所有权；旧索引、页表和来源随 completed 退役，不在渲染线程析构。
            std::swap(m_Builder,*completed.replacement);m_Pages.swap(completed.pages);m_PageSlots.swap(completed.pageSlots);
            m_Parameters=completed.parameters;m_PendingCoarseBricks=completed.pendingCoarseBricks;
        }
        m_PendingBricks=m_Builder.PendingCount();
        if(completed.sourcesChanged)
        {
            m_LayoutIndex.swap(completed.layoutIndex);m_SourceChunks.swap(completed.chunks);
            m_SourceChanges.swap(completed.retiredChanges);m_PublishedInstanceGeneration=completed.instanceGeneration;
            m_SourceUpdateStats.preparedInstances=completed.preparedInstances;
        }
        return completed.changed.Empty()?nullptr:&completed.changed;
    }
    void VansGIWorld::RecordUpdates(VansVKCommandBuffer& command)
    {
        // 高度数据和探针查询在同一 GI 提交链发布，不依赖异步 GBuffer 队列的录制顺序。
        RecordTerrainUpdates(command);
        if(!m_ReadyBuild){QueueBuild();return;}
        auto completed=std::move(*m_ReadyBuild);m_ReadyBuild.reset();
        const bool moved=bool(completed.replacement);
        if(!moved&&completed.bricks.empty()){QueueBuild();return;}
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        if(moved)
        {
            const auto* bytes=reinterpret_cast<const uint8_t*>(m_Pages.data());size_t total=m_Pages.size()*sizeof(GIWorldPageGPU);
            for(size_t offset=0;offset<total;offset+=65536)command.UpdateBuffer(m_Buffers[3].GetNativeBuffer(),offset,std::min<size_t>(65536,total-offset),bytes+offset);
            command.UpdateBuffer(m_Buffers[0].GetNativeBuffer(),0,sizeof(m_Parameters),&m_Parameters);
            if(!completed.bricks.empty())
            {
                barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            }
        }
        // 砖键属于本批次的世界页；先发布相同代次的页表，才能写入新分配的物理地址。
        for(const auto& brick:completed.bricks)
        {
            const auto slot=m_PageSlots.at(brick.key);auto& page=m_Pages[slot];
            command.UpdateBuffer(m_Buffers[4].GetNativeBuffer(),VkDeviceSize(page.data.x)*sizeof(GIWorldVoxel),sizeof(brick.voxels),brick.voxels.data());
            if(!page.data.y && brick.key.level==int(m_Builder.LevelCount())-1)--m_PendingCoarseBricks;
            page.data.y=1;command.UpdateBuffer(m_Buffers[3].GetNativeBuffer(),VkDeviceSize(slot)*sizeof(GIWorldPageGPU),sizeof(GIWorldPageGPU),&page);
        }
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});++m_Revision;
        m_RetiredBuild=std::make_unique<BuildResult>(std::move(completed));QueueBuild();
    }
}
