#pragma once
#include "VansBaseWindowComponent.h"

#include <string>
#include <vector>
namespace VansGraphics
{
	enum InspectResourceType
	{
		None = 0,
		TextAsset,
		TextureAsset,
		ModelAsset,
	};
	class VansInspectorWindow : public VansBaseWindowComponent
	{
	private:

		void ShowWindow(VansVKDevice& device) override;

	private:

		void ShowTextAsset();

		void ShowTextureAsset(VansVKDevice& device);

		void ShowModelTextureAsset(VansVKDevice& device);
	};
}