#pragma once

#include "EngineDTOs.h"

#include <cstdint>
#include <string>

namespace Vans::EditorAPI
{
	class ITimelineEditorAPI
	{
	public:
		virtual ~ITimelineEditorAPI() = default;
		virtual TimelinePreviewResult StartTimelinePreview(
			const TimelinePreviewStartRequest& request) = 0;
		virtual TimelinePreviewResult ConfigureTimelinePreviewPlayback(
			const TimelinePreviewPlaybackRequest& request) = 0;
		virtual TimelinePreviewResult PlayTimelinePreview(const std::string& previewId) = 0;
		virtual TimelinePreviewResult PauseTimelinePreview(const std::string& previewId) = 0;
		virtual TimelinePreviewResult SeekTimelinePreview(
			const std::string& previewId, std::int64_t tick, bool safeEdges) = 0;
		virtual TimelinePreviewResult StopTimelinePreview(const std::string& previewId) = 0;
		virtual TimelinePreviewResult GetTimelinePreview(const std::string& previewId) const = 0;
	};
}
