#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansFrameSubmitOrchestrator.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"

#include <algorithm>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
	struct EnumHash
	{
		template <typename T>
		size_t operator()(T value) const
		{
			return static_cast<size_t>(value);
		}
	};

	struct WaitEdge
	{
		size_t producerNode = 0;
		size_t consumerNode = 0;
		VansGraphics::VansSyncPoint point = VansGraphics::VansSyncPoint::FrameRenderDone;
		VkPipelineStageFlags stages = 0;
		VkSemaphore semaphore = VK_NULL_HANDLE;
	};

}

bool VansGraphics::HasSubmitDependencyPath(
	size_t producer,
	size_t consumer,
	const std::vector<std::vector<size_t>>& edges)
{
	if (producer == consumer)
		return true;
	std::vector<bool> visited(edges.size(), false);
	std::queue<size_t> pending;
	pending.push(producer);
	visited[producer] = true;
	while (!pending.empty())
	{
		const size_t current = pending.front();
		pending.pop();
		for (size_t next : edges[current])
		{
			if (next == consumer)
				return true;
			if (!visited[next])
			{
				visited[next] = true;
				pending.push(next);
			}
		}
	}
	return false;
}

bool VansGraphics::VansQueueCapabilities::HasValidGraphicsQueue() const
{
	return graphicsFamily != VK_QUEUE_FAMILY_IGNORED;
}

bool VansGraphics::VansQueueCapabilities::HasValidComputeQueue() const
{
	return computeFamily != VK_QUEUE_FAMILY_IGNORED;
}

bool VansGraphics::VansQueueCapabilities::SupportsAsyncCompute() const
{
	return HasValidGraphicsQueue() && HasValidComputeQueue() && hasDedicatedAsyncComputeQueue;
}

bool VansGraphics::VansQueueTimelines::Initialize(
	VkDevice device, VkQueue graphicsQueue, VkQueue computeQueue)
{
	Shutdown();
	if (device == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE ||
		computeQueue == VK_NULL_HANDLE)
		return false;
	m_Device = device;
	m_Graphics.queue = graphicsQueue;
	m_Compute.queue = computeQueue;
	VkSemaphoreTypeCreateInfo typeInfo{};
	typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	VkSemaphoreCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = &typeInfo;
	if (VansGraphics::vkCreateSemaphore(device, &createInfo, nullptr,
		&m_Graphics.semaphore) != VK_SUCCESS)
	{
		Shutdown();
		return false;
	}
	if (computeQueue != graphicsQueue &&
		VansGraphics::vkCreateSemaphore(device, &createInfo, nullptr,
			&m_Compute.semaphore) != VK_SUCCESS)
	{
		Shutdown();
		return false;
	}
	return true;
}

void VansGraphics::VansQueueTimelines::Shutdown()
{
	if (m_Device != VK_NULL_HANDLE)
	{
		if (m_Compute.semaphore != VK_NULL_HANDLE)
			VansGraphics::vkDestroySemaphore(m_Device, m_Compute.semaphore, nullptr);
		if (m_Graphics.semaphore != VK_NULL_HANDLE)
			VansGraphics::vkDestroySemaphore(m_Device, m_Graphics.semaphore, nullptr);
	}
	m_Device = VK_NULL_HANDLE;
	m_Graphics = {};
	m_Compute = {};
}

const VansGraphics::VansQueueTimelines::QueueState*
VansGraphics::VansQueueTimelines::Find(VkQueue queue) const
{
	if (queue == m_Graphics.queue)
		return &m_Graphics;
	if (queue == m_Compute.queue)
		return &m_Compute;
	return nullptr;
}

VansGraphics::VansQueueTimelines::QueueState*
VansGraphics::VansQueueTimelines::Find(VkQueue queue)
{
	return const_cast<QueueState*>(
		static_cast<const VansQueueTimelines*>(this)->Find(queue));
}

VansGraphics::VansQueueTimelinePoint
VansGraphics::VansQueueTimelines::NextSignal(VkQueue queue) const
{
	const QueueState* state = Find(queue);
	if (state == nullptr || state->semaphore == VK_NULL_HANDLE ||
		state->lastSubmittedValue == UINT64_MAX)
		return {};
	return { state->semaphore, state->lastSubmittedValue + 1 };
}

