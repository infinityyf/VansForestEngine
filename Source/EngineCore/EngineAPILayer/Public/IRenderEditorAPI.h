#pragma once

#include "EngineDTOs.h"

#include <cstdint>
#include <vector>

namespace Vans::EditorAPI
{
	class IRenderEditorAPI
	{
	public:
		virtual ~IRenderEditorAPI() = default;
		virtual RenderTexturePreview GetViewportPreview(ViewportId id) const = 0;
		virtual UpscalerSettingsSnapshot GetUpscalerSettings() const = 0;
		virtual std::vector<UpscalerCapabilitiesSnapshot> GetUpscalerCapabilities() const = 0;
		virtual ApplyUpscalerSettingsResult ApplyUpscalerSettings(
			const UpscalerSettingsSnapshot& settings) = 0;
		virtual CommandRecordingSettingsSnapshot GetCommandRecordingSettings() const = 0;
        virtual ProjectRenderPassSettingsSnapshot GetProjectRenderPassSettings() const = 0;
        virtual ApplyProjectRenderPassSettingsResult ApplyProjectRenderPassSettings(const std::vector<std::string>& assetGuids) = 0;
		virtual ApplyCommandRecordingSettingsResult SetCommandRecordingSettings(
			const CommandRecordingSettingsSnapshot& settings) = 0;
		virtual std::vector<RenderTexturePreview> QueryRenderTexturePreviews(
			RenderTextureFilter filter) const = 0;
		virtual std::vector<RenderTextureSourceInfo> QueryAvailableRenderTextures(RenderTextureFilter filter) const = 0;
		virtual RenderTexturePreviewRegistrationResult RegisterRenderTexturePreview(const std::string& resourceKey, const RenderTexturePreviewViewSpec& view) = 0;
		virtual void UnregisterRenderTexturePreview(std::uint64_t registration) = 0;
		virtual std::vector<RenderTexturePreview> QueryRegisteredRenderTexturePreviews() const = 0;
		virtual std::uint32_t GetAmbientSkyCacheDebugMode() const = 0;
		virtual void SetAmbientSkyCacheDebugMode(std::uint32_t mode) = 0;
		virtual void RequestPunctualShadowDebugPreview() = 0;
		virtual PunctualShadowDebugSnapshot GetPunctualShadowDebugSnapshot() const = 0;
		virtual void ApplyPunctualScreenSpaceShadowSettings(
			const PunctualScreenSpaceShadowSettingsSnapshot& settings) = 0;
		virtual RenderBackendDiagnostics GetRenderBackendDiagnostics(
			bool includeRenderGraphSummary = false) const = 0;
		virtual PipelineRegistryStatsSnapshot GetPipelineRegistryStats() const = 0;
		virtual RenderDocStatusSnapshot GetRenderDocStatus() const = 0;
		virtual void SetRenderDocAPIValidationEnabled(bool enabled) = 0;
		virtual void SetRenderDocReferenceAllResources(bool enabled) = 0;
		virtual void CaptureNextRenderDocFrame() = 0;
		virtual void OpenRenderDocUI() = 0;
		virtual MainCameraHiZCullDebugSnapshot GetMainCameraHiZCullDebugSnapshot() const = 0;
	};
}
