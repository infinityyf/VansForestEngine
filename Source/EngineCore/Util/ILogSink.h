#pragma once

#include <string>

enum class VansLogLevel;

enum class VansLogChannel
{
	Engine,
	Script
};

class ILogSink
{
public:
	virtual ~ILogSink() = default;
	virtual void OnLog(VansLogChannel channel, VansLogLevel level, const std::string& message) = 0;
};
