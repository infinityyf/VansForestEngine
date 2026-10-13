#define VK_NO_PROTOTYPES
#include "RenderTexturePreviewGpuContractTests.h"
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VansRenderSystem.h"
#include "../EngineCore/RenderCore/VansRenderFrameSource.h"
#include "../EngineCore/EngineAPILayer/Private/VansRenderTexturePreviewSession.h"
#include "../EngineCore/EngineAPILayer/Private/VansEditorPreviewRegistry.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/RuntimeCore/VansFramePhase.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace
{
using namespace VansGraphics;
using namespace Vans::EditorAPI;
std::uint32_t validationErrors=0;
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* message,void*)
{
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validationErrors;std::cerr<<"[RTPreviewGPU validation] "<<message->pMessage<<'\n';}
    return VK_FALSE;
}
struct Window final:INativeWindowProvider
{
    GLFWwindow* handle=nullptr;
    void* GetNativeWindowHandle()const override{return handle;}
    ~Window(){if(handle)glfwDestroyWindow(handle);glfwTerminate();}
};
struct EmptyFrameSource final:IVansRenderFrameSource
{
    std::optional<VansRenderFrameSourceOutput> PrepareMainThreadRenderFrame(const VansRenderFramePreparationContext&)override{return {};}
};
// Only the GUI port is substituted. Descriptors, SPIR-V conversion and ImGui rendering use the native backend.
struct GuiPort final:VansGUIBackEnd
{
    std::unordered_map<void*,std::shared_ptr<IVansRenderFrameTextureBinding>> bindings;
    std::unordered_map<void*,void*> resources;
    void InitBackEnd(VansGraphicsDevice&,GLFWwindow*)override{}
    std::unique_ptr<IVansRenderThreadTransaction> CreateRenderThreadInitialization()override{return {};}
    std::unique_ptr<IVansRenderThreadTransaction> CreateRenderThreadShutdown()override{return {};}
    void BeginFrame()override{}
    std::unique_ptr<IVansRenderFrameOverlay> CaptureDrawData(ImDrawData*)override{return {};}
    void RegisterTextureResource(void* texture,void* image,int)override{resources[texture]=image;}
    void UnregisterTextureResource(void* texture)override{resources.erase(texture);}
    bool RegisterFrameTextureBinding(void* token,std::shared_ptr<IVansRenderFrameTextureBinding> value)override{bindings[token]=std::move(value);return true;}
    void UnregisterFrameTextureBinding(void* token)override{bindings.erase(token);}
    bool RegisterFrameTextureVariant(void*,void*)override{return true;}
    void UnregisterFrameTextureVariant(void*)override{}
    void ShutdownBackEnd()override{}
};
void Barrier(VansVKDevice& device,VansVKImage& image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=src;barrier.dstAccessMask=dst;barrier.oldLayout=from;barrier.newLayout=to;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image.GetImage();
    barrier.subresourceRange={image.GetImageAspect(),0,image.GetImageCreateInfo().mipLevels,0,image.GetImageCreateInfo().arrayLayers};
    vkCmdPipelineBarrier(device.GetCommandBuffer().GetVKCommandBuffer(),from==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
struct Fixture
{
    VansVKDevice& device;
    VansVKImage display;
    VansVKBuffer readback;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    bool imgui=false;
    explicit Fixture(VansVKDevice& device):device(device)
    {
        auto logic=device.GetLogicDevice();
        Check(display.CreateVulkanImage(logic,{32,32,1},VK_FORMAT_R8G8B8A8_UNORM,1,1,VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_SAMPLE_COUNT_1_BIT),"display allocation");
        Check(readback.CreatVulkanBuffer(logic,32*32*4,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.PersistentMap(),"readback allocation");
        VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount=1;info.pAttachments=&attachment;info.subpassCount=1;info.pSubpasses=&subpass;
        Check(vkCreateRenderPass(logic,&info,nullptr,&pass)==VK_SUCCESS,"display render pass");
        const auto view=display.GetImageView();VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass=pass;fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=32;fb.layers=1;
        Check(vkCreateFramebuffer(logic,&fb,nullptr,&framebuffer)==VK_SUCCESS,"display framebuffer");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,128};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;dp.maxSets=128;dp.poolSizeCount=1;dp.pPoolSizes=&size;
        Check(vkCreateDescriptorPool(logic,&dp,nullptr,&pool)==VK_SUCCESS,"Editor descriptor pool");
        ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
        Check(ImGui_ImplVulkan_LoadFunctions([](const char* name,void* data){auto& device=*static_cast<VansVKDevice*>(data);
            auto function=vkGetDeviceProcAddr(device.GetLogicDevice(),name);return function?function:vkGetInstanceProcAddr(device.GetInstance(),name);},&device),"ImGui functions");
        ImGui_ImplVulkan_InitInfo init{};init.Instance=device.GetInstance();init.PhysicalDevice=device.GetPhysicalDevice();init.Device=logic;
        init.QueueFamily=device.GetGraphicsQueueFamilyIndex();init.Queue=device.GetGraphicsQueue();init.DescriptorPool=pool;init.RenderPass=pass;
        init.MinImageCount=init.ImageCount=2;init.MSAASamples=VK_SAMPLE_COUNT_1_BIT;
        Check(ImGui_ImplVulkan_Init(&init),"ImGui native initialization");imgui=true;
        Check(ImGui_ImplVulkan_CreateFontsTexture(),"ImGui font resource initialization");
    }
    ~Fixture()
    {
        device.DrainDeferredDeletesAfterDeviceIdle();auto logic=device.GetLogicDevice();
        if(imgui)ImGui_ImplVulkan_Shutdown();if(ImGui::GetCurrentContext())ImGui::DestroyContext();
        if(pool)vkDestroyDescriptorPool(logic,pool,nullptr);if(framebuffer)vkDestroyFramebuffer(logic,framebuffer,nullptr);
        if(pass)vkDestroyRenderPass(logic,pass,nullptr);readback.DestroyVulkanBuffer(logic);display.DestroyVulkanImage(logic);
    }
    void Begin()
    {
        VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
        Check(device.GetCommandBuffer().ResetCommandBuffer(false)&&device.GetCommandBuffer().BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Editor command begin");
    }
    void Submit()
    {
        Check(device.GetCommandBuffer().EndCommandBufferRecord(),"Editor command end");
        auto logic=device.GetLogicDevice();auto queue=device.GetGraphicsQueue();const auto cb=device.GetCommandBuffer().GetVKCommandBuffer();
        Check(VansVKCommandBuffer::SubmitCommands(queue,logic,{cb},{},{},VK_NULL_HANDLE,false),"Editor command submit");
        Check(vkQueueWaitIdle(queue)==VK_SUCCESS,"Editor command completion");
    }
    std::array<int,4> DrawAndRead(void* texture)
    {
        Barrier(device,display,display.GetImageLayout(),VK_IMAGE_LAYOUT_GENERAL,display.GetImageLayout()==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkClearValue clear{};VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;
        begin.renderArea.extent={32,32};begin.clearValueCount=1;begin.pClearValues=&clear;
        auto cb=device.GetCommandBuffer().GetVKCommandBuffer();vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
        ImGui::GetIO().DisplaySize={32,32};ImGui::GetIO().DeltaTime=1.0f/60.0f;
        ImGui_ImplVulkan_NewFrame();ImGui::NewFrame();ImGui::GetBackgroundDrawList()->AddImage(texture,{0,0},{32,32});ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),cb);vkCmdEndRenderPass(cb);
        Barrier(device,display,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={32,32,1};
        vkCmdCopyImageToBuffer(cb,display.GetImage(),VK_IMAGE_LAYOUT_GENERAL,readback.GetNativeBuffer(),1,&copy);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        device.GetCommandBuffer().PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{host});display.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        Submit();readback.InvalidateMappedRange(0,32*32*4);const auto* bytes=static_cast<const std::uint8_t*>(readback.GetMappedPtr());const auto offset=(16*32+16)*4;
        return {bytes[offset],bytes[offset+1],bytes[offset+2],bytes[offset+3]};
    }
};
void Verify(Fixture& fixture,GuiPort& gui,VansRenderTexturePreviewSession& session,const std::shared_ptr<VansEditorPreviewRegistry>& registry,
    VkFormat format,VkSampleCountFlagBits samples,bool sampled,RenderTexturePreviewViewSpec view,VkClearColorValue clear,int expected,std::uint64_t ownerGeneration)
{
    auto& device=fixture.device;auto logic=device.GetLogicDevice();
    const bool depth=format==VK_FORMAT_D32_SFLOAT_S8_UINT;
    VansVKImage source, replacementSource;
    struct Cleanup{VansVKDevice& device;VansVKImage& source;VansVKImage& replacement;~Cleanup(){device.UnregisterRuntimeRenderTextureSource("external:contract");device.DrainDeferredDeletesAfterDeviceIdle();source.DestroyVulkanImage(device.GetLogicDevice());replacement.DestroyVulkanImage(device.GetLogicDevice());}} cleanup{device,source,replacementSource};
    const auto layers=samples==VK_SAMPLE_COUNT_1_BIT&&!depth?2u:1u;
    const auto mips=layers==2?2u:1u;
    Check(source.CreateVulkanImage(logic,{32,32,1},format,mips,layers,VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|(sampled?VK_IMAGE_USAGE_SAMPLED_BIT:0),samples),"source allocation");
    fixture.Begin();Barrier(device,source,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageSubresourceRange range{source.GetImageAspect(),0,mips,0,layers};const auto cb=device.GetCommandBuffer().GetVKCommandBuffer();
    if(depth){VkClearDepthStencilValue value{.25f,7};vkCmdClearDepthStencilImage(cb,source.GetImage(),VK_IMAGE_LAYOUT_GENERAL,&value,1,&range);}
    else for(std::uint32_t layer=0;layer<layers;++layer)for(std::uint32_t mip=0;mip<mips;++mip)
    {
        const VkClearColorValue empty{};range={VK_IMAGE_ASPECT_COLOR_BIT,mip,1,layer,1};
        const auto& selected=layer==view.layer&&mip==view.mipLevel?clear:empty;
        vkCmdClearColorImage(cb,source.GetImage(),VK_IMAGE_LAYOUT_GENERAL,&selected,1,&range);
    }
    Barrier(device,source,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT);source.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);fixture.Submit();
    VansRuntimeRenderTextureCatalog::Source publish;publish.key="external:contract";publish.name="Native preview fixture";publish.category="Contract";publish.owner="Fixture";
    publish.ownerGeneration=ownerGeneration;publish.images={&source};std::string error;
    Check(device.RegisterRuntimeRenderTextureSource(publish,error),error);Check(!device.RegisterRuntimeRenderTextureSource(publish,error),"duplicate registration accepted");
    device.RefreshRuntimeRenderTextureCatalog();
    VANS_INIT_MAIN_THREAD();const auto sources=session.Sources({"Contract",{}});Check(sources.size()==1&&sources[0].previewable,"source not discovered");
    const auto registration=session.Register(publish.key,view);Check(registration.accepted,registration.error);
    const auto shared=session.Register(publish.key,view);Check(shared.accepted,shared.error);
    const auto previews=session.Previews();Check(previews.size()==2&&previews[0].texture&&previews[1].texture,"preview binding missing");
    auto captured=gui.bindings.at(previews[0].texture);
    fixture.Begin();auto* texture=captured->Resolve(device);Check(texture!=nullptr,"preview resolve failed: "+session.Previews()[0].status);
    Check(gui.bindings.at(previews[1].texture)->Resolve(device)==texture,"identical subscriptions did not share a GPU view");
    const auto pixels=fixture.DrawAndRead(texture);
    for(int i=0;i<3;++i)Check(std::abs(pixels[i]-expected)<=2,"conversion/ImGui pixel differs: channel="+std::to_string(i)+" actual="+std::to_string(pixels[i])+" expected="+std::to_string(expected));
    Check(pixels[3]==255,"opaque display alpha differs");
    Check(source.GetImageLayout()==VK_IMAGE_LAYOUT_GENERAL,"preview changed source layout");
    // 保持订阅，换成内容不同的真实 Image；代际标签和显示像素必须一起切换。
    Check(replacementSource.CreateVulkanImage(logic,{32,32,1},format,mips,layers,VK_IMAGE_TYPE_2D,
        source.GetImageCreateInfo().usage,samples),"replacement source allocation");
    Check(replacementSource.GetImage()!=source.GetImage(),"replacement reused a live image identity");
    fixture.Begin();Barrier(device,replacementSource,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    const auto replacementCommand=device.GetCommandBuffer().GetVKCommandBuffer();
    range={replacementSource.GetImageAspect(),0,mips,0,layers};
    if(depth){VkClearDepthStencilValue value{view.minimum,0};vkCmdClearDepthStencilImage(replacementCommand,replacementSource.GetImage(),VK_IMAGE_LAYOUT_GENERAL,&value,1,&range);}
    else {
        VkClearColorValue changed{};
        for(int component=0;component<4;++component) {
            if(format==VK_FORMAT_R32_SINT) changed.int32[component]=static_cast<std::int32_t>(view.minimum);
            else if(format==VK_FORMAT_R32_UINT) changed.uint32[component]=static_cast<std::uint32_t>(view.minimum);
            else changed.float32[component]=view.minimum;
        }
        vkCmdClearColorImage(replacementCommand,replacementSource.GetImage(),VK_IMAGE_LAYOUT_GENERAL,&changed,1,&range);
    }
    Barrier(device,replacementSource,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT);
    replacementSource.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);fixture.Submit();
    device.UnregisterRuntimeRenderTextureSource(publish.key);publish.ownerGeneration+=1000;publish.images={&replacementSource};
    Check(device.RegisterRuntimeRenderTextureSource(publish,error),error);device.RefreshRuntimeRenderTextureCatalog();
    VANS_INIT_MAIN_THREAD();const auto reloaded=session.Previews();Check(reloaded.size()==2&&reloaded[0].generation!=previews[0].generation,"owner generation did not reach pinned previews");
    auto replacement=gui.bindings.at(reloaded[0].texture);
    fixture.Begin();Check(captured->Resolve(device)==nullptr,"old generation still resolves after republish");
    auto* updated=replacement->Resolve(device);Check(updated!=nullptr,"pinned preview failed to rebind its generation");
    const auto replacementPixels=fixture.DrawAndRead(updated);
    for(int channel=0;channel<3;++channel)Check(replacementPixels[channel]<=2,"new generation retained old preview pixels");
    Check(replacementPixels[3]==255,"replacement opaque display alpha differs");
    VANS_INIT_MAIN_THREAD();session.Unregister(shared.registration);Check(session.Previews().size()==1,"shared subscription cancellation removed its peer");
    VANS_INIT_RENDER_THREAD();device.UnregisterRuntimeRenderTextureSource(publish.key);fixture.Begin();Check(replacement->Resolve(device)==nullptr,"unregistered source remains resolvable");fixture.Submit();
    VANS_INIT_MAIN_THREAD();session.Unregister(registration.registration);Check(session.Previews().empty()&&gui.bindings.empty(),"unregister retained a subscription");
    VANS_INIT_RENDER_THREAD();Check(device.DrainDeferredDeletesAfterDeviceIdle(),"preview retirement");Check(registry->RegistrationCount()==0&&gui.resources.empty(),"preview descriptor leaked");
    fixture.Begin();Check(captured->Resolve(device)==nullptr,"captured subscription survives retirement");fixture.Submit();
    std::cout<<"[RTPreviewGPU] format="<<format<<" samples="<<samples<<" sampled="<<sampled<<" pixels/shared-view/generation/registration/retirement PASS\n";
}
}
bool RunRenderTexturePreviewGpuContractTests()
{
    try
    {
        VANS_INIT_MAIN_THREAD();validationErrors=0;const auto* layers=std::getenv("VK_INSTANCE_LAYERS");
        Check(layers&&std::string(layers).find("VK_LAYER_KHRONOS_validation")!=std::string::npos,"validation layer must be enabled");
        Check(!m_GraphicsDevice&&!m_GUIBackEnd,"separate process required");
        Window window;Check(glfwInit()==GLFW_TRUE,"GLFW initialization");glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window.handle=glfwCreateWindow(64,64,"RT preview native contract",nullptr,nullptr);Check(window.handle!=nullptr,"hidden window");
        auto device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);Check(device->IsInitialized(),"native device initialization");
        GuiPort gui;m_GraphicsDevice=device.get();m_GUIBackEnd=&gui;
        struct Globals{~Globals(){m_GUIBackEnd=nullptr;m_GraphicsDevice=nullptr;}} globals;
        auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkCreateDebugUtilsMessengerEXT"));
        auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkDestroyDebugUtilsMessengerEXT"));Check(create&&destroy,"debug messenger functions");
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;info.pfnUserCallback=Validation;
        VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;Check(create(device->GetInstance(),&info,nullptr,&messenger)==VK_SUCCESS,"debug messenger");
        struct Messenger{VkInstance instance;VkDebugUtilsMessengerEXT messenger;PFN_vkDestroyDebugUtilsMessengerEXT destroy;~Messenger(){destroy(instance,messenger,nullptr);}} messageCleanup{device->GetInstance(),messenger,destroy};
        Fixture fixture(*device);EmptyFrameSource source;VansRenderSystem system(*device,source);
        auto registry=std::make_shared<VansEditorPreviewRegistry>();registry->BindRenderSystem(&system);
        VansRenderTexturePreviewSession session(*device,system,registry,std::filesystem::current_path()/"EngineAssets/Editor/Shaders/RenderTexturePreview",std::filesystem::current_path()/"Library/Artifacts/Shaders");
        RenderTexturePreviewViewSpec view;view.layer=view.mipLevel=1;view.channel=RenderTexturePreviewChannel::R;
        VkClearColorValue clear{};for(auto& value:clear.float32)value=.25f;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLE_COUNT_1_BIT,true,view,clear,64,1);
        Verify(fixture,gui,session,registry,VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLE_COUNT_1_BIT,false,view,clear,64,2);
        view.minimum=-16;view.maximum=16;for(auto& value:clear.int32)value=0;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32_SINT,VK_SAMPLE_COUNT_1_BIT,true,view,clear,128,3);
        view.minimum=0;view.maximum=16;for(auto& value:clear.uint32)value=12;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32_UINT,VK_SAMPLE_COUNT_1_BIT,true,view,clear,191,4);
        view={};view.channel=RenderTexturePreviewChannel::R;for(auto& value:clear.float32)value=.25f;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLE_COUNT_4_BIT,true,view,clear,64,5);
        view.minimum=-16;view.maximum=16;for(auto& value:clear.int32)value=0;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32_SINT,VK_SAMPLE_COUNT_4_BIT,true,view,clear,128,9);
        view.minimum=0;view.maximum=16;for(auto& value:clear.uint32)value=12;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32_UINT,VK_SAMPLE_COUNT_4_BIT,true,view,clear,191,10);
        view.minimum=0;view.maximum=1;for(auto& value:clear.float32)value=.25f;
        view.channel=RenderTexturePreviewChannel::RGBA;
        Verify(fixture,gui,session,registry,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_SAMPLE_COUNT_1_BIT,false,view,clear,64,6);
        Verify(fixture,gui,session,registry,VK_FORMAT_R8G8B8A8_UNORM,VK_SAMPLE_COUNT_1_BIT,true,view,clear,64,7);
        view.channel=RenderTexturePreviewChannel::R;view.exposure=1;view.gamma=2;
        Verify(fixture,gui,session,registry,VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLE_COUNT_1_BIT,true,view,clear,180,8);
        Check(validationErrors==0,"Vulkan validation errors="+std::to_string(validationErrors));
        std::cout<<"[RTPreviewGPU] CHECKS PASS validationErrors=0\n";return true;
    }
    catch(const std::exception& error){std::cerr<<"[RTPreviewGPU] FAIL "<<error.what()<<'\n';return false;}
}
