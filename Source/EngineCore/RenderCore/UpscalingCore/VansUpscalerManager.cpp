#include "VansUpscalerManager.h"
#include "VansUpscaleResolutionPolicy.h"

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

	bool VansUpscalerManager::IsCapabilityUsable(
		const VansUpscalerCapabilities& capabilities,
		VansUpscaleQualityMode quality)
	{
		return capabilities.compiledIn &&
			capabilities.runtimeAvailable &&
			capabilities.deviceSupported &&
			capabilities.Supports(quality);
	}

	VansUpscalerFallbackReason VansUpscalerManager::ClassifyCapabilityFailure(
		const VansUpscalerCapabilities& capabilities,
		VansUpscaleQualityMode quality)
	{
		if (!capabilities.compiledIn)
			return VansUpscalerFallbackReason::NotCompiled;
		if (capabilities.unavailableReasonCode !=
			VansUpscalerFallbackReason::None)
		{
			return capabilities.unavailableReasonCode;
		}
		if (!capabilities.runtimeAvailable)
			return VansUpscalerFallbackReason::RuntimeUnavailable;
		if (!capabilities.deviceSupported)
			return VansUpscalerFallbackReason::UnsupportedDevice;
		if (!capabilities.Supports(quality))
			return VansUpscalerFallbackReason::UnsupportedQuality;
		return VansUpscalerFallbackReason::None;
	}

	VansUpscalerSelectionChange VansUpscalerManager::RequestConfig(
		const VansUpscalerConfig& requested,
		const VansUpscalerCapabilitySet& capabilities)
	{
		VansUpscalerSelectionChange change;
		if (!VansUpscaleResolutionPolicy::ValidateConfig(requested, change.error))
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
				fallbackReason = ClassifyCapabilityFailure(
					requestedCapabilities, requested.quality);
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
				fallbackReason = ClassifyCapabilityFailure(
					requestedCapabilities, requested.quality);
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

}
