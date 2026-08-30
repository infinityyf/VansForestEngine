#include "VansVolumetricCloudSystem.h"

#include "../AtmosphereCore/VansNearMediaSystem.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"

#include <vector>

namespace VansGraphics
{
namespace
{
	constexpr VkImageUsageFlags CloudImageUsage =
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	std::uint32_t DivideRoundUp(std::uint32_t value, std::uint32_t divisor)
	{
		return (value + divisor - 1u) / divisor;
	}
}

bool VansVolumetricCloudSystem::Initialize(VansVKDevice& device, VansScene& scene,
	const VansCloudShadowQualityConfig& quality,
	std::uint32_t renderWidth, std::uint32_t renderHeight)
{
	Shutdown();
	m_Device = &device;
	m_Scene = &scene;
	m_Quality = quality;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;
	m_CloudWidth = DivideRoundUp(renderWidth, 4u);
	m_CloudHeight = DivideRoundUp(renderHeight, 4u);
	m_RayMarchShader = VansShaderManager::Get().FindComputeShader("VolumetricCloudRayMarch");
	m_ShadowShader = VansShaderManager::Get().FindComputeShader("VolumetricCloudShadow");
	if (!m_RayMarchShader || !m_ShadowShader || !CreatePersistentResources() ||
		!CreateViewResources() || !CreateDescriptors())
	{
		VANS_LOG_ERROR("[VolumetricCloud] Initialization failed");
		Shutdown();
		return false;
	}
	m_Initialized = true;
	return true;
}

bool VansVolumetricCloudSystem::CreatePersistentResources()
{
	VkDevice device = m_Device->GetLogicDevice();
	m_ParamsBufferCreated = m_ParamsBuffer.CreatVulkanBuffer(device,
		sizeof(VansVolumetricCloudRuntimeParamsGPU), VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (!m_ParamsBufferCreated || !LoadNoiseResources())
		return false;
	return true;
}

bool VansVolumetricCloudSystem::CreateViewResources()
{
	VkDevice device = m_Device->GetLogicDevice();
	if (!m_Result.CreateVulkanImage(device, { m_CloudWidth, m_CloudHeight, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_2D,
		CloudImageUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_Depth.CreateVulkanImage(device, { m_CloudWidth, m_CloudHeight, 1 },
		VK_FORMAT_R32G32_SFLOAT, 1, 1, VK_IMAGE_TYPE_2D,
		CloudImageUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_OpticalDepth.CreateVulkanImage(device, { m_CloudWidth, m_CloudHeight, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_2D,
		CloudImageUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	return m_Shadow.CreateVulkanImage(device,
		{ m_Quality.resolution, m_Quality.resolution, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, m_Quality.clipmapCount,
		VK_IMAGE_TYPE_2D, CloudImageUsage, VK_SAMPLE_COUNT_1_BIT,
		false, true, true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

bool VansVolumetricCloudSystem::LoadNoiseResources()
{
	const std::string projectRoot = VansConfigration::GetInstance()->GetProjectRootPath();
	VansVKCommandBuffer& commandBuffer = m_Device->GetImmediateGraphicsCommandBuffer();
	m_MainNoise = new VansTexture();
	if (!m_MainNoise->LoadTexture3DFromSlices(commandBuffer,
		projectRoot + "EngineAssets/Textures/VolumeCloud/Slice_Z_%03d.png",
		128, 4, VK_SAMPLER_ADDRESS_MODE_REPEAT))
		return false;
	m_DetailNoise = new VansTexture();
	return m_DetailNoise->LoadTexture3DFromSlices(commandBuffer,
		projectRoot + "EngineAssets/Textures/VolumeCloud/Detail/Detail_Z_%03d.png",
		32, 4, VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

bool VansVolumetricCloudSystem::CreateDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	const std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
	};
	if (!descriptors->CreateDesciptorSetLayout(bindings, m_PassLayout))
		return false;
	std::vector<VkDescriptorSet> sets;
	if (!descriptors->AllocateDescriptorSet({ m_PassLayout }, sets,
		VansDescriptorLifetimeRole::ScenePersistent) || sets.empty())
		return false;
	m_PassSet = sets.front();
	descriptors->BeginDescriptorUpdate();
	descriptors->WriteImageDescriptor(m_PassSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_Result.GetSampler(), m_Result.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_Depth.GetSampler(), m_Depth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteBufferDescriptor(m_PassSet, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansVolumetricCloudRuntimeParamsGPU) }});
	descriptors->WriteImageDescriptor(m_PassSet, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_MainNoise->GetImage().GetSampler(), m_MainNoise->GetImage().GetImageView(),
		   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_DetailNoise->GetImage().GetSampler(), m_DetailNoise->GetImage().GetImageView(),
		   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_Shadow.GetSampler(), m_Shadow.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_OpticalDepth.GetSampler(), m_OpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	if (auto* nearMedia = m_Scene->GetNearMediaSystem())
	{
		descriptors->WriteImageDescriptor(m_PassSet, 7,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ nearMedia->GetCurrentInjectionDescriptor() });
	}
	auto* renderPasses = VansRenderPassManager::GetInstance();
	descriptors->WriteImageDescriptor(m_PassSet, 8,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ renderPasses->GetDepth().GetSampler(),
		   renderPasses->GetDepth().GetImageView(),
		   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
	descriptors->CommitDescriptorUpdates();
	return true;
}

void VansVolumetricCloudSystem::UploadParameters()
{
	const auto& environment = m_Scene->GetEnvironmentSettings();
	const auto& source = environment.volumetricClouds;
	VansVolumetricCloudRuntimeParamsGPU params{};
	auto& p = params.medium;
	p.planetBottomRadius = static_cast<float>(environment.planet.bottomRadiusMeters);
#define VANS_COPY_CLOUD_FIELD(name) p.name = source.name
	VANS_COPY_CLOUD_FIELD(cloudMinHeight); VANS_COPY_CLOUD_FIELD(cloudMaxHeight);
	VANS_COPY_CLOUD_FIELD(density); VANS_COPY_CLOUD_FIELD(coverage);
	VANS_COPY_CLOUD_FIELD(sunBrightness);
	p.atmosphereShadowStrength = source.shadow.enabled
		? source.shadow.atmosphereStrength : 0.0f;
	VANS_COPY_CLOUD_FIELD(mainTileMeters); VANS_COPY_CLOUD_FIELD(detailTileMeters);
	VANS_COPY_CLOUD_FIELD(mainHeightScale); VANS_COPY_CLOUD_FIELD(detailHeightScale);
	VANS_COPY_CLOUD_FIELD(thresholdLowCoverage); VANS_COPY_CLOUD_FIELD(thresholdHighCoverage);
	VANS_COPY_CLOUD_FIELD(densityRemapLow); VANS_COPY_CLOUD_FIELD(densityRemapHigh);
	VANS_COPY_CLOUD_FIELD(mainErosionStrength); VANS_COPY_CLOUD_FIELD(detailErosionStrength);
	VANS_COPY_CLOUD_FIELD(edgeErosionStrength); VANS_COPY_CLOUD_FIELD(verticalShapePower);
	VANS_COPY_CLOUD_FIELD(detailErosionLow); VANS_COPY_CLOUD_FIELD(detailErosionHigh);
	VANS_COPY_CLOUD_FIELD(detailEdgeStrength);
	VANS_COPY_CLOUD_FIELD(sigmaTRef); VANS_COPY_CLOUD_FIELD(viewAbsorption);
	VANS_COPY_CLOUD_FIELD(lightAbsorption); VANS_COPY_CLOUD_FIELD(singleScatteringAlbedo);
	VANS_COPY_CLOUD_FIELD(forwardEccentricity); VANS_COPY_CLOUD_FIELD(backwardEccentricity);
	VANS_COPY_CLOUD_FIELD(msAttenuation); VANS_COPY_CLOUD_FIELD(msContribution);
	VANS_COPY_CLOUD_FIELD(msEccentricity); VANS_COPY_CLOUD_FIELD(scatteringTintR);
	VANS_COPY_CLOUD_FIELD(scatteringTintG); VANS_COPY_CLOUD_FIELD(scatteringTintB);
	VANS_COPY_CLOUD_FIELD(scatterSourceODScale); VANS_COPY_CLOUD_FIELD(scatterSourceCurvePow);
	VANS_COPY_CLOUD_FIELD(aoUpwardScale); VANS_COPY_CLOUD_FIELD(ambientBottomStrength);
	VANS_COPY_CLOUD_FIELD(ambientTopStrength); VANS_COPY_CLOUD_FIELD(ambientDuskWarmth);
	VANS_COPY_CLOUD_FIELD(boundaryConfidence); VANS_COPY_CLOUD_FIELD(boundaryWrap);
	VANS_COPY_CLOUD_FIELD(phiFwdIntensity); VANS_COPY_CLOUD_FIELD(phiFwdDepthPow);
	VANS_COPY_CLOUD_FIELD(phiFwdDepthBias); VANS_COPY_CLOUD_FIELD(phiFwdMSBuildScale);
	VANS_COPY_CLOUD_FIELD(phiFwdCompress); VANS_COPY_CLOUD_FIELD(phiFwdMaxDistance);
	VANS_COPY_CLOUD_FIELD(phiFwdConeRatio); VANS_COPY_CLOUD_FIELD(phiFwdMinStep);
	VANS_COPY_CLOUD_FIELD(lightStepCount); VANS_COPY_CLOUD_FIELD(boundaryGradientStep);
	VANS_COPY_CLOUD_FIELD(boundaryGradientStrength); VANS_COPY_CLOUD_FIELD(shadingDebugMode);
#undef VANS_COPY_CLOUD_FIELD
	if (!source.enabled)
		p.density = 0.0f;
	params.shadowClipmapParameters = glm::vec4(
		m_Quality.nearCoverageMeters, m_Quality.farCoverageMeters,
		static_cast<float>(m_Quality.resolution), static_cast<float>(m_Quality.clipmapCount));
	params.shadowRayMarchParameters = glm::vec4(
		static_cast<float>(m_Quality.rayMarchSamples),
		m_Quality.clipmapCrossFadeFraction,
		source.shadow.enabled ? source.shadow.ambientOcclusionStrength : 0.0f,
		source.shadow.enabled ? 1.0f : 0.0f);
	m_ParamsBuffer.SetBufferData(&params, 0, sizeof(params));
}

void VansVolumetricCloudSystem::TransitionForWrite(VansVKCommandBuffer& commandBuffer,
	VansVKImage& image, bool initialized)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = initialized ? (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) : 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.oldLayout = initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
		image.GetImageCreateInfo().arrayLayers };
	commandBuffer.PipelineBarrier(initialized
		? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
		: VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{}, {}, { barrier });
	image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VansVolumetricCloudSystem::BarrierForSampling(VansVKCommandBuffer& commandBuffer,
	VansVKImage& image)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
		image.GetImageCreateInfo().arrayLayers };
	commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		{}, {}, { barrier });
}

void VansVolumetricCloudSystem::RecordShadow(VansVKCommandBuffer& commandBuffer)
{
	if (!m_Initialized)
		return;
	UploadParameters();
	const std::vector<VkDescriptorSetLayout> layouts = {
		m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout };
	const std::vector<VkDescriptorSet> sets = {
		m_Scene->GetGlobalDescriptorSet(), m_PassSet };
	TransitionForWrite(commandBuffer, m_Shadow, m_ShadowInitialized);
	commandBuffer.EnsureComputeShader(*m_ShadowShader, layouts);
	commandBuffer.DispatchCompute(*m_ShadowShader,
		DivideRoundUp(m_Quality.resolution, 8u),
		DivideRoundUp(m_Quality.resolution, 8u), m_Quality.clipmapCount, sets);
	BarrierForSampling(commandBuffer, m_Shadow);
	m_ShadowInitialized = true;
}

void VansVolumetricCloudSystem::RecordRayMarch(VansVKCommandBuffer& commandBuffer)
{
	if (!m_Initialized || !m_ShadowInitialized)
		return;
	// 局部雾 Froxel 使用 ping-pong 历史；云必须绑定本帧刚完成注入的纹理。
	if (auto* nearMedia = m_Scene->GetNearMediaSystem())
	{
		auto* descriptors = VansVKDescriptorManager::GetInstance();
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(m_PassSet, 7,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ nearMedia->GetCurrentInjectionDescriptor() });
		descriptors->CommitDescriptorUpdates();
	}
	const std::vector<VkDescriptorSetLayout> layouts = {
		m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout };
	const std::vector<VkDescriptorSet> sets = {
		m_Scene->GetGlobalDescriptorSet(), m_PassSet };
	TransitionForWrite(commandBuffer, m_Result, m_ResultInitialized);
	TransitionForWrite(commandBuffer, m_Depth, m_ResultInitialized);
	TransitionForWrite(commandBuffer, m_OpticalDepth, m_ResultInitialized);
	commandBuffer.EnsureComputeShader(*m_RayMarchShader, layouts);
	commandBuffer.DispatchCompute(*m_RayMarchShader,
		DivideRoundUp(m_CloudWidth, 8u), DivideRoundUp(m_CloudHeight, 8u), 1u, sets);
	BarrierForSampling(commandBuffer, m_Result);
	BarrierForSampling(commandBuffer, m_Depth);
	BarrierForSampling(commandBuffer, m_OpticalDepth);
	m_ResultInitialized = true;
}

void VansVolumetricCloudSystem::BindGlobalDescriptors(VkDescriptorSet globalSet)
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	descriptors->BeginDescriptorUpdate();
	descriptors->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_VOLUMETRIC_CLOUD_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansVolumetricCloudRuntimeParamsGPU) }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_CLOUD_DEPTH,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { GetDepthDescriptor() });
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_CLOUD_OPTICAL_DEPTH,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { GetOpticalDepthDescriptor() });
	descriptors->CommitDescriptorUpdates();
}

