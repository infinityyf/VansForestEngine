#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansReflectionProbeSystem.h"
#include "VansReflectionProbePublication.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansVKMemoryManager.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include <queue>
#include <limits>
#include <system_error>
#include <fstream>
#include <../../GLM/gtc/matrix_transform.hpp>
#include "VansReflectionProbeCache.h"

namespace fs = std::filesystem;

namespace VansGraphics
{
	namespace
	{
		void NameProbeResource(VkDevice device, VkObjectType type, uint64_t handle, const std::string& name)
		{
#ifdef _DEBUG
			if (!handle || !vkSetDebugUtilsObjectNameEXT) return;
			VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
			info.objectType = type; info.objectHandle = handle; info.pObjectName = name.c_str();
			vkSetDebugUtilsObjectNameEXT(device, &info);
#endif
		}

		struct CaptureCameraData
		{
			glm::mat4 viewProjection;
			glm::mat4 inverseViewProjection;
			glm::vec4 position;
			glm::vec4 giVolumeMin;
			glm::vec4 giVolumeSizeAndBias;
			glm::vec4 giGridDimensions;
		};

		struct CaptureDrawData
		{
			glm::mat4 model;
			glm::vec4 albedo;
			glm::vec4 emissive;
			glm::vec4 params;
		};

		glm::vec3 ToVec3(const std::array<float, 3>& value)
		{
			return glm::vec3(value[0], value[1], value[2]);
		}

		template <typename T>
		void ApplyOptional(const std::optional<T>& source, T& destination)
		{
			if (source.has_value())
				destination = *source;
		}

		uint32_t NormalizeResolution(uint32_t value)
		{
			value = std::clamp(value, 32u, 512u);
			uint32_t result = 1;
			while (result < value) result <<= 1;
			return result;
		}
	}

	VansReflectionProbeSystem::VansReflectionProbeSystem() : m_State(std::make_unique<RuntimeState>()) {}

	VansReflectionProbeSystem::~VansReflectionProbeSystem()
	{
		if (m_State->m_Device != VK_NULL_HANDLE) Clear(m_State->m_Device);
	}

	void VansReflectionProbeSystem::LoadFromSceneConfig(
		const Vans::VansSceneReflectionProbeConfig& config,
		const std::string& scenePath)
	{
		m_State->config.m_Probes.clear(); m_State->m_BakeResults.clear(); m_State->m_BakeQueue.clear();
		m_State->config.m_PlacementSettings = {}; m_State->config.m_LightingSettings = {}; m_State->config.m_PlacementResult = {}; m_State->m_PlacementError.clear();
		std::vector<VansReflectionProbeDesc> overrides;
		m_State->config.m_ScenePath = scenePath;
		if (config.hasBlock)
		{
			if (config.lighting.maxBlendCount.has_value())
				m_State->config.m_LightingSettings.maxBlendCount = std::clamp(*config.lighting.maxBlendCount, 1u, 4u);
			ApplyOptional(config.lighting.ssrRoughnessFadeStart, m_State->config.m_LightingSettings.ssrRoughnessFadeStart);
			ApplyOptional(config.lighting.ssrRoughnessFadeEnd, m_State->config.m_LightingSettings.ssrRoughnessFadeEnd);


			ApplyOptional(config.placement.enabled, m_State->config.m_PlacementSettings.enabled);
			m_State->config.m_PlacementSettings.geometryOnly = config.placement.geometryOnly.value_or(true);
			if (config.placement.volumeMin.has_value()) m_State->config.m_PlacementSettings.volumeMin = ToVec3(*config.placement.volumeMin);
			if (config.placement.volumeMax.has_value()) m_State->config.m_PlacementSettings.volumeMax = ToVec3(*config.placement.volumeMax);
			ApplyOptional(config.placement.cellSize, m_State->config.m_PlacementSettings.cellSize);
			ApplyOptional(config.placement.minCaptureClearance, m_State->config.m_PlacementSettings.minCaptureClearance);
			ApplyOptional(config.placement.indoorSpacing, m_State->config.m_PlacementSettings.indoorSpacing);
			ApplyOptional(config.placement.corridorSpacing, m_State->config.m_PlacementSettings.corridorSpacing);
			ApplyOptional(config.placement.outdoorSpacing, m_State->config.m_PlacementSettings.outdoorSpacing);
			ApplyOptional(config.placement.solidThreshold, m_State->config.m_PlacementSettings.solidThreshold);
			ApplyOptional(config.placement.refinementThreshold, m_State->config.m_PlacementSettings.refinementThreshold);
			ApplyOptional(config.placement.maxProbeCount, m_State->config.m_PlacementSettings.maxProbeCount);
			if (config.placement.uniformSpacing.has_value())
				m_State->config.m_PlacementSettings.uniformSpacing = std::max(*config.placement.uniformSpacing, 0.5f);
			if (config.placement.uniformBoxSizeScale.has_value())
				m_State->config.m_PlacementSettings.uniformBoxSizeScale = std::clamp(*config.placement.uniformBoxSizeScale, 0.05f, 1.0f);
			if (config.placement.uniformProbeResolution.has_value())
				m_State->config.m_PlacementSettings.uniformProbeResolution = NormalizeResolution(*config.placement.uniformProbeResolution);

			auto decodeProbe = [&](const Vans::VansSceneReflectionProbeDescConfig& probeConfig)
			{
				VansReflectionProbeDesc probe;
				ApplyOptional(probeConfig.name, probe.name);
				if (probeConfig.type.has_value())
				{
					const std::string& type = *probeConfig.type;
					probe.type = type == "realtime" ? ReflectionProbeType::Realtime :
						(type == "sky" ? ReflectionProbeType::Sky : ReflectionProbeType::Baked);
				}
				if (probeConfig.shape.has_value())
					probe.shape = *probeConfig.shape == "sphere" ? ReflectionProbeShape::Sphere : ReflectionProbeShape::Box;
				if (probeConfig.refreshMode.has_value())
				{
					const std::string& refresh = *probeConfig.refreshMode;
					probe.refreshMode = refresh == "every_frame" ? ReflectionProbeRefreshMode::EveryFrame :
						(refresh == "time_sliced" ? ReflectionProbeRefreshMode::TimeSliced :
						(refresh == "on_demand" ? ReflectionProbeRefreshMode::OnDemand : ReflectionProbeRefreshMode::OnLoad));
				}
				if (probeConfig.position.has_value()) probe.position = ToVec3(*probeConfig.position);
				probe.capturePosition = probeConfig.capturePosition.has_value() ? ToVec3(*probeConfig.capturePosition) : probe.position;
				if (probeConfig.boxMin.has_value()) probe.boxMin = ToVec3(*probeConfig.boxMin);
				if (probeConfig.boxMax.has_value()) probe.boxMax = ToVec3(*probeConfig.boxMax);
				if (probeConfig.radius.has_value()) probe.radius = std::max(*probeConfig.radius, 0.01f);
				if (probeConfig.blendDistance.has_value()) probe.blendDistance = std::max(*probeConfig.blendDistance, 0.001f);
				ApplyOptional(probeConfig.priority, probe.priority);
				if (probeConfig.intensity.has_value()) probe.intensity = std::max(*probeConfig.intensity, 0.0f);
				if (probeConfig.specularIntensity.has_value()) probe.specularIntensity = std::max(*probeConfig.specularIntensity, 0.0f);
				if (probeConfig.nearPlane.has_value()) probe.nearPlane = std::max(*probeConfig.nearPlane, 0.001f);
				if (probeConfig.farPlane.has_value()) probe.farPlane = std::max(*probeConfig.farPlane, probe.nearPlane + 0.01f);
				if (probeConfig.resolution.has_value()) probe.resolution = NormalizeResolution(*probeConfig.resolution);
				ApplyOptional(probeConfig.cullingMask, probe.cullingMask);
				ApplyOptional(probeConfig.regionId, probe.regionId);
				if (probeConfig.facesPerFrame.has_value())
					probe.realtimeFacesPerFrame = std::clamp(*probeConfig.facesPerFrame, 1u, 6u);
				ApplyOptional(probeConfig.enabled, probe.enabled);
				ApplyOptional(probeConfig.boxProjection, probe.boxProjection);
				ApplyOptional(probeConfig.autoGenerated, probe.autoGenerated);
				if (probe.autoGenerated)
					probe.boxProjection = false;
				ApplyOptional(probeConfig.portal, probe.portal);
				ApplyOptional(probeConfig.cachePath, probe.cachePath);
				return probe;
			};
			for (const auto& probe : config.probes) m_State->config.m_Probes.push_back(decodeProbe(probe));
			for (const auto& probe : config.placementOverrides)
			{
				auto decoded = decodeProbe(probe); decoded.autoGenerated = false;
				if (decoded.type != ReflectionProbeType::Sky) overrides.push_back(std::move(decoded));
			}
		}
		EnsureDefaults();
		m_State->config.m_Layout.Reset(m_State->config.m_Probes, std::move(overrides));
		m_State->config.m_Layout.BuildActive(false, m_State->config.m_Probes, m_State->config.m_ProbeOrigins);
		m_State->m_BakeResults.resize(m_State->config.m_Probes.size());
		for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
		{
			m_State->m_BakeResults[i].arrayLayer = (uint32_t)i;
			m_State->m_BakeResults[i].cachePath = m_State->config.m_Probes[i].cachePath;
			m_State->m_BakeResults[i].dirty = m_State->config.m_Probes[i].type != ReflectionProbeType::Sky;
		}
	}

	void VansReflectionProbeSystem::EnsureDefaults()
	{
		m_State->config.m_Probes.erase(std::remove_if(m_State->config.m_Probes.begin(), m_State->config.m_Probes.end(), [](const auto& p) { return p.type == ReflectionProbeType::Sky; }), m_State->config.m_Probes.end());
		VansReflectionProbeDesc sky;
		sky.name = "Sky Fallback"; sky.type = ReflectionProbeType::Sky; sky.shape = ReflectionProbeShape::Sphere;
		sky.radius = 0.0f; sky.priority = -100000.0f; sky.boxProjection = false; sky.enabled = true;
		m_State->config.m_Probes.push_back(sky);
	}

