#pragma once

#include "../../TimelineCore/VansTimelineAsset.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace Vans
{
struct VansOpenAssetDocument;
namespace EditorAPI { class IEngineEditorAPI; }

struct TimelineEditResult
{
	bool success = false;
	std::string message;
	VansTimelineId objectId;

	explicit operator bool() const { return success; }
};

class VansTimelineEditService
{
public:
	TimelineEditResult Open(const std::filesystem::path& sourcePath);
	TimelineEditResult Reload();
	TimelineEditResult Save(EditorAPI::IEngineEditorAPI& editorAPI);
	TimelineEditResult RevertToSaved();
	TimelineEditResult Undo();
	TimelineEditResult Redo();

	TimelineEditResult BeginInteraction();
	TimelineEditResult CommitInteraction();
	TimelineEditResult CancelInteraction();
	bool IsInteracting() const { return m_InteractionSnapshot.has_value(); }

	TimelineEditResult AddBinding(VansTimelineBinding binding);
	TimelineEditResult AddGroup(VansTimelineGroup group);
	TimelineEditResult AddMarker(VansTimelineMarker marker);
	TimelineEditResult MoveMarker(VansTimelineId markerId, VansTimelineTick tick);
	TimelineEditResult AddTrack(VansTimelineTrackTypeId type, VansTimelineId bindingId,
		VansTimelineId groupId = {}, VansSerializedValue extensionData = VansSerializedValue::Object({}));
	TimelineEditResult AddSection(VansTimelineId trackId, VansTimelineSection section);
	TimelineEditResult AddKey(VansTimelineId trackId, VansTimelineId sectionId,
		std::size_t channelIndex, VansTimelineKey key);
	TimelineEditResult MoveKey(VansTimelineId trackId, VansTimelineId sectionId,
		std::size_t channelIndex, VansTimelineId keyId, VansTimelineTick tick);
	TimelineEditResult MoveKeysBy(const std::unordered_set<VansTimelineId>& keyIds,
		VansTimelineTick deltaTicks);
	TimelineEditResult DuplicateKeys(const std::unordered_set<VansTimelineId>& keyIds,
		VansTimelineTick offsetTicks);
	TimelineEditResult SetKeyValue(VansTimelineId trackId, VansTimelineId sectionId,
		std::size_t channelIndex, VansTimelineId keyId, VansTimelineKeyValue value);
	TimelineEditResult ReplaceChannelKeys(VansTimelineId trackId, VansTimelineId sectionId,
		std::size_t channelIndex, std::vector<VansTimelineKey> keys);
	TimelineEditResult MoveSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick startTick);
	TimelineEditResult MoveSectionsBy(const std::unordered_set<VansTimelineId>& sectionIds, VansTimelineTick deltaTicks);
	TimelineEditResult SlipSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick sourceDeltaTicks);
	TimelineEditResult RippleMoveSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick startTick);
	TimelineEditResult TrimSection(VansTimelineId trackId, VansTimelineId sectionId,
		VansTimelineTick startTick, VansTimelineTick durationTicks);
	TimelineEditResult RippleTrimSection(VansTimelineId trackId, VansTimelineId sectionId,
		VansTimelineTick startTick, VansTimelineTick durationTicks);
	TimelineEditResult ScaleSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick durationTicks);
	TimelineEditResult LoopExtendSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick durationTicks);
	TimelineEditResult SplitSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick splitTick);
	TimelineEditResult DuplicateSection(VansTimelineId trackId, VansTimelineId sectionId, VansTimelineTick offsetTicks);
	TimelineEditResult PasteSection(VansTimelineId trackId, VansTimelineSection section, VansTimelineTick startTick);
	TimelineEditResult MoveTrack(VansTimelineId trackId, VansTimelineId groupId, VansTimelineId beforeTrackId = {});
	TimelineEditResult SetTrackLocked(VansTimelineId trackId, bool locked);
	TimelineEditResult SetPlaybackRange(VansTimelineTick startTick, VansTimelineTick endTick);
	TimelineEditResult RenameObject(const VansTimelineId& objectId, std::string name);
	TimelineEditResult RemoveObject(const VansTimelineId& objectId);
	TimelineEditResult ReplaceAsset(VansTimelineAsset asset);

	const VansTimelineAsset& Asset() const { return m_Asset; }
	VansTimelineAsset& PreviewAsset() { return m_Asset; }
	const VansTimelineDiagnostics& Diagnostics() const { return m_Diagnostics; }
	const std::shared_ptr<VansOpenAssetDocument>& Document() const { return m_Document; }
	bool IsDirty() const;
	bool CanUndo() const;
	bool CanRedo() const;

	static VansSerializedValue DefaultExtensionData(VansTimelineTrackTypeId type);
	static VansTimelineId NewStableId();

private:
	TimelineEditResult ValidateWorkingCopy();
	TimelineEditResult CommitWorkingCopy();
	TimelineEditResult FinishMutation(VansTimelineId objectId = {});
	VansTimelineTrack* FindTrack(const VansTimelineId& id);
	VansTimelineSection* FindSection(VansTimelineTrack& track, const VansTimelineId& id);

	std::filesystem::path m_SourcePath;
	std::shared_ptr<VansOpenAssetDocument> m_Document;
	VansTimelineAsset m_Asset;
	VansTimelineDiagnostics m_Diagnostics;
	std::optional<VansTimelineAsset> m_InteractionSnapshot;
};
}
