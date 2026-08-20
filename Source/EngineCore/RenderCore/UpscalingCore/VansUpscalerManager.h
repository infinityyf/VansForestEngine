#pragma once

#include "VansUpscalerHistoryState.h"

#include <string>

namespace VansGraphics
{
	struct VansUpscalerCapabilitySet
	{
		VansUpscalerCapabilities off;
		VansUpscalerCapabilities fsr;
		VansUpscalerCapabilities dlss;

		const VansUpscalerCapabilities& For(VansUpscalerBackend backend) const;
	};

	struct VansUpscalerSelectionChange
	{
		bool accepted = false;
		bool desiredChanged = false;
		bool effectiveBackendChanged = false;
		bool effectiveQualityChanged = false;
		bool backendSettingsChanged = false;
		bool fallbackActive = false;
		std::string error;

		bool RequiresContextRebuild() const
		{
			return effectiveBackendChanged || effectiveQualityChanged;
		}
	};

	class VansUpscalerManager
	{
	public:
		VansUpscalerSelectionChange RequestConfig(
			const VansUpscalerConfig& requested,
			const VansUpscalerCapabilitySet& capabilities);

		void ActivateRuntimeFallback(
			VansUpscalerBackend backend,
			VansUpscaleQualityMode quality,
			VansUpscalerFallbackReason reason,
			std::string message);

		void ClearFallback();

		const VansUpscalerConfig& GetDesiredConfig() const { return m_Desired; }
		const VansUpscalerConfig& GetEffectiveConfig() const { return m_Effective; }
		VansUpscalerFallbackReason GetFallbackReason() const { return m_FallbackReason; }
		const std::string& GetFallbackMessage() const { return m_FallbackMessage; }
		VansUpscalerHistoryState& GetHistory() { return m_History; }
		const VansUpscalerHistoryState& GetHistory() const { return m_History; }

	private:
		static bool ValidateConfig(const VansUpscalerConfig& config, std::string& error);
		static bool IsCapabilityUsable(
			const VansUpscalerCapabilities& capabilities,
			VansUpscaleQualityMode quality);

		// The runtime starts without an active temporal backend. Project defaults
		// remain owned by VansUpscalerConfig and become active only through
		// RequestConfig after the project settings have been loaded.
		VansUpscalerConfig m_Desired{
			VansUpscalerBackend::Off,
			VansUpscaleQualityMode::NativeAA };
		VansUpscalerConfig m_Effective{
			VansUpscalerBackend::Off,
			VansUpscaleQualityMode::NativeAA };
		VansUpscalerFallbackReason m_FallbackReason =
			VansUpscalerFallbackReason::None;
		std::string m_FallbackMessage;
		VansUpscalerHistoryState m_History;
	};
}