	void VansReflectionProbeSystem::Clear(VkDevice device)
	{
		VANS_LOG("[ReflectionProbe] Releasing scene GPU resources");
		DestroyCaptureResources();
		VANS_LOG("[ReflectionProbe] Capture resources released");

		// Pipelines retain pipeline layouts derived from the descriptor layout.
		// Destroy them before releasing descriptor and image-view dependencies.
		if (m_State->m_PrefilterShader)
			m_State->m_PrefilterShader->TriggerReCreateComputePipeline();
		m_State->m_PrefilterShader = nullptr;
		VANS_LOG("[ReflectionProbe] Prefilter pipeline released");

		auto* descriptors = VansVKDescriptorManager::GetInstance();
		if (!m_State->m_PrefilterSets.empty()) descriptors->DestroyDescriptorSet(m_State->m_PrefilterSets);
		if (m_State->m_PrefilterLayout != VK_NULL_HANDLE) descriptors->ReleaseDescriptorSetLayout(m_State->m_PrefilterLayout);
		for (VkImageView& view : m_State->m_PrefilterMipViews) VansVKImage::DestroyImageView(device, view);
		for (VkImageView& view : m_State->m_PrefilterSourceViews) VansVKImage::DestroyImageView(device, view);
		m_State->m_PrefilterSourceViews.clear();
		m_State->m_PrefilterOffsets.clear();
		m_State->m_PrefilterSets.clear(); m_State->m_PrefilterMipViews.clear(); m_State->m_PrefilterLayout = VK_NULL_HANDLE;
		VANS_LOG("[ReflectionProbe] Prefilter descriptors and views released");
		for (VkImageView& view : m_State->m_EditorPreviewFaceViews) VansVKImage::DestroyImageView(device, view);
		m_State->m_EditorPreviewFaceViews.clear();
		m_State->m_MetadataBuffer.DestroyVulkanBuffer(device);
		m_State->m_SpatialIndexBuffer.DestroyVulkanBuffer(device);
		m_State->m_SpatialIndex.Clear();
		m_State->m_GlobalDescriptorSet = VK_NULL_HANDLE;
		VANS_LOG("[ReflectionProbe] Metadata buffer released");
		m_State->m_SpecularPages.clear(); m_State->m_CaptureTexture.reset(); m_State->m_PageLayout = {};
		VANS_LOG("[ReflectionProbe] Specular pages released");
		m_State->m_Device = VK_NULL_HANDLE;
		m_State->m_GPUProbes.clear(); m_State->m_BakeQueue.clear();
		m_State->m_ActiveBakeIndex = size_t(-1); m_State->m_ActiveBakeFace = 0;
		VANS_LOG("[ReflectionProbe] Scene GPU resources released");
	}

	VkImageView VansReflectionProbeSystem::GetPreviewFaceView(size_t probeIndex, uint32_t face, uint32_t mipLevel)
	{
		if (!GetProbeTexture(probeIndex) || m_State->m_Device == VK_NULL_HANDLE || probeIndex >= m_State->m_BakeResults.size() || face >= 6)
			return VK_NULL_HANDLE;

		const auto& result = m_State->m_BakeResults[probeIndex];
		mipLevel = std::min(mipLevel, result.mipCount > 0 ? result.mipCount - 1 : 0);
		const uint32_t arrayLayer = result.arrayLayer * 6u + face;
		auto& image = GetProbeTexture(probeIndex)->GetImage();
		if (arrayLayer >= image.GetImageCreateInfo().arrayLayers ||
			mipLevel >= image.GetImageCreateInfo().mipLevels)
		{
			return VK_NULL_HANDLE;
		}

		const size_t key = (probeIndex * 6u + face) * m_State->m_CaptureMipCount + mipLevel;
		if (m_State->m_EditorPreviewFaceViews.size() <= key)
			m_State->m_EditorPreviewFaceViews.resize(key + 1, VK_NULL_HANDLE);
		if (m_State->m_EditorPreviewFaceViews[key] == VK_NULL_HANDLE)
			m_State->m_EditorPreviewFaceViews[key] = image.CreateLayerMipView(m_State->m_Device, arrayLayer, mipLevel);
		return m_State->m_EditorPreviewFaceViews[key];
	}

	bool VansReflectionProbeSystem::ClearAutoProbes(const VansScene& scene, VansVKDevice& device)
	{
		auto placement = m_State->config.m_PlacementSettings;
		placement.enabled = false;
		return ApplySettings(scene, device, placement, m_State->config.m_LightingSettings,
			m_State->config.m_EditorState, m_State->config.m_Probes, false);
	}

	void VansReflectionProbeSystem::ConvertToManual(size_t index)
	{
		if (index >= m_State->config.m_Probes.size() || index >= m_State->config.m_ProbeOrigins.size() || m_State->config.m_Probes[index].type == ReflectionProbeType::Sky) return;
		m_State->config.m_ProbeOrigins[index] = m_State->config.m_Layout.Pin(m_State->config.m_ProbeOrigins[index], m_State->config.m_Probes[index]);
		m_State->config.m_Probes[index].autoGenerated = false;
	}

	namespace
	{
		bool EditableProbeEquals(const VansReflectionProbeDesc& a, const VansReflectionProbeDesc& b)
		{
			return a.name == b.name && a.resolution == b.resolution &&
				a.position == b.position && a.capturePosition == b.capturePosition && a.boxMin == b.boxMin
				&& a.boxMax == b.boxMax && a.radius == b.radius && a.blendDistance == b.blendDistance
				&& a.priority == b.priority && a.intensity == b.intensity && a.specularIntensity == b.specularIntensity
				&& a.enabled == b.enabled && a.boxProjection == b.boxProjection;
		}
		bool PlacementEquals(const ReflectionProbePlacementSettings& a, const ReflectionProbePlacementSettings& b)
		{
			return a.enabled == b.enabled && a.volumeMin == b.volumeMin && a.volumeMax == b.volumeMax
				&& a.cellSize == b.cellSize && a.minCaptureClearance == b.minCaptureClearance
				&& a.indoorSpacing == b.indoorSpacing && a.corridorSpacing == b.corridorSpacing
				&& a.outdoorSpacing == b.outdoorSpacing && a.solidThreshold == b.solidThreshold
				&& a.refinementThreshold == b.refinementThreshold && a.maxProbeCount == b.maxProbeCount
				&& a.uniformProbeResolution == b.uniformProbeResolution;
		}
	}

	void VansReflectionProbeSystem::CopyConfigurationTo(VansReflectionProbeSystem& pending) const
	{
		pending.m_State->config = m_State->config;
        pending.m_State->skyLightingSourceKey = m_State->skyLightingSourceKey;
		pending.m_State->m_BakeResults = m_State->m_BakeResults;
		pending.m_State->m_PageLayout = m_State->m_PageLayout;
		pending.m_State->m_BakeRevision = m_State->m_BakeRevision;
	}

	void VansReflectionProbeSystem::ReleaseSharedPipelines()
	{
		// 注册表中的 shader 是共享对象。只在 idle 发布点失效旧 pipeline，
		// 延迟销毁旧纹理时不能再使新布局正在使用的 pipeline 失效。
		if (m_State->m_PrefilterShader) m_State->m_PrefilterShader->TriggerReCreateComputePipeline();
		if (m_State->m_CaptureGeometryShader) m_State->m_CaptureGeometryShader->TriggerReCreateGraphicsPipeline();
		if (m_State->m_CaptureSkyShader) m_State->m_CaptureSkyShader->TriggerReCreateGraphicsPipeline();
		m_State->m_PrefilterShader = nullptr;
		m_State->m_CaptureGeometryShader = nullptr;
		m_State->m_CaptureSkyShader = nullptr;
	}

	void VansReflectionProbeSystem::CommitResources(VansVKDevice& device,
		const std::shared_ptr<VansReflectionProbeSystem>& pending)
	{
		// 先准备退役记录；若分配记录失败，当前描述符和布局仍未被修改。
		// 当前 GUI 帧可能已引用旧 face view，沿用现有帧完成后的释放机制。
		device.EnqueueDeferredDelete([pending]() {});
		pending->UpdateGlobalDescriptors(m_State->m_GlobalDescriptorSet);
		m_State.swap(pending->m_State);
		pending->ReleaseSharedPipelines();
	}

	bool VansReflectionProbeSystem::ApplySettings(const VansScene& scene, VansVKDevice& device,
		const ReflectionProbePlacementSettings& placement, const ReflectionProbeLightingSettings& lighting,
		const ReflectionProbeEditorState& editor, const std::vector<VansReflectionProbeDesc>& edits,
		bool forceRegenerate)
	{
		auto pending = std::make_shared<VansReflectionProbeSystem>();
		try
		{
			CopyConfigurationTo(*pending);
			bool rebuild = false;
			if (!pending->PrepareSettings(scene, device, placement, lighting, editor, edits, forceRegenerate, rebuild))
			{ m_State->m_PlacementError = pending->m_State->m_PlacementError; return false; }
			if (m_State->m_Device == VK_NULL_HANDLE)
			{
				m_State.swap(pending->m_State);
				return true;
			}
			if (rebuild)
			{
				if (!pending->InitializeGPUResources(device, device.GetImmediateGraphicsCommandBuffer()))
				{
					m_State->m_PlacementError = pending->m_State->m_PlacementError;
					// 尚未发布的候选没有创建共享 pipeline，不能销毁当前布局的 pipeline。
					pending->m_State->m_PrefilterShader = nullptr;
					return false;
				}
				CommitResources(device, pending);
			}
			else
			{
				// 仅编辑影响域、权重或 UI 时保留捕获纹理及在途捕获进度。
				// 新元数据与查询索引先完整生成，再一起发布，不原地缩容或扩容。
				if (!pending->InitializeMetadataResources(device))
				{ m_State->m_PlacementError = pending->m_State->m_PlacementError; return false; }
				pending->WriteGlobalDescriptors(m_State->m_GlobalDescriptorSet, m_State->m_SpecularPages);
				const size_t active = m_State->m_ActiveBakeIndex;
				if (active < m_State->config.m_Probes.size() &&
					m_State->config.m_Probes[active].capturePosition != pending->m_State->config.m_Probes[active].capturePosition)
					m_State->m_ActiveBakeFace = 0;
				using std::swap;
				swap(m_State->config, pending->m_State->config);
				swap(m_State->m_BakeResults, pending->m_State->m_BakeResults);
				swap(m_State->m_Header, pending->m_State->m_Header);
				swap(m_State->m_GPUProbes, pending->m_State->m_GPUProbes);
				swap(m_State->m_MetadataBuffer, pending->m_State->m_MetadataBuffer);
				swap(m_State->m_SpatialIndex, pending->m_State->m_SpatialIndex);
				swap(m_State->m_SpatialIndexBuffer, pending->m_State->m_SpatialIndexBuffer);
				m_State->m_PlacementError.clear();
			}
			return true;
		}
		catch (const std::exception& error)
		{
			pending->m_State->m_PrefilterShader = nullptr;
			m_State->m_PlacementError = error.what();
			VANS_LOG_ERROR("[ReflectionProbe] Settings preparation failed: " << m_State->m_PlacementError);
			return false;
		}
	}

