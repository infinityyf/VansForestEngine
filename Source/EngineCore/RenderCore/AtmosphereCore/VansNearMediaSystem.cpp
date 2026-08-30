#include "VansNearMediaSystem.h"

#include "../VansCamera.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace VansGraphics
{
namespace
{
	constexpr VkImageUsageFlags NearMediaUsage =
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	std::uint32_t DivideRoundUp(std::uint32_t value, std::uint32_t divisor)
	{
		return (value + divisor - 1u) / divisor;
	}

	glm::vec3 ToVec3(const std::array<float, 3>& value)
	{
		return { value[0], value[1], value[2] };
	}
}

bool VansNearMediaSystem::Initialize(VansVKDevice& device, VansScene& scene,
	const VansNearMediaQualityConfig& quality,
	std::uint32_t renderWidth, std::uint32_t renderHeight)
{
	Shutdown();
	m_Device = &device;
	m_Scene = &scene;
	m_Quality = quality;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;
	m_InjectionShader = VansShaderManager::Get().FindComputeShader("LocalMediaInjection");
	m_IntegrationShader = VansShaderManager::Get().FindComputeShader("LocalMediaIntegration");
	if (!m_InjectionShader || !m_IntegrationShader ||
		!CreateResources() || !CreateDescriptors())
	{
		VANS_LOG_ERROR("[NearMedia] Initialization failed");
		Shutdown();
		return false;
	}
	m_Initialized = true;
	return true;
}

bool VansNearMediaSystem::CreateResources()
{
	if (!m_Device || m_Quality.tileSize == 0 || m_Quality.slices == 0)
		return false;
	m_GridWidth = DivideRoundUp(m_RenderWidth, m_Quality.tileSize);
	m_GridHeight = DivideRoundUp(m_RenderHeight, m_Quality.tileSize);
	const std::size_t tileCount = static_cast<std::size_t>(m_GridWidth) * m_GridHeight;
	m_LocalFogVolumeScratch.reserve(MaxLocalFogVolumes);
	m_LocalFogTileHeaderScratch.reserve(tileCount);
	m_LocalFogTileIndexScratch.reserve(
		tileCount * MaxLocalFogCandidatesPerTile);
	VkDevice device = m_Device->GetLogicDevice();
	m_ParamsBufferCreated = m_ParamsBuffer.CreatVulkanBuffer(
		device, sizeof(VansNearMediaParamsGPU), VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_LocalFogVolumesBufferCreated = m_LocalFogVolumesBuffer.CreatVulkanBuffer(
		device, sizeof(VansLocalFogVolumeGPU) * MaxLocalFogVolumes,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_LocalFogTileHeadersBufferCreated = m_LocalFogTileHeadersBuffer.CreatVulkanBuffer(
		device, sizeof(glm::uvec2) * tileCount, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_LocalFogTileIndicesBufferCreated = m_LocalFogTileIndicesBuffer.CreatVulkanBuffer(
		device, sizeof(std::uint32_t) * tileCount * MaxLocalFogCandidatesPerTile,
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (!m_ParamsBufferCreated || !m_LocalFogVolumesBufferCreated ||
		!m_LocalFogTileHeadersBufferCreated || !m_LocalFogTileIndicesBufferCreated)
		return false;
	const VkExtent3D extent{ m_GridWidth, m_GridHeight, m_Quality.slices };
	for (auto& injection : m_Injection)
	{
		if (!injection.CreateVulkanImage(device, extent,
			VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
			NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
			return false;
	}
	if (!m_Scattering.CreateVulkanImage(device, extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
		NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	return m_OpticalDepth.CreateVulkanImage(device, extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
		NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

bool VansNearMediaSystem::CreateDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	const std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VANS_PUNCTUAL_SHADOW_ATLAS_COUNT, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
	};
	if (!descriptors->CreateDesciptorSetLayout(bindings, m_PassLayout))
		return false;
	std::vector<VkDescriptorSet> sets;
	if (!descriptors->AllocateDescriptorSet(
		{ m_PassLayout, m_PassLayout }, sets,
		VansDescriptorLifetimeRole::ScenePersistent) || sets.size() != 2)
		return false;
	m_PassSets[0] = sets[0];
	m_PassSets[1] = sets[1];
	auto* renderPasses = VansRenderPassManager::GetInstance();
	for (std::uint32_t target = 0; target < 2; ++target)
	{
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(m_PassSets[target], 0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_Injection[target].GetSampler(), m_Injection[target].GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_Injection[target ^ 1u].GetSampler(), m_Injection[target ^ 1u].GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(m_PassSets[target], 2,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansNearMediaParamsGPU) }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPasses->GetCascadeShadowSampler(), renderPasses->GetCascadeShadowArrayView(),
			   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 4,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			renderPasses->GetPunctualShadowDescriptorInfos());
		descriptors->WriteImageDescriptor(m_PassSets[target], 5,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_Scattering.GetSampler(), m_Scattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 6,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_OpticalDepth.GetSampler(), m_OpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(m_PassSets[target], 7,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogVolumesBuffer.GetNativeBuffer(), 0,
			   sizeof(VansLocalFogVolumeGPU) * MaxLocalFogVolumes }});
		const VkDeviceSize tileCount = static_cast<VkDeviceSize>(m_GridWidth) * m_GridHeight;
		descriptors->WriteBufferDescriptor(m_PassSets[target], 8,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogTileHeadersBuffer.GetNativeBuffer(), 0,
			   sizeof(glm::uvec2) * tileCount }});
		descriptors->WriteBufferDescriptor(m_PassSets[target], 9,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogTileIndicesBuffer.GetNativeBuffer(), 0,
			   sizeof(std::uint32_t) * tileCount * MaxLocalFogCandidatesPerTile }});
		descriptors->CommitDescriptorUpdates();
	}
	return true;
}

void VansNearMediaSystem::RefreshLocalFogRegistry()
{
	if (!m_Scene)
		return;
	const std::uint64_t generation =
		m_Scene->GetSceneObjectCollectionGeneration();
	if (m_LocalFogRegistryGeneration == generation)
		return;

	m_LocalFogRegistry.clear();
	for (VansScriptObject* object : m_Scene->GetSceneObjects())
	{
		if (!object)
			continue;
		auto* component =
			object->GetComponent<VansScriptLocalVolumetricFogComponent>();
		if (component)
			m_LocalFogRegistry.push_back({ object, component });
	}
	m_LocalFogRegistryGeneration = generation;
}

void VansNearMediaSystem::CollectLocalFogVolumes(
	std::vector<VansLocalFogVolumeGPU>& outVolumes,
	std::vector<glm::uvec2>& outTileHeaders,
	std::vector<std::uint32_t>& outTileIndices)
{
	outVolumes.clear();
	const std::size_t tileCount =
		static_cast<std::size_t>(m_GridWidth) * m_GridHeight;
	outTileHeaders.resize(tileCount);
	outTileIndices.assign(tileCount * MaxLocalFogCandidatesPerTile, 0u);
	for (std::size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
	{
		outTileHeaders[tileIndex] = glm::uvec2(
			static_cast<std::uint32_t>(tileIndex) *
				MaxLocalFogCandidatesPerTile,
			0u);
	}
	if (!m_Scene)
		return;

	RefreshLocalFogRegistry();
	VansCamera* camera = m_Scene->GetCamera();
	const glm::vec3 cameraWorld = camera
		? glm::vec3(camera->GetPosition()) : glm::vec3(0.0f);
	const glm::mat4 viewProjection = camera
		? camera->GetProjectiveMatrix() * camera->GetViewMatrix()
		: glm::mat4(1.0f);

	for (const LocalFogRegistryEntry& entry : m_LocalFogRegistry)
	{
		VansScriptObject* object = entry.object;
		auto* component = entry.component;
		if (!object || !component || !object->IsActive() ||
			outVolumes.size() >= MaxLocalFogVolumes ||
			!component->IsEffectivelyEnabled() ||
			!VansTransformStore::IsAllocated(object->m_TransformID))
			continue;
		const auto& settings = component->m_Settings;
		const VansTransform& transform =
			VansTransformStore::GetTransform(object->m_TransformID);
		glm::mat4 localToWorld =
			const_cast<VansTransform&>(transform).GetModelMatrix();
		const glm::vec3 dimensions = glm::max(glm::vec3(
			glm::length(glm::vec3(localToWorld[0])),
			glm::length(glm::vec3(localToWorld[1])),
			glm::length(glm::vec3(localToWorld[2]))), glm::vec3(0.001f));
		const glm::mat4 worldToLocal = glm::inverse(localToWorld);

		std::uint32_t minimumTileX = 0u;
		std::uint32_t minimumTileY = 0u;
		std::uint32_t maximumTileX = m_GridWidth - 1u;
		std::uint32_t maximumTileY = m_GridHeight - 1u;
		if (camera)
		{
			const glm::vec3 cameraLocal = glm::vec3(
				worldToLocal * glm::vec4(cameraWorld, 1.0f));
			const bool cameraInside = glm::all(glm::lessThanEqual(
				glm::abs(cameraLocal), glm::vec3(0.5f)));
			glm::vec2 minimumUv(std::numeric_limits<float>::max());
			glm::vec2 maximumUv(-std::numeric_limits<float>::max());
			bool anyInFront = false;
			bool anyBehindNearPlane = false;
			for (int z = 0; z < 2; ++z)
			for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
			{
				const glm::vec3 localCorner(
					x == 0 ? -0.5f : 0.5f,
					y == 0 ? -0.5f : 0.5f,
					z == 0 ? -0.5f : 0.5f);
				const glm::vec4 worldCorner =
					localToWorld * glm::vec4(localCorner, 1.0f);
				const glm::vec4 clip = viewProjection * worldCorner;
				if (clip.w <= 1.0e-4f)
				{
					anyBehindNearPlane = true;
					continue;
				}
				anyInFront = true;
				const glm::vec2 ndc = glm::vec2(clip) / clip.w;
				const glm::vec2 uv(
					ndc.x * 0.5f + 0.5f,
					1.0f - (ndc.y * 0.5f + 0.5f));
				minimumUv = glm::min(minimumUv, uv);
				maximumUv = glm::max(maximumUv, uv);
			}
			if (!anyInFront)
				continue;
			if (!cameraInside && !anyBehindNearPlane)
			{
				if (maximumUv.x < 0.0f || minimumUv.x > 1.0f ||
					maximumUv.y < 0.0f || minimumUv.y > 1.0f)
					continue;
				minimumUv = glm::clamp(minimumUv, glm::vec2(0.0f), glm::vec2(1.0f));
				maximumUv = glm::clamp(maximumUv, glm::vec2(0.0f), glm::vec2(1.0f));
				minimumTileX = (std::min)(static_cast<std::uint32_t>(
					minimumUv.x * static_cast<float>(m_GridWidth)), m_GridWidth - 1u);
				minimumTileY = (std::min)(static_cast<std::uint32_t>(
					minimumUv.y * static_cast<float>(m_GridHeight)), m_GridHeight - 1u);
				maximumTileX = (std::min)(static_cast<std::uint32_t>(
					maximumUv.x * static_cast<float>(m_GridWidth)), m_GridWidth - 1u);
				maximumTileY = (std::min)(static_cast<std::uint32_t>(
					maximumUv.y * static_cast<float>(m_GridHeight)), m_GridHeight - 1u);
			}
		}

		VansLocalFogVolumeGPU volume{};
		volume.worldToLocal = worldToLocal;
		volume.extinctionAnisotropyCloudPadding = glm::vec4(
			1.0f / (std::max)(settings.visibilityDistanceMeters, 0.1f),
			settings.anisotropy,
			settings.receiveCloudShadows ? 1.0f : 0.0f,
			0.0f);
		volume.dimensionsAndEdgeFade = glm::vec4(
			dimensions, (std::max)(settings.edgeFadeDistanceMeters, 0.0f));
		volume.scatteringAlbedo = glm::vec4(
			glm::clamp(ToVec3(settings.singleScatteringAlbedo),
				glm::vec3(0.0f), glm::vec3(1.0f)), 0.0f);
		volume.emissivePerMeter = glm::vec4(
			glm::max(ToVec3(settings.emissivePerMeter), glm::vec3(0.0f)), 0.0f);
		volume.lightingAndDistanceFade = glm::vec4(
			settings.directLightingScale,
			settings.skyLightingScale,
			settings.distanceFadeStartMeters,
			settings.distanceFadeEndMeters);
		const std::uint32_t volumeIndex =
			static_cast<std::uint32_t>(outVolumes.size());
		outVolumes.push_back(volume);

		for (std::uint32_t tileY = minimumTileY; tileY <= maximumTileY; ++tileY)
		for (std::uint32_t tileX = minimumTileX; tileX <= maximumTileX; ++tileX)
		{
			glm::uvec2& header = outTileHeaders[
				static_cast<std::size_t>(tileY) * m_GridWidth + tileX];
			if (header.y == std::numeric_limits<std::uint32_t>::max())
				continue;
			if (header.y >= MaxLocalFogCandidatesPerTile)
			{
				// 极端重叠时让 shader 回退遍历全部可见雾体，保证结果正确。
				header.y = std::numeric_limits<std::uint32_t>::max();
				continue;
			}
			outTileIndices[header.x + header.y] = volumeIndex;
			++header.y;
		}
	}
}

void VansNearMediaSystem::UploadParameters()
{
	CollectLocalFogVolumes(
		m_LocalFogVolumeScratch,
		m_LocalFogTileHeaderScratch,
		m_LocalFogTileIndexScratch);
	const auto& volumes = m_LocalFogVolumeScratch;
	const auto& tileHeaders = m_LocalFogTileHeaderScratch;
	const auto& tileIndices = m_LocalFogTileIndexScratch;
	const auto& environment = m_Scene->GetEnvironmentSettings();
	const float effectiveFarDistanceMeters = environment.heightFog.enabled
		? (std::max)(m_Quality.farDistanceMeters,
			environment.heightFog.maximumDistanceMeters)
		: m_Quality.farDistanceMeters;
	const bool volumesChanged = volumes.size() != m_PreviousVolumes.size() ||
		(!volumes.empty() && std::memcmp(volumes.data(), m_PreviousVolumes.data(),
			volumes.size() * sizeof(VansLocalFogVolumeGPU)) != 0);
	const bool depthRangeChanged = std::abs(effectiveFarDistanceMeters -
		m_PreviousEffectiveFarDistanceMeters) > 0.001f;
	if (volumesChanged || depthRangeChanged)
		m_HistoryValid = false;
	m_PreviousVolumes = volumes;
	m_PreviousEffectiveFarDistanceMeters = effectiveFarDistanceMeters;

	std::array<VansLocalFogVolumeGPU, MaxLocalFogVolumes> upload{};
	std::copy(volumes.begin(), volumes.end(), upload.begin());
	m_LocalFogVolumesBuffer.SetBufferData(
		upload.data(), 0, sizeof(upload));
	m_LocalFogTileHeadersBuffer.SetBufferData(tileHeaders.data(), 0,
		tileHeaders.size() * sizeof(glm::uvec2));
	m_LocalFogTileIndicesBuffer.SetBufferData(tileIndices.data(), 0,
		tileIndices.size() * sizeof(std::uint32_t));

	VansNearMediaParamsGPU params{};
	params.depthRangeAndGrid = glm::vec4(
		m_Quality.nearDistanceMeters,
		effectiveFarDistanceMeters,
		static_cast<float>(m_GridWidth),
		static_cast<float>(m_GridHeight));
	params.sliceHistoryAndVolumeCount = glm::vec4(
		m_Quality.sliceDistributionPower,
		(m_HistoryValid && m_Quality.temporalReprojection) ? 1.0f : 0.0f,
		m_Quality.historyWeight,
		static_cast<float>(volumes.size()));
	params.localFogTileGridAndLimits = glm::vec4(
		static_cast<float>(m_GridWidth),
		static_cast<float>(m_GridHeight),
		static_cast<float>(MaxLocalFogCandidatesPerTile),
		0.0f);
	m_ParamsBuffer.SetBufferData(&params, 0, sizeof(params));
}

void VansNearMediaSystem::BindGlobalDescriptor(VkDescriptorSet globalSet)
{
	if (!m_ParamsBufferCreated || globalSet == VK_NULL_HANDLE)
		return;
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	descriptors->BeginDescriptorUpdate();
	descriptors->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_LOCAL_MEDIA_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansNearMediaParamsGPU) }});
	descriptors->CommitDescriptorUpdates();
}

