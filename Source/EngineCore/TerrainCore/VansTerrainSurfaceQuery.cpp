#include "VansTerrainSurfaceQuery.h"

#include "VansTerrainAsset.h"
#include "VansTerrainHeightEncoding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Vans
{
bool VansTerrainSurfaceQuery::Validate(const VansTerrainAsset& terrain, std::string& error)
{
	error.clear();
	if (terrain.width < 2 || terrain.height < 2 ||
		terrain.heights.size() != static_cast<std::size_t>(terrain.width) * terrain.height ||
		!std::isfinite(terrain.settings.terrainSize) || terrain.settings.terrainSize <= 0.0f ||
		!std::isfinite(terrain.settings.maxHeight) ||
		!std::isfinite(terrain.settings.heightOffset))
	{
		error = "The bound terrain heightfield is unavailable in memory.";
		return false;
	}
	return true;
}

bool VansTerrainSurfaceQuery::Sample(const VansTerrainAsset& terrain,
	float worldX, float worldZ, VansTerrainSurfaceSample& result)
{
	const float size = terrain.settings.terrainSize;
	const float half = size * 0.5f;
	if (!std::isfinite(worldX) || !std::isfinite(worldZ) ||
		worldX < -half || worldX > half || worldZ < -half || worldZ > half)
		return false;
	result.pixelX = std::clamp((worldX / size + 0.5f) * terrain.width - 0.5f,
		0.0f, static_cast<float>(terrain.width - 1u));
	result.pixelY = std::clamp((worldZ / size + 0.5f) * terrain.height - 0.5f,
		0.0f, static_cast<float>(terrain.height - 1u));
	const std::uint32_t x0 = static_cast<std::uint32_t>(result.pixelX);
	const std::uint32_t y0 = static_cast<std::uint32_t>(result.pixelY);
	const std::uint32_t x1 = std::min(x0 + 1u, terrain.width - 1u);
	const std::uint32_t y1 = std::min(y0 + 1u, terrain.height - 1u);
	const float tx = result.pixelX - x0;
	const float ty = result.pixelY - y0;
	const auto sample = [&](std::uint32_t x, std::uint32_t y)
	{
		return VansTerrainHeightEncoding::DecodeWorld(
			terrain.heights[static_cast<std::size_t>(y) * terrain.width + x],
			terrain.settings.maxHeight, terrain.settings.heightOffset);
	};
	const float h00 = sample(x0, y0);
	const float h10 = sample(x1, y0);
	const float h01 = sample(x0, y1);
	const float h11 = sample(x1, y1);
	result.height = (h00 + (h10 - h00) * tx) * (1.0f - ty) +
		(h01 + (h11 - h01) * tx) * ty;
	const float dx = (worldX / size + 0.5f) * terrain.width < 0.5f ? 0.0f :
		((h10 - h00) * (1.0f - ty) + (h11 - h01) * ty) * terrain.width / size;
	const float dz = (worldZ / size + 0.5f) * terrain.height < 0.5f ? 0.0f :
		((h01 - h00) * (1.0f - tx) + (h11 - h10) * tx) * terrain.height / size;
	const float length = std::sqrt(dx * dx + 1.0f + dz * dz);
	result.normal = { -dx / length, 1.0f / length, -dz / length };
	return true;
}

bool VansTerrainSurfaceQuery::Raycast(const VansTerrainAsset& terrain,
	const std::array<float, 3>& origin,
	const std::array<float, 3>& direction,
	float maximumDistance,
	VansTerrainSurfaceRayHit& hit)
{
	std::string error;
	if (!Validate(terrain, error) || !std::isfinite(maximumDistance) || maximumDistance <= 0.0f)
		return false;
	double norm = 0.0;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return false;
		norm += static_cast<double>(direction[axis]) * direction[axis];
	}
	if (norm < 1.0e-20) return false;
	norm = std::sqrt(norm);
	const std::array<double, 3> ray{
		direction[0] / norm, direction[1] / norm, direction[2] / norm };
	const double size = terrain.settings.terrainSize;
	const double half = size * 0.5;
	const double height0 = terrain.settings.heightOffset;
	const double height1 = height0 + terrain.settings.maxHeight;
	const std::array<double, 3> minimum{ -half, std::min(height0, height1), -half };
	const std::array<double, 3> maximum{ half, std::max(height0, height1), half };
	double enter = 0.0;
	double leave = maximumDistance;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		if (std::abs(ray[axis]) < 1.0e-12)
		{
			if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
			continue;
		}
		double a = (minimum[axis] - origin[axis]) / ray[axis];
		double b = (maximum[axis] - origin[axis]) / ray[axis];
		if (a > b) std::swap(a, b);
		enter = std::max(enter, a);
		leave = std::min(leave, b);
		if (enter > leave) return false;
	}
	const auto signedDistance = [&](double distance)
	{
		VansTerrainSurfaceSample sample;
		const float x = static_cast<float>(std::clamp(origin[0] + ray[0] * distance, -half, half));
		const float z = static_cast<float>(std::clamp(origin[2] + ray[2] * distance, -half, half));
		Sample(terrain, x, z, sample);
		return origin[1] + ray[1] * distance - sample.height;
	};
	const auto publish = [&](double distance)
	{
		hit.position = {
			static_cast<float>(origin[0] + ray[0] * distance),
			static_cast<float>(origin[1] + ray[1] * distance),
			static_cast<float>(origin[2] + ray[2] * distance) };
		VansTerrainSurfaceSample sample;
		if (!Sample(terrain,
			std::clamp(hit.position[0], static_cast<float>(-half), static_cast<float>(half)),
			std::clamp(hit.position[2], static_cast<float>(-half), static_cast<float>(half)), sample))
			return false;
		hit.position[1] = sample.height;
		hit.normal = sample.normal;
		hit.distance = static_cast<float>(distance);
		hit.pixelX = sample.pixelX;
		hit.pixelY = sample.pixelY;
		return true;
	};

	// 在线性采样单元边界切分射线；单元内高度沿射线是二次函数，精确保留最近根。
	std::vector<double> cuts{ enter, leave };
	for (const std::size_t axis : { std::size_t(0), std::size_t(2) })
	{
		if (std::abs(ray[axis]) < 1.0e-12) continue;
		const std::uint32_t count = axis == 0 ? terrain.width : terrain.height;
		const double pixel0 = (origin[axis] + ray[axis] * enter + half) / size * count - 0.5;
		const double pixel1 = (origin[axis] + ray[axis] * leave + half) / size * count - 0.5;
		const int first = static_cast<int>(std::max(0.0, std::ceil(std::min(pixel0, pixel1))));
		const int last = static_cast<int>(std::min(
			static_cast<double>(count - 1u), std::floor(std::max(pixel0, pixel1))));
		for (int grid = first; grid <= last; ++grid)
		{
			const double distance = (((grid + 0.5) / count - 0.5) * size - origin[axis]) / ray[axis];
			if (distance > enter && distance < leave) cuts.push_back(distance);
		}
	}
	std::sort(cuts.begin(), cuts.end());
	constexpr double tolerance = 1.0e-5;
	if (std::abs(signedDistance(enter)) <= tolerance) return publish(enter);
	for (std::size_t index = 1; index < cuts.size(); ++index)
	{
		const double begin = cuts[index - 1u];
		const double end = cuts[index];
		if (end - begin < 1.0e-10) continue;
		const double c = signedDistance(begin);
		const double middle = signedDistance((begin + end) * 0.5);
		const double finish = signedDistance(end);
		const double a = 2.0 * (c + finish - 2.0 * middle);
		const double b = finish - c - a;
		double roots[2]{ 2.0, 2.0 };
		if (std::abs(a) < 1.0e-8)
		{
			if (std::abs(b) > 1.0e-12) roots[0] = -c / b;
		}
		else
		{
			const double discriminant = b * b - 4.0 * a * c;
			if (discriminant >= -1.0e-10)
			{
				const double root = std::sqrt(std::max(0.0, discriminant));
				roots[0] = (-b - root) / (2.0 * a);
				roots[1] = (-b + root) / (2.0 * a);
				if (roots[0] > roots[1]) std::swap(roots[0], roots[1]);
			}
		}
		for (const double root : roots)
		{
			if (root >= -1.0e-7 && root <= 1.0 + 1.0e-7)
				return publish(begin + (end - begin) * std::clamp(root, 0.0, 1.0));
		}
	}
	return false;
}
}
