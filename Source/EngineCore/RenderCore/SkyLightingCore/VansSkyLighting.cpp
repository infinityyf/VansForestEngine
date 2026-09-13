#include "VansSkyLighting.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VansShaderManager.h"
#include "../../Util/VansLog.h"
#include <array>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace VansGraphics
{
    void VansSkyLighting::Initialize(VansVKDevice& device, VansVKCommandBuffer& commandBuffer, const std::string& sourceDirectory)
    {
		if (m_Radiance) throw std::logic_error("Sky lighting initialized twice");
        VkDevice logicalDevice = device.GetLogicDevice();
        m_Radiance = std::make_unique<VansTexture>();
        m_SourceHash = m_Radiance->LoadCubeTexture(commandBuffer, sourceDirectory);
        // 发布包只含 cooked shader，使用实际加载的二进制哈希，避免运行时依赖源码路径。
        for (const char* name : {"PreConDiffuseEnvironment", "PreConSpecularEnvironment", "ReflectionProbeCapture", "ReflectionProbeCaptureSky"})
        {
            const auto* shader = VansShaderManager::Get().FindShader(name);
            if (!shader || !shader->GetShaderBinaryHash()) throw std::runtime_error(std::string("Sky shader missing: ") + name);
            const uint64_t hash = shader->GetShaderBinaryHash();
            for (unsigned shift = 0; shift < 64; shift += 8)
            { m_SourceHash ^= static_cast<uint8_t>(hash >> shift); m_SourceHash *= 1099511628211ull; }
        }
		VansTexture* environmentRadiance = m_Radiance.get();
		m_Diffuse = std::make_unique<VansTexture>();
		if (!m_Diffuse->InitTextureWithoutData(
			commandBuffer, 512, 512, 1,
			VK_FORMAT_R32G32B32A32_SFLOAT, true, false, true)) throw std::runtime_error("Sky irradiance allocation failed");
		m_Specular = std::make_unique<VansTexture>();
		if (!m_Specular->InitTextureWithoutData(
			commandBuffer, 512, 512, 1,
			VK_FORMAT_R32G32B32A32_SFLOAT, true, true, true)) throw std::runtime_error("Sky specular allocation failed");
		// 静态天空源在初始化时生成未缩放的辐射、辐照度、镜面和 SH 表示。
		// 消费端只选择所需表示；强度按帧上传，标量修改不重做卷积。
		constexpr std::uint32_t environmentResolution = 512u;
		constexpr std::uint32_t maxShaderMipCount = 12u;
		const std::uint32_t mipCount =
			m_Specular->GetImage().GetImageCreateInfo().mipLevels;
		const std::array<std::uint32_t, 4> filterParameters = {
			environmentResolution, mipCount, 64u, 128u
		};
		const std::array<float, 27> zeroSH{};

        // 初始化暂存资源随作用域清理；任一步失败都不泄漏 descriptor 或参数缓冲。
        struct FilterResources
        {
            VkDevice device;
            VansVKBuffer parameters;
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            std::vector<VkDescriptorSet> sets;
            ~FilterResources()
            {
                auto* descriptors = VansVKDescriptorManager::GetInstance();
                if (!sets.empty()) descriptors->DestroyDescriptorSet(sets);
                if (layout != VK_NULL_HANDLE) descriptors->ReleaseDescriptorSetLayout(layout);
                parameters.DestroyVulkanBuffer(device);
            }
        } filter{logicalDevice};
		if (!filter.parameters.CreatVulkanBuffer(
			logicalDevice,
			sizeof(filterParameters),
			VK_FORMAT_R32_UINT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			VANS_LOG_ERROR("[VansVKDevice] IBL filter parameter buffer creation failed.");
			throw std::runtime_error("Sky lighting resource initialization failed");
		}
		filter.parameters.SetBufferData(
			filterParameters.data(), 0, sizeof(filterParameters));

		if (!m_SH.CreatVulkanBuffer(
			logicalDevice,
			sizeof(zeroSH),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			VANS_LOG_ERROR("[VansVKDevice] IBL SH buffer creation failed.");
			throw std::runtime_error("Sky lighting resource initialization failed");
		}
		m_SH.SetBufferData(
			zeroSH.data(), 0, sizeof(zeroSH));

		VansComputeShader* diffuseShader =
			VansShaderManager::Get().FindComputeShader("PreConDiffuseEnvironment");
		VansComputeShader* specularShader =
			VansShaderManager::Get().FindComputeShader("PreConSpecularEnvironment");
		if (!diffuseShader || !specularShader || mipCount == 0u ||
			mipCount > maxShaderMipCount)
		{
			VANS_LOG_ERROR("[VansVKDevice] IBL filter shaders or mip contract are invalid.");
			throw std::runtime_error("Sky lighting resource initialization failed");
		}

		if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
			{
				{ PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
					VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ PassBinding::UAV_IMAGE_0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
					VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ PassBinding::UAV_IMAGE_1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					maxShaderMipCount, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ PassBinding::CBUFFER_3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
					VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ PassBinding::BUFFER_4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
					VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
			},
			filter.layout,
			filter.sets) || filter.sets.empty())
		{
			VANS_LOG_ERROR("[VansVKDevice] IBL filter descriptor allocation failed.");
			throw std::runtime_error("Sky lighting resource initialization failed");
		}

		std::vector<VkDescriptorImageInfo> specularMipDescriptors;
		specularMipDescriptors.reserve(maxShaderMipCount);
		for (std::uint32_t mip = 0; mip < maxShaderMipCount; ++mip)
		{
			const std::uint32_t imageMip = (std::min)(mip, mipCount - 1u);
			specularMipDescriptors.push_back({
				m_Specular->GetImage().GetSampler(),
				m_Specular->GetImage().GetImageMipView(
					static_cast<int>(imageMip)),
				VK_IMAGE_LAYOUT_GENERAL
			});
		}

		auto* descriptors = VansVKDescriptorManager::GetInstance();
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(
			filter.sets[0], PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ environmentRadiance->GetImage().GetSampler(),
			   environmentRadiance->GetImage().GetImageView(),
			   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		descriptors->WriteImageDescriptor(
			filter.sets[0], PassBinding::UAV_IMAGE_0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_Diffuse->GetImage().GetSampler(),
			   m_Diffuse->GetImage().GetImageView(),
			   VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(
			filter.sets[0], PassBinding::UAV_IMAGE_1,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			specularMipDescriptors);
		descriptors->WriteBufferDescriptor(
			filter.sets[0], PassBinding::CBUFFER_3,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ filter.parameters.GetNativeBuffer(), 0,
			   filter.parameters.GetBufferSize() }});
		descriptors->WriteBufferDescriptor(
			filter.sets[0], PassBinding::BUFFER_4,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_SH.GetNativeBuffer(), 0,
			   m_SH.GetBufferSize() }});
		descriptors->CommitDescriptorUpdates();

		const std::uint32_t workgroups = (environmentResolution + 7u) / 8u;
		bool submitted = commandBuffer.BeginCommandBufferRecord(
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		if (submitted)
		{
			commandBuffer.EnsureComputeShader(*diffuseShader, { filter.layout });
			commandBuffer.DispatchCompute(
				*diffuseShader, workgroups, workgroups, 6u, filter.sets);
			commandBuffer.EnsureComputeShader(*specularShader, { filter.layout });
			commandBuffer.DispatchCompute(
				*specularShader, workgroups, workgroups, 6u * mipCount, filter.sets);

			auto transitionForSampling = [&](VansTexture* texture)
			{
				texture->GetImage().SetImageMemoryBarrier(
					commandBuffer,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					{
						texture->GetImage().GetImage(),
						VK_ACCESS_SHADER_WRITE_BIT,
						VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						texture->GetImage().GetImageAspect()
					});
			};
			transitionForSampling(m_Diffuse.get());
			transitionForSampling(m_Specular.get());

			VkBufferMemoryBarrier shBarrier{};
			shBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			shBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			shBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			shBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			shBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			shBarrier.buffer = m_SH.GetNativeBuffer();
			shBarrier.offset = 0;
			shBarrier.size = m_SH.GetBufferSize();
			commandBuffer.PipelineBarrier(
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				{}, { shBarrier }, {});

			submitted = commandBuffer.EndCommandBufferRecord() &&
				VansVKCommandBuffer::SubmitCommands(
					device.GetGraphicsQueue(),
					logicalDevice,
					{ commandBuffer.GetVKCommandBuffer() },
					{}, {}, commandBuffer.m_CommandBufferFinishSubmitFence) &&
				commandBuffer.ResetCommandBuffer(false);
		}
		if (!submitted)
			VANS_LOG_ERROR("[VansVKDevice] One-time IBL convolution submit failed.");

		device.WaitForDevice();

        if (!submitted) throw std::runtime_error("Sky lighting convolution failed");
        if (!m_Parameters.CreatVulkanBuffer(logicalDevice, sizeof(glm::vec4), VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            throw std::runtime_error("Sky lighting parameter allocation failed");
        if (!UploadFrame({})) throw std::runtime_error("Sky lighting parameter upload failed");
        VANS_LOG("[SkyLighting] Static HDR source ready: format=" << m_Radiance->GetImage().GetImageCreateInfo().format
            << " resolution=" << m_Radiance->GetImage().GetImageDimension().width
            << " sourceKey=" << CaptureSourceKey(1.0f));

    }
    std::string VansSkyLighting::CaptureSourceKey(float authoredIntensity) const
    {
        uint32_t intensityBits = 0; std::memcpy(&intensityBits, &authoredIntensity, sizeof(intensityBits));
        std::ostringstream key; key << std::hex << m_SourceHash << '-' << intensityBits;
        return key.str();
    }
    bool VansSkyLighting::UploadFrame(const VansSkyLightingFrame& frame)
    {
        const glm::vec4 data(frame.radianceScale, 0.0f, 0.0f, 0.0f);
        return m_Parameters.GetNativeBuffer() != VK_NULL_HANDLE && m_Parameters.SetBufferData(&data, 0, sizeof(data));
    }
    void VansSkyLighting::Destroy(VkDevice device)
    {
        m_Radiance.reset(); m_Diffuse.reset(); m_Specular.reset();
        m_SH.DestroyVulkanBuffer(device); m_Parameters.DestroyVulkanBuffer(device);
    }
}