void VansNearMediaSystem::TransitionForWrite(VansVKCommandBuffer& commandBuffer,
	VansVKImage& image, bool initialized)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = initialized ?
		(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) : 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.oldLayout = initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	commandBuffer.PipelineBarrier(
		initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { barrier });
	image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VansNearMediaSystem::BarrierForSampling(
	VansVKCommandBuffer& commandBuffer, VansVKImage& image)
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
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { barrier });
}

void VansNearMediaSystem::Record(VansVKCommandBuffer& commandBuffer)
{
	if (!m_Initialized)
		return;
	UploadParameters();
	const std::uint32_t target = m_FrameParity & 1u;
	TransitionForWrite(commandBuffer, m_Injection[target], m_HistoryValid);
	commandBuffer.EnsureComputeShader(*m_InjectionShader,
		{ m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout });
	commandBuffer.DispatchCompute(*m_InjectionShader,
		DivideRoundUp(m_GridWidth, 4u), DivideRoundUp(m_GridHeight, 4u),
		DivideRoundUp(m_Quality.slices, 4u),
		{ m_Scene->GetGlobalDescriptorSet(), m_PassSets[target] });
	BarrierForSampling(commandBuffer, m_Injection[target]);
	TransitionForWrite(commandBuffer, m_Scattering, m_OutputInitialized);
	TransitionForWrite(commandBuffer, m_OpticalDepth, m_OutputInitialized);
	commandBuffer.EnsureComputeShader(*m_IntegrationShader,
		{ m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout });
	commandBuffer.DispatchCompute(*m_IntegrationShader,
		DivideRoundUp(m_GridWidth, 8u), DivideRoundUp(m_GridHeight, 8u), 1u,
		{ m_Scene->GetGlobalDescriptorSet(), m_PassSets[target] });
	BarrierForSampling(commandBuffer, m_Scattering);
	BarrierForSampling(commandBuffer, m_OpticalDepth);
	m_HistoryValid = true;
	m_OutputInitialized = true;
	m_FrameParity ^= 1u;
}

