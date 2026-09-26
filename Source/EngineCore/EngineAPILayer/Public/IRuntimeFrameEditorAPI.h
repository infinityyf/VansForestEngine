#pragma once

namespace Vans::EditorAPI
{
	class IRuntimeFrameEditorAPI
	{
	public:
		virtual ~IRuntimeFrameEditorAPI() = default;
		virtual void UpdateRuntimeNonCameraScripts() = 0;
		virtual void AdvanceCameraRuntime(double deltaSeconds) = 0;
		virtual void UpdateRuntimeActionsEarly(double deltaSeconds) = 0;
		virtual void UpdateRuntimeAI(double deltaSeconds) = 0;
		virtual void RunRuntimeActionLateContinuation() = 0;
		virtual void UpdateRuntimeTimelinesPostScript(double deltaSeconds) = 0;
		virtual void BeginRuntimeCameraControlFrame() = 0;
		virtual void UpdateRuntimeCameraScripts() = 0;
		virtual void CaptureRuntimeCameraControlBase() = 0;
		virtual void UpdateRuntimeTimelinesCamera(double deltaSeconds) = 0;
		virtual void UpdateTimelinePreviewsPostScript(double deltaSeconds) = 0;
		virtual void UpdateTimelinePreviewsCamera(double deltaSeconds) = 0;
		virtual void ResolveRuntimeCameraControlFrame() = 0;
	};
}
