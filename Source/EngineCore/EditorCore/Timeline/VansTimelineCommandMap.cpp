#include "VansTimelineCommandMap.h"

namespace Vans
{
namespace
{
constexpr std::size_t Index(VansTimelineCommand command)
{
	return static_cast<std::size_t>(command);
}
}

VansTimelineCommandMap::VansTimelineCommandMap()
{
	m_Bindings[Index(VansTimelineCommand::Save)] = { "Save", { ImGuiKey_S, true } };
	m_Bindings[Index(VansTimelineCommand::Undo)] = { "Undo", { ImGuiKey_Z, true } };
	m_Bindings[Index(VansTimelineCommand::Redo)] = { "Redo", { ImGuiKey_Y, true } };
	m_Bindings[Index(VansTimelineCommand::Copy)] = { "Copy", { ImGuiKey_C, true } };
	m_Bindings[Index(VansTimelineCommand::Paste)] = { "Paste", { ImGuiKey_V, true } };
	m_Bindings[Index(VansTimelineCommand::Duplicate)] = { "Duplicate", { ImGuiKey_D, true } };
	m_Bindings[Index(VansTimelineCommand::SplitSection)] = { "Split Section", { ImGuiKey_Slash, true } };
	m_Bindings[Index(VansTimelineCommand::DeleteSelection)] = { "Delete", { ImGuiKey_Delete } };
	m_Bindings[Index(VansTimelineCommand::PlayPause)] = { "Play/Pause", { ImGuiKey_Space } };
	m_Bindings[Index(VansTimelineCommand::Stop)] = { "Stop", { ImGuiKey_Space, false, true } };
	m_Bindings[Index(VansTimelineCommand::PreviousKey)] = { "Previous Key", { ImGuiKey_Comma } };
	m_Bindings[Index(VansTimelineCommand::NextKey)] = { "Next Key", { ImGuiKey_Period } };
	m_Bindings[Index(VansTimelineCommand::FrameSelection)] = { "Frame Selection", { ImGuiKey_F } };
	m_Bindings[Index(VansTimelineCommand::FrameAll)] = { "Frame All", { ImGuiKey_Home } };
	m_Bindings[Index(VansTimelineCommand::AddKey)] = { "Add Key", { ImGuiKey_Enter } };
	m_Bindings[Index(VansTimelineCommand::Rename)] = { "Rename", { ImGuiKey_F2 } };
	m_Bindings[Index(VansTimelineCommand::SetPlaybackStart)] = { "Set Playback Start", { ImGuiKey_LeftBracket } };
	m_Bindings[Index(VansTimelineCommand::SetPlaybackEnd)] = { "Set Playback End", { ImGuiKey_RightBracket } };
	m_Bindings[Index(VansTimelineCommand::SetSelectionStart)] = { "Set Selection Start", { ImGuiKey_I } };
	m_Bindings[Index(VansTimelineCommand::SetSelectionEnd)] = { "Set Selection End", { ImGuiKey_O } };
	m_Bindings[Index(VansTimelineCommand::AddMarker)] = { "Add Marker", { ImGuiKey_M } };
	m_Bindings[Index(VansTimelineCommand::ToggleAutoKey)] = { "Toggle Auto Key", { ImGuiKey_K } };
	m_Bindings[Index(VansTimelineCommand::CancelInteraction)] = { "Cancel Interaction", { ImGuiKey_Escape } };
}

bool VansTimelineCommandMap::IsTriggered(VansTimelineCommand command) const
{
	const VansTimelineCommandChord& chord = GetBinding(command).chord;
	if (chord.key == ImGuiKey_None) return false;
	const ImGuiIO& input = ImGui::GetIO();
	return input.KeyCtrl == chord.control && input.KeyShift == chord.shift &&
		input.KeyAlt == chord.alt && ImGui::IsKeyPressed(chord.key, false);
}

const VansTimelineCommandBinding& VansTimelineCommandMap::GetBinding(
	VansTimelineCommand command) const
{
	return m_Bindings[Index(command)];
}

void VansTimelineCommandMap::SetBinding(
	VansTimelineCommand command,
	VansTimelineCommandChord chord)
{
	m_Bindings[Index(command)].chord = chord;
}
}
