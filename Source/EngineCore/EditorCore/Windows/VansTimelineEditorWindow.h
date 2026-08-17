#pragma once

#include "VansBaseWindowComponent.h"
#include "../Timeline/VansTimelineCommandMap.h"
#include "../Timeline/VansTimelineEditService.h"
#include "../Timeline/VansTimelinePreviewSession.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace VansGraphics
{
class VansTimelineEditorWindow final : public VansBaseWindowComponent
{
public:
	void Open(const std::string& timelinePath, std::string ownerEntityGuid = {});
	void Close();
	bool IsOpen() const { return m_IsOpen; }
	void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

private:
	enum class SelectionKind { Asset, Track, Section, Channel, Key, Marker };
	struct Selection
	{
		SelectionKind kind = SelectionKind::Asset;
		Vans::VansTimelineId id;
		Vans::VansTimelineId trackId;
		Vans::VansTimelineId sectionId;
		std::size_t channelIndex = 0;
	};

	enum class TimelineRowKind { Track, Section, Channel };
	struct TimelineRow
	{
		TimelineRowKind kind = TimelineRowKind::Track;
		std::size_t trackIndex = 0;
		std::size_t sectionIndex = 0;
		std::size_t channelIndex = 0;
	};

	enum class CanvasDragKind { None, MoveSection, TrimSectionStart, TrimSectionEnd, MoveKey, MoveMarker };
	struct CanvasDrag
	{
		CanvasDragKind kind = CanvasDragKind::None;
		Vans::VansTimelineId trackId;
		Vans::VansTimelineId sectionId;
		Vans::VansTimelineId objectId;
		std::size_t channelIndex = 0;
		Vans::VansTimelineTick initialStartTick = 0;
		Vans::VansTimelineTick initialDurationTicks = 0;
		Vans::VansTimelineTick initialLocalTick = 0;
		float initialMouseX = 0.0f;
	};

	void DrawToolbar();
	void DrawOutliner();
	void DrawTimeline();
	void DrawInspector();
	void DrawAddTrackMenu();
	void HandleShortcuts();
	void HandleCanvasDrag();
	void EnsurePreview();
	void RefreshPreview();
	void SeekPreview(Vans::VansTimelineTick tick);
	void SetError(std::string message);
	void DeleteSelection();
	void CopySelection();
	void PasteSelection();
	void DuplicateSelection();
	void AddKeyAtPlayhead();
	void AddKey(const Vans::VansTimelineId& trackId, const Vans::VansTimelineId& sectionId,
		std::size_t channelIndex, Vans::VansTimelineTick globalTick);
	bool ApplyAsset(Vans::VansTimelineAsset asset);
	bool DrawSerializedValue(const char* label, Vans::VansSerializedValue& value, bool readOnly = false);
	std::vector<TimelineRow> BuildRows() const;
	double TickToX(Vans::VansTimelineTick tick, double originX) const;
	Vans::VansTimelineTick XToTick(double x, double originX) const;
	Vans::VansTimelineTick SnapTick(Vans::VansTimelineTick tick) const;
	std::string FormatTick(Vans::VansTimelineTick tick) const;
	Vans::VansTimelineTrack* FindTrack(Vans::VansTimelineAsset& asset) const;
	Vans::VansTimelineSection* FindSection(Vans::VansTimelineAsset& asset) const;
	Vans::VansTimelineChannel* FindChannel(Vans::VansTimelineAsset& asset) const;
	Vans::VansTimelineKey* FindKey(Vans::VansTimelineAsset& asset) const;

	bool m_IsOpen = false;
	std::string m_Path;
	std::string m_InstanceOwnerGuid;
	std::string m_LastError;
	Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
	Vans::VansTimelineEditService m_Edit;
	Vans::VansTimelineCommandMap m_CommandMap;
	Vans::VansTimelinePreviewSession m_Preview;
	Selection m_Selection;
	CanvasDrag m_CanvasDrag;
	Vans::VansTimelineTick m_Playhead = 0;
	Vans::VansTimelineTick m_ViewStart = 0;
	double m_PixelsPerTick = 0.01;
	float m_RowHeight = 27.0f;
	float m_VerticalScroll = 0.0f;
	float m_LastCanvasWidth = 640.0f;
	bool m_SnapEnabled = true;
	bool m_PreviewSafeEvents = false;
	bool m_IncludeSubTimelines = true;
	std::unordered_set<Vans::VansTimelineId> m_CollapsedTracks;
	std::optional<Vans::VansTimelineTrack> m_TrackClipboard;
	std::optional<Vans::VansTimelineSection> m_SectionClipboard;
};
}
