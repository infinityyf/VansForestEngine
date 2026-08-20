#pragma once

#include "../UpscalingCore/VansUpscalerTypes.h"
#include "../../../Graphics/Vulkan/VansStreamlineRuntime.h"

#include <cstdint>
#include <string>

namespace VansGraphics
{
	struct VansDLSSDiagnostics
	{
		bool contextReady = false;
		bool lastDispatchSucceeded = false;
		std::uint32_t lastCreateCode = 0;
		std::uint32_t lastDispatchCode = 0;
		std::uint64_t successfulDispatchCount = 0;
		std::uint64_t failedDispatchCount = 0;
		std::string lastError;
	};

	class VansDLSS
	{
	public:
		bool InitializeContext(
			VansUpscaleQualityMode quality,
			std::uint32_t outputWidth,
			std::uint32_t outputHeight,
			bool useExternalExposure);
		void Cleanup();
		bool Dispatch(const VansStreamlineDLSSDispatch& dispatch);

		bool QueryRecommendedRenderExtent(
			VansUpscaleQualityMode quality,
			std::uint32_t outputWidth,
			std::uint32_t outputHeight,
			VansExtent2D& renderExtent) const;

		const VansDLSSDiagnostics& GetDiagnostics() const { return m_Diagnostics; }

	private:
		static VansStreamlineDLSSMode ToStreamlineMode(VansUpscaleQualityMode quality);

		VansDLSSDiagnostics m_Diagnostics;
	};
}