void VansGraphics::VansQueueTimelines::CommitSignal(VkQueue queue, uint64_t value)
{
	QueueState* state = Find(queue);
	if (state != nullptr && state->semaphore != VK_NULL_HANDLE &&
		value == state->lastSubmittedValue + 1)
		state->lastSubmittedValue = value;
}

void VansGraphics::VansFrameSubmitOrchestrator::Bind(
	VkDevice device,
	VkQueue graphicsQueue,
	VkQueue computeQueue)
{
	m_Device = device;
	m_GraphicsQueue = graphicsQueue;
	m_ComputeQueue = computeQueue;
}

void VansGraphics::VansFrameSubmitOrchestrator::Shutdown()
{
	if (!WaitForCompletion() && m_Device != VK_NULL_HANDLE)
		VansGraphics::vkDeviceWaitIdle(m_Device);
	DestroySemaphorePool();
	m_PendingFence = VK_NULL_HANDLE;
	m_Nodes.clear();
	m_LastError.clear();
	m_DeviceLost = false;
	m_Device = VK_NULL_HANDLE;
	m_GraphicsQueue = VK_NULL_HANDLE;
	m_ComputeQueue = VK_NULL_HANDLE;
	m_QueueTimelines = nullptr;
	m_CrossFrameResourceLedger = nullptr;
	m_SubmittedTimelinePoints.clear();
}

void VansGraphics::VansFrameSubmitOrchestrator::Reset()
{
	// Reset 只开始构建下一张图，不得回收尚由 GPU 使用的同步资源。
	m_Nodes.clear();
	m_LastError.clear();
	m_SubmittedTimelinePoints.clear();
}

void VansGraphics::VansFrameSubmitOrchestrator::AddNode(VansFrameSubmitNode node)
{
	m_Nodes.emplace_back(std::move(node));
}
void VansGraphics::VansFrameSubmitOrchestrator::AddExternalPrerequisite(VansQueueTimelinePoint completion)
{
	if (!completion) return;
	for (auto& node : m_Nodes)
	{
		auto existing = std::find_if(node.externalWaits.begin(), node.externalWaits.end(),
			[&](const auto& wait) { return wait.timeline && wait.semaphore == completion.semaphore; });
		if (existing == node.externalWaits.end())
			node.externalWaits.push_back({completion.semaphore, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, completion.value, true});
		else { existing->value = std::max(existing->value, completion.value); existing->stages |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; }
	}
}
void VansGraphics::VansFrameSubmitOrchestrator::AddQueueReadResources(VansQueueRole queue, const std::vector<VansSubmitResourceAccess>& resources)
{
    for (auto& node:m_Nodes) if (node.queue==queue)
        node.resources.insert(node.resources.end(),resources.begin(),resources.end());
}

