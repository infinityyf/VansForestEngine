#pragma once

#include "EngineDTOs.h"
#include "PcgSplineDTOs.h"

#include <cstdint>
#include <string>

namespace Vans::EditorAPI
{
	class IPcgEditorAPI
	{
	public:
		virtual ~IPcgEditorAPI() = default;
		virtual PcgEditorOperationResult BuildPcgPlantLods(const std::string& guid) = 0;
		virtual PcgSplineSnapshot GetPcgSplineSnapshot() = 0;
		virtual PcgEditorOperationResult CreatePcgSplineAsset(const std::string& name) = 0;
		virtual PcgEditorOperationResult BindPcgSplineAsset(const std::string& guid) = 0;
		virtual PcgEditorOperationResult SelectPcgSpline(
			const std::string& spline, const std::string& point, bool toolEnabled) = 0;
		virtual PcgEditorOperationResult ApplyPcgSplineEdit(const PcgSplineEditRequest& request) = 0;
		virtual PcgEditorOperationResult ExecutePcgSplineCommand(const PcgSplineCommandRequest& request) = 0;
		virtual PcgEditorOperationResult AppendPcgSplinePoint(const Ray& ray) = 0;
		virtual PcgEditorSnapshot GetPcgEditorSnapshot() const = 0;
		virtual PcgLayerCreateResult CreatePcgLayer(const PcgLayerCreateRequest& request) = 0;
		virtual PcgEditorOperationResult RemovePcgLayer(const PcgBrushTarget& target) = 0;
		virtual PcgEditorOperationResult BindPcgRecipeToScene(const std::string& guid) = 0;
		virtual PcgPlantConfiguration GetPcgPlantConfiguration(const std::string& guid) = 0;
		virtual PcgLayerConfiguration GetPcgLayerConfiguration(const PcgBrushTarget& target) = 0;
		virtual PcgEditorOperationResult ApplyPcgPlantConfiguration(
			const PcgPlantConfiguration& configuration) = 0;
		virtual PcgEditorOperationResult ApplyPcgLayerConfiguration(
			const PcgBrushTarget& target, const PcgLayerConfiguration& configuration) = 0;
		virtual PcgEditorOperationResult EditPcgConfiguration(
			const std::string& guid, PcgConfigurationAction action) = 0;
		virtual PcgMaskPreviewSnapshot GetPcgMaskPreview(const std::string& maskGuid) const = 0;
		virtual PcgBrushSnapshot GetPcgBrushSnapshot() const = 0;
		virtual PcgEditorOperationResult SelectPcgBrushTarget(
			const PcgBrushTarget& target, bool enabled) = 0;
		virtual PcgEditorOperationResult ConfigurePcgBrush(const PcgBrushSettings& settings) = 0;
		virtual PcgBrushResult ApplyPcgBrushInput(const PcgBrushInput& input) = 0;
		virtual PcgEditorOperationResult EditPcgMaskDocument(PcgMaskDocumentAction action) = 0;
		virtual PcgEditorOperationResult EditPcgMaskData(const PcgMaskDataRequest& request) = 0;
		virtual PcgEditorOperationResult CreatePcgExclusionMask(const PcgBrushTarget& target) = 0;
		virtual PcgInstanceSnapshot GetPcgInstances(
			const PcgBrushTarget& target, std::uint64_t offset) = 0;
		virtual PcgEditorOperationResult EditPcgInstance(const PcgInstanceEditRequest& request) = 0;
	};
}
