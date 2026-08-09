#pragma once

#include "../../TimelineCore/VansTimelineAsset.h"

#include <filesystem>
#include <string>
#include <unordered_set>

namespace Vans
{
struct VansTimelineEditorUserState
{
	VansTimelineTick playhead = 0;
	VansTimelineTick viewStart = 0;
	double pixelsPerTick = 0.02;
	float trackScroll = 0.0f;
	float rowHeight = 30.0f;
	float curveHeight = 190.0f;
	double curveValueZoom = 1.0;
	double curveValuePan = 0.0;
	bool showCurves = true;
	bool showWaveforms = true;
	bool showThumbnails = true;
	bool snapEnabled = true;
	bool snapFrames = true;
	bool snapKeys = true;
	bool snapMarkers = true;
	bool snapSections = true;
	bool snapRanges = true;
	bool snapPlayhead = true;
	bool previewSafeEvents = false;
	bool includeSubTimelines = false;
	bool reversePlayback = false;
	bool loopPlaybackRange = false;
	int autoKeyMode = 0;
	int timeDisplayMode = 0;
	int sectionEditMode = 0;
	std::string search;
	std::unordered_set<VansTimelineId> mutedTracks;
	std::unordered_set<VansTimelineId> soloTracks;
	std::unordered_set<VansTimelineId> hiddenTracks;
	std::unordered_set<VansTimelineId> pinnedTracks;
};

class VansTimelineEditorStateStore
{
public:
	static bool LoadUserState(const std::filesystem::path& sourcePath,
		VansTimelineEditorUserState& state, std::string& error);
	static bool SaveUserState(const std::filesystem::path& sourcePath,
		const VansTimelineEditorUserState& state, std::string& error);
	static bool LoadRecoveryIfNewer(const std::filesystem::path& sourcePath,
		VansTimelineAsset& asset, bool& available, std::string& error);
	static bool SaveRecovery(const std::filesystem::path& sourcePath,
		const VansTimelineAsset& asset, std::string& error);
	static void RemoveUserState(const std::filesystem::path& sourcePath);
	static void RemoveRecovery(const std::filesystem::path& sourcePath);

private:
	static std::filesystem::path UserStatePath(const std::filesystem::path& sourcePath);
	static std::filesystem::path RecoveryPath(const std::filesystem::path& sourcePath);
};
}
