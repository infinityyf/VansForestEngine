#include "VansRainRenderSystem.h"

#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"

namespace VansGraphics
{
VansRainRenderSystem::~VansRainRenderSystem()
{
	Shutdown();
}

bool VansRainRenderSystem::Initialize(VansScene& scene)
{
	Shutdown();
	m_Scene = &scene;
	m_StreakShader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset("RainStreak"));
	m_SplashShader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset("RainSplash"));
	m_SurfaceRippleShader = VansShaderManager::Get().FindComputeShader("SurfaceWeatherRipple");
	if (!m_StreakShader || !m_SplashShader || !m_SurfaceRippleShader)
	{
		VANS_LOG_ERROR("[Rain] Precipitation shaders are unavailable");
		Shutdown();
		return false;
	}
	auto* device = scene.GetRuntimeResourceDevice();
	if (device == nullptr || !m_SurfaceRippleImage.CreateVulkanImage(
		device->GetLogicDevice(), { SurfaceRippleResolution, SurfaceRippleResolution, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_SAMPLE_COUNT_1_BIT, false, false, true, VK_SAMPLER_ADDRESS_MODE_REPEAT))
	{
		VANS_LOG_ERROR("[Rain] Failed to create shared surface-ripple normal texture");
		Shutdown();
		return false;
	}
	if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom({
		{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
	}, m_SurfaceRippleLayout, m_SurfaceRippleSets) || m_SurfaceRippleSets.empty())
	{
		VANS_LOG_ERROR("[Rain] Failed to allocate surface-ripple descriptor set");
		Shutdown();
		return false;
	}

	const VkShaderStageFlags positionStages =
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	const std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, positionStages, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
	};
	if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
		bindings, m_PassLayout, m_PassSets) || m_PassSets.empty())
	{
		VANS_LOG_ERROR("[Rain] Failed to allocate precipitation descriptor set");
		Shutdown();
		return false;
	}
	m_Layouts = { scene.GetGlobalDescriptorSetLayout(), m_PassLayout };
	m_Sets = { scene.GetGlobalDescriptorSet(), m_PassSets.front() };
	if (!RebindSceneTextures())
	{
		Shutdown();
		return false;
	}
	return true;
}

