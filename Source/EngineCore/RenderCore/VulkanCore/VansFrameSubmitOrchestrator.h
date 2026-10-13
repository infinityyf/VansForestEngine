#pragma once

#include "vulkan/vulkan.h"
#include "VansVKCommandBuffer.h"
#include "VansResourceStateTracker.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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
		GTAORawReady,
		ShadowMapsReady,
		TileLightReady,
		ScreenLightingReady,
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
		uint64_t value = 0;
		bool timeline = false;
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
		VansPhysicalResourceIdentity physical;
	};

	struct VansFrameSubmitNode
	{
		std::string name;
		VansQueueRole queue = VansQueueRole::Graphics;
		std::vector<VkCommandBuffer> commandBuffers;
		std::vector<VansSubmitSyncWait> waits;
		std::vector<VansSyncPoint> signals;
		std::vector<VansExternalSemaphoreWait> externalWaits;
		std::vector<SignalSemaphoreInfo> externalSignals;
		std::vector<VansSubmitResourceAccess> resources;
		VkFence fence = VK_NULL_HANDLE;
	};

	struct VansQueueTimelinePoint
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		uint64_t value = 0;
		explicit operator bool() const { return semaphore != VK_NULL_HANDLE && value != 0; }
	};

	// One monotonic timeline per actual VkQueue, shared by all frame-slot graphs.
	// RenderThread serializes allocation and queue submission.
	class VansQueueTimelines final
	{
	public:
		bool Initialize(VkDevice device, VkQueue graphicsQueue, VkQueue computeQueue);
		void Shutdown();
		bool IsReady() const { return m_Graphics.semaphore != VK_NULL_HANDLE; }
		VansQueueTimelinePoint NextSignal(VkQueue queue) const;
		void CommitSignal(VkQueue queue, uint64_t value);

	private:
		struct QueueState
		{
			VkQueue queue = VK_NULL_HANDLE;
			VkSemaphore semaphore = VK_NULL_HANDLE;
			uint64_t lastSubmittedValue = 0;
		};
		const QueueState* Find(VkQueue queue) const;
		QueueState* Find(VkQueue queue);
		VkDevice m_Device = VK_NULL_HANDLE;
		QueueState m_Graphics;
		QueueState m_Compute;
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
		// 已提交的前置阶段：所有节点在任意 GPU stage 开始前等待该 timeline。
		void AddExternalPrerequisite(VansQueueTimelinePoint completion);
        // 图构建完成后，为指定队列节点声明共享的采样输入。
        void AddQueueReadResources(VansQueueRole queue, const std::vector<VansSubmitResourceAccess>& resources);
		void SetQueueTimelines(VansQueueTimelines* timelines) { m_QueueTimelines = timelines; }
		void SetCrossFrameResourceLedger(VansCrossFrameResourceLedger* ledger)
		{
			m_CrossFrameResourceLedger = ledger;
		}

		bool Validate(std::string* error = nullptr) const;
		bool Execute();
		// 提交与完成分离；调用方在复用资源前退役，fence 重置由资源所有者负责。
		bool WaitForCompletion();
		bool HasPendingWork() const { return m_PendingFence != VK_NULL_HANDLE; }
		std::string BuildDebugSummary() const;
		const std::string& GetLastError() const { return m_LastError; }
		bool WasDeviceLost() const { return m_DeviceLost; }
		std::optional<VansQueueTimelinePoint> GetSubmittedTimelinePoint(
			const std::string& nodeName) const;

	private:
		VkQueue ResolveQueue(VansQueueRole role) const;
		bool CreateEdgeSemaphore(VkSemaphore& semaphore);
		void RecycleEdgeSemaphores();
		void DestroyActiveEdgeSemaphores();
		void DestroySemaphorePool();
		bool Fail(const std::string& message);

		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_ComputeQueue = VK_NULL_HANDLE;
		VkFence m_PendingFence = VK_NULL_HANDLE;
		std::vector<VansFrameSubmitNode> m_Nodes;
		std::vector<VkSemaphore> m_ActiveEdgeSemaphores;
		std::vector<VkSemaphore> m_AvailableEdgeSemaphores;
		VansQueueTimelines* m_QueueTimelines = nullptr;
		VansCrossFrameResourceLedger* m_CrossFrameResourceLedger = nullptr;
		std::vector<std::pair<std::string, VansQueueTimelinePoint>> m_SubmittedTimelinePoints;
		mutable VansResourceStateTracker m_ResourceStateTracker;
		std::string m_LastError;
		bool m_DeviceLost = false;
	};

	const char* ToString(VansQueueRole role);
	const char* ToString(VansSyncPoint point);
}