bool VansGraphics::VansFrameSubmitOrchestrator::Validate(std::string* error) const
{
	auto setError = [&](const std::string& message)
	{
		if (error != nullptr)
			*error = message;
		return false;
	};

	if (m_Device == VK_NULL_HANDLE)
		return setError("frame submit orchestrator has no Vulkan device");
	if (m_CrossFrameResourceLedger != nullptr &&
		(m_QueueTimelines == nullptr || !m_CrossFrameResourceLedger->IsUsable()))
		return setError("cross-frame resource ledger has no usable queue timeline");
	if (m_Nodes.empty())
		return setError("frame submit graph contains no nodes");
	if (HasPendingWork() || !m_ActiveEdgeSemaphores.empty())
		return setError("previous frame still owns active sync semaphores");

	std::unordered_map<VansSyncPoint, size_t, EnumHash> producers;
	std::unordered_set<std::string> names;
	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		const VansFrameSubmitNode& node = m_Nodes[nodeIndex];
		if (node.name.empty())
			return setError("frame submit node has an empty name");
		if (!names.emplace(node.name).second)
			return setError("duplicate frame submit node name: " + node.name);
		if (ResolveQueue(node.queue) == VK_NULL_HANDLE)
			return setError("frame submit node has no bound queue: " + node.name);
		if (node.commandBuffers.empty())
			return setError("frame submit node has no command buffer: " + node.name);
		for (VkCommandBuffer commandBuffer : node.commandBuffers)
		{
			if (commandBuffer == VK_NULL_HANDLE)
				return setError("frame submit node contains a null command buffer: " + node.name);
		}
		for (const VansExternalSemaphoreWait& wait : node.externalWaits)
		{
			if (wait.semaphore == VK_NULL_HANDLE || wait.stages == 0)
				return setError("frame submit node contains an invalid external wait: " + node.name);
			if (!wait.timeline && wait.value != 0)
				return setError("binary external wait has a timeline value: " + node.name);
		}
		for (const SignalSemaphoreInfo& signal : node.externalSignals)
		{
			if (signal.semaphore == VK_NULL_HANDLE)
				return setError("frame submit node contains a null external signal: " + node.name);
			if (signal.timeline && signal.value == 0)
				return setError("frame submit node has a zero timeline signal value: " + node.name);
			if (!signal.timeline && signal.value != 0)
				return setError("binary external signal has a timeline value: " + node.name);
		}
		for (VansSyncPoint point : node.signals)
		{
			if (!producers.emplace(point, nodeIndex).second)
				return setError(std::string("sync point has multiple producers: ") + ToString(point));
		}
	}

	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		for (const VansSubmitSyncWait& wait : m_Nodes[nodeIndex].waits)
		{
			if (wait.stages == 0)
				return setError("sync wait has an empty stage mask: " + m_Nodes[nodeIndex].name);
			const auto producer = producers.find(wait.point);
			if (producer == producers.end())
				return setError(std::string("sync point has no producer: ") + ToString(wait.point));
			if (producer->second >= nodeIndex)
				return setError(std::string("sync dependency is not topologically ordered: ") + ToString(wait.point));
		}
	}
	if (m_Nodes.back().fence == VK_NULL_HANDLE)
		return setError("frame submit graph requires a completion fence on the final node");
	std::string resourceError;
	if (!m_ResourceStateTracker.ValidateAndBuild(m_Nodes, &resourceError))
		return setError(resourceError);

	// The final completion fence is also the lifetime proof for every frame-local
	// edge semaphore and command buffer. Reject disconnected work instead of
	// relying on an implicit queue-idle assumption.
	const size_t finalNodeIndex = m_Nodes.size() - 1u;
	std::vector<std::vector<size_t>> dependencyEdges(m_Nodes.size());
	std::unordered_map<VkQueue, size_t> previousQueueNode;
	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		const VkQueue queue = ResolveQueue(m_Nodes[nodeIndex].queue);
		const auto previous = previousQueueNode.find(queue);
		if (previous != previousQueueNode.end())
			dependencyEdges[previous->second].push_back(nodeIndex);
		previousQueueNode[queue] = nodeIndex;
		for (const VansSubmitSyncWait& wait : m_Nodes[nodeIndex].waits)
			dependencyEdges[producers.at(wait.point)].push_back(nodeIndex);
	}
	for (size_t nodeIndex = 0; nodeIndex < finalNodeIndex; ++nodeIndex)
	{
		if (!HasSubmitDependencyPath(nodeIndex, finalNodeIndex, dependencyEdges))
			return setError("frame submit node is not covered by the final completion fence: "
				+ m_Nodes[nodeIndex].name);
	}

	return true;
}

