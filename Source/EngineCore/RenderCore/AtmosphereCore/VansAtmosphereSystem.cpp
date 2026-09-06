#include "VansAtmosphereSystem.h"

#include "../VansCamera.h"

#include "../BRDFData/VansLight.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace VansGraphics
{
namespace
{
	constexpr VkImageUsageFlags AtmosphereImageUsage =
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	glm::vec3 ToVec3(const std::array<float, 3>& value)
	{
		return { value[0], value[1], value[2] };
	}

	glm::dvec3 ToDouble3(const std::array<double, 3>& value)
	{
		return { value[0], value[1], value[2] };
	}

	std::uint32_t DivideRoundUp(std::uint32_t value, std::uint32_t divisor)
	{
		return (value + divisor - 1u) / divisor;
	}

	bool EqualVec4(const glm::vec4& left, const glm::vec4& right)
	{
		return left.x == right.x && left.y == right.y &&
			left.z == right.z && left.w == right.w;
	}

	bool EqualVec3(const glm::vec4& left, const glm::vec4& right)
	{
		return left.x == right.x && left.y == right.y && left.z == right.z;
	}

	bool StaticLutInputsEqual(
		const VansAtmosphereStaticParamsGPU& left,
		const VansAtmosphereStaticParamsGPU& right)
	{
		return left.planetRadiiMeters.x == right.planetRadiiMeters.x &&
			left.planetRadiiMeters.y == right.planetRadiiMeters.y &&
			left.planetRadiiMeters.z == right.planetRadiiMeters.z &&
			EqualVec4(left.rayleighScatteringAndScaleHeight,
				right.rayleighScatteringAndScaleHeight) &&
			EqualVec4(left.mieScatteringAndScaleHeight,
				right.mieScatteringAndScaleHeight) &&
			EqualVec4(left.mieAbsorptionAndAnisotropy,
				right.mieAbsorptionAndAnisotropy) &&
			EqualVec4(left.ozoneAbsorptionAndCenterAltitude,
				right.ozoneAbsorptionAndCenterAltitude) &&
			EqualVec4(left.ozoneHalfWidthAndGroundAlbedo,
				right.ozoneHalfWidthAndGroundAlbedo) &&
			left.featureFlags.x == right.featureFlags.x &&
			left.lutSampleCounts.x == right.lutSampleCounts.x &&
			left.lutSampleCounts.y == right.lutSampleCounts.y &&
			left.lutDimensions.x == right.lutDimensions.x &&
			left.lutDimensions.y == right.lutDimensions.y &&
			left.lutDimensions.z == right.lutDimensions.z &&
			left.lutDimensions.w == right.lutDimensions.w;
	}

	bool CreateUniformBuffer(
		VansVKBuffer& buffer,
		bool& created,
		VkDevice device,
		VkDeviceSize size)
	{
		created = buffer.CreatVulkanBuffer(
			device,
			size,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		return created;
	}
}

bool VansAtmosphereSystem::Initialize(
	VansVKDevice& device,
	VansScene& scene,
	const VansAtmosphereQualityConfig& quality,
	std::uint32_t renderWidth,
	std::uint32_t renderHeight)
{
	if (m_Initialized)
		Shutdown();
	m_Device = &device;
	m_Scene = &scene;
	m_Quality = quality;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;

	if (!CreateStaticResources() || !CreateViewResources() ||
		!CreateDescriptorResources())
	{
		VANS_LOG_ERROR("[Atmosphere] GPU resource initialization failed");
		Shutdown();
		return false;
	}

	m_TransmittanceShader = VansShaderManager::Get().FindComputeShader("AtmosphereTransmittance");
	m_MultiScatteringShader = VansShaderManager::Get().FindComputeShader("AtmosphereMultiScattering");
	m_SkyViewShader = VansShaderManager::Get().FindComputeShader("AtmosphereSkyView");
	m_AerialPerspectiveShader = VansShaderManager::Get().FindComputeShader("AtmosphereAerialPerspective");
	m_CompositeShader = VansShaderManager::Get().FindComputeShader("AtmosphereComposite");
	if (!m_TransmittanceShader || !m_MultiScatteringShader || !m_SkyViewShader ||
		!m_AerialPerspectiveShader || !m_CompositeShader)
	{
		VANS_LOG_ERROR("[Atmosphere] Required compute shaders are not registered");
		Shutdown();
		return false;
	}

	// 所有可选介质都使用有物理含义的中性资源，禁止空 descriptor。
	VansVKCommandBuffer& commandBuffer = device.GetImmediateGraphicsCommandBuffer();
	if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
	{
		Shutdown();
		return false;
	}
	VkImageMemoryBarrier toClear{};
	toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toClear.srcAccessMask = 0;
	toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toClear.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	auto clearNeutral = [&](VansVKImage& image, std::uint32_t layers,
		const VkClearColorValue& value)
	{
		toClear.image = image.GetImage();
		toClear.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{}, {}, { toClear });
		image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer.ClearColorImage(image, VK_IMAGE_LAYOUT_GENERAL, value);
		VkImageMemoryBarrier toSample = toClear;
		toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toSample.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		toSample.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, {}, { toSample });
	};
	VkClearColorValue black{};
	VkClearColorValue transparentCloud{};
	transparentCloud.float32[3] = 1.0f;
	VkClearColorValue white{};
	white.float32[0] = white.float32[1] = white.float32[2] = white.float32[3] = 1.0f;
	clearNeutral(m_NeutralCloudShadow, 1u, white);
	clearNeutral(m_NeutralCloudResult, 1u, transparentCloud);
	clearNeutral(m_NeutralLocalMedia, 1u, black);
	if (!commandBuffer.EndCommandBufferRecord() ||
		!VansVKCommandBuffer::SubmitCommands(
			device.GetGraphicsQueue(),
			device.GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() },
			{}, {}, commandBuffer.m_CommandBufferFinishSubmitFence) ||
		!commandBuffer.ResetCommandBuffer(false))
	{
		VANS_LOG_ERROR("[Atmosphere] Neutral media initialization submit failed");
		Shutdown();
		return false;
	}
	device.WaitForDevice();
	m_AppliedEnvironmentGeneration = std::numeric_limits<std::uint64_t>::max();
	m_HasUploadedStaticParams = false;
	m_StaticLutsDirty = true;
	m_Initialized = true;
	return true;
}

