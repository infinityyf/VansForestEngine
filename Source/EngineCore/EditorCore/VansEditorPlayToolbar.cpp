#include "VansEditorPlayToolbar.h"

#include "VansEditorConfiguration.h"
#include "VansEditorTheme.h"
#include "imgui.h"

VansGraphics::VansEditorPlayToolbarState
VansGraphics::VansEditorPlayToolbar::Resolve(
	Vans::EditorAPI::EnginePlayState playState,
	bool sceneReady)
{
	VansEditorPlayToolbarState state;
	state.playEnabled = sceneReady &&
		playState == Vans::EditorAPI::EnginePlayState::Edit;
	state.showResume = playState == Vans::EditorAPI::EnginePlayState::Pause;
	state.pauseResumeEnabled = sceneReady &&
		(playState == Vans::EditorAPI::EnginePlayState::Play || state.showResume);
	state.stopEnabled = sceneReady &&
		playState != Vans::EditorAPI::EnginePlayState::Edit;
	state.pauseResumeCommand = state.showResume
		? VansEditorPlayCommand::Resume
		: VansEditorPlayCommand::Pause;
	return state;
}

void VansGraphics::VansEditorPlayToolbar::Draw(
	const VansEditorPlayToolbarState& state,
	const VansEditorToolbarConfiguration& configuration,
	const CommandHandler& execute)
{
	const float buttonWidth = configuration.buttonWidth;
	const float buttonHeight = configuration.buttonHeight;
	const float buttonSpacing = configuration.buttonSpacing;
	const float totalWidth = buttonWidth * 3.0f + buttonSpacing * 2.0f;

	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(buttonSpacing, 0.0f));

	if (!state.playEnabled) ImGui::BeginDisabled();
	VansEditorTheme::PushToolbarActionColors(VansEditorToolbarAction::Play);
	if (ImGui::Button(u8"\u25b6 Play", ImVec2(buttonWidth, buttonHeight)) && execute)
		execute(VansEditorPlayCommand::Play);
	VansEditorTheme::PopToolbarActionColors();
	if (!state.playEnabled) ImGui::EndDisabled();

	ImGui::SameLine();
	if (!state.pauseResumeEnabled) ImGui::BeginDisabled();
	VansEditorTheme::PushToolbarActionColors(VansEditorToolbarAction::Pause);
	const char* pauseLabel = state.showResume ? u8"\u25b6 Resume" : u8"\u23f8 Pause";
	if (ImGui::Button(pauseLabel, ImVec2(buttonWidth, buttonHeight)) && execute)
		execute(state.pauseResumeCommand);
	VansEditorTheme::PopToolbarActionColors();
	if (!state.pauseResumeEnabled) ImGui::EndDisabled();

	ImGui::SameLine();
	if (!state.stopEnabled) ImGui::BeginDisabled();
	VansEditorTheme::PushToolbarActionColors(VansEditorToolbarAction::Stop);
	if (ImGui::Button(u8"\u23f9 Stop", ImVec2(buttonWidth, buttonHeight)) && execute)
		execute(VansEditorPlayCommand::Stop);
	VansEditorTheme::PopToolbarActionColors();
	if (!state.stopEnabled) ImGui::EndDisabled();

	ImGui::PopStyleVar();
}
