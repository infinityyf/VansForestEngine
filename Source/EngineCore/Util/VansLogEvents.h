#pragma once

#include "VansLog.h"

#include <string>

namespace Vans
{
	struct VansLogEvent
	{
		VansLogChannel channel = VansLogChannel::Engine;
		VansLogLevel level = VansLogLevel::Info;
		std::string message;
		std::string timestamp;
	};
}