	bool VansReflectionProbeSystem::PrepareSettings(const VansScene& scene, VansVKDevice& device,
		const ReflectionProbePlacementSettings& placement, const ReflectionProbeLightingSettings& lighting,
		const ReflectionProbeEditorState& editor, const std::vector<VansReflectionProbeDesc>& edits,
		bool forceRegenerate, bool& rebuildResources)
	{
		rebuildResources = false;
		auto pending = m_State->config.m_Layout;
		bool overridesChanged = false;
		for (size_t i = 0; i < std::min({edits.size(), m_State->config.m_Probes.size(), m_State->config.m_ProbeOrigins.size()}); ++i)
		{
			if (EditableProbeEquals(edits[i], m_State->config.m_Probes[i])) continue;
			if (pending.Edit(m_State->config.m_ProbeOrigins[i], edits[i]))
				overridesChanged |= m_State->config.m_ProbeOrigins[i].source == VansReflectionProbeSource::Override;
		}
		const bool regenerate = placement.enabled && (forceRegenerate || !PlacementEquals(placement, m_State->config.m_PlacementSettings) || overridesChanged);
		VansReflectionProbePlacementResult generated;
		if (regenerate)
		{
			VansSceneGeometrySnapshot geometry;
			if (!VansSceneGeometrySnapshot::Capture(scene, device, geometry, m_State->m_PlacementError)
				|| !VansReflectionProbePlacement::Generate(geometry, placement, pending.Overrides(), generated, m_State->m_PlacementError))
			{
				VANS_LOG_ERROR("[ReflectionProbe] Automatic placement failed: " << m_State->m_PlacementError);
				return false;
			}
			VANS_LOG("[ReflectionProbe] Surface placement: staticInstances=" << geometry.staticInstanceCount
				<< " receivers=" << generated.receiverCount << " covered=" << generated.coveredReceiverCount
				<< " generated=" << generated.probes.size() << " surfaceCoverage=" << generated.coveredSurfaceFraction
				<< " candidates=" << generated.candidateCount << " reachableSurface=" << generated.reachableSurfaceFraction);
			pending.SetGenerated(std::move(generated.probes));
		}
		std::vector<VansReflectionProbeDesc> active;
		std::vector<VansReflectionProbeOrigin> origins;
		pending.BuildActive(placement.enabled, active, origins);
		std::vector<uint32_t> resolutions;
		for (const auto& probe : active)
			resolutions.push_back(probe.type == ReflectionProbeType::Sky ? 0u : NormalizeResolution(probe.resolution));
		VansReflectionProbePageLayout capacity;
		if (!capacity.Build(resolutions, device.GetDeviceProperties().limits.maxImageArrayLayers, m_State->m_PlacementError))
			return false;
		rebuildResources = regenerate || placement.enabled != m_State->config.m_PlacementSettings.enabled || active.size() != m_State->config.m_Probes.size();
		if (!rebuildResources)
			for (size_t i = 0; i < active.size(); ++i)
				rebuildResources |= active[i].name != m_State->config.m_Probes[i].name || active[i].resolution != m_State->config.m_Probes[i].resolution;
		if (rebuildResources)
		{
			m_State->m_BakeResults.assign(active.size(), {});
			m_State->m_BakeQueue.clear(); m_State->m_ActiveBakeIndex = size_t(-1); m_State->m_ActiveBakeFace = 0;
		}
		else
			for (size_t i = 0; i < active.size(); ++i)
				if (active[i].capturePosition != m_State->config.m_Probes[i].capturePosition) MarkDirty(i);
		m_State->config.m_Layout = std::move(pending); m_State->config.m_Probes = std::move(active); m_State->config.m_ProbeOrigins = std::move(origins);
		m_State->config.m_PlacementSettings = placement; m_State->config.m_LightingSettings = lighting; m_State->config.m_EditorState = editor;
		if (regenerate) m_State->config.m_PlacementResult = std::move(generated);
		if (rebuildResources) m_State->config.m_EditorState.selectedProbeIndex = -1;
		m_State->m_PlacementError.clear();
		return true;
	}

	bool VansReflectionProbeSystem::GenerateAutoProbes(const VansScene& scene, VansVKDevice& device)
	{
		auto placement = m_State->config.m_PlacementSettings; placement.enabled = true;
		return ApplySettings(scene, device, placement, m_State->config.m_LightingSettings, m_State->config.m_EditorState, m_State->config.m_Probes, true);
	}

	std::vector<std::string> VansReflectionProbeSystem::ValidatePlacement() const
	{
		std::vector<std::string> errors;
		if (!m_State->m_PlacementError.empty()) errors.push_back(m_State->m_PlacementError);
		if (m_State->config.m_PlacementSettings.enabled && m_State->config.m_PlacementResult.coveredReceiverCount < m_State->config.m_PlacementResult.receiverCount)
			errors.push_back("Automatic layout leaves " + std::to_string(m_State->config.m_PlacementResult.receiverCount - m_State->config.m_PlacementResult.coveredReceiverCount)
				+ " receiver samples uncovered" + (m_State->config.m_PlacementResult.budgetExhausted ? " (probe budget exhausted)" : " (coverage tolerance reached or no valid capture found)"));
		for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
		{
			const auto& p = m_State->config.m_Probes[i];
			if (p.type == ReflectionProbeType::Sky) continue;
			if (p.shape == ReflectionProbeShape::Box && glm::any(glm::lessThanEqual(p.boxMax, p.boxMin))) errors.push_back(p.name + ": invalid box bounds");
			if (p.shape == ReflectionProbeShape::Sphere && p.radius <= 0.0f) errors.push_back(p.name + ": radius must be positive");
			if (p.blendDistance <= 0.0f) errors.push_back(p.name + ": blend distance must be positive");
			if (p.capturePosition.x < p.boxMin.x || p.capturePosition.y < p.boxMin.y || p.capturePosition.z < p.boxMin.z ||
				p.capturePosition.x > p.boxMax.x || p.capturePosition.y > p.boxMax.y || p.capturePosition.z > p.boxMax.z)
				if (p.shape == ReflectionProbeShape::Box) errors.push_back(p.name + ": capture position is outside influence box");
			if (!m_State->config.m_PlacementGrid.cells.empty())
			{
				glm::ivec3 cell = glm::ivec3(glm::floor((p.capturePosition - m_State->config.m_PlacementGrid.origin) / m_State->config.m_PlacementGrid.cellSize));
				if (glm::all(glm::greaterThanEqual(cell, glm::ivec3(0))) && glm::all(glm::lessThan(cell, glm::ivec3(m_State->config.m_PlacementGrid.dimensions))))
				{
					uint32_t index = cell.x + m_State->config.m_PlacementGrid.dimensions.x * (cell.y + m_State->config.m_PlacementGrid.dimensions.y * cell.z);
					if (m_State->config.m_PlacementGrid.cells[index].cellClass == ProbeCellClass::Solid) errors.push_back(p.name + ": capture position intersects static geometry");
					uint32_t cellRegion = m_State->config.m_PlacementGrid.cells[index].regionId;
					if (!p.portal && p.regionId != 0xffffffffu && cellRegion != 0xffffffffu && p.regionId != cellRegion) errors.push_back(p.name + ": capture position is assigned to a different region");
				}
			}
		}
		for (size_t i = 0; i < m_State->config.m_GeometryErrors.size(); ++i)
			if (m_State->config.m_GeometryErrors[i].uncoveredCellRatio > m_State->config.m_PlacementSettings.refinementThreshold)
				errors.push_back("Region " + std::to_string(i < m_State->config.m_Regions.size() ? m_State->config.m_Regions[i].id : (uint32_t)i) + ": uncovered cell ratio " + std::to_string(m_State->config.m_GeometryErrors[i].uncoveredCellRatio));
		return errors;
	}

	bool VansReflectionProbeSystem::LoadCachedProbes(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
    {
        // CPU 解码与已有 staging 上传都分批，不随全场 probe 数量积累整场纹理副本。
        constexpr uint64_t DecodeBudgetBytes = 32u * 1024u * 1024u;
        std::vector<VansReflectionProbeCacheData> decoded;
        std::vector<VansTextureMipChainUpload> uploads;
        std::vector<size_t> indices;
        decoded.reserve(128); uploads.reserve(128); indices.reserve(128);
        uint64_t decodedBytes = 0, loaded = 0;
        const auto flush = [&]() {
            if (uploads.empty()) return true;
            if (!device.SubmitTextureMipChainUploadBatch(commandBuffer, uploads)) return false;
            for (size_t index : indices) m_State->m_BakeResults[index].valid = true;
            loaded += indices.size();
            uploads.clear(); decoded.clear(); indices.clear(); decodedBytes = 0;
            return true;
        };
        for (size_t index = 0; index < m_State->config.m_Probes.size(); ++index)
        {
            auto& probe = m_State->config.m_Probes[index];
            if (probe.type == ReflectionProbeType::Sky) continue;
            fs::path base = probe.cachePath;
            if (base.empty())
            {
                base = fs::path(m_State->config.m_ScenePath).parent_path() / "ReflectionProbes" / probe.name;
                probe.cachePath = base.string();
                if (index < m_State->config.m_ProbeOrigins.size()) m_State->config.m_Layout.UpdateCachePath(m_State->config.m_ProbeOrigins[index], probe.cachePath);
            }
            std::ifstream sourceKeyFile(base / "sky-lighting.key");
            std::string cachedSourceKey; sourceKeyFile >> cachedSourceKey;
            if (cachedSourceKey.empty() || cachedSourceKey != m_State->skyLightingSourceKey) continue;
            auto* texture = GetProbeTexture(index);
            if (!texture) return false;
            std::string error;
            std::vector<VansReflectionProbeCacheSurface> surfaces; uint64_t bytes = 0; uint32_t mips = 0;
            if (!VansReflectionProbeCache::Describe(texture->GetImage().GetImageDimension().width, surfaces, bytes, mips, error)) return false;
            // 先释放已满批次，再解码下一文件；临时单文件读取空间不累积到全场。
            if ((!uploads.empty() && decodedBytes + bytes > DecodeBudgetBytes) || uploads.size() == 128u)
                if (!flush()) return false;
            VansReflectionProbeCacheData data;
            if (!VansReflectionProbeCache::Load(base / VansReflectionProbeCache::FileName, data, error)) continue;
            if (data.resolution != texture->GetImage().GetImageDimension().width || data.mipCount != mips)
            { VANS_LOG_ERROR("[ReflectionProbe] Cache dimensions do not match allocated probe: " << probe.name); continue; }
            VansTextureMipChainUpload upload;
            upload.destImage = &texture->GetImage(); upload.data = data.texels.data(); upload.dataSize = int(data.texels.size());
            for (const auto& surface : surfaces)
            {
                VkBufferImageCopy copy{}; copy.bufferOffset = surface.offset;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, surface.mip,
                    m_State->m_BakeResults[index].arrayLayer * 6u + surface.face, 1u};
                copy.imageExtent = {surface.size, surface.size, 1u};
                upload.regions.push_back(copy);
            }
            decodedBytes += data.texels.size();
            decoded.push_back(std::move(data)); uploads.push_back(std::move(upload)); indices.push_back(index);
        }
        if (!flush()) return false;
        VANS_LOG("[ReflectionProbe] Complete mip caches uploaded=" << loaded << " cachedPrefilterDispatches=0");
        return true;
    }