VkDescriptorImageInfo VansVolumetricCloudSystem::GetShadowDescriptor()
{ return { m_Shadow.GetSampler(), m_Shadow.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }; }
VkDescriptorImageInfo VansVolumetricCloudSystem::GetResultDescriptor()
{ return { m_Result.GetSampler(), m_Result.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }; }
VkDescriptorImageInfo VansVolumetricCloudSystem::GetDepthDescriptor()
{ return { m_Depth.GetSampler(), m_Depth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }; }
VkDescriptorImageInfo VansVolumetricCloudSystem::GetOpticalDepthDescriptor()
{ return { m_OpticalDepth.GetSampler(), m_OpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }; }

bool VansVolumetricCloudSystem::Reinitialize(const VansCloudShadowQualityConfig& quality,
	std::uint32_t renderWidth, std::uint32_t renderHeight)
{
	if (!m_Device || !m_Scene)
		return false;
	VansVKDevice* device = m_Device;
	device->WaitForDevice();
	DestroyDescriptors();
	DestroyViewResources();
	m_Quality = quality;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;
	m_CloudWidth = DivideRoundUp(renderWidth, 4u);
	m_CloudHeight = DivideRoundUp(renderHeight, 4u);
	m_ResultInitialized = false;
	m_ShadowInitialized = false;
	if (!CreateViewResources() || !CreateDescriptors())
	{
		VANS_LOG_ERROR("[VolumetricCloud] View resource reinitialization failed");
		Shutdown();
		return false;
	}
	m_Initialized = true;
	return true;
}

