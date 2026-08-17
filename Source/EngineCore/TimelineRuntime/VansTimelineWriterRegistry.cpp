#include "VansTimelineWriterRegistry.h"

namespace Vans
{
VansTimelineWriterHandle VansTimelineWriterRegistry::Acquire(const VansTimelineWriterDesc& desc)
{
	const Key key{ desc.session, desc.trackIndex, desc.sectionIndex, desc.outputType };
	const auto found = m_ByKey.find(key);
	if (found != m_ByKey.end() && m_Writers.Contains(found->second)) return found->second;
	const VansTimelineWriterHandle handle = m_Writers.Emplace(desc);
	m_ByKey[key] = handle;
	return handle;
}

VansTimelineWriterHandle VansTimelineWriterRegistry::Find(
	VansTimelineSessionHandle session,
	std::uint32_t trackIndex,
	std::uint32_t sectionIndex,
	VansTimelineOutputTypeId outputType) const
{
	const auto found = m_ByKey.find(Key{ session, trackIndex, sectionIndex, outputType });
	return found == m_ByKey.end() || !m_Writers.Contains(found->second)
		? VansTimelineWriterHandle{} : found->second;
}

const VansTimelineWriterDesc* VansTimelineWriterRegistry::Resolve(VansTimelineWriterHandle handle) const
{
	return m_Writers.Resolve(handle);
}

bool VansTimelineWriterRegistry::Release(VansTimelineWriterHandle handle)
{
	const VansTimelineWriterDesc* desc = m_Writers.Resolve(handle);
	if (!desc) return false;
	m_ByKey.erase(Key{ desc->session, desc->trackIndex, desc->sectionIndex, desc->outputType });
	return m_Writers.Release(handle);
}

std::vector<VansTimelineWriterHandle> VansTimelineWriterRegistry::ReleaseSession(
	VansTimelineSessionHandle session)
{
	std::vector<VansTimelineWriterHandle> handles;
	m_Writers.ForEach([&](VansTimelineWriterHandle handle, const VansTimelineWriterDesc& desc)
	{
		if (desc.session == session) handles.push_back(handle);
	});
	for (VansTimelineWriterHandle handle : handles) Release(handle);
	return handles;
}

std::vector<VansTimelineWriterHandle> VansTimelineWriterRegistry::ReleaseRoot(
	VansTimelineSessionHandle root)
{
	std::vector<VansTimelineWriterHandle> handles;
	m_Writers.ForEach([&](VansTimelineWriterHandle handle, const VansTimelineWriterDesc& desc)
	{
		if (desc.root == root) handles.push_back(handle);
	});
	for (VansTimelineWriterHandle handle : handles) Release(handle);
	return handles;
}
}