	VansTexture* VansReflectionProbeSystem::GetProbeTexture(size_t probeIndex) const
	{
		if (probeIndex >= m_State->m_BakeResults.size() || probeIndex >= m_State->m_PageLayout.ProbeCount()) return nullptr;
		const auto address = m_State->m_PageLayout.Address(static_cast<uint32_t>(probeIndex));
		return address.page < m_State->m_SpecularPages.size() ? m_State->m_SpecularPages[address.page].get() : nullptr;
	}

	uint32_t VansReflectionProbeSystem::GetProbeResolution(size_t probeIndex) const
	{
		auto* texture = GetProbeTexture(probeIndex);
		return texture ? texture->GetImage().GetImageDimension().width : 0u;
	}

	uint32_t VansReflectionProbeSystem::GetProbeMipCount(size_t probeIndex) const
	{
		auto* texture = GetProbeTexture(probeIndex);
		return texture ? texture->GetImage().GetImageCreateInfo().mipLevels : 0u;
	}

	bool VansReflectionProbeSystem::CreateGPUResources(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
	{
		auto pending = std::make_shared<VansReflectionProbeSystem>();
		try
		{
			CopyConfigurationTo(*pending);
			if (!pending->InitializeGPUResources(device, commandBuffer))
			{
				pending->m_State->m_PrefilterShader = nullptr;
				m_State->m_PlacementError = pending->m_State->m_PlacementError;
				return false;
			}
			CommitResources(device, pending);
			return true;
		}
		catch (const std::exception& error)
		{
			pending->m_State->m_PrefilterShader = nullptr;
			m_State->m_PlacementError = error.what();
			VANS_LOG_ERROR("[ReflectionProbe] Resource preparation failed: " << m_State->m_PlacementError);
			return false;
		}
	}

	bool VansReflectionProbeSystem::InitializeGPUResources(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
	{
		EnsureDefaults();
		VansReflectionProbePageLayout layout;
		std::vector<uint32_t> resolutions;
		for (const auto& probe : m_State->config.m_Probes)
			resolutions.push_back(probe.type == ReflectionProbeType::Sky ? 0u : NormalizeResolution(probe.resolution));
		if (!layout.Build(resolutions, device.GetDeviceProperties().limits.maxImageArrayLayers, m_State->m_PlacementError)) return false;
		// 此对象尚未发布；所有页面、工作纹理和元数据均准备完成后才提交。
		std::vector<std::unique_ptr<VansTexture>> pages;
		for (const auto& description : layout.Pages())
		{
			auto page = std::make_unique<VansTexture>();
			if (!page->InitCubeTextureArray(commandBuffer, description.resolution, description.resolution,
				description.cubeCount, 4, true, HDR_PRES_16))
			{ m_State->m_PlacementError = "Reflection texture page allocation failed"; return false; }
			NameProbeResource(device.GetLogicDevice(), VK_OBJECT_TYPE_IMAGE,
				reinterpret_cast<uint64_t>(page->GetImage().GetImage()), "ReflectionProbe.Page." + std::to_string(pages.size()));
			pages.push_back(std::move(page));
		}
		// 单个工作 cubemap：低分辨率捕获直接使用匹配尺寸的 mip，不创建每 probe 捕获纹理。
		auto captureTexture = std::make_unique<VansTexture>();
		if (!captureTexture->InitCubeTextureArray(commandBuffer, layout.CaptureResolution(), layout.CaptureResolution(), 1, 4, true, HDR_PRES_16))
		{ m_State->m_PlacementError = "Reflection capture workspace allocation failed"; return false; }
		NameProbeResource(device.GetLogicDevice(), VK_OBJECT_TYPE_IMAGE,
			reinterpret_cast<uint64_t>(captureTexture->GetImage().GetImage()), "ReflectionProbe.CaptureWorkspace");
		m_State->m_Device = device.GetLogicDevice();
		m_State->m_CaptureTexture = std::move(captureTexture);
		m_State->m_PageLayout = std::move(layout); m_State->m_SpecularPages = std::move(pages);
		m_State->m_CaptureResolution = m_State->m_PageLayout.CaptureResolution();
		m_State->m_CaptureMipCount = m_State->m_PageLayout.CaptureMipCount();
		m_State->m_BakeResults.resize(m_State->config.m_Probes.size());
		for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
		{
			const auto address = m_State->m_PageLayout.Address(static_cast<uint32_t>(i));
			auto& result = m_State->m_BakeResults[i];
			result.texturePage = address.page; result.arrayLayer = address.cube;
			result.mipCount = address.page < m_State->m_PageLayout.Pages().size() ? m_State->m_PageLayout.Pages()[address.page].mipCount : 0u;
			result.valid = false;
        }
        if (!LoadCachedProbes(device, commandBuffer))
        { m_State->m_PlacementError = "Reflection complete mip cache upload failed"; return false; }
        for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
        {
            auto& result = m_State->m_BakeResults[i];
			result.dirty = !result.valid && m_State->config.m_Probes[i].type != ReflectionProbeType::Sky;
			result.cachePath = m_State->config.m_Probes[i].cachePath;
			result.status = result.valid ? "Loaded baked cache" : (m_State->config.m_Probes[i].type == ReflectionProbeType::Sky ? "Global sky fallback" : "Bake required");
			if (result.dirty && (m_State->config.m_Probes[i].type == ReflectionProbeType::Baked || m_State->config.m_Probes[i].refreshMode == ReflectionProbeRefreshMode::OnLoad)) QueueCapture(i, false);
		}
		if (!CreatePrefilterResources(device))
		{ m_State->m_PlacementError = "Reflection prefilter resources could not be created"; return false; }
		if (!InitializeMetadataResources(device)) return false;
		VANS_LOG("[ReflectionProbe] Texture pages=" << m_State->m_SpecularPages.size() << " logicalProbes=" << m_State->config.m_Probes.size()
			<< " maxResolution=" << m_State->m_CaptureResolution << " residentBytes=" << m_State->m_PageLayout.ResidentBytes()
			<< " maxImageArrayLayers=" << device.GetDeviceProperties().limits.maxImageArrayLayers
			<< " captureWorkspaceCubes=1 pendingCaptures=" << m_State->m_BakeQueue.size());
		return m_State->m_SpatialIndexBuffer.GetNativeBuffer() != VK_NULL_HANDLE;
	}

	bool VansReflectionProbeSystem::InitializeMetadataResources(VansVKDevice& device)
	{
		m_State->m_Device = device.GetLogicDevice();
		BuildGPUData();
		const size_t size = sizeof(m_State->m_Header) + std::max<size_t>(1, m_State->m_GPUProbes.size()) * sizeof(VansReflectionProbeGPU);
		if (!m_State->m_MetadataBuffer.CreatVulkanBuffer(m_State->m_Device, size, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{ m_State->m_PlacementError = "Reflection metadata buffer allocation failed"; return false; }
		NameProbeResource(m_State->m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_State->m_MetadataBuffer.GetNativeBuffer()), "ReflectionProbe.Metadata");
		try { UploadMetadata(); }
		catch (const std::exception& error)
		{ m_State->m_PlacementError = error.what(); VANS_LOG_ERROR("[ReflectionProbe] " << m_State->m_PlacementError); return false; }
		return true;
	}

	bool VansReflectionProbeSystem::CreatePrefilterResources(VansVKDevice& device)
	{
		if (m_State->m_SpecularPages.empty() || !m_State->m_CaptureTexture) return false;
		if (m_State->m_CaptureMipCount <= 1) return true;
		m_State->m_PrefilterShader = VansShaderManager::Get().FindComputeShader("ReflectionProbePrefilter");
		if (!m_State->m_PrefilterShader) return false;
		VkDescriptorSetLayoutBinding source{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
		VkDescriptorSetLayoutBinding output{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
		auto* manager = VansVKDescriptorManager::GetInstance();
		// 每种实际分辨率对应一个源 mip；描述符数量不随逻辑 probe 数量增加。
		m_State->m_PrefilterOffsets.assign(m_State->m_CaptureMipCount, UINT32_MAX);
		uint32_t setCount = 0;
		for (const auto& page : m_State->m_PageLayout.Pages())
		{
			const uint32_t baseMip = m_State->m_CaptureMipCount - page.mipCount;
			if (m_State->m_PrefilterOffsets[baseMip] != UINT32_MAX) continue;
			m_State->m_PrefilterOffsets[baseMip] = setCount;
			setCount += page.mipCount - 1u;
		}
		if (!setCount) return true;
		if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom({source, output}, m_State->m_PrefilterLayout, m_State->m_PrefilterSets, setCount)) return false;
		m_State->m_PrefilterMipViews.resize(m_State->m_CaptureMipCount, VK_NULL_HANDLE);
		m_State->m_PrefilterSourceViews.resize(m_State->m_CaptureMipCount, VK_NULL_HANDLE);
		auto& image = m_State->m_CaptureTexture->GetImage();
		for (uint32_t baseMip = 0; baseMip + 1u < m_State->m_CaptureMipCount; ++baseMip)
		{
			if (m_State->m_PrefilterOffsets[baseMip] == UINT32_MAX) continue;
			// 源视图只包含捕获六面，不能把同批写入的粗糙 mip 纳入采样范围。
			m_State->m_PrefilterSourceViews[baseMip] = image.CreateCubeMipView(device.GetLogicDevice(), baseMip);
			if (m_State->m_PrefilterSourceViews[baseMip] == VK_NULL_HANDLE) return false;
			for (uint32_t mip = baseMip + 1u; mip < m_State->m_CaptureMipCount; ++mip)
			{
				if (m_State->m_PrefilterMipViews[mip] == VK_NULL_HANDLE)
					m_State->m_PrefilterMipViews[mip] = image.CreateMipArrayView(device.GetLogicDevice(), mip);
				if (m_State->m_PrefilterMipViews[mip] == VK_NULL_HANDLE) return false;
			}
		}
		manager->BeginDescriptorUpdate();
		for (uint32_t baseMip = 0; baseMip + 1u < m_State->m_CaptureMipCount; ++baseMip)
		{
			if (m_State->m_PrefilterOffsets[baseMip] == UINT32_MAX) continue;
			for (uint32_t mip = baseMip + 1u; mip < m_State->m_CaptureMipCount; ++mip)
			{
				const uint32_t index = m_State->m_PrefilterOffsets[baseMip] + mip - baseMip - 1u;
				NameProbeResource(device.GetLogicDevice(), VK_OBJECT_TYPE_DESCRIPTOR_SET,
					reinterpret_cast<uint64_t>(m_State->m_PrefilterSets[index]), "ReflectionProbe.Prefilter.Capture."
					+ std::to_string(baseMip) + ".Mip." + std::to_string(mip));
				manager->WriteImageDescriptor(m_State->m_PrefilterSets[index], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{{image.GetSampler(), m_State->m_PrefilterSourceViews[baseMip], VK_IMAGE_LAYOUT_GENERAL}});
				manager->WriteImageDescriptor(m_State->m_PrefilterSets[index], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{{VK_NULL_HANDLE, m_State->m_PrefilterMipViews[mip], VK_IMAGE_LAYOUT_GENERAL}});
			}
		}
		manager->CommitDescriptorUpdates();
		return true;
	}

	bool VansReflectionProbeSystem::PublishCapture(VansVKDevice& device, VansVKCommandBuffer& commandBuffer, uint32_t probeIndex)
	{
		if (!m_State->m_CaptureTexture || !GetProbeTexture(probeIndex) || m_State->m_ActiveBakeFace != 6u) return false;
		const auto address = m_State->m_PageLayout.Address(probeIndex);
		auto& source = m_State->m_CaptureTexture->GetImage(); auto& destination = m_State->m_SpecularPages[address.page]->GetImage();
		const uint32_t baseMip = m_State->m_CaptureMipCount - destination.GetImageCreateInfo().mipLevels;
		if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return false;
		// 六面与 mip 完成后复用原过滤提交发布；不增加每 probe 的队列提交。
		if (!RecordPrefilterCapture(commandBuffer, baseMip) ||
			!RecordReflectionProbePublication(commandBuffer.GetVKCommandBuffer(), source.GetImage(), source.GetImageCreateInfo(), baseMip,
				destination.GetImage(), destination.GetImageCreateInfo(), address.cube) || !commandBuffer.EndCommandBufferRecord())
		{ commandBuffer.ResetCommandBuffer(false); return false; }
		const bool submitted = VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
			{commandBuffer.GetVKCommandBuffer()}, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false);
		return submitted;
	}

	bool VansReflectionProbeSystem::RecordPrefilterCapture(VansVKCommandBuffer& commandBuffer, uint32_t baseMip)
	{
		if (!m_State->m_CaptureTexture || baseMip >= m_State->m_CaptureMipCount) return false;
		auto& image = m_State->m_CaptureTexture->GetImage();
		const uint32_t mipCount = m_State->m_CaptureMipCount - baseMip;
		if (mipCount <= 1) return true;
		if (baseMip >= m_State->m_PrefilterOffsets.size()) return false;
		const uint32_t descriptorOffset = m_State->m_PrefilterOffsets[baseMip];
		if (!m_State->m_PrefilterShader || descriptorOffset > m_State->m_PrefilterSets.size() ||
			mipCount - 1u > m_State->m_PrefilterSets.size() - descriptorOffset) return false;
		VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		toGeneral.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		toGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toGeneral.image = image.GetImage();
		toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 6};
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, {toGeneral});
		PrefilterPushConstants params{};
		params.cubeCount = 1; params.sampleCount = 128; params.baseCube = 0;
		commandBuffer.EnsureComputeShader(*m_State->m_PrefilterShader, {m_State->m_PrefilterLayout});
		for (uint32_t mip = 1; mip < mipCount; ++mip)
		{
			params.roughness = float(mip) / float(mipCount - 1); params.outputSize = m_State->m_CaptureResolution >> (baseMip + mip);
			commandBuffer.DispatchCompute(*m_State->m_PrefilterShader, (params.outputSize + 7u) / 8u, (params.outputSize + 7u) / 8u, 6u,
				{m_State->m_PrefilterSets[descriptorOffset + mip - 1]}, &params, sizeof(params));
		}
		VkImageMemoryBarrier toRead = toGeneral;
		toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL; toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, {}, {toRead});
		return true;
	}