bool VansRainRenderSystem::RebindSceneTextures()
{
	if (!m_Scene || m_PassSets.empty())
		return false;
	auto* descriptorManager = VansVKDescriptorManager::GetInstance();
	auto* renderPassManager = VansRenderPassManager::GetInstance();
	if (!descriptorManager || !renderPassManager)
		return false;
	auto* rainLayer = static_cast<VansTexture*>(m_Scene->GetTextureAsset("rainLayerTexture"));
	auto* splashAtlas = static_cast<VansTexture*>(m_Scene->GetTextureAsset("rainSplashAtlas"));
	if (rainLayer == nullptr || splashAtlas == nullptr)
	{
		VANS_LOG_ERROR("[Rain] Built-in rain layer or splash atlas texture is unavailable");
		return false;
	}
	auto* rippleSeed = static_cast<VansTexture*>(m_Scene->GetTextureAsset("rainRippleSeed"));
	if (rippleSeed == nullptr || m_SurfaceRippleSets.empty())
	{
		VANS_LOG_ERROR("[Rain] Built-in ripple seed texture is unavailable");
		return false;
	}

	descriptorManager->BeginDescriptorUpdate();
	auto writeImage = [&](std::uint32_t binding, VansVKImage& image)
	{
		descriptorManager->WriteImageDescriptor(
			m_PassSets.front(), binding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { image.GetSampler(), image.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	};
	writeImage(0, renderPassManager->GetGbuffer2());
	writeImage(1, renderPassManager->GetNormal());
	writeImage(2, rainLayer->GetImage());
	writeImage(3, splashAtlas->GetImage());
	descriptorManager->WriteImageDescriptor(
		m_PassSets.front(), 4,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { renderPassManager->GetCascadeShadowSampler(),
			renderPassManager->GetCascadeShadowArrayView(),
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL } });
	descriptorManager->WriteImageDescriptor(
		m_PassSets.front(), 5,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		renderPassManager->GetPunctualShadowDescriptorInfos());
	descriptorManager->WriteImageDescriptor(
		m_SurfaceRippleSets.front(), 0,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ rippleSeed->GetImage().GetSampler(), rippleSeed->GetImage().GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
	descriptorManager->WriteImageDescriptor(
		m_SurfaceRippleSets.front(), 1,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ VK_NULL_HANDLE, m_SurfaceRippleImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptorManager->WriteImageDescriptor(
		m_Scene->GetGlobalDescriptorSet(), GLOBAL_BINDING_SURFACE_WEATHER_RIPPLE,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_SurfaceRippleImage.GetSampler(), m_SurfaceRippleImage.GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL }});
	descriptorManager->CommitDescriptorUpdates();
	const auto& rippleInfo = m_SurfaceRippleImage.GetImageCreateInfo();
	VANS_LOG("[Rain] Surface ripple ready: seed=" << rippleSeed->GetWidth()
		<< "x" << rippleSeed->GetHeight() << ", normal=" << rippleInfo.extent.width
		<< "x" << rippleInfo.extent.height << ", mips=" << rippleInfo.mipLevels);
	return true;
}

void VansRainRenderSystem::Shutdown()
{
	auto* descriptorManager = VansVKDescriptorManager::GetInstance();
	if (descriptorManager && !m_PassSets.empty())
		descriptorManager->DestroyDescriptorSet(m_PassSets);
	if (descriptorManager && !m_SurfaceRippleSets.empty())
		descriptorManager->DestroyDescriptorSet(m_SurfaceRippleSets);
	if (descriptorManager && m_PassLayout != VK_NULL_HANDLE)
		descriptorManager->ReleaseDescriptorSetLayout(m_PassLayout);
	if (descriptorManager && m_SurfaceRippleLayout != VK_NULL_HANDLE)
		descriptorManager->ReleaseDescriptorSetLayout(m_SurfaceRippleLayout);
	if (m_Scene && m_Scene->GetRuntimeResourceDevice() && m_SurfaceRippleImage.HasResources())
		m_SurfaceRippleImage.DestroyVulkanImage(m_Scene->GetRuntimeResourceDevice()->GetLogicDevice());
	m_PassSets.clear();
	m_SurfaceRippleSets.clear();
	m_Layouts.clear();
	m_Sets.clear();
	m_PassLayout = VK_NULL_HANDLE;
	m_SurfaceRippleLayout = VK_NULL_HANDLE;
	m_StreakShader = nullptr;
	m_SplashShader = nullptr;
	m_SurfaceRippleShader = nullptr;
	m_Scene = nullptr;
}

void VansRainRenderSystem::GenerateSurfaceRipple(VansVKCommandBuffer& commandBuffer)
{
	if (!m_Scene || !m_SurfaceRippleShader || m_SurfaceRippleSets.empty() ||
		!m_SurfaceRippleImage.HasResources())
		return;
	VANS_GPU_SCOPE(commandBuffer.GetVKCommandBuffer(), "Surface Weather Ripple");
	VkImageMemoryBarrier toCompute{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	const VkImageLayout oldLayout = m_SurfaceRippleImage.GetImageLayout();
	toCompute.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;
	toCompute.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toCompute.oldLayout = oldLayout;
	toCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	toCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toCompute.image = m_SurfaceRippleImage.GetImage();
	toCompute.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	commandBuffer.PipelineBarrier(
		oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { toCompute });
	m_SurfaceRippleImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
	commandBuffer.EnsureComputeShader(*m_SurfaceRippleShader,
		{ m_Scene->GetGlobalDescriptorSetLayout(), m_SurfaceRippleLayout });
	commandBuffer.DispatchCompute(*m_SurfaceRippleShader,
		(SurfaceRippleResolution + 7u) / 8u, (SurfaceRippleResolution + 7u) / 8u, 1,
		{ m_Scene->GetGlobalDescriptorSet(), m_SurfaceRippleSets.front() });
	VkMemoryBarrier toSurfaceReaders{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	toSurfaceReaders.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toSurfaceReaders.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		{ toSurfaceReaders });
}

void VansRainRenderSystem::Render(
	VansVKCommandBuffer& commandBuffer,
	GlobalStateData& globalState)
{
	if (!m_StreakShader || !m_SplashShader || m_Sets.size() != 2)
		return;
	globalState.vertexInputBindingDescriptions = nullptr;
	globalState.vertexInputAttributeDescriptions = nullptr;

	auto drawInstances = [&](VansGraphicsShader& shader,
		std::uint32_t vertexCount, std::uint32_t instanceCount)
	{
		auto* pipeline = commandBuffer.EnsureGraphicsShader(shader, globalState, m_Layouts);
		if (!pipeline)
			return;
		commandBuffer.BindGraphicsPipeline(*pipeline);
		commandBuffer.BindDescriptorSets(
			VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, m_Sets, {});
		commandBuffer.Draw(vertexCount, instanceCount, 0, 0);
	};
	drawInstances(*m_StreakShader, DoubleConeSegments * 6u, 1u);
	drawInstances(*m_SplashShader, 6u, MaximumSplashCount);
}
}
