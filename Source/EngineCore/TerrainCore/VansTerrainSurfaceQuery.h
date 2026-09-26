#pragma once

#include <array>
#include <string>

namespace Vans
{
struct VansTerrainAsset;

struct VansTerrainSurfaceSample
{
	float height = 0.0f;
	std::array<float, 3> normal{ 0.0f, 1.0f, 0.0f };
	float pixelX = 0.0f;
	float pixelY = 0.0f;
};

struct VansTerrainSurfaceRayHit
{
	std::array<float, 3> position{};
	std::array<float, 3> normal{ 0.0f, 1.0f, 0.0f };
	float distance = 0.0f;
	float pixelX = 0.0f;
	float pixelY = 0.0f;
};

// CPU 高度场采样和射线求交的唯一实现；坐标与线性采样纹理的像素中心一致。
class VansTerrainSurfaceQuery
{
public:
	static bool Validate(const VansTerrainAsset& terrain, std::string& error);
	static bool Sample(const VansTerrainAsset& terrain, float worldX, float worldZ,
		VansTerrainSurfaceSample& sample);
	static bool Raycast(const VansTerrainAsset& terrain,
		const std::array<float, 3>& origin,
		const std::array<float, 3>& direction,
		float maximumDistance,
		VansTerrainSurfaceRayHit& hit);
};
}
