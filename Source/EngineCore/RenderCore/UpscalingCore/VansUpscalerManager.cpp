#include "VansUpscalerManager.h"

#include <cmath>
#include <utility>

namespace VansGraphics
{
	const VansUpscalerCapabilities& VansUpscalerCapabilitySet::For(
		VansUpscalerBackend backend) const
	{
		switch (backend)
		{
		case VansUpscalerBackend::FSR: return fsr;
		case VansUpscalerBackend::DLSS: return dlss;
		case VansUpscalerBackend::Off:
		default: return off;
		}
	}

	bool VansUpscalerManager::ValidateConfig(
		const VansUpscalerConfig& config,
		std::string& error)
	{
		if (!std::isfinite(config.fsrSharpness) ||
			config.fsrSharpness < 0.0f ||
			config.fsrSharpness > 1.0f)
		{
			error = "FSR sharpness must be finite and in [0, 1]";
			return false;
		}
		if (config.backend == VansUpscalerBackend::Off &&
			config.quality != VansUpscaleQualityMode::NativeAA)
		{
			error = "Off backend requires NativeAA quality";
			return false;
		}
		error.clear();
		return true;
	}

	bool VansUpscalerManager::IsCapabilityUsable(
		const VansUpscalerCapabilities& capabilities,
		VansUpscaleQualityMode quality)
	{
		return capabilities.compiledIn &&
			capabilities.runtimeAvailable &&
			capabilities.deviceSupported &&
			capabilities.Supports(quality);
	}

	VansUpscalerSelectionChange VansUpscalerManager::RequestConfig(
		const VansUpscalerConfig& requested,
		const VansUpscalerCapabilitySet& capabilities)
	{
		VansUpscalerSelectionChange change;
		if (!ValidateConfig(requested, change.error))
			return change;

		VansUpscalerConfig effective = requested;
		VansUpscalerFallbackReason fallbackReason = VansUpscalerFallbackReason::None;
		std::string fallbackMessage;
		const VansUpscalerCapabilities& requestedCapabilities =
			capabilities.For(requested.backend);
		if (!IsCapabilityUsable(requestedCapabilities, requested.quality))
		{
			if (requested.backend == VansUpscalerBackend::DLSS)
			{
				if (IsCapabilityUsable(capabilities.fsr, requested.quality))
				effective.backend = VansUpscalerBackend::FSR;
				else if (IsCapabilityUsable(
					capabilities.fsr,
					VansUpscaleQualityMode::Quality))
				{
					effective.backend = VansUpscalerBackend::FSR;
					effective.quality = VansUpscaleQualityMode::Quality;
				}
				else
				{
					effective.backend = VansUpscalerBackend::Off;
					effective.quality = VansUpscaleQualityMode::NativeAA;
				}
				if (!requestedCapabilities.compiledIn)
					fallbackReason = VansUpscalerFallbackReason::NotCompiled;
				else if (!requestedCapabilities.runtimeAvailable)
					fallbackReason = VansUpscalerFallbackReason::RuntimeUnavailable;
				else
					fallbackReason = VansUpscalerFallbackReason::UnsupportedDevice;
				fallbackMessage = requestedCapabilities.unavailableReason.empty()
					? "DLSS is unavailable; using a supported fallback"
					: requestedCapabilities.unavailableReason;
			}
			else if (requested.backend == VansUpscalerBackend::FSR)
			{
				if (IsCapabilityUsable(
					capabilities.fsr,
					VansUpscaleQualityMode::Quality))
				{
					effective.quality = VansUpscaleQualityMode::Quality;
				}
				else
				{
					effective.backend = VansUpscalerBackend::Off;
					effective.quality = VansUpscaleQualityMode::NativeAA;
				}
				fallbackReason = VansUpscalerFallbackReason::RuntimeUnavailable;
				fallbackMessage = requestedCapabilities.unavailableReason.empty()
					? "FSR is unavailable; using native output"
					: requestedCapabilities.unavailableReason;
			}
			else
			{
				change.error = requestedCapabilities.unavailableReason.empty()
					? "Off backend is unavailable"
					: requestedCapabilities.unavailableReason;
				return change;
			}
		}

		change.desiredChanged = requested != m_Desired;
		change.effectiveBackendChanged = effective.backend != m_Effective.backend;
		change.effectiveQualityChanged = effective.quality != m_Effective.quality;
		change.backendSettingsChanged =
			effective.fsrSharpness != m_Effective.fsrSharpness ||
			effective.fsrDebugView != m_Effective.fsrDebugView;
		change.fallbackActive = fallbackReason != VansUpscalerFallbackReason::None;
		change.accepted = true;

		m_Desired = requested;
		m_Effective = effective;
		m_FallbackReason = fallbackReason;
		m_FallbackMessage = std::move(fallbackMessage);
		if (change.effectiveBackendChanged)
			m_History.RequestReset(VansUpscalerResetReason::BackendChange);
		if (change.effectiveQualityChanged)
			m_History.RequestReset(VansUpscalerResetReason::QualityChange);
		return change;
	}

	void VansUpscalerManager::ActivateRuntimeFallback(
		VansUpscalerBackend backend,
		VansUpscaleQualityMode quality,
		VansUpscalerFallbackReason reason,
		std::string message)
	{
		if (m_Effective.backend != backend)
			m_History.RequestReset(VansUpscalerResetReason::BackendChange);
		if (m_Effective.quality != quality)
			m_History.RequestReset(VansUpscalerResetReason::QualityChange);
		m_Effective.backend = backend;
		m_Effective.quality = quality;
		m_FallbackReason = reason;
		m_FallbackMessage = std::move(message);
	}

	void VansUpscalerManager::ClearFallback()
	{
		m_FallbackReason = VansUpscalerFallbackReason::None;
		m_FallbackMessage.clear();
	}
}
