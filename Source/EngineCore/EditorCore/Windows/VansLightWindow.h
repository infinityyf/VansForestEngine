#pragma once

#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansLightWindow : public VansBaseWindowComponent
	{
	private:
		// Mirrors the FogParams UBO layout — kept in sync with VolumetricFog.comp
		struct FogParamsData
		{
			float fogDensity      = 0.01f;
			float heightFalloff   = 0.05f;
			float sunScatterScale = 0.3f;
			float ambientScale    = 0.5f;
			float fogMinHeight    = -100.0f;
			float skyFogDistance  = 3000000.0f;
		};

		FogParamsData m_FogParams;

		// Mirrors the FogVolumeParams UBO layout — kept in sync with FogLightInjection.comp (std140, 64 bytes)
		struct FogVolumeParamsData
		{
			float density      = 0.05f;
			float anisotropy   = 0.6f;
			float scatterScale = 1.0f;
			float ambientScale = 0.05f;
			float volumeNear   = 2.0f;
			float volumeFar    = 200.0f;
			float slicePower   = 2.0f;
			float _pad1        = 0.0f;
			float fogBoxMin[4] = {-50.0f, -50.0f, -50.0f, 0.0f};
			float fogBoxMax[4] = { 50.0f,  50.0f,  50.0f, 0.0f};
		};

		FogVolumeParamsData m_FogVolumeParams;

		void DrawFogParameters(VansVKDevice& device);
		void DrawFogVolumeParameters(VansVKDevice& device);
		void ShowWindow(VansVKDevice& device) override;
	};
}