	void VansReflectionProbeSystem::BuildGPUData()
	{
		m_State->m_GPUProbes.clear();
		for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
		{
			const auto& p = m_State->config.m_Probes[i];
			if (p.type == ReflectionProbeType::Sky) continue;
			VansReflectionProbeGPU gpu{};
			gpu.positionAndRadius = glm::vec4(p.position, p.radius);
			gpu.boxMinAndType = glm::vec4(p.boxMin, p.shape == ReflectionProbeShape::Box ? 1.0f : 0.0f);
			gpu.boxMaxAndPriority = glm::vec4(p.boxMax, p.priority);
			gpu.fadeAndIntensity = glm::vec4(p.blendDistance, p.intensity, 0.0f, 0.0f);
			const auto address = i < m_State->m_PageLayout.ProbeCount() ? m_State->m_PageLayout.Address(static_cast<uint32_t>(i)) : VansReflectionProbeTextureAddress{};
			gpu.capturePositionAndLayer = glm::vec4(p.capturePosition, float(address.cube));
			uint32_t flags = (p.enabled && i < m_State->m_BakeResults.size() && m_State->m_BakeResults[i].valid ? 1u : 0u) |
				(p.boxProjection ? 2u : 0u) | (p.portal ? 4u : 0u) | (uint32_t(p.type) << 8u);
			gpu.regionAndFlags = glm::uvec4(p.regionId, flags, p.cullingMask, address.page);
			gpu.specularAndMip = glm::vec4(0.0f, p.specularIntensity, float(std::max(1u, i < m_State->m_BakeResults.size() ? m_State->m_BakeResults[i].mipCount : 0u) - 1u), 0.0f);
			m_State->m_GPUProbes.push_back(gpu);
		}
		m_State->m_Header.probeCount = (uint32_t)m_State->m_GPUProbes.size();
		m_State->m_Header.activeProbeCount = (uint32_t)std::count_if(m_State->m_GPUProbes.begin(), m_State->m_GPUProbes.end(), [](const auto& p) { return (p.regionAndFlags.y & 1u) != 0u; });
		m_State->m_Header.maxBlendCount = m_State->config.m_LightingSettings.maxBlendCount;
		m_State->m_Header.debugView = (uint32_t)m_State->config.m_EditorState.debugView;
		RefreshLightingParams();
		m_State->m_Header.uniformGridOrigin = glm::vec4(0.0f);
		m_State->m_Header.uniformGridInvCellSize = glm::vec4(0.0f);
		m_State->m_Header.uniformGridDimensionsAndFlags = glm::uvec4(0u);
		const glm::vec3 volumeSize = glm::max(
			m_State->config.m_PlacementSettings.volumeMax - m_State->config.m_PlacementSettings.volumeMin, glm::vec3(0.5f));
		const glm::uvec3 gridDimensions = glm::uvec3(glm::clamp(
			glm::ivec3(glm::ceil(volumeSize / std::max(m_State->config.m_PlacementSettings.uniformSpacing, 0.5f))),
			glm::ivec3(1), glm::ivec3(32)));
		const size_t expectedUniformProbeCount = size_t(gridDimensions.x) * gridDimensions.y * gridDimensions.z;
		const glm::vec3 cellSize = volumeSize / glm::vec3(gridDimensions);
		const float maxExactFade = 0.5f * std::min(cellSize.x, std::min(cellSize.y, cellSize.z));
		bool pureUniformGrid = m_State->m_GPUProbes.size() == expectedUniformProbeCount;
		// The eight-candidate shader path is exact only when probes preserve the
		// generated row-major grid and no fade can cross two cell boundaries.
		size_t gridProbeIndex = 0;
		for (const VansReflectionProbeDesc& probe : m_State->config.m_Probes)
		{
			if (!pureUniformGrid || probe.type == ReflectionProbeType::Sky)
				continue;
			const uint32_t x = uint32_t(gridProbeIndex) % gridDimensions.x;
			const uint32_t yz = uint32_t(gridProbeIndex) / gridDimensions.x;
			const uint32_t y = yz % gridDimensions.y;
			const uint32_t z = yz / gridDimensions.y;
			const glm::vec3 cellMin = m_State->config.m_PlacementSettings.volumeMin + glm::vec3(x, y, z) * cellSize;
			const glm::vec3 cellMax = cellMin + cellSize;
			const glm::vec3 expectedCenter = (cellMin + cellMax) * 0.5f;
			constexpr float kGridEpsilon = 1e-3f;
			pureUniformGrid = probe.autoGenerated && probe.type != ReflectionProbeType::Sky &&
				probe.shape == ReflectionProbeShape::Box &&
				glm::all(glm::lessThanEqual(glm::abs(probe.position - expectedCenter), glm::vec3(kGridEpsilon))) &&
				glm::all(glm::greaterThanEqual(probe.boxMin, cellMin - glm::vec3(kGridEpsilon))) &&
				glm::all(glm::lessThanEqual(probe.boxMax, cellMax + glm::vec3(kGridEpsilon))) &&
				probe.blendDistance <= maxExactFade + kGridEpsilon;
			++gridProbeIndex;
		}
		pureUniformGrid = pureUniformGrid && gridProbeIndex == expectedUniformProbeCount;
		if (pureUniformGrid)
		{
			m_State->m_Header.uniformGridOrigin = glm::vec4(m_State->config.m_PlacementSettings.volumeMin, 0.0f);
			m_State->m_Header.uniformGridInvCellSize = glm::vec4(1.0f / cellSize, 0.0f);
			m_State->m_Header.uniformGridDimensionsAndFlags = glm::uvec4(gridDimensions, 1u);
		}

	}

