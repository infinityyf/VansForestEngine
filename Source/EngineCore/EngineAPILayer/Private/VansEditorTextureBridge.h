#pragma once

#include "../Public/EngineDTOs.h"

namespace Vans::Editor
{
	class VansEditorTextureBridge
	{
	public:
		static EditorAPI::EditorTextureHandle RegisterTexture(
			void* sampler, void* imageView, int imageLayout, void* image);
		static bool RegisterFrameTextureVariant(
			EditorAPI::EditorTextureHandle primary,
			EditorAPI::EditorTextureHandle alternate);
		static void UnregisterFrameTextureVariant(EditorAPI::EditorTextureHandle primary);
		static void RemoveTexture(EditorAPI::EditorTextureHandle texture);
	};
}
