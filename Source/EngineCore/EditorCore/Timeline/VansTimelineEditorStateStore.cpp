#include "VansTimelineEditorStateStore.h"

#include "../../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../../TimelineCore/VansTimelineSerialization.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Vans
{
namespace
{
using Json = nlohmann::ordered_json;

std::filesystem::path EditorStateRoot()
{
	if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData && *localAppData)
		return std::filesystem::path(localAppData) / "ForestEngine" / "Editor" / "Timeline";
	return std::filesystem::temp_directory_path() / "ForestEngine" / "Editor" / "Timeline";
}

std::string StablePathKey(const std::filesystem::path& sourcePath)
{
	std::string normalized = std::filesystem::absolute(sourcePath).lexically_normal().generic_string();
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	std::uint64_t hash = 1469598103934665603ull;
	for (unsigned char character : normalized)
	{
		hash ^= character;
		hash *= 1099511628211ull;
	}
	std::ostringstream stream;
	stream << std::hex << std::setw(16) << std::setfill('0') << hash;
	return stream.str();
}

Json EncodeSet(const std::unordered_set<VansTimelineId>& values)
{
	std::vector<std::string> sorted(values.begin(), values.end());
	std::sort(sorted.begin(), sorted.end());
	return sorted;
}

std::unordered_set<VansTimelineId> DecodeSet(const Json& root, const char* field)
{
	std::unordered_set<VansTimelineId> values;
	for (const auto& value : root.value(field, Json::array()))
		if (value.is_string()) values.insert(value.get<std::string>());
	return values;
}

bool PublishJson(const std::filesystem::path& path, const Json& root, std::string& error)
{
	error.clear();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec) { error = "Could not create Timeline Editor state directory: " + ec.message(); return false; }
	const std::filesystem::path temporary = path.string() + ".tmp";
	{
		std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
		if (!stream) { error = "Could not stage Timeline Editor user state"; return false; }
		stream << root.dump(2) << '\n';
		if (!stream.good()) { error = "Could not write Timeline Editor user state"; return false; }
	}
	VansStagedFileTransaction transaction;
	transaction.Add({ path, temporary });
	return transaction.Publish(error);
}
}

std::filesystem::path VansTimelineEditorStateStore::UserStatePath(const std::filesystem::path& sourcePath)
{
	return EditorStateRoot() / "UserState" / (StablePathKey(sourcePath) + ".json");
}

std::filesystem::path VansTimelineEditorStateStore::RecoveryPath(const std::filesystem::path& sourcePath)
{
	return EditorStateRoot() / "Recovery" / (StablePathKey(sourcePath) + ".vtimeline");
}

bool VansTimelineEditorStateStore::LoadUserState(
	const std::filesystem::path& sourcePath,
	VansTimelineEditorUserState& state,
	std::string& error)
{
	error.clear();
	const auto path = UserStatePath(sourcePath);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec)) return true;
	try
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) { error = "Could not open Timeline Editor user state"; return false; }
		Json root; stream >> root;
		state.playhead = root.value("playhead", state.playhead);
		state.viewStart = root.value("viewStart", state.viewStart);
		state.pixelsPerTick = root.value("pixelsPerTick", state.pixelsPerTick);
		state.trackScroll = root.value("trackScroll", state.trackScroll);
		state.rowHeight = root.value("rowHeight", state.rowHeight);
		state.curveHeight = root.value("curveHeight", state.curveHeight);
		state.curveValueZoom = root.value("curveValueZoom", state.curveValueZoom);
		state.curveValuePan = root.value("curveValuePan", state.curveValuePan);
		state.showCurves = root.value("showCurves", state.showCurves);
		state.showWaveforms = root.value("showWaveforms", state.showWaveforms);
		state.showThumbnails = root.value("showThumbnails", state.showThumbnails);
		state.snapEnabled = root.value("snapEnabled", state.snapEnabled);
		state.snapFrames = root.value("snapFrames", state.snapFrames);
		state.snapKeys = root.value("snapKeys", state.snapKeys);
		state.snapMarkers = root.value("snapMarkers", state.snapMarkers);
		state.snapSections = root.value("snapSections", state.snapSections);
		state.snapRanges = root.value("snapRanges", state.snapRanges);
		state.snapPlayhead = root.value("snapPlayhead", state.snapPlayhead);
		state.previewSafeEvents = root.value("previewSafeEvents", state.previewSafeEvents);
		state.includeSubTimelines = root.value("includeSubTimelines", state.includeSubTimelines);
		state.reversePlayback = root.value("reversePlayback", state.reversePlayback);
		state.loopPlaybackRange = root.value("loopPlaybackRange", state.loopPlaybackRange);
		state.autoKeyMode = root.value("autoKeyMode", state.autoKeyMode);
		state.timeDisplayMode = root.value("timeDisplayMode", state.timeDisplayMode);
		state.sectionEditMode = root.value("sectionEditMode", state.sectionEditMode);
		state.search = root.value("search", state.search);
		state.mutedTracks = DecodeSet(root, "mutedTracks");
		state.soloTracks = DecodeSet(root, "soloTracks");
		state.hiddenTracks = DecodeSet(root, "hiddenTracks");
		state.pinnedTracks = DecodeSet(root, "pinnedTracks");
		return true;
	}
	catch (const std::exception& exception)
	{
		error = std::string("Timeline Editor user state is invalid: ") + exception.what();
		return false;
	}
}

