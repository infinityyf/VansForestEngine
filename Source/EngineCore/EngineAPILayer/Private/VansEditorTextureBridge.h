#pragma once

#include "../Public/EngineDTOs.h"

namespace Vans::Editor
{
	class VansEditorTextureBridge
	{
	public:
		static EditorAPI::EditorTextureHandle RegisterTexture(void* sampler, void* imageView, int imageLayout);
		static void RemoveTexture(EditorAPI::EditorTextureHandle texture);
	};
}
