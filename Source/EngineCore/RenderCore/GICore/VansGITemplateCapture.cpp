#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansGITemplateCapture.h"
#include "../GeometryCore/VansMeshGeometryReadback.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Util/VansProfiler.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace VansGraphics
{
    namespace
    {
        void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
        size_t Align(size_t value,size_t alignment){return (value+alignment-1)/alignment*alignment;}
    }
    struct VansGITemplateCapture::State
    {
        struct Part
        {
            uint32_t firstMesh=0,meshCount=0,material=0,width=1,height=1;
            uint32_t textureSourcePart=UINT32_MAX;
            size_t pixelOffset=0;
            glm::vec3 albedo{1};float metallic=0,roughness=1,alphaCutoff=.5f;
            bool alphaTest=false,porous=false,hasTexture=false;
            VkDescriptorImageInfo image{};
        };
        struct Input {uint32_t firstPart=0,partCount=0;size_t estimatedBytes=0;};
        VansVKDevice* device=nullptr;
        VansMeshGeometryReadback geometry;
        std::vector<Part> parts;
        std::vector<Input> inputs;
        std::vector<VansVKResourceLifetime::Lease> textures;
        VansVKBuffer readback;
        VansVKCommandBuffer command;
        VkFence fence=VK_NULL_HANDLE;
        VkDescriptorSetLayout layout=VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;
        std::shared_ptr<VansComputeShader> shader;
        size_t bytes=0;
        bool prepared=false,issued=false,submitted=false,ready=false,decoded=false,gpuUseReleased=false;
        void ReleaseGpuUse()
        {
            if(!device || gpuUseReleased)return;
            auto& logical=device->GetLogicDevice();
            command.DestroyVulkanCommandBuffer(logical);
            if(fence){vkDestroyFence(logical,fence,nullptr);fence=VK_NULL_HANDLE;}
            shader.reset();
            auto* manager=VansVKDescriptorManager::GetInstance();
            if(!sets.empty())manager->DestroyDescriptorSet(sets);
            if(layout)manager->ReleaseDescriptorSetLayout(layout);
            sets.clear();layout=VK_NULL_HANDLE;
            geometry.ReleaseSourcesAfterGpuCompletion();textures.clear();gpuUseReleased=true;
        }
        ~State()
        {
            if(!device)return;
            auto& logical=device->GetLogicDevice();
            // A cancelled/retired world may still own submitted capture work.
            // Normal per-frame completion uses Poll and never takes this wait.
            if(submitted && !ready)vkWaitForFences(logical,1,&fence,VK_TRUE,UINT64_MAX);
            ReleaseGpuUse();readback.DestroyVulkanBuffer(logical);
        }
    };
    VansGITemplateCapture::VansGITemplateCapture():m_State(std::make_unique<State>()){}
    VansGITemplateCapture::~VansGITemplateCapture()=default;
    bool VansGITemplateCapture::Ready() const{return m_State->ready;}
    bool VansGITemplateCapture::Submitted() const{return m_State->submitted || m_State->ready;}
    size_t VansGITemplateCapture::EstimatedBytes(uint32_t input) const{return m_State->inputs.at(input).estimatedBytes;}

    uint32_t VansGITemplateCapture::Prepare(VansVKDevice& device,const std::vector<GITemplateCaptureInput>& inputs,
        uint32_t firstInput,size_t byteBudget,const std::shared_ptr<VansComputeShader>& textureShader)
    {
        auto& state=*m_State;Require(!state.device,"GI capture cannot be prepared twice");state.device=&device;
        Require(bool(textureShader),"GI capture requires its owner's prepared texture shader");
        std::vector<VansMesh*> meshes;
        std::unordered_multimap<VkImageView,uint32_t> textureSources;
        size_t estimated=4096; // Ticket, layouts, command metadata; not total driver allocation.
        Require(estimated<=byteBudget,"GI capture metadata exceeds its byte budget");
        Require(firstInput<inputs.size(),"GI capture requires a nonempty input range");
        for(size_t inputIndex=firstInput;inputIndex<inputs.size();++inputIndex)
        {
            const auto& input=inputs[inputIndex];
            Require(uint64_t(input.materialBase)+input.parts.size()<=65536,"GI voxel material table overflow");
            Require(input.materials.size()==input.parts.size(),"GI capture requires immutable material inputs for every part");
            State::Input captured;captured.firstPart=uint32_t(state.parts.size());captured.partCount=uint32_t(input.parts.size());
            const size_t begin=estimated;
            const auto firstMesh=meshes.size(),firstTexture=state.textures.size();
            bool exceeded=false;
            estimated+=sizeof(State::Input)+256+input.parts.size()*(sizeof(GIVoxelSourcePart)+sizeof(VansGIVoxelMaterial));
            for(size_t partIndex=0;partIndex<input.parts.size();++partIndex)
            {
                const auto& part=input.parts[partIndex];const auto& material=input.materials[partIndex];
                auto* root=part.mesh;
                Require(root&&part.material,"GI geometry capture requires a loaded mesh and PBR material");
                State::Part value;value.firstMesh=uint32_t(meshes.size());
                if(root->m_IsMultiMesh)
                {for(size_t i=0;i<root->m_SubMeshes.size();++i)if(part.submesh<0||i==size_t(part.submesh))meshes.push_back(root->m_SubMeshes[i]);}
                else if(part.submesh<=0)meshes.push_back(root);
                value.meshCount=uint32_t(meshes.size())-value.firstMesh;
                value.material=input.materialBase+uint32_t(state.parts.size())-captured.firstPart;
                value.albedo=material.albedo;value.metallic=material.metallic;
                value.roughness=material.roughness;value.alphaTest=material.alphaTest;
                value.alphaCutoff=material.alphaCutoff;value.porous=part.leaf||value.alphaTest;
                for(size_t i=value.firstMesh;i<meshes.size();++i)
                {
                    auto* mesh=meshes[i];const size_t vertices=mesh->GetMeshVertexCount();
                    const size_t indices=mesh->GetIndexCount()?mesh->GetIndexCount():vertices;
                    estimated+=vertices*(mesh->GetMeshVertexStride()+sizeof(glm::vec3)+sizeof(glm::vec2))+
                        indices*8+sizeof(GIWorldMesh)+256;
                }
                if(auto* texture=material.baseColorTexture)
                {
                    Require(texture->GetWidth()>0 && texture->GetHeight()>0,"GI material has an invalid texture extent");
                    value.hasTexture=true;value.width=uint32_t(std::min(texture->GetWidth(),512));
                    value.height=uint32_t(std::min(texture->GetHeight(),512));
                    auto& image=texture->GetImage();
                    value.image={image.GetSampler(),image.GetImageView(),image.GetImageLayout()};
                    Require(value.image.sampler && value.image.imageView,"GI material texture is not loaded");
                    // 同批共用采样图像只生成一份 GPU 像素快照；独立 alpha 所有者仍分别计入 CPU 预算。
                    const auto matching=textureSources.equal_range(value.image.imageView);
                    for(auto candidate=matching.first;candidate!=matching.second;++candidate)
                    {
                        const auto i=candidate->second;
                        const auto& prior=state.parts[i];
                        if(prior.hasTexture && prior.width==value.width && prior.height==value.height &&
                            prior.image.imageView==value.image.imageView && prior.image.sampler==value.image.sampler && prior.image.imageLayout==value.image.imageLayout)
                        {value.textureSourcePart=i;break;}
                    }
                    if(value.textureSourcePart==UINT32_MAX)
                    {
                        state.textures.push_back(image.RetainNativeResources());
                        textureSources.emplace(value.image.imageView,uint32_t(state.parts.size()));
                        estimated+=size_t(value.width)*value.height*sizeof(glm::vec4)+
                            device.GetDeviceProperties().limits.minStorageBufferOffsetAlignment;
                    }
                    if(value.alphaTest)estimated+=size_t(value.width)*value.height*sizeof(float);
                }
                estimated+=sizeof(State::Part)+sizeof(glm::vec4)+sizeof(glm::vec3)+256;
                if(estimated>byteBudget){exceeded=true;break;}
                state.parts.push_back(value);
            }
            if(exceeded)
            {
                if(state.inputs.empty())throw std::runtime_error("GI single template capture needs "+std::to_string(estimated)+
                    " bytes, exceeding the available budget of "+std::to_string(byteBudget));
                state.parts.resize(captured.firstPart);meshes.resize(firstMesh);state.textures.resize(firstTexture);
                break;
            }
            captured.estimatedBytes=estimated-begin+(state.inputs.empty()?4096:0);state.inputs.push_back(captured);
        }
        std::string error;
        if(!state.geometry.Prepare(meshes,error,{false,true}))throw std::runtime_error(error);
        state.bytes=state.geometry.ByteSize();
        const size_t alignment=std::max<size_t>(16,device.GetDeviceProperties().limits.minStorageBufferOffsetAlignment);
        for(auto& part:state.parts)if(part.hasTexture)
        {
            if(part.textureSourcePart!=UINT32_MAX){part.pixelOffset=state.parts[part.textureSourcePart].pixelOffset;continue;}
            part.pixelOffset=Align(state.bytes,alignment);
            state.bytes=part.pixelOffset+size_t(part.width)*part.height*sizeof(glm::vec4);
        }
        Require(state.bytes<=byteBudget,"GI GPU capture exceeds its byte budget");
        state.shader=textureShader;state.prepared=true;
        return uint32_t(state.inputs.size());
    }
    void VansGITemplateCapture::Submit()
    {
        auto& state=*m_State;
        Require(state.prepared && !state.issued,"GI capture requires a prepared snapshot and a single submission attempt");
        state.issued=true;
        struct Attempt
        {
            State& state;
            ~Attempt()
            {
                if(!state.submitted && !state.ready)
                {state.ReleaseGpuUse();state.readback.DestroyVulkanBuffer(state.device->GetLogicDevice());}
            }
        } attempt{state};
        auto& device=*state.device;auto& logical=device.GetLogicDevice();
        const auto textureCount=std::count_if(state.parts.begin(),state.parts.end(),[](const auto& part)
            {return part.hasTexture && part.textureSourcePart==UINT32_MAX;});
        if(!state.bytes){state.ready=true;state.ReleaseGpuUse();return;}
        {
            VANS_CPU_SCOPE("GIWorld.Capture.Readback");
            Require(state.readback.CreatVulkanBuffer(logical,state.bytes,VK_FORMAT_UNDEFINED,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT) && state.readback.PersistentMap(),
                "GI capture readback allocation failed");
        }
        if(textureCount)
        {
            VANS_CPU_SCOPE("GIWorld.Capture.Descriptors");
            auto* manager=VansVKDescriptorManager::GetInstance();
            Require(manager->CreateDesciptorSetLayout({{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
                {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},state.layout) &&
                manager->AllocateDescriptorSet(std::vector<VkDescriptorSetLayout>(textureCount,state.layout),state.sets),
                "GI capture texture descriptors failed");
            manager->BeginDescriptorUpdate();size_t index=0;
            for(const auto& part:state.parts)if(part.hasTexture && part.textureSourcePart==UINT32_MAX)
            {
                manager->WriteImageDescriptor(state.sets[index],0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,{part.image});
                manager->WriteBufferDescriptor(state.sets[index++],1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    {{state.readback.GetNativeBuffer(),part.pixelOffset,size_t(part.width)*part.height*sizeof(glm::vec4)}});
            }
            manager->UpdateDescriptorSets();
        }
        {
            VANS_CPU_SCOPE("GIWorld.Capture.Record");
            CommandBufferCreateParams params{VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
            Require(state.command.CreateVulkanCommandBuffer(device,device.GetGraphicsQueueFamilyIndex(),params) &&
                state.command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"GI capture command allocation failed");
            VkMemoryBarrier toRead{VK_STRUCTURE_TYPE_MEMORY_BARRIER};toRead.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
            toRead.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT;
            state.command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{toRead});
            state.geometry.Record(state.command,state.readback);
            size_t index=0;
            for(const auto& part:state.parts)if(part.hasTexture && part.textureSourcePart==UINT32_MAX)
            {
                const glm::uvec4 extent(part.width,part.height,0,0);
                state.command.EnsureComputeShader(*state.shader,{state.layout});
                state.command.DispatchCompute(*state.shader,(part.width+7)/8,(part.height+7)/8,1,{state.sets[index++]},&extent,sizeof(extent));
            }
            VkMemoryBarrier toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};toHost.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            toHost.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            state.command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{toHost});
            Require(state.command.EndCommandBufferRecord(),"GI capture command recording failed");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            Require(vkCreateFence(logical,&fenceInfo,nullptr,&state.fence)==VK_SUCCESS,"GI capture fence creation failed");
        }
        VANS_CPU_SCOPE("GIWorld.Capture.Submit");
        Require(VansVKCommandBuffer::SubmitCommands(state.device->GetGraphicsQueue(),state.device->GetLogicDevice(),
            {state.command.GetVKCommandBuffer()},{},{},state.fence,false),"GI capture GPU submission failed");
        state.submitted=true;
    }
    bool VansGITemplateCapture::Poll()
    {
        auto& state=*m_State;if(state.ready)return true;
        Require(state.submitted,"GI capture has not been submitted");
        const auto status=vkGetFenceStatus(state.device->GetLogicDevice(),state.fence);
        if(status==VK_NOT_READY)return false;
        Require(status==VK_SUCCESS,"GI capture GPU completion failed");
        state.readback.InvalidateMappedRange(0,state.bytes);state.ready=true;state.ReleaseGpuUse();return true;
    }
    void VansGITemplateCapture::WaitForInitialization()
    {
        auto& state=*m_State;if(state.ready)return;
        Require(state.submitted && vkWaitForFences(state.device->GetLogicDevice(),1,&state.fence,VK_TRUE,UINT64_MAX)==VK_SUCCESS,
            "GI initial capture completion failed");
        Poll();
    }
    std::vector<GITemplateCaptureResult> VansGITemplateCapture::Decode()
    {
        auto& state=*m_State;Require(state.ready && !state.decoded,"GI capture requires completed, unconsumed readback data");
        std::vector<VansMeshGeometryData> geometry;std::string error;
        if(!state.geometry.Decode(state.readback.GetMappedPtr(),geometry,error))throw std::runtime_error(error);
        std::vector<GITemplateCaptureResult> result(state.inputs.size());
        const auto* bytes=static_cast<const uint8_t*>(state.readback.GetMappedPtr());
        for(size_t input=0;input<state.inputs.size();++input)
        {
            const auto& range=state.inputs[input];auto& output=result[input];
            auto meshes=std::make_shared<std::vector<GIWorldMesh>>();size_t count=0,retainedPixels=0,retainedGeometry=0;
            for(uint32_t p=range.firstPart;p<range.firstPart+range.partCount;++p)
                count+=state.parts[p].meshCount;
            meshes->reserve(count);output.materials.reserve(range.partCount);output.materialBaseColors.reserve(range.partCount);
            for(uint32_t p=range.firstPart;p<range.firstPart+range.partCount;++p)
            {
                const auto& part=state.parts[p];const size_t pixelsCount=size_t(part.width)*part.height;
                const glm::vec4 white(1);const auto* pixels=part.hasTexture?reinterpret_cast<const glm::vec4*>(bytes+part.pixelOffset):&white;
                std::shared_ptr<std::vector<float>> alphaPixels;
                if(part.alphaTest)alphaPixels=std::make_shared<std::vector<float>>(pixelsCount);
                glm::vec3 color(0);float weight=0;
                for(size_t i=0;i<pixelsCount;++i)
                {
                    const auto value=pixels[i];
                    if(alphaPixels)(*alphaPixels)[i]=value.a;
                    float a=part.alphaTest?(value.a>=part.alphaCutoff?1.f:0.f):1.f;color+=glm::vec3(value)*a;weight+=a;
                }
                color=weight>0?color/weight:glm::vec3(0);
                output.materialBaseColors.push_back(color);
                output.materials.push_back(glm::vec4(color*part.albedo*(1-part.metallic),part.roughness));
                const std::shared_ptr<const std::vector<float>> alpha=std::move(alphaPixels);
                if(alpha)retainedPixels+=pixelsCount*sizeof(float);
                for(uint32_t m=part.firstMesh;m<part.firstMesh+part.meshCount;++m)
                {
                    auto& decoded=geometry[m];GIWorldMesh mesh;mesh.material=part.material;mesh.porous=part.porous;
                    mesh.positions=std::move(decoded.positions);mesh.uvs=std::move(decoded.texcoords);mesh.indices=std::move(decoded.indices);
                    if(alpha)
                    {
                        mesh.alphaCutoff=part.alphaCutoff;
                        mesh.alpha=[alpha,w=part.width,h=part.height](glm::vec2 uv)
                        {uv=glm::fract(uv);return (*alpha)[std::min(uint32_t(uv.y*h),h-1)*w+std::min(uint32_t(uv.x*w),w-1)];};
                    }
                    retainedGeometry+=mesh.positions.capacity()*sizeof(glm::vec3)+mesh.uvs.capacity()*sizeof(glm::vec2)+mesh.indices.capacity()*sizeof(uint32_t);
                    meshes->push_back(std::move(mesh));
                }
            }
            output.retainedBytes=sizeof(*meshes)+meshes->capacity()*sizeof(GIWorldMesh)+retainedPixels+retainedGeometry+
                output.materialBaseColors.capacity()*sizeof(glm::vec3);
            output.meshes=std::move(meshes);
        }
        state.readback.DestroyVulkanBuffer(state.device->GetLogicDevice());state.decoded=true;
        return result;
    }
}
