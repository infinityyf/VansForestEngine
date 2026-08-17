#include "VansResourceStateTracker.h"

#include "VansFrameSubmitOrchestrator.h"

#include <queue>
#include <sstream>
#include <unordered_map>

namespace
{
	struct ResourceAccessState
	{
		size_t nodeIndex = 0;
		VansGraphics::VansQueueRole queue = VansGraphics::VansQueueRole::Graphics;
		VansGraphics::VansSubmitResourceAccess access;
	};

	bool HasDependencyPath(
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
}

bool VansGraphics::VansResourceStateTracker::ValidateAndBuild(
	const std::vector<VansFrameSubmitNode>& nodes,
	std::string* error)
{
	m_States.clear();
	auto fail = [&](const std::string& message)
	{
		if (error != nullptr)
			*error = message;
		return false;
	};

	std::unordered_map<VansSyncPoint, size_t> syncProducers;
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		for (VansSyncPoint point : nodes[nodeIndex].signals)
			syncProducers.emplace(point, nodeIndex);
	}

	std::vector<std::vector<size_t>> dependencyEdges(nodes.size());
	std::unordered_map<VansQueueRole, size_t> previousQueueNode;
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const auto previous = previousQueueNode.find(nodes[nodeIndex].queue);
		if (previous != previousQueueNode.end())
			dependencyEdges[previous->second].push_back(nodeIndex);
		previousQueueNode[nodes[nodeIndex].queue] = nodeIndex;
		for (const VansSubmitSyncWait& wait : nodes[nodeIndex].waits)
		{
			const auto producer = syncProducers.find(wait.point);
			if (producer != syncProducers.end())
				dependencyEdges[producer->second].push_back(nodeIndex);
		}
	}

	std::unordered_map<std::string, ResourceAccessState> lastAccesses;
	std::unordered_map<std::string, size_t> stateIndices;
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const VansFrameSubmitNode& node = nodes[nodeIndex];
		for (const VansSubmitResourceAccess& access : node.resources)
		{
			if (access.name.empty() || access.stages == 0 || access.access == 0)
				return fail("invalid resource contract in submit node: " + node.name);
			if (access.image && access.layout == VK_IMAGE_LAYOUT_UNDEFINED)
				return fail("image resource contract has undefined layout: " + access.name);
			if (access.write && access.hostReadable && node.fence == VK_NULL_HANDLE)
				return fail("host-readable resource writer has no completion fence: " + access.name);

			const auto previous = lastAccesses.find(access.name);
			if (previous != lastAccesses.end())
			{
				const ResourceAccessState& prior = previous->second;
				const bool hazard = prior.access.write || access.write;
				if (hazard && prior.queue != node.queue
					&& !HasDependencyPath(prior.nodeIndex, nodeIndex, dependencyEdges))
				{
					return fail("cross-queue resource hazard has no sync path: " + access.name
						+ " (" + nodes[prior.nodeIndex].name + " -> " + node.name + ")");
				}
				if (hazard && prior.queue != node.queue)
				{
					bool directWait = false;
					bool stageCovered = false;
					for (const VansSubmitSyncWait& wait : node.waits)
					{
						const auto producer = syncProducers.find(wait.point);
						if (producer != syncProducers.end() && producer->second == prior.nodeIndex)
						{
							directWait = true;
							stageCovered = stageCovered || ((wait.stages & access.stages) != 0);
						}
					}
					if (directWait && !stageCovered)
						return fail("sync wait stage does not cover resource consumer: " + access.name);
				}
				if (access.image && prior.access.image
					&& prior.access.layout != access.layout
					&& prior.queue != node.queue)
				{
					return fail("cross-queue image layout contract mismatch: " + access.name);
				}
			}

			lastAccesses[access.name] = ResourceAccessState{ nodeIndex, node.queue, access };
			auto stateIndex = stateIndices.find(access.name);
			if (stateIndex == stateIndices.end())
			{
				stateIndices.emplace(access.name, m_States.size());
				m_States.emplace_back();
				stateIndex = stateIndices.find(access.name);
			}
			VansTrackedResourceState& state = m_States[stateIndex->second];
			state.name = access.name;
			state.lastStages = access.stages;
			state.lastAccess = access.access;
			state.currentLayout = access.image ? access.layout : VK_IMAGE_LAYOUT_UNDEFINED;
			state.lastAccessWasWrite = access.write;
			state.persistent = state.persistent || access.persistent;
			state.hostReadable = state.hostReadable || access.hostReadable;
			state.lastNode = node.name;
		}
	}

	return true;
}

std::string VansGraphics::VansResourceStateTracker::BuildDebugSummary() const
{
	std::ostringstream stream;
	stream << "ResourceStateTracker resources=" << m_States.size() << '\n';
	for (const VansTrackedResourceState& state : m_States)
	{
		stream << "  " << state.name
			<< " lastNode=" << state.lastNode
			<< " stages=0x" << std::hex << state.lastStages
			<< " access=0x" << state.lastAccess << std::dec
			<< " layout=" << static_cast<int>(state.currentLayout)
			<< " write=" << (state.lastAccessWasWrite ? "true" : "false")
			<< " persistent=" << (state.persistent ? "true" : "false")
			<< " hostReadable=" << (state.hostReadable ? "true" : "false") << '\n';
	}
	return stream.str();
}
