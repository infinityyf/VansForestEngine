#pragma once

#include "vulkan/vulkan.h"
#include "VansVKCommandBuffer.h"
#include "VansResourceStateTracker.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	enum class VansQueueRole : uint8_t
	{
		Graphics,
		Compute
	};

	enum class VansSyncPoint : uint8_t
	{
		VegetationReady,
		DepthReady,
		GBufferMaterialReady,
		SSAORawReady,
		ShadowMapsReady,
		HairShadowReady,
		TileLightReady,
		ScreenLightingReady,
		CloudReady,
		WaterWaveDone,
		WaterInputsReady,
		WaterPrecomputeDone,
		FrameRenderDone
	};

	struct VansQueueCapabilities
	{
		uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;
		bool hasDedicatedAsyncComputeQueue = false;
		bool HasValidGraphicsQueue() const;
		bool HasValidComputeQueue() const;
		bool SupportsAsyncCompute() const;
	};

	struct VansSubmitSyncWait
	{
		VansSyncPoint point = VansSyncPoint::FrameRenderDone;
		VkPipelineStageFlags stages = 0;
	};

	struct VansExternalSemaphoreWait
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		VkPipelineStageFlags stages = 0;
	};

	struct VansSubmitResourceAccess
	{
		std::string name;
		VkPipelineStageFlags stages = 0;
		VkAccessFlags access = 0;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool image = false;
		bool write = false;
		bool persistent = false;
		bool hostReadable = false;
	};

	struct VansFrameSubmitNode
	{
		std::string name;
		VansQueueRole queue = VansQueueRole::Graphics;
		std::vector<VkCommandBuffer> commandBuffers;
		std::vector<VansSubmitSyncWait> waits;
		std::vector<VansSyncPoint> signals;
		std::vector<VansExternalSemaphoreWait> externalWaits;
		std::vector<VkSemaphore> externalSignals;
		std::vector<VansSubmitResourceAccess> resources;
		VkFence fence = VK_NULL_HANDLE;
		bool waitForCompletion = false;
	};

	bool HasSubmitDependencyPath(
		size_t producer,
		size_t consumer,
		const std::vector<std::vector<size_t>>& edges);

	class VansFrameSubmitOrchestrator
	{
	public:
		void Bind(
			VkDevice device,
			VkQueue graphicsQueue,
			VkQueue computeQueue);
		void Shutdown();
		void Reset();
		void AddNode(VansFrameSubmitNode node);

		bool Validate(std::string* error = nullptr) const;
		bool Execute();
		std::string BuildDebugSummary() const;
		const std::string& GetLastError() const { return m_LastError; }

	private:
		VkQueue ResolveQueue(VansQueueRole role) const;
		bool CreateEdgeSemaphore(VkSemaphore& semaphore);
		void RecycleEdgeSemaphores();
		void DestroySemaphorePool();
		bool Fail(const std::string& message, bool waitForDevice);

		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_ComputeQueue = VK_NULL_HANDLE;
		std::vector<VansFrameSubmitNode> m_Nodes;
		std::vector<VkSemaphore> m_ActiveEdgeSemaphores;
		std::vector<VkSemaphore> m_AvailableEdgeSemaphores;
		mutable VansResourceStateTracker m_ResourceStateTracker;
		std::string m_LastError;
	};

	const char* ToString(VansQueueRole role);
	const char* ToString(VansSyncPoint point);
}
