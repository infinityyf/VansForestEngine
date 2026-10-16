#pragma once

#include "EngineDTOs.h"
#include "../../RenderCore/GICore/VansGIFrameCost.h"
#include "../../RenderCore/GICore/VansGIFrameWork.h"
#include "../../RenderCore/GICore/VansGIMemorySnapshot.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Vans::EditorAPI
{
	class IGIEditorAPI
	{
	public:
		virtual ~IGIEditorAPI() = default;
		virtual GIInspectorSettingsSnapshot GetGISettings() const = 0;
		virtual VansGraphics::GIFrameCostSnapshot GetGIFrameCost() const = 0;
		virtual VansGraphics::VansGIFrameWork GetGIFrameWork() const = 0;
		virtual VansGraphics::VansGIMemorySnapshot GetGIMemorySnapshot() const = 0;
		virtual bool ApplyGISettings(const GIInspectorSettingsSnapshot& settings) = 0;
		virtual void SaveGIConfiguration() = 0;
		virtual void SetGIProbeVisualization(
			bool showPositions, bool showVolume, std::uint32_t stride) = 0;
		virtual std::shared_ptr<const GIProbeDebugSnapshot> GetGIProbeDebugSnapshot() const = 0;
		virtual std::vector<RenderTexturePreview> RequestGIRTPreviews(
			std::uint32_t zSlice,
			std::uint32_t rayIndex,
			float exposure,
			float positionScale) = 0;
	};
}
