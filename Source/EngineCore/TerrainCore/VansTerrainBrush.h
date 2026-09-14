#pragma once

#include "VansTerrainAsset.h"

#include <cstdint>
#include <string>

namespace Vans
{
struct VansTerrainDirtyRect
{
	std::uint32_t minX = 0;
	std::uint32_t minY = 0;
	std::uint32_t maxX = 0;
	std::uint32_t maxY = 0;
	bool valid = false;

	std::uint32_t Width() const { return valid ? maxX - minX + 1u : 0u; }
	std::uint32_t Height() const { return valid ? maxY - minY + 1u : 0u; }
	void Include(const VansTerrainDirtyRect& other);
};

enum class VansTerrainBrushOperation
{
	Raise,
	Lower,
	SmoothHeight,
	Flatten,
	Noise,
	PaintLayer,
	EraseLayer,
	SmoothWeights
};

enum class VansTerrainBrushPattern
{
	SmoothCircle,
	LinearCircle,
	Sphere,
	Tip,
	SoftSquare,
	Ridge,
	Crater,
	Rocky
};

struct VansTerrainBrushDab
{
	VansTerrainBrushOperation operation = VansTerrainBrushOperation::Raise;
	float centerX = 0.0f;
	float centerY = 0.0f;
	float radius = 1.0f;
	float strength = 0.1f;
	float hardness = 0.5f;
	VansTerrainBrushPattern pattern = VansTerrainBrushPattern::SmoothCircle;
	float rotationRadians = 0.0f;
	float targetHeight = 0.5f;
	std::uint32_t selectedLayer = 0;
	std::uint32_t weightBaseLayer = 0;
	std::uint32_t noiseSeed = 0;
};

struct VansTerrainBrushResult
{
	bool changed = false;
	VansTerrainDirtyRect dirtyRect;
	std::string error;

	explicit operator bool() const { return error.empty(); }
};

class VansTerrainBrush
{
public:
	static float EvaluateInfluence(const VansTerrainBrushDab& dab, float x, float y);
	static VansTerrainDirtyRect CalculateAffectedRect(
		const VansTerrainAsset& terrain,
		const VansTerrainBrushDab& dab);
	static VansTerrainBrushResult Apply(VansTerrainAsset& terrain, const VansTerrainBrushDab& dab);
};
}