void VansVolumetricCloudSystem::DestroyDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	if (m_PassSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> sets{ m_PassSet };
		descriptors->DestroyDescriptorSet(sets);
		m_PassSet = VK_NULL_HANDLE;
	}
	if (m_PassLayout != VK_NULL_HANDLE)
	{
		descriptors->DestroyDescriptorSetLayout(m_PassLayout);
		m_PassLayout = VK_NULL_HANDLE;
	}
}

void VansVolumetricCloudSystem::DestroyViewResources()
{
	if (!m_Device)
		return;
	VkDevice device = m_Device->GetLogicDevice();
	m_Result.DestroyVulkanImage(device);
	m_Depth.DestroyVulkanImage(device);
	m_OpticalDepth.DestroyVulkanImage(device);
	m_Shadow.DestroyVulkanImage(device);
}

void VansVolumetricCloudSystem::DestroyPersistentResources()
{
	if (!m_Device)
		return;
	VkDevice device = m_Device->GetLogicDevice();
	delete m_MainNoise;
	delete m_DetailNoise;
	m_MainNoise = nullptr;
	m_DetailNoise = nullptr;
	if (m_ParamsBufferCreated)
		m_ParamsBuffer.DestroyVulkanBuffer(device);
	m_ParamsBufferCreated = false;
}

void VansVolumetricCloudSystem::Shutdown()
{
	if (m_Device)
	{
		DestroyDescriptors();
		DestroyViewResources();
		DestroyPersistentResources();
	}
	m_Device = nullptr;
	m_Scene = nullptr;
	m_ResultInitialized = false;
	m_ShadowInitialized = false;
	m_Initialized = false;
}
}
