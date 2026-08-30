#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansFrameSubmitOrchestrator.h"
#include "../../Util/VansLog.h"

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
	DestroySemaphorePool();
	m_Nodes.clear();
	m_LastError.clear();
	m_Device = VK_NULL_HANDLE;
	m_GraphicsQueue = VK_NULL_HANDLE;
	m_ComputeQueue = VK_NULL_HANDLE;
}

void VansGraphics::VansFrameSubmitOrchestrator::Reset()
{
	// Active edges are recycled by Execute after the final completion fence, or
	// by the failure path after device idle. Reaching Reset with active edges is
	// a lifecycle error, so keep them visible to validation instead of reusing them.
	m_Nodes.clear();
	m_LastError.clear();
}

void VansGraphics::VansFrameSubmitOrchestrator::AddNode(VansFrameSubmitNode node)
{
	m_Nodes.emplace_back(std::move(node));
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
	if (m_Nodes.empty())
		return setError("frame submit graph contains no nodes");
	if (!m_ActiveEdgeSemaphores.empty())
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
		}
		for (VkSemaphore signal : node.externalSignals)
		{
			if (signal == VK_NULL_HANDLE)
				return setError("frame submit node contains a null external signal: " + node.name);
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
	bool hasInternalWait = false;
	for (const VansFrameSubmitNode& node : m_Nodes)
		hasInternalWait = hasInternalWait || !node.waits.empty();
	if (hasInternalWait && !m_Nodes.back().waitForCompletion)
		return setError("binary sync edges require a completion fence on the final node");
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
			<< " commandBuffers=" << node.commandBuffers.size()
			<< " waitForCompletion=" << (node.waitForCompletion ? "true" : "false") << '\n';
		for (const VansSubmitSyncWait& wait : node.waits)
		{
			stream << "    wait " << ToString(wait.point)
				<< " stages=0x" << std::hex << wait.stages << std::dec << '\n';
		}
		for (VansSyncPoint signal : node.signals)
			stream << "    signal " << ToString(signal) << '\n';
		for (const VansExternalSemaphoreWait& wait : node.externalWaits)
		{
			stream << "    externalWait stages=0x" << std::hex << wait.stages << std::dec << '\n';
		}
		if (!node.externalSignals.empty())
			stream << "    externalSignals=" << node.externalSignals.size() << '\n';
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

bool VansGraphics::VansFrameSubmitOrchestrator::Execute()
{
	m_LastError.clear();
	std::string validationError;
	if (!Validate(&validationError))
		return Fail(validationError, false);
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
				return Fail(std::string("failed to create semaphore for sync point: ") + ToString(wait.point), false);
			edges.emplace_back(edge);
		}
	}

	bool anySubmitSucceeded = false;
	std::vector<VkFence> submittedFences;
	for (size_t nodeIndex = 0; nodeIndex < m_Nodes.size(); ++nodeIndex)
	{
		const VansFrameSubmitNode& node = m_Nodes[nodeIndex];
		std::vector<WaitSemaphoreInfo> waits;
		std::vector<VkSemaphore> signals = node.externalSignals;
		waits.reserve(node.externalWaits.size() + node.waits.size());

		for (const VansExternalSemaphoreWait& externalWait : node.externalWaits)
			waits.push_back({ externalWait.semaphore, externalWait.stages });
		for (const WaitEdge& edge : edges)
		{
			if (edge.consumerNode == nodeIndex)
				waits.push_back({ edge.semaphore, edge.stages });
			if (edge.producerNode == nodeIndex)
				signals.push_back(edge.semaphore);
		}

		VkQueue queue = ResolveQueue(node.queue);
		if (!VansVKCommandBuffer::SubmitCommands(
			queue,
			m_Device,
			node.commandBuffers,
			waits,
			signals,
			node.fence,
			node.waitForCompletion))
		{
			if (anySubmitSucceeded)
			{
				VansGraphics::vkDeviceWaitIdle(m_Device);
				if (!submittedFences.empty())
					VansGraphics::vkResetFences(m_Device, static_cast<uint32_t>(submittedFences.size()), submittedFences.data());
			}
			return Fail("queue submit failed for node: " + node.name, false);
		}
		anySubmitSucceeded = true;
		if (node.fence != VK_NULL_HANDLE && !node.waitForCompletion)
			submittedFences.push_back(node.fence);
	}

	// Edge semaphores are frame-local. A completion wait on the final node proves
	// all transitive producers and consumers are no longer using them.
	if (m_Nodes.back().waitForCompletion)
		RecycleEdgeSemaphores();
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

void VansGraphics::VansFrameSubmitOrchestrator::DestroySemaphorePool()
{
	if (m_Device != VK_NULL_HANDLE)
	{
		for (VkSemaphore semaphore : m_ActiveEdgeSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
				VansGraphics::vkDestroySemaphore(m_Device, semaphore, nullptr);
		}
		for (VkSemaphore semaphore : m_AvailableEdgeSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
				VansGraphics::vkDestroySemaphore(m_Device, semaphore, nullptr);
		}
	}
	m_ActiveEdgeSemaphores.clear();
	m_AvailableEdgeSemaphores.clear();
}

bool VansGraphics::VansFrameSubmitOrchestrator::Fail(const std::string& message, bool waitForDevice)
{
	m_LastError = message;
	VANS_LOG_ERROR("[FrameSubmitOrchestrator] " << message);
	if (waitForDevice && m_Device != VK_NULL_HANDLE)
		VansGraphics::vkDeviceWaitIdle(m_Device);
	RecycleEdgeSemaphores();
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
	case VansSyncPoint::SSAORawReady: return "SSAORawReady";
	case VansSyncPoint::ShadowMapsReady: return "ShadowMapsReady";
	case VansSyncPoint::HairShadowReady: return "HairShadowReady";
	case VansSyncPoint::TileLightReady: return "TileLightReady";
	case VansSyncPoint::ScreenLightingReady: return "ScreenLightingReady";
	case VansSyncPoint::WaterWaveDone: return "WaterWaveDone";
	case VansSyncPoint::WaterInputsReady: return "WaterInputsReady";
	case VansSyncPoint::WaterPrecomputeDone: return "WaterPrecomputeDone";
	case VansSyncPoint::FrameRenderDone: return "FrameRenderDone";
	}
	return "Unknown";
}
