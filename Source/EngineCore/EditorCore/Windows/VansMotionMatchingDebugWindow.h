#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansMotionMatchingDebugWindow final : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

		static bool SceneOverlayEnabled() { return s_SceneOverlayEnabled; }
		static bool ShowHistory() { return s_ShowHistory; }
		static bool ShowFutureTrajectory() { return s_ShowFutureTrajectory; }
		static bool ShowFutureVelocities() { return s_ShowFutureVelocities; }
		static bool ShowActualVelocity() { return s_ShowActualVelocity; }
		static bool ShowPlannedVelocity() { return s_ShowPlannedVelocity; }
		static bool ShowDesiredVelocity() { return s_ShowDesiredVelocity; }
		static bool ShowActiveClipVelocity() { return s_ShowActiveClipVelocity; }
		static bool ShowSelectedCandidateVelocity() { return s_ShowSelectedCandidateVelocity; }
		static bool ShowAppliedRootMotionVelocity() { return s_ShowAppliedRootMotionVelocity; }
		static bool ShowPivot() { return s_ShowPivot; }
		static bool ShowLabels() { return s_ShowLabels; }

	private:
		static bool s_SceneOverlayEnabled;
		static bool s_ShowHistory;
		static bool s_ShowFutureTrajectory;
		static bool s_ShowFutureVelocities;
		static bool s_ShowActualVelocity;
		static bool s_ShowPlannedVelocity;
		static bool s_ShowDesiredVelocity;
		static bool s_ShowActiveClipVelocity;
		static bool s_ShowSelectedCandidateVelocity;
		static bool s_ShowAppliedRootMotionVelocity;
		static bool s_ShowPivot;
		static bool s_ShowLabels;
	};
}
