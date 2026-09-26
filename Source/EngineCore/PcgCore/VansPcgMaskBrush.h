#pragma once

#include "VansPcgMask.h"

#include <map>

namespace Vans
{
enum class VansPcgBrushOperation { Add, Subtract, Set, Smooth, Erase };

struct VansPcgBrushSettings
{
	VansPcgBrushOperation operation = VansPcgBrushOperation::Add;
	float radius = 3.0f;
	float strength = 0.25f;
	float hardness = 0.5f;
	float targetValue = 1.0f;
	float spacingFraction = 0.2f;
	bool IsValid() const;
};

struct VansPcgMaskTileEdit
{
	VansPcgPixelRect rect;
	std::vector<std::uint16_t> before;
	std::vector<std::uint16_t> after;
};

struct VansPcgMaskEdit
{
	VansPcgMaskTarget target;
	VansPcgBounds bounds;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	VansPcgPixelRect dirtyRect;
	std::vector<VansPcgMaskTileEdit> tiles;
	bool Empty() const { return tiles.empty(); }
	bool Apply(VansPcgMask& mask, bool redo, std::string& error) const;
};

// 笔画在固定世界距离上重采样，与鼠标事件频率无关；只记录触及的像素块。
// 编辑器负责视口射线和工具互斥，此处不访问地形、磁盘或 GPU。
class VansPcgMaskStroke
{
public:
	bool Begin(const VansPcgMask& mask, const VansPcgBrushSettings& settings, std::string& error);
	bool AddPoint(VansPcgMask& mask, float worldX, float worldZ, bool invert,
		VansPcgPixelRect& changed, std::string& error);
	void BreakSegment();
	bool Finish(const VansPcgMask& mask, VansPcgMaskEdit& edit, std::string& error);
	bool Cancel(VansPcgMask& mask, std::string& error);
	bool IsActive() const { return m_Active; }

private:
	bool Matches(const VansPcgMask& mask) const;
	void ApplyDab(VansPcgMask& mask, double x, double z, bool invert, VansPcgPixelRect& changed);
	void CaptureTiles(const VansPcgMask& mask, const VansPcgPixelRect& rect);
	VansPcgMaskEdit BuildEdit(const VansPcgMask& mask) const;
	VansPcgMaskTarget m_Target;
	VansPcgBounds m_Bounds;
	std::uint32_t m_Width = 0;
	std::uint32_t m_Height = 0;
	VansPcgBrushSettings m_Settings;
	bool m_Active = false;
	bool m_HasPoint = false;
	double m_PreviousX = 0;
	double m_PreviousZ = 0;
	double m_DistanceToNext = 0;
	VansPcgPixelRect m_Dirty;
	std::map<std::uint64_t, VansPcgMaskTileEdit> m_Tiles;
};
}