bool VansTimelineEditorStateStore::SaveUserState(
	const std::filesystem::path& sourcePath,
	const VansTimelineEditorUserState& state,
	std::string& error)
{
	Json root{
		{ "playhead", state.playhead }, { "viewStart", state.viewStart },
		{ "pixelsPerTick", state.pixelsPerTick }, { "trackScroll", state.trackScroll },
		{ "rowHeight", state.rowHeight },
		{ "curveHeight", state.curveHeight }, { "curveValueZoom", state.curveValueZoom },
		{ "curveValuePan", state.curveValuePan },
		{ "showCurves", state.showCurves }, { "showWaveforms", state.showWaveforms },
		{ "showThumbnails", state.showThumbnails }, { "snapEnabled", state.snapEnabled },
		{ "snapFrames", state.snapFrames }, { "snapKeys", state.snapKeys },
		{ "snapMarkers", state.snapMarkers }, { "snapSections", state.snapSections },
		{ "snapRanges", state.snapRanges }, { "snapPlayhead", state.snapPlayhead },
		{ "previewSafeEvents", state.previewSafeEvents },
		{ "includeSubTimelines", state.includeSubTimelines },
		{ "reversePlayback", state.reversePlayback }, { "loopPlaybackRange", state.loopPlaybackRange },
		{ "autoKeyMode", state.autoKeyMode },
		{ "timeDisplayMode", state.timeDisplayMode },
		{ "sectionEditMode", state.sectionEditMode }, { "search", state.search },
		{ "mutedTracks", EncodeSet(state.mutedTracks) }, { "soloTracks", EncodeSet(state.soloTracks) },
		{ "hiddenTracks", EncodeSet(state.hiddenTracks) }, { "pinnedTracks", EncodeSet(state.pinnedTracks) }
	};
	return PublishJson(UserStatePath(sourcePath), root, error);
}

bool VansTimelineEditorStateStore::LoadRecoveryIfNewer(
	const std::filesystem::path& sourcePath,
	VansTimelineAsset& asset,
	bool& available,
	std::string& error)
{
	available = false;
	error.clear();
	const auto recovery = RecoveryPath(sourcePath);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(recovery, ec)) return true;
	const auto recoveryTime = std::filesystem::last_write_time(recovery, ec);
	if (ec) { error = "Could not inspect Timeline recovery snapshot"; return false; }
	const bool sourceExists = std::filesystem::is_regular_file(sourcePath, ec);
	if (ec) ec.clear();
	if (sourceExists)
	{
		const auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
		if (ec) { error = "Could not inspect Timeline source timestamp"; return false; }
		if (recoveryTime <= sourceTime) return true;
	}
	if (!VansTimelineSerialization::Load(recovery, asset, error)) return false;
	available = true;
	return true;
}

bool VansTimelineEditorStateStore::SaveRecovery(
	const std::filesystem::path& sourcePath,
	const VansTimelineAsset& asset,
	std::string& error)
{
	const auto recovery = RecoveryPath(sourcePath);
	std::error_code ec;
	std::filesystem::create_directories(recovery.parent_path(), ec);
	if (ec) { error = "Could not create Timeline recovery directory: " + ec.message(); return false; }
	return VansTimelineSerialization::SaveAtomic(recovery, asset, error);
}

void VansTimelineEditorStateStore::RemoveRecovery(const std::filesystem::path& sourcePath)
{
	std::error_code ignored;
	std::filesystem::remove(RecoveryPath(sourcePath), ignored);
}

void VansTimelineEditorStateStore::RemoveUserState(const std::filesystem::path& sourcePath)
{
	std::error_code ignored;
	std::filesystem::remove(UserStatePath(sourcePath), ignored);
}
}
