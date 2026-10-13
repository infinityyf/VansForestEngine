#include "VansRenderTexturePreviewSession.h"
#include "VansEditorPreviewRegistry.h"
#include "../../RenderCore/VansRenderSystem.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

namespace Vans::EditorAPI
{
namespace
{
using namespace VansGraphics;
bool SameView(const RenderTexturePreviewViewSpec& a,const RenderTexturePreviewViewSpec& b)
{
    return a.layer==b.layer && a.mipLevel==b.mipLevel && a.channel==b.channel && a.minimum==b.minimum &&
        a.maximum==b.maximum && a.exposure==b.exposure && a.gamma==b.gamma && a.previousFrame==b.previousFrame && a.opaque==b.opaque;
}
bool ValidView(const std::string& key,const RenderTexturePreviewViewSpec& view)
{
    return !key.empty()&&key.size()<=512&&std::isfinite(view.minimum)&&std::isfinite(view.maximum)&&std::isfinite(view.exposure)&&std::isfinite(view.gamma)&&
        view.maximum>view.minimum&&view.gamma>0&&view.gamma<=16&&view.exposure>=-32&&view.exposure<=32&&static_cast<unsigned>(view.channel)<=4;
}
int NumericClass(VkFormat format)
{
    switch(format)
    {
    case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8B8_SINT: case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16B16_SINT: case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_SINT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32_SINT: case VK_FORMAT_R32G32B32A32_SINT: return 1;
    case VK_FORMAT_S8_UINT: case VK_FORMAT_R8_UINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8B8_UINT: case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R16_UINT: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16B16_UINT: case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R32_UINT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32B32_UINT: case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32: case VK_FORMAT_A2R10G10B10_UINT_PACK32: return 2;
    case VK_FORMAT_R64_SINT: case VK_FORMAT_R64_UINT: case VK_FORMAT_R64G64_SINT: case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64B64_SINT: case VK_FORMAT_R64G64B64_UINT: case VK_FORMAT_R64G64B64A64_SINT: case VK_FORMAT_R64G64B64A64_UINT: return -1;
    default:return 0;
    }
}
std::string FormatName(VkFormat format)
{
    switch(format) {
    case VK_FORMAT_R8_UNORM:return "R8_UNORM"; case VK_FORMAT_R8G8B8A8_UNORM:return "RGBA8_UNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT:return "RGBA16_FLOAT";case VK_FORMAT_R32G32B32A32_SFLOAT:return "RGBA32_FLOAT";
    case VK_FORMAT_D32_SFLOAT:return "D32_FLOAT"; case VK_FORMAT_R32_SFLOAT:return "R32_FLOAT";
    case VK_FORMAT_R32_UINT:return "R32_UINT";case VK_FORMAT_R32_SINT:return "R32_SINT";
    default:return "VkFormat "+std::to_string(format);
    }
}
void Barrier(VansVKDevice& backend,VansVKImage& image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst)
{
    VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask=src;barrier.dstAccessMask=dst;barrier.oldLayout=from;barrier.newLayout=to;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image.GetImage();
    barrier.subresourceRange={image.GetImageAspect(),0,image.GetImageCreateInfo().mipLevels,0,image.GetImageCreateInfo().arrayLayers};
    const auto command=backend.GetCommandBuffer().GetVKCommandBuffer();
    VansGraphics::vkCmdPipelineBarrier(command,from==VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
struct PreviewBudget {std::uint64_t bytes=0;static constexpr std::uint64_t Limit=128ull*1024*1024;};
bool DirectView(VansVKImage& source,const RenderTexturePreviewViewSpec& view)
{
    const auto& info=source.GetImageCreateInfo();
    const auto format=info.format;
    const bool color=format==VK_FORMAT_R8G8B8A8_UNORM || format==VK_FORMAT_R8G8B8A8_SRGB ||
        format==VK_FORMAT_B8G8R8A8_UNORM || format==VK_FORMAT_B8G8R8A8_SRGB ||
        format==VK_FORMAT_R16G16B16A16_SFLOAT || format==VK_FORMAT_R32G32B32A32_SFLOAT;
    return color && info.samples==VK_SAMPLE_COUNT_1_BIT && (info.usage&VK_IMAGE_USAGE_SAMPLED_BIT) &&
        (source.GetImageLayout()==VK_IMAGE_LAYOUT_GENERAL || source.GetImageLayout()==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) &&
        view.channel==RenderTexturePreviewChannel::RGBA && view.minimum==0 && view.maximum==1 && view.exposure==0 && view.gamma==1;
}
struct PreviewGpuResources
{
    struct Slot {VansVKImage proxy,copy;VkImageView sourceView=VK_NULL_HANDLE;VkImage source=VK_NULL_HANDLE;EditorTextureHandle texture=nullptr;VansEditorPreviewRegistry::RegistrationId registration=0;bool initialized=false;};
    VkDevice device=VK_NULL_HANDLE;
    std::shared_ptr<PreviewBudget> budget;
    std::shared_ptr<VansEditorPreviewRegistry> registry;
    std::uint64_t bytes=0,generation=0;
    std::vector<Slot> slots;
    VkDescriptorSetLayout layout=VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets;
    VkSampler sampler=VK_NULL_HANDLE;
    VansComputeShader shader;
    std::uint32_t width=0,height=0,sourceWidth=0,sourceHeight=0;
    VkFormat format=VK_FORMAT_UNDEFINED;VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    bool copySource=false,direct=false;
    ~PreviewGpuResources()
    {
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        descriptors->DestroyDescriptorSet(sets);descriptors->ReleaseDescriptorSetLayout(layout);
        for(auto& slot:slots) {
            if (registry) registry->ReleaseTextureNow(slot.registration);
            VansVKImage::DestroyImageView(device,slot.sourceView);
            slot.proxy.DestroyVulkanImage(device);slot.copy.DestroyVulkanImage(device);
        }
        if(sampler) VansGraphics::vkDestroySampler(device,sampler,nullptr);
        if(budget)budget->bytes-=bytes;
    }
};
struct PreviewCache
{
    VansVKDevice& device;VansRenderSystem& renderSystem;
    std::shared_ptr<VansEditorPreviewRegistry> registry;
    std::filesystem::path shaderRoot,artifactRoot;
    std::shared_ptr<PreviewBudget> budget;
    std::shared_ptr<PreviewGpuResources> gpu;
    std::atomic<bool> retired{false};
    std::uint64_t lastSerial=UINT64_MAX,lastGeneration=0;
    mutable std::mutex mutex;
    std::string status="pending";
    std::uint64_t frameSerial=0;
    PreviewCache(VansVKDevice& device,VansRenderSystem& renderSystem,std::shared_ptr<VansEditorPreviewRegistry> registry,std::filesystem::path shaderRoot,
        std::filesystem::path artifactRoot,std::shared_ptr<PreviewBudget> budget)
        :device(device),renderSystem(renderSystem),registry(std::move(registry)),shaderRoot(std::move(shaderRoot)),artifactRoot(std::move(artifactRoot)),budget(std::move(budget)) {}
    void Status(std::string value,std::uint64_t serial=0) {std::lock_guard<std::mutex> lock(mutex);status=std::move(value);frameSerial=serial;}
    bool Build(VansVKImage& source,const RenderTexturePreviewViewSpec& view,std::uint64_t generation,std::string& error)
    {
        const auto& info=source.GetImageCreateInfo();
        if(info.imageType!=VK_IMAGE_TYPE_2D || NumericClass(info.format)<0 || view.layer>=info.arrayLayers || view.mipLevel>=info.mipLevels)
        {error="Unsupported image type, numeric format, layer or mip";return false;}
        const auto sw=std::max(1u,info.extent.width>>view.mipLevel),sh=std::max(1u,info.extent.height>>view.mipLevel);
        const auto scale=std::min(1.0,512.0/std::max(sw,sh));
        const auto width=std::max(1u,static_cast<std::uint32_t>(sw*scale)),height=std::max(1u,static_cast<std::uint32_t>(sh*scale));
        const bool copy=!(info.usage&VK_IMAGE_USAGE_SAMPLED_BIT);
        const bool direct=DirectView(source,view);
        if(copy && !(info.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {error="Source usage disallows sampling and copying";return false;}
        const auto slots=device.GetEffectiveFramesInFlight();
        const std::uint64_t bytes=direct ? 0 : (std::uint64_t(width)*height*4+(copy ? std::uint64_t(sw)*sh*16*info.samples : 0))*slots;
        if(bytes>32ull*1024*1024 || bytes>PreviewBudget::Limit-budget->bytes) {error="Preview image budget exceeded (32 MiB per view, 128 MiB per session)";return false;}
        VkFormatProperties format{};VansGraphics::vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(),info.format,&format);
        if(!(format.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {error="Format cannot be sampled for visualization";return false;}
        if(copy && !(format.optimalTilingFeatures&VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {error="Format cannot be copied to a sampled visualization image";return false;}
        VkFormatProperties proxyFormat{};VansGraphics::vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(),VK_FORMAT_R8G8B8A8_UNORM,&proxyFormat);
        if(!direct && (proxyFormat.optimalTilingFeatures&(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))!=
            (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        {error="Device does not support the Editor visualization proxy format";return false;}
        auto candidate=std::make_shared<PreviewGpuResources>();candidate->device=device.GetLogicDevice();candidate->budget=budget;candidate->registry=registry;
        candidate->bytes=bytes;candidate->generation=generation;budget->bytes+=bytes;candidate->width=width;candidate->height=height;candidate->sourceWidth=sw;candidate->sourceHeight=sh;
        candidate->format=info.format;candidate->samples=info.samples;candidate->copySource=copy;candidate->direct=direct;candidate->slots.resize(slots);
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        if(!direct && (!descriptors->CreateDesciptorSetLayout({{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},candidate->layout) ||
            !descriptors->AllocateDescriptorSet(std::vector<VkDescriptorSetLayout>(slots,candidate->layout),candidate->sets,VansDescriptorLifetimeRole::PassPersistent)))
        {error="Preview descriptors allocation failed";return false;}
        VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
        if(direct && (format.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
        sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if(VansGraphics::vkCreateSampler(candidate->device,&sampler,nullptr,&candidate->sampler)!=VK_SUCCESS) {error="Preview sampler allocation failed";return false;}
        const char* classes[]={"Float","Int","UInt"};const auto stage=std::string(classes[NumericClass(info.format)])+(info.samples==VK_SAMPLE_COUNT_1_BIT ? "" : "MS")+".comp";
        if(!direct) {
        candidate->shader.SetArtifactRoot(artifactRoot);
        if(!candidate->shader.InitShader(candidate->device,shaderRoot.string(),{{VK_SHADER_STAGE_COMPUTE_BIT,stage}})) {error="Editor visualization Shader load failed";return false;}
        auto program=candidate->shader.GetPipelineProgramDesc(); program.kind=VansPipelineProgramKind::Compute;
        candidate->shader.SetPipelineProgramDesc(program); candidate->shader.SetPushConstant(48);
        }
        for(auto& slot:candidate->slots) {
            if(direct) continue;
            if(!slot.proxy.CreateVulkanImage(candidate->device,{width,height,1},VK_FORMAT_R8G8B8A8_UNORM,1,1,VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_SAMPLE_COUNT_1_BIT,false,false,true,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
            {error="Preview proxy allocation failed";return false;}
            if(copy && !slot.copy.CreateVulkanImage(candidate->device,{sw,sh,1},info.format,1,1,VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,info.samples,false,false,false)) {error="Preview source copy allocation failed";return false;}
            slot.registration=registry->RegisterTexture("RenderTexturePreview",slot.proxy.GetSampler(),slot.proxy.GetImageView(),VK_IMAGE_LAYOUT_GENERAL,slot.proxy.GetImage(),slot.texture);
            if(!slot.texture) {error="Editor texture registration failed";return false;}
        }
        if(gpu) device.RetireAfterCurrentFrame([old=std::move(gpu)]{});
        gpu=std::move(candidate);return true;
    }
    void Retire()
    {
        retired.store(true,std::memory_order_release);
        // Main 把 release 排在已捕获的 Overlay 之后；资源在其 GPU fence 完成后释放。
    }
};
class FrameBinding final:public IVansRenderFrameTextureBinding
{
public:
    std::shared_ptr<PreviewCache> cache;
    std::string key;std::uint64_t generation=0;
    RenderTexturePreviewViewSpec view;
    void* Resolve(VansGraphicsDevice& backend) override
    {
        auto* device=dynamic_cast<VansVKDevice*>(&backend);
        if(!device || cache->retired.load(std::memory_order_acquire)) return nullptr;
        auto* source=device->ResolveRuntimeRenderTexture(key,generation,view.previousFrame);
        if(!source) {cache->Status("missing, uninitialized, or generation changed");return nullptr;}
        const auto& sourceInfo=source->GetImageCreateInfo();
        if (view.layer>=sourceInfo.arrayLayers || view.mipLevel>=sourceInfo.mipLevels || view.mipLevel>=32)
        { cache->Status("Layer or mip is out of range"); return nullptr; }
        const auto serial=device->GetFrameRecordingSerial();
        if(cache->lastSerial==serial && cache->lastGeneration==generation && cache->gpu &&
            cache->gpu->slots.at(device->GetFrameResourceSlot()).texture)
            return cache->gpu->slots.at(device->GetFrameResourceSlot()).texture;
        const auto& create=source->GetImageCreateInfo();
        const auto sw=std::max(1u,create.extent.width>>view.mipLevel),sh=std::max(1u,create.extent.height>>view.mipLevel);
        auto& gpu=cache->gpu;
        // 源 owner 的新代际必须整组替换视图/descriptor；不能留下另一个帧槽的旧绑定。
        if(gpu && gpu->generation!=generation)
        {
            device->RetireAfterCurrentFrame([old=std::move(gpu)]{});
        }
        if(!gpu || gpu->sourceWidth!=sw || gpu->sourceHeight!=sh || gpu->format!=create.format || gpu->samples!=create.samples ||
            gpu->slots.size()!=device->GetEffectiveFramesInFlight() || gpu->copySource!=!(create.usage&VK_IMAGE_USAGE_SAMPLED_BIT) || gpu->direct!=DirectView(*source,view))
        {std::string error;if(!cache->Build(*source,view,generation,error)){cache->Status(error);return nullptr;}}
        auto& slot=gpu->slots.at(device->GetFrameResourceSlot());
        auto& logic=gpu->device;
        VansVKImage* sampled = gpu->copySource ? &slot.copy : source;
        if(slot.source!=sampled->GetImage() || (gpu->direct && !slot.texture))
        {
            VkComponentMapping components{};
            if(gpu->direct && view.opaque) components.a=VK_COMPONENT_SWIZZLE_ONE;
            auto candidate=sampled->CreateLayerMipView(logic,gpu->copySource?0:view.layer,gpu->copySource?0:view.mipLevel,components,
                sampled->GetImageAspect()&VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : sampled->GetImageAspect());
            if(!candidate) {cache->Status("Source view creation failed");return nullptr;}
            if(slot.sourceView) {auto old=slot.sourceView;auto oldRegistration=slot.registration;auto registry=cache->registry;
                device->RetireAfterCurrentFrame([logic,old,oldRegistration,registry,direct=gpu->direct]()mutable{
                    if(direct) registry->ReleaseTextureNow(oldRegistration);VansVKImage::DestroyImageView(logic,old);});}
            slot.sourceView=candidate;slot.source=sampled->GetImage();
            if(gpu->direct) {
                slot.registration=cache->registry->RegisterTexture("RenderTexturePreview",gpu->sampler,slot.sourceView,source->GetImageLayout(),source->GetImage(),slot.texture);
                if(!slot.texture) {cache->Status("Editor texture registration failed");return nullptr;}
            }
        }
        if(gpu->direct)
        {
            const auto layout=source->GetImageLayout();
            Barrier(*device,*source,layout,layout,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
            device->RecordOverlaySampledImage(source->GetImage(),layout);
            cache->lastSerial=serial;cache->lastGeneration=generation;cache->Status("valid (direct)",serial);return slot.texture;
        }
        auto* command=&device->GetCommandBuffer();
        command->EnsureComputeShader(gpu->shader,{gpu->layout});
        if(!gpu->shader.GetComputePipeline()) {cache->Status("Visualization pipeline creation failed");return nullptr;}
        const auto original=source->GetImageLayout();
        if(gpu->copySource)
        {
            Barrier(*device,*source,original,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            Barrier(*device,slot.copy,slot.initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                slot.initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageCopy copy{};copy.srcSubresource={source->GetImageAspect(),view.mipLevel,view.layer,1};copy.dstSubresource={source->GetImageAspect(),0,0,1};
            copy.extent={sw,sh,1};
            std::vector<VkImageCopy> regions;
            for (auto aspect:{VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_ASPECT_STENCIL_BIT})
                if (source->GetImageAspect()&aspect) { copy.srcSubresource.aspectMask=copy.dstSubresource.aspectMask=aspect;regions.push_back(copy); }
            VansGraphics::vkCmdCopyImage(device->GetCommandBuffer().GetVKCommandBuffer(),source->GetImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                slot.copy.GetImage(),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,static_cast<std::uint32_t>(regions.size()),regions.data());
            Barrier(*device,*source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,original,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
            Barrier(*device,slot.copy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
            device->RecordOverlaySampledImage(source->GetImage(),original,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        }
        else {
            Barrier(*device,*source,original,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
            device->RecordOverlaySampledImage(source->GetImage(),original,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        Barrier(*device,slot.proxy,slot.initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
            slot.initialized?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
        const std::array<VkDescriptorImageInfo,2> images{{{gpu->sampler,slot.sourceView,VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE,slot.proxy.GetImageView(),VK_IMAGE_LAYOUT_GENERAL}}};
        std::array<VkWriteDescriptorSet,2> writes{};
        for(std::uint32_t i=0;i<2;++i) {writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=gpu->sets[device->GetFrameResourceSlot()];
            writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&images[i];}
        VansGraphics::vkUpdateDescriptorSets(logic,2,writes.data(),0,nullptr);
        struct Parameters {std::array<float,4> mapping;std::array<std::uint32_t,4> view,extent;} parameters{
            {view.minimum,view.maximum,view.exposure,view.gamma},{static_cast<std::uint32_t>(view.channel==RenderTexturePreviewChannel::RGBA && (source->GetImageAspect()&VK_IMAGE_ASPECT_DEPTH_BIT) ? RenderTexturePreviewChannel::R : view.channel),view.opaque?1u:0u,sw,sh},
            {gpu->width,gpu->height,static_cast<std::uint32_t>(create.samples),0}};
        static_assert(sizeof(Parameters)==48,"Preview push constant ABI");
        command->DispatchCompute(gpu->shader,(gpu->width+7)/8,(gpu->height+7)/8,1,{gpu->sets[device->GetFrameResourceSlot()]},&parameters,sizeof(parameters));
        Barrier(*device,slot.proxy,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        if(!gpu->copySource) Barrier(*device,*source,VK_IMAGE_LAYOUT_GENERAL,original,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
        slot.proxy.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);slot.copy.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);slot.initialized=true;
        device->RecordOverlaySampledImage(slot.proxy.GetImage(),VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,true);
        cache->lastSerial=serial;cache->lastGeneration=generation;cache->Status("valid",serial);return slot.texture;
    }
};
}
struct VansRenderTexturePreviewSession::State
{
    struct Subscription {std::uint64_t id=0;std::string key;RenderTexturePreviewViewSpec view;std::shared_ptr<PreviewCache> cache;mutable std::shared_ptr<FrameBinding> binding;};
    VansVKDevice& device;VansRenderSystem& renderSystem;
    std::shared_ptr<VansEditorPreviewRegistry> registry;
    std::filesystem::path shaderRoot,artifactRoot;
    std::shared_ptr<PreviewBudget> budget=std::make_shared<PreviewBudget>();
    mutable std::map<std::uint64_t,Subscription> subscriptions;
    RenderTexturePreview MakePreview(Subscription& subscription,const std::vector<VansRuntimeRenderTextureInfo>& sources) const
    {
        RenderTexturePreview preview;preview.registration=subscription.id;preview.id=static_cast<RenderTextureId>(subscription.id);preview.resourceKey=subscription.key;
        const auto source=std::find_if(sources.begin(),sources.end(),[&](const auto& s){return s.key==subscription.key;});
        if(source==sources.end()){preview.name=subscription.key;preview.status="missing";return preview;}
        preview.name=source->name;preview.width=std::max(1u,source->width>>std::min(subscription.view.mipLevel,31u));
        preview.height=std::max(1u,source->height>>std::min(subscription.view.mipLevel,31u));preview.generation=source->generation;preview.status=source->status;
        if(subscription.view.layer>=source->layers || subscription.view.mipLevel>=source->mips)preview.status="Layer or mip is out of range";
        else if(subscription.view.previousFrame&&!source->previousAvailable)preview.status="previousFrame unavailable";
        else if(source->initialized&&source->width&&subscription.view.layer<source->layers&&subscription.view.mipLevel<source->mips)
        {
            if(!subscription.binding||subscription.binding->generation!=source->generation)
            {
                if(subscription.binding&&m_GUIBackEnd)m_GUIBackEnd->UnregisterFrameTextureBinding(subscription.binding.get());
                auto binding=std::make_shared<FrameBinding>();binding->key=subscription.key;binding->view=subscription.view;binding->generation=source->generation;binding->cache=subscription.cache;
                if(m_GUIBackEnd&&m_GUIBackEnd->RegisterFrameTextureBinding(binding.get(),binding))subscription.binding=std::move(binding);
            }
            preview.texture=subscription.binding.get();
            std::lock_guard<std::mutex> lock(subscription.cache->mutex);preview.status=subscription.cache->status;preview.frameSerial=subscription.cache->frameSerial;
        }
        return preview;
    }
    void ReleaseCache(std::shared_ptr<PreviewCache> cache)
    {
        cache->Retire();
        registry->RetireResource([cache] {cache->gpu.reset();}, &device);
    }
};
VansRenderTexturePreviewSession::VansRenderTexturePreviewSession(VansVKDevice& device,VansRenderSystem& renderSystem, std::shared_ptr<VansEditorPreviewRegistry> registry,
    std::filesystem::path shaderRoot,std::filesystem::path artifactRoot)
    :m_State(std::make_unique<State>(State{device,renderSystem,std::move(registry),std::move(shaderRoot),std::move(artifactRoot)})) {}
VansRenderTexturePreviewSession::~VansRenderTexturePreviewSession(){Clear();}
std::vector<RenderTextureSourceInfo> VansRenderTexturePreviewSession::Sources(RenderTextureFilter filter) const
{
    std::vector<RenderTextureSourceInfo> result;
    for(const auto& source:m_State->device.QueryRuntimeRenderTextures())
        if((filter.category.empty()||filter.category==source.category) && (filter.name.empty()||source.name.find(filter.name)!=std::string::npos||source.key.find(filter.name)!=std::string::npos||source.owner.find(filter.name)!=std::string::npos||source.category.find(filter.name)!=std::string::npos))
            result.push_back({source.key,source.name,source.category,source.owner,FormatName(source.format),source.status,source.generation,
                source.width,source.height,source.layers,source.mips,source.samples,source.frameSlots,
                source.initialized&&(source.usage&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT)),source.previousAvailable});
    return result;
}
RenderTexturePreviewRegistrationResult VansRenderTexturePreviewSession::Register(const std::string& key,const RenderTexturePreviewViewSpec& view)
{
    if(!ValidView(key,view))
        return {false,0,"Invalid resource key, channel, range, exposure or gamma"};
    if(m_State->subscriptions.size()>=32)return {false,0,"Preview subscription limit is 32"};
    std::shared_ptr<PreviewCache> cache;
    for(const auto& [id,subscription]:m_State->subscriptions)if(subscription.key==key&&SameView(subscription.view,view)){cache=subscription.cache;break;}
    if(!cache)cache=std::make_shared<PreviewCache>(m_State->device,m_State->renderSystem,m_State->registry,m_State->shaderRoot,m_State->artifactRoot,m_State->budget);
    const auto id=m_State->registry->ReserveRegistrationId();
    m_State->subscriptions.emplace(id,State::Subscription{id,key,view,std::move(cache),{}});return {true,id,{}};
}
void VansRenderTexturePreviewSession::Unregister(std::uint64_t id)
{
    const auto found=m_State->subscriptions.find(id);if(found==m_State->subscriptions.end())return;
    if(found->second.binding&&m_GUIBackEnd)m_GUIBackEnd->UnregisterFrameTextureBinding(found->second.binding.get());
    auto cache=found->second.cache;m_State->subscriptions.erase(found);
    if(std::none_of(m_State->subscriptions.begin(),m_State->subscriptions.end(),[&](const auto& item){return item.second.cache==cache;}))m_State->ReleaseCache(std::move(cache));
}
std::vector<RenderTexturePreview> VansRenderTexturePreviewSession::Previews() const
{
    const auto sources=m_State->device.QueryRuntimeRenderTextures();std::vector<RenderTexturePreview> result;
    for(auto& [id,subscription]:m_State->subscriptions)
        result.push_back(m_State->MakePreview(subscription,sources));
    return result;
}
void VansRenderTexturePreviewSession::Clear(){while(!m_State->subscriptions.empty())Unregister(m_State->subscriptions.begin()->first);}
}
