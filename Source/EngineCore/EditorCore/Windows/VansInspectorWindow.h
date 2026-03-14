#pragma once
#include "VansBaseWindowComponent.h"
#include "../../AnimationCore/VansAnimationTypes.h"
#include "../../AnimationCore/VansAnimationClip.h"

#include <string>
#include <vector>
#include <filesystem>
namespace VansGraphics
{
	enum InspectResourceType
	{
		None = 0,
		TextAsset,
		TextureAsset,
		ModelAsset,
		AnimationClipAsset,
	};
	class VansInspectorWindow : public VansBaseWindowComponent
	{
	private:

		void ShowWindow(VansVKDevice& device) override;

	private:

		void ShowTextAsset();

		void ShowTextureAsset(VansVKDevice& device);

		void ShowModelTextureAsset(VansVKDevice& device);

		void ShowAnimationClipAsset();

		// ── Cached .vclip data for the inspector ──
		struct CachedVClipData
		{
			std::filesystem::path loadedPath;
			bool                  valid = false;
			VansAnimationClip     clip;
			Skeleton              skeleton;
			float                 scrubTime   = 0.0f;
			int                   selectedBone = -1;
		};
		CachedVClipData m_VClipCache;
	};
}