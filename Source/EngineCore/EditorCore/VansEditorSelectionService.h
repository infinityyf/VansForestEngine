#pragma once

#include "VansEditorObjectReference.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
	enum class EditorSelectionOperation
	{
		Replace,
		Add,
		Toggle,
		Remove,
		Clear
	};

	struct EditorSelectionSnapshot
	{
		std::vector<EditorObjectHandle> objects;
		EditorObjectHandle active;
		std::uint64_t revision = 0;
		std::string source;
	};

	class VansEditorSelectionService
	{
	public:
		static VansEditorSelectionService& Get();

		const EditorSelectionSnapshot& Snapshot() const { return m_Snapshot; }
		void Apply(
			EditorSelectionOperation operation,
			const std::vector<EditorObjectHandle>& handles,
			const EditorObjectHandle& active,
			const std::string& source);
		void Clear(const std::string& source);
		bool Contains(const EditorObjectHandle& handle) const;

		void SelectEntity(std::string entityGuid, const std::string& source);
		void SelectScene(const std::string& source);
		void SelectAsset(std::filesystem::path assetPath, const std::string& source);

		const std::string& EntityGuid() const { return m_ActiveEntityGuid; }
		const std::filesystem::path& AssetPath() const { return m_ActiveAssetPath; }
		bool IsSceneSelected() const { return m_SceneSelected; }

	private:
		void ReplaceFacadeStateFromActive(const EditorObjectHandle& active);
		static bool SameObject(const EditorObjectHandle& left, const EditorObjectHandle& right);

		EditorSelectionSnapshot m_Snapshot;
		std::string m_ActiveEntityGuid;
		std::filesystem::path m_ActiveAssetPath;
		bool m_SceneSelected = false;
	};
}

