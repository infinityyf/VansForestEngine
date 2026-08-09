#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace VansEngine
{
struct VansAudioWaveform
{
	double durationSeconds = 0.0;
	std::vector<float> minima;
	std::vector<float> maxima;
};

class VansAudioWaveformBuilder
{
public:
	static bool Build(const std::filesystem::path& sourcePath, std::size_t binCount,
		VansAudioWaveform& waveform, std::string& error);
};
}
