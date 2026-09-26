#pragma once

#include <cstdint>

namespace VansGraphics
{
struct VansEditorConfiguration;

enum class VansEditorToolbarAction : std::uint8_t
{
	Play,
	Pause,
	Stop
};

class VansEditorTheme final
{
public:
	static void Apply(const VansEditorConfiguration& configuration);
	static void PushToolbarActionColors(VansEditorToolbarAction action);
	static void PopToolbarActionColors();
};
}
