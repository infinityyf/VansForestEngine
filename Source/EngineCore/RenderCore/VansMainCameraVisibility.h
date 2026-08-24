#pragma once

#include "VansRenderBounds.h"
#include "VansRenderProxyHandle.h"
#include "VansRenderSceneSnapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansCamera;
	class VansRenderNode;

	enum VansMainCameraCullFlags : uint32_t
	{
		VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE = 1u << 0,
		VANS_MAIN_CAMERA_CULL_TRANSPARENT = 1u << 1,
	};

	struct VansMainCameraCullCandidate
	{
		uint64_t nodeId = 0;
		std::string nodeName;
		VansRenderBounds bounds;
		VansMainCameraCullClass cullClass = VansMainCameraCullClass::Opaque;
		uint32_t flags = 0;
		uint32_t visibilityIndex = 0;
	};

	struct alignas(16) VansMainCameraCullObjectGPU
	{
		glm::vec4 center = glm::vec4(0.0f);
		glm::vec4 axisXHalf = glm::vec4(0.0f);
		glm::vec4 axisYHalf = glm::vec4(0.0f);
		glm::vec4 axisZHalf = glm::vec4(0.0f);
		uint32_t nodeIndex = 0;
		uint32_t flags = 0;
		uint32_t padding0 = 0;
		uint32_t padding1 = 0;
	};

	struct alignas(16) VansMainCameraHiZCullPushConstants
	{
		uint32_t objectCount = 0;
		uint32_t hizMipCount = 0;
		uint32_t hizEnabled = 0;
		uint32_t frameIndex = 0;
		float depthBiasMeters = 0.35f;
		float maxScreenCoverageForCull = 0.65f;
		float padding0 = 0.0f;
		float padding1 = 0.0f;
	};

	struct VansMainCameraHiZHistoryState
	{
		glm::mat4 previousViewProjection = glm::mat4(1.0f);
		glm::vec3 previousCameraPosition = glm::vec3(0.0f);
		glm::vec3 previousCameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::uvec2 previousExtent{ 0u, 0u };
		float previousFov = 0.0f;
		float previousNearClip = 0.0f;
		float previousFarClip = 0.0f;
		uint32_t invalidFrames = 2;
		bool hasValidHistory = false;
	};

	struct VansMainCameraVisibilityStats
	{
		uint32_t candidateCount = 0;
		uint32_t frustumVisibleCount = 0;
		uint32_t hizCulledCount = 0;
		uint32_t forcedVisibleCount = 0;
		uint32_t preCullDrawCallCount = 0;
		uint32_t culledDrawCallCount = 0;
		uint32_t drawnDrawCallCount = 0;
		bool enabled = false;
		bool historyValid = false;
	};

	struct VansMainCameraHiZCulledNodeDebug
	{
		uint64_t nodeId = 0;
		std::string nodeName;
		VansMainCameraCullClass cullClass = VansMainCameraCullClass::Opaque;
		VansRenderBounds bounds;
	};

	struct VansMainCameraVisibilityDebugSnapshot final
	{
		VansMainCameraVisibilityStats stats;
		std::vector<VansMainCameraHiZCulledNodeDebug> culledNodes;
	};

	bool TryGetStaticNodeWorldBounds(VansRenderNode* node, VansRenderBounds& bounds);
	bool IsNodeVisibleInFrustum(VansRenderNode* node, const glm::mat4& worldToClip);
}
