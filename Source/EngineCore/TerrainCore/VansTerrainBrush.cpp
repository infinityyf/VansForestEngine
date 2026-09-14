#include "VansTerrainBrush.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Vans
{
namespace
{
float Saturate(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStep(float value)
{
	value = Saturate(value);
	return value * value * (3.0f - 2.0f * value);
}

std::uint32_t Hash(std::uint32_t x, std::uint32_t y, std::uint32_t seed)
{
	std::uint32_t value = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
	value ^= value >> 16u;
	value *= 0x7feb352du;
	value ^= value >> 15u;
	value *= 0x846ca68bu;
	return value ^ (value >> 16u);
}

float Falloff(float distance, float hardness, VansTerrainBrushPattern pattern)
{
	if (distance > 1.0f)
		return 0.0f;
	hardness = Saturate(hardness);
	if (distance <= hardness || hardness >= 1.0f)
		return 1.0f;
	const float edge = Saturate((distance - hardness) / (1.0f - hardness));
	switch (pattern)
	{
	case VansTerrainBrushPattern::LinearCircle:
		return 1.0f - edge;
	case VansTerrainBrushPattern::Sphere:
		return std::sqrt(std::max(0.0f, 1.0f - edge * edge));
	case VansTerrainBrushPattern::Tip:
	{
		const float inverse = 1.0f - edge;
		return 1.0f - std::sqrt(std::max(0.0f, 1.0f - inverse * inverse));
	}
	default:
		return 1.0f - SmoothStep(edge);
	}
}

float ValueNoise(float x, float y, std::uint32_t seed)
{
	const int x0 = static_cast<int>(std::floor(x));
	const int y0 = static_cast<int>(std::floor(y));
	const float tx = SmoothStep(x - static_cast<float>(x0));
	const float ty = SmoothStep(y - static_cast<float>(y0));
	const auto sample = [seed](int sx, int sy)
	{
		return static_cast<float>(Hash(
			static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy), seed) & 0xffffu) /
			65535.0f;
	};
	const float a = sample(x0, y0);
	const float b = sample(x0 + 1, y0);
	const float c = sample(x0, y0 + 1);
	const float d = sample(x0 + 1, y0 + 1);
	const float top = a + (b - a) * tx;
	const float bottom = c + (d - c) * tx;
	return top + (bottom - top) * ty;
}

std::uint8_t WeightAt(const VansTerrainAsset& terrain, std::size_t pixel, std::uint32_t layer)
{
	return terrain.splatPixels[layer / 4u][pixel * 4u + layer % 4u];
}

void SetWeight(VansTerrainAsset& terrain, std::size_t pixel, std::uint32_t layer, std::uint8_t value)
{
	terrain.splatPixels[layer / 4u][pixel * 4u + layer % 4u] = value;
}

std::array<std::uint8_t, VANS_TERRAIN_LAYER_COUNT> ReadWeights(
	const VansTerrainAsset& terrain, std::size_t pixel)
{
	std::array<std::uint8_t, VANS_TERRAIN_LAYER_COUNT> result{};
	for (std::uint32_t layer = 0; layer < VANS_TERRAIN_LAYER_COUNT; ++layer)
		result[layer] = WeightAt(terrain, pixel, layer);
	return result;
}

void NormalizeWeights(
	std::array<float, VANS_TERRAIN_LAYER_COUNT> source,
	std::uint32_t weightBaseLayer,
	std::array<std::uint8_t, VANS_TERRAIN_LAYER_COUNT>& result)
{
	weightBaseLayer = std::min<std::uint32_t>(weightBaseLayer, VANS_TERRAIN_LAYER_COUNT - 1u);
	float total = 0.0f;
	for (float& value : source)
	{
		value = std::max(value, 0.0f);
		total += value;
	}
	if (total <= (std::numeric_limits<float>::epsilon)())
	{
		result.fill(0);
		result[weightBaseLayer] = 255;
		return;
	}
	std::uint32_t quantizedTotal = 0;
	std::array<float, VANS_TERRAIN_LAYER_COUNT> fractions{};
	for (std::uint32_t layer = 0; layer < VANS_TERRAIN_LAYER_COUNT; ++layer)
	{
		const float exact = source[layer] * 255.0f / total;
		const auto quantized = static_cast<std::uint8_t>(std::floor(exact));
		result[layer] = quantized;
		fractions[layer] = exact - quantized;
		quantizedTotal += quantized;
	}
	while (quantizedTotal < 255u)
	{
		const auto found = std::max_element(fractions.begin(), fractions.end());
		const std::size_t layer = static_cast<std::size_t>(found - fractions.begin());
		++result[layer];
		*found = -1.0f;
		++quantizedTotal;
	}
}

void WriteWeights(
	VansTerrainAsset& terrain,
	std::size_t pixel,
	const std::array<std::uint8_t, VANS_TERRAIN_LAYER_COUNT>& weights)
{
	for (std::uint32_t layer = 0; layer < VANS_TERRAIN_LAYER_COUNT; ++layer)
		SetWeight(terrain, pixel, layer, weights[layer]);
}

VansTerrainDirtyRect DabBounds(const VansTerrainAsset& terrain, const VansTerrainBrushDab& dab)
{
	VansTerrainDirtyRect result;
	if (!terrain.HasPixelData() || !std::isfinite(dab.centerX) || !std::isfinite(dab.centerY) ||
		!std::isfinite(dab.radius) || dab.radius <= 0.0f)
		return result;
	const int minX = std::max(0, static_cast<int>(std::floor(dab.centerX - dab.radius)));
	const int minY = std::max(0, static_cast<int>(std::floor(dab.centerY - dab.radius)));
	const int maxX = std::min(static_cast<int>(terrain.width) - 1,
		static_cast<int>(std::ceil(dab.centerX + dab.radius)));
	const int maxY = std::min(static_cast<int>(terrain.height) - 1,
		static_cast<int>(std::ceil(dab.centerY + dab.radius)));
	if (minX > maxX || minY > maxY)
		return result;
	result.minX = static_cast<std::uint32_t>(minX);
	result.minY = static_cast<std::uint32_t>(minY);
	result.maxX = static_cast<std::uint32_t>(maxX);
	result.maxY = static_cast<std::uint32_t>(maxY);
	result.valid = true;
	return result;
}
}

VansTerrainDirtyRect VansTerrainBrush::CalculateAffectedRect(
	const VansTerrainAsset& terrain,
	const VansTerrainBrushDab& dab)
{
	return DabBounds(terrain, dab);
}

float VansTerrainBrush::EvaluateInfluence(const VansTerrainBrushDab& dab, float x, float y)
{
	if (!std::isfinite(dab.radius) || dab.radius <= 0.0f ||
		!std::isfinite(dab.rotationRadians))
		return 0.0f;
	const float localX = (x - dab.centerX) / dab.radius;
	const float localY = (y - dab.centerY) / dab.radius;
	const float cosine = std::cos(dab.rotationRadians);
	const float sine = std::sin(dab.rotationRadians);
	const float u = localX * cosine + localY * sine;
	const float v = -localX * sine + localY * cosine;
	const float radialDistance = std::sqrt(u * u + v * v);
	if (radialDistance > 1.0f && dab.pattern != VansTerrainBrushPattern::SoftSquare)
		return 0.0f;

	float mask = 0.0f;
	switch (dab.pattern)
	{
	case VansTerrainBrushPattern::SoftSquare:
		mask = Falloff(std::max(std::abs(u), std::abs(v)), dab.hardness, dab.pattern);
		break;
	case VansTerrainBrushPattern::Ridge:
		mask = Falloff(radialDistance, dab.hardness, dab.pattern) *
			std::exp(-18.0f * v * v);
		break;
	case VansTerrainBrushPattern::Crater:
	{
		const float ring = (radialDistance - 0.62f) / 0.16f;
		mask = Falloff(radialDistance, dab.hardness, dab.pattern) * std::exp(-ring * ring);
		break;
	}
	case VansTerrainBrushPattern::Rocky:
	{
		const float coarse = ValueNoise(u * 3.5f + 11.0f, v * 3.5f + 17.0f, dab.noiseSeed);
		const float fine = ValueNoise(u * 8.0f + 31.0f, v * 8.0f + 47.0f, dab.noiseSeed + 1u);
		mask = Falloff(radialDistance, dab.hardness, dab.pattern) *
			(0.25f + 0.75f * SmoothStep(coarse * 0.7f + fine * 0.3f));
		break;
	}
	default:
		mask = Falloff(radialDistance, dab.hardness, dab.pattern);
		break;
	}
	return Saturate(dab.strength) * Saturate(mask);
}

void VansTerrainDirtyRect::Include(const VansTerrainDirtyRect& other)
{
	if (!other.valid)
		return;
	if (!valid)
	{
		*this = other;
		return;
	}
	minX = std::min(minX, other.minX);
	minY = std::min(minY, other.minY);
	maxX = std::max(maxX, other.maxX);
	maxY = std::max(maxY, other.maxY);
}

VansTerrainBrushResult VansTerrainBrush::Apply(
	VansTerrainAsset& terrain,
	const VansTerrainBrushDab& dab)
{
	VansTerrainBrushResult result;
	if (!terrain.HasPixelData())
	{
		result.error = "Terrain brush requires complete CPU pixel data";
		return result;
	}
	if (!std::isfinite(dab.strength) || dab.strength < 0.0f || dab.strength > 1.0f ||
		!std::isfinite(dab.hardness) || dab.hardness < 0.0f || dab.hardness > 1.0f ||
		!std::isfinite(dab.rotationRadians))
	{
		result.error = "Terrain brush strength, hardness, or rotation is invalid";
		return result;
	}
	if ((dab.operation == VansTerrainBrushOperation::PaintLayer ||
		dab.operation == VansTerrainBrushOperation::EraseLayer ||
		dab.operation == VansTerrainBrushOperation::SmoothWeights) &&
		(dab.selectedLayer >= terrain.layers.size() || dab.weightBaseLayer >= terrain.layers.size()))
	{
		result.error = "Terrain brush layer index is outside the active layer set";
		return result;
	}
	const VansTerrainDirtyRect bounds = CalculateAffectedRect(terrain, dab);
	if (!bounds.valid)
		return result;

	std::vector<std::uint16_t> sourceHeights;
	std::array<std::vector<std::uint8_t>, 2> sourceSplats;
	const std::uint32_t sampleMinX = bounds.minX > 0 ? bounds.minX - 1u : 0u;
	const std::uint32_t sampleMinY = bounds.minY > 0 ? bounds.minY - 1u : 0u;
	const std::uint32_t sampleMaxX = std::min(bounds.maxX + 1u, terrain.width - 1u);
	const std::uint32_t sampleMaxY = std::min(bounds.maxY + 1u, terrain.height - 1u);
	const std::uint32_t sampleWidth = sampleMaxX - sampleMinX + 1u;
	const std::uint32_t sampleHeight = sampleMaxY - sampleMinY + 1u;
	if (dab.operation == VansTerrainBrushOperation::SmoothHeight)
	{
		sourceHeights.resize(static_cast<std::size_t>(sampleWidth) * sampleHeight);
		for (std::uint32_t row = 0; row < sampleHeight; ++row)
		{
			const std::size_t sourceOffset =
				static_cast<std::size_t>(sampleMinY + row) * terrain.width + sampleMinX;
			const std::size_t destinationOffset = static_cast<std::size_t>(row) * sampleWidth;
			std::copy_n(terrain.heights.data() + sourceOffset, sampleWidth,
				sourceHeights.data() + destinationOffset);
		}
	}
	if (dab.operation == VansTerrainBrushOperation::SmoothWeights)
	{
		for (std::size_t image = 0; image < sourceSplats.size(); ++image)
		{
			sourceSplats[image].resize(static_cast<std::size_t>(sampleWidth) * sampleHeight * 4u);
			for (std::uint32_t row = 0; row < sampleHeight; ++row)
			{
				const std::size_t sourceOffset =
					(static_cast<std::size_t>(sampleMinY + row) * terrain.width + sampleMinX) * 4u;
				const std::size_t destinationOffset = static_cast<std::size_t>(row) * sampleWidth * 4u;
				std::copy_n(terrain.splatPixels[image].data() + sourceOffset,
					static_cast<std::size_t>(sampleWidth) * 4u,
					sourceSplats[image].data() + destinationOffset);
			}
		}
	}

	for (std::uint32_t y = bounds.minY; y <= bounds.maxY; ++y)
	{
		for (std::uint32_t x = bounds.minX; x <= bounds.maxX; ++x)
		{
			const float alpha = EvaluateInfluence(dab, static_cast<float>(x), static_cast<float>(y));
			if (alpha <= 0.0f)
				continue;
			const std::size_t pixel = static_cast<std::size_t>(y) * terrain.width + x;
			if (dab.operation == VansTerrainBrushOperation::Raise ||
				dab.operation == VansTerrainBrushOperation::Lower ||
				dab.operation == VansTerrainBrushOperation::Flatten ||
				dab.operation == VansTerrainBrushOperation::Noise ||
				dab.operation == VansTerrainBrushOperation::SmoothHeight)
			{
				const std::uint16_t before = terrain.heights[pixel];
				float target = static_cast<float>(before);
				if (dab.operation == VansTerrainBrushOperation::Raise ||
					dab.operation == VansTerrainBrushOperation::Lower)
				{
					const float direction = dab.operation == VansTerrainBrushOperation::Raise ? 1.0f : -1.0f;
					target += direction * alpha * 65535.0f;
				}
				else if (dab.operation == VansTerrainBrushOperation::Flatten)
				{
					target += (Saturate(dab.targetHeight) * 65535.0f - target) * alpha;
				}
				else if (dab.operation == VansTerrainBrushOperation::Noise)
				{
					const float noise = static_cast<float>(Hash(x, y, dab.noiseSeed) & 0xffffu) / 32767.5f - 1.0f;
					target += noise * alpha * 65535.0f;
				}
				else
				{
					std::uint32_t count = 0;
					std::uint32_t sum = 0;
					for (int oy = -1; oy <= 1; ++oy)
						for (int ox = -1; ox <= 1; ++ox)
						{
							const std::uint32_t sx = static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(x) + ox, 0, terrain.width - 1));
							const std::uint32_t sy = static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(y) + oy, 0, terrain.height - 1));
							const std::size_t sample =
								static_cast<std::size_t>(sy - sampleMinY) * sampleWidth +
								(sx - sampleMinX);
							sum += sourceHeights[sample];
							++count;
						}
					target += (static_cast<float>(sum) / count - target) * alpha;
				}
				const auto after = static_cast<std::uint16_t>(std::lround(std::clamp(target, 0.0f, 65535.0f)));
				if (after != before)
				{
					terrain.heights[pixel] = after;
					result.changed = true;
				}
				continue;
			}

			std::array<std::uint8_t, VANS_TERRAIN_LAYER_COUNT> before = ReadWeights(terrain, pixel);
			std::array<float, VANS_TERRAIN_LAYER_COUNT> desired{};
			if (dab.operation == VansTerrainBrushOperation::SmoothWeights)
			{
				for (std::uint32_t layer = 0; layer < terrain.layers.size(); ++layer)
				{
					float sum = 0.0f;
					std::uint32_t count = 0;
					for (int oy = -1; oy <= 1; ++oy)
						for (int ox = -1; ox <= 1; ++ox)
						{
							const std::uint32_t sx = static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(x) + ox, 0, terrain.width - 1));
							const std::uint32_t sy = static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(y) + oy, 0, terrain.height - 1));
							const std::size_t sample =
								static_cast<std::size_t>(sy - sampleMinY) * sampleWidth +
								(sx - sampleMinX);
							sum += sourceSplats[layer / 4u][sample * 4u + layer % 4u];
							++count;
						}
					desired[layer] = before[layer] + (sum / count - before[layer]) * alpha;
				}
			}
			else
			{
				const float selected = before[dab.selectedLayer];
				const float newSelected = dab.operation == VansTerrainBrushOperation::PaintLayer
					? selected + (255.0f - selected) * alpha
					: selected * (1.0f - alpha);
				float otherTotal = 0.0f;
				for (std::uint32_t layer = 0; layer < terrain.layers.size(); ++layer)
					if (layer != dab.selectedLayer) otherTotal += before[layer];
				desired[dab.selectedLayer] = newSelected;
				const float remainder = 255.0f - newSelected;
				if (otherTotal > 0.0f)
				{
					for (std::uint32_t layer = 0; layer < terrain.layers.size(); ++layer)
						if (layer != dab.selectedLayer) desired[layer] = before[layer] * remainder / otherTotal;
				}
				else
				{
					const std::uint32_t receiver = dab.weightBaseLayer == dab.selectedLayer && terrain.layers.size() > 1
						? (dab.selectedLayer + 1u) % static_cast<std::uint32_t>(terrain.layers.size())
						: dab.weightBaseLayer;
					desired[receiver] = remainder;
				}
			}
			std::array<std::uint8_t, VANS_TERRAIN_LAYER_COUNT> after{};
			NormalizeWeights(desired, dab.weightBaseLayer, after);
			if (after != before)
			{
				WriteWeights(terrain, pixel, after);
				result.changed = true;
			}
		}
	}
	if (result.changed)
		result.dirtyRect = bounds;
	return result;
}
}
