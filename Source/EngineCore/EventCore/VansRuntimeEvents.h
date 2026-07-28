#pragma once

#include <cstdint>
#include <string>

namespace Vans
{
	struct VansFrameStartedEvent
	{
		std::uint64_t frameIndex = 0;
		float deltaTime = 0.0f;
	};

	struct VansFrameEndedEvent
	{
		std::uint64_t frameIndex = 0;
	};

	struct VansMainThreadJobFailedEvent
	{
		std::string message;
	};

	struct VansRuntimeShutdownEvent
	{
	};
}
