#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
struct VansPcgBounds
{
	std::array<float, 2> min{};
	std::array<float, 2> max{};
	bool IsValid() const;
	bool Contains(float x, float z) const;
	bool operator==(const VansPcgBounds& other) const;
};

// 所有编辑命令绑定完整目标，列表索引和共享模型不参与 Mask 寻址。
struct VansPcgMaskTarget
{
	std::string regionId;
	std::string layerId;
	std::string maskId;
	bool IsValid() const;
	bool operator==(const VansPcgMaskTarget& other) const;
};

struct VansPcgPixelRect
{
	std::uint32_t minX = 0;
	std::uint32_t minY = 0;
	std::uint32_t maxX = 0;
	std::uint32_t maxY = 0;
	bool Empty() const { return minX >= maxX || minY >= maxY; }
	std::uint32_t Width() const { return Empty() ? 0 : maxX - minX; }
	std::uint32_t Height() const { return Empty() ? 0 : maxY - minY; }
	void Include(const VansPcgPixelRect& other);
};

// 线性单通道密度，值语义保证复制后可独立修改；没有纹理路径或渲染依赖。
struct VansPcgMask
{
	VansPcgMaskTarget target;
	VansPcgBounds bounds;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<std::uint16_t> pixels;

	bool IsValid() const;
	float Sample(float worldX, float worldZ) const;
	std::uint64_t ContentHash() const;
	static bool CreateBlank(const VansPcgMaskTarget& target, const VansPcgBounds& bounds,
		std::uint32_t width, std::uint32_t height, VansPcgMask& result, std::string& error);
};
}
