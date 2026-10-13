#define VK_NO_PROTOTYPES
#include "TerrainInteractionContractTests.h"
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/TerrainCore/VansTerrain.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace VansGraphics;
void Check(bool valid,const char* message) { if (!valid) throw std::runtime_error(message); }
struct Targets
{
    VansVKDevice& device;
    std::array<VansVKImage,5> images;
    VansVKBuffer readback,camera,weather,metadata;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;
    VkDescriptorSetLayout globalLayout=VK_NULL_HANDLE,fieldsLayout=VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globals,fields;
    ~Targets()
    {
        device.WaitForDevice();
        for (const auto* name : {"Terrain","TerrainTess"})
            if (auto* shader=VansShaderManager::Get().FindGraphicsShader(name)) shader->RetireRenderPassPipelineVariants(pass);
        if (framebuffer) vkDestroyFramebuffer(device.GetLogicDevice(),framebuffer,nullptr);
        if (pass) vkDestroyRenderPass(device.GetLogicDevice(),pass,nullptr);
        auto* manager=VansVKDescriptorManager::GetInstance();
        manager->DestroyDescriptorSet(globals);manager->DestroyDescriptorSet(fields);
        if (globalLayout) manager->ReleaseDescriptorSetLayout(globalLayout);
        if (fieldsLayout) manager->ReleaseDescriptorSetLayout(fieldsLayout);
        for (auto& image : images) image.DestroyVulkanImage(device.GetLogicDevice());
        for (auto* buffer : {&readback,&camera,&weather,&metadata}) buffer->DestroyVulkanBuffer(device.GetLogicDevice());
    }
};
void Buffer(VansVKDevice& device,VansVKBuffer& buffer,std::size_t size,VkBufferUsageFlags usage,const void* bytes=nullptr)
{
    Check(buffer.CreatVulkanBuffer(device.GetLogicDevice(),size,VK_FORMAT_UNDEFINED,usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && buffer.PersistentMap(),"Terrain GPU buffer allocation");
    if (bytes) buffer.SetBufferData(const_cast<void*>(bytes),0,size);
}
}
void VerifyTerrainInteractionGpu(VansGraphics::VansVKDevice& device)
{
    using namespace VansGraphics;
    VANS_INIT_RENDER_THREAD();
    auto logic=device.GetLogicDevice();auto& shaders=VansShaderManager::Get();
    VansShaderEntry shader;shader.depthTest=shader.depthWrite=VK_FALSE;shader.cullMode=VK_CULL_MODE_NONE;shader.colorAttachmentCount=5;
    shader.relativePath="EngineAssets/Shaders/Terrain/Deferred";
    shader.explicitStageFiles={{VK_SHADER_STAGE_VERTEX_BIT,"Terrain.vert"},{VK_SHADER_STAGE_FRAGMENT_BIT,"Terrain.frag"}};
    shaders.RegisterGraphicsShader("Terrain",shader);
    shader.relativePath="EngineAssets/Shaders/Terrain/DeferredTess";shader.primitiveTopology=VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;shader.patchControlPoints=3;
    shader.explicitStageFiles={{VK_SHADER_STAGE_VERTEX_BIT,"TerrainTess.vert"},{VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,"TerrainTess.tesc"},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,"TerrainTess.tese"},{VK_SHADER_STAGE_FRAGMENT_BIT,"TerrainTess.frag"}};
    shaders.RegisterGraphicsShader("TerrainTess",shader);
    shader.relativePath="EngineAssets/Shaders/Terrain/Shadow";shader.primitiveTopology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;shader.patchControlPoints=1;
    shader.colorAttachmentCount=0;shader.pushConstantSize=4;
    shader.explicitStageFiles={{VK_SHADER_STAGE_VERTEX_BIT,"TerrainShadow.vert"},{VK_SHADER_STAGE_FRAGMENT_BIT,"TerrainShadow.frag"}};
    shaders.RegisterGraphicsShader("TerrainShadow",shader);
    Check(shaders.LoadAll(std::filesystem::current_path().generic_string()+"/",std::filesystem::current_path()/"Library/Artifacts/Shaders",logic),"Production Terrain Shader load");
    Targets targets{device};
    constexpr std::uint32_t width=32;
    constexpr std::size_t imageBytes=width*width*4*sizeof(float);
    std::array<VkAttachmentDescription,5> attachments{};std::array<VkAttachmentReference,5> references{};std::array<VkImageView,5> views{};
    for (std::uint32_t i=0; i<5; ++i)
    {
        Check(targets.images[i].CreateVulkanImage(logic,{width,width,1},VK_FORMAT_R32G32B32A32_SFLOAT,1,1,VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_SAMPLE_COUNT_1_BIT),"Terrain GPU target allocation");
        auto& attachment=attachments[i];attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        references[i]={i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};views[i]=targets.images[i].GetImageView();
    }
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=5;subpass.pColorAttachments=references.data();
    VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount=5;info.pAttachments=attachments.data();info.subpassCount=1;info.pSubpasses=&subpass;
    info.dependencyCount=1;info.pDependencies=&dependency;Check(vkCreateRenderPass(logic,&info,nullptr,&targets.pass)==VK_SUCCESS,"Terrain GPU render pass");
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=targets.pass;fb.attachmentCount=5;fb.pAttachments=views.data();fb.width=fb.height=width;fb.layers=1;
    Check(vkCreateFramebuffer(logic,&fb,nullptr,&targets.framebuffer)==VK_SUCCESS,"Terrain GPU framebuffer");
    Buffer(device,targets.readback,imageBytes*5,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VansCameraDataGPU camera{};camera.cameraPosition={0,4,0,1};camera.viewMatrix=glm::mat4(1);camera.projectionMatrix=glm::mat4(1);
    glm::mat4 projection(0);projection[0][0]=1;projection[2][1]=1;projection[1][2]=.1f;projection[3][2]=.5f;projection[3][3]=1;
    camera.viewProjectionMatrix=camera.unjitteredViewProjectionMatrix=camera.lastUnjitteredViewProjectionMatrix=projection;
    camera.screenParams={width,width,1.0f/width,1.0f/width};
    Buffer(device,targets.camera,sizeof(camera),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&camera);
    const std::array<glm::vec4,7> weather{};const std::array<glm::uvec4,2> metadata{};
    Buffer(device,targets.weather,sizeof(weather),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,weather.data());
    Buffer(device,targets.metadata,sizeof(metadata),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,metadata.data());
    std::array<VansTexture,6> textures;
    const std::array<glm::vec4,6> texels{{{.2f,.4f,.6f,1},{.5f,.5f,1,1},{0,1,0,.5f},
        {.6f,.075f,.15f,.75f},{.35f,.45f,.25f,.5f},{-.075f,0,0,0}}};
    for (std::size_t i=0;i<textures.size();++i)
        textures[i].LoadFromMemory(device.GetCommandBuffer(),&texels[i],sizeof(glm::vec4),1,1,VK_FORMAT_R32G32B32A32_SFLOAT);
    const auto descriptor=[](VansTexture& texture)
    { auto& image=texture.GetImage();return VkDescriptorImageInfo{image.GetSampler(),image.GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; };
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(targets.globalLayout,targets.globals,MAX_BINDLESS_TEXTURES,1);
    VansDescriptorSetLayoutFactory::CreateAndAllocate_PcgSplineField(targets.fieldsLayout,targets.fields,1);
    auto* manager=VansVKDescriptorManager::GetInstance();manager->BeginDescriptorUpdate();
    manager->WriteBufferDescriptor(targets.globals[0],0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,{{targets.camera.GetNativeBuffer(),0,sizeof(camera)}});
    manager->WriteBufferDescriptor(targets.globals[0],41,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,{{targets.weather.GetNativeBuffer(),0,sizeof(weather)}});
    manager->WriteImageDescriptor(targets.globals[0],42,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,{descriptor(textures[1])});
    manager->WriteBufferDescriptor(targets.fields[0],0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{{targets.metadata.GetNativeBuffer(),0,sizeof(metadata)}});
    for (std::uint32_t binding=1;binding<=6;++binding) manager->WriteImageDescriptor(targets.fields[0],binding,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,{descriptor(textures[0])});
    manager->CommitDescriptorUpdates();
    auto asset=std::make_shared<Vans::VansTerrainAsset>();asset->width=asset->height=2;asset->heights.resize(4);
    asset->splatPixels[0].resize(16);asset->splatPixels[1].resize(16);for(std::size_t i=0;i<4;++i) asset->splatPixels[0][i*4]=255;
    asset->settings.terrainSize=64;asset->settings.maxHeight=1;asset->settings.heightOffset=0;asset->settings.heightDetailEnabled=false;
    asset->settings.tessellationTargetPixels=1;
    TerrainConfig config;config.assetGuid=Vans::VansAssetGuid::FromStableName("TerrainGPU","terrain");config.asset=asset;
    config.layers={{&textures[0],&textures[1],&textures[2],1}};
    VansTerrain terrain;terrain.Init(&device,config);
    Check(terrain.m_DescriptorSets.size()==VansCommandRecordingConfig::MaximumFramesInFlight,"Terrain must allocate per-frame descriptors");
    std::string error;
    for (std::size_t i=0;i<3;++i)
    {
        VansRuntimeRenderTextureCatalog::Source source;source.key="external:terrain-test-"+std::to_string(i);source.name="Terrain test";source.owner="Generic external producer";
        source.ownerGeneration=1;source.images={&textures[i+3].GetImage()};
        source.worldRect=glm::vec4(-.8f,-.8f,1.6f,1.6f);
        Check(device.RegisterRuntimeRenderTextureSource(std::move(source),error),error.c_str());
    }
    struct SourcesCleanup {VansVKDevice& device;~SourcesCleanup(){device.WaitForDevice();for(int i=0;i<3;++i)device.UnregisterRuntimeRenderTextureSource("external:terrain-test-"+std::to_string(i));}} cleanup{device};
    device.RefreshRuntimeRenderTextureCatalog();
    Vans::VansTerrainInteraction input;input.window.worldRect={-.8f,-.8f,1.6f,1.6f};input.edgeFadeMeters=0;
    for (int test=0;test<7;++test)
    {
        input.enabled=test!=0;input.albedo=input.normal=input.height="";
        if (test>=2) {input.albedo="external:terrain-test-0";input.normal="external:terrain-test-1";input.height="external:terrain-test-2";}
        input.window.worldRect=test==3?glm::vec4(10,10,1.6f,1.6f):glm::vec4(-.8f,-.8f,1.6f,1.6f);
        input.window.cameraCentered=test==6;
        if (test==6) input.window.worldRect={10,10,1.6f,1.6f}; // 相机要求的新窗口不能取代仍对应旧图像的生产窗口。
        if (test==4) {input.albedo="external:missing";input.normal=input.height="";}
        terrain.SetTessellationEnabled(test!=5);
        terrain.UpdateInteraction(input);terrain.Update(camera.cameraPosition,projection);
        auto& command=device.GetCommandBuffer();Check(command.ResetCommandBuffer(false)&&command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Terrain GPU draw begin");
        std::array<VkClearValue,5> clear{};VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=targets.pass;begin.framebuffer=targets.framebuffer;
        begin.renderArea={{0,0},{width,width}};begin.clearValueCount=5;begin.pClearValues=clear.data();command.BeginRenderPass(begin);
        GlobalStateData state;state.currentRenderPass=targets.pass;state.viewport={0,0,width,width,0,1};state.scissor={{0,0},{width,width}};
        command.SetViewport(0,{state.viewport});command.SetScissor(0,{state.scissor});
        std::vector<VkDescriptorSetLayout> layouts{targets.globalLayout,terrain.m_DescriptorSetLayout,targets.fieldsLayout};
        std::vector<VkDescriptorSet> sets{targets.globals[0],terrain.m_DescriptorSets[0],targets.fields[0]};
        terrain.Draw(command,state,layouts,sets);command.EndRenderPass();
        for(std::size_t i=0;i<5;++i)
        {
            VkBufferImageCopy copy{};copy.bufferOffset=i*imageBytes;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,width,1};
            vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(),targets.images[i].GetImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,targets.readback.GetNativeBuffer(),1,&copy);
        }
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=targets.readback.GetNativeBuffer();host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command.GetVKCommandBuffer(),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        Check(command.EndCommandBufferRecord() && VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(),logic,{command.GetVKCommandBuffer()},{},{},command.m_CommandBufferFinishSubmitFence,true),"Terrain GPU submit");
        targets.readback.InvalidateMappedRange(0,imageBytes*5);const auto* bytes=static_cast<const float*>(targets.readback.GetMappedPtr());
        const auto pixel=(16*width+16)*4;const auto* normal=bytes+pixel;const auto* color=bytes+imageBytes/sizeof(float)+pixel;const auto* position=bytes+3*imageBytes/sizeof(float)+pixel;
        const bool active=test==2||test==5||test==6;
        Check(std::abs(position[0]-.03125f)<.002f,"Terrain GPU must produce actual geometry at the sampled world position");
        Check(std::abs(position[1]-(active?-.075f:0))<.001f,"Terrain actual TES/VS position must use interaction height, or remain neutral");
        Check(std::abs(color[0]-(active?.65f:.2f))<.001f && std::abs(color[1]-(active?.175f:.4f))<.001f,"Terrain actual deferred albedo/coverage");
        Check(std::abs(normal[0]-(active?.2297529f:0))<.002f && std::abs(normal[1]-(active?.973249f:1))<.002f,"Terrain actual deferred world normal with partial premultiplied coverage");
        Check(command.ResetCommandBuffer(false),"Terrain GPU draw reset");
    }
    std::cout<<"[TerrainInteractionGPU] production Terrain TES/VS/Deferred, external RT, albedo/normal/height, disabled/empty/outside/missing/produced-window PASS\n";
}
