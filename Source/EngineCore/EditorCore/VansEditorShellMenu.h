#pragma once

#include "VansEditorWindowCatalog.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <array>
#include <functional>
#include <string_view>

namespace VansGraphics
{
	enum class VansEditorAssetMenuGroup
	{
		Root,
		GameplayAction,
		Rendering,
		Animation,
		Audio
	};

	struct VansEditorAssetMenuEntry
	{
		std::string_view label;
		VansEditorAssetMenuGroup group = VansEditorAssetMenuGroup::Root;
		Vans::EditorAPI::ProjectAssetCreationKind kind =
			Vans::EditorAPI::ProjectAssetCreationKind::Timeline;
	};

	enum class VansEditorShellCommandType
	{
		SaveScene,
		SaveAsset,
		SaveProjectDocuments,
		SaveAll,
		Exit,
		CreateAsset,
		Undo,
		Redo,
		SetSceneAnimationPreviewOpen,
		OpenSelectedAnimationGraph
	};

	struct VansEditorShellCommand
	{
		VansEditorShellCommandType type = VansEditorShellCommandType::SaveScene;
		Vans::EditorAPI::ProjectAssetCreationKind assetCreationKind =
			Vans::EditorAPI::ProjectAssetCreationKind::Timeline;
		bool open = false;
	};

	struct VansEditorShellMenuState
	{
		bool canSaveScene = false;
		bool canSaveAsset = false;
		bool canSaveProjectDocuments = false;
		bool canSaveAll = false;
		std::function<bool()> canCreateAssets;
		bool canUndo = false;
		bool canRedo = false;
		bool canOpenSelectedAnimationGraph = false;
		bool sceneAnimationPreviewAvailable = false;
		bool sceneAnimationPreviewOpen = false;
		bool reflectionProbeWindowAvailable = false;
		bool giWindowAvailable = false;
		bool* wireframeMode = nullptr;
		bool* vehicleDebugGizmos = nullptr;
	};

	class VansEditorShellMenu final
	{
	public:
		using CommandHandler = std::function<void(const VansEditorShellCommand&)>;
		using EmbeddedDraw = std::function<void()>;

		static const std::array<VansEditorAssetMenuEntry, 18>& AssetCreationEntries();

		static void Draw(
			const VansEditorShellMenuState& state,
			VansEditorWindowCatalog& windowCatalog,
			const CommandHandler& execute,
			const EmbeddedDraw& drawBuildMenu,
			const EmbeddedDraw& drawPlayToolbar);
	};
}
