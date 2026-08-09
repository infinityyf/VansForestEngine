#include "VansTimelineDerivedMediaService.h"

#include "../../AudioCore/VansAudioWaveform.h"
#include "../../RenderCore/VulkanCore/VansVideoThumbnail.h"

#include <chrono>
#include <system_error>
#include <utility>

namespace Vans::EditorAPI
{
namespace
{
constexpr auto FingerprintProbeInterval = std::chrono::seconds(1);
}

template <typename Snapshot>
void VansTimelineDerivedMediaService::WaitForPending(Entry<Snapshot>& entry)
{
	if (entry.pending.valid()) entry.pending.wait();
}

template <typename Snapshot>
void VansTimelineDerivedMediaService::PollPending(Entry<Snapshot>& entry)
{
	if (!entry.pending.valid() ||
		entry.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;

	auto result = entry.pending.get();
	entry.snapshot = std::move(result.snapshot);
	entry.error = std::move(result.error);
	entry.failed = !entry.snapshot;
}

VansTimelineDerivedMediaService::~VansTimelineDerivedMediaService()
{
	Clear();
}

bool VansTimelineDerivedMediaService::FileFingerprint::operator==(
	const FileFingerprint& other) const
{
	return normalizedPath == other.normalizedPath && size == other.size &&
		writeTime == other.writeTime && readable == other.readable;
}

VansTimelineDerivedMediaService::FileFingerprint VansTimelineDerivedMediaService::ProbeFile(
	const std::filesystem::path& sourcePath)
{
	FileFingerprint fingerprint;
	fingerprint.normalizedPath = sourcePath.lexically_normal().generic_string();

	std::error_code sizeError;
	fingerprint.size = std::filesystem::file_size(sourcePath, sizeError);
	std::error_code timeError;
	const auto writeTime = std::filesystem::last_write_time(sourcePath, timeError);
	fingerprint.writeTime = timeError ? 0 : writeTime.time_since_epoch().count();
	fingerprint.readable = !sizeError && !timeError;
	if (!fingerprint.readable) fingerprint.size = 0;
	return fingerprint;
}

bool VansTimelineDerivedMediaService::ProbeDue(std::chrono::steady_clock::time_point lastProbe)
{
	return lastProbe.time_since_epoch().count() == 0 ||
		std::chrono::steady_clock::now() - lastProbe >= FingerprintProbeInterval;
}

TimelineAudioWaveformHandle VansTimelineDerivedMediaService::RequestWaveform(
	const std::string& assetIdentity,
	const std::filesystem::path& sourcePath)
{
	if (assetIdentity.empty() || sourcePath.empty()) return {};
	auto [found, inserted] = m_Waveforms.try_emplace(assetIdentity);
	auto& entry = found->second;
	const bool pathChanged = entry.initialized &&
		entry.fingerprint.normalizedPath != sourcePath.lexically_normal().generic_string();
	if (inserted || pathChanged || ProbeDue(entry.lastProbe))
	{
		entry.lastProbe = std::chrono::steady_clock::now();
		const FileFingerprint fingerprint = ProbeFile(sourcePath);
		if (!entry.initialized || fingerprint != entry.fingerprint)
		{
			WaitForPending(entry);
			entry.fingerprint = fingerprint;
			entry.initialized = true;
			entry.snapshot.reset();
			entry.error.clear();
			entry.failed = false;
			entry.pending = std::async(std::launch::async, [sourcePath]()
			{
				BuildResult<TimelineAudioWaveformSnapshot> result;
				VansEngine::VansAudioWaveform waveform;
				if (!VansEngine::VansAudioWaveformBuilder::Build(
					sourcePath, 1024, waveform, result.error))
					return result;

				auto snapshot = std::make_shared<TimelineAudioWaveformSnapshot>();
				snapshot->durationSeconds = waveform.durationSeconds;
				snapshot->minima = std::move(waveform.minima);
				snapshot->maxima = std::move(waveform.maxima);
				result.snapshot = std::move(snapshot);
				return result;
			});
		}
	}
	PollPending(entry);
	return entry.snapshot;
}

TimelineVideoThumbnailHandle VansTimelineDerivedMediaService::RequestThumbnail(
	const std::string& assetIdentity,
	const std::filesystem::path& sourcePath)
{
	if (assetIdentity.empty() || sourcePath.empty()) return {};
	auto [found, inserted] = m_Thumbnails.try_emplace(assetIdentity);
	auto& entry = found->second;
	const bool pathChanged = entry.initialized &&
		entry.fingerprint.normalizedPath != sourcePath.lexically_normal().generic_string();
	if (inserted || pathChanged || ProbeDue(entry.lastProbe))
	{
		entry.lastProbe = std::chrono::steady_clock::now();
		const FileFingerprint fingerprint = ProbeFile(sourcePath);
		if (!entry.initialized || fingerprint != entry.fingerprint)
		{
			WaitForPending(entry);
			entry.fingerprint = fingerprint;
			entry.initialized = true;
			entry.snapshot.reset();
			entry.error.clear();
			entry.failed = false;
			entry.pending = std::async(std::launch::async, [sourcePath]()
			{
				BuildResult<TimelineVideoThumbnailSnapshot> result;
				VansGraphics::VansVideoThumbnail thumbnail;
				if (!VansGraphics::VansVideoThumbnailBuilder::Build(
					sourcePath, 64, 36, thumbnail, result.error))
					return result;

				auto snapshot = std::make_shared<TimelineVideoThumbnailSnapshot>();
				snapshot->width = thumbnail.width;
				snapshot->height = thumbnail.height;
				snapshot->durationSeconds = thumbnail.durationSeconds;
				snapshot->rgba = std::move(thumbnail.rgba);
				result.snapshot = std::move(snapshot);
				return result;
			});
		}
	}
	PollPending(entry);
	return entry.snapshot;
}

void VansTimelineDerivedMediaService::Clear()
{
	for (auto& [identity, entry] : m_Waveforms)
	{
		(void)identity;
		WaitForPending(entry);
	}
	for (auto& [identity, entry] : m_Thumbnails)
	{
		(void)identity;
		WaitForPending(entry);
	}
	m_Waveforms.clear();
	m_Thumbnails.clear();
}
}
