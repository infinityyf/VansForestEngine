#pragma once

#include "../Public/EngineDTOs.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>

namespace Vans::EditorAPI
{
class VansTimelineDerivedMediaService
{
public:
	~VansTimelineDerivedMediaService();

	TimelineAudioWaveformHandle RequestWaveform(
		const std::string& assetIdentity, const std::filesystem::path& sourcePath);
	TimelineVideoThumbnailHandle RequestThumbnail(
		const std::string& assetIdentity, const std::filesystem::path& sourcePath);
	void Clear();

private:
	struct FileFingerprint
	{
		std::string normalizedPath;
		std::uintmax_t size = 0;
		std::int64_t writeTime = 0;
		bool readable = false;

		bool operator==(const FileFingerprint& other) const;
		bool operator!=(const FileFingerprint& other) const { return !(*this == other); }
	};

	template <typename Snapshot>
	struct BuildResult
	{
		std::shared_ptr<const Snapshot> snapshot;
		std::string error;
	};

	template <typename Snapshot>
	struct Entry
	{
		FileFingerprint fingerprint;
		std::chrono::steady_clock::time_point lastProbe{};
		std::future<BuildResult<Snapshot>> pending;
		std::shared_ptr<const Snapshot> snapshot;
		std::string error;
		bool initialized = false;
		bool failed = false;
	};

	static FileFingerprint ProbeFile(const std::filesystem::path& sourcePath);
	static bool ProbeDue(std::chrono::steady_clock::time_point lastProbe);
	template <typename Snapshot>
	static void WaitForPending(Entry<Snapshot>& entry);
	template <typename Snapshot>
	static void PollPending(Entry<Snapshot>& entry);
	std::unordered_map<std::string, Entry<TimelineAudioWaveformSnapshot>> m_Waveforms;
	std::unordered_map<std::string, Entry<TimelineVideoThumbnailSnapshot>> m_Thumbnails;
};
}
