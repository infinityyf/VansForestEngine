#include "VansAudioWaveform.h"

#include "VansAudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace VansEngine
{
bool VansAudioWaveformBuilder::Build(
	const std::filesystem::path& sourcePath,
	std::size_t binCount,
	VansAudioWaveform& waveform,
	std::string& error)
{
	waveform = {};
	error.clear();
	binCount = std::clamp<std::size_t>(binCount, 32, 4096);
	if (sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath))
	{
		error = "Audio waveform source does not exist";
		return false;
	}

	VansAudioDecoder decoder;
	if (!decoder.Open(sourcePath.string(), 1, 24000))
	{
		error = "Audio waveform decoder could not open the source";
		return false;
	}
	waveform.durationSeconds = decoder.GetDuration();
	const std::uint64_t expectedSamples = waveform.durationSeconds > 0.0
		? std::max<std::uint64_t>(1, static_cast<std::uint64_t>(
			std::ceil(waveform.durationSeconds * decoder.GetSampleRate())))
		: 0;

	std::vector<std::int16_t> unknownDurationSamples;
	waveform.minima.assign(binCount, std::numeric_limits<float>::max());
	waveform.maxima.assign(binCount, std::numeric_limits<float>::lowest());
	std::vector<std::uint32_t> counts(binCount, 0);
	std::uint64_t sampleIndex = 0;
	for (;;)
	{
		AudioPCMChunk chunk = decoder.DecodeNextChunk();
		for (std::int16_t sample : chunk.samples)
		{
			if (expectedSamples == 0)
			{
				unknownDurationSamples.push_back(sample);
				continue;
			}
			const std::size_t bin = std::min<std::size_t>(binCount - 1,
				static_cast<std::size_t>(sampleIndex * binCount / expectedSamples));
			const float value = static_cast<float>(sample) / 32768.0f;
			waveform.minima[bin] = std::min(waveform.minima[bin], value);
			waveform.maxima[bin] = std::max(waveform.maxima[bin], value);
			++counts[bin];
			++sampleIndex;
		}
		if (chunk.endOfStream) break;
	}

	if (expectedSamples == 0)
	{
		if (unknownDurationSamples.empty())
		{
			error = "Audio waveform source contains no decoded samples";
			return false;
		}
		waveform.durationSeconds = static_cast<double>(unknownDurationSamples.size()) /
			static_cast<double>(std::max(1, decoder.GetSampleRate()));
		for (std::size_t index = 0; index < unknownDurationSamples.size(); ++index)
		{
			const std::size_t bin = std::min<std::size_t>(binCount - 1,
				index * binCount / unknownDurationSamples.size());
			const float value = static_cast<float>(unknownDurationSamples[index]) / 32768.0f;
			waveform.minima[bin] = std::min(waveform.minima[bin], value);
			waveform.maxima[bin] = std::max(waveform.maxima[bin], value);
			++counts[bin];
		}
	}
	if (sampleIndex == 0 && unknownDurationSamples.empty())
	{
		error = "Audio waveform source contains no decoded samples";
		return false;
	}
	for (std::size_t bin = 0; bin < binCount; ++bin)
	{
		if (counts[bin] != 0) continue;
		waveform.minima[bin] = 0.0f;
		waveform.maxima[bin] = 0.0f;
	}
	return true;
}
}
