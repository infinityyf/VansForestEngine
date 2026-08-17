#pragma once

#include <imgui.h>

#include <array>
#include <cstddef>

namespace Vans
{
enum class VansTimelineCommand : std::size_t
{
	Save,
	Undo,
	Redo,
	Copy,
	Paste,
	Duplicate,
	DeleteSelection,
	Count
};

struct VansTimelineCommandChord
{
	ImGuiKey key = ImGuiKey_None;
	bool control = false;
	bool shift = false;
	bool alt = false;
};

struct VansTimelineCommandBinding
{
	const char* name = "";
	VansTimelineCommandChord chord;
};

class VansTimelineCommandMap
{
public:
	VansTimelineCommandMap();

	bool IsTriggered(VansTimelineCommand command) const;
	const VansTimelineCommandBinding& GetBinding(VansTimelineCommand command) const;
	void SetBinding(VansTimelineCommand command, VansTimelineCommandChord chord);

private:
	std::array<VansTimelineCommandBinding,
		static_cast<std::size_t>(VansTimelineCommand::Count)> m_Bindings;
};
}
