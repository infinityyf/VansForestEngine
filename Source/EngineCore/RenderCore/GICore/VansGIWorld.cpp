#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansGIWorld.h"
#include "VansGIProbeDispatchParameters.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansProfiler.h"
#include "VansGIFrameCost.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansShaderManager.h"
#include "VansGIVoxelSource.h"
#include "../TerrainCore/VansTerrain.h"
#include "../../TerrainCore/VansTerrainHeightEncoding.h"
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
#include "../../AssetCore/Importers/Shader/VansShaderArtifactCache.h"

namespace VansGraphics
{
    namespace
    {
        std::atomic<uint64_t> WorldGeneration{0};
        constexpr uint32_t MaxWorldMaterials=65536u;
        constexpr size_t MaxTemplateCells=1048576u;
        // Largest shipped forest template needs about 78 MiB including capture/decode inputs.
        // This limits the single active batch; it does not reserve a persistent allocation.
        constexpr size_t MaxPendingTemplateBytes=80u*1024u*1024u;
        void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

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
    uint64_t VansGIWorld::BufferCapacityBytes() const
    {uint64_t bytes=0;for(const auto& b:m_Buffers)bytes+=b.GetBufferSize();return bytes;}
    VansGIMemoryUsage VansGIWorld::GetMemoryUsage() const
    {
        VansGIMemoryUsage usage;
        for (const auto& buffer : m_Buffers)
        {
            usage.bufferCapacityBytes += buffer.GetBufferSize();
            usage.bufferAllocationBytes += buffer.GetAllocationSize();
        }
        return usage;
    }
    std::unique_ptr<VansComputeShader> VansGIWorld::LoadShader(const std::string& file,uint32_t pushBytes)
    {
        auto shader=std::make_unique<VansComputeShader>();
        VansPipelineProgramDesc desc{};desc.name="GIWorld/"+file;desc.shaderPath=m_ShaderFolder;desc.kind=VansPipelineProgramKind::Compute;desc.pushConstantSize=pushBytes;
        shader->SetName(desc.name);shader->SetPipelineProgramDesc(desc);shader->SetArtifactRoot(m_ShaderArtifactRoot);
        Require(shader->InitShader(m_Device->GetLogicDevice(),m_ShaderFolder,{{VK_SHADER_STAGE_COMPUTE_BIT,file+".comp"}}),"GI world shader load failed");
        // InitShader 会重置接口；与 ShaderManager 一样，在加载成功后发布计算管线和常量范围。
        shader->SetPipelineProgramDesc(desc);shader->SetPushConstant(pushBytes);
        return shader;
    }
    void VansGIWorld::MakeBuffer(VansVKBuffer& buffer,const void* data,size_t bytes,VkBufferUsageFlags usage,size_t capacity)
    {
        const size_t actual=std::max({bytes,capacity,size_t(16)});
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
        const auto& shader=m_TextureCaptureShader;
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
    std::shared_ptr<VansGIWorld::SourceTemplate> VansGIWorld::FindTemplate(const GIVoxelSource& source) const
    {
        const auto found=std::find_if(m_SourceTemplates.begin(),m_SourceTemplates.end(),[&](const auto& item)
            {return item->identity==source.modelIdentity && item->parts==source.parts;});
        return found==m_SourceTemplates.end()?nullptr:*found;
    }
    std::shared_ptr<VansGIWorld::SourceTemplate> VansGIWorld::CaptureTemplate(const GIVoxelSource& source,
        uint32_t materialBase,std::vector<glm::vec4>& materials,size_t byteBudget)
    {
        VANS_CPU_SCOPE(GIProfileScope(GICostStage::WorldCapture));
        Require(uint64_t(materialBase)+materials.size()+source.parts.size()<=MaxWorldMaterials,"GI voxel material table overflow");
        auto definition=std::make_shared<SourceTemplate>();definition->identity=source.modelIdentity;definition->parts=source.parts;
        definition->materialInputs=CaptureMaterialInputs(source.parts);
        definition->materialBase=materialBase+uint32_t(materials.size());
        VansGITemplateCapture capture;
        capture.Prepare(*m_Device,{{source.parts,definition->materialBase,definition->materialInputs}},0,byteBudget,m_TextureCaptureShader);
        capture.Submit();capture.WaitForInitialization();auto output=capture.Decode();
        materials.insert(materials.end(),output[0].materials.begin(),output[0].materials.end());
        definition->materialBaseColors=std::move(output[0].materialBaseColors);
        definition->captured=std::move(output[0].meshes);
        definition->capturedBytes=sizeof(SourceTemplate)+definition->parts.capacity()*sizeof(GIVoxelSourcePart)+
            definition->materialInputs.capacity()*sizeof(VansGIVoxelMaterial)+output[0].retainedBytes;
        return definition;
    }

    void VansGIWorld::CaptureGeometry(const std::vector<GIVoxelSource>& sources,std::vector<GIWorldInstance>& instances)
    {
        for(const auto& source:sources)
        {
            // 缺少流送资源时不建立空几何，也不把它解释为删除；查询保持既有 unknown 语义。
            if(source.availability!=GIVoxelSourceAvailability::Available)continue;
            Require(!source.key.empty() && source.modelIdentity && !m_SourceChunks.count(source.key),"GI source requires a unique key and immutable model identity");
            auto definition=FindTemplate(source);
            if(!definition)
            {
                std::vector<glm::vec4> materials;
                definition=CaptureTemplate(source,uint32_t(m_Materials.size()),materials,SIZE_MAX);
                auto baked=std::make_shared<GIWorldTemplate>();std::string error;
                if(!BakeGIWorldTemplate(*definition->captured,m_Settings.voxelSize,uint32_t(MaxTemplateCells),*baked,error))throw std::runtime_error(error);
                m_TemplateCells+=baked->CellCount();Require(m_TemplateCells<=MaxTemplateCells,"GI world template cache exceeds one million occupied cells; increase voxel size");
                definition->model=std::move(baked);definition->captured.reset();definition->capturedBytes=0;
                m_Materials.insert(m_Materials.end(),materials.begin(),materials.end());m_SourceTemplates.push_back(definition);
            }
            const auto& model=definition->model;
            auto request=std::make_shared<InstanceSource>();request->definition=definition;request->transforms=source.instances;
            auto captured=std::make_shared<CapturedSource>();captured->source=request;
            if(!model->cells.empty())for(const auto& transform:source.instances)captured->instances.push_back(MakeGIWorldInstance(model,transform));
            m_SourceChunks.emplace(source.key,std::move(captured));
        }
        // 初始捕获和后续区块发布都按稳定键排列，编辑不能改变未修改实例的累加顺序。
        for(const auto& item:m_SourceChunks)instances.insert(instances.end(),item.second->instances.begin(),item.second->instances.end());
    }
    void VansGIWorld::Initialize(VansVKDevice& device,VansScene& scene,const GIWorldSettings& settings,bool hardware,uint32_t rayCapacity,
        const std::shared_ptr<const VansGIVoxelMaterialInputs>& materials)
    {
        Require(settings.enabled,"Disabled GI world must not allocate resources");m_Device=&device;m_Settings=settings;
        m_MaterialInputs=materials;
        m_QueryPublication.generation=++WorldGeneration;
        auto* base=VansShaderManager::Get().FindComputeShader("GIPointLight");Require(base,"GI base shader is missing");
        m_ShaderFolder=(std::filesystem::path(base->GetShaderFolder()).parent_path()/"GIWorld").string();
        m_ShaderArtifactRoot=base->GetArtifactRoot();
        m_TextureCaptureShader=LoadShader("GIWorldTexture",16);
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
            for(size_t i=0;i<asset.heights.size();++i)
                asset.heights[i]=Vans::VansTerrainHeightEncoding::EncodeNormalized(heightPixels[i].r);
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
        MakeBuffer(m_Buffers[5],m_Materials.data(),m_Materials.size()*sizeof(glm::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            size_t(MaxWorldMaterials)*sizeof(glm::vec4));
        MakeBuffer(m_Buffers[6],terrainColors.data(),terrainColors.size()*sizeof(glm::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MakeBuffer(m_Buffers[7],nullptr,sizeof(uint32_t)*kGIMaximumRegionCount+size_t(std::max(rayCapacity,1u))*sizeof(glm::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        auto* manager=VansVKDescriptorManager::GetInstance();std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(uint32_t i=0;i<m_Buffers.size();++i)bindings.push_back({i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        Require(manager->CreateDesciptorSetLayout(bindings,m_Layout)&&manager->AllocateDescriptorSet({m_Layout},m_Sets),"GI world descriptors failed");
        manager->BeginDescriptorUpdate();for(uint32_t i=0;i<m_Buffers.size();++i)manager->WriteBufferDescriptor(m_Sets[0],i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{m_Buffers[i].GetNativeBuffer(),0,m_Buffers[i].GetBufferSize()}});manager->UpdateDescriptorSets();
        m_Trace=LoadShader("GIWorldTrace",sizeof(VansGIProbeDispatchParameters));m_Lighting=LoadShader("GIWorldLighting",sizeof(VansGIProbeDispatchParameters));
        m_Visibility=LoadShader("GIWorldReceiverVisibility");m_Bias=LoadShader("GIWorldReceiverBias");
        m_Atlas=LoadShader("GIWorldAtlas",sizeof(VansGIProbeDispatchParameters));m_State=LoadShader("GIWorldState",sizeof(VansGIProbeDispatchParameters));
    }
    uint32_t VansGIWorld::RecordScatterRanges(VansVKCommandBuffer& command,const std::array<uint32_t,kGIMaximumRegionCount>& rayCounts)
    {
        std::array<glm::uvec4,kGIMaximumRegionCount/4> offsets{};uint64_t total=0;
        for(size_t i=0;i<rayCounts.size();++i)
        {
            Require(total<=UINT32_MAX,"GI scatter offset overflow");
            offsets[i/4][i%4]=uint32_t(total);total+=rayCounts[i];
        }
        Require(total<=(m_Buffers[7].GetBufferSize()-sizeof(offsets))/sizeof(glm::vec4),"GI scatter work exceeds shared ray capacity");
        command.UpdateBuffer(m_Buffers[7].GetNativeBuffer(),0,sizeof(offsets),offsets.data());
        return sizeof(offsets);
    }
    bool VansGIWorld::ApplyHeightPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,
        const std::vector<uint8_t>& pixels,GIWorldHeightData::Patch& changed)
    {
        std::string error;
        if(!m_Height.ApplyPatch(x,z,w,h,pixels,changed,error))return false;
        m_PendingHeightSpans.insert(m_PendingHeightSpans.end(),changed.heights.begin(),changed.heights.end());
        m_PendingRangeSpans.insert(m_PendingRangeSpans.end(),changed.ranges.begin(),changed.ranges.end());
        if(!changed.heights.empty())
            VansGIWorldQueryChanges::Include(m_PendingGeometryBounds,{changed.minimum,changed.maximum});
        return true;
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
    void VansGIWorld::RecordTerrainUpdates(VansVKCommandBuffer& command,VansGIWorldUploadWork& work)
    {
        if(m_PendingHeightSpans.empty() && m_PendingRangeSpans.empty() && m_PendingColorSpans.empty())return;
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        auto upload=[&](uint32_t buffer,const void* data,uint32_t stride,std::vector<glm::uvec2>& spans,uint64_t& uploadedBytes)
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
                {const size_t length=std::min<size_t>(end-offset,65536);command.UpdateBuffer(m_Buffers[buffer].GetNativeBuffer(),offset,length,bytes+offset);offset+=length;uploadedBytes+=length;++work.updateCommands;}
            }
            spans.clear();
        };
        upload(1,m_Height.heights.data(),sizeof(float),m_PendingHeightSpans,work.heightBytes);
        upload(2,m_Height.ranges.data(),sizeof(glm::vec2),m_PendingRangeSpans,work.rangeBytes);
        upload(6,m_TerrainColors.colors.data(),sizeof(glm::vec4),m_PendingColorSpans,work.colorBytes);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
    }
    bool VansGIWorld::IsPositionValid(glm::vec3 p, float clearance) const
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
    }
    VansGeometrySurfaceMeasure VansGIWorld::MeasureSurface(glm::vec3 lo,glm::vec3 hi) const
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
    }
    bool VansGIWorld::CookShaders(const std::string& shaderRoot,
        const std::filesystem::path& artifactRoot,
        std::vector<Vans::VansShaderCookProgram>& programs,std::string& error)
    {
        for(const char* name:{"GIWorldTexture","GIWorldTrace","GIWorldLighting","GIWorldReceiverVisibility","GIWorldReceiverBias","GIWorldAtlas","GIWorldState"})
        {
            Vans::VansShaderCompileRequest request;request.programId=std::string("GIWorld/")+name;
            request.sourceFolder=std::filesystem::path(shaderRoot)/"GIWorld";request.includeRoots.push_back(shaderRoot);
            request.artifactRoot=artifactRoot;
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
    void VansGIWorld::SubmitCapture(const std::shared_ptr<VansGITemplateCapture>& capture,
        const std::vector<std::shared_ptr<SourceTemplate>>& definitions)
    {
        VANS_CPU_SCOPE(GIProfileScope(GICostStage::WorldCapture));
        size_t bytes=0;
        for(const auto& definition:definitions)if(definition->captureBatch==capture)
            bytes+=capture->EstimatedBytes(definition->captureIndex);
        Require(m_PendingTemplateBytes<=MaxPendingTemplateBytes && bytes<=MaxPendingTemplateBytes-m_PendingTemplateBytes,
            "GI active capture input budget exhausted");
        capture->Submit();
        for(const auto& definition:definitions)if(definition->captureBatch==capture)
            definition->capturedBytes=capture->EstimatedBytes(definition->captureIndex);
        m_PendingTemplateBytes+=bytes;++m_CpuWorkStats.captureSubmissions;
        m_CpuWorkStats.peakCaptureInputBytes=std::max<uint64_t>(m_CpuWorkStats.peakCaptureInputBytes,m_PendingTemplateBytes);
    }
    std::vector<VansGIVoxelMaterial> VansGIWorld::CaptureMaterialInputs(const std::vector<GIVoxelSourcePart>& parts) const
    {
        Require(bool(m_MaterialInputs),"GI source has no published material inputs");
        std::vector<VansGIVoxelMaterial> values;values.reserve(parts.size());
        for(const auto& part:parts)
        {
            const auto found=m_MaterialInputs->find(part.material);
            Require(found!=m_MaterialInputs->end(),"GI source material is absent from the published asset snapshot");
            values.push_back(found->second.ForSourcePart(part.leaf));
        }
        return values;
    }

    bool VansGIWorld::MaterialCoverageChanged(const SourceTemplate& definition) const
    {
        if(!m_MaterialInputs)return false;
        for(size_t part=0;part<definition.parts.size();++part)
        {
            const auto found=m_MaterialInputs->find(definition.parts[part].material);
            if(found!=m_MaterialInputs->end() &&
                !found->second.ForSourcePart(definition.parts[part].leaf).CoverageEquals(definition.materialInputs.at(part)))return true;
        }
        return false;
    }

    std::vector<std::shared_ptr<VansGITemplateCapture>> VansGIWorld::PrepareCaptures(const std::vector<std::shared_ptr<SourceTemplate>>& definitions)
    {
        std::vector<GITemplateCaptureInput> inputs;inputs.reserve(definitions.size());
        for(const auto& definition:definitions)inputs.push_back({definition->parts,definition->materialBase,definition->materialInputs});
        std::vector<std::shared_ptr<VansGITemplateCapture>> batches;
        for(uint32_t first=0;first<inputs.size();)
        {
            auto capture=std::make_shared<VansGITemplateCapture>();
            const auto count=capture->Prepare(*m_Device,inputs,first,MaxPendingTemplateBytes,m_TextureCaptureShader);
            Require(count>0,"GI capture preparation made no progress");
            for(uint32_t i=0;i<count;++i)
            {definitions[first+i]->captureBatch=capture;definitions[first+i]->captureIndex=i;}
            batches.push_back(std::move(capture));first+=count;
        }
        return batches;
    }

    void VansGIWorld::QueueMaterialRefresh()
    {
        // 只保留最新输入；已有来源/捕获事务先完成，避免每次拖动都堆积一份候选和 GPU 租约。
        if(!m_MaterialRefreshPending || !m_CaptureBatches.empty() || m_RetiredBuild ||
            m_InstanceGeneration!=m_PublishedInstanceGeneration)return;
        m_MaterialRefreshPending=false;
        try
        {
            std::set<const SourceTemplate*> used;
            for(const auto& chunk:m_SourceChunks)used.insert(chunk.second->source->definition.get());
            std::map<const SourceTemplate*,std::shared_ptr<SourceTemplate>> replacements;
            std::vector<std::shared_ptr<SourceTemplate>> definitions;
            for(const auto& old:m_SourceTemplates)
                if(used.count(old.get()) && MaterialCoverageChanged(*old))
                {
                    Require(old->model && !old->previous,"GI coverage refresh requires a published template");
                    auto next=std::make_shared<SourceTemplate>();next->identity=old->identity;next->parts=old->parts;
                    next->materialBase=old->materialBase;next->materialInputs=CaptureMaterialInputs(next->parts);next->previous=old;
                    replacements.emplace(old.get(),next);definitions.push_back(std::move(next));
                }
            if(definitions.empty())return;
            SourceChanges prepared;
            for(const auto& chunk:m_SourceChunks)
                if(auto found=replacements.find(chunk.second->source->definition.get());found!=replacements.end())
                {
                    auto request=std::make_shared<InstanceSource>();request->definition=found->second;
                    request->transforms=chunk.second->source->transforms;prepared.emplace(chunk.first,std::move(request));
                }
            auto batches=PrepareCaptures(definitions);
            m_CaptureBatches.reserve(batches.size());
            SubmitCapture(batches.front(),definitions);
            // 从此处起只转移已分配的所有权；拒绝提交不改变旧场、缓存索引或材质槽。
            for(auto& cached:m_SourceTemplates)
                if(auto found=replacements.find(cached.get());found!=replacements.end())cached=found->second;
            m_CaptureBatches=std::move(batches);m_SourceChanges=std::move(prepared);++m_InstanceGeneration;
        }
        catch(const std::exception& error)
        {VANS_LOG_ERROR("[GIWorldMaterial] coverage capture rejected; previous field retained: "<<error.what());}
    }

    bool VansGIWorld::RollbackMaterialRefresh(std::shared_ptr<VansGITemplateCapture> failedCapture)
    {
        // 只回滚覆盖候选。新增来源没有旧模板；GPU 尚未确认完成时也不能局部释放票据。
        if(!failedCapture)return false;
        bool coverageFailure=false;
        std::set<std::shared_ptr<VansGITemplateCapture>> captures;
        size_t candidateCount=0;
        for(const auto& definition:m_SourceTemplates)
        {
            if(definition->captureBatch==failedCapture)
            {
                if(!definition->previous)return false;
                coverageFailure=true;
            }
            if(!definition->previous)continue;
            ++candidateCount;
            if(definition->captureBatch)
            {
                if(definition->captureBatch->Submitted() && !definition->captureBatch->Ready())return false;
                captures.insert(definition->captureBatch);
            }
        }
        if(!coverageFailure)return false;
        SourceChanges prepared=m_SourceChanges;
        for(auto item=prepared.begin();item!=prepared.end();)
        {
            if(!item->second || !item->second->definition->previous){++item;continue;}
            auto restored=std::make_shared<InstanceSource>(*item->second);
            restored->definition=restored->definition->previous;
            const auto published=m_SourceChunks.find(item->first);
            // 保留捕获期间的新变换/来源请求；没有实际来源变化的覆盖请求不再重建旧页表。
            if(published!=m_SourceChunks.end() && published->second->source->definition==restored->definition &&
                published->second->source->transforms==restored->transforms)item=prepared.erase(item);
            else{item->second=std::move(restored);++item;}
        }
        if(!m_RetiredBuild)m_RetiredBuild=std::make_unique<BuildResult>();
        auto& retired=m_RetiredBuild->retiredTemplates;
        retired.reserve(retired.size()+candidateCount);
        for(auto& definition:m_SourceTemplates)if(definition->previous)
        {
            auto published=definition->previous;
            if(definition->model)
            {m_TemplateCells-=definition->model->CellCount();m_TemplateCells+=published->model->CellCount();}
            m_PendingTemplateBytes-=definition->capturedBytes;
            retired.push_back(std::move(definition));definition=std::move(published);
        }
        m_CaptureBatches.erase(std::remove_if(m_CaptureBatches.begin(),m_CaptureBatches.end(),
            [&](const auto& capture){return captures.count(capture)!=0;}),m_CaptureBatches.end());
        m_SourceChanges.swap(prepared);++m_InstanceGeneration;
        if(m_SourceChanges.empty())m_PublishedInstanceGeneration=m_InstanceGeneration;
        // 失败本身不重试；捕获期间后续输入的 pending 通知仍由原流程消费。
        return true;
    }

    bool VansGIWorld::UpdateTemplateMaterials(const SourceTemplate& definition)
    {
        if(!m_MaterialInputs || definition.materialBaseColors.empty() || definition.previous)return false;
        Require(definition.materialBaseColors.size()==definition.parts.size(),"GI material base-color count mismatch");
        bool changed=false;
        for(size_t part=0;part<definition.parts.size();++part)
        {
            const auto found=m_MaterialInputs->find(definition.parts[part].material);
            if(found==m_MaterialInputs->end())continue;
            const auto next=found->second.Resolve(definition.materialBaseColors[part]);
            const size_t slot=definition.materialBase+part;
            if(m_Materials.at(slot)==next)continue;
            m_Materials[slot]=next;changed=true;
            m_PendingMaterialBegin=std::min(m_PendingMaterialBegin,slot);
            m_PendingMaterialEnd=std::max(m_PendingMaterialEnd,slot+1);
        }
        return changed;
    }

    void VansGIWorld::ApplyMaterials(const std::shared_ptr<const VansGIVoxelMaterialInputs>& inputs)
    {
        if(inputs==m_MaterialInputs)return;
        const auto previous=m_MaterialInputs;
        m_MaterialInputs=inputs;
        std::set<const SourceTemplate*> changed;
        for(const auto& definition:m_SourceTemplates)
        {
            if(inputs)for(const auto& part:definition->parts)
            {
                const auto value=inputs->find(part.material);
                if(value==inputs->end())continue;
                if(!previous || !previous->count(part.material) ||
                    !value->second.ForSourcePart(part.leaf).CoverageEquals(previous->at(part.material).ForSourcePart(part.leaf)))
                    m_MaterialRefreshPending=true;
            }
            // 覆盖候选尚未发布时，改色仍更新正在被旧场引用的均值与材质槽。
            const auto& published=definition->previous?definition->previous:definition;
            if(UpdateTemplateMaterials(*published))changed.insert(published.get());
        }
        if(changed.empty())return;
        // 仅内容变化时遍历来源范围；稳态只比较不可变快照身份，不扫描实例。
        for(const auto& item:m_SourceChunks)
            if(changed.count(item.second->source->definition.get()))
                for(const auto& instance:item.second->instances)
                    VansGIWorldQueryChanges::Include(m_PendingLightingBounds,{instance.minimum,instance.maximum});
    }

    bool VansGIWorld::QueueSourceChanges(GIVoxelSourceChanges changes)
    {
        if(m_SourceDirty)return false;
        if(changes.Empty())return true;
        std::map<std::string,std::shared_ptr<const InstanceSource>> prepared;
        std::vector<std::shared_ptr<SourceTemplate>> capturedTemplates;
        std::vector<glm::vec4> capturedMaterials;
        std::vector<std::shared_ptr<VansGITemplateCapture>> captureBatches;
        bool refreshMaterials=false;
        uint64_t submittedInstances=0;
        std::set<std::string> keys;
        for(const auto& key:changes.removed)Require(!key.empty() && keys.insert(key).second,"Invalid or duplicate GI source removal");
        for(auto& source:changes.updated)
        {
            if(source.availability!=GIVoxelSourceAvailability::Available)continue;
            Require(!source.key.empty() && source.modelIdentity && keys.insert(source.key).second,"Invalid or duplicate GI source update");
            auto definition=FindTemplate(source);
            if(definition && MaterialCoverageChanged(*definition))refreshMaterials=true;
            if(!definition)
            {
                const auto found=std::find_if(capturedTemplates.begin(),capturedTemplates.end(),[&](const auto& item)
                    {return item->identity==source.modelIdentity && item->parts==source.parts;});
                if(found!=capturedTemplates.end())definition=*found;
                else
                {
                    // 捕获失败不提交本批任何来源或材质；调用方沿既有失败链保留旧 owner。
                    try
                    {
                        Require(m_SourceTemplates.size()+capturedTemplates.size()<MaxWorldMaterials,"GI source template cache is full");
                        Require(m_Materials.size()+capturedMaterials.size()+source.parts.size()<=MaxWorldMaterials,"GI voxel material table overflow");
                        definition=std::make_shared<SourceTemplate>();definition->identity=source.modelIdentity;definition->parts=source.parts;
                        definition->materialBase=uint32_t(m_Materials.size()+capturedMaterials.size());
                        definition->materialInputs=CaptureMaterialInputs(source.parts);
                        capturedMaterials.resize(capturedMaterials.size()+source.parts.size(),glm::vec4(0));
                    }
                    catch(const std::exception& error)
                    {VANS_LOG_ERROR("[GIWorldTemplate] capture rejected: "<<error.what());return false;}
                    capturedTemplates.push_back(definition);
                }
            }
            submittedInstances+=source.instances.size();
            auto request=std::make_shared<InstanceSource>();request->definition=std::move(definition);request->transforms=std::move(source.instances);
            prepared.emplace(std::move(source.key),std::move(request));
        }
        if(!capturedTemplates.empty())
        {
            try
            {
                VANS_CPU_SCOPE(GIProfileScope(GICostStage::WorldCapture));
                captureBatches=PrepareCaptures(capturedTemplates);
            }
            catch(const std::exception& error)
            {VANS_LOG_ERROR("[GIWorldCapture] preparation rejected: "<<error.what());return false;}
        }
        // 请求表包含所有尚未发布的修改；A 被 B 合并时，不能丢掉 A 修改过的其他区块。
        const auto updatedChunks=prepared.size();
        for(const auto& key:changes.removed)prepared.emplace(key,nullptr);
        // 提交前准备容量，同时保持摊销增长；逐个引入新模型不能每次复制整个缓存。
        const auto reserveAppend=[](auto& destination,size_t count)
        {
            const size_t required=destination.size()+count;
            if(required>destination.capacity())
                destination.reserve(std::max(required,std::min(size_t(MaxWorldMaterials),destination.capacity()*2)));
        };
        reserveAppend(m_SourceTemplates,capturedTemplates.size());
        reserveAppend(m_Materials,capturedMaterials.size());
        reserveAppend(m_CaptureBatches,captureBatches.size());
        if(!captureBatches.empty() && m_CaptureBatches.empty() && m_PendingTemplateBytes==0 &&
            !m_BuildFuture.valid() && !m_ReadyBuild && (!m_RetiredBuild || !m_RetiredBuild->HasRetainedInputs()))
        {
            try{SubmitCapture(captureBatches.front(),capturedTemplates);}
            catch(const std::exception& error)
            {VANS_LOG_ERROR("[GIWorldCapture] submission rejected: "<<error.what());return false;}
        }
        m_CaptureBatches.insert(m_CaptureBatches.end(),captureBatches.begin(),captureBatches.end());
        m_Materials.insert(m_Materials.end(),capturedMaterials.begin(),capturedMaterials.end());
        m_SourceTemplates.insert(m_SourceTemplates.end(),capturedTemplates.begin(),capturedTemplates.end());
        // 所有节点在提交前分配；覆盖已有键或转移 map 节点，不复制累计请求表。
        for(auto& item:prepared)
            if(auto found=m_SourceChanges.find(item.first);found!=m_SourceChanges.end())found->second.swap(item.second);
        m_SourceChanges.merge(prepared);
        m_SourceUpdateStats.updatedChunks=updatedChunks;m_SourceUpdateStats.removedChunks=changes.removed.size();
        m_SourceUpdateStats.submittedInstances=submittedInstances;
        m_MaterialRefreshPending|=refreshMaterials;
        if(updatedChunks==0 && changes.removed.empty())return true;
        ++m_InstanceGeneration;return true;
    }
    void VansGIWorld::PlanPages(BuildResult& result) const
    {
        // 与砖规划同一后台事务；发布前旧页表只读，旧场仍可被 GPU 查询。
        const auto& builder=*result.replacement;
        result.queryChanges.geometry=result.changed.Bounds();
        auto includePage=[&](const GIWorldCell& key)
        {
            const float size=m_Settings.voxelSize*8.f*float(1u<<key.level);
            const auto lo=glm::vec3(key.x,key.y,key.z)*size;
            VansGIWorldQueryChanges::Include(result.queryChanges.residency,{lo,lo+size});
        };
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
                includePage(key);
            }
            if(result.changed.IntersectsPage(key,m_Settings.voxelSize))
            {
                if(page.data.y)includePage(key);
                page.data.y=0;
            }
            auto slot=GIWorldHash(key)&(uint32_t(pages.size())-1);while(pages[slot].data.x!=~0u)slot=(slot+1)&(uint32_t(pages.size())-1);
            pages[slot]=page;slots.emplace(key,slot);
            result.readyBricks+=page.data.y!=0;
            if(key.level==int(builder.LevelCount())-1 && !page.data.y)++result.pendingCoarseBricks;
        }
        for(const auto& old:m_PageSlots)if(slots.find(old.first)==slots.end())includePage(old.first);
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
        // 页内容未变时，相机中心、可用层级和全局裁剪盒仍可能改变实际查询路径。
        if(parameters.grid.z!=m_Parameters.grid.z || parameters.grid.w!=m_Parameters.grid.w ||
            parameters.voxelMin!=m_Parameters.voxelMin || parameters.voxelMax!=m_Parameters.voxelMax)
        {
            if(m_Parameters.table.y)VansGIWorldQueryChanges::Include(result.queryChanges.residency,
                {glm::vec3(m_Parameters.voxelMin),glm::vec3(m_Parameters.voxelMax)});
            if(parameters.table.y)VansGIWorldQueryChanges::Include(result.queryChanges.residency,{voxelMin,voxelMax});
        }
        else if(parameters.table.y && parameters.center!=m_Parameters.center)
        {
            float radius=0;
            for(uint32_t level=0;level+1<builder.LevelCount();++level)
                if(builder.AvailableLevels()&(1u<<level))
                    radius=std::max(radius,std::min(m_Settings.coverageDistance,m_Settings.voxelSize*32.f*float(1u<<level)));
            if(radius>0)for(auto center:{glm::vec3(m_Parameters.center),glm::vec3(parameters.center)})
                VansGIWorldQueryChanges::Include(result.queryChanges.residency,{center-radius,center+radius});
        }
    }
    void VansGIWorld::QueueBuild()
    {
        if(m_SourceDirty || m_BuildFuture.valid() || m_ReadyBuild)return;
        QueueMaterialRefresh();
        std::shared_ptr<VansGITemplateCapture> captureWork;
        if(!m_CaptureBatches.empty())
        {
            const auto& capture=m_CaptureBatches.front();
            const bool needed=std::any_of(m_SourceChanges.begin(),m_SourceChanges.end(),[&](const auto& item)
                {return item.second && item.second->definition->captureBatch==capture;});
            // 上一批原始/解码输入必须先在后台退役，下一批才可以占用预算。
            if(!capture->Submitted() && needed && (!m_RetiredBuild || !m_RetiredBuild->HasRetainedInputs()) && m_PendingTemplateBytes==0)
            {
                try{SubmitCapture(capture,m_SourceTemplates);}
                catch(const std::exception& error)
                {
                    VANS_LOG_ERROR("[GIWorldCapture] queued submission rejected; previous field retained: "<<error.what());
                    if(!RollbackMaterialRefresh(capture))m_SourceDirty=true;
                    return;
                }
            }
            if(capture->Ready() || (!capture->Submitted() && !needed))captureWork=capture;
        }
        const float scroll=m_Settings.voxelSize*32.f;
        const bool moved=glm::any(glm::greaterThan(glm::abs(m_ViewCenter-glm::vec3(m_Parameters.center)),glm::vec3(scroll)));
        const bool hasChanges=m_InstanceGeneration!=m_PublishedInstanceGeneration;
        const bool changedSources=hasChanges && (m_CaptureBatches.empty() || (m_CaptureBatches.size()==1 && captureWork));
        std::vector<std::shared_ptr<SourceTemplate>> pendingTemplates;
        if(hasChanges)
            for(const auto& definition:m_SourceTemplates)
                if(definition->captured || (captureWork && definition->captureBatch==captureWork))pendingTemplates.push_back(definition);
        const bool templateWork=!pendingTemplates.empty();
        const bool needsWork=moved || m_Builder.PendingCount() || changedSources || templateWork;
        if(!needsWork && !m_RetiredBuild)return;
        const auto center=glm::floor(m_ViewCenter/scroll)*scroll;
        std::optional<decltype(m_SourceChanges)> requests;if(changedSources || templateWork)requests=m_SourceChanges;
        const auto instanceGeneration=m_InstanceGeneration;
        const auto submittingThread=std::this_thread::get_id();
        auto task=std::make_shared<std::packaged_task<BuildResult()>>([this,moved,needsWork,changedSources,captureWork,center,instanceGeneration,submittingThread,
            requests=std::move(requests),pendingTemplates=std::move(pendingTemplates),retired=std::move(m_RetiredBuild)]() mutable
        {
            VANS_CPU_SCOPE(GIProfileScope(GICostStage::WorldBuild));
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
                result.sourceSnapshot=true;result.sourcesChanged=changedSources;result.processedCapture=captureWork;
                result.instanceGeneration=instanceGeneration;
                std::set<const SourceTemplate*> neededTemplates;
                for(const auto& item:*requests)if(item.second)neededTemplates.insert(item.second->definition.get());
                std::map<const SourceTemplate*,std::shared_ptr<const std::vector<GIWorldMesh>>> decoded;
                try
                {
                    const auto start=std::chrono::steady_clock::now();
                    std::map<VansGITemplateCapture*,std::vector<GITemplateCaptureResult>> outputs;
                    // An entirely cancelled batch only needs GPU completion and retirement.
                    for(const auto& definition:pendingTemplates)if(definition->captureBatch && neededTemplates.count(definition.get()))
                    {
                        auto* batch=definition->captureBatch.get();
                        auto found=outputs.find(batch);
                        if(found==outputs.end())
                        {
                            found=outputs.emplace(batch,batch->Decode()).first;
                            result.captureDecoded=true;
                        }
                    }
                    for(const auto& definition:pendingTemplates)if(definition->captureBatch)
                    {
                        auto found=outputs.find(definition->captureBatch.get());
                        if(found==outputs.end())continue;
                        auto& output=found->second.at(definition->captureIndex);
                        decoded.emplace(definition.get(),output.meshes);
                        result.captures.push_back({definition,std::move(output)});
                    }
                    result.cpu.captureDecodes=outputs.size();result.cpu.submissionThreadCaptureDecodes=onSubmittingThread?outputs.size():0;
                    result.cpu.captureDecodeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                }
                catch(const std::exception& error){result.error=error.what();return result;}
                std::map<const SourceTemplate*,std::shared_ptr<const GIWorldTemplate>> bakedTemplates;
                for(const auto& definition:pendingTemplates)
                    if(!neededTemplates.count(definition.get()))result.unusedCaptures.push_back(definition);
                size_t templateCells=m_TemplateCells;
                const auto bakeStart=std::chrono::steady_clock::now();
                for(const auto& item:*requests)if(item.second)
                {
                    const auto& definition=item.second->definition;
                    if(definition->model || bakedTemplates.count(definition.get()))continue;
                    if(!definition->captured && !decoded.count(definition.get()))continue;
                    auto baked=std::make_shared<GIWorldTemplate>();
                    const auto input=definition->captured?definition->captured:decoded.at(definition.get());
                    if(!input || !BakeGIWorldTemplate(*input,m_Settings.voxelSize,
                        uint32_t(MaxTemplateCells),*baked,result.error))
                    {if(result.error.empty())result.error="GI template has no captured input";return result;}
                    if(definition->previous)templateCells-=definition->previous->model->CellCount();
                    templateCells+=baked->CellCount();
                    if(templateCells>MaxTemplateCells)
                    {result.error="GI template cache exceeds one million occupied cells";return result;}
                    bakedTemplates.emplace(definition.get(),baked);result.templates.push_back({definition,std::move(baked)});
                }
                result.cpu.templateBakes=result.templates.size();
                result.cpu.submissionThreadTemplateBakes=onSubmittingThread?result.templates.size():0;
                result.cpu.templateBakeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-bakeStart).count();
                result.retiredCaptures.reserve(result.templates.size()+result.unusedCaptures.size());
                if(changedSources)
                {
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
                            const auto& definition=item.second->definition;
                            const auto model=definition->model?definition->model:bakedTemplates.at(definition.get());
                            if(!model->cells.empty())for(const auto& transform:item.second->transforms)
                                chunk->instances.push_back(MakeGIWorldInstance(model,transform));
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
            }
            if(!result.replacement && moved)
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
        try
        {
            if(!m_CaptureBatches.empty() && m_CaptureBatches.front()->Submitted())m_CaptureBatches.front()->Poll();
        }
        catch(const std::exception& error)
        {VANS_LOG_ERROR("[GIWorldCapture] completion rejected; previous field retained: "<<error.what());m_SourceDirty=true;return {};}
        if(m_ReadyBuild || !m_BuildFuture.valid() ||
            m_BuildFuture.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
        m_ReadyBuild=m_BuildFuture.get();auto& completed=*m_ReadyBuild;
        if(!completed.error.empty())
        {
            VANS_LOG_ERROR("[GIWorldTemplate] bake rejected; previous field retained: "<<completed.error);
            const auto failedCapture=completed.processedCapture;
            m_RetiredBuild=std::make_unique<BuildResult>(std::move(completed));m_ReadyBuild.reset();
            if(!RollbackMaterialRefresh(failedCapture))m_SourceDirty=true;
            return {};
        }
        for(auto& capture:completed.captures)
        {
            auto& definition=*capture.definition;auto& output=capture.output;
            const size_t retained=sizeof(SourceTemplate)+definition.parts.capacity()*sizeof(GIVoxelSourcePart)+
                definition.materialInputs.capacity()*sizeof(VansGIVoxelMaterial)+output.retainedBytes;
            Require(retained<=definition.capturedBytes,"GI decoded capture exceeded its reserved byte budget");
            m_PendingTemplateBytes-=definition.capturedBytes-retained;definition.capturedBytes=retained;
            definition.captured=std::move(output.meshes);definition.captureBatch.reset();
            definition.materialBaseColors=std::move(output.materialBaseColors);
            if(!definition.previous && !output.materials.empty())
            {
                std::copy(output.materials.begin(),output.materials.end(),m_Materials.begin()+definition.materialBase);
                m_PendingMaterialBegin=std::min(m_PendingMaterialBegin,size_t(definition.materialBase));
                m_PendingMaterialEnd=std::max(m_PendingMaterialEnd,size_t(definition.materialBase)+output.materials.size());
            }
            // 捕获期间发生改色时，完成值使用当前不可变帧输入；不再次读取作者材质。
            UpdateTemplateMaterials(definition);
        }
        m_CpuWorkStats.captureDecodes+=completed.cpu.captureDecodes;
        m_CpuWorkStats.submissionThreadCaptureDecodes+=completed.cpu.submissionThreadCaptureDecodes;
        m_CpuWorkStats.captureDecodeMilliseconds+=completed.cpu.captureDecodeMilliseconds;
        if(completed.cpu.captureDecodes)
            VANS_LOG("[GIWorldCapture] batches="<<completed.cpu.captureDecodes<<" submissionThreadDecodes="<<completed.cpu.submissionThreadCaptureDecodes
                <<" decodeMs="<<completed.cpu.captureDecodeMilliseconds);
        // 模板输入不可变，已完成烘焙可复用；来源事务过时也不能重新捕获/重烘焙同一模板。
        for(auto& baked:completed.templates)
        {
            auto& definition=*baked.definition;
            if(definition.previous)m_TemplateCells-=definition.previous->model->CellCount();
            m_TemplateCells+=baked.model->CellCount();definition.model=std::move(baked.model);
            m_PendingTemplateBytes-=definition.capturedBytes;definition.capturedBytes=0;
            completed.retiredCaptures.push_back(std::move(definition.captured));
        }
        m_CpuWorkStats.templateBakes+=completed.cpu.templateBakes;
        m_CpuWorkStats.submissionThreadTemplateBakes+=completed.cpu.submissionThreadTemplateBakes;
        m_CpuWorkStats.templateBakeMilliseconds+=completed.cpu.templateBakeMilliseconds;
        if(completed.cpu.templateBakes)
            VANS_LOG("[GIWorldTemplate] baked="<<completed.cpu.templateBakes<<" submissionThreadBakes="<<completed.cpu.submissionThreadTemplateBakes
                <<" bakeMs="<<completed.cpu.templateBakeMilliseconds<<" cachedCells="<<m_TemplateCells);
        const bool currentSourceSnapshot=!completed.sourceSnapshot || completed.instanceGeneration==m_InstanceGeneration;
        if(completed.processedCapture && (completed.captureDecoded || currentSourceSnapshot))
        {
            Require(!m_CaptureBatches.empty() && m_CaptureBatches.front()==completed.processedCapture,"GI capture queue completion is out of order");
            m_CaptureBatches.erase(m_CaptureBatches.begin());
        }
        m_CpuWorkStats.pagePlanJobs+=completed.cpu.pagePlanJobs;m_CpuWorkStats.pagesPlanned+=completed.cpu.pagesPlanned;
        m_CpuWorkStats.retirementJobs+=completed.cpu.retirementJobs;m_CpuWorkStats.submissionThreadPagePlans+=completed.cpu.submissionThreadPagePlans;
        m_CpuWorkStats.submissionThreadRetirements+=completed.cpu.submissionThreadRetirements;
        m_CpuWorkStats.pagePlanMilliseconds+=completed.cpu.pagePlanMilliseconds;m_CpuWorkStats.retirementMilliseconds+=completed.cpu.retirementMilliseconds;
        // 连续刷绘期间旧事务只持有候选索引，不能覆盖较新的来源请求。
        if(completed.sourcesChanged && completed.instanceGeneration!=m_InstanceGeneration)
        {m_RetiredBuild=std::make_unique<BuildResult>(std::move(completed));m_ReadyBuild.reset();return {};}
        if(currentSourceSnapshot)for(auto& definition:completed.unusedCaptures)
        {
            m_PendingTemplateBytes-=definition->capturedBytes;definition->capturedBytes=0;
            completed.retiredCaptures.push_back(std::move(definition->captured));
            if(definition->previous)
            {
                const auto found=std::find(m_SourceTemplates.begin(),m_SourceTemplates.end(),definition);
                if(found!=m_SourceTemplates.end())*found=definition->previous;
                completed.retiredTemplates.push_back(std::move(definition->previous));
            }
            else m_SourceTemplates.erase(std::remove(m_SourceTemplates.begin(),m_SourceTemplates.end(),definition),m_SourceTemplates.end());
        }
        if(completed.replacement)
        {
            // 只交换容器所有权；旧索引、页表和来源随 completed 退役，不在渲染线程析构。
            std::swap(m_Builder,*completed.replacement);m_Pages.swap(completed.pages);m_PageSlots.swap(completed.pageSlots);
            m_Parameters=completed.parameters;m_PendingCoarseBricks=completed.pendingCoarseBricks;
            m_ReadyBricks=completed.readyBricks;
        }
        m_PendingBricks=m_Builder.PendingCount();
        if(completed.sourcesChanged)
        {
            m_LayoutIndex.swap(completed.layoutIndex);m_SourceChunks.swap(completed.chunks);
            m_SourceChanges.swap(completed.retiredChanges);m_PublishedInstanceGeneration=completed.instanceGeneration;
            m_SourceUpdateStats.preparedInstances=completed.preparedInstances;
            for(const auto& definition:m_SourceTemplates)if(definition->previous)
            {
                completed.retiredTemplates.push_back(std::move(definition->previous));
                UpdateTemplateMaterials(*definition);
            }
        }
        return completed.changed.Empty()?nullptr:&completed.changed;
    }
    VansGIWorldUploadWork VansGIWorld::RecordUpdates(VansVKCommandBuffer& command)
    {
        VANS_CPU_SCOPE(GIProfileScope(GICostStage::WorldRecord));
        VANS_GPU_SCOPE_LANE(command.GetVKCommandBuffer(), GIProfileScope(GICostStage::WorldUpload),
            m_Device->IsAsyncComputeEnabled() ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics);
        VansGIWorldUploadWork work;
        const auto finish=[&]()
        {
            work.bufferCapacityBytes=BufferCapacityBytes();work.mappedBricks=uint32_t(m_PageSlots.size());
            work.readyBricks=m_ReadyBricks;work.brickCapacity=m_Settings.maxBricks;work.pendingBuildBricks=m_PendingBricks;
            return work;
        };
        // 高度数据和探针查询在同一 GI 提交链发布，不依赖异步 GBuffer 队列的录制顺序。
        VansGIWorldQueryChanges changes;changes.geometry=m_PendingGeometryBounds;
        if(m_PendingMaterialBegin!=SIZE_MAX)
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            const auto* bytes=reinterpret_cast<const uint8_t*>(m_Materials.data());
            const size_t end=m_PendingMaterialEnd*sizeof(glm::vec4);
            for(size_t offset=m_PendingMaterialBegin*sizeof(glm::vec4);offset<end;offset+=65536)
            {
                const size_t length=std::min<size_t>(65536,end-offset);
                command.UpdateBuffer(m_Buffers[5].GetNativeBuffer(),offset,length,bytes+offset);
                work.materialBytes+=length;++work.updateCommands;
            }
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            m_PendingMaterialBegin=SIZE_MAX;m_PendingMaterialEnd=0;
        }
        RecordTerrainUpdates(command,work);
        m_PendingGeometryBounds.reset();
        if(!m_ReadyBuild){if(!changes.Empty())PublishQueryChanges(std::move(changes));QueueBuild();return finish();}
        auto completed=std::move(*m_ReadyBuild);m_ReadyBuild.reset();
        const bool moved=bool(completed.replacement);
        if(!moved&&completed.bricks.empty())
        {
            if(!changes.Empty())PublishQueryChanges(std::move(changes));
            if(completed.HasRetainedInputs())
                m_RetiredBuild=std::make_unique<BuildResult>(std::move(completed));
            QueueBuild();return finish();
        }
        if(completed.queryChanges.geometry)VansGIWorldQueryChanges::Include(changes.geometry,*completed.queryChanges.geometry);
        changes.residency=completed.queryChanges.residency;
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        if(moved)
        {
            const auto* bytes=reinterpret_cast<const uint8_t*>(m_Pages.data());size_t total=m_Pages.size()*sizeof(GIWorldPageGPU);
            for(size_t offset=0;offset<total;offset+=65536)
            {const auto length=std::min<size_t>(65536,total-offset);command.UpdateBuffer(m_Buffers[3].GetNativeBuffer(),offset,length,bytes+offset);work.pageTableBytes+=length;++work.updateCommands;}
            command.UpdateBuffer(m_Buffers[0].GetNativeBuffer(),0,sizeof(m_Parameters),&m_Parameters);
            work.parameterBytes+=sizeof(m_Parameters);++work.updateCommands;
            if(!completed.bricks.empty())
            {
                barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            }
        }
        // 砖键属于本批次的世界页；先发布相同代次的页表，才能写入新分配的物理地址。
        for(const auto& brick:completed.bricks)
        {
            const float size=m_Settings.voxelSize*8.f*float(1u<<brick.key.level);
            const auto lo=glm::vec3(brick.key.x,brick.key.y,brick.key.z)*size;
            VansGIWorldQueryChanges::Include(changes.residency,{lo,lo+size});
            const auto slot=m_PageSlots.at(brick.key);auto& page=m_Pages[slot];
            command.UpdateBuffer(m_Buffers[4].GetNativeBuffer(),VkDeviceSize(page.data.x)*sizeof(GIWorldVoxel),sizeof(brick.voxels),brick.voxels.data());
            work.voxelBytes+=sizeof(brick.voxels);++work.updateCommands;++work.uploadedBricks;
            if(!page.data.y)++m_ReadyBricks;
            if(!page.data.y && brick.key.level==int(m_Builder.LevelCount())-1)--m_PendingCoarseBricks;
            page.data.y=1;page.data.z=brick.occupancy.x;page.data.w=brick.occupancy.y;
            command.UpdateBuffer(m_Buffers[3].GetNativeBuffer(),VkDeviceSize(slot)*sizeof(GIWorldPageGPU),sizeof(GIWorldPageGPU),&page);
            work.pageTableBytes+=sizeof(GIWorldPageGPU);++work.updateCommands;
        }
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
        PublishQueryChanges(std::move(changes));
        m_RetiredBuild=std::make_unique<BuildResult>(std::move(completed));QueueBuild();
        return finish();
    }
    void VansGIWorld::PublishQueryChanges(VansGIWorldQueryChanges changes)
    {
        m_QueryPublication.changes=std::move(changes);
        ++m_QueryPublication.sequence;
    }
}
