#include "VansEditorShellMenu.h"

#include "imgui.h"

namespace
{
	using namespace VansGraphics;

	void Execute(
		const VansEditorShellMenu::CommandHandler& handler,
		VansEditorShellCommandType type)
	{
		if (handler)
			handler({ type });
	}

	void DrawAssetGroup(
		VansEditorAssetMenuGroup group,
		const VansEditorShellMenu::CommandHandler& handler)
	{
		for (const VansEditorAssetMenuEntry& entry :
			VansEditorShellMenu::AssetCreationEntries())
		{
			if (entry.group != group)
				continue;
			if (ImGui::MenuItem(entry.label.data()) && handler)
			{
				VansEditorShellCommand command;
				command.type = VansEditorShellCommandType::CreateAsset;
				command.assetCreationKind = entry.kind;
				handler(command);
			}
		}
	}

	void DrawWindowGroup(
		VansEditorWindowMenuGroup group,
		VansEditorWindowCatalog& catalog,
		const VansEditorShellMenuState& state)
	{
		for (const VansEditorWindowDescriptor& descriptor : VansEditorWindowCatalog::All())
		{
			if (descriptor.menuGroup != group)
				continue;
			const bool available =
				(descriptor.id != VansEditorWindowId::ReflectionProbe ||
					state.reflectionProbeWindowAvailable) &&
				(descriptor.id != VansEditorWindowId::GI || state.giWindowAvailable);
			if (!available)
				ImGui::BeginDisabled();
			ImGui::MenuItem(descriptor.menuLabel.data(), nullptr,
				catalog.OpenState(descriptor.id));
			if (!available)
				ImGui::EndDisabled();
		}
	}
}

const std::array<VansGraphics::VansEditorAssetMenuEntry, 18>&
VansGraphics::VansEditorShellMenu::AssetCreationEntries()
{
	using Kind = Vans::EditorAPI::ProjectAssetCreationKind;
	static constexpr std::array<VansEditorAssetMenuEntry, 18> entries = {{
		{ "Timeline", VansEditorAssetMenuGroup::Root, Kind::Timeline },
		{ "Action", VansEditorAssetMenuGroup::GameplayAction, Kind::ActionDefinition },
		{ "Action Set", VansEditorAssetMenuGroup::GameplayAction, Kind::ActionSet },
		{ "Effect", VansEditorAssetMenuGroup::GameplayAction, Kind::GameplayEffect },
		{ "Cue", VansEditorAssetMenuGroup::GameplayAction, Kind::GameplayCue },
		{ "Attribute Set", VansEditorAssetMenuGroup::GameplayAction, Kind::AttributeSet },
		{ "Targeting Policy", VansEditorAssetMenuGroup::GameplayAction, Kind::TargetingPolicy },
		{ "Tag Tree", VansEditorAssetMenuGroup::GameplayAction, Kind::GameplayTagTree },
		{ "Payload Schema", VansEditorAssetMenuGroup::GameplayAction, Kind::PayloadSchema },
		{ "Action Graph", VansEditorAssetMenuGroup::GameplayAction, Kind::ActionGraph },
		{ "Camera Rig", VansEditorAssetMenuGroup::GameplayAction, Kind::CameraRigProfile },
		{ "Camera Shake", VansEditorAssetMenuGroup::GameplayAction, Kind::CameraShakeProfile },
		{ "Skin Profile", VansEditorAssetMenuGroup::Rendering, Kind::SkinProfile },
		{ "Animator Controller", VansEditorAssetMenuGroup::Animation, Kind::AnimatorController },
		{ "Bone Mask", VansEditorAssetMenuGroup::Animation, Kind::BoneMask },
		{ "Reverb Preset", VansEditorAssetMenuGroup::Audio, Kind::AudioReverbPreset },
		{ "Bus Snapshot", VansEditorAssetMenuGroup::Audio, Kind::AudioBusSnapshot },
		{ "Ducking Rules", VansEditorAssetMenuGroup::Audio, Kind::AudioDuckingRules }
	}};
	return entries;
}

