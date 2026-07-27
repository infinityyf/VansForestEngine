#pragma once

#include "../../ScriptCore/VansCommonUtils.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace VansGraphics
{
	constexpr uint32_t VANS_INVALID_SHADOW_INDEX = (std::numeric_limits<uint32_t>::max)();
	constexpr uint32_t VANS_MAX_PUNCTUAL_LIGHTS = 160;
	constexpr uint32_t VANS_MAX_PUNCTUAL_SHADOW_VIEWS = 480;

	enum class VansPunctualShadowLightType : uint8_t
	{
		Point = 0,
		Spot = 1,
		Rect = 2
	};

	enum class VansShadowPolicy : uint8_t
	{
		Disabled = 0,
		Auto = 1,
		Hero = 2,
		DistanceDynamic = 3
	};

	enum class VansShadowResolution : uint16_t
	{
		Auto = 0,
		R128 = 128,
		R256 = 256,
		R512 = 512,
		R1024 = 1024
	};

	enum class VansShadowUpdateMode : uint8_t
	{
		EveryFrame = 0,
		OnChange = 1,
		Budgeted = 2
	};

	enum class VansShadowFallback : uint8_t
	{
		None = 0,
		ScreenSpace = 1
	};

	enum class VansShadowRuntimeState : uint8_t
	{
		Disabled = 0,
		Candidate,
		PendingAllocation,
		PendingRender,
		ResidentDirty,
		ResidentClean,
		FallbackScreenSpace,
		FallbackNone,
		Evicting
	};

	enum VansShadowDirtyReason : uint32_t
	{
		VansShadowDirty_None = 0,
		VansShadowDirty_NewAllocation = 1u << 0,
		VansShadowDirty_LightTransform = 1u << 1,
		VansShadowDirty_Projection = 1u << 2,
		VansShadowDirty_CasterTransform = 1u << 3,
		VansShadowDirty_CasterGeometry = 1u << 4,
		VansShadowDirty_CasterMaterial = 1u << 5,
		VansShadowDirty_DynamicCaster = 1u << 6,
		VansShadowDirty_Resolution = 1u << 7
	};

	enum VansPunctualShadowGPUFlags : uint32_t
	{
		VansShadowGPU_None = 0,
		VansShadowGPU_HasAtlas = 1u << 0,
		VansShadowGPU_FallbackEligible = 1u << 1,
		VansShadowGPU_Hero = 1u << 2,
		VansShadowGPU_AffectsFog = 1u << 3,
		VansShadowGPU_AffectsGI = 1u << 4
	};

	struct VansPunctualShadowSettings
	{
		bool castShadows = false;
		VansShadowPolicy policy = VansShadowPolicy::Auto;
		uint8_t priority = 128;
		VansShadowResolution resolution = VansShadowResolution::Auto;
		VansShadowUpdateMode updateMode = VansShadowUpdateMode::OnChange;
		VansShadowFallback fallback = VansShadowFallback::ScreenSpace;

		float maxShadowDistance = 30.0f;
		float nearPlaneOverride = 0.0f;
		float depthBiasTexels = 1.0f;
		float normalBiasTexels = 1.0f;
		float sourceRadius = 0.02f;

		bool affectsVolumetricFog = true;
		bool affectsGI = true;
		uint32_t shadowCasterMask = 0xffffffffu;
	};

	struct VansShadowAtlasBlock
	{
		uint32_t nodeIndex = VANS_INVALID_SHADOW_INDEX;
		uint16_t generation = 0;
		uint16_t resolution = 0;
		uint16_t x = 0;
		uint16_t y = 0;
		uint16_t gutter = 0;

		bool IsValid() const { return nodeIndex != VANS_INVALID_SHADOW_INDEX; }
	};

	struct VansShadowAtlasHandle
	{
		uint32_t index = VANS_INVALID_SHADOW_INDEX;
		uint16_t generation = 0;
		uint16_t viewCount = 0;

		bool IsValid() const { return index != VANS_INVALID_SHADOW_INDEX; }
	};

	struct VansShadowRect
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	struct VansShadowAABB
	{
		glm::vec3 min = glm::vec3((std::numeric_limits<float>::max)());
		glm::vec3 max = glm::vec3(-(std::numeric_limits<float>::max)());

		bool IsValid() const
		{
			return min.x <= max.x && min.y <= max.y && min.z <= max.z;
		}
	};

	struct VansPunctualShadowCameraData
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
		float verticalFovRadians = glm::radians(45.0f);
		float aspectRatio = 1.0f;
		float nearPlane = 0.01f;
		float farPlane = 1000.0f;
		uint32_t viewportWidth = 1920;
		uint32_t viewportHeight = 1080;
	};

	struct VansPunctualShadowLightInput
	{
		uint32_t stableLightId = 0;
		VansPunctualShadowLightType type = VansPunctualShadowLightType::Point;
		uint32_t gpuLightIndex = 0;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
		glm::vec3 color = glm::vec3(1.0f);
		float intensity = 0.0f;
		float radius = 0.0f;
		float outerConeRadians = glm::radians(45.0f);
		float halfWidth = 0.5f;
		float halfHeight = 0.5f;
		VansPunctualShadowSettings settings;
	};

	struct alignas(16) VansPunctualShadowGPU
	{
		uint32_t firstView = VANS_INVALID_SHADOW_INDEX;
		uint32_t viewCount = 0;
		uint32_t flags = VansShadowGPU_None;
		uint32_t generation = 0;

		float atlasWeight = 0.0f;
		float sourceRadius = 0.02f;
		float maxShadowDistance = 0.0f;
		float importance = 0.0f;
	};

	struct alignas(16) VansPunctualShadowViewGPU
	{
		glm::mat4 worldToShadow = glm::mat4(1.0f);
		glm::vec4 atlasScaleBias = glm::vec4(0.0f);
		glm::vec4 atlasClamp = glm::vec4(0.0f);
		glm::vec4 texelBiasParams = glm::vec4(0.0f);
	};

	static_assert(sizeof(VansPunctualShadowGPU) == 32, "PunctualShadowGPU must match std430 layout");
	static_assert(sizeof(VansPunctualShadowViewGPU) == 112, "PunctualShadowViewGPU must match std430 layout");

	struct VansPunctualShadowRenderJob
	{
		uint32_t stableLightId = 0;
		uint32_t gpuLightIndex = 0;
		uint32_t shadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		uint32_t shadowViewIndex = VANS_INVALID_SHADOW_INDEX;
		uint32_t atomicGroupId = 0;
		uint32_t dirtyReasons = VansShadowDirty_None;
		VansPunctualShadowLightType lightType = VansPunctualShadowLightType::Point;
		uint8_t faceIndex = 0;
		uint16_t resolution = 0;
		uint32_t shadowCasterMask = 0xffffffffu;
		VansShadowRect atlasRect;
		glm::mat4 worldToShadow = glm::mat4(1.0f);
		std::vector<uint64_t> casterIds;
	};

	struct VansPunctualShadowBudget
	{
		uint32_t atlasPageBudget = 1024;
		uint64_t maxDirtyTexelsPerFrame = 12ull * 1024ull * 1024ull;
		uint32_t maxShadowDrawsPerFrame = 2000;
		float targetGpuMilliseconds = 2.0f;
	};

	struct VansPunctualShadowStatistics
	{
		uint32_t residentLights = 0;
		uint32_t residentViews = 0;
		uint32_t renderedViews = 0;
		uint32_t fallbackLights = 0;
		uint32_t allocationFailures = 0;
		uint32_t evictions = 0;
		uint32_t usedAtlasPages = 0;
		uint64_t dirtyTexels = 0;
	};

	// RenderCore-owned, editor-agnostic diagnostics. Tooling consumes this
	// through EngineAPILayer DTO conversion; no editor or Vulkan types belong
	// in this contract.
	struct VansPunctualShadowRuntimeDebug
	{
		uint32_t stableLightId = 0;
		uint32_t gpuLightIndex = 0;
		VansPunctualShadowLightType lightType = VansPunctualShadowLightType::Point;
		VansShadowRuntimeState runtimeState = VansShadowRuntimeState::Disabled;
		VansShadowPolicy policy = VansShadowPolicy::Disabled;
		VansShadowFallback fallback = VansShadowFallback::None;
		uint8_t priority = 0;
		uint8_t dirtyFaceMask = 0;
		uint8_t validFaceMask = 0;
		uint16_t activeResolution = 0;
		uint16_t targetResolution = 0;
		float importance = 0.0f;
		float coverage = 0.0f;
		float cameraDistance = 0.0f;
		float distancePriority = 0.0f;
		float atlasWeight = 0.0f;
		uint32_t residencyFrames = 0;
		uint32_t staleFrames = 0;
		uint64_t lastRenderedFrame = 0;
		bool castShadows = false;
		bool affectsVolumetricFog = false;
		bool affectsGI = false;
		std::array<VansShadowAtlasBlock, 6> activeBlocks{};
	};

	struct VansPunctualShadowDebugSnapshot
	{
		uint64_t frameIndex = 0;
		uint32_t atlasSize = 0;
		uint32_t basePageSize = 0;
		uint32_t gutter = 0;
		VansPunctualShadowBudget budget;
		VansPunctualShadowStatistics statistics;
		std::vector<VansPunctualShadowRuntimeDebug> lights;
	};
}
