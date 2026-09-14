#include "VansPcgMaskBrush.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Vans
{
namespace
{
constexpr std::uint32_t TileSize = 32;

std::vector<std::uint16_t> ReadRect(const VansPcgMask& mask, const VansPcgPixelRect& rect)
{
	std::vector<std::uint16_t> values;
	values.reserve(static_cast<std::size_t>(rect.Width()) * rect.Height());
	for (std::uint32_t y = rect.minY; y < rect.maxY; ++y)
	{
		const auto start = mask.pixels.begin() + static_cast<std::size_t>(y) * mask.width + rect.minX;
		values.insert(values.end(), start, start + rect.Width());
	}
	return values;
}

void WriteRect(VansPcgMask& mask, const VansPcgPixelRect& rect, const std::vector<std::uint16_t>& values)
{
	for (std::uint32_t y = rect.minY; y < rect.maxY; ++y)
	{
		const auto start = values.begin() + static_cast<std::size_t>(y - rect.minY) * rect.Width();
		std::copy_n(start, rect.Width(), mask.pixels.begin() + static_cast<std::size_t>(y) * mask.width + rect.minX);
	}
}

bool Unit(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
}

bool VansPcgBrushSettings::IsValid() const
{
	return operation >= VansPcgBrushOperation::Add && operation <= VansPcgBrushOperation::Erase &&
		std::isfinite(radius) && radius > 0 && Unit(strength) && Unit(hardness) && Unit(targetValue) &&
		std::isfinite(spacingFraction) && spacingFraction >= 0.01f && spacingFraction <= 1.0f;
}

bool VansPcgMaskEdit::Apply(VansPcgMask& mask, bool redo, std::string& error) const
{
	error.clear();
	if (!mask.IsValid() || !(mask.target == target) || !(mask.bounds == bounds) ||
		mask.width != width || mask.height != height)
	{
		error = "PCG Mask edit target or mapping has changed";
		return false;
	}
	// 先验证整个事务，再写入；目标错误或撤销顺序错误不会造成部分写入。
	for (const auto& tile : tiles)
	{
		const std::size_t count = static_cast<std::size_t>(tile.rect.Width()) * tile.rect.Height();
		if (tile.rect.Empty() || tile.rect.maxX > width || tile.rect.maxY > height ||
			tile.before.size() != count || tile.after.size() != count ||
			ReadRect(mask, tile.rect) != (redo ? tile.before : tile.after))
		{
			error = "PCG Mask edit is invalid or conflicts with a later pixel edit";
			return false;
		}
	}
	for (const auto& tile : tiles) WriteRect(mask, tile.rect, redo ? tile.after : tile.before);
	return true;
}

bool VansPcgMaskStroke::Matches(const VansPcgMask& mask) const
{
	return mask.IsValid() && mask.target == m_Target && mask.bounds == m_Bounds &&
		mask.width == m_Width && mask.height == m_Height;
}

bool VansPcgMaskStroke::Begin(const VansPcgMask& mask, const VansPcgBrushSettings& settings, std::string& error)
{
	error.clear();
	if (m_Active || !mask.IsValid() || !settings.IsValid())
	{
		error = "PCG brush requires a valid target and settings, with no active stroke";
		return false;
	}
	m_Target = mask.target;
	m_Bounds = mask.bounds;
	m_Width = mask.width;
	m_Height = mask.height;
	m_Settings = settings;
	m_Tiles.clear();
	m_Dirty = {};
	m_HasPoint = false;
	m_Active = true;
	return true;
}

void VansPcgMaskStroke::BreakSegment() { m_HasPoint = false; }

bool VansPcgMaskStroke::AddPoint(VansPcgMask& mask, float worldX, float worldZ, bool invert,
	VansPcgPixelRect& changed, std::string& error)
{
	changed = {};
	error.clear();
	if (!m_Active || !Matches(mask) || !std::isfinite(worldX) || !std::isfinite(worldZ))
	{
		error = "PCG brush point does not match the active target or is not finite";
		return false;
	}
	if (!mask.bounds.Contains(worldX, worldZ)) { BreakSegment(); return true; }
	const double pixelSize = std::min(
		(static_cast<double>(mask.bounds.max[0]) - mask.bounds.min[0]) / mask.width,
		(static_cast<double>(mask.bounds.max[1]) - mask.bounds.min[1]) / mask.height);
	const double spacing = std::max(static_cast<double>(m_Settings.radius) * m_Settings.spacingFraction, pixelSize * 0.25);
	if (!m_HasPoint)
	{
		ApplyDab(mask, worldX, worldZ, invert, changed);
		m_HasPoint = true;
		m_DistanceToNext = spacing;
	}
	else
	{
		const double dx = worldX - m_PreviousX, dz = worldZ - m_PreviousZ;
		const double distance = std::hypot(dx, dz);
		if (distance / spacing > 65536)
		{
			error = "PCG brush segment exceeds the input work budget; split the segment";
			return false;
		}
		double offset = m_DistanceToNext;
		while (distance > 0 && offset <= distance)
		{
			ApplyDab(mask, m_PreviousX + dx * (offset / distance), m_PreviousZ + dz * (offset / distance), invert, changed);
			offset += spacing;
		}
		m_DistanceToNext = offset - distance;
	}
	m_PreviousX = worldX;
	m_PreviousZ = worldZ;
	m_Dirty.Include(changed);
	return true;
}

void VansPcgMaskStroke::CaptureTiles(const VansPcgMask& mask, const VansPcgPixelRect& rect)
{
	if (rect.Empty()) return;
	for (std::uint32_t ty = rect.minY / TileSize; ty <= (rect.maxY - 1) / TileSize; ++ty)
		for (std::uint32_t tx = rect.minX / TileSize; tx <= (rect.maxX - 1) / TileSize; ++tx)
		{
			const std::uint64_t key = (static_cast<std::uint64_t>(ty) << 32) | tx;
			if (m_Tiles.find(key) != m_Tiles.end()) continue;
			VansPcgMaskTileEdit tile;
			tile.rect = { tx * TileSize, ty * TileSize,
				static_cast<std::uint32_t>(std::min<std::uint64_t>((static_cast<std::uint64_t>(tx) + 1) * TileSize, mask.width)),
				static_cast<std::uint32_t>(std::min<std::uint64_t>((static_cast<std::uint64_t>(ty) + 1) * TileSize, mask.height)) };
			tile.before = ReadRect(mask, tile.rect);
			m_Tiles.emplace(key, std::move(tile));
		}
}

void VansPcgMaskStroke::ApplyDab(VansPcgMask& mask, double x, double z, bool invert, VansPcgPixelRect& changed)
{
	if (m_Settings.strength == 0) return;
	const double radius = m_Settings.radius;
	const double sx = (static_cast<double>(mask.bounds.max[0]) - mask.bounds.min[0]) / mask.width;
	const double sz = (static_cast<double>(mask.bounds.max[1]) - mask.bounds.min[1]) / mask.height;
	const auto low = [](double p, double minimum, double scale, std::uint32_t count) {
		return static_cast<std::uint32_t>(std::clamp(std::floor((p - minimum) / scale - 0.5), 0.0, static_cast<double>(count)));
	};
	const auto high = [](double p, double minimum, double scale, std::uint32_t count) {
		return static_cast<std::uint32_t>(std::clamp(std::ceil((p - minimum) / scale + 0.5), 0.0, static_cast<double>(count)));
	};
	const VansPcgPixelRect rect{ low(x - radius, mask.bounds.min[0], sx, mask.width),
		low(z - radius, mask.bounds.min[1], sz, mask.height),
		high(x + radius, mask.bounds.min[0], sx, mask.width), high(z + radius, mask.bounds.min[1], sz, mask.height) };
	CaptureTiles(mask, rect);
	VansPcgBrushOperation operation = m_Settings.operation;
	if (invert) operation = operation == VansPcgBrushOperation::Subtract || operation == VansPcgBrushOperation::Erase
		? VansPcgBrushOperation::Add : VansPcgBrushOperation::Subtract;
	const VansPcgPixelRect smoothRect{ rect.minX ? rect.minX - 1 : 0, rect.minY ? rect.minY - 1 : 0,
		std::min(rect.maxX, mask.width - 1) + 1, std::min(rect.maxY, mask.height - 1) + 1 };
	const auto smooth = operation == VansPcgBrushOperation::Smooth ? ReadRect(mask, smoothRect) : std::vector<std::uint16_t>();
	for (std::uint32_t py = rect.minY; py < rect.maxY; ++py)
		for (std::uint32_t px = rect.minX; px < rect.maxX; ++px)
		{
			const double wx = mask.bounds.min[0] + (px + 0.5) * sx, wz = mask.bounds.min[1] + (py + 0.5) * sz;
			const double distance = std::hypot(wx - x, wz - z) / radius;
			if (distance > 1) continue;
			const double linear = distance <= m_Settings.hardness ? 1.0 :
				(1 - distance) / (1 - m_Settings.hardness);
			const double weight = linear * linear * (3 - 2 * linear) * m_Settings.strength;
			const std::size_t index = static_cast<std::size_t>(py) * mask.width + px;
			const double before = mask.pixels[index] / 65535.0;
			double after = before;
			switch (operation)
			{
			case VansPcgBrushOperation::Add: after += weight; break;
			case VansPcgBrushOperation::Subtract: after -= weight; break;
			case VansPcgBrushOperation::Set: after += (m_Settings.targetValue - before) * weight; break;
			case VansPcgBrushOperation::Erase: after *= 1 - weight; break;
			case VansPcgBrushOperation::Smooth:
			{
				double sum = 0;
				for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
				{
					const auto nx = static_cast<std::uint32_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(px) + dx, 0, mask.width - 1));
					const auto ny = static_cast<std::uint32_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(py) + dy, 0, mask.height - 1));
					sum += smooth[static_cast<std::size_t>(ny - smoothRect.minY) * smoothRect.Width() + nx - smoothRect.minX];
				}
				after += (sum / (9 * 65535.0) - before) * weight;
				break;
			}
			}
			const auto quantized = static_cast<std::uint16_t>(std::lround(std::clamp(after, 0.0, 1.0) * 65535.0));
			if (quantized == mask.pixels[index]) continue;
			mask.pixels[index] = quantized;
			changed.Include({ px, py, px + 1, py + 1 });
		}
}

