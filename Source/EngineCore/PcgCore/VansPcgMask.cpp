#include "VansPcgMask.h"
#include "../Util/VansFileFingerprint.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Vans
{
bool VansPcgBounds::IsValid() const
{
	return std::isfinite(min[0]) && std::isfinite(min[1]) &&
		std::isfinite(max[0]) && std::isfinite(max[1]) && max[0] > min[0] && max[1] > min[1];
}

bool VansPcgBounds::Contains(float x, float z) const
{
	return x >= min[0] && x < max[0] && z >= min[1] && z < max[1];
}

bool VansPcgBounds::operator==(const VansPcgBounds& other) const
{
	return min == other.min && max == other.max;
}

bool VansPcgMaskTarget::IsValid() const
{
	return !regionId.empty() && !layerId.empty() && !maskId.empty();
}

bool VansPcgMaskTarget::operator==(const VansPcgMaskTarget& other) const
{
	return regionId == other.regionId && layerId == other.layerId && maskId == other.maskId;
}

void VansPcgPixelRect::Include(const VansPcgPixelRect& other)
{
	if (other.Empty()) return;
	if (Empty()) { *this = other; return; }
	minX = std::min(minX, other.minX);
	minY = std::min(minY, other.minY);
	maxX = std::max(maxX, other.maxX);
	maxY = std::max(maxY, other.maxY);
}

bool VansPcgMask::IsValid() const
{
	return target.IsValid() && bounds.IsValid() && width && height &&
		static_cast<std::uint64_t>(width) * height == pixels.size();
}

bool VansPcgMask::CreateBlank(const VansPcgMaskTarget& target, const VansPcgBounds& bounds,
	std::uint32_t width, std::uint32_t height, VansPcgMask& result, std::string& error)
{
	error.clear();
	const std::uint64_t count = static_cast<std::uint64_t>(width) * height;
	if (!target.IsValid() || !bounds.IsValid() || !width || !height ||
		count > std::vector<std::uint16_t>().max_size())
	{
		error = "PCG Mask requires an owner, a nonempty world range and valid dimensions";
		return false;
	}
	VansPcgMask blank;
	blank.target = target;
	blank.bounds = bounds;
	blank.width = width;
	blank.height = height;
	blank.pixels.assign(static_cast<std::size_t>(count), 0);
	result = std::move(blank);
	return true;
}

float VansPcgMask::Sample(float worldX, float worldZ) const
{
	// 缺失或无效 Mask 始终禁止生成，不再等同于全白。
	if (!IsValid() || !bounds.Contains(worldX, worldZ)) return 0.0f;
	const double x = std::clamp((static_cast<double>(worldX) - bounds.min[0]) /
		(static_cast<double>(bounds.max[0]) - bounds.min[0]) * width - 0.5, 0.0, static_cast<double>(width - 1));
	const double y = std::clamp((static_cast<double>(worldZ) - bounds.min[1]) /
		(static_cast<double>(bounds.max[1]) - bounds.min[1]) * height - 0.5, 0.0, static_cast<double>(height - 1));
	const auto x0 = static_cast<std::uint32_t>(x), y0 = static_cast<std::uint32_t>(y);
	const auto x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
	const double u = x - x0, v = y - y0;
	const auto at = [&](std::uint32_t px, std::uint32_t py) {
		return static_cast<double>(pixels[static_cast<std::size_t>(py) * width + px]);
	};
	return static_cast<float>(((at(x0, y0) * (1 - u) + at(x1, y0) * u) * (1 - v) +
		(at(x0, y1) * (1 - u) + at(x1, y1) * u) * v) / 65535.0);
}

std::uint64_t VansPcgMask::ContentHash() const
{
	std::uint64_t hash = VANS_FNV1A64_OFFSET_BASIS;
	const auto append = [&](const void* data, std::size_t size) {
		hash = ContinueMemoryFnv1a64(hash, data, size);
	};
	for (const auto* id : { &target.regionId, &target.layerId, &target.maskId })
	{
		append(id->data(), id->size());
		const unsigned char separator = 0;
		append(&separator, 1);
	}
	append(bounds.min.data(), sizeof(float) * 2);
	append(bounds.max.data(), sizeof(float) * 2);
	append(&width, sizeof(width));
	append(&height, sizeof(height));
	append(pixels.data(), pixels.size() * sizeof(std::uint16_t));
	return hash;
}
}