	void VansReflectionProbeSystem::UploadMetadata()
	{
		if (m_State->m_MetadataBuffer.GetNativeBuffer() == VK_NULL_HANDLE) return;
		BuildGPUData();
		UploadSpatialIndex();
		std::vector<uint8_t> bytes(sizeof(m_State->m_Header) + m_State->m_GPUProbes.size() * sizeof(VansReflectionProbeGPU));
		std::memcpy(bytes.data(), &m_State->m_Header, sizeof(m_State->m_Header));
		if (!m_State->m_GPUProbes.empty()) std::memcpy(bytes.data() + sizeof(m_State->m_Header), m_State->m_GPUProbes.data(), m_State->m_GPUProbes.size() * sizeof(VansReflectionProbeGPU));
		if (!m_State->m_MetadataBuffer.SetBufferData(bytes.data(), 0, bytes.size()))
			throw std::runtime_error("Failed to upload reflection probe metadata");
	}

	void VansReflectionProbeSystem::UploadSpatialIndex()
	{
		const bool changed = m_State->m_SpatialIndex.Update(m_State->m_GPUProbes);
		if (!changed && m_State->m_SpatialIndexBuffer.GetNativeBuffer() != VK_NULL_HANDLE) return;
		const auto& header = m_State->m_SpatialIndex.GetHeader();
		const auto& words = m_State->m_SpatialIndex.GetWords();
		const size_t byteCount = sizeof(header) + words.size() * sizeof(uint32_t);
		const bool resize = m_State->m_SpatialIndexBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
			m_State->m_SpatialIndexBuffer.GetBufferSize() < byteCount;
		if (resize)
		{
			// 几何变更通过编辑器的 render-thread idle transaction 发布；烘焙状态变化不重建索引。
			m_State->m_SpatialIndexBuffer.DestroyVulkanBuffer(m_State->m_Device);
			if (!m_State->m_SpatialIndexBuffer.CreatVulkanBuffer(m_State->m_Device, byteCount, VK_FORMAT_R32_UINT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
				throw std::runtime_error("Failed to allocate reflection probe spatial index");
			NameProbeResource(m_State->m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_State->m_SpatialIndexBuffer.GetNativeBuffer()), "ReflectionProbe.SpatialIndex");
		}
		std::vector<uint8_t> bytes(byteCount);
		std::memcpy(bytes.data(), &header, sizeof(header));
		std::memcpy(bytes.data() + sizeof(header), words.data(), words.size() * sizeof(uint32_t));
		if (!m_State->m_SpatialIndexBuffer.SetBufferData(bytes.data(), 0, byteCount))
			throw std::runtime_error("Failed to upload reflection probe spatial index");
		if (resize && m_State->m_GlobalDescriptorSet != VK_NULL_HANDLE)
			UpdateGlobalDescriptors(m_State->m_GlobalDescriptorSet);
	}

	void VansReflectionProbeSystem::RefreshLightingParams()
	{
		m_State->m_Header.lightingParams = glm::vec4(m_State->config.m_LightingSettings.ssrRoughnessFadeStart,
			std::max(m_State->config.m_LightingSettings.ssrRoughnessFadeEnd, m_State->config.m_LightingSettings.ssrRoughnessFadeStart + 0.001f),
			std::clamp(m_State->config.m_EditorState.debugRoughness, 0.0f, 1.0f),
			std::exp2(std::clamp(m_State->config.m_EditorState.debugExposureEV, -10.0f, 10.0f)));
	}


	void VansReflectionProbeSystem::UpdateGlobalDescriptors(VkDescriptorSet globalSet)
	{
		WriteGlobalDescriptors(globalSet, m_State->m_SpecularPages);
	}

	void VansReflectionProbeSystem::WriteGlobalDescriptors(VkDescriptorSet globalSet,
		const std::vector<std::unique_ptr<VansTexture>>& pages)
	{
		if (pages.empty() || globalSet == VK_NULL_HANDLE || m_State->m_MetadataBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
			m_State->m_SpatialIndexBuffer.GetNativeBuffer() == VK_NULL_HANDLE) return;
		m_State->m_GlobalDescriptorSet = globalSet;
		NameProbeResource(m_State->m_Device, VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(globalSet), "Scene.GlobalDescriptors");
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		std::vector<VkDescriptorImageInfo> pageDescriptors;
		pageDescriptors.reserve(ReflectionProbeMaxTexturePages);
		for (uint32_t page = 0; page < ReflectionProbeMaxTexturePages; ++page)
		{
			auto& image = pages[page < pages.size() ? page : 0]->GetImage();
			pageDescriptors.push_back({image.GetSampler(), image.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
		}
		desc->WriteImageDescriptor(globalSet, GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, pageDescriptors);
		desc->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_REFLECTION_PROBE_BUFFER,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_State->m_MetadataBuffer.GetNativeBuffer(), 0, m_State->m_MetadataBuffer.GetBufferSize() }});
		desc->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_REFLECTION_PROBE_INDEX,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_State->m_SpatialIndexBuffer.GetNativeBuffer(), 0, m_State->m_SpatialIndexBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
	}

	void VansReflectionProbeSystem::QueueCapture(size_t index, bool persistCache)
	{
		if (index >= m_State->config.m_Probes.size() || m_State->config.m_Probes[index].type == ReflectionProbeType::Sky) return;
		auto existing = std::find_if(m_State->m_BakeQueue.begin(), m_State->m_BakeQueue.end(),
			[index](const CaptureRequest& request) { return request.probeIndex == index; });
		if (existing == m_State->m_BakeQueue.end()) m_State->m_BakeQueue.push_back({index, persistCache});
		else existing->persistCache = existing->persistCache || persistCache;
		m_State->m_BakeResults[index].status = "Queued";
	}