VansPcgMaskEdit VansPcgMaskStroke::BuildEdit(const VansPcgMask& mask) const
{
	VansPcgMaskEdit edit;
	edit.target = m_Target;
	edit.bounds = m_Bounds;
	edit.width = m_Width;
	edit.height = m_Height;
	edit.dirtyRect = m_Dirty;
	for (const auto& entry : m_Tiles)
	{
		auto tile = entry.second;
		tile.after = ReadRect(mask, tile.rect);
		if (tile.before != tile.after) edit.tiles.push_back(std::move(tile));
	}
	if (edit.Empty()) edit.dirtyRect = {};
	return edit;
}

bool VansPcgMaskStroke::Finish(const VansPcgMask& mask, VansPcgMaskEdit& edit, std::string& error)
{
	error.clear();
	if (!m_Active || !Matches(mask)) { error = "PCG brush finish target has changed"; return false; }
	edit = BuildEdit(mask);
	m_Tiles.clear();
	m_Active = false;
	m_HasPoint = false;
	return true;
}

bool VansPcgMaskStroke::Cancel(VansPcgMask& mask, std::string& error)
{
	error.clear();
	if (!m_Active || !Matches(mask)) { error = "PCG brush cancel target has changed"; return false; }
	const auto edit = BuildEdit(mask);
	if (!edit.Apply(mask, false, error)) return false;
	m_Tiles.clear();
	m_Active = false;
	m_HasPoint = false;
	return true;
}
}
