#include "VansDLSS.h"

#include "../../Util/VansLog.h"

namespace VansGraphics
{
	VansStreamlineDLSSMode VansDLSS::ToStreamlineMode(
		VansUpscaleQualityMode quality)
	{
		switch (quality)
		{
		case VansUpscaleQualityMode::NativeAA: return VansStreamlineDLSSMode::DLAA;
		case VansUpscaleQualityMode::Quality: return VansStreamlineDLSSMode::Quality;
		case VansUpscaleQualityMode::Balanced: return VansStreamlineDLSSMode::Balanced;
		case VansUpscaleQualityMode::Performance: return VansStreamlineDLSSMode::Performance;
		case VansUpscaleQualityMode::UltraPerformance:
			return VansStreamlineDLSSMode::UltraPerformance;
		default: return VansStreamlineDLSSMode::Quality;
		}
	}

	bool VansDLSS::QueryRecommendedRenderExtent(
		VansUpscaleQualityMode quality,
		std::uint32_t outputWidth,
		std::uint32_t outputHeight,
		VansExtent2D& renderExtent) const
	{
		VansStreamlineOptimalSettings settings;
		if (!VansStreamlineRuntime::Get().QueryOptimalSettings(
			ToStreamlineMode(quality), outputWidth, outputHeight, settings))
		{
			return false;
		}
		renderExtent = { settings.renderWidth, settings.renderHeight };
		return renderExtent.IsValid();
	}

	bool VansDLSS::InitializeContext(
		VansUpscaleQualityMode quality,
		std::uint32_t outputWidth,
		std::uint32_t outputHeight,
		bool useExternalExposure)
	{
		Cleanup();
		m_Diagnostics = {};
		if (!VansStreamlineRuntime::Get().IsDLSSAvailable())
		{
			m_Diagnostics.lastError =
				VansStreamlineRuntime::Get().GetUnavailableReason();
			return false;
		}
		if (!VansStreamlineRuntime::Get().ConfigureDLSS(
			ToStreamlineMode(quality), outputWidth, outputHeight,
			useExternalExposure))
		{
			m_Diagnostics.lastCreateCode = 1;
			m_Diagnostics.lastError = "slDLSSSetOptions failed";
			return false;
		}
		m_Diagnostics.contextReady = true;
		VANS_LOG(
			"[DLSS] Context ready: output=" << outputWidth << "x" << outputHeight <<
			" quality=" << ToString(quality) <<
			" externalExposure=" << (useExternalExposure ? "true" : "false"));
		return true;
	}

	void VansDLSS::Cleanup()
	{
		if (m_Diagnostics.contextReady)
			VansStreamlineRuntime::Get().ReleaseDLSSResources();
		m_Diagnostics.contextReady = false;
	}

	bool VansDLSS::Dispatch(const VansStreamlineDLSSDispatch& dispatch)
	{
		m_Diagnostics.lastDispatchSucceeded = false;
		if (!m_Diagnostics.contextReady ||
			!VansStreamlineRuntime::Get().EvaluateDLSS(dispatch))
		{
			m_Diagnostics.lastDispatchCode = 1;
			m_Diagnostics.lastError = "slEvaluateFeature(DLSS) failed";
			++m_Diagnostics.failedDispatchCount;
			return false;
		}
		m_Diagnostics.lastDispatchCode = 0;
		m_Diagnostics.lastError.clear();
		m_Diagnostics.lastDispatchSucceeded = true;
		++m_Diagnostics.successfulDispatchCount;
		return true;
	}
}