std::string VansGraphics::VansFrameSubmitOrchestrator::BuildDebugSummary() const
{
	std::ostringstream stream;
	stream << "FrameSubmitGraph nodes=" << m_Nodes.size() << '\n';
	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		const VansFrameSubmitNode& node = m_Nodes[nodeIndex];
		stream << "  [" << nodeIndex << "] " << node.name
			<< " queue=" << ToString(node.queue)
			<< " commandBuffers=" << node.commandBuffers.size() << '\n';
		for (const VansSubmitSyncWait& wait : node.waits)
		{
			stream << "    wait " << ToString(wait.point)
				<< " stages=0x" << std::hex << wait.stages << std::dec << '\n';
		}
		for (VansSyncPoint signal : node.signals)
			stream << "    signal " << ToString(signal) << '\n';
		for (const VansExternalSemaphoreWait& wait : node.externalWaits)
		{
			stream << "    externalWait stages=0x" << std::hex << wait.stages << std::dec
				<< " timeline=" << (wait.timeline ? "true" : "false")
				<< " value=" << wait.value << '\n';
		}
		for (const SignalSemaphoreInfo& signal : node.externalSignals)
			stream << "    externalSignal timeline=" << (signal.timeline ? "true" : "false")
				<< " value=" << signal.value << '\n';
		for (const VansSubmitResourceAccess& resource : node.resources)
		{
			stream << "    resource " << resource.name
				<< " stages=0x" << std::hex << resource.stages
				<< " access=0x" << resource.access << std::dec
				<< " layout=" << static_cast<int>(resource.layout)
				<< " image=" << (resource.image ? "true" : "false")
				<< " write=" << (resource.write ? "true" : "false")
				<< " persistent=" << (resource.persistent ? "true" : "false")
				<< " hostReadable=" << (resource.hostReadable ? "true" : "false") << '\n';
		}
	}
	stream << m_ResourceStateTracker.BuildDebugSummary();
	return stream.str();
}

std::optional<VansGraphics::VansQueueTimelinePoint>
VansGraphics::VansFrameSubmitOrchestrator::GetSubmittedTimelinePoint(
	const std::string& nodeName) const
{
	for (const auto& [name, point] : m_SubmittedTimelinePoints)
	{
		if (name == nodeName)
			return point;
	}
	return std::nullopt;
}

