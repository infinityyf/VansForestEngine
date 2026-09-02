#include "VansTransformGraph.h"

#include "../../Util/VansLog.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace Vans
{
namespace
{
constexpr float kTransformEpsilon = 1.0e-6f;

bool Finite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool Finite(const glm::quat& value)
{
	return std::isfinite(value.w) && std::isfinite(value.x)
		&& std::isfinite(value.y) && std::isfinite(value.z);
}

bool ValidLocalTransform(const VansLocalTransform& value)
{
	return Finite(value.position) && Finite(value.rotation) && Finite(value.scale)
		&& glm::length(value.rotation) > kTransformEpsilon
		&& std::abs(value.scale.x) > kTransformEpsilon
		&& std::abs(value.scale.y) > kTransformEpsilon
		&& std::abs(value.scale.z) > kTransformEpsilon;
}
}

glm::mat4 VansLocalTransform::ToMatrix() const
{
	return glm::translate(glm::mat4(1.0f), position)
		* glm::mat4_cast(glm::normalize(rotation))
		* glm::scale(glm::mat4(1.0f), scale);
}

bool VansLocalTransform::TryFromMatrix(
	const glm::mat4& matrix,
	VansLocalTransform& outTransform)
{
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::vec3 position;
	glm::quat rotation;
	glm::vec3 scale;
	if (!glm::decompose(matrix, scale, rotation, position, skew, perspective)
		|| !Finite(position) || !Finite(rotation) || !Finite(scale)
		|| glm::length(rotation) <= kTransformEpsilon
		|| std::abs(scale.x) <= kTransformEpsilon
		|| std::abs(scale.y) <= kTransformEpsilon
		|| std::abs(scale.z) <= kTransformEpsilon
		|| glm::length(skew) > 1.0e-4f)
		return false;
	outTransform.position = position;
	outTransform.rotation = glm::normalize(rotation);
	outTransform.scale = scale;
	return true;
}

bool VansTransformGraph::ValidateTransformId(std::uint32_t transformId) const
{
	return transformId < VansGraphics::VansTransformStore::GlobalTransforms.size()
		&& VansGraphics::VansTransformStore::IsAllocated(transformId);
}

bool VansTransformGraph::WouldCreateCycle(
	std::uint32_t childTransformId,
	std::uint32_t parentTransformId) const
{
	std::uint32_t current = parentTransformId;
	while (current != UINT32_MAX)
	{
		if (current == childTransformId)
			return true;
		const auto found = m_Nodes.find(current);
		if (found == m_Nodes.end())
			return false;
		current = found->second.link.parentTransformId;
	}
	return false;
}

bool VansTransformGraph::SetParent(
	std::uint32_t childTransformId,
	std::uint32_t parentTransformId,
	VansTransformReparentMode mode)
{
	m_LastError.clear();
	if (!ValidateTransformId(childTransformId) || !ValidateTransformId(parentTransformId))
	{
		m_LastError = "Transform parent references an invalid transform";
		return false;
	}
	if (childTransformId == parentTransformId
		|| WouldCreateCycle(childTransformId, parentTransformId))
	{
		m_LastError = "Transform parent would create a cycle";
		return false;
	}

	Node& node = m_Nodes[childTransformId];
	const bool hadNode = node.link.childTransformId != UINT32_MAX;
	if (!hadNode)
	{
		node.link.childTransformId = childTransformId;
		if (!VansLocalTransform::TryFromMatrix(
			VansGraphics::VansTransformStore::GetTransform(childTransformId).GetModelMatrix(),
			node.local))
		{
			m_Nodes.erase(childTransformId);
			m_LastError = "Child world transform cannot be decomposed";
			return false;
		}
	}
	node.link.parentTransformId = parentTransformId;
	node.link.usesAnchor = false;
	node.link.anchor = {};
	if (mode == VansTransformReparentMode::Snap)
		node.local = {};
	node.importWorldBeforeResolve = mode == VansTransformReparentMode::KeepWorld;
	RebuildTopologicalOrder();
	return true;
}

bool VansTransformGraph::SetAnchor(
	std::uint32_t childTransformId,
	std::uint32_t ownerTransformId,
	VansTransformAnchorHandle anchor,
	VansTransformReparentMode mode)
{
	m_LastError.clear();
	if (!ValidateTransformId(childTransformId) || !ValidateTransformId(ownerTransformId)
		|| !anchor.IsValid())
	{
		m_LastError = "Transform anchor references an invalid transform or anchor";
		return false;
	}
	if (childTransformId == ownerTransformId || WouldCreateCycle(childTransformId, ownerTransformId))
	{
		m_LastError = "Transform anchor would create a cycle";
		return false;
	}

	Node& node = m_Nodes[childTransformId];
	if (node.link.childTransformId == UINT32_MAX)
	{
		node.link.childTransformId = childTransformId;
		if (!VansLocalTransform::TryFromMatrix(
			VansGraphics::VansTransformStore::GetTransform(childTransformId).GetModelMatrix(),
			node.local))
		{
			m_Nodes.erase(childTransformId);
			m_LastError = "Anchored child world transform cannot be decomposed";
			return false;
		}
	}
	node.link.parentTransformId = ownerTransformId;
	node.link.usesAnchor = true;
	node.link.anchor = std::move(anchor);
	if (mode == VansTransformReparentMode::Snap)
		node.local = {};
	node.importWorldBeforeResolve = mode == VansTransformReparentMode::KeepWorld;
	RebuildTopologicalOrder();
	return true;
}

bool VansTransformGraph::SetAnchorWithLocalTransform(
	std::uint32_t childTransformId,
	std::uint32_t ownerTransformId,
	VansTransformAnchorHandle anchor,
	const VansLocalTransform& localTransform)
{
	m_LastError.clear();
	if (!ValidateTransformId(childTransformId) || !ValidateTransformId(ownerTransformId)
		|| !anchor.IsValid() || !ValidLocalTransform(localTransform))
	{
		m_LastError = "Transform attachment profile references an invalid transform, anchor, or local TRS";
		return false;
	}
	if (childTransformId == ownerTransformId || WouldCreateCycle(childTransformId, ownerTransformId))
	{
		m_LastError = "Transform attachment profile would create a cycle";
		return false;
	}

	Node& node = m_Nodes[childTransformId];
	node.link.childTransformId = childTransformId;
	node.link.parentTransformId = ownerTransformId;
	node.link.usesAnchor = true;
	node.link.anchor = std::move(anchor);
	node.local = localTransform;
	node.local.rotation = glm::normalize(node.local.rotation);
	node.importWorldBeforeResolve = false;
	node.anchorWasUnresolved = false;
	RebuildTopologicalOrder();
	return true;
}

bool VansTransformGraph::ClearParent(
	std::uint32_t childTransformId,
	VansTransformReparentMode mode)
{
	const auto found = m_Nodes.find(childTransformId);
	if (found == m_Nodes.end())
		return false;
	if (mode == VansTransformReparentMode::KeepLocal
		|| mode == VansTransformReparentMode::Snap)
	{
		const glm::mat4 promotedWorld = mode == VansTransformReparentMode::Snap
			? glm::mat4(1.0f) : found->second.local.ToMatrix();
		if (!WriteResolvedWorld(childTransformId, promotedWorld))
		{
			m_LastError = mode == VansTransformReparentMode::Snap
				? "Identity transform cannot be written while clearing the parent"
				: "Local transform cannot be promoted to world TRS";
			return false;
		}
	}
	m_Nodes.erase(found);
	RebuildTopologicalOrder();
	return true;
}

bool VansTransformGraph::HasParent(std::uint32_t childTransformId) const
{
	return m_Nodes.find(childTransformId) != m_Nodes.end();
}

std::uint32_t VansTransformGraph::GetParent(std::uint32_t childTransformId) const
{
	const auto found = m_Nodes.find(childTransformId);
	return found == m_Nodes.end() ? UINT32_MAX : found->second.link.parentTransformId;
}

const VansTransformGraphLink* VansTransformGraph::GetLink(std::uint32_t childTransformId) const
{
	const auto found = m_Nodes.find(childTransformId);
	return found == m_Nodes.end() ? nullptr : &found->second.link;
}

std::vector<VansTransformGraphLink> VansTransformGraph::GetAllLinks() const
{
	std::vector<VansTransformGraphLink> links;
	links.reserve(m_TopologicalOrder.size());
	for (const std::uint32_t id : m_TopologicalOrder)
		links.push_back(m_Nodes.at(id).link);
	return links;
}

bool VansTransformGraph::SetLocalTransform(
	std::uint32_t transformId,
	const VansLocalTransform& localTransform)
{
	const auto found = m_Nodes.find(transformId);
	if (found == m_Nodes.end() || !ValidLocalTransform(localTransform))
		return false;
	found->second.local = localTransform;
	found->second.local.rotation = glm::normalize(found->second.local.rotation);
	found->second.importWorldBeforeResolve = false;
	return true;
}

bool VansTransformGraph::SetWorldTransform(
	std::uint32_t transformId,
	const glm::mat4& worldTransform)
{
	if (!ValidateTransformId(transformId))
		return false;
	if (!WriteResolvedWorld(transformId, worldTransform))
		return false;
	const auto found = m_Nodes.find(transformId);
	if (found != m_Nodes.end())
		found->second.importWorldBeforeResolve = true;
	return true;
}

bool VansTransformGraph::TryGetLocalTransform(
	std::uint32_t transformId,
	VansLocalTransform& outTransform) const
{
	const auto found = m_Nodes.find(transformId);
	if (found == m_Nodes.end())
	{
		if (!ValidateTransformId(transformId))
			return false;
		return VansLocalTransform::TryFromMatrix(
			VansGraphics::VansTransformStore::GetTransform(transformId).GetModelMatrix(), outTransform);
	}
	outTransform = found->second.local;
	return true;
}

void VansTransformGraph::MarkWorldDirty(std::uint32_t transformId)
{
	const auto found = m_Nodes.find(transformId);
	if (found != m_Nodes.end())
		found->second.importWorldBeforeResolve = true;
}

bool VansTransformGraph::ResolveParentWorld(const Node& node, glm::mat4& outParentWorld) const
{
	outParentWorld = VansGraphics::VansTransformStore::GetTransform(
		node.link.parentTransformId).GetModelMatrix();
	if (!node.link.usesAnchor)
		return true;
	if (!m_AnchorProvider)
		return false;
	glm::mat4 anchorModel(1.0f);
	std::uint64_t poseRevision = 0;
	if (!m_AnchorProvider->ResolveModelSpaceTransform(
		node.link.anchor, anchorModel, poseRevision))
		return false;
	outParentWorld *= anchorModel;
	return true;
}

bool VansTransformGraph::ImportLocalFromCurrentWorld(Node& node)
{
	glm::mat4 parentWorld(1.0f);
	if (!ResolveParentWorld(node, parentWorld))
		return false;
	const glm::mat4 childWorld = VansGraphics::VansTransformStore::GetTransform(
		node.link.childTransformId).GetModelMatrix();
	return VansLocalTransform::TryFromMatrix(glm::inverse(parentWorld) * childWorld, node.local);
}

bool VansTransformGraph::Resolve()
{
	m_LastError.clear();
	bool allResolved = true;
	for (const std::uint32_t transformId : m_TopologicalOrder)
	{
		Node& node = m_Nodes.at(transformId);
		glm::mat4 parentWorld(1.0f);
		if (!ResolveParentWorld(node, parentWorld))
		{
			allResolved = false;
			if (!node.anchorWasUnresolved)
			{
				VANS_LOG_WARN("[TransformGraph] Anchor unresolved for transform " << transformId
					<< " (anchorGuid=" << node.link.anchor.anchorGuid << ")");
				node.anchorWasUnresolved = true;
			}
			continue;
		}
		node.anchorWasUnresolved = false;
		if (node.importWorldBeforeResolve)
		{
			if (!ImportLocalFromCurrentWorld(node))
			{
				allResolved = false;
				m_LastError = "A world transform could not be converted to local TRS";
				continue;
			}
			node.importWorldBeforeResolve = false;
		}
		if (!WriteResolvedWorld(transformId, parentWorld * node.local.ToMatrix()))
		{
			allResolved = false;
			m_LastError = "A resolved world transform cannot be represented as TRS";
		}
	}
	return allResolved;
}

void VansTransformGraph::Clear()
{
	m_Nodes.clear();
	m_TopologicalOrder.clear();
	m_LastError.clear();
}

void VansTransformGraph::RebuildTopologicalOrder()
{
	m_TopologicalOrder.clear();
	m_TopologicalOrder.reserve(m_Nodes.size());
	std::unordered_map<std::uint32_t, std::uint32_t> indegree;
	std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> children;
	for (const auto& [id, node] : m_Nodes)
	{
		indegree.emplace(id, 0u);
		if (m_Nodes.find(node.link.parentTransformId) != m_Nodes.end())
		{
			++indegree[id];
			children[node.link.parentTransformId].push_back(id);
		}
	}
	std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready;
	for (const auto& [id, degree] : indegree)
		if (degree == 0)
			ready.push(id);
	while (!ready.empty())
	{
		const std::uint32_t id = ready.top();
		ready.pop();
		m_TopologicalOrder.push_back(id);
		for (const std::uint32_t child : children[id])
			if (--indegree[child] == 0)
				ready.push(child);
	}
}

bool VansTransformGraph::WriteResolvedWorld(
	std::uint32_t transformId,
	const glm::mat4& worldTransform)
{
	VansLocalTransform decomposed;
	if (!VansLocalTransform::TryFromMatrix(worldTransform, decomposed))
		return false;
	VansGraphics::VansTransform& transform =
		VansGraphics::VansTransformStore::GetTransform(transformId);
	transform.m_Position = decomposed.position;
	transform.m_Rotation = glm::degrees(glm::eulerAngles(decomposed.rotation));
	transform.m_Scale = decomposed.scale;
	VansGraphics::VansTransformStore::TransformIDToTransformDirty[transformId] = true;
	return true;
}
}