void VansGraphics::VansEditorShellMenu::Draw(
	const VansEditorShellMenuState& state,
	VansEditorWindowCatalog& windowCatalog,
	const CommandHandler& execute,
	const EmbeddedDraw& drawBuildMenu,
	const EmbeddedDraw& drawPlayToolbar)
{
	if (!ImGui::BeginMenuBar())
		return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, state.canSaveScene))
			Execute(execute, VansEditorShellCommandType::SaveScene);
		if (ImGui::MenuItem("Save Asset", nullptr, false, state.canSaveAsset))
			Execute(execute, VansEditorShellCommandType::SaveAsset);
		if (ImGui::MenuItem("Save Project Documents", nullptr, false,
			state.canSaveProjectDocuments))
		{
			Execute(execute, VansEditorShellCommandType::SaveProjectDocuments);
		}
		if (ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, state.canSaveAll))
			Execute(execute, VansEditorShellCommandType::SaveAll);
		ImGui::Separator();
		if (ImGui::MenuItem("Exit"))
			Execute(execute, VansEditorShellCommandType::Exit);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Asset"))
	{
		const bool canCreateAssets = state.canCreateAssets && state.canCreateAssets();
		if (ImGui::BeginMenu("Create", canCreateAssets))
		{
			DrawAssetGroup(VansEditorAssetMenuGroup::Root, execute);
			if (ImGui::BeginMenu("Gameplay Action"))
			{
				DrawAssetGroup(VansEditorAssetMenuGroup::GameplayAction, execute);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Rendering"))
			{
				DrawAssetGroup(VansEditorAssetMenuGroup::Rendering, execute);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Animation"))
			{
				DrawAssetGroup(VansEditorAssetMenuGroup::Animation, execute);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Audio"))
			{
				DrawAssetGroup(VansEditorAssetMenuGroup::Audio, execute);
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (drawBuildMenu)
		drawBuildMenu();

	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, state.canUndo))
			Execute(execute, VansEditorShellCommandType::Undo);
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, state.canRedo))
			Execute(execute, VansEditorShellCommandType::Redo);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		DrawWindowGroup(VansEditorWindowMenuGroup::General, windowCatalog, state);
		ImGui::Separator();
		if (ImGui::BeginMenu("Animation"))
		{
			bool previewOpen = state.sceneAnimationPreviewOpen;
			if (ImGui::MenuItem("Scene Animation Preview", nullptr, &previewOpen) &&
				state.sceneAnimationPreviewAvailable && execute)
			{
				VansEditorShellCommand command;
				command.type = VansEditorShellCommandType::SetSceneAnimationPreviewOpen;
				command.open = previewOpen;
				execute(command);
			}
			if (ImGui::MenuItem("Animation Graph", nullptr, false,
				state.canOpenSelectedAnimationGraph))
			{
				Execute(execute, VansEditorShellCommandType::OpenSelectedAnimationGraph);
			}
			DrawWindowGroup(VansEditorWindowMenuGroup::Animation, windowCatalog, state);
			if (!state.canOpenSelectedAnimationGraph)
				ImGui::TextDisabled("Select an entity with Animation");
			ImGui::EndMenu();
		}
		ImGui::Separator();
		DrawWindowGroup(VansEditorWindowMenuGroup::Rendering, windowCatalog, state);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		if (state.wireframeMode)
			ImGui::MenuItem("Wireframe", nullptr, state.wireframeMode);
		if (state.vehicleDebugGizmos)
			ImGui::MenuItem("Vehicle Debug Gizmos", nullptr, state.vehicleDebugGizmos);
		ImGui::EndMenu();
	}

	if (drawPlayToolbar)
		drawPlayToolbar();

	ImGui::EndMenuBar();
}
