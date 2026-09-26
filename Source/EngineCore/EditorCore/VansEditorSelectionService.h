#pragma once

#include "../AuthoringCore/VansEditorObjectReference.h"

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
		void SelectSceneSubObject(EditorObjectHandle handle, const std::string& source);
		void SelectScene(const std::string& source);
		void SelectAsset(std::filesystem::path assetPath, const std::string& source);

		const std::string& EntityGuid() const;
		std::filesystem::path AssetPath() const;
		bool IsSceneSelected() const;

	private:
		static bool SameObject(const EditorObjectHandle& left, const EditorObjectHandle& right);

		EditorSelectionSnapshot m_Snapshot;
	};
}