bool VansGraphics::VansFrameSubmitOrchestrator::Execute()
{
	m_LastError.clear();
	m_SubmittedTimelinePoints.clear();
	if (m_DeviceLost)
		return Fail("frame submit graph cannot continue after device loss");
	std::string validationError;
	if (!Validate(&validationError))
		return Fail(validationError);
	std::unordered_map<VansSyncPoint, size_t, EnumHash> producers;
	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		for (VansSyncPoint point : m_Nodes[nodeIndex].signals)
			producers.emplace(point, nodeIndex);
	}

	std::vector<WaitEdge> edges;
	for (size_t consumerIndex = 0; consumerIndex < m_Nodes.size(); ++consumerIndex)
	{
		for (const VansSubmitSyncWait& wait : m_Nodes[consumerIndex].waits)
		{
			WaitEdge edge;
			edge.producerNode = producers.at(wait.point);
			edge.consumerNode = consumerIndex;
			edge.point = wait.point;
			edge.stages = wait.stages;
			if (!CreateEdgeSemaphore(edge.semaphore))
			{
				RecycleEdgeSemaphores(); // 尚未提交，全部信号量仍未 signaled。
				return Fail(std::string("failed to create semaphore for sync point: ") + ToString(wait.point));
			}
			edges.emplace_back(edge);
		}
	}

	bool anySubmitSucceeded = false;
	std::vector<VkFence> submittedFences;
	std::vector<std::pair<std::string, VansQueueTimelinePoint>> submittedPoints;
	const VansCrossFrameResourceLedger previousFrameResources =
		m_CrossFrameResourceLedger != nullptr
			? *m_CrossFrameResourceLedger : VansCrossFrameResourceLedger{};
	const auto abortPartialSubmit = [&](const std::string& message)
	{
		// Consumers with persistent state must know which nodes actually ran,
		// even when a later node fails and the submitted work is retired here.
		m_SubmittedTimelinePoints = std::move(submittedPoints);
		if (anySubmitSucceeded)
		{
			const VkResult idleResult = VansGraphics::vkDeviceWaitIdle(m_Device);
			m_DeviceLost = m_DeviceLost || idleResult == VK_ERROR_DEVICE_LOST;
			if (m_CrossFrameResourceLedger != nullptr)
			{
				if (idleResult == VK_SUCCESS)
					m_CrossFrameResourceLedger->Clear();
				else
					m_CrossFrameResourceLedger->Poison();
			}
			if (idleResult == VK_SUCCESS && !submittedFences.empty())
			{
				const VkResult resetResult = VansGraphics::vkResetFences(m_Device,
					static_cast<uint32_t>(submittedFences.size()), submittedFences.data());
				m_DeviceLost = m_DeviceLost || resetResult == VK_ERROR_DEVICE_LOST;
			}
		}
		// A partial submit may leave a signaled binary edge with no consumer.
		DestroyActiveEdgeSemaphores();
		return Fail(message);
	};
	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		const VansFrameSubmitNode& node = m_Nodes[nodeIndex];
		std::vector<WaitSemaphoreInfo> waits;
		std::vector<SignalSemaphoreInfo> signals = node.externalSignals;
		waits.reserve(node.externalWaits.size() + node.waits.size());

		for (const VansExternalSemaphoreWait& externalWait : node.externalWaits)
			waits.push_back({ externalWait.semaphore, externalWait.stages,
				externalWait.value, externalWait.timeline });
		for (const WaitEdge& edge : edges)
		{
			if (edge.consumerNode == nodeIndex)
				waits.push_back({ edge.semaphore, edge.stages, 0 });
			if (edge.producerNode == nodeIndex)
				signals.push_back({ edge.semaphore, 0 });
		}

		VkQueue queue = ResolveQueue(node.queue);
		if (m_CrossFrameResourceLedger != nullptr)
		{
			std::vector<VansCrossFrameResourceWait> resourceWaits;
			for (const VansSubmitResourceAccess& resource : node.resources)
				previousFrameResources.CollectWaits(resource.physical, resource.write,
					resource.stages, resourceWaits);
			for (const VansCrossFrameResourceWait& resourceWait : resourceWaits)
			{
				auto existing = std::find_if(waits.begin(), waits.end(),
					[&](const WaitSemaphoreInfo& wait)
					{
						return wait.semaphore == resourceWait.semaphore && wait.timeline;
					});
				if (existing == waits.end())
					waits.push_back({ resourceWait.semaphore, resourceWait.stages,
						resourceWait.value, true });
				else
				{
					existing->value = std::max(existing->value, resourceWait.value);
					existing->waiting_stage |= resourceWait.stages;
				}
			}
		}
		VansQueueTimelinePoint timelinePoint;
		if (m_QueueTimelines != nullptr)
		{
			timelinePoint = m_QueueTimelines->NextSignal(queue);
			if (!timelinePoint)
				return abortPartialSubmit("frame submit node has no queue timeline signal: " + node.name);
			signals.push_back({ timelinePoint.semaphore, timelinePoint.value, true });
		}
		VkResult submitResult = VK_SUCCESS;
		if (!VansVKCommandBuffer::SubmitCommands(
			queue,
			m_Device,
			node.commandBuffers,
			waits,
			signals,
			node.fence,
			false,
			&submitResult))
		{
			m_DeviceLost = m_DeviceLost || submitResult == VK_ERROR_DEVICE_LOST;
			return abortPartialSubmit("queue submit failed for node: " + node.name);
		}
		anySubmitSucceeded = true;
		if (timelinePoint)
		{
			m_QueueTimelines->CommitSignal(queue, timelinePoint.value);
			submittedPoints.emplace_back(node.name, timelinePoint);
			if (m_CrossFrameResourceLedger != nullptr)
			{
				for (const VansSubmitResourceAccess& resource : node.resources)
					m_CrossFrameResourceLedger->PublishAccess(resource.physical,
						resource.write, queue, timelinePoint.semaphore, timelinePoint.value);
			}
		}
		if (node.fence != VK_NULL_HANDLE)
			submittedFences.push_back(node.fence);
	}

	m_PendingFence = m_Nodes.back().fence;
	m_SubmittedTimelinePoints = std::move(submittedPoints);
	return true;
}

bool VansGraphics::VansFrameSubmitOrchestrator::WaitForCompletion()
{
	if (!HasPendingWork()) return true;
	VANS_PROFILE_WAIT("Vulkan::WaitFence.FrameGraph");
	const VkResult waitResult = VansGraphics::vkWaitForFences(
		m_Device, 1, &m_PendingFence, VK_TRUE, UINT64_MAX);
	if (waitResult != VK_SUCCESS)
	{
		m_DeviceLost = m_DeviceLost || waitResult == VK_ERROR_DEVICE_LOST;
		return Fail("failed to retire submitted frame graph");
	}
	RecycleEdgeSemaphores();
	m_PendingFence = VK_NULL_HANDLE;
	return true;
}

