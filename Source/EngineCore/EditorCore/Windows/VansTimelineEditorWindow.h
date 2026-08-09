#pragma once

#include "VansBaseWindowComponent.h"
#include "../Timeline/VansTimelineCommandMap.h"
#include "../Timeline/VansTimelineEditService.h"
#include "../Timeline/VansTimelineEditorStateStore.h"
#include "../Timeline/VansTimelinePreviewSession.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <optional>
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
	enum class SelectionKind { Asset, Binding, Group, Track, Section, Channel, Key, Marker };
	enum class CanvasDragKind { None, MoveSection, TrimSectionStart, TrimSectionEnd, MoveKey, MoveMarker };
	enum class CurveDragKind { None, MoveKey, ArriveTangent, LeaveTangent };
	enum class SectionEditMode { Move, Slip, Ripple, Scale, LoopExtend };
	enum class AutoKeyMode { Off, KeyExisting, KeyAllAllowed };
	enum class TimeDisplayMode { Frames, Seconds, Timecode };
	struct Selection
	{
		SelectionKind kind = SelectionKind::Asset;
		Vans::VansTimelineId id;
		Vans::VansTimelineId trackId;
		Vans::VansTimelineId sectionId;
		std::size_t channelIndex = 0;
	};
	struct CanvasDrag
	{
		CanvasDragKind kind = CanvasDragKind::None;
		Vans::VansTimelineId trackId;
		Vans::VansTimelineId sectionId;
		Vans::VansTimelineId keyId;
		std::size_t channelIndex = 0;
		Vans::VansTimelineTick initialStartTick = 0;
		Vans::VansTimelineTick initialDurationTicks = 0;
		Vans::VansTimelineTick initialKeyTick = 0;
		float initialMouseX = 0.0f;
		Vans::VansTimelineTick lastDeltaTick = 0;
	};
	struct CurveDrag
	{
		CurveDragKind kind = CurveDragKind::None;
		Vans::VansTimelineId trackId;
		Vans::VansTimelineId sectionId;
		Vans::VansTimelineId keyId;
		std::size_t channelIndex = 0;
		Vans::VansTimelineTick initialTick = 0;
		double initialValue = 0.0;
		double initialArriveTangent = 0.0;
		double initialLeaveTangent = 0.0;
		double initialArriveWeight = 0.0;
		double initialLeaveWeight = 0.0;
	};
	struct MarqueeSelection
	{
		bool active = false;
		bool additive = false;
		float originX = 0.0f;
		float originY = 0.0f;
		float currentX = 0.0f;
		float currentY = 0.0f;
	};
	struct CurveBuffer
	{
		Vans::VansTimelineId trackId;
		Vans::VansTimelineId sectionId;
		Vans::VansTimelineId channelId;
		std::size_t channelIndex = 0;
		std::vector<Vans::VansTimelineKey> keys;
	};

	void DrawToolbar();
	void DrawOutliner();
	void DrawCanvas();
	void DrawInspector();
	void DrawCurveAndDopeSheet();
	void DrawStatusBar();
	void HandleShortcuts();
	void DrawAddTrackMenu();
	bool DrawSerializedValue(const std::string& label, Vans::VansSerializedValue& value,
		int depth = 0, Vans::VansSerializedValue* parent = nullptr);
	void ApplySerializedEdit(Vans::VansSerializedValue root);
	void CommitPendingInteraction();
	void EnsurePreview();
	void RefreshPreview();
	void SeekPreview(Vans::VansTimelineTick tick);
	void SetError(std::string message);
	void Select(Selection selection, bool additive = false);
	void SelectKeyRange(Selection selection);
	bool IsSelected(const Vans::VansTimelineId& id) const;
	void DeleteSelection();
	void SplitSelection();
	void CopySelection();
	void PasteSelection();
	void DuplicateSelection();
	void AddMarkerAtPlayhead();
	void FrameSelection();
	void FrameAll();
	void SetPlaybackBoundary(bool start);
	void SetSelectionBoundary(bool start);
	void BeginRenameSelection();
	void DrawRenamePopup();
	Vans::VansTimelineAsset BuildPreviewAsset() const;
	void LoadUserState();
	void SaveUserState();
	void UpdateRecovery();

	Vans::VansTimelineTrack* SelectedTrack();
	Vans::VansTimelineSection* SelectedSection();
	Vans::VansTimelineChannel* SelectedChannel();
	Vans::VansTimelineKey* SelectedKey();
	Vans::VansTimelineMarker* SelectedMarker();
	Vans::VansTimelineTick AdjacentKeyTick(int direction) const;
	Vans::VansTimelineTick SnapTick(Vans::VansTimelineTick tick) const;
	std::string FormatTick(Vans::VansTimelineTick tick) const;
	double TickToX(Vans::VansTimelineTick tick, double originX) const;
	Vans::VansTimelineTick XToTick(double x, double originX) const;

	bool m_IsOpen = false;
	bool m_CloseRequested = false;
	std::string m_Path;
	std::string m_InstanceOwnerGuid;
	std::string m_LastError;
	Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
	Vans::VansTimelineEditService m_Edit;
	Vans::VansTimelineCommandMap m_CommandMap;
	Vans::VansTimelinePreviewSession m_Preview;
	Selection m_Selection;
	Vans::VansTimelineTick m_Playhead = 0;
	Vans::VansTimelineTick m_ViewStart = 0;
	std::optional<Vans::VansTimelineTick> m_SelectionRangeStart;
	std::optional<Vans::VansTimelineTick> m_SelectionRangeEnd;
	double m_PixelsPerTick = 0.02;
	float m_LastCanvasWidth = 800.0f;
	float m_TrackScroll = 0.0f;
	float m_RowHeight = 30.0f;
	float m_CurveHeight = 190.0f;
	double m_CurveValueZoom = 1.0;
	double m_CurveValuePan = 0.0;
	double m_KeyTimeScale = 1.0;
	double m_KeyValueScale = 1.0;
	Vans::VansTimelineInterpolation m_BatchInterpolation = Vans::VansTimelineInterpolation::Auto;
	Vans::VansTimelineTangentMode m_BatchTangentMode = Vans::VansTimelineTangentMode::Unified;
	bool m_ShowCurves = true;
	bool m_ShowWaveforms = true;
	bool m_ShowThumbnails = true;
	bool m_SnapEnabled = true;
	bool m_SnapFrames = true;
	bool m_SnapKeys = true;
	bool m_SnapMarkers = true;
	bool m_SnapSections = true;
	bool m_SnapRanges = true;
	bool m_SnapPlayhead = true;
	bool m_PreviewSafeEvents = false;
	bool m_IncludeSubTimelines = false;
	int m_PlaybackDirection = 1;
	bool m_LoopPlaybackRange = false;
	AutoKeyMode m_AutoKeyMode = AutoKeyMode::Off;
	TimeDisplayMode m_TimeDisplayMode = TimeDisplayMode::Frames;
	mutable Vans::VansTimelineTick m_LastSnapTarget = 0;
	mutable bool m_HasSnapTarget = false;
	bool m_InteractionPending = false;
	bool m_CommandSurfaceFocused = false;
	bool m_OpenRenamePopup = false;
	char m_RenameBuffer[256]{};
	SectionEditMode m_SectionEditMode = SectionEditMode::Move;
	CanvasDrag m_CanvasDrag;
	CurveDrag m_CurveDrag;
	MarqueeSelection m_CanvasMarquee;
	MarqueeSelection m_CurveMarquee;
	std::unordered_set<Vans::VansTimelineId> m_SelectedIds;
	std::optional<Vans::VansTimelineSection> m_SectionClipboard;
	std::optional<CurveBuffer> m_CurveBuffer;
	bool m_ShowCurveComparison = true;
	std::optional<Vans::VansTimelineAsset> m_RecoveryAsset;
	Vans::VansTimelineTrackType m_ClipboardTrackType = Vans::VansTimelineTrackType::Transform;
	bool m_ShowRecoveryPrompt = false;
	std::uint64_t m_LastRecoveryStateId = 0;
	std::chrono::steady_clock::time_point m_LastRecoveryWrite{};
	std::unordered_set<Vans::VansTimelineId> m_SessionMutedTracks;
	std::unordered_set<Vans::VansTimelineId> m_SessionSoloTracks;
	std::unordered_set<Vans::VansTimelineId> m_HiddenTracks;
	std::unordered_set<Vans::VansTimelineId> m_PinnedTracks;
	char m_Search[128]{};
};
}
