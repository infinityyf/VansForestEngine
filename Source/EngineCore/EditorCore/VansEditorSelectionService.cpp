#include "VansEditorSelectionService.h"

#include <algorithm>
#include <utility>

namespace Vans
{
	VansEditorSelectionService& VansEditorSelectionService::Get()
	{
		static VansEditorSelectionService service;
		return service;
	}

	bool VansEditorSelectionService::SameObject(
		const EditorObjectHandle& left,
		const EditorObjectHandle& right)
	{
		return left.domain == right.domain &&
			left.guid == right.guid &&
			left.path == right.path &&
			left.entityGuid == right.entityGuid &&
			left.componentGuid == right.componentGuid &&
			left.subObjectName == right.subObjectName;
	}

	bool VansEditorSelectionService::Contains(const EditorObjectHandle& handle) const
	{
		return std::any_of(
			m_Snapshot.objects.begin(),
			m_Snapshot.objects.end(),
			[&handle](const EditorObjectHandle& selected)
			{
				return SameObject(selected, handle);
			});
	}

	void VansEditorSelectionService::Apply(
		EditorSelectionOperation operation,
		const std::vector<EditorObjectHandle>& handles,
		const EditorObjectHandle& active,
		const std::string& source)
	{
		switch (operation)
		{
		case EditorSelectionOperation::Replace:
			m_Snapshot.objects = handles;
			break;
		case EditorSelectionOperation::Add:
			for (const EditorObjectHandle& handle : handles)
			{
				if (!Contains(handle))
					m_Snapshot.objects.push_back(handle);
			}
			break;
		case EditorSelectionOperation::Toggle:
			for (const EditorObjectHandle& handle : handles)
			{
				auto found = std::find_if(
					m_Snapshot.objects.begin(),
					m_Snapshot.objects.end(),
					[&handle](const EditorObjectHandle& selected)
					{
						return SameObject(selected, handle);
					});
				if (found == m_Snapshot.objects.end())
					m_Snapshot.objects.push_back(handle);
				else
					m_Snapshot.objects.erase(found);
			}
			break;
		case EditorSelectionOperation::Remove:
			for (const EditorObjectHandle& handle : handles)
			{
				m_Snapshot.objects.erase(
					std::remove_if(
						m_Snapshot.objects.begin(),
						m_Snapshot.objects.end(),
						[&handle](const EditorObjectHandle& selected)
						{
							return SameObject(selected, handle);
						}),
					m_Snapshot.objects.end());
			}
			break;
		case EditorSelectionOperation::Clear:
			m_Snapshot.objects.clear();
			break;
		}

		m_Snapshot.active = active;
		if (m_Snapshot.objects.empty())
			m_Snapshot.active = {};
		else if (!SameObject(active, {}) && !Contains(active))
			m_Snapshot.objects.push_back(active);
		else if (SameObject(active, {}))
			m_Snapshot.active = m_Snapshot.objects.back();

		m_Snapshot.source = source;
		++m_Snapshot.revision;
		ReplaceFacadeStateFromActive(m_Snapshot.active);
	}

	void VansEditorSelectionService::Clear(const std::string& source)
	{
		Apply(EditorSelectionOperation::Clear, {}, {}, source);
	}

	void VansEditorSelectionService::SelectEntity(std::string entityGuid, const std::string& source)
	{
		EditorObjectHandle handle;
		handle.domain = EditorObjectDomain::SceneEntity;
		handle.guid = entityGuid;
		handle.entityGuid = std::move(entityGuid);
		Apply(EditorSelectionOperation::Replace, { handle }, handle, source);
	}

	void VansEditorSelectionService::SelectScene(const std::string& source)
	{
		EditorObjectHandle handle;
		handle.domain = EditorObjectDomain::Unknown;
		handle.displayName = "Scene Settings";
		Apply(EditorSelectionOperation::Replace, { handle }, handle, source);
		m_SceneSelected = true;
	}

	void VansEditorSelectionService::SelectAsset(std::filesystem::path assetPath, const std::string& source)
	{
		EditorObjectHandle handle;
		handle.domain = EditorObjectDomain::ProjectAsset;
		handle.path = assetPath.string();
		handle.displayName = assetPath.filename().string();
		Apply(EditorSelectionOperation::Replace, { handle }, handle, source);
	}

	void VansEditorSelectionService::ReplaceFacadeStateFromActive(const EditorObjectHandle& active)
	{
		m_ActiveEntityGuid.clear();
		m_ActiveAssetPath.clear();
		m_SceneSelected = false;

		switch (active.domain)
		{
		case EditorObjectDomain::SceneEntity:
			m_ActiveEntityGuid = active.entityGuid.empty() ? active.guid : active.entityGuid;
			break;
		case EditorObjectDomain::ProjectAsset:
			m_ActiveAssetPath = active.path;
			break;
		default:
			break;
		}
	}
}

