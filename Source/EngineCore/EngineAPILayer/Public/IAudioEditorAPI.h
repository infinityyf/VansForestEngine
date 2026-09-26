#pragma once

#include "EngineDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class IAudioEditorAPI
	{
	public:
		virtual ~IAudioEditorAPI() = default;
		virtual AudioBusDebugSnapshot GetAudioBusDebugSnapshot() const = 0;
		virtual void SetAudioBusGain(const std::string& busName, float gain) = 0;
		virtual void SetAudioBusMuted(const std::string& busName, bool muted) = 0;
		virtual void SetAudioBusSoloed(const std::string& busName, bool soloed) = 0;
		virtual void SetAudioMaxActiveVoices(int maxActiveVoices) = 0;
		virtual void SetAudioSourceLimit(int sourceLimit) = 0;
	};
}
