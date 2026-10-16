#define VK_NO_PROTOTYPES
#include "TreeShadowGpuContractTests.h"
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/BRDFData/VansLight.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationSystem.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace VansGraphics;
void Check(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}
struct Resources
{
    VansVKDevice& device;
    VansVKImage depth;
    VansVKBuffer readback, lights, materials, leaves, instances, visible, vertices;
    VansTexture texture;
    VansGraphicsShader shader;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout globalLayout = VK_NULL_HANDLE, emptyLayout = VK_NULL_HANDLE, treeLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globals, trees;
    ~Resources()
    {
        device.WaitForDevice();
        shader.RetireRenderPassPipelineVariants(pass);
        auto logic = device.GetLogicDevice();
        if (framebuffer) vkDestroyFramebuffer(logic, framebuffer, nullptr);
        if (pass) vkDestroyRenderPass(logic, pass, nullptr);
        auto* manager = VansVKDescriptorManager::GetInstance();
        manager->DestroyDescriptorSet(globals);
        manager->DestroyDescriptorSet(trees);
        for (auto layout : {globalLayout, emptyLayout, treeLayout})
            if (layout) manager->ReleaseDescriptorSetLayout(layout);
        depth.DestroyVulkanImage(logic);
        for (auto* buffer : {&readback, &lights, &materials, &leaves, &instances, &visible, &vertices})
            buffer->DestroyVulkanBuffer(logic);
    }
};
void Buffer(VansVKDevice& device, VansVKBuffer& buffer, std::size_t size,
    VkBufferUsageFlags usage, const void* bytes = nullptr)
{
    Check(buffer.CreatVulkanBuffer(device.GetLogicDevice(), size, VK_FORMAT_UNDEFINED, usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && buffer.PersistentMap(),
        "Tree shadow buffer allocation");
    if (bytes) buffer.SetBufferData(const_cast<void*>(bytes), 0, size);
}
}

