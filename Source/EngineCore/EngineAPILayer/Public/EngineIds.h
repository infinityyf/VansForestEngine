#pragma once

#include <cstdint>

namespace Vans::EditorAPI
{
	using EntityId = std::uint64_t;
	using ComponentId = std::uint64_t;
	using AssetId = std::uint64_t;
	using ViewportId = std::uint32_t;
	using RenderTextureId = std::uint64_t;

	constexpr EntityId InvalidEntityId = 0;
	constexpr ComponentId InvalidComponentId = 0;
	constexpr AssetId InvalidAssetId = 0;
	constexpr ViewportId MainViewportId = 0;
}
