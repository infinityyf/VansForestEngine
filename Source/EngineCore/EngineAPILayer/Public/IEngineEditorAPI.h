#pragma once

#include "EngineDTOs.h"
#include "IAIEditorAPI.h"
#include "IAnimationEditorAPI.h"
#include "IAnimationPreviewEditorAPI.h"
#include "IAssetAuthoringEditorAPI.h"
#include "IAssetEditorAPI.h"
#include "IAudioEditorAPI.h"
#include "IGAFEditorAPI.h"
#include "IGIEditorAPI.h"
#include "IMotionMatchingEditorAPI.h"
#include "IParticleEditorAPI.h"
#include "IPcgEditorAPI.h"
#include "IPlayModeEditorAPI.h"
#include "IProjectEditorAPI.h"
#include "IReflectionProbeEditorAPI.h"
#include "IRenderEditorAPI.h"
#include "IRuntimeCommandHistoryEditorAPI.h"
#include "IRuntimeFrameEditorAPI.h"
#include "IRuntimeSceneEditorAPI.h"
#include "IRuntimePhysicsEditorAPI.h"
#include "ISceneInteractionEditorAPI.h"
#include "ISceneSettingsEditorAPI.h"
#include "IShaderEditorAPI.h"
#include "IScriptLifecycleEditorAPI.h"
#include "ITerrainEditorAPI.h"
#include "ITimelineEditorAPI.h"
#include "IUIEditorAPI.h"
#include "IVehicleEditorAPI.h"
#include "IWaterEditorAPI.h"

#include <memory>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class IEngineEditorAPI : public IAIEditorAPI,
		public IAnimationEditorAPI,
		public IAnimationPreviewEditorAPI,
		public IAssetAuthoringEditorAPI,
		public IAssetEditorAPI,
		public IAudioEditorAPI,
		public IGAFEditorAPI,
		public IGIEditorAPI,
		public IMotionMatchingEditorAPI,
		public IParticleEditorAPI,
		public IPcgEditorAPI,
		public IPlayModeEditorAPI,
		public IProjectEditorAPI,
		public IReflectionProbeEditorAPI,
		public IRenderEditorAPI,
		public IRuntimeCommandHistoryEditorAPI,
		public IRuntimeFrameEditorAPI,
		public IRuntimePhysicsEditorAPI,
		public IRuntimeSceneEditorAPI,
		public ISceneInteractionEditorAPI,
		public ISceneSettingsEditorAPI,
		public IShaderEditorAPI,
		public IScriptLifecycleEditorAPI,
		public ITerrainEditorAPI,
		public ITimelineEditorAPI,
		public IUIEditorAPI,
		public IVehicleEditorAPI,
		public IWaterEditorAPI
	{
	public:
		~IEngineEditorAPI() override = default;
	};
}
