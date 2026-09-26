#pragma once

#include "VansPcgMask.h"
#include "VansPcgConfigurationField.h"

#include <functional>

namespace Vans
{
struct VansPcgVariantChoice
{
	std::string id;
	float weight = 0;
	float footprintRadius = 0;
};

struct VansPcgPlacementSettings
{
	float density = 0;
	float positionJitter = 0;
	float minimumSpacing = 0;
	std::array<float, 3> scaleMin{ 1, 1, 1 };
	std::array<float, 3> scaleMax{ 1, 1, 1 };
	bool uniformScale = true;
	float yawMinDegrees = 0;
	float yawMaxDegrees = 0;
	float normalAlignment = 0;
	float maximumTiltDegrees = 0;
	float rootOffset = 0;
	float maskThreshold = 0;
	float maskMultiplier = 1;
	bool invertMask = false;
};

inline const std::array<VansPcgConfigurationFieldDescriptor<VansPcgPlacementSettings>, 14> VansPcgPlacementConfigurationFields{
	VansPcgConfigurationFieldDescriptor<VansPcgPlacementSettings>{ "density", "Density / m2", "", &VansPcgPlacementSettings::density, .01f, 0, 0, true, false, false, 0, 0, 0,
		VansPcgConfigurationFieldPersistence::Always, VansPcgConfigurationFieldVisibility::DensitySourceOnly },
	{ "positionJitter", "Position jitter", "", &VansPcgPlacementSettings::positionJitter, .01f, 0, 1, true, true, false, 0, 0, 1 },
	{ "minimumSpacing", "Minimum spacing (m)", "", &VansPcgPlacementSettings::minimumSpacing, .01f, 0, 0, true, false, false, 0, 0, 2 },
	{ "yawMinDegrees", "Yaw minimum (degrees)", "", &VansPcgPlacementSettings::yawMinDegrees, .01f, 0, 0, false, false, false, 0, 0, 6 },
	{ "yawMaxDegrees", "Yaw maximum (degrees)", "", &VansPcgPlacementSettings::yawMaxDegrees, .01f, 0, 0, false, false, false, 0, 0, 7 },
	{ "normalAlignment", "Normal alignment", "", &VansPcgPlacementSettings::normalAlignment, .01f, 0, 1, true, true, false, 0, 0, 8 },
	{ "maximumTiltDegrees", "Maximum tilt (degrees)", "", &VansPcgPlacementSettings::maximumTiltDegrees, .01f, 0, 180, true, true, false, 0, 0, 9 },
	{ "rootOffset", "Root offset (m)", "", &VansPcgPlacementSettings::rootOffset, .01f, -1073741824.f, 1073741824.f, true, true, false, 0, 0, 10 },
	{ "maskThreshold", "Mask threshold", "", &VansPcgPlacementSettings::maskThreshold, .01f, 0, 1, true, true, false, 0, 0, 11 },
	{ "maskMultiplier", "Mask multiplier", "", &VansPcgPlacementSettings::maskMultiplier, .01f, 0, 0, true, false, false, 0, 0, 12 },
	{ "scaleMin", "Scale minimum", "", &VansPcgPlacementSettings::scaleMin, .01f, 0, 0, false, false, false, 0, 0, 4 },
	{ "scaleMax", "Scale maximum", "", &VansPcgPlacementSettings::scaleMax, .01f, 0, 0, false, false, false, 0, 0, 5 },
	{ "uniformScale", "Uniform scale", "", &VansPcgPlacementSettings::uniformScale, .01f, 0, 0, false, false, false, 0, 0, 3 },
	{ "invertMask", "Invert Mask", "", &VansPcgPlacementSettings::invertMask, .01f, 0, 0, false, false, false, 0, 0, 13 }
};

struct VansPcgDistributionSettings : VansPcgPlacementSettings
{
	std::string regionId;
	std::string layerId;
	VansPcgBounds bounds;
	std::uint64_t seed = 0;
	std::vector<VansPcgVariantChoice> variants;
};

std::vector<std::string> ValidatePcgPlacement(const VansPcgPlacementSettings& settings);

struct VansPcgSurfacePoint
{
	float height = 0;
	std::array<float, 3> normal{ 0, 1, 0 };
};
using VansPcgSurfaceSampler = std::function<bool(float, float, VansPcgSurfacePoint&)>;

struct VansPcgPoint
{
	std::uint64_t id = 0;
	std::uint32_t variantIndex = 0;
	std::array<float, 3> position{};
	std::array<float, 3> scale{ 1, 1, 1 };
	std::array<float, 4> rotation{ 0, 0, 0, 1 };
};

struct VansPcgGenerationBudget
{
	std::uint64_t maxCandidates = 0;
	std::uint64_t maxInstances = 0;
};

struct VansPcgGenerationStats
{
	std::uint64_t candidates = 0;
	std::uint64_t maskRejected = 0;
	std::uint64_t surfaceRejected = 0;
	std::uint64_t spacingRejected = 0;
	std::uint64_t neighborQueries = 0;
	std::uint64_t requestedCount = 0;
	std::uint64_t generatedCount = 0;
	bool candidateBudgetExhausted = false;
};

struct VansPcgGenerationResult
{
	// 变体表按稳定 ID 排序；实例中仅保存索引，不为每根草分配字符串。
	std::vector<std::string> variantIds;
	std::vector<VansPcgPoint> points;
	VansPcgGenerationStats stats;
	std::string error;
	explicit operator bool() const { return error.empty(); }
};

class VansPcgPointGenerator
{
public:
	// 按稳定实例 ID 采样 Mask；灰度只决定保留与否，不改变实例的位置或模型。
	static bool PassesMask(const VansPcgPlacementSettings& settings,
		const VansPcgMask& densityMask, const VansPcgMask* exclusionMask,
		float x, float z, std::uint64_t id, double acceptance = 1);
	// outputBounds 是当前待更新块，邻域由用户间距确定；输出只归属该块。
	// 输入须来自同一不可变快照。纯计算不读取场景、材质、海拔或地形权重。
	static VansPcgGenerationResult GenerateDensity(const VansPcgDistributionSettings& settings,
		const VansPcgMask& densityMask, const VansPcgMask* exclusionMask,
		const VansPcgBounds& outputBounds, const VansPcgSurfaceSampler& surface,
		const VansPcgGenerationBudget& budget);
	// 数量模式按整个区域的稳定候选顺序补足目标数，再筛选输出块；Mask 编辑会影响全区。
	// 达到候选预算但未补足时保留合法结果并返回明确的短缺统计，不越过硬实例预算。
	static VansPcgGenerationResult GenerateCount(const VansPcgDistributionSettings& settings,
		const VansPcgMask& densityMask, const VansPcgMask* exclusionMask, std::uint32_t targetCount,
		const VansPcgBounds& outputBounds, const VansPcgSurfaceSampler& surface,
		const VansPcgGenerationBudget& budget);
	static std::uint64_t AuthoredInstanceId(const std::string& region, const std::string& layer, const std::string& instance);
	static std::string PointIdText(std::uint64_t id);
	static bool ReadPointIdText(const std::string& text, std::uint64_t& id);
};
}
