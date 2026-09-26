#pragma once

#include <cstdint>

namespace Vans::EditorAPI
{
	using AssetId = std::uint64_t;
	using ViewportId = std::uint32_t;
	using RenderTextureId = std::uint64_t;

	constexpr AssetId InvalidAssetId = 0;
	constexpr ViewportId MainViewportId = 0;
}
