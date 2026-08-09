#pragma once

#include "VansBaseWindowComponent.h"
#include "../../EngineAPILayer/Public/EngineDTOs.h"

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace Vans { struct VansOpenAssetDocument; }

namespace VansGraphics
{
	class VansBoneMaskEditorWindow final : public VansBaseWindowComponent
	{
	public:
		void Open(const std::string& boneMaskPath);
		void Close();
		bool IsOpen() const { return m_IsOpen; }
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	private:
		bool DecodeDocument();
		bool CommitDocument();
		bool Save();
		bool Undo();
		bool Redo();
		void RefreshSkeleton();
		void Recompile();
		void DrawToolbar();
		void DrawSkeletonTree();
		void DrawSkeletonPreview();
		void DrawRulesPanel();
		void DrawDiagnostics();
		void DrawBoneTreeNode(int boneIndex);
		void SelectBone(int boneIndex);
		void AddRule(Vans::EditorAPI::BoneMaskRuleMode mode);
		void ApplyTemplate(const char* templateName);
		void MarkEdited();

		bool m_IsOpen = false;
		bool m_CloseRequested = false;
		bool m_NeedsDecode = false;
		bool m_WorkingDirty = false;
		std::string m_Path;
		std::string m_LastError;
		std::shared_ptr<Vans::VansOpenAssetDocument> m_Document;
		Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
		std::uint64_t m_DocumentStateId = 0;
		Vans::EditorAPI::BoneMaskDocumentDTO m_Asset;
		Vans::EditorAPI::BoneMaskCompileResult m_Compiled;
		Vans::EditorAPI::AssetSkeletonSnapshot m_Skeleton;
		std::unordered_set<int> m_SelectedBones;
		int m_SelectedRule = -1;
		char m_Search[128]{};
		bool m_LockSelection = false;
		bool m_ShowOnlyWeighted = false;
		float m_PreviewYaw = 0.35f;
		float m_PreviewPitch = -0.15f;
		float m_PreviewZoom = 0.9f;
	};
}
