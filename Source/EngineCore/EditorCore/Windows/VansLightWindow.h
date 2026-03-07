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

		void DrawFogParameters(VansVKDevice& device);
		void ShowWindow(VansVKDevice& device) override;
	};
}
