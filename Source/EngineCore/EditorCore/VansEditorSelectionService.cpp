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
			left.subObjectKind == right.subObjectKind &&
			left.subObjectGuid == right.subObjectGuid &&
			left.subObjectName == right.subObjectName;
	}

	void VansEditorSelectionService::SelectSceneSubObject(
		EditorObjectHandle handle,
		const std::string& source)
	{
		if (handle.domain != EditorObjectDomain::SceneSubObject
			|| handle.entityGuid.empty() || handle.componentGuid.empty()
			|| handle.subObjectKind == SceneSubObjectKind::None
			|| handle.subObjectGuid.empty())
			return;
		Apply(EditorSelectionOperation::Replace, { handle }, handle, source);
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
		else if (SameObject(active, {}) || !Contains(active))
			m_Snapshot.active = m_Snapshot.objects.back();

		m_Snapshot.source = source;
		++m_Snapshot.revision;
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
	}

	void VansEditorSelectionService::SelectAsset(std::filesystem::path assetPath, const std::string& source)
	{
		EditorObjectHandle handle;
		handle.domain = EditorObjectDomain::ProjectAsset;
		handle.path = assetPath.string();
		handle.displayName = assetPath.filename().string();
		Apply(EditorSelectionOperation::Replace, { handle }, handle, source);
	}

	const std::string& VansEditorSelectionService::EntityGuid() const
	{
		static const std::string empty;
		switch (m_Snapshot.active.domain)
		{
		case EditorObjectDomain::SceneEntity:
			return m_Snapshot.active.entityGuid.empty()
				? m_Snapshot.active.guid
				: m_Snapshot.active.entityGuid;
		case EditorObjectDomain::SceneSubObject:
			return m_Snapshot.active.entityGuid;
		default:
			return empty;
		}
	}

	std::filesystem::path VansEditorSelectionService::AssetPath() const
	{
		return m_Snapshot.active.domain == EditorObjectDomain::ProjectAsset
			? std::filesystem::path(m_Snapshot.active.path)
			: std::filesystem::path{};
	}

	bool VansEditorSelectionService::IsSceneSelected() const
	{
		return m_Snapshot.active.domain == EditorObjectDomain::Unknown
			&& m_Snapshot.active.displayName == "Scene Settings";
	}
}