VkDescriptorImageInfo VansNearMediaSystem::GetScatteringDescriptor()
{
	return { m_Scattering.GetSampler(), m_Scattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

VkDescriptorImageInfo VansNearMediaSystem::GetOpticalDepthDescriptor()
{
	return { m_OpticalDepth.GetSampler(), m_OpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

VkDescriptorImageInfo VansNearMediaSystem::GetCurrentInjectionDescriptor()
{
	const std::uint32_t current = m_HistoryValid ?
		((m_FrameParity ^ 1u) & 1u) : 0u;
	return { m_Injection[current].GetSampler(),
		m_Injection[current].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

bool VansNearMediaSystem::Reinitialize(const VansNearMediaQualityConfig& quality,
	std::uint32_t renderWidth, std::uint32_t renderHeight)
{
	if (!m_Device || !m_Scene)
		return false;
	VansVKDevice* device = m_Device;
	VansScene* scene = m_Scene;
	device->WaitForDevice();
	return Initialize(*device, *scene, quality, renderWidth, renderHeight);
}

void VansNearMediaSystem::DestroyDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	std::vector<VkDescriptorSet> sets;
	for (VkDescriptorSet& set : m_PassSets)
	{
		if (set != VK_NULL_HANDLE)
			sets.push_back(set);
		set = VK_NULL_HANDLE;
	}
	if (!sets.empty())
		descriptors->DestroyDescriptorSet(sets);
	if (m_PassLayout != VK_NULL_HANDLE)
	{
		descriptors->DestroyDescriptorSetLayout(m_PassLayout);
		m_PassLayout = VK_NULL_HANDLE;
	}
}

void VansNearMediaSystem::DestroyResources()
{
	if (!m_Device)
		return;
	VkDevice device = m_Device->GetLogicDevice();
	for (auto& injection : m_Injection)
		injection.DestroyVulkanImage(device);
	m_Scattering.DestroyVulkanImage(device);
	m_OpticalDepth.DestroyVulkanImage(device);
	if (m_ParamsBufferCreated)
		m_ParamsBuffer.DestroyVulkanBuffer(device);
	if (m_LocalFogVolumesBufferCreated)
		m_LocalFogVolumesBuffer.DestroyVulkanBuffer(device);
	if (m_LocalFogTileHeadersBufferCreated)
		m_LocalFogTileHeadersBuffer.DestroyVulkanBuffer(device);
	if (m_LocalFogTileIndicesBufferCreated)
		m_LocalFogTileIndicesBuffer.DestroyVulkanBuffer(device);
	m_ParamsBufferCreated = false;
	m_LocalFogVolumesBufferCreated = false;
	m_LocalFogTileHeadersBufferCreated = false;
	m_LocalFogTileIndicesBufferCreated = false;
}

void VansNearMediaSystem::Shutdown()
{
	if (m_Device)
	{
		DestroyDescriptors();
		DestroyResources();
	}
	m_Device = nullptr;
	m_Scene = nullptr;
	m_LocalFogRegistry.clear();
	m_LocalFogVolumeScratch.clear();
	m_LocalFogTileHeaderScratch.clear();
	m_LocalFogTileIndexScratch.clear();
	m_LocalFogRegistryGeneration = UINT64_MAX;
	m_PreviousVolumes.clear();
	m_PreviousEffectiveFarDistanceMeters = 0.0f;
	m_HistoryValid = false;
	m_OutputInitialized = false;
	m_FrameParity = 0;
	m_Initialized = false;
}
}