VkQueue VansGraphics::VansFrameSubmitOrchestrator::ResolveQueue(VansQueueRole role) const
{
	switch (role)
	{
	case VansQueueRole::Graphics: return m_GraphicsQueue;
	case VansQueueRole::Compute: return m_ComputeQueue;
	}
	return VK_NULL_HANDLE;
}

bool VansGraphics::VansFrameSubmitOrchestrator::CreateEdgeSemaphore(VkSemaphore& semaphore)
{
	if (!m_AvailableEdgeSemaphores.empty())
	{
		semaphore = m_AvailableEdgeSemaphores.back();
		m_AvailableEdgeSemaphores.pop_back();
		m_ActiveEdgeSemaphores.push_back(semaphore);
		return true;
	}

	VkSemaphoreCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	const VkResult result = VansGraphics::vkCreateSemaphore(m_Device, &createInfo, nullptr, &semaphore);
	if (result != VK_SUCCESS)
	{
		m_DeviceLost = m_DeviceLost || result == VK_ERROR_DEVICE_LOST;
		semaphore = VK_NULL_HANDLE;
		return false;
	}
	m_ActiveEdgeSemaphores.push_back(semaphore);
	return true;
}

void VansGraphics::VansFrameSubmitOrchestrator::RecycleEdgeSemaphores()
{
	m_AvailableEdgeSemaphores.insert(
		m_AvailableEdgeSemaphores.end(),
		m_ActiveEdgeSemaphores.begin(),
		m_ActiveEdgeSemaphores.end());
	m_ActiveEdgeSemaphores.clear();
}

void VansGraphics::VansFrameSubmitOrchestrator::DestroyActiveEdgeSemaphores()
{
	if (m_Device != VK_NULL_HANDLE)
	{
		for (VkSemaphore semaphore : m_ActiveEdgeSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
				VansGraphics::vkDestroySemaphore(m_Device, semaphore, nullptr);
		}
	}
	m_ActiveEdgeSemaphores.clear();
}

void VansGraphics::VansFrameSubmitOrchestrator::DestroySemaphorePool()
{
	DestroyActiveEdgeSemaphores();
	if (m_Device != VK_NULL_HANDLE)
	{
		for (VkSemaphore semaphore : m_AvailableEdgeSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
				VansGraphics::vkDestroySemaphore(m_Device, semaphore, nullptr);
		}
	}
	m_AvailableEdgeSemaphores.clear();
}

bool VansGraphics::VansFrameSubmitOrchestrator::Fail(const std::string& message)
{
	m_LastError = message;
	VANS_LOG_ERROR("[FrameSubmitOrchestrator] " << message);
	return false;
}

const char* VansGraphics::ToString(VansQueueRole role)
{
	switch (role)
	{
	case VansQueueRole::Graphics: return "Graphics";
	case VansQueueRole::Compute: return "Compute";
	}
	return "Unknown";
}

const char* VansGraphics::ToString(VansSyncPoint point)
{
	switch (point)
	{
	case VansSyncPoint::VegetationReady: return "VegetationReady";
	case VansSyncPoint::DepthReady: return "DepthReady";
	case VansSyncPoint::GBufferMaterialReady: return "GBufferMaterialReady";
	case VansSyncPoint::GTAORawReady: return "GTAORawReady";
	case VansSyncPoint::ShadowMapsReady: return "ShadowMapsReady";
	case VansSyncPoint::TileLightReady: return "TileLightReady";
	case VansSyncPoint::ScreenLightingReady: return "ScreenLightingReady";
	case VansSyncPoint::WaterWaveDone: return "WaterWaveDone";
	case VansSyncPoint::WaterInputsReady: return "WaterInputsReady";
	case VansSyncPoint::WaterPrecomputeDone: return "WaterPrecomputeDone";
	case VansSyncPoint::FrameRenderDone: return "FrameRenderDone";
	}
	return "Unknown";
}
