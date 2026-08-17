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
	m_Bindings[Index(VansTimelineCommand::DeleteSelection)] = { "Delete", { ImGuiKey_Delete } };
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
