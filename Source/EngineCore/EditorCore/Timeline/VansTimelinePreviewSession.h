#pragma once

#include "../../TimelineCore/VansTimelineAsset.h"

#include <cstdint>
#include <string>

namespace Vans::EditorAPI { class IEngineEditorAPI; }

namespace Vans
{
enum class VansTimelinePreviewState
{
	Detached,
	Ready,
	Playing,
	Paused,
	Scrubbing,
	Restoring,
	Faulted
};

class VansTimelinePreviewSession
{
public:
	~VansTimelinePreviewSession();

	bool Attach(EditorAPI::IEngineEditorAPI& editorAPI, const VansTimelineAsset& asset,
		std::string sourceAssetPath, std::string ownerEntityGuid,
		bool safeEvents, bool includeSubTimelines,
		int playbackDirection, bool loopPlaybackRange, std::string& error);
	bool Refresh(const VansTimelineAsset& asset, std::string& error);
	bool ConfigurePlayback(int direction, bool loopPlaybackRange, std::string& error);
	bool Play(std::string& error);
	bool Pause(std::string& error);
	bool Seek(VansTimelineTick tick, std::string& error);
	void RestoreAndDetach();
	void Poll();

	VansTimelinePreviewState State() const { return m_State; }
	VansTimelineTick CurrentTick() const { return m_CurrentTick; }
	const std::string& LastError() const { return m_LastError; }
	const std::string& ResolvedOwnerEntityGuid() const { return m_ResolvedOwnerEntityGuid; }
	bool SafeEvents() const { return m_SafeEvents; }
	void SetSafeEvents(bool enabled) { m_SafeEvents = enabled; }

private:
	bool Start(const VansTimelineAsset& asset, std::string& error);
	static std::string MakeSessionId();

	EditorAPI::IEngineEditorAPI* m_EditorAPI = nullptr;
	std::string m_SessionId;
	std::string m_SourceAssetPath;
	std::string m_OwnerEntityGuid;
	std::string m_ResolvedOwnerEntityGuid;
	VansTimelineAsset m_Asset;
	VansTimelinePreviewState m_State = VansTimelinePreviewState::Detached;
	VansTimelineTick m_CurrentTick = 0;
	bool m_SafeEvents = false;
	bool m_IncludeSubTimelines = false;
	int m_PlaybackDirection = 1;
	bool m_LoopPlaybackRange = false;
	std::string m_LastError;
};
}
