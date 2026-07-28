#pragma once

#include <cstddef>

namespace Vans
{
	enum class VansEventLane
	{
		MainThread = 0,
		Input,
		Physics,
		GameLogic,
		Script,
		Editor,
		RenderPrep,
		Diagnostics,
		Count
	};

	constexpr std::size_t ToEventLaneIndex(VansEventLane lane)
	{
		return static_cast<std::size_t>(lane);
	}

	inline const char* ToString(VansEventLane lane)
	{
		switch (lane)
		{
		case VansEventLane::MainThread: return "MainThread";
		case VansEventLane::Input: return "Input";
		case VansEventLane::Physics: return "Physics";
		case VansEventLane::GameLogic: return "GameLogic";
		case VansEventLane::Script: return "Script";
		case VansEventLane::Editor: return "Editor";
		case VansEventLane::RenderPrep: return "RenderPrep";
		case VansEventLane::Diagnostics: return "Diagnostics";
		default: return "Unknown";
		}
	}
}
