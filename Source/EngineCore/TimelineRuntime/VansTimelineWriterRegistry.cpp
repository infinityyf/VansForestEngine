#include "VansTimelineWriterRegistry.h"

namespace Vans
{
VansTimelineWriterHandle VansTimelineWriterRegistry::Acquire(const VansTimelineWriterIdentity& identity)
{
	const Key key{ identity.session, identity.trackIndex, identity.sectionIndex, identity.outputType };
	const auto found = m_ByKey.find(key);
	if (found != m_ByKey.end() && m_Writers.Contains(found->second)) return found->second;
	const VansTimelineWriterHandle handle = m_Writers.Emplace(identity);
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

const VansTimelineWriterIdentity* VansTimelineWriterRegistry::Resolve(VansTimelineWriterHandle handle) const
{
	return m_Writers.Resolve(handle);
}

bool VansTimelineWriterRegistry::Release(VansTimelineWriterHandle handle)
{
	const VansTimelineWriterIdentity* identity = m_Writers.Resolve(handle);
	if (!identity) return false;
	m_ByKey.erase(Key{ identity->session, identity->trackIndex, identity->sectionIndex, identity->outputType });
	return m_Writers.Release(handle);
}

std::vector<VansTimelineWriterHandle> VansTimelineWriterRegistry::ReleaseSession(
	VansTimelineSessionHandle session)
{
	std::vector<VansTimelineWriterHandle> handles;
	m_Writers.ForEach([&](VansTimelineWriterHandle handle, const VansTimelineWriterIdentity& identity)
	{
		if (identity.session == session) handles.push_back(handle);
	});
	for (VansTimelineWriterHandle handle : handles) Release(handle);
	return handles;
}

}
