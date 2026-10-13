#include "VansRenderBoundsIndex.h"

#include <algorithm>
#include <limits>
#include <utility>

using namespace VansGraphics;

void VansRenderBoundsIndex::Build(std::vector<Entry> entries)
{
	m_Entries = std::move(entries);
	m_Bounded.clear();
	m_Unbounded.clear();
	m_Nodes.clear();
	m_Bounded.reserve(m_Entries.size());
	m_Unbounded.reserve(m_Entries.size());
	for (std::uint32_t i = 0; i < m_Entries.size(); ++i)
	{
		const Entry& entry = m_Entries[i];
		if (entry.hasBounds && entry.bounds.IsValid())
			m_Bounded.push_back(i);
		else
			m_Unbounded.push_back(i);
	}
	m_Nodes.reserve(m_Bounded.size() * 2);
	if (!m_Bounded.empty())
		BuildNode(0, static_cast<std::uint32_t>(m_Bounded.size()));
}

std::uint32_t VansRenderBoundsIndex::BuildNode(std::uint32_t begin, std::uint32_t end)
{
	Node node;
	node.begin = begin;
	node.end = end;
	glm::vec3 centroidMin((std::numeric_limits<float>::max)());
	glm::vec3 centroidMax(-(std::numeric_limits<float>::max)());
	for (std::uint32_t i = begin; i < end; ++i)
	{
		const VansRenderAABB& bounds = m_Entries[m_Bounded[i]].bounds.aabb;
		node.categoryMask |= m_Entries[m_Bounded[i]].categoryMask;
		node.bounds.min = glm::min(node.bounds.min, bounds.min);
		node.bounds.max = glm::max(node.bounds.max, bounds.max);
		const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
		centroidMin = glm::min(centroidMin, center);
		centroidMax = glm::max(centroidMax, center);
	}
	const std::uint32_t index = static_cast<std::uint32_t>(m_Nodes.size());
	m_Nodes.push_back(node);
	if (end - begin <= 8)
	{
		m_Nodes[index].leaf = true;
		return index;
	}
	const glm::vec3 extent = centroidMax - centroidMin;
	int axis = 0;
	if (extent.y > extent.x) axis = 1;
	if (extent.z > extent[axis]) axis = 2;
	const std::uint32_t middle = begin + (end - begin) / 2;
	std::nth_element(m_Bounded.begin() + begin, m_Bounded.begin() + middle,
		m_Bounded.begin() + end, [&](std::uint32_t a, std::uint32_t b)
		{
			const VansRenderAABB& left = m_Entries[a].bounds.aabb;
			const VansRenderAABB& right = m_Entries[b].bounds.aabb;
			return left.min[axis] + left.max[axis] < right.min[axis] + right.max[axis];
		});
	const std::uint32_t left = BuildNode(begin, middle);
	const std::uint32_t right = BuildNode(middle, end);
	m_Nodes[index].left = left;
	m_Nodes[index].right = right;
	return index;
}

void VansRenderBoundsIndex::QueryClipFrustum(const glm::mat4& worldToClip,
	std::uint32_t categoryMask,
	std::vector<std::uint32_t>& sourceIndices) const
{
	sourceIndices.clear();
	for (std::uint32_t entryIndex : m_Unbounded)
	{
		const Entry& entry = m_Entries[entryIndex];
		if ((entry.categoryMask & categoryMask) != 0u)
			sourceIndices.push_back(entry.sourceIndex);
	}
	if (!m_Nodes.empty())
		QueryNode(0, worldToClip, categoryMask, sourceIndices);
	std::sort(sourceIndices.begin(), sourceIndices.end());
}

void VansRenderBoundsIndex::QueryNode(std::uint32_t nodeIndex,
	const glm::mat4& worldToClip, std::uint32_t categoryMask,
	std::vector<std::uint32_t>& sourceIndices) const
{
	const Node& node = m_Nodes[nodeIndex];
	if ((node.categoryMask & categoryMask) == 0u)
		return;
	if (!RenderAABBIntersectsClipFrustum(node.bounds.min, node.bounds.max, worldToClip))
		return;
	if (!node.leaf)
	{
		QueryNode(node.left, worldToClip, categoryMask, sourceIndices);
		QueryNode(node.right, worldToClip, categoryMask, sourceIndices);
		return;
	}
	for (std::uint32_t i = node.begin; i < node.end; ++i)
	{
		const Entry& entry = m_Entries[m_Bounded[i]];
		if ((entry.categoryMask & categoryMask) != 0u &&
			RenderBoundsIntersectsClipFrustum(entry.bounds, worldToClip))
			sourceIndices.push_back(entry.sourceIndex);
	}
}
