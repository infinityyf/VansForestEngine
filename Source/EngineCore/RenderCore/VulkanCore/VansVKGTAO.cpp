#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansDescriptorSetLayouts.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "VansShader.h"
#include "VansTexture.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../../Util/VansProfiler.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace VansGraphics
{
    void VansVKDevice::PrepareGTAORenderData()
    {
        auto& manager = *m_Scene->GetMaterialManager();
        if (!GetDeviceFeatures().shaderStorageImageExtendedFormats)
            throw std::runtime_error("GTAO requires storage image extended formats (RG16F)");
        auto create = [&](const char* name, VkFormat format, uint32_t mipLimit)
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(GetPhysicalDevice(), format, &properties);
            const auto required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if ((properties.optimalTilingFeatures & required) != required)
                throw std::runtime_error(std::string("Unsupported GTAO storage/sampled format: ") + name);
            auto texture = std::make_unique<VansTexture>();
            if (!texture->InitTextureWithoutData(m_VansVKCommandBuffer,
                m_RenderWidth, m_RenderHeight, 1, format, false, mipLimit > 1, true,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mipLimit))
                throw std::runtime_error(std::string("GTAO texture allocation failed: ") + name);
            const uint32_t levels = texture->GetImage().GetImageCreateInfo().mipLevels;
            manager.RegisterRuntimeRenderTexture(name, texture.release());
            return levels;
        };
        manager.m_GTAOParameters = VansGTAOParameters{};
        manager.m_GTAOParameters.mipCount = static_cast<int32_t>(create(
            VansMaterialManager::RT_GTAO_DEPTH, VK_FORMAT_R32_SFLOAT, VansGTAOParameters::MaximumDepthMipLevels));
        create(VansMaterialManager::RT_GTAO_RAW, VK_FORMAT_R16G16_SFLOAT, 1);
        create(VansMaterialManager::RT_GTAO_RESULT, VK_FORMAT_R16G16_SFLOAT, 1);
        // Packed four 2-bit connections. FP32 stores this integer exactly and uses
        // the framework's float sampler without integer/linear-filter conflicts.
        create(VansMaterialManager::RT_GTAO_EDGES, VK_FORMAT_R32_SFLOAT, 1);
        VansDescriptorSetLayoutFactory::CreateAndAllocate_GTAODepth(
            manager.m_GTAODepthSetLayout, manager.m_GTAODepthDescriptorSets, manager.m_GTAOParameters.mipCount);
        VansDescriptorSetLayoutFactory::CreateAndAllocate_GTAOMain(
            manager.m_GTAOMainSetLayout, manager.m_GTAOMainDescriptorSets);
        VansDescriptorSetLayoutFactory::CreateAndAllocate_GTAODenoise(
            manager.m_GTAODenoiseSetLayout, manager.m_GTAODenoiseDescriptorSets);
        auto& shaders = VansShaderManager::Get();
        manager.m_GTAODepthShader = shaders.FindComputeShader("GTAODepth");
        manager.m_GTAOMainShader = shaders.FindComputeShader("GTAOMain");
        manager.m_GTAODenoiseShader = shaders.FindComputeShader("GTAODenoise");
        if (!manager.m_GTAODepthShader || !manager.m_GTAOMainShader || !manager.m_GTAODenoiseShader)
            throw std::runtime_error("Required GTAO compute shaders are missing");
    }

    void VansVKDevice::UpdateGTAODescriptorSets(VansRenderPassManager* renderPassManager)
    {
        if (IsFeatureDescriptorCurrent(m_GTAODescSetGeneration)) return;
        auto& manager = *m_Scene->GetMaterialManager();
        auto& depth = manager.GetRuntimeRenderTexture(VansMaterialManager::RT_GTAO_DEPTH)->GetImage();
        auto& raw = manager.GetRuntimeRenderTexture(VansMaterialManager::RT_GTAO_RAW)->GetImage();
        auto& edges = manager.GetRuntimeRenderTexture(VansMaterialManager::RT_GTAO_EDGES)->GetImage();
        auto& result = manager.GetRuntimeRenderTexture(VansMaterialManager::RT_GTAO_RESULT)->GetImage();
        auto& normal = renderPassManager->GetNormal();
        auto& material = renderPassManager->GetGbuffer1();
        auto& position = renderPassManager->GetGbuffer2();
        auto* descriptors = VansVKDescriptorManager::GetInstance();
        descriptors->BeginDescriptorUpdate();
        auto sampled = [&](VkDescriptorSet set, uint32_t binding, VansVKImage& image, VkImageLayout layout)
        {
            descriptors->WriteImageDescriptor(set, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ image.GetSampler(), image.GetImageView(), layout }});
        };
        auto storage = [&](VkDescriptorSet set, uint32_t binding, VansVKImage& image, VkImageView view)
        {
            descriptors->WriteImageDescriptor(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                {{ VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL }});
        };
        for (uint32_t mip = 0; mip < manager.m_GTAODepthDescriptorSets.size(); ++mip)
        {
            const auto set = manager.m_GTAODepthDescriptorSets[mip];
            sampled(set, GTAO_DEPTH_POSITION, position, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            const auto sourceView = manager.m_GTAOParameters.mipCount == 1
                ? depth.GetImageView() : depth.GetImageMipView(mip > 0 ? mip - 1 : 0);
            descriptors->WriteImageDescriptor(set, GTAO_DEPTH_SOURCE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ depth.GetSampler(), sourceView, VK_IMAGE_LAYOUT_GENERAL }});
            storage(set, GTAO_DEPTH_RESULT, depth,
                manager.m_GTAOParameters.mipCount == 1 ? depth.GetImageView() : depth.GetImageMipView(mip));
        }
        const auto mainSet = manager.m_GTAOMainDescriptorSets[0];
        sampled(mainSet, GTAO_MAIN_NORMAL, normal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(mainSet, GTAO_MAIN_MATERIAL, material, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(mainSet, GTAO_MAIN_POSITION, position, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(mainSet, GTAO_MAIN_DEPTH, depth, VK_IMAGE_LAYOUT_GENERAL);
        storage(mainSet, GTAO_MAIN_RAW, raw, raw.GetImageView());
        storage(mainSet, GTAO_MAIN_EDGES, edges, edges.GetImageView());
        const auto denoiseSet = manager.m_GTAODenoiseDescriptorSets[0];
        sampled(denoiseSet, GTAO_DENOISE_RAW, raw, VK_IMAGE_LAYOUT_GENERAL);
        sampled(denoiseSet, GTAO_DENOISE_EDGES, edges, VK_IMAGE_LAYOUT_GENERAL);
        sampled(denoiseSet, GTAO_DENOISE_NORMAL, normal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(denoiseSet, GTAO_DENOISE_MATERIAL, material, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(denoiseSet, GTAO_DENOISE_POSITION, position, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        storage(denoiseSet, GTAO_DENOISE_RESULT, result, result.GetImageView());
        descriptors->CommitDescriptorUpdates();
        MarkFeatureDescriptorCurrent(m_GTAODescSetGeneration);
    }

    void VansVKDevice::UpdateGTAODepth(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
    {
        VANS_GPU_SCOPE(commandBuffer.GetVKCommandBuffer(), "GTAO.Depth");
        UpdateGTAODescriptorSets(renderPassManager);
        auto& manager = *m_Scene->GetMaterialManager();
        VkMemoryBarrier inputBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        inputBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        inputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { inputBarrier });
        commandBuffer.EnsureComputeShader(*manager.m_GTAODepthShader,
            { m_Scene->GetGlobalDescriptorSetLayout(), manager.m_GTAODepthSetLayout });
        for (uint32_t mip = 0; mip < manager.m_GTAODepthDescriptorSets.size(); ++mip)
        {
            auto parameters = manager.m_GTAOParameters;
            parameters.sourceMip = static_cast<int32_t>(mip) - 1;
            manager.m_GTAODepthShader->SetPushConstantData(&parameters);
            const uint32_t width = (std::max)(1u, m_RenderWidth >> mip);
            const uint32_t height = (std::max)(1u, m_RenderHeight >> mip);
            commandBuffer.DispatchCompute(*manager.m_GTAODepthShader, (width + 7) / 8, (height + 7) / 8, 1,
                { m_Scene->GetGlobalDescriptorSet(), manager.m_GTAODepthDescriptorSets[mip] });
            VkMemoryBarrier mipBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { mipBarrier });
        }
    }

    void VansVKDevice::UpdateGTAOMain(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
    {
        VANS_GPU_SCOPE(commandBuffer.GetVKCommandBuffer(), "GTAO.Main");
        UpdateGTAODescriptorSets(renderPassManager);
        auto& manager = *m_Scene->GetMaterialManager();
        manager.m_GTAOMainShader->SetPushConstantData(&manager.m_GTAOParameters);
        commandBuffer.EnsureComputeShader(*manager.m_GTAOMainShader,
            { m_Scene->GetGlobalDescriptorSetLayout(), manager.m_GTAOMainSetLayout });
        commandBuffer.DispatchCompute(*manager.m_GTAOMainShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1,
            { m_Scene->GetGlobalDescriptorSet(), manager.m_GTAOMainDescriptorSets[0] });
        VkMemoryBarrier rawBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        rawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rawBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { rawBarrier });
    }

    void VansVKDevice::DenoiseGTAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
    {
        UpdateGTAODescriptorSets(renderPassManager);
        auto& manager = *m_Scene->GetMaterialManager();
        commandBuffer.EnsureComputeShader(*manager.m_GTAODenoiseShader,
            { m_Scene->GetGlobalDescriptorSetLayout(), manager.m_GTAODenoiseSetLayout });
        commandBuffer.DispatchCompute(*manager.m_GTAODenoiseShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1,
            { m_Scene->GetGlobalDescriptorSet(), manager.m_GTAODenoiseDescriptorSets[0] });
        VkMemoryBarrier outputBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        outputBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        outputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        // A compute-only queue cannot use FRAGMENT_SHADER in a barrier. Its
        // semaphore provides the compute -> Deferred visibility dependency.
        const bool graphicsQueue = &commandBuffer != &m_VansVKGraphicsScreenCommandBuffer;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            graphicsQueue ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            { outputBarrier });
    }
}
