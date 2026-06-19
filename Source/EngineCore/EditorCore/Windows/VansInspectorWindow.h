#pragma once
#include "VansBaseWindowComponent.h"
#include "../../AnimationCore/VansAnimationTypes.h"
#include "../../AnimationCore/VansAnimationClip.h"

#include <string>
#include <vector>
#include <filesystem>
#include <array>
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
	public:
		~VansInspectorWindow();
	private:

		void ShowWindow(VansVKDevice& device) override;

	private:

		void ShowTextAsset();

		void ShowTextureAsset(VansVKDevice& device);

		void ShowModelTextureAsset(VansVKDevice& device);

		void ShowAnimationClipAsset();
		void ShowReflectionProbeEditor(VansVKDevice& device);
		void ResetProbePreview();

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
		std::array<VkImageView, 6> m_ProbePreviewViews{};
		std::array<VkDescriptorSet, 6> m_ProbePreviewSets{};
		VkDevice m_ProbePreviewDevice = VK_NULL_HANDLE;
		int m_ProbePreviewIndex = -1;
		int m_ProbePreviewMip = -1;
	};
}
