#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansProjectRenderPassRuntime.h"
#include "VansProjectPassShaderContract.h"
#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansVKDescriptorManager.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "VansMesh.h"
#include "VansTexture.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../RuntimeCore/VansFramePhase.h"
#include "../../Util/VansProfiler.h"
#include "VansDescriptorSetLayouts.h"
#include "VansPipelineRegistry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace VansGraphics
{
namespace
{
VkFormat NativeFormat(Vans::VansRenderTargetFormat format)
{
    switch (format)
    {
    case Vans::VansRenderTargetFormat::R8: return VK_FORMAT_R8_UNORM;
    case Vans::VansRenderTargetFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case Vans::VansRenderTargetFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Vans::VansRenderTargetFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Vans::VansRenderTargetFormat::D32: return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}
std::uint32_t TexelBytes(Vans::VansRenderTargetFormat format)
{
    switch (format) {case Vans::VansRenderTargetFormat::R8: return 1; case Vans::VansRenderTargetFormat::RGBA16F: return 8;
    case Vans::VansRenderTargetFormat::RGBA32F: return 16; default: return 4;}
}
VkImageUsageFlags NativeUsage(std::uint32_t usage)
{
    VkImageUsageFlags result = 0;
    if (usage & Vans::RenderTargetColorAttachment) result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage & Vans::RenderTargetDepthAttachment) result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (usage & Vans::RenderTargetSampled) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & Vans::RenderTargetTransferSource) result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage & Vans::RenderTargetTransferDestination) result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return result;
}
void Transition(VansVKCommandBuffer& command, VansVKImage& image, VkImageLayout from, VkImageLayout to,
    VkAccessFlags src, VkAccessFlags dst)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from; barrier.newLayout = to;
    barrier.srcAccessMask = src; barrier.dstAccessMask = dst;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.GetImage();
    barrier.subresourceRange = {image.GetImageAspect(), 0, 1, 0, 1};
    vkCmdPipelineBarrier(command.GetVKCommandBuffer(), from == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
VansSubmitResourceAccess ImageAccess(const std::string& key, VansVKImage& image, bool write, VkPipelineStageFlags stage, VkAccessFlags access)
{
    VansSubmitResourceAccess result;
    result.name = key; result.image = true; result.persistent = true; result.write = write;
    result.stages = stage; result.access = access; result.layout = VK_IMAGE_LAYOUT_GENERAL;
    result.physical = VansPhysicalResourceIdentity::ForImage(image.GetImage()); return result;
}
bool HostBuffer(VkDevice device, VansVKBuffer& buffer, std::uint64_t bytes, VkBufferUsageFlags usage)
{
    return buffer.CreatVulkanBuffer(device, bytes, VK_FORMAT_UNDEFINED, usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && buffer.PersistentMap();
}
VansRenderNode* SceneNodeFor(VansScene& scene, const VansProjectSceneDrawCandidate& candidate)
{
    const std::vector<VansRenderNode*>* nodes = nullptr;
    switch (candidate.bucket)
    {
    case VansProjectSceneBucket::Opaque: nodes = &scene.GetOpaqueRenderNodes(); break;
    case VansProjectSceneBucket::ForwardOpaque: nodes = &scene.GetForwardOpaquePreAtmosphereRenderNodes(); break;
    case VansProjectSceneBucket::Transparent: nodes = &scene.GetTransparentRenderNodes(); break;
    case VansProjectSceneBucket::Hair: nodes = &scene.GetHairRenderNodes(); break;
    case VansProjectSceneBucket::Decal: nodes = &scene.GetDecalRenderNodes(); break;
    case VansProjectSceneBucket::PostProcess: nodes = &scene.GetPostProcessRenderNodes(); break;
    }
    return nodes && candidate.nodeIndex < nodes->size() ? (*nodes)[candidate.nodeIndex] : nullptr;
}
bool ValidateGeometry(VansMesh& mesh, const VansProjectPassShaderSignature& signature)
{
    if (!mesh.GetVertexBufferParameter().Buffer || !mesh.GetIndexBufferParameter().Buffer || !mesh.GetIndexCount()) return false;
    for (const auto& input : signature.vertexInputs)
    {
        const auto found = std::find_if(mesh.m_VertexInputAttributeDescriptions.begin(),mesh.m_VertexInputAttributeDescriptions.end(),
            [&](const auto& attribute) { return attribute.location == input.location; });
        if (found == mesh.m_VertexInputAttributeDescriptions.end()) return false;
        auto actual = found->format;
        switch(actual) {
        case VK_FORMAT_R16_SFLOAT: actual=VK_FORMAT_R32_SFLOAT; break;
        case VK_FORMAT_R16G16_SFLOAT: actual=VK_FORMAT_R32G32_SFLOAT; break;
        case VK_FORMAT_R16G16B16_SFLOAT: actual=VK_FORMAT_R32G32B32_SFLOAT; break;
        case VK_FORMAT_R16G16B16A16_SFLOAT: actual=VK_FORMAT_R32G32B32A32_SFLOAT; break;
        default: break; }
        if (actual != input.format) return false;
    }
    return true;
}
}

// Pass 实例资源所有者，复用引擎 Image/Buffer 分配与 Device 退役接口；不建立资源分配器或全局注册表。
struct VansProjectRenderPassRuntime::Owner
{
    struct Slot
    {
        VansVKBuffer parameters;
        std::vector<VansVKBuffer> inputs;
    };
    struct TargetSlot
    {
        std::vector<VansVKImage> targets;
        VansFrameBuffer framebuffer;
        bool initialized = false;
        std::array<std::int64_t, 2> worldTexelOrigin{};
        std::optional<glm::vec4> worldRect;
    };
    struct Pass
    {
        VkExtent2D extent{};
        std::vector<Slot> slots;
        std::vector<TargetSlot> targetSlots;
        std::array<VansVKRenderPass, 3> renderPasses; // First use, first use with history, subsequent use.
        std::vector<VkClearValue> clear;
        VkDescriptorSetLayout passLayout = VK_NULL_HANDLE, objectLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> passSets, objectSets;
        VansVKDrawInstanceArena drawArena;
        VansDrawSubmissionList submission;
        std::vector<VansGraphicsShader*> shaders;
        std::vector<VansProjectPassShaderSignature> signatures;
        std::uint32_t lastSubmittedSlot = UINT32_MAX;
        std::uint32_t previousSubmittedSlot = UINT32_MAX;
        std::uint32_t recordedTargetSlot = UINT32_MAX;
        std::uint32_t parameterBytes = 16;
        std::array<std::int64_t, 2> recordedWorldTexelOrigin{};
        std::optional<glm::vec4> recordedWorldRect;
        VansProjectRenderPassFrameDiagnostic diagnostic, pendingDiagnostic;
    };
    VkDevice device = VK_NULL_HANDLE;
    std::shared_ptr<const VansProjectRenderPassConfiguration> configuration;
    std::uint32_t frameSlots = 0;
    VkExtent2D resolution{};
    std::uint64_t sceneEpoch = 0, generation = 0, allocatedBytes = 0;
    std::vector<Pass> passes;
    VkDescriptorSetLayout globalLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globalSets;
    ~Owner()
    {
        auto* descriptors = VansVKDescriptorManager::GetInstance();
        descriptors->DestroyDescriptorSet(globalSets);
        descriptors->ReleaseDescriptorSetLayout(globalLayout);
        for (auto& pass : passes)
        {
            descriptors->DestroyDescriptorSet(pass.passSets);
            descriptors->DestroyDescriptorSet(pass.objectSets);
            descriptors->ReleaseDescriptorSetLayout(pass.passLayout);
            descriptors->ReleaseDescriptorSetLayout(pass.objectLayout);
            for (auto& slot : pass.slots)
            {
                slot.parameters.DestroyVulkanBuffer(device);
                for (auto& buffer : slot.inputs) buffer.DestroyVulkanBuffer(device);
            }
            for (auto& slot : pass.targetSlots)
            {
                slot.framebuffer.DestroyFrameBuffer(device);
                for (auto& target : slot.targets) target.DestroyVulkanImage(device);
            }
            for (auto& renderPass : pass.renderPasses)
            {
                if (renderPass.GetRenderPass()) for (auto* shader : pass.shaders) shader->RetireRenderPassPipelineVariants(renderPass.GetRenderPass());
                renderPass.DestroyRenderPass(device);
            }
            pass.drawArena.Destroy(device);
        }
        VansPipelineRegistry::Get().Compact();
    }
    bool Build(VansVKDevice& backend, VansScene& scene, const VansProjectRenderPassFrameInput& frame, std::string& error)
    {
        const auto fail = [&](const std::string& message) { error = message; return false; };
        auto* descriptors = VansVKDescriptorManager::GetInstance();
        VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(globalLayout, globalSets, MAX_BINDLESS_TEXTURES, frameSlots);
        if (!globalLayout || globalSets.size() != frameSlots) return fail("Project Pass global descriptor allocation failed");
        const auto& limits = backend.GetDeviceProperties().limits;
        passes.resize(configuration->passes.size());
        for (std::size_t pi = 0; pi < passes.size(); ++pi)
        {
            auto& pass = passes[pi]; const auto& definition = configuration->passes[pi].definition;
            const auto& size = definition.targets.front();
            pass.extent = size.followsRenderResolution ? VkExtent2D{static_cast<std::uint32_t>(std::max(1.0, std::ceil(resolution.width * double(size.resolutionScale)))),
                static_cast<std::uint32_t>(std::max(1.0, std::ceil(resolution.height * double(size.resolutionScale))))} : VkExtent2D{size.width, size.height};
            if (pass.extent.width > limits.maxFramebufferWidth || pass.extent.height > limits.maxFramebufferHeight ||
                pass.extent.width > limits.maxImageDimension2D || pass.extent.height > limits.maxImageDimension2D) return fail(definition.passId + ": target extent exceeds device limits");
            pass.parameterBytes = static_cast<std::uint32_t>(frame.passes[pi].parameters.size());
            if (pass.parameterBytes > limits.maxUniformBufferRange) return fail(definition.passId + ": parameter UBO exceeds device limit");
            std::uint64_t bytes = pass.parameterBytes;
            for (const auto& buffer : definition.buffers)
            {
                const auto count = std::uint64_t(buffer.stride) * buffer.maxElements;
                if (count > (buffer.role == "uniformInput" ? limits.maxUniformBufferRange : limits.maxStorageBufferRange)) return fail(definition.passId + ": input buffer exceeds descriptor range limit");
                bytes += count;
            }
            bytes *= frameSlots;
            // History 独立于 frame context；单帧在途也需要不同的当前/上一帧图像。
            const auto targetSlots = definition.previousFrameHistory ? std::max(frameSlots, 2u) : frameSlots;
            for (const auto& target : definition.targets)
                bytes += std::uint64_t(pass.extent.width) * pass.extent.height * TexelBytes(target.format) * targetSlots;
            if (definition.source == Vans::VansRenderPassSource::Scene) bytes += std::uint64_t(definition.maxDraws) * sizeof(VansDrawInstanceDataGPU) * VansVKDrawInstanceArena::FrameSlotCount;
            if (bytes > definition.budgetBytes) return fail(definition.passId + ": complete frame-slot resource allocation exceeds budgetBytes");
            allocatedBytes += bytes;
            pass.diagnostic.passId = definition.passId;
            pass.diagnostic.payloadBytes = bytes;
            const auto& shaderNames = configuration->passes[pi].shaderNames;
            if (shaderNames.size() != definition.shaders.size()) return fail(definition.passId + ": resolved Shader list is inconsistent");
            for (std::size_t shaderIndex = 0; shaderIndex < shaderNames.size(); ++shaderIndex)
            {
                const auto& name = shaderNames[shaderIndex];
                const auto* entry = VansShaderManager::Get().FindShaderEntry(name);
                if (!entry || entry->assetGuid != definition.shaders[shaderIndex].ToString())
                    return fail(definition.passId + ": Shader runtime name belongs to another asset: " + name);
                auto* shader = VansShaderManager::Get().FindGraphicsShader(name);
                VansProjectPassShaderSignature signature;
                if (!shader || !ValidateProjectPassShader(definition, *shader, pass.parameterBytes, signature, error)) return false;
                pass.shaders.push_back(shader); pass.signatures.push_back(std::move(signature));
            }
            std::vector<VkDescriptorSetLayoutBinding> bindings{{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr}};
            for (const auto& input : definition.buffers) bindings.push_back({input.binding,
                input.role == "uniformInput" ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr});
            std::vector<VkDescriptorImageInfo> textureInfos;
            const auto& textureNames = configuration->passes[pi].textureNames;
            if (textureNames.size() != definition.textures.size()) return fail(definition.passId + ": resolved Texture list is inconsistent");
            for (std::size_t ti = 0; ti < textureNames.size(); ++ti)
            {
                auto* texture = dynamic_cast<VansTexture*>(scene.GetTextureAsset(textureNames[ti]));
                if (!texture || texture->m_TextureType != TEXTURE_2D || !texture->GetImage().GetImageView() || !texture->GetImage().GetSampler())
                    return fail(definition.passId + ": sampled Texture is unavailable: " + textureNames[ti]);
                textureInfos.push_back({texture->GetImage().GetSampler(), texture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                bindings.push_back({definition.textures[ti].binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr});
            }
            if (!descriptors->CreateDesciptorSetLayout(bindings, pass.passLayout) ||
                !descriptors->AllocateDescriptorSet(std::vector<VkDescriptorSetLayout>(frameSlots, pass.passLayout), pass.passSets, VansDescriptorLifetimeRole::PassPersistent)) return fail(definition.passId + ": Pass descriptors allocation failed");
            if (definition.source == Vans::VansRenderPassSource::Scene)
            {
                if (!pass.drawArena.Initialize(device, definition.maxDraws) || !descriptors->CreateDesciptorSetLayout({
                    {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr},
                    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr}}, pass.objectLayout) ||
                    !descriptors->AllocateDescriptorSet(std::vector<VkDescriptorSetLayout>(frameSlots, pass.objectLayout), pass.objectSets, VansDescriptorLifetimeRole::PassPersistent))
                    return fail(definition.passId + ": scene draw descriptors allocation failed");
            }
            pass.slots.resize(frameSlots);
            pass.targetSlots.resize(targetSlots);
            std::vector<VkAttachmentDescription> attachments;
            SubpassParameters subpass{}; subpass.PipelineType = VK_PIPELINE_BIND_POINT_GRAPHICS;
            VkAttachmentReference depth{}; bool hasDepth = false; std::size_t colorIndex = 0;
            for (std::uint32_t index = 0; index < definition.targets.size(); ++index)
            {
                const auto& target = definition.targets[index];
                const bool isDepth = target.format == Vans::VansRenderTargetFormat::D32;
                VkFormatProperties formatProperties{};
                vkGetPhysicalDeviceFormatProperties(backend.GetPhysicalDevice(), NativeFormat(target.format), &formatProperties);
                VkFormatFeatureFlags features = isDepth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
                if (!isDepth)
                {
                    for (const auto* shader : pass.shaders)
                    {
                        const auto& blends = shader->GetPipelineProgramDesc().graphicsState.colorAttachmentBlends;
                        if (!blends.empty() && blends[colorIndex] != "disabled") features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
                    }
                    ++colorIndex;
                }
                if (target.usage & Vans::RenderTargetSampled) features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
                if (target.usage & Vans::RenderTargetTransferSource) features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
                if (target.usage & Vans::RenderTargetTransferDestination) features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
                if ((formatProperties.optimalTilingFeatures & features) != features) return fail(definition.passId + ": image format/usage is not supported by this device");
                VkAttachmentDescription attachment{};
                attachment.format = NativeFormat(target.format); attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = target.load == Vans::VansRenderTargetLoad::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
                attachments.push_back(attachment);
                VkClearValue clear{};
                if (isDepth) { depth = {index, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; hasDepth = true; clear.depthStencil = {target.clear[0], 0}; }
                else { subpass.ColorAttachments.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}); std::copy(target.clear.begin(), target.clear.end(), clear.color.float32); }
                pass.clear.push_back(clear);
            }
            subpass.DepthStencilAttachment = hasDepth ? &depth : nullptr;
            std::vector<SubpassParameters> subpasses{subpass};
            const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            const VkAccessFlags access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            std::vector<VkSubpassDependency> dependencies{
                {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,stages,VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,access,0},
                {0,VK_SUBPASS_EXTERNAL,stages,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,access,VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,0}};
            for (std::size_t variant = 0; variant < 3; ++variant)
            {
                auto selected = attachments;
                if (variant < 2) for (std::size_t i = 0; i < selected.size(); ++i)
                    if (variant == 0 || definition.targets[i].load != Vans::VansRenderTargetLoad::CopyPrevious) selected[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                pass.renderPasses[variant].CreateRenderPass(device, selected, subpasses, dependencies, pass.extent);
                if (!pass.renderPasses[variant].GetRenderPass()) return fail(definition.passId + ": Vulkan RenderPass creation failed");
            }
            for (auto& slot : pass.targetSlots)
            {
                slot.targets.resize(definition.targets.size());
                std::vector<VkImageView> views;
                for (std::size_t i = 0; i < slot.targets.size(); ++i)
                {
                    const auto& target = definition.targets[i];
                    if (!slot.targets[i].CreateVulkanImage(device, {pass.extent.width,pass.extent.height,1}, NativeFormat(target.format), 1, 1,
                        VK_IMAGE_TYPE_2D, NativeUsage(target.usage), VK_SAMPLE_COUNT_1_BIT, false, false,
                        (target.usage & Vans::RenderTargetSampled) != 0, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) return fail(definition.passId + ": target allocation failed");
                    views.push_back(slot.targets[i].GetImageView());
                }
                auto renderPass = pass.renderPasses[2].GetRenderPass();
                slot.framebuffer.CreateFrameBuffer(device, renderPass, views, {pass.extent.width,pass.extent.height,1});
                if (!slot.framebuffer.GetFrameBuffer()) return fail(definition.passId + ": framebuffer allocation failed");
            }
            for (std::uint32_t si = 0; si < frameSlots; ++si)
            {
                auto& slot = pass.slots[si]; slot.inputs.resize(definition.buffers.size());
                if (!HostBuffer(device, slot.parameters, pass.parameterBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) return fail(definition.passId + ": UBO allocation failed");
                std::vector<VkDescriptorBufferInfo> bufferInfos{{slot.parameters.GetNativeBuffer(),0,pass.parameterBytes}};
                for (std::size_t i = 0; i < slot.inputs.size(); ++i)
                {
                    const auto& input = definition.buffers[i]; const auto size = std::uint64_t(input.stride) * input.maxElements;
                    if (!HostBuffer(device, slot.inputs[i], size, input.role == "uniformInput" ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return fail(definition.passId + ": input allocation failed");
                    bufferInfos.push_back({slot.inputs[i].GetNativeBuffer(),0,size});
                }
                std::vector<VkWriteDescriptorSet> writes;
                for (std::size_t i = 0; i < bindings.size(); ++i)
                {
                    VkWriteDescriptorSet write{}; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = pass.passSets[si]; write.dstBinding = bindings[i].binding; write.descriptorCount = 1;
                    write.descriptorType = bindings[i].descriptorType;
                    if (i < bufferInfos.size()) write.pBufferInfo = &bufferInfos[i];
                    else write.pImageInfo = &textureInfos[i - bufferInfos.size()];
                    writes.push_back(write);
                }
                vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
            // 已声明的项目几何在没有实例输入、Pass 禁用时也必须完成 Pipeline 验证。
            if (definition.source == Vans::VansRenderPassSource::ProjectInstances)
            {
                auto* mesh = dynamic_cast<VansMesh*>(scene.GetMeshAsset(configuration->passes[pi].meshName));
                if (!mesh) return fail(definition.passId + ": project instance geometry is unavailable");
                GlobalStateData state;
                state.currentRenderPass = pass.renderPasses[2].GetRenderPass();
                state.viewport = {0,float(pass.extent.height),float(pass.extent.width),-float(pass.extent.height),0,1};
                state.scissor = {{0,0},pass.extent};
                state.vertexInputAttributeDescriptions = &mesh->m_VertexInputAttributeDescriptions;
                state.vertexInputBindingDescriptions = &mesh->m_VertexInputBindingDescriptions;
                for (std::size_t i=0;i<pass.shaders.size();++i)
                {
                    if (!ValidateGeometry(*mesh,pass.signatures[i])) return fail(definition.passId + ": vertex attributes do not match the Shader");
                    auto* pipeline = pass.shaders[i]->GetGraphicsPipeline(device,state,{globalLayout,pass.passLayout});
                    if (!pipeline || !pipeline->GetNativePipeline()) return fail(definition.passId + ": project Pipeline preparation failed");
                }
            }
        }
        return true;
    }
};

VansProjectRenderPassRuntime::VansProjectRenderPassRuntime() = default;
VansProjectRenderPassRuntime::~VansProjectRenderPassRuntime() = default;
bool VansProjectRenderPassRuntime::Prepare(VansVKDevice& backend,VansScene& scene,const VansProjectRenderPassFrameInput& frame,
    std::uint32_t frameSlots,VkExtent2D resolution,std::uint64_t sceneEpoch,std::string& error)
{
    VANS_ASSERT_RENDER_THREAD();
    if (HasRecordedWork()) { error="Cannot replace a project Pass with an unresolved submission"; return false; }
    const auto fail=[&] { PublishSnapshot(error); return false; };
    if (!frame.configuration || frame.passes.size()!=frame.configuration->passes.size() || !frameSlots ||
        frameSlots>VansCommandRecordingConfig::MaximumFramesInFlight || !resolution.width || !resolution.height)
    { error="Project Pass preparation requires a consistent configuration, frame slots and extent"; return fail(); }
    if (frame.configuration->passes.empty()) { Release(backend); error.clear(); return true; }
    auto candidate=std::make_shared<Owner>();
    candidate->device=backend.GetLogicDevice();candidate->configuration=frame.configuration;
    candidate->sceneEpoch=sceneEpoch;candidate->frameSlots=frameSlots;candidate->resolution=resolution;
    candidate->generation=m_Generation+1;
    if (!candidate->Build(backend,scene,frame,error)) return fail();
    for(std::size_t pi=0;pi<candidate->passes.size();++pi)
    {
        auto& pass=candidate->passes[pi];
        const auto& definition=frame.configuration->passes[pi].definition;
        if(definition.source!=Vans::VansRenderPassSource::Scene)continue;
        for(const auto& draw:frame.passes[pi].sceneDraws)
        {
            auto* node=SceneNodeFor(scene,draw);
            std::vector<VkDescriptorSetLayout> layouts;std::vector<VkDescriptorSet> sets;
            if(!node||!node->GetDrawMesh()||draw.shaderIndex>=pass.shaders.size()||
                !ValidateGeometry(*node->GetDrawMesh(),pass.signatures[draw.shaderIndex])||!node->CopyProjectDrawBindings(layouts,sets))
            {error=definition.passId+": candidate scene geometry or bindings are incompatible";return fail();}
            layouts[0]=candidate->globalLayout;layouts[1]=pass.passLayout;layouts[2]=pass.objectLayout;
            GlobalStateData state;
            state.currentRenderPass=pass.renderPasses[2].GetRenderPass();
            state.viewport={0,float(pass.extent.height),float(pass.extent.width),-float(pass.extent.height),0,1};
            state.scissor={{0,0},pass.extent};
            state.vertexInputAttributeDescriptions=&node->GetDrawMesh()->m_VertexInputAttributeDescriptions;
            state.vertexInputBindingDescriptions=&node->GetDrawMesh()->m_VertexInputBindingDescriptions;
            auto* pipeline=pass.shaders[draw.shaderIndex]->GetGraphicsPipeline(candidate->device,state,layouts);
            if(!pipeline||!pipeline->GetNativePipeline())
            {error=definition.passId+": candidate scene Pipeline preparation failed";return fail();}
        }
    }
    if (m_Owner) backend.RetireAfterCurrentFrame([old=std::move(m_Owner)] {});
    m_Owner=std::move(candidate);++m_Generation;PublishSnapshot();error.clear();return true;
}
const std::vector<VansSubmitResourceAccess>& VansProjectRenderPassRuntime::RecordedResources() const { return m_RecordedResources; }
bool VansProjectRenderPassRuntime::BindOutputDescriptors(VkDevice device, VkDescriptorSet globalSet, VkDescriptorImageInfo neutral) const
{
    VANS_ASSERT_RENDER_THREAD();
    if (!device || !globalSet || !neutral.imageView || !neutral.sampler) return false;
    std::array<VkDescriptorImageInfo,Vans::VansProjectRenderTextureSlotCount> images;
    images.fill(neutral);
    if (m_Owner) for (std::size_t pi=0;pi<m_Owner->passes.size();++pi)
    {
        auto& pass=m_Owner->passes[pi];
        if (pass.lastSubmittedSlot==UINT32_MAX) continue;
        const auto& definitions=m_Owner->configuration->passes[pi].definition.targets;
        for (std::size_t ti=0;ti<definitions.size();++ti)
        {
            const auto slot=definitions[ti].exportSlot;
            if (slot==UINT32_MAX) continue;
            if (slot>=images.size()) return false;
            auto& image=pass.targetSlots[pass.lastSubmittedSlot].targets[ti];
            images[slot]={image.GetSampler(),image.GetImageView(),VK_IMAGE_LAYOUT_GENERAL};
        }
    }
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=globalSet;write.dstBinding=GLOBAL_BINDING_PROJECT_RENDER_TEXTURES;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount=static_cast<std::uint32_t>(images.size());write.pImageInfo=images.data();
    vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    return true;
}
std::vector<VansSubmitResourceAccess> VansProjectRenderPassRuntime::ExportedReadResources() const
{
    VANS_ASSERT_RENDER_THREAD();
    std::vector<VansSubmitResourceAccess> result;
    if (m_Owner) for (std::size_t pi=0;pi<m_Owner->passes.size();++pi)
    {
        auto& pass=m_Owner->passes[pi];
        if (pass.lastSubmittedSlot==UINT32_MAX) continue;
        const auto& config=m_Owner->configuration->passes[pi];
        for (std::size_t ti=0;ti<config.definition.targets.size();++ti)
        {
            const auto& definition=config.definition.targets[ti];
            if (definition.exportSlot==UINT32_MAX) continue;
            auto& image=pass.targetSlots[pass.lastSubmittedSlot].targets[ti];
            result.push_back(ImageAccess("project:"+config.guid.ToString()+"/"+definition.name+"/slot"+std::to_string(pass.lastSubmittedSlot),
                image,false,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_ACCESS_SHADER_READ_BIT));
        }
    }
    return result;
}
VansProjectRenderPassRuntimeSnapshot VansProjectRenderPassRuntime::CaptureSnapshot() const
{ std::lock_guard<std::mutex> lock(m_SnapshotMutex); return m_Snapshot; }
void VansProjectRenderPassRuntime::PublishSnapshot(const std::string& error)
{
    VansProjectRenderPassRuntimeSnapshot state;
    state.error = error;
    if (m_Owner)
    {
        state.revision = m_Owner->configuration->revision; state.generation = m_Owner->generation;
        state.allocatedBytes = m_Owner->allocatedBytes; state.passCount = static_cast<std::uint32_t>(m_Owner->passes.size());
        for (std::size_t pi = 0; pi < m_Owner->passes.size(); ++pi)
        {
            const auto& pass = m_Owner->configuration->passes[pi]; const auto& runtime = m_Owner->passes[pi];
            const auto worldRect=runtime.lastSubmittedSlot==UINT32_MAX?std::optional<glm::vec4>{}:
                runtime.targetSlots[runtime.lastSubmittedSlot].worldRect;
            for (const auto& target : pass.definition.targets) state.targets.push_back({"project:" + pass.guid.ToString() + "/" + target.name,
                pass.definition.passId, target.name, target.exportChannel, m_Owner->generation, runtime.extent.width, runtime.extent.height,
                static_cast<std::uint32_t>(runtime.targetSlots.size()), target.format, target.usage, pass.definition.previousFrameHistory, runtime.lastSubmittedSlot != UINT32_MAX,
                runtime.previousSubmittedSlot, runtime.lastSubmittedSlot, worldRect});
            auto diagnostic = runtime.diagnostic;
            for (std::size_t si = 0; si < pass.shaderNames.size(); ++si)
                if (diagnostic.shaderMask & (std::uint64_t(1) << si))
                    diagnostic.shaders.push_back(pass.definition.shaders[si].ToString() + " (" + pass.shaderNames[si] + ")");
            state.draws += diagnostic.instances;
            state.passes.push_back(std::move(diagnostic));
        }
    }
    std::lock_guard<std::mutex> lock(m_SnapshotMutex); m_Snapshot = std::move(state);
}
void VansProjectRenderPassRuntime::Release(VansVKDevice& device)
{
    VANS_ASSERT_RENDER_THREAD();
    if (!m_Owner && m_RecordedResources.empty() && m_RecordedPasses.empty()) return;
    if (m_Owner) device.RetireAfterCurrentFrame([owner = std::move(m_Owner)] {});
    m_RecordedResources.clear(); m_RecordedPasses.clear(); PublishSnapshot();
}
void VansProjectRenderPassRuntime::ShutdownAtIdle()
{
    m_Owner.reset(); m_RecordedResources.clear(); m_RecordedPasses.clear(); PublishSnapshot();
}
bool VansProjectRenderPassRuntime::ValidateShaderCandidate(const std::string& name,
    const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stages, std::string& error) const
{
    if (!m_Owner) return true;
    for (std::size_t i = 0; i < m_Owner->configuration->passes.size(); ++i)
    {
        const auto& config = m_Owner->configuration->passes[i];
        if (std::find(config.shaderNames.begin(), config.shaderNames.end(), name) == config.shaderNames.end()) continue;
        VansProjectPassShaderSignature signature;
        if (!ValidateProjectPassShaderSpirv(config.definition, stages, m_Owner->passes[i].parameterBytes, signature, error)) return false;
    }
    return true;
}
VansVKImage* VansProjectRenderPassRuntime::ResolveTarget(const std::string& key, std::uint64_t generation, std::uint32_t slot, bool requireInitialized)
{
    VANS_ASSERT_RENDER_THREAD();
    if (!m_Owner || m_Owner->generation != generation) return nullptr;
    for (std::size_t pi = 0; pi < m_Owner->passes.size(); ++pi)
    {
        const auto& config = m_Owner->configuration->passes[pi];
        const auto& pass = m_Owner->passes[pi];
        if (slot >= pass.targetSlots.size()) continue;
        for (std::size_t ti = 0; ti < config.definition.targets.size(); ++ti)
            if (key == "project:" + config.guid.ToString() + "/" + config.definition.targets[ti].name &&
                (!requireInitialized || pass.targetSlots[slot].initialized ||
                    (pass.recordedTargetSlot == slot && std::find(m_RecordedPasses.begin(),m_RecordedPasses.end(),pi) != m_RecordedPasses.end())))
                return &m_Owner->passes[pi].targetSlots[slot].targets[ti];
    }
    return nullptr;
}
void VansProjectRenderPassRuntime::ResolveSubmission(bool submitted)
{
    if (submitted && m_Owner) for (auto& pass : m_Owner->passes)
    {
        pass.diagnostic = pass.pendingDiagnostic;
        pass.diagnostic.submitted = pass.pendingDiagnostic.enabled;
    }
    if (submitted && m_Owner) for (const auto i : m_RecordedPasses)
    { auto& pass = m_Owner->passes[i]; pass.targetSlots[pass.recordedTargetSlot].initialized = true;
      pass.targetSlots[pass.recordedTargetSlot].worldTexelOrigin = pass.recordedWorldTexelOrigin;
      pass.targetSlots[pass.recordedTargetSlot].worldRect = pass.recordedWorldRect;
      pass.previousSubmittedSlot = pass.lastSubmittedSlot; pass.lastSubmittedSlot = pass.recordedTargetSlot; }
    if (m_Owner) for (const auto i : m_RecordedPasses)
    {
        auto& pass = m_Owner->passes[i]; auto& target = pass.targetSlots[pass.recordedTargetSlot];
        for (auto& image : target.targets) image.SetTrackedImageLayout(target.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED);
    }
    m_RecordedPasses.clear();
    if (submitted) PublishSnapshot();
}

bool VansProjectRenderPassRuntime::Record(VansVKDevice& backend, VansScene& scene, const VansProjectRenderPassFrameInput& frame,
    VansVKCommandBuffer& command, std::uint32_t slotIndex, std::uint32_t frameSlots,
    VkExtent2D resolution, std::uint64_t sceneEpoch, std::string& error)
{
    VANS_ASSERT_RENDER_THREAD();
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);
    m_RecordedPasses.clear(); m_RecordedResources.clear();
    const auto previousOwner = m_Owner;
    bool replacingOwner = false;
    const auto fail = [&](const std::string& message) {
        ResolveSubmission(false);
        m_RecordedResources.clear();
        if (replacingOwner) m_Owner = previousOwner;
        error = message; PublishSnapshot(message); return false;
    };
    const auto commitOwner = [&] {
        if (!replacingOwner) return;
        if (previousOwner) backend.RetireAfterCurrentFrame([old = previousOwner] {});
        ++m_Generation;
    };
    if (!frame.configuration || frame.configuration->passes.empty()) { Release(backend); error.clear(); return true; }
    VANS_PROFILE_SCOPE("ProjectPass::Record", Vans::ProfileCategory::CommandRecord);
    if (frame.passes.size() != frame.configuration->passes.size() || !frameSlots || frameSlots > VansCommandRecordingConfig::MaximumFramesInFlight || slotIndex >= frameSlots)
        return fail("Project Pass frame snapshot/slot is inconsistent");
    if (!m_Owner || m_Owner->configuration != frame.configuration || m_Owner->sceneEpoch != sceneEpoch || m_Owner->frameSlots != frameSlots ||
        m_Owner->resolution.width != resolution.width || m_Owner->resolution.height != resolution.height)
    {
        auto candidate = std::make_shared<Owner>();
        candidate->device = backend.GetLogicDevice(); candidate->configuration = frame.configuration;
        candidate->sceneEpoch = sceneEpoch; candidate->frameSlots = frameSlots; candidate->resolution = resolution;
        candidate->generation = m_Generation + 1;
        if (!candidate->Build(backend, scene, frame, error)) return fail(error.empty() ? "Project Pass candidate creation failed" : error);
        // 候选资源、几何和 Pipeline 全部验证成功后才发布代际并退役旧 owner。
        m_Owner = std::move(candidate); replacingOwner = true;
    }
    VkDevice device = backend.GetLogicDevice();
    // 项目阶段持有自己的 Set 0，后续内建资源重建不能更新正在执行的 descriptor set。
    std::vector<VkCopyDescriptorSet> copies;
    for (std::size_t pi=0;pi<m_Owner->passes.size();++pi)
    {
        auto& pass=m_Owner->passes[pi];
        for (std::size_t si=0;si<pass.shaders.size();++si)
            if (pass.shaders[si]->GetShaderBinaryHash()!=pass.signatures[si].binaryHash &&
                !ValidateProjectPassShader(m_Owner->configuration->passes[pi].definition,*pass.shaders[si],pass.parameterBytes,pass.signatures[si],error)) return fail(error);
    }
    std::vector<std::uint32_t> activeGlobals;
    for (const auto& pass:m_Owner->passes) for (const auto& signature:pass.signatures)
        for (const auto binding:signature.globalBindings)
            if (std::find(activeGlobals.begin(),activeGlobals.end(),binding)==activeGlobals.end()) activeGlobals.push_back(binding);
    if (!activeGlobals.empty() && !scene.GetGlobalDescriptorSet()) return fail("Project Shader requires unavailable global descriptors");
    for (const auto& binding : VansDescriptorSetLayoutFactory::GlobalBindings(MAX_BINDLESS_TEXTURES))
    {
        if (std::find(activeGlobals.begin(),activeGlobals.end(),binding.binding)==activeGlobals.end()) continue;
        VkCopyDescriptorSet copy{}; copy.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
        copy.srcSet = scene.GetGlobalDescriptorSet(); copy.srcBinding = binding.binding;
        copy.dstSet = m_Owner->globalSets[slotIndex]; copy.dstBinding = binding.binding;
        copy.descriptorCount = binding.descriptorCount; copies.push_back(copy);
    }
    vkUpdateDescriptorSets(device, 0, nullptr, static_cast<std::uint32_t>(copies.size()), copies.data());
    // Validate every packet before recording resource writes. A failed pipeline cannot leave a partially updated target.
    for (std::size_t pi = 0; pi < m_Owner->passes.size(); ++pi)
    {
        auto& pass = m_Owner->passes[pi]; const auto& config = m_Owner->configuration->passes[pi];
        const auto& definition = config.definition; const auto& input = frame.passes[pi];
        pass.pendingDiagnostic = {};
        auto& diagnostic = pass.pendingDiagnostic;
        diagnostic.passId = definition.passId; diagnostic.payloadBytes = pass.diagnostic.payloadBytes;
        diagnostic.enabled = input.enabled; diagnostic.commandSlot = slotIndex; diagnostic.filter = input.filter;
        pass.submission.Clear();
        if (!input.enabled) continue;
        if (!input.worldWindowValid) return fail(definition.passId + ": camera world window exceeds finite texel coordinates");
        pass.recordedWorldTexelOrigin = input.worldTexelOrigin;
        pass.recordedTargetSlot = definition.previousFrameHistory ?
            (pass.lastSubmittedSlot == 0 ? 1u : 0u) : slotIndex;
        diagnostic.targetSlot = pass.recordedTargetSlot;
        if (input.parameters.size() != pass.parameterBytes || input.buffers.size() != definition.buffers.size() || input.sceneDraws.size() > definition.maxDraws)
            return fail(definition.passId + ": input shape or draw count exceeds declaration");
        pass.recordedWorldRect.reset();
        if (!definition.cameraWorldRect.empty())
        {
            const auto parameter=std::find_if(definition.parameters.begin(),definition.parameters.end(),
                [&](const auto& p){return p.name==definition.cameraWorldRect;});
            glm::vec4 rectangle;
            std::memcpy(&rectangle,input.parameters.data()+parameter->offset,sizeof(rectangle));
            pass.recordedWorldRect=rectangle;
        }
        auto& slot = pass.slots[slotIndex];
        {
            VANS_PROFILE_SCOPE("ProjectPass::UploadInputs", Vans::ProfileCategory::RenderPrepare);
            if (!slot.parameters.SetBufferData(input.parameters.data(), 0, input.parameters.size())) return fail(definition.passId + ": parameter upload failed");
            diagnostic.uploadedBytes = input.parameters.size();
            for (std::size_t bi = 0; bi < slot.inputs.size(); ++bi)
            {
                const auto& declared = definition.buffers[bi]; const auto& bytes = input.buffers[bi];
                if (bytes.size() > slot.inputs[bi].GetBufferSize() || bytes.size() % declared.stride != 0) return fail(definition.passId + ": Buffer size/stride mismatch");
                std::memset(slot.inputs[bi].GetMappedPtr(), 0, static_cast<std::size_t>(slot.inputs[bi].GetBufferSize()));
                if (!bytes.empty()) std::memcpy(slot.inputs[bi].GetMappedPtr(), bytes.data(), bytes.size());
                slot.inputs[bi].FlushMappedRange(0, slot.inputs[bi].GetBufferSize());
                diagnostic.uploadedBytes += slot.inputs[bi].GetBufferSize();
            }
        }
        GlobalStateData state;
        state.currentRenderPass = pass.renderPasses[2].GetRenderPass();
        state.viewport = {0,float(pass.extent.height),float(pass.extent.width),-float(pass.extent.height),0,1};
        state.scissor = {{0,0},pass.extent};
        if (definition.source == Vans::VansRenderPassSource::Scene)
        {
            pass.drawArena.BeginFrame(slotIndex);
            const auto& transforms = scene.GetInstanceTransformDataBuffer(slotIndex);
            const std::array<VkDescriptorBufferInfo, 2> infos{{{transforms.GetNativeBuffer(),0,transforms.GetBufferSize()},
                {pass.drawArena.GetBuffer().GetNativeBuffer(),0,pass.drawArena.GetBuffer().GetBufferSize()}}};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (std::uint32_t i = 0; i < 2; ++i) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = pass.objectSets[slotIndex];
                writes[i].dstBinding = i; writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &infos[i]; }
            vkUpdateDescriptorSets(device, 2, writes.data(), 0, nullptr);
            for (const auto& draw : input.sceneDraws)
            {
                const auto candidateId = definition.passId + ": proxy " + std::to_string(MakeRenderProxyStableId(draw.proxy));
                auto* node = SceneNodeFor(scene,draw);
                if (!node || !backend.IsCurrentRenderProxyEnabled(draw.proxy) || !node->GetDrawMesh() || node->m_HasSkeletonBone || node->m_AnimationEnabled ||
                    draw.shaderIndex >= pass.shaders.size() || !ValidateGeometry(*node->GetDrawMesh(), pass.signatures[draw.shaderIndex])) return fail(candidateId + ": stale scene candidate or incompatible static geometry");
                std::vector<VkDescriptorSetLayout> layouts; std::vector<VkDescriptorSet> sets;
                if (!node->CopyProjectDrawBindings(layouts, sets)) return fail(candidateId + ": scene descriptor bindings unavailable");
                if (definition.cameraVisibleOnly && !backend.ShouldDrawProjectPassProxy(draw.proxy)) { ++diagnostic.filter.cameraCulled; continue; }
                layouts[0] = m_Owner->globalLayout; sets[0] = m_Owner->globalSets[slotIndex];
                layouts[1] = pass.passLayout; sets[1] = pass.passSets[slotIndex];
                layouts[2] = pass.objectLayout; sets[2] = pass.objectSets[slotIndex];
                VansDrawPacket packet;
                if (!VansDrawSubmission::BuildPacket(device, *node, *pass.shaders[draw.shaderIndex], layouts, sets, state, 0, 0,
                    pass.submission.packets.size(), 0, packet)) return fail(candidateId + ": DrawPacket build failed for Shader " + definition.shaders[draw.shaderIndex].ToString());
                pass.submission.packets.push_back(std::move(packet));
                diagnostic.shaderMask |= std::uint64_t(1) << draw.shaderIndex;
            }
            if (!VansDrawSubmission::Finalize(pass.drawArena, VansDrawSortPolicy::State, pass.submission)) return fail(definition.passId + ": draw arena capacity/upload failed");
            diagnostic.uploadedBytes += pass.submission.instanceData.size() * sizeof(VansDrawInstanceDataGPU);
        }
        else if (input.instanceCount)
        {
            auto* mesh = dynamic_cast<VansMesh*>(scene.GetMeshAsset(config.meshName));
            const auto buffer = std::find_if(definition.buffers.begin(), definition.buffers.end(), [](const auto& b) { return b.role == "instanceInput"; });
            if (buffer == definition.buffers.end() || input.instanceCount > buffer->maxElements ||
                input.buffers[buffer - definition.buffers.begin()].size() != std::uint64_t(input.instanceCount) * sizeof(VansProjectDrawInstance) ||
                !mesh || !ValidateGeometry(*mesh, pass.signatures[0])) return fail(definition.passId + ": project instance geometry/stream is incompatible");
            VansDrawPacket packet;
            if (!VansDrawSubmission::BuildGeometryPacket(device,*mesh,*pass.shaders[0],
                {m_Owner->globalLayout,pass.passLayout}, {m_Owner->globalSets[slotIndex],pass.passSets[slotIndex]}, state,packet))
                return fail(definition.passId + ": project instance pipeline failed");
            pass.submission.batches.push_back({packet.pipeline,packet.pipelineLayout,packet.descriptorSets,packet.geometry,0,input.instanceCount});
            pass.submission.stats.instanceCount = input.instanceCount; pass.submission.stats.batchCount = 1;
            diagnostic.shaderMask = 1;
        }
        diagnostic.batches = pass.submission.stats.batchCount; diagnostic.instances = pass.submission.stats.instanceCount;
        m_RecordedPasses.push_back(pi);
    }
    if (m_RecordedPasses.empty())
    {
        for (auto& pass : m_Owner->passes) pass.diagnostic = pass.pendingDiagnostic;
        commitOwner(); PublishSnapshot(); error.clear(); return true;
    }
    if (!command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return fail("Project prelude command buffer begin failed");
    // Share the existing frame query pool; BeginQueue is idempotent across this
    // prelude and the later built-in graphics command buffers.
#if VANS_PROFILER_ENABLED
    Vans::VansGpuProfiler::Get().BeginQueue(command.GetVKCommandBuffer(), Vans::VansGpuQueueLane::Graphics);
#endif
    {
        VANS_GPU_SCOPE(command.GetVKCommandBuffer(), "BeforeBuiltInRender");
        VkMemoryBarrier host{}; host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER; host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command.GetVKCommandBuffer(),VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,0,1,&host,0,nullptr,0,nullptr);
        for (const auto pi : m_RecordedPasses)
        {
            auto& pass = m_Owner->passes[pi]; auto& slot = pass.targetSlots[pass.recordedTargetSlot]; const auto& config = m_Owner->configuration->passes[pi];
            const auto scopeName = "Project Pass: " + config.definition.passId;
            VANS_GPU_SCOPE(command.GetVKCommandBuffer(), scopeName.c_str());
            const bool hasHistory = pass.lastSubmittedSlot != UINT32_MAX;
            const auto transition = [&](VansVKImage& image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
                Transition(command, image, from, to, src, dst); ++pass.pendingDiagnostic.imageBarriers;
            };
            for (std::size_t ti = 0; ti < slot.targets.size(); ++ti)
            {
                auto& target = slot.targets[ti]; const auto& declaration = config.definition.targets[ti];
                const auto key = "project:" + config.guid.ToString() + "/" + declaration.name;
                transition(target,slot.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
                    slot.initialized ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0,VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT);
                if (declaration.load == Vans::VansRenderTargetLoad::CopyPrevious && hasHistory && pass.lastSubmittedSlot != pass.recordedTargetSlot)
                {
                    auto& previous = pass.targetSlots[pass.lastSubmittedSlot].targets[ti];
                    transition(previous,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                    transition(target,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                    VkImageCopy copy{}; copy.srcSubresource = {previous.GetImageAspect(),0,0,1}; copy.dstSubresource = {target.GetImageAspect(),0,0,1};
                    copy.extent = {pass.extent.width,pass.extent.height,1};
                    bool overlap = true;
                    if (!config.definition.cameraWorldRect.empty())
                    {
                        const auto& oldOrigin = pass.targetSlots[pass.lastSubmittedSlot].worldTexelOrigin;
                        const auto dx = pass.recordedWorldTexelOrigin[0] - oldOrigin[0];
                        const auto dz = pass.recordedWorldTexelOrigin[1] - oldOrigin[1];
                        // 只做整数 texel 拷贝，旧脚印对应的世界格点不变；无滤波、wrap 或重采样。
                        overlap = std::abs(dx) < pass.extent.width && std::abs(dz) < pass.extent.height;
                        if (dx || dz)
                        {
                            VkClearColorValue clear{};
                            std::copy(declaration.clear.begin(), declaration.clear.end(), clear.float32);
                            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                            vkCmdClearColorImage(command.GetVKCommandBuffer(), target.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
                            if (overlap) transition(target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                        }
                        if (overlap)
                        {
                            copy.srcOffset = {static_cast<std::int32_t>(std::max<std::int64_t>(dx,0)),static_cast<std::int32_t>(std::max<std::int64_t>(dz,0)),0};
                            copy.dstOffset = {static_cast<std::int32_t>(std::max<std::int64_t>(-dx,0)),static_cast<std::int32_t>(std::max<std::int64_t>(-dz,0)),0};
                            copy.extent = {pass.extent.width-static_cast<std::uint32_t>(std::abs(dx)),pass.extent.height-static_cast<std::uint32_t>(std::abs(dz)),1};
                        }
                    }
                    if (overlap) vkCmdCopyImage(command.GetVKCommandBuffer(),previous.GetImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target.GetImage(),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
                    transition(previous,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT);
                    transition(target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
                    m_RecordedResources.push_back(ImageAccess(key + "/slot" + std::to_string(pass.lastSubmittedSlot),previous,false,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT));
                }
                target.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
                m_RecordedResources.push_back(ImageAccess(key + "/slot" + std::to_string(pass.recordedTargetSlot),target,true,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT));
            }
            VkRenderPassBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = pass.renderPasses[slot.initialized ? 2 : hasHistory ? 1 : 0].GetRenderPass();
            begin.framebuffer = slot.framebuffer.GetFrameBuffer(); begin.renderArea = {{0,0},pass.extent};
            begin.clearValueCount = static_cast<std::uint32_t>(pass.clear.size()); begin.pClearValues = pass.clear.data();
            command.BeginRenderPass(begin);
            command.SetViewport(0,{{0,float(pass.extent.height),float(pass.extent.width),-float(pass.extent.height),0,1}});
            command.SetScissor(0,{{{0,0},pass.extent}});
            VansDrawSubmission::Record(command,pass.submission,0,pass.submission.batches.size());
            command.EndRenderPass();
            const auto recordBuffer = [&](VansVKBuffer& buffer,const std::string& name) {
                VansSubmitResourceAccess access; access.name = name; access.stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
                access.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT; access.persistent = true;
                access.physical = VansPhysicalResourceIdentity::ForBuffer(buffer.GetNativeBuffer()); m_RecordedResources.push_back(std::move(access));
            };
            auto& inputs = pass.slots[slotIndex];
            recordBuffer(inputs.parameters,config.definition.passId + "/parameters");
            for (std::size_t bi = 0; bi < inputs.inputs.size(); ++bi) recordBuffer(inputs.inputs[bi],config.definition.passId + "/" + config.definition.buffers[bi].name);
        }
        // 此 barrier 的第二同步范围覆盖同一 graphics queue 后续提交，包括准备阶段的维护命令。
        VkMemoryBarrier prerequisite{}; prerequisite.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        prerequisite.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        prerequisite.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(command.GetVKCommandBuffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &prerequisite, 0, nullptr, 0, nullptr);
    }
    if (!command.EndCommandBufferRecord()) return fail("Project prelude command buffer end failed");
    commitOwner(); PublishSnapshot(); error.clear(); return true;
}
}
