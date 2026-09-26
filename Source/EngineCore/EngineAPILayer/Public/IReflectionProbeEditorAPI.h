#pragma once

#include "EngineDTOs.h"

#include <cstdint>

namespace Vans::EditorAPI
{
	class IReflectionProbeEditorAPI
	{
	public:
		virtual ~IReflectionProbeEditorAPI() = default;
		virtual void BakeQueuedReflectionProbesNow() = 0;
		virtual ReflectionProbeSettingsSnapshot GetReflectionProbeSettings() const = 0;
		virtual bool ApplyReflectionProbeSettings(
			const ReflectionProbeSettingsSnapshot& settings) = 0;
		virtual void GenerateAutoReflectionProbes() = 0;
		virtual void ClearAutoReflectionProbes() = 0;
		virtual void RequestReflectionProbeBakeAll() = 0;
		virtual void RequestReflectionProbeBake(std::uint32_t probeIndex) = 0;
		virtual void SaveReflectionProbeConfiguration() = 0;
		virtual void ConvertReflectionProbeToManual(std::uint32_t probeIndex) = 0;
	};
}