void VerifyTreeShadowGpu(VansGraphics::VansVKDevice& device)
{
    using namespace VansGraphics;
#ifdef _DEBUG
    struct RestoreThreadRole
    {
        VansThreadRole previous = g_CurrentThreadRole;
        ~RestoreThreadRole() { g_CurrentThreadRole = previous; }
    } restoreThreadRole;
#endif
    VANS_INIT_RENDER_THREAD();
    Resources r{device};
    auto logic = device.GetLogicDevice();
    constexpr uint32_t width = 3;
    Check(r.depth.CreateVulkanImage(logic, {width, 1, 1}, VK_FORMAT_D32_SFLOAT, 1, 1, VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT),
        "Tree shadow depth allocation");
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1; info.pAttachments = &attachment;
    info.subpassCount = 1; info.pSubpasses = &subpass;
    info.dependencyCount = 1; info.pDependencies = &dependency;
    Check(vkCreateRenderPass(logic, &info, nullptr, &r.pass) == VK_SUCCESS, "Tree shadow render pass");
    const auto view = r.depth.GetImageView();
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = r.pass; fb.attachmentCount = 1; fb.pAttachments = &view;
    fb.width = width; fb.height = fb.layers = 1;
    Check(vkCreateFramebuffer(logic, &fb, nullptr, &r.framebuffer) == VK_SUCCESS, "Tree shadow framebuffer");
    Buffer(device, r.readback, width * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VansRenderLightFrameData lightFrame;
    lightFrame.prepared = true; lightFrame.punctualShadowMapWidth = 1;
    lightFrame.directionalLights.resize(1);
    for (auto& matrix : lightFrame.directionalLights[0].m_ShadowMatrix) matrix = glm::mat4(1);
    std::vector<uint8_t> lightBytes;
    Check(VansLightManager::BuildRenderLightBufferPayload(lightFrame, {}, {}, lightBytes), "Tree shadow light payload");
    Buffer(device, r.lights, lightBytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lightBytes.data());
    // Two independent material slots and three exactly sampled alpha texels.
    std::array<glm::vec4, 4> pbr{};
    std::array<VansTreeLeafParamsGPU, 2> leaf{};
    Buffer(device, r.materials, sizeof(pbr), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer(device, r.leaves, sizeof(leaf), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    TreeInstanceGPU instance;
    const uint32_t index = 0;
    Buffer(device, r.instances, sizeof(instance), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &instance);
    Buffer(device, r.visible, sizeof(index), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &index);
    struct Vertex { glm::vec4 position; glm::vec2 uv; glm::vec3 normal, tangent, bitangent; };
    const std::array<Vertex, 3> vertices{{{{-1,-1,0,1},{0,0}}, {{3,-1,0,1},{2,0}}, {{-1,3,0,1},{0,2}}}};
    Buffer(device, r.vertices, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
    const std::array<glm::vec4, 3> texels{{{1,1,1,0}, {1,1,1,.5f}, {1,1,1,1}}};
    auto& command = device.GetImmediateGraphicsCommandBuffer();
    r.texture.LoadFromMemory(command, texels.data(), sizeof(texels), width, 1, VK_FORMAT_R32G32B32A32_SFLOAT);
    Check(r.texture.GetImage().GetImage() != VK_NULL_HANDLE, "Tree shadow alpha upload");
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(r.globalLayout, r.globals, MAX_BINDLESS_TEXTURES, 1);
    auto* manager = VansVKDescriptorManager::GetInstance();
    Check(manager->CreateDesciptorSetLayout({}, r.emptyLayout), "Tree shadow empty layout");
    const std::vector<VkDescriptorSetLayoutBinding> treeBindings{
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};
    Check(manager->CreateDesciptorSetLayout(treeBindings, r.treeLayout) &&
        manager->AllocateDescriptorSet({r.treeLayout}, r.trees), "Tree shadow instance descriptors");
    manager->BeginDescriptorUpdate();
    manager->WriteBufferDescriptor(r.globals[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{r.lights.GetNativeBuffer(),0,lightBytes.size()}});
    manager->WriteBufferDescriptor(r.globals[0], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{r.materials.GetNativeBuffer(),0,sizeof(pbr)}});
    manager->WriteBufferDescriptor(r.globals[0], 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{r.leaves.GetNativeBuffer(),0,sizeof(leaf)}});
    auto& texture = r.texture.GetImage();
    manager->WriteImageDescriptor(r.globals[0], 50, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        std::vector<VkDescriptorImageInfo>(10, {texture.GetSampler(),texture.GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}));
    manager->WriteBufferDescriptor(r.trees[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{r.instances.GetNativeBuffer(),0,sizeof(instance)}});
    manager->WriteBufferDescriptor(r.trees[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{r.visible.GetNativeBuffer(),0,sizeof(index)}});
    manager->CommitDescriptorUpdates();
    VansShaderEntry entry;
    entry.name = "TreeShadowGPU"; entry.relativePath = "EngineAssets/Shaders/Tree/Shadow";
    entry.depthTest = entry.depthWrite = VK_TRUE; entry.cullMode = VK_CULL_MODE_NONE;
    entry.colorAttachmentCount = 0; entry.pushConstantSize = sizeof(TreeShadowPushConstants);
    entry.explicitStageFiles = {{VK_SHADER_STAGE_VERTEX_BIT,"TreeShadow.vert"},{VK_SHADER_STAGE_FRAGMENT_BIT,"TreeShadow.frag"}};
    Check(VansShaderManager::Get().ConfigureGraphicsShader(r.shader, entry, entry.relativePath), "Tree shadow shader state");
    r.shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
    Check(r.shader.InitShader(logic, entry.relativePath, entry.explicitStageFiles), "Production TreeShadow shader load");
    r.shader.SetPushConstant(entry.pushConstantSize);
    std::vector<VkVertexInputBindingDescription> bindings{{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}};
    std::vector<VkVertexInputAttributeDescription> attributes{
        {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,position)},
        {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,uv)},
        {2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)},
        {3,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,tangent)},
        {4,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,bitangent)}};
    GlobalStateData state; state.currentRenderPass = r.pass;
    state.viewport = {0,0,width,1,0,1}; state.scissor = {{0,0},{width,1}};
    state.vertexInputBindingDescriptions = &bindings; state.vertexInputAttributeDescriptions = &attributes;
    auto* pipeline = r.shader.GetGraphicsPipeline(logic, state, {r.globalLayout,r.emptyLayout,r.emptyLayout,r.treeLayout});
    Check(pipeline && pipeline->GetNativePipeline(), "Tree shadow graphics pipeline");
    struct Case { int material; float cutoff, ordinaryCutoff; uint32_t leaf, expected; };
    const std::array<Case, 9> cases{{
        {0,.5f,0,1,6}, {0,.75f,.25f,1,4}, {0,.25f,.75f,1,6},
        {1,.75f,.25f,1,4}, {1,.25f,.75f,1,6},
        {1,0,.75f,1,7}, {1,1,.25f,1,4}, {0,.75f,.25f,0,7}, {1,1,1,0,7}}};
    for (const auto& test : cases)
    {
        leaf[0].scattering.w = leaf[1].scattering.w = .5f;
        leaf[test.material].scattering.w = test.cutoff;
        pbr[1].w = pbr[3].w = test.ordinaryCutoff;
        r.leaves.SetBufferData(leaf.data(), 0, sizeof(leaf));
        r.materials.SetBufferData(pbr.data(), 0, sizeof(pbr));
        Check(command.ResetCommandBuffer(false) && command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Tree shadow begin");
        const auto cb = command.GetVKCommandBuffer();
        VkClearValue clear{}; clear.depthStencil.depth = 1;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = r.pass; begin.framebuffer = r.framebuffer; begin.renderArea = state.scissor;
        begin.clearValueCount = 1; begin.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetNativePipeline());
        vkCmdSetViewport(cb,0,1,&state.viewport); vkCmdSetScissor(cb,0,1,&state.scissor);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->GetNativePipelineLayout(),0,1,r.globals.data(),0,nullptr);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->GetNativePipelineLayout(),3,1,r.trees.data(),0,nullptr);
        const auto vertexBuffer = r.vertices.GetNativeBuffer(); const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb,0,1,&vertexBuffer,&offset);
        const TreeShadowPushConstants push{test.material,0,0,0,test.leaf};
        vkCmdPushConstants(cb,pipeline->GetNativePipelineLayout(),VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
        vkCmdDraw(cb,3,1,0,0); vkCmdEndRenderPass(cb);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1}; copy.imageExtent = {width,1,1};
        vkCmdCopyImageToBuffer(cb,r.depth.GetImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,r.readback.GetNativeBuffer(),1,&copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = r.readback.GetNativeBuffer(); host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        Check(command.EndCommandBufferRecord() && VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(),logic,{cb},{},{},
            command.m_CommandBufferFinishSubmitFence,true), "Tree shadow submit");
        r.readback.InvalidateMappedRange(0,width*sizeof(float));
        const auto* depths = static_cast<const float*>(r.readback.GetMappedPtr());
        uint32_t mask = 0;
        for (uint32_t i=0;i<width;++i)
        {
            Check(std::abs(depths[i]-.5f)<.0001f || std::abs(depths[i]-1)<.0001f, "Tree shadow depth must be geometry or clear");
            if (depths[i]<.75f) mask |= 1u<<i;
        }
        std::cout << "[TreeShadowGPU] material=" << test.material << " leaf=" << test.leaf << " cutoff=" << test.cutoff
            << " ordinary=" << test.ordinaryCutoff << " mask=" << mask << " expected=" << test.expected << '\n';
        Check(mask == test.expected, "Production TreeShadow must use leaf cutoff and preserve solid trunks");
    }
    Check(command.ResetCommandBuffer(false), "Tree shadow reset");
    std::cout << "[TreeShadowGPU] PASS: production VS/FS depth, alpha endpoints/equality, material slots, edits and trunks\n";
}
