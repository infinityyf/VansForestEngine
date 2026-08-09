#include "VansTimelinePreviewSession.h"

#include "../../AssetCore/VansAssetGuid.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../../TimelineCore/VansTimelineSerialization.h"

#include <nlohmann/json.hpp>

namespace Vans
{
namespace
{
VansTimelinePreviewState ConvertState(EditorAPI::TimelinePreviewState state)
{
	switch (state)
	{
	case EditorAPI::TimelinePreviewState::Playing: return VansTimelinePreviewState::Playing;
	case EditorAPI::TimelinePreviewState::Paused: return VansTimelinePreviewState::Paused;
	case EditorAPI::TimelinePreviewState::Error: return VansTimelinePreviewState::Faulted;
	case EditorAPI::TimelinePreviewState::Stopped:
	case EditorAPI::TimelinePreviewState::Completed: return VansTimelinePreviewState::Ready;
	case EditorAPI::TimelinePreviewState::Detached: return VansTimelinePreviewState::Detached;
	}
	return VansTimelinePreviewState::Faulted;
}
}

VansTimelinePreviewSession::~VansTimelinePreviewSession()
{
	RestoreAndDetach();
}

std::string VansTimelinePreviewSession::MakeSessionId()
{
	return VansAssetGuid::New().ToString();
}

bool VansTimelinePreviewSession::Attach(
	EditorAPI::IEngineEditorAPI& editorAPI,
	const VansTimelineAsset& asset,
	std::string sourceAssetPath,
	std::string ownerEntityGuid,
	bool safeEvents,
	bool includeSubTimelines,
	int playbackDirection,
	bool loopPlaybackRange,
	std::string& error)
{
	RestoreAndDetach();
	m_EditorAPI = &editorAPI;
	m_SessionId = MakeSessionId();
	m_SourceAssetPath = std::move(sourceAssetPath);
	m_OwnerEntityGuid = std::move(ownerEntityGuid);
	m_SafeEvents = safeEvents;
	m_IncludeSubTimelines = includeSubTimelines;
	m_PlaybackDirection = playbackDirection < 0 ? -1 : 1;
	m_LoopPlaybackRange = loopPlaybackRange;
	return Start(asset, error);
}

bool VansTimelinePreviewSession::Start(const VansTimelineAsset& asset, std::string& error)
{
	error.clear();
	if (!m_EditorAPI)
	{
		error = "Timeline preview API is unavailable";
		m_State = VansTimelinePreviewState::Faulted;
		return false;
	}
	m_Asset = asset;
	EditorAPI::TimelinePreviewStartRequest request;
	request.previewId = m_SessionId;
	request.canonicalJson = VansTimelineSerialization::Encode(asset).dump();
	request.sourceAssetPath = m_SourceAssetPath;
	request.ownerEntityGuid = m_OwnerEntityGuid;
	request.safeEvents = m_SafeEvents;
	request.includeSubTimelines = m_IncludeSubTimelines;
	const EditorAPI::TimelinePreviewResult result = m_EditorAPI->StartTimelinePreview(request);
	if (!result.success)
	{
		m_LastError = error = result.message.empty() ? "Timeline preview could not start" : result.message;
		m_State = VansTimelinePreviewState::Faulted;
		return false;
	}
	m_CurrentTick = result.currentTick;
	m_ResolvedOwnerEntityGuid = result.ownerEntityGuid;
	m_State = VansTimelinePreviewState::Ready;
	m_LastError.clear();
	return ConfigurePlayback(m_PlaybackDirection, m_LoopPlaybackRange, error);
}

bool VansTimelinePreviewSession::ConfigurePlayback(
	int direction,
	bool loopPlaybackRange,
	std::string& error)
{
	error.clear();
	m_PlaybackDirection = direction < 0 ? -1 : 1;
	m_LoopPlaybackRange = loopPlaybackRange;
	if (!m_EditorAPI || m_SessionId.empty())
		return true;
	EditorAPI::TimelinePreviewPlaybackRequest request;
	request.previewId = m_SessionId;
	request.direction = m_PlaybackDirection;
	request.loopPlaybackRange = m_LoopPlaybackRange;
	const EditorAPI::TimelinePreviewResult result =
		m_EditorAPI->ConfigureTimelinePreviewPlayback(request);
	if (!result.success)
	{
		m_LastError = error = result.message.empty()
			? "Timeline preview playback settings were rejected" : result.message;
		m_State = VansTimelinePreviewState::Faulted;
		return false;
	}
	m_CurrentTick = result.currentTick;
	m_State = ConvertState(result.state);
	return true;
}

bool VansTimelinePreviewSession::Refresh(const VansTimelineAsset& asset, std::string& error)
{
	const VansTimelineTick tick = m_CurrentTick;
	const bool wasPlaying = m_State == VansTimelinePreviewState::Playing;
	if (m_EditorAPI && !m_SessionId.empty()) m_EditorAPI->StopTimelinePreview(m_SessionId);
	if (!Start(asset, error)) return false;
	if (!Seek(tick, error)) return false;
	return !wasPlaying || Play(error);
}

bool VansTimelinePreviewSession::Play(std::string& error)
{
	error.clear();
	if (!m_EditorAPI) { error = "Timeline preview is detached"; return false; }
	if (m_State == VansTimelinePreviewState::Detached || m_State == VansTimelinePreviewState::Faulted)
		if (!Start(m_Asset, error)) return false;
	const auto result = m_EditorAPI->PlayTimelinePreview(m_SessionId);
	if (!result.success) { m_LastError = error = result.message; m_State = VansTimelinePreviewState::Faulted; return false; }
	m_State = ConvertState(result.state);
	m_CurrentTick = result.currentTick;
	return true;
}

bool VansTimelinePreviewSession::Pause(std::string& error)
{
	error.clear();
	if (!m_EditorAPI) { error = "Timeline preview is detached"; return false; }
	const auto result = m_EditorAPI->PauseTimelinePreview(m_SessionId);
	if (!result.success) { m_LastError = error = result.message; return false; }
	m_State = ConvertState(result.state);
	m_CurrentTick = result.currentTick;
	return true;
}

bool VansTimelinePreviewSession::Seek(VansTimelineTick tick, std::string& error)
{
	error.clear();
	if (!m_EditorAPI) { error = "Timeline preview is detached"; return false; }
	m_State = VansTimelinePreviewState::Scrubbing;
	const auto result = m_EditorAPI->SeekTimelinePreview(m_SessionId, tick, m_SafeEvents);
	if (!result.success) { m_LastError = error = result.message; m_State = VansTimelinePreviewState::Faulted; return false; }
	m_CurrentTick = result.currentTick;
	m_State = result.state == EditorAPI::TimelinePreviewState::Playing
		? VansTimelinePreviewState::Playing : VansTimelinePreviewState::Paused;
	return true;
}

void VansTimelinePreviewSession::RestoreAndDetach()
{
	if (m_EditorAPI && !m_SessionId.empty())
	{
		m_State = VansTimelinePreviewState::Restoring;
		m_EditorAPI->StopTimelinePreview(m_SessionId);
	}
	m_EditorAPI = nullptr;
	m_SessionId.clear();
	m_SourceAssetPath.clear();
	m_OwnerEntityGuid.clear();
	m_ResolvedOwnerEntityGuid.clear();
	m_State = VansTimelinePreviewState::Detached;
	m_CurrentTick = 0;
}

void VansTimelinePreviewSession::Poll()
{
	if (!m_EditorAPI || m_SessionId.empty()) return;
	const auto result = m_EditorAPI->GetTimelinePreview(m_SessionId);
	if (!result.success) return;
	m_State = ConvertState(result.state);
	m_CurrentTick = result.currentTick;
}
}