bool VansAtmosphereSystem::CreateStaticResources()
{
	if (!m_Device)
		return false;
	VkDevice device = m_Device->GetLogicDevice();
	if (!CreateUniformBuffer(
		m_StaticParamsBuffer, m_StaticParamsBufferCreated,
		device, sizeof(VansAtmosphereStaticParamsGPU)) ||
		!CreateUniformBuffer(
		m_FrameParamsBuffer, m_FrameParamsBufferCreated,
		device, sizeof(VansAtmosphereFrameParamsGPU)))
		return false;

	if (!m_TransmittanceLut.CreateVulkanImage(
		device,
		{ m_Quality.transmittanceWidth, m_Quality.transmittanceHeight, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_2D, AtmosphereImageUsage,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_MultiScatteringLut.CreateVulkanImage(
		device,
		{ m_Quality.multiScatteringWidth, m_Quality.multiScatteringHeight, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_2D, AtmosphereImageUsage,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_NeutralCloudShadow.CreateVulkanImage(
		device, { 1, 1, 1 }, VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_NeutralCloudResult.CreateVulkanImage(
		device, { 1, 1, 1 }, VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	return m_NeutralLocalMedia.CreateVulkanImage(
		device, { 1, 1, 1 }, VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_3D,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

VkDescriptorImageInfo VansAtmosphereSystem::GetNeutralCloudShadowDescriptor()
{
	return { m_NeutralCloudShadow.GetSampler(), m_NeutralCloudShadow.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

VkDescriptorImageInfo VansAtmosphereSystem::GetNeutralCloudResultDescriptor()
{
	return { m_NeutralCloudResult.GetSampler(), m_NeutralCloudResult.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

VkDescriptorImageInfo VansAtmosphereSystem::GetNeutralLocalMediaDescriptor()
{
	return { m_NeutralLocalMedia.GetSampler(), m_NeutralLocalMedia.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

bool VansAtmosphereSystem::CreateViewResources()
{
	if (!m_Device || m_RenderWidth == 0 || m_RenderHeight == 0)
		return false;
	m_AerialWidth = DivideRoundUp(m_RenderWidth, m_Quality.farAerialTileSize);
	m_AerialHeight = DivideRoundUp(m_RenderHeight, m_Quality.farAerialTileSize);
	VkDevice device = m_Device->GetLogicDevice();
	if (!m_SkyViewLut.CreateVulkanImage(
		device,
		{ m_Quality.skyViewWidth, m_Quality.skyViewHeight, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_2D, AtmosphereImageUsage,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_AerialScattering.CreateVulkanImage(
		device,
		{ m_AerialWidth, m_AerialHeight, m_Quality.farAerialSlices },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_3D, AtmosphereImageUsage,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_AerialClearScattering.CreateVulkanImage(
		device,
		{ m_AerialWidth, m_AerialHeight, m_Quality.farAerialSlices },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_3D, AtmosphereImageUsage,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	return m_AerialOpticalDepth.CreateVulkanImage(
		device,
		{ m_AerialWidth, m_AerialHeight, m_Quality.farAerialSlices },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1, VK_IMAGE_TYPE_3D, AtmosphereImageUsage,
		VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

bool VansAtmosphereSystem::CreateDescriptorResources()
{
	if (!m_Scene)
		return false;
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	const VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
	const std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr },
		{ 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr },
		{ 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr },
		{ 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr },
		{ 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
		{ 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr }
	};
	if (!descriptors->CreateDesciptorSetLayout(bindings, m_PassLayout))
		return false;
	std::vector<VkDescriptorSet> sets;
	if (!descriptors->AllocateDescriptorSet(
		{ m_PassLayout }, sets, VansDescriptorLifetimeRole::ScenePersistent) || sets.empty())
		return false;
	m_PassSet = sets[0];

	auto* renderPasses = VansRenderPassManager::GetInstance();
	descriptors->BeginDescriptorUpdate();
	descriptors->WriteImageDescriptor(m_PassSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_TransmittanceLut.GetSampler(), m_TransmittanceLut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_MultiScatteringLut.GetSampler(), m_MultiScatteringLut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_SkyViewLut.GetSampler(), m_SkyViewLut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_AerialScattering.GetSampler(), m_AerialScattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_AerialOpticalDepth.GetSampler(), m_AerialOpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ renderPasses->GetRawOpaqueSceneColor().GetSampler(),
		   renderPasses->GetRawOpaqueSceneColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ renderPasses->GetDepth().GetSampler(),
		   renderPasses->GetDepth().GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ renderPasses->GetColor().GetSampler(),
		   renderPasses->GetColor().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ renderPasses->GetWaterGBufNormal().GetSampler(),
		   renderPasses->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ renderPasses->GetWaterGBufLinearDepth().GetSampler(),
		   renderPasses->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{ m_AerialClearScattering.GetSampler(), m_AerialClearScattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(m_PassSet, 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_AerialClearScattering.GetSampler(), m_AerialClearScattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->CommitDescriptorUpdates();
	return true;
}

void VansAtmosphereSystem::BindGlobalDescriptors(
	VkDescriptorSet globalSet,
	const VkDescriptorImageInfo& cloudShadow,
	const VkDescriptorImageInfo& cloudResult,
	const VkDescriptorImageInfo& localScattering,
	const VkDescriptorImageInfo& localOpticalDepth)
{
	if (!m_Initialized)
		return;
	m_GlobalSet = globalSet;
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	descriptors->BeginDescriptorUpdate();
	descriptors->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_ATMOSPHERE_STATIC_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{ m_StaticParamsBuffer.GetNativeBuffer(), 0, sizeof(VansAtmosphereStaticParamsGPU) }});
	descriptors->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_ATMOSPHERE_FRAME_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{ m_FrameParamsBuffer.GetNativeBuffer(), 0, sizeof(VansAtmosphereFrameParamsGPU) }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_TRANSMITTANCE_LUT,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_TransmittanceLut.GetSampler(), m_TransmittanceLut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_MULTI_SCATTERING_LUT,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_MultiScatteringLut.GetSampler(), m_MultiScatteringLut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_SKY_VIEW_LUT,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_SkyViewLut.GetSampler(), m_SkyViewLut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_AERIAL_SCATTERING,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_AerialScattering.GetSampler(), m_AerialScattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_AERIAL_OPTICAL_DEPTH,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{{ m_AerialOpticalDepth.GetSampler(), m_AerialOpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_CLOUD_SHADOW,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { cloudShadow });
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_CLOUD_RESULT,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { cloudResult });
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_LOCAL_MEDIA_SCATTERING,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { localScattering });
	descriptors->WriteImageDescriptor(globalSet, GLOBAL_BINDING_LOCAL_MEDIA_OPTICAL_DEPTH,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { localOpticalDepth });
	descriptors->CommitDescriptorUpdates();
}

void VansAtmosphereSystem::UploadEnvironmentParameters(std::uint64_t frameIndex)
{
	if (!m_Scene)
		return;
	const auto& environment = m_Scene->GetEnvironmentSettings();
	const std::uint64_t generation = m_Scene->GetEnvironmentSettingsGeneration();
	if (m_AppliedEnvironmentGeneration != generation)
	{
		VansAtmosphereStaticParamsGPU params{};
		const float bottomRadius =
			static_cast<float>(environment.planet.bottomRadiusMeters);
		const float atmosphereHeight =
			static_cast<float>(environment.planet.atmosphereHeightMeters);
		const auto& physical = environment.physicalAtmosphere;
		const auto& fog = environment.heightFog;
		params.planetRadiiMeters = glm::vec4(
			bottomRadius, bottomRadius + atmosphereHeight, atmosphereHeight, 0.0f);
		params.rayleighScatteringAndScaleHeight = glm::vec4(
			ToVec3(physical.rayleigh.scatteringPerMeterAtGround),
			physical.rayleigh.densityScaleHeightMeters);
		params.mieScatteringAndScaleHeight = glm::vec4(
			ToVec3(physical.mie.scatteringPerMeterAtGround),
			physical.mie.densityScaleHeightMeters);
		params.mieAbsorptionAndAnisotropy = glm::vec4(
			ToVec3(physical.mie.absorptionPerMeterAtGround),
			physical.mie.anisotropy);
		params.ozoneAbsorptionAndCenterAltitude = glm::vec4(
			ToVec3(physical.ozone.absorptionPerMeter),
			physical.ozone.centerAltitudeMeters);
		params.ozoneHalfWidthAndGroundAlbedo = glm::vec4(
			physical.ozone.halfWidthMeters,
			physical.groundAlbedo[0],
			physical.groundAlbedo[1],
			physical.groundAlbedo[2]);
		params.aerialPerspectiveAndVolumetricLighting = glm::vec4(
			physical.aerialPerspective.distanceScale,
			physical.mainLightVolumetricScatteringScale, 0.0f, 0.0f);
		params.heightFogDensityParameters = glm::vec4(
			fog.groundHeightWorldMeters,
			1.0f / (std::max)(fog.visibilityAtGroundMeters, 1.0e-3f),
			fog.densityFalloffHeightMeters,
			fog.enabled ? 1.0f : 0.0f);
		params.heightFogDistanceParameters = glm::vec4(
			fog.startDistanceMeters,
			fog.nearFadeDistanceMeters,
			fog.maximumDistanceMeters,
			fog.farFadeDistanceMeters);
		params.heightFogAlbedoAndAnisotropy = glm::vec4(
			ToVec3(fog.singleScatteringAlbedo), fog.anisotropy);
		params.heightFogEmissiveAndSkyScale = glm::vec4(
			ToVec3(fog.emissivePerMeter), fog.skyLightingScale);
		params.heightFogLightingParameters = glm::vec4(
			fog.mainLightVolumetricScale,
			fog.receiveCloudShadows ? 1.0f : 0.0f, 0.0f, 0.0f);
		params.featureFlags = glm::uvec4(
			physical.enabled ? 1u : 0u,
			fog.enabled ? 1u : 0u,
			static_cast<std::uint32_t>((std::min)(
				physical.celestialBodies.size(),
				static_cast<std::size_t>(VansMaxCelestialBodies))),
			0u);
		params.lutSampleCounts = glm::uvec4(
			m_Quality.transmittanceSamples,
			m_Quality.multiScatteringSamples,
			m_Quality.skyViewSamples,
			m_Quality.farAerialSamplesPerSlice);
		params.lutDimensions = glm::uvec4(
			m_Quality.transmittanceWidth,
			m_Quality.transmittanceHeight,
			m_Quality.multiScatteringWidth,
			m_Quality.multiScatteringHeight);
		const bool staticLutInputsChanged = !m_HasUploadedStaticParams ||
			!StaticLutInputsEqual(m_LastUploadedStaticParams, params);

		m_StaticParamsBuffer.SetBufferData(&params, 0, sizeof(params));
		m_AppliedEnvironmentGeneration = generation;
		m_StaticLutsDirty = m_StaticLutsDirty || staticLutInputsChanged;
		m_LastUploadedStaticParams = params;
		m_HasUploadedStaticParams = true;
	}

	VansAtmosphereFrameParamsGPU frame{};
	VansCamera* camera = m_Scene->GetCamera();
	const glm::dvec3 cameraWorld = camera
		? glm::dvec3(camera->GetPosition()) : glm::dvec3(0.0);
	const glm::dvec3 planetCenter = ToDouble3(environment.planet.centerWorldMeters);
	const double altitude = glm::length(cameraWorld - planetCenter) -
		environment.planet.bottomRadiusMeters;
	const glm::dvec3 centerRelative = planetCenter - cameraWorld;
	frame.planetCenterRelativeToCameraMeters = glm::vec4(
		glm::vec3(centerRelative), static_cast<float>(altitude));
	frame.cameraWorldMetersAndMaxDistance = glm::vec4(
		glm::vec3(cameraWorld), m_Quality.farAerialMaxDistanceMeters *
			environment.physicalAtmosphere.aerialPerspective.distanceScale);
	frame.aerialPerspectiveParameters = glm::vec4(
		static_cast<float>(m_AerialWidth),
		static_cast<float>(m_AerialHeight),
		static_cast<float>(m_Quality.farAerialSlices),
		static_cast<float>(m_Quality.farAerialSamplesPerSlice));
	frame.viewParameters = glm::uvec4(
		m_RenderWidth, m_RenderHeight,
		m_Quality.farAerialTileSize,
		static_cast<std::uint32_t>(frameIndex));
	frame.surfaceCompositionFlags = glm::uvec4(
		m_Scene->HasWaterNodes() ? 1u : 0u, 0u, 0u, 0u);

	auto* lightManager = m_Scene->GetLightManager();
	const auto& lights = lightManager->GetDirectionLights();
	const bool hasPreparedMainLight = !lights.empty();
	const VansCelestialLightingState& preparedMainLight =
		lightManager->GetMainCelestialLightingState();
	frame.preparedMainLightDirectionAndValidity = glm::vec4(
		glm::normalize(preparedMainLight.direction), hasPreparedMainLight ? 1.0f : 0.0f);
	frame.preparedMainLightColorAndIntensity = glm::vec4(
		glm::max(preparedMainLight.color, glm::vec3(0.0f)),
		(glm::max)(preparedMainLight.intensity, 0.0f));

	for (std::size_t bodyIndex = 0;
		bodyIndex < environment.physicalAtmosphere.celestialBodies.size() &&
		bodyIndex < VansMaxCelestialBodies;
		++bodyIndex)
	{
		const auto& body = environment.physicalAtmosphere.celestialBodies[bodyIndex];
		const VansScriptObject* object = m_Scene->FindObjectByGuid(body.lightEntityId);
		const auto* component = object
			? object->GetComponent<VansScriptDirectionalLightComponent>() : nullptr;
		if (!component || component->m_LightIndex < 0 ||
			static_cast<std::size_t>(component->m_LightIndex) >= lights.size())
		{
			continue;
		}
		const VansDirectionalLight& light = lights[component->m_LightIndex];
		const glm::vec3 direction = glm::normalize(light.m_Direction);
		const glm::vec3 irradiance = glm::max(light.m_Color * light.m_Intensity, glm::vec3(0.0f));
		auto& gpuBody = frame.celestialBodies[bodyIndex];
		gpuBody.directionAndValidity = glm::vec4(direction, 1.0f);
		gpuBody.topOfAtmosphereIrradiance = glm::vec4(irradiance, 0.0f);
		gpuBody.diskParameters = glm::vec4(
			body.disk.enabled ? body.disk.angularRadiusRadians : 0.0f,
			body.disk.featherRadians,
			body.disk.radianceScale,
			body.disk.occlusionStrength);
	}
	m_FrameParamsBuffer.SetBufferData(&frame, 0, sizeof(frame));
}

void VansAtmosphereSystem::TransitionForWrite(
	VansVKCommandBuffer& commandBuffer,
	VansVKImage& image,
	bool wasWritten,
	std::uint32_t mipCount,
	std::uint32_t layerCount)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = wasWritten
		? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.oldLayout = wasWritten ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange = {
		VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount };
	commandBuffer.PipelineBarrier(
		wasWritten
			? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			: VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{}, {}, { barrier });
	image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VansAtmosphereSystem::BarrierForSampling(
	VansVKCommandBuffer& commandBuffer,
	VansVKImage& image,
	std::uint32_t mipCount,
	std::uint32_t layerCount)
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
	barrier.subresourceRange = {
		VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount };
	commandBuffer.PipelineBarrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{}, {}, { barrier });
}

void VansAtmosphereSystem::RecordStaticLutUpdates(
	VansVKCommandBuffer& commandBuffer,
	std::uint64_t frameIndex)
{
	if (!m_Initialized || m_GlobalSet == VK_NULL_HANDLE)
		return;
	UploadEnvironmentParameters(frameIndex);
	const std::vector<VkDescriptorSetLayout> layouts = {
		m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout
	};
	const std::vector<VkDescriptorSet> sets = { m_GlobalSet, m_PassSet };

	if (m_StaticLutsDirty)
	{
		TransitionForWrite(commandBuffer, m_TransmittanceLut, m_TransmittanceReady);
		commandBuffer.EnsureComputeShader(*m_TransmittanceShader, layouts);
		commandBuffer.DispatchCompute(
			*m_TransmittanceShader,
			DivideRoundUp(m_Quality.transmittanceWidth, 8u),
			DivideRoundUp(m_Quality.transmittanceHeight, 8u), 1u, sets);
		BarrierForSampling(commandBuffer, m_TransmittanceLut);
		m_TransmittanceReady = true;

		TransitionForWrite(commandBuffer, m_MultiScatteringLut, m_MultiScatteringReady);
		commandBuffer.EnsureComputeShader(*m_MultiScatteringShader, layouts);
		commandBuffer.DispatchCompute(
			*m_MultiScatteringShader,
			DivideRoundUp(m_Quality.multiScatteringWidth, 8u),
			DivideRoundUp(m_Quality.multiScatteringHeight, 8u), 1u, sets);
		BarrierForSampling(commandBuffer, m_MultiScatteringLut);
		m_MultiScatteringReady = true;
		m_StaticLutsDirty = false;
	}
}

void VansAtmosphereSystem::RecordViewLutUpdates(
	VansVKCommandBuffer& commandBuffer)
{
	if (!m_Initialized || m_GlobalSet == VK_NULL_HANDLE ||
		!m_TransmittanceReady ||
		!m_MultiScatteringReady)
		return;
	const std::vector<VkDescriptorSetLayout> layouts = {
		m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout
	};
	const std::vector<VkDescriptorSet> sets = { m_GlobalSet, m_PassSet };

	TransitionForWrite(commandBuffer, m_SkyViewLut, m_SkyViewReady);
	commandBuffer.EnsureComputeShader(*m_SkyViewShader, layouts);
	commandBuffer.DispatchCompute(
		*m_SkyViewShader,
		DivideRoundUp(m_Quality.skyViewWidth, 8u),
		DivideRoundUp(m_Quality.skyViewHeight, 8u), 1u, sets);
	BarrierForSampling(commandBuffer, m_SkyViewLut);
	m_SkyViewReady = true;

	TransitionForWrite(commandBuffer, m_AerialScattering, m_AerialReady);
	TransitionForWrite(commandBuffer, m_AerialClearScattering, m_AerialReady);
	TransitionForWrite(commandBuffer, m_AerialOpticalDepth, m_AerialReady);
	commandBuffer.EnsureComputeShader(*m_AerialPerspectiveShader, layouts);
	commandBuffer.DispatchCompute(
		*m_AerialPerspectiveShader,
		DivideRoundUp(m_AerialWidth, 8u),
		DivideRoundUp(m_AerialHeight, 8u),
		1u, sets);
	BarrierForSampling(commandBuffer, m_AerialScattering);
	BarrierForSampling(commandBuffer, m_AerialClearScattering);
	BarrierForSampling(commandBuffer, m_AerialOpticalDepth);
	m_AerialReady = true;
}

void VansAtmosphereSystem::RecordComposite(VansVKCommandBuffer& commandBuffer)
{
	if (!m_Initialized || !m_AerialReady || m_GlobalSet == VK_NULL_HANDLE)
		return;
	auto* renderPasses = VansRenderPassManager::GetInstance();
	VansVKImage& color = renderPasses->GetColor();
	VkImageMemoryBarrier toGeneral{};
	toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toGeneral.srcAccessMask = m_FinalColorReady
		? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
	toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toGeneral.oldLayout = m_FinalColorReady
		? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.image = color.GetImage();
	toGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	commandBuffer.PipelineBarrier(
		m_FinalColorReady
			? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
			: VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{}, {}, { toGeneral });
	color.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

	commandBuffer.EnsureComputeShader(
		*m_CompositeShader,
		{ m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout });
	commandBuffer.DispatchCompute(
		*m_CompositeShader,
		DivideRoundUp(m_RenderWidth, 8u),
		DivideRoundUp(m_RenderHeight, 8u), 1u,
		{ m_GlobalSet, m_PassSet });

	VkImageMemoryBarrier toSample = toGeneral;
	toSample.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	toSample.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	commandBuffer.PipelineBarrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		{}, {}, { toSample });
	color.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_FinalColorReady = true;
}

bool VansAtmosphereSystem::ReinitializeViewResources(
	const VansAtmosphereQualityConfig& quality,
	std::uint32_t renderWidth,
	std::uint32_t renderHeight)
{
	if (!m_Initialized || !m_Device)
		return false;
	m_Device->WaitForDevice();
	DestroyDescriptorResources();
	DestroyViewResources();
	m_Quality = quality;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;
	if (!CreateViewResources() || !CreateDescriptorResources())
	{
		Shutdown();
		return false;
	}
	m_StaticLutsDirty = true;
	m_SkyViewReady = false;
	m_AerialReady = false;
	m_FinalColorReady = false;
	return true;
}

void VansAtmosphereSystem::DestroyViewResources()
{
	if (!m_Device)
		return;
	VkDevice device = m_Device->GetLogicDevice();
	m_SkyViewLut.DestroyVulkanImage(device);
	m_AerialScattering.DestroyVulkanImage(device);
	m_AerialClearScattering.DestroyVulkanImage(device);
	m_AerialOpticalDepth.DestroyVulkanImage(device);
	m_SkyViewReady = false;
	m_AerialReady = false;
}

void VansAtmosphereSystem::DestroyDescriptorResources()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	if (m_PassSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> sets = { m_PassSet };
		descriptors->DestroyDescriptorSet(sets);
		m_PassSet = VK_NULL_HANDLE;
	}
	if (m_PassLayout != VK_NULL_HANDLE)
	{
		descriptors->DestroyDescriptorSetLayout(m_PassLayout);
		m_PassLayout = VK_NULL_HANDLE;
	}
}

void VansAtmosphereSystem::Shutdown()
{
	if (!m_Device)
		return;
	VkDevice device = m_Device->GetLogicDevice();
	DestroyDescriptorResources();
	DestroyViewResources();
	m_TransmittanceLut.DestroyVulkanImage(device);
	m_MultiScatteringLut.DestroyVulkanImage(device);
	m_NeutralCloudShadow.DestroyVulkanImage(device);
	m_NeutralCloudResult.DestroyVulkanImage(device);
	m_NeutralLocalMedia.DestroyVulkanImage(device);
	if (m_StaticParamsBufferCreated)
		m_StaticParamsBuffer.DestroyVulkanBuffer(device);
	if (m_FrameParamsBufferCreated)
		m_FrameParamsBuffer.DestroyVulkanBuffer(device);
	m_StaticParamsBufferCreated = false;
	m_FrameParamsBufferCreated = false;
	m_TransmittanceReady = false;
	m_MultiScatteringReady = false;
	m_HasUploadedStaticParams = false;
	m_FinalColorReady = false;
	m_GlobalSet = VK_NULL_HANDLE;
	m_Device = nullptr;
	m_Scene = nullptr;
	m_Initialized = false;
}
}