	// 显式 Bake 才允许写缓存；OnLoad 和实时刷新只更新 GPU 内容。
	void VansReflectionProbeSystem::RequestBake(size_t index) { QueueCapture(index, true); }
	void VansReflectionProbeSystem::RequestBakeAll() { for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i) RequestBake(i); }
	void VansReflectionProbeSystem::MarkBakeComplete(size_t index, bool success, const std::string& message)
	{
		if (index >= m_State->m_BakeResults.size()) return;
		auto& r = m_State->m_BakeResults[index]; r.dirty = !success; r.status = message;
		// 捕获或过滤失败只丢弃工作内容；已经发布的旧数据继续有效。
		if (success) { r.valid = true; r.revision = ++m_State->m_BakeRevision; UploadMetadata(); }
	}
	void VansReflectionProbeSystem::MarkDirty(size_t index)
	{
		if (index < m_State->m_BakeResults.size()) { m_State->m_BakeResults[index].dirty = true; m_State->m_BakeResults[index].status = "Modified - bake required"; }
	}
    void VansReflectionProbeSystem::SetSkyLightingSource(const std::string& sourceKey)
    {
        if (m_State->skyLightingSourceKey == sourceKey) return;
        m_State->skyLightingSourceKey = sourceKey;
        if (m_State->m_Device == VK_NULL_HANDLE) return;
        // 保留上次完整发布直到新捕获完成，禁止把灯光/自发光混合缓存整体缩放。
        m_State->m_ActiveBakeFace = 0;
        for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
            if (m_State->config.m_Probes[i].enabled && m_State->config.m_Probes[i].type != ReflectionProbeType::Sky)
            { MarkDirty(i); QueueCapture(i, false); }
    }

	void VansReflectionProbeSystem::UpdateRealtimeProbes(uint32_t frameIndex)
	{
		for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
		{
			const auto& p = m_State->config.m_Probes[i]; if (!p.enabled || p.type != ReflectionProbeType::Realtime) continue;
			if (p.refreshMode == ReflectionProbeRefreshMode::EveryFrame || (p.refreshMode == ReflectionProbeRefreshMode::TimeSliced && frameIndex % 6u == i % 6u)) QueueCapture(i, false);
		}
	}

	bool VansReflectionProbeSystem::EnsureCaptureResources(VansScene& scene, VansVKDevice& device)
	{
		if (m_State->m_SpecularPages.empty() || scene.GetGlobalDescriptorSet() == VK_NULL_HANDLE) return false;
		auto* materialManager = scene.GetMaterialManager();
		if (!materialManager || !materialManager->m_SkyLighting.DiffuseIrradiance())
			return false;
		VansVKImage& skyDiffuseEnvironment =
			materialManager->m_SkyLighting.DiffuseIrradiance()->GetImage();
		if (m_State->m_PageLayout.ProbeCount() != m_State->config.m_Probes.size()) return false;
		if (m_State->m_CaptureRenderPassCreated) return true;

		VkDevice logicalDevice = device.GetLogicDevice();
		auto fail = [this]()
		{
			DestroyCaptureResources();
			return false;
		};
		if (!m_State->m_CaptureCameraBuffer.CreatVulkanBuffer(logicalDevice, sizeof(CaptureCameraData), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return false;
		m_State->m_CaptureCameraBufferCreated = true;

		auto* descriptors = VansVKDescriptorManager::GetInstance();
		auto& rayTracing = device.GetRayTracingContext();
		auto* irradianceAtlas = rayTracing.GetGIRegionIrradianceAtlas(0u);
		auto* visibilityAtlas = rayTracing.GetGIRegionVisibilityAtlas(0u);
		const VansVKBuffer* probeState = rayTracing.GetGIRegionProbeStateBuffer(0u);
		if (!irradianceAtlas || !visibilityAtlas ||
			!probeState || probeState->GetNativeBuffer() == VK_NULL_HANDLE)
		{
			VANS_LOG_ERROR("Reflection probe capture requires sky diffuse and DDGI atlas/state resources.");
			return fail();
		}

		const std::vector<VkDescriptorSetLayoutBinding> bindings = {
			{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
		};
		std::vector<VkDescriptorSet> captureSets;
		if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
			bindings,
			m_State->m_CaptureDescriptorLayout,
			captureSets) || captureSets.empty()) return fail();
		m_State->m_CaptureDescriptorSet = captureSets[0];
		NameProbeResource(logicalDevice, VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(m_State->m_CaptureDescriptorSet), "ReflectionProbe.CaptureDescriptors");
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteBufferDescriptor(m_State->m_CaptureDescriptorSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ m_State->m_CaptureCameraBuffer.GetNativeBuffer(), 0, sizeof(CaptureCameraData) }});
		descriptors->WriteImageDescriptor(m_State->m_CaptureDescriptorSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPassManager->GetCascadeShadowSampler(), renderPassManager->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		descriptors->WriteImageDescriptor(m_State->m_CaptureDescriptorSet, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ skyDiffuseEnvironment.GetSampler(), skyDiffuseEnvironment.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
        std::vector<VkDescriptorImageInfo> irradianceInfos, visibilityInfos;
        std::vector<VkDescriptorBufferInfo> stateInfos;
        for (uint32_t slot = 0; slot < VANS_SSGI_MAX_GI_REGIONS; ++slot)
        {
            const uint32_t index = slot < rayTracing.GetGIRegionCount() ? slot : 0u;
            auto* irradiance = rayTracing.GetGIRegionIrradianceAtlas(index);
            auto* visibility = rayTracing.GetGIRegionVisibilityAtlas(index);
            const auto* state = rayTracing.GetGIRegionProbeStateBuffer(index);
            irradianceInfos.push_back({irradiance->GetImage().GetSampler(), irradiance->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL});
            visibilityInfos.push_back({visibility->GetImage().GetSampler(), visibility->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL});
            stateInfos.push_back({state->GetNativeBuffer(), 0, state->GetBufferSize()});
        }
        descriptors->WriteImageDescriptor(m_State->m_CaptureDescriptorSet, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, irradianceInfos);
        descriptors->WriteImageDescriptor(m_State->m_CaptureDescriptorSet, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, visibilityInfos);
        descriptors->WriteBufferDescriptor(m_State->m_CaptureDescriptorSet, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stateInfos);
        const auto& layoutBuffer = rayTracing.GetGIProbeLayoutBuffer();
        descriptors->WriteBufferDescriptor(m_State->m_CaptureDescriptorSet, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{layoutBuffer.GetNativeBuffer(), 0, layoutBuffer.GetBufferSize()}});
		descriptors->CommitDescriptorUpdates();

		const VkExtent3D extent{ m_State->m_CaptureResolution, m_State->m_CaptureResolution, 1u };
		if (!m_State->m_CaptureDepthImage.CreateVulkanImage(logicalDevice, extent, VK_FORMAT_D32_SFLOAT, 1, 1,
			VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_SAMPLE_COUNT_1_BIT)) return fail();
		m_State->m_CaptureDepthCreated = true;
		NameProbeResource(logicalDevice, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_State->m_CaptureDepthImage.GetImage()), "ReflectionProbe.CaptureDepth");

		std::vector<VkAttachmentDescription> attachments(2);
		attachments[0] = { 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		attachments[1] = { 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
		SubpassParameters subpass{};
		subpass.PipelineType = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.ColorAttachments = { colorRef };
		subpass.DepthStencilAttachment = &depthRef;
		std::vector<SubpassParameters> subpasses{ subpass };
		std::vector<VkSubpassDependency> dependencies(2);
		// 工作 cubemap 与深度附件在所有捕获间复用，同时覆盖过滤/传输和前一面的深度写入。
		dependencies[0] = { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_DEPENDENCY_BY_REGION_BIT };
		dependencies[1] = { 0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT };
		const VkExtent2D captureExtent{ m_State->m_CaptureResolution, m_State->m_CaptureResolution };
		m_State->m_CaptureRenderPass.CreateRenderPass(logicalDevice, attachments, subpasses, dependencies, captureExtent);
		m_State->m_CaptureRenderPassCreated = m_State->m_CaptureRenderPass.GetRenderPass() != VK_NULL_HANDLE;
		if (!m_State->m_CaptureRenderPassCreated) return fail();



		m_State->m_CaptureGeometryShader = VansShaderManager::Get().FindGraphicsShader("ReflectionProbeCapture");
		m_State->m_CaptureSkyShader = VansShaderManager::Get().FindGraphicsShader("ReflectionProbeCaptureSky");
		if (!m_State->m_CaptureGeometryShader || !m_State->m_CaptureSkyShader) return fail();
		return true;
	}


	bool VansReflectionProbeSystem::PrepareCaptureTarget(uint32_t baseMip)
	{
		auto* texture = m_State->m_CaptureTexture.get();
		if (!texture || !m_State->m_CaptureRenderPassCreated || baseMip >= m_State->m_CaptureMipCount) return false;
		// 视图/附件数量由尺寸档位限定；在原 idle 捕获事务内按需创建后复用。
		m_State->m_CaptureFaceViews.resize(m_State->m_CaptureMipCount * 6u, VK_NULL_HANDLE);
		m_State->m_CaptureFramebuffers.resize(m_State->m_CaptureMipCount * 6u);
		const uint32_t resolution = m_State->m_CaptureResolution >> baseMip;
		for (uint32_t face = 0; face < 6; ++face)
		{
			const uint32_t index = baseMip * 6u + face;
			if (m_State->m_CaptureFramebuffers[index].GetFrameBuffer() != VK_NULL_HANDLE) continue;
			if (m_State->m_CaptureFaceViews[index] == VK_NULL_HANDLE)
				m_State->m_CaptureFaceViews[index] = texture->GetImage().CreateLayerMipView(m_State->m_Device, face, baseMip);
			if (m_State->m_CaptureFaceViews[index] == VK_NULL_HANDLE) return false;
			VkRenderPass renderPass = m_State->m_CaptureRenderPass.GetRenderPass();
			m_State->m_CaptureFramebuffers[index].CreateFrameBuffer(m_State->m_Device, renderPass,
				{m_State->m_CaptureFaceViews[index], m_State->m_CaptureDepthImage.GetImageView()}, {resolution, resolution, 1});
			if (m_State->m_CaptureFramebuffers[index].GetFrameBuffer() == VK_NULL_HANDLE) return false;
		}
		return true;
	}

	void VansReflectionProbeSystem::DestroyCaptureResources()
	{
		if (m_State->m_Device == VK_NULL_HANDLE) return;

		// Capture pipelines depend on the capture render pass and descriptor layout.
		if (m_State->m_CaptureGeometryShader)
			m_State->m_CaptureGeometryShader->TriggerReCreateGraphicsPipeline();
		m_State->m_CaptureGeometryShader = nullptr;
		if (m_State->m_CaptureSkyShader)
			m_State->m_CaptureSkyShader->TriggerReCreateGraphicsPipeline();
		m_State->m_CaptureSkyShader = nullptr;
		auto* descriptors = VansVKDescriptorManager::GetInstance();
		if (m_State->m_CaptureDescriptorSet != VK_NULL_HANDLE)
		{
			std::vector<VkDescriptorSet> sets{ m_State->m_CaptureDescriptorSet };
			descriptors->DestroyDescriptorSet(sets);
			m_State->m_CaptureDescriptorSet = VK_NULL_HANDLE;
		}
		descriptors->ReleaseDescriptorSetLayout(m_State->m_CaptureDescriptorLayout);

		for (VansFrameBuffer& framebuffer : m_State->m_CaptureFramebuffers)
			framebuffer.DestroyFrameBuffer(m_State->m_Device);
		for (VkImageView view : m_State->m_CaptureFaceViews)
			VansVKImage::DestroyImageView(m_State->m_Device, view);
		m_State->m_CaptureFramebuffers.clear(); m_State->m_CaptureFaceViews.clear();
		m_State->m_ActiveBakeFace = 0; // GI 绑定被重建时，下一次从第一面重新捕获。
		if (m_State->m_CaptureRenderPassCreated) m_State->m_CaptureRenderPass.DestroyRenderPass(m_State->m_Device);
		m_State->m_CaptureRenderPassCreated = false;
		if (m_State->m_CaptureDepthCreated) m_State->m_CaptureDepthImage.DestroyVulkanImage(m_State->m_Device);
		if (m_State->m_CaptureCameraBufferCreated) m_State->m_CaptureCameraBuffer.DestroyVulkanBuffer(m_State->m_Device);
		m_State->m_CaptureDepthCreated = false; m_State->m_CaptureCameraBufferCreated = false;
	}

	bool VansReflectionProbeSystem::CaptureFaceGPU(VansScene& scene, VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer, const VansReflectionProbeDesc& probe, size_t probeIndex, uint32_t face)
	{
		if (face >= 6 || !GetProbeTexture(probeIndex) || !EnsureCaptureResources(scene, device)) return false;
		const uint32_t baseMip = m_State->m_CaptureMipCount - GetProbeMipCount(probeIndex);
		const uint32_t resolution = GetProbeResolution(probeIndex);
		if (!PrepareCaptureTarget(baseMip)) return false;
		static const glm::vec3 directions[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
		static const glm::vec3 up[6] = { {0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0} };
		const glm::vec3 placementSize = glm::max(
			m_State->config.m_PlacementSettings.volumeMax - m_State->config.m_PlacementSettings.volumeMin, glm::vec3(0.5f));
		const float captureNear = std::clamp(std::min(probe.nearPlane, 0.01f), 0.001f, 0.05f);
		const float captureFar = std::max(probe.farPlane, std::max(glm::length(placementSize) * 2.0f, 1000.0f));
		glm::mat4 view = glm::lookAt(probe.capturePosition, probe.capturePosition + directions[face], up[face]);
		glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, captureNear, captureFar);
		VansGISettings gi = scene.GetGISettings();
		NormalizeGISettings(gi);
		const GIResolvedRegion primaryRegion = ResolveGIRegion(GetPrimaryGIRegionDesc(gi));
		CaptureCameraData cameraData{};
		cameraData.viewProjection = projection * view;
		cameraData.inverseViewProjection = glm::inverse(cameraData.viewProjection);
		cameraData.position = glm::vec4(probe.capturePosition, 1.0f);
		cameraData.giVolumeMin = glm::vec4(primaryRegion.volumeMin, 0.0f);
		cameraData.giVolumeSizeAndBias = glm::vec4(primaryRegion.volumeSize, primaryRegion.normalBias);
		cameraData.giGridDimensions = glm::vec4(glm::vec3(primaryRegion.gridDimensions), 0.0f);
		m_State->m_CaptureCameraBuffer.SetBufferData(&cameraData, 0, sizeof(cameraData));

		if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return false;
		VkClearValue clears[2]{}; clears[0].color = {{ 0.0f, 0.0f, 0.0f, 1.0f }}; clears[1].depthStencil = { 1.0f, 0 };
		VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.renderPass = m_State->m_CaptureRenderPass.GetRenderPass();
		beginInfo.framebuffer = m_State->m_CaptureFramebuffers[baseMip * 6u + face].GetFrameBuffer();
		beginInfo.renderArea = { {0,0}, {resolution,resolution} }; beginInfo.clearValueCount = 2; beginInfo.pClearValues = clears;
		commandBuffer.BeginRenderPass(beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		GlobalStateData state; state.currentRenderPass = m_State->m_CaptureRenderPass.GetRenderPass(); state.currentSubpass = 0;
		// Cube images use Vulkan's native top-to-bottom t coordinate. A negative
		// viewport vertically mirrored every captured face relative to samplerCube.
		state.viewport = { 0.0f, 0.0f, float(resolution), float(resolution), 0.0f, 1.0f };
		state.scissor = { {0,0}, {resolution,resolution} };
		commandBuffer.SetViewport(0, { state.viewport }); commandBuffer.SetScissor(0, { state.scissor });
		const std::vector<VkDescriptorSetLayout> layouts{ scene.GetGlobalDescriptorSetLayout(), m_State->m_CaptureDescriptorLayout };
		const std::vector<VkDescriptorSet> sets{ scene.GetGlobalDescriptorSet(), m_State->m_CaptureDescriptorSet };
		commandBuffer.EnsureGraphicsShader(*m_State->m_CaptureSkyShader, state, layouts);
		commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_State->m_CaptureSkyShader, 0, sets, {});
		commandBuffer.BindGraphicsPipeline(*m_State->m_CaptureSkyShader->GetGraphicsPipeline());
		commandBuffer.Draw(3, 1, 0, 0);

		std::vector<VansRenderNode*> nodes = scene.GetOpaqueRenderNodes();
		if (scene.GetTerrainRenderNode()) nodes.push_back(scene.GetTerrainRenderNode());
		for (VansRenderNode* node : nodes)
		{
			if (!node || !node->m_Mesh || !node->m_Material || (probe.cullingMask & uint32_t(node->GetNodeType())) == 0u) continue;
			CaptureDrawData draw{}; draw.model = node->m_ModelData.ModelMatrix; draw.albedo = glm::vec4(0.6f,0.6f,0.6f,1.0f);
			draw.params.w = -1.0f;
			if (auto* material = dynamic_cast<VansPBRMaterial*>(node->m_Material))
			{
				draw.albedo = glm::vec4(material->m_BasePBRParam.m_albedo, 1.0f);
				draw.params.y = material->m_BasePBRParam.m_roughness;
				draw.params.z = material->m_BasePBRParam.m_metallic;
				draw.params.w = float(material->m_MaterialIndex);
			}
			else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(node->m_Material))
			{
				draw.emissive = glm::vec4(emissive->m_BasePBRParam.m_albedo * emissive->m_BasePBRParam.m_roughness, 1.0f);
				draw.params.x = 1.0f;
				draw.params.w = float(emissive->m_MaterialIndex);
			}
			commandBuffer.BindMesh(*node->m_Mesh, 0, state);
			commandBuffer.EnsureGraphicsShader(*m_State->m_CaptureGeometryShader, state, layouts);
			commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_State->m_CaptureGeometryShader, 0, sets, {});
			commandBuffer.UpdatePushConstants(*m_State->m_CaptureGeometryShader->GetGraphicsPipeline(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw), &draw);
			commandBuffer.DrawMesh(*node->m_Mesh, *m_State->m_CaptureGeometryShader, 1);
		}
		commandBuffer.EndRenderPass();
		if (!commandBuffer.EndCommandBufferRecord()) { commandBuffer.ResetCommandBuffer(false); return false; }
		const bool submitted = VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() }, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false);
		return submitted;
	}

	bool VansReflectionProbeSystem::SaveProbeCacheGPU(VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
        const VansReflectionProbeDesc& probe, size_t probeIndex)
    {
        if (!GetProbeTexture(probeIndex) || probeIndex >= m_State->config.m_Probes.size()) return false;
        auto& image = GetProbeTexture(probeIndex)->GetImage();
        VansReflectionProbeCacheData data; data.resolution = image.GetImageDimension().width;
        std::vector<VansReflectionProbeCacheSurface> surfaces; uint64_t bytes = 0; std::string error;
        if (image.GetImageCreateInfo().format != VK_FORMAT_R16G16B16A16_SFLOAT ||
            !VansReflectionProbeCache::Describe(data.resolution, surfaces, bytes, data.mipCount, error) ||
            data.mipCount != image.GetImageCreateInfo().mipLevels) return false;
        VansVKBuffer readback;
        if (!readback.CreatVulkanBuffer(device.GetLogicDevice(), bytes, VK_FORMAT_R16_UINT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return false;
        struct Cleanup { VansVKBuffer& buffer; VkDevice device; ~Cleanup() { buffer.DestroyVulkanBuffer(device); } } cleanup{readback, device.GetLogicDevice()};
        if (!readback.PersistentMap() || !commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return false;
        std::vector<VkBufferImageCopy> regions;
        for (const auto& surface : surfaces)
        {
            VkBufferImageCopy copy{}; copy.bufferOffset = surface.offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, surface.mip,
                m_State->m_BakeResults[probeIndex].arrayLayer * 6u + surface.face, 1u};
            copy.imageExtent = {surface.size, surface.size, 1u}; regions.push_back(copy);
        }
        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image.GetImage();
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, data.mipCount, m_State->m_BakeResults[probeIndex].arrayLayer * 6u, 6u};
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {}, {}, {toTransfer});
        VansVKMemoryManager::CopyImageToBuffer(commandBuffer, image, readback, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, regions);
        auto toRead = toTransfer;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkMemoryBarrier toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            {toHost}, {}, {toRead});
        if (!commandBuffer.EndCommandBufferRecord()) { commandBuffer.ResetCommandBuffer(false); return false; }
        const bool submitted = VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
            {commandBuffer.GetVKCommandBuffer()}, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
        commandBuffer.ResetCommandBuffer(false);
        if (!submitted) return false;
        readback.InvalidateMappedRange(0, bytes);
        const auto* source = static_cast<const uint8_t*>(readback.GetMappedPtr());
        data.texels.assign(source, source + bytes);
        const fs::path base = probe.cachePath.empty() ? fs::path(m_State->config.m_ScenePath).parent_path()/"ReflectionProbes"/probe.name : fs::path(probe.cachePath);
        // 先撤销有效标记，避免数据文件替换后中断仍沿用旧天空指纹。
        std::error_code markerError;
        fs::remove(base / "sky-lighting.key", markerError);
        if (markerError) return false;
        const bool saved = VansReflectionProbeCache::Save(base / VansReflectionProbeCache::FileName, data, error);
        if (!saved) VANS_LOG_ERROR("[ReflectionProbe] Complete mip cache save failed: " << error);
        if (!saved) return false;
        std::ofstream sourceKeyFile(base / "sky-lighting.key", std::ios::trunc);
        sourceKeyFile << m_State->skyLightingSourceKey << '\n';
        sourceKeyFile.close();
        return bool(sourceKeyFile);
    }

	void VansReflectionProbeSystem::ProcessBakeQueue(VansScene& scene, VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer)
	{
		if (!HasCaptureWork()) return;
		VANS_PROFILE_SCOPE("ReflectionProbe::CaptureQueue", Vans::ProfileCategory::CommandRecord);
		if (m_State->m_ActiveBakeIndex == size_t(-1)) { m_State->m_ActiveBakeIndex = m_State->m_BakeQueue.front().probeIndex; m_State->m_ActiveBakeFace = 0; }
		if (m_State->m_ActiveBakeIndex >= m_State->config.m_Probes.size()) { m_State->m_BakeQueue.erase(m_State->m_BakeQueue.begin()); m_State->m_ActiveBakeIndex = size_t(-1); return; }
		auto& probe = m_State->config.m_Probes[m_State->m_ActiveBakeIndex]; auto& result = m_State->m_BakeResults[m_State->m_ActiveBakeIndex];
		result.status = "Capturing face " + std::to_string(m_State->m_ActiveBakeFace + 1) + "/6";
		if (!CaptureFaceGPU(scene, device, commandBuffer, probe, m_State->m_ActiveBakeIndex, m_State->m_ActiveBakeFace))
		{
			MarkBakeComplete(m_State->m_ActiveBakeIndex, false, "Capture failed"); m_State->m_BakeQueue.erase(m_State->m_BakeQueue.begin()); m_State->m_ActiveBakeIndex = size_t(-1); return;
		}
		++m_State->m_ActiveBakeFace;
		if (m_State->m_ActiveBakeFace >= 6)
		{
			const bool published = PublishCapture(device, commandBuffer, uint32_t(m_State->m_ActiveBakeIndex));
			const bool persist = m_State->m_BakeQueue.front().persistCache;
			const bool saved = published && persist && SaveProbeCacheGPU(device, commandBuffer, probe, m_State->m_ActiveBakeIndex);
			if (saved)
			{
				if (probe.cachePath.empty())
					probe.cachePath = (fs::path(m_State->config.m_ScenePath).parent_path()/"ReflectionProbes"/probe.name).string();
				result.cachePath = probe.cachePath;
				if (m_State->m_ActiveBakeIndex < m_State->config.m_ProbeOrigins.size()) m_State->config.m_Layout.UpdateCachePath(m_State->config.m_ProbeOrigins[m_State->m_ActiveBakeIndex], probe.cachePath);
			}
			const char* status = !published ? "Capture filtering or publication failed" : !persist ? "GPU capture ready" :
				(saved ? "GPU captured, GGX prefiltered and cached" : "GPU captured and prefiltered; cache readback failed");
			MarkBakeComplete(m_State->m_ActiveBakeIndex, published, status);
			m_State->m_BakeQueue.erase(m_State->m_BakeQueue.begin()); m_State->m_ActiveBakeIndex = size_t(-1); m_State->m_ActiveBakeFace = 0;
			if (m_State->m_BakeQueue.empty() && probe.type == ReflectionProbeType::Baked)
			{
				uint32_t ready = 0, total = 0;
				for (size_t i = 0; i < m_State->config.m_Probes.size(); ++i)
					if (m_State->config.m_Probes[i].enabled && m_State->config.m_Probes[i].type != ReflectionProbeType::Sky)
					{ ++total; if (i < m_State->m_BakeResults.size() && m_State->m_BakeResults[i].valid) ++ready; }
				VANS_LOG("[ReflectionProbe] Bake queue finished: ready=" << ready << "/" << total);
			}
		}
	}

	void VansReflectionProbeSystem::BakeQueuedProbesNow(VansScene& scene, VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer)
	{
		if (m_State->m_SpecularPages.empty()) return;
		while (!m_State->m_BakeQueue.empty())
			ProcessBakeQueue(scene, device, commandBuffer);
	}

	uint32_t VansReflectionProbeSystem::GetBakeFaceBudget() const
	{
		const size_t index = m_State->m_ActiveBakeIndex != size_t(-1) ? m_State->m_ActiveBakeIndex : (m_State->m_BakeQueue.empty() ? size_t(-1) : m_State->m_BakeQueue.front().probeIndex);
		if (index >= m_State->config.m_Probes.size() || m_State->config.m_Probes[index].type != ReflectionProbeType::Realtime) return 1u;
		return std::clamp(m_State->config.m_Probes[index].realtimeFacesPerFrame, 1u, 6u);
	}
}

