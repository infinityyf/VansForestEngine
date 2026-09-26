#include "VansPcgPointGenerator.h"
#include "VansPcgDeterminism.h"
#include "VansPcgInfluence.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace Vans
{
std::vector<std::string> ValidatePcgPlacement(const VansPcgPlacementSettings& settings)
{
	std::vector<std::string> errors;
	const auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
	const auto nonnegative = [](float value) { return std::isfinite(value) && value >= 0; };
	if (!nonnegative(settings.density) || !unit(settings.positionJitter) || !nonnegative(settings.minimumSpacing) ||
		!unit(settings.normalAlignment) || !nonnegative(settings.maximumTiltDegrees) || settings.maximumTiltDegrees > 180 ||
		!std::isfinite(settings.rootOffset) || std::abs(static_cast<double>(settings.rootOffset)) >= 1073741824.0 ||
		!std::isfinite(settings.yawMinDegrees) || !std::isfinite(settings.yawMaxDegrees) ||
		settings.yawMaxDegrees < settings.yawMinDegrees || !unit(settings.maskThreshold) || !nonnegative(settings.maskMultiplier))
		errors.push_back("PCG distribution parameters are invalid");
	for (std::size_t axis = 0; axis < 3; ++axis)
		if (!std::isfinite(settings.scaleMin[axis]) || !std::isfinite(settings.scaleMax[axis]) ||
			settings.scaleMin[axis] <= 0 || settings.scaleMax[axis] < settings.scaleMin[axis])
			errors.push_back("PCG scale ranges must be finite, ordered and positive");
	if (settings.uniformScale && (settings.scaleMin[0] != settings.scaleMin[1] || settings.scaleMin[0] != settings.scaleMin[2] ||
		settings.scaleMax[0] != settings.scaleMax[1] || settings.scaleMax[0] != settings.scaleMax[2]))
		errors.push_back("Uniform scale requires equal ranges on all axes");
	return errors;
}

namespace
{
bool Nonnegative(float value) { return std::isfinite(value) && value >= 0; }

bool Owns(const VansPcgMask& mask, const VansPcgDistributionSettings& settings)
{
	return mask.IsValid() && mask.target.regionId == settings.regionId && mask.target.layerId == settings.layerId;
}

struct Candidate
{
	VansPcgPoint point;
	std::uint64_t priority = 0;
	std::int32_t cellX = 0;
	std::int32_t cellZ = 0;
	std::uint32_t rank = 0;
	float anchorX = 0;
	float anchorZ = 0;
	float radius = 0;
};

bool Precedes(const Candidate& a, const Candidate& b)
{
	if (a.priority != b.priority) return a.priority < b.priority;
	if (a.cellX != b.cellX) return a.cellX < b.cellX;
	if (a.cellZ != b.cellZ) return a.cellZ < b.cellZ;
	return a.rank < b.rank;
}

struct Bucket
{
	std::int64_t x;
	std::int64_t z;
	bool operator==(const Bucket& other) const { return x == other.x && z == other.z; }
};

struct BucketHash
{
	std::size_t operator()(const Bucket& key) const
	{
		return static_cast<std::size_t>(PcgMix64(static_cast<std::uint64_t>(key.x)) ^ PcgMix64(static_cast<std::uint64_t>(key.z) + 31));
	}
};

bool MakeTransform(const VansPcgDistributionSettings& settings, const VansPcgSurfacePoint& surface,
	Candidate& candidate)
{
	const glm::vec3 normal(surface.normal[0], surface.normal[1], surface.normal[2]);
	const float length = glm::length(normal);
	if (!std::isfinite(surface.height) || !std::isfinite(length) || length < 0.000001f) return false;
	const glm::vec3 n = normal / length;
	const glm::vec3 position = glm::vec3(candidate.anchorX, surface.height, candidate.anchorZ) + n * settings.rootOffset;
	if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
	candidate.point.position = { position.x, position.y, position.z };
	const float angle = std::min(std::acos(std::clamp(n.y, -1.0f, 1.0f)) * settings.normalAlignment,
		glm::radians(settings.maximumTiltDegrees));
	glm::vec3 axis = glm::cross(glm::vec3(0, 1, 0), n);
	axis = glm::length(axis) > 0.000001f ? glm::normalize(axis) : glm::vec3(1, 0, 0);
	const float yaw = static_cast<float>(settings.yawMinDegrees +
		(static_cast<double>(settings.yawMaxDegrees) - settings.yawMinDegrees) * PcgRandom01(candidate.point.id, 5));
	const glm::quat rotation = glm::angleAxis(angle, axis) * glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0));
	candidate.point.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
	return true;
}
bool PrepareGeneration(const VansPcgDistributionSettings& settings,
	const VansPcgMask& densityMask, const VansPcgMask* exclusionMask,
	const VansPcgBounds& outputBounds, const VansPcgSurfaceSampler& surface,
	VansPcgGenerationResult& result, std::vector<std::size_t>& variants, double& weightSum, double& largestRadius)
{
	const auto fail = [&](const char* error) { result.error = error; return false; };
	if (settings.regionId.empty() || settings.layerId.empty() || !settings.bounds.IsValid() || !outputBounds.IsValid() ||
		!Owns(densityMask, settings) || (exclusionMask && (!Owns(*exclusionMask, settings) ||
			exclusionMask->target.maskId == densityMask.target.maskId)))
		return fail("PCG generation requires valid bounds and independently owned density/exclusion masks");
	const auto placementErrors = ValidatePcgPlacement(settings);
	if (!placementErrors.empty()) return fail(placementErrors.front().c_str());
	// 单位网格坐标使用整数身份；拒绝不可表达的输入，而不是截断到另一个位置。
	for (std::size_t axis = 0; axis < 2; ++axis)
		if (std::abs(static_cast<double>(settings.bounds.min[axis])) >= 1073741824.0 ||
			std::abs(static_cast<double>(settings.bounds.max[axis])) >= 1073741824.0)
			return fail("PCG bounds exceed the supported stable cell coordinates");
	if (!surface) return fail("PCG generation requires an explicitly bound surface sampler");
	variants.resize(settings.variants.size());
	std::iota(variants.begin(), variants.end(), 0);
	std::sort(variants.begin(), variants.end(), [&](std::size_t a, std::size_t b) { return settings.variants[a].id < settings.variants[b].id; });
	weightSum = 0;
	largestRadius = 0;
	for (const auto index : variants)
	{
		const auto& variant = settings.variants[index];
		if (variant.id.empty() || (!result.variantIds.empty() && result.variantIds.back() == variant.id) ||
			!Nonnegative(variant.weight) || !Nonnegative(variant.footprintRadius))
			return fail("PCG model variants require unique stable IDs and valid weights/footprints");
		result.variantIds.push_back(variant.id);
		weightSum += variant.weight;
	}
	largestRadius = PcgInfluenceRadius(settings, settings.variants, VansPcgVariantInfluence::PositiveWeight);
	return true;
}

enum class Evaluation { Rejected, Ready, Error };

Evaluation EvaluateCandidate(const VansPcgDistributionSettings& settings, const VansPcgMask& densityMask,
	const VansPcgMask* exclusionMask, const VansPcgSurfaceSampler& surface,
	const std::vector<std::size_t>& variants, double weightSum, double acceptance,
	Candidate& candidate, VansPcgGenerationResult& result)
{
	if (!VansPcgPointGenerator::PassesMask(settings, densityMask, exclusionMask,
		candidate.anchorX, candidate.anchorZ, candidate.point.id, acceptance))
	{ ++result.stats.maskRejected; return Evaluation::Rejected; }
	const double choice = PcgRandom01(candidate.point.id, 3) * weightSum;
	double cumulative = 0;
	for (std::size_t v = 0; v < variants.size(); ++v)
	{
		cumulative += settings.variants[variants[v]].weight;
		if (choice < cumulative) { candidate.point.variantIndex = static_cast<std::uint32_t>(v); break; }
	}
	for (std::size_t axis = 0; axis < 3; ++axis)
		candidate.point.scale[axis] = static_cast<float>(settings.scaleMin[axis] +
			(static_cast<double>(settings.scaleMax[axis]) - settings.scaleMin[axis]) *
			PcgRandom01(candidate.point.id, settings.uniformScale ? 4 : 4 + axis * 11));
	candidate.radius = settings.variants[variants[candidate.point.variantIndex]].footprintRadius *
		std::max(candidate.point.scale[0], candidate.point.scale[2]);
	VansPcgSurfacePoint sample;
	if (!surface(candidate.anchorX, candidate.anchorZ, sample)) { ++result.stats.surfaceRejected; return Evaluation::Rejected; }
	if (!MakeTransform(settings, sample, candidate))
	{ result.error = "PCG surface returned an invalid height or normal"; return Evaluation::Error; }
	candidate.priority = PcgMix64(candidate.point.id ^ 0xa0761d6478bd642full);
	return Evaluation::Ready;
}

}

bool VansPcgPointGenerator::PassesMask(const VansPcgPlacementSettings& settings,
	const VansPcgMask& densityMask, const VansPcgMask* exclusionMask,
	float x, float z, std::uint64_t id, double acceptance)
{
	// 反转只作用于已定义的 Mask 范围，范围外不能把“无数据”变成全白。
	if (!densityMask.bounds.Contains(x, z)) return false;
	double maskValue = densityMask.Sample(x, z);
	if (settings.invertMask) maskValue = 1 - maskValue;
	maskValue = std::clamp(maskValue * settings.maskMultiplier, 0.0, 1.0);
	if (maskValue < settings.maskThreshold) maskValue = 0;
	if (exclusionMask) maskValue *= 1 - exclusionMask->Sample(x, z);
	return PcgRandom01(id, 2) < acceptance * maskValue;
}

VansPcgGenerationResult VansPcgPointGenerator::GenerateDensity(const VansPcgDistributionSettings& settings,
	const VansPcgMask& densityMask, const VansPcgMask* exclusionMask,
	const VansPcgBounds& outputBounds, const VansPcgSurfaceSampler& surface,
	const VansPcgGenerationBudget& budget)
{
	VansPcgGenerationResult result;
	const auto fail = [&](const char* error) {
		result.error = error;
		result.points.clear();
		return result;
	};
	std::vector<std::size_t> variants;
	double weightSum = 0, largestRadius = 0;
	if (!PrepareGeneration(settings, densityMask, exclusionMask, outputBounds, surface, result, variants, weightSum, largestRadius)) return result;
	if (settings.density == 0) return result;
	if (largestRadius > 1073741824.0) return fail("PCG scaled footprint exceeds the supported world range");
	if (weightSum <= 0 || !std::isfinite(weightSum)) return fail("PCG generation requires a user-configured model variant with positive weight");
	if (!budget.maxCandidates || !budget.maxInstances) return fail("PCG generation requires a nonzero work and instance budget");
	if (settings.density > budget.maxCandidates || settings.density > 4294967294.0)
		return fail("PCG density exceeds the candidate work budget");
	VansPcgBounds owned{ { std::max(outputBounds.min[0], settings.bounds.min[0]), std::max(outputBounds.min[1], settings.bounds.min[1]) },
		{ std::min(outputBounds.max[0], settings.bounds.max[0]), std::min(outputBounds.max[1], settings.bounds.max[1]) } };
	if (!owned.IsValid()) return result;
	const double spacingDistance = PcgSpacingHalo(settings, largestRadius);
	const double halo = PcgGenerationHalo(settings, largestRadius);
	const auto minX = static_cast<std::int32_t>(std::floor(std::max(static_cast<double>(settings.bounds.min[0]), owned.min[0] - halo)));
	const auto minZ = static_cast<std::int32_t>(std::floor(std::max(static_cast<double>(settings.bounds.min[1]), owned.min[1] - halo)));
	const auto maxX = static_cast<std::int32_t>(std::ceil(std::min(static_cast<double>(settings.bounds.max[0]), owned.max[0] + halo)));
	const auto maxZ = static_cast<std::int32_t>(std::ceil(std::min(static_cast<double>(settings.bounds.max[1]), owned.max[1] + halo)));
	const auto ranks = static_cast<std::uint32_t>(std::ceil(settings.density));
	const double required = static_cast<double>(static_cast<std::int64_t>(maxX) - minX) *
		(static_cast<std::int64_t>(maxZ) - minZ) * ranks;
	if (required > static_cast<double>(budget.maxCandidates)) return fail("PCG candidate work budget exceeded; no partial result was published");
	const std::uint64_t identity = PcgMix64(settings.seed) ^ PcgMix64(PcgTextHash(settings.regionId)) ^
		PcgMix64(PcgTextHash(settings.layerId) + 17);
	std::vector<Candidate> candidates;
	for (std::int32_t cz = minZ; cz < maxZ; ++cz) for (std::int32_t cx = minX; cx < maxX; ++cx)
		for (std::uint32_t rank = 0; rank < ranks; ++rank)
		{
			++result.stats.candidates;
			Candidate candidate;
			candidate.cellX = cx;
			candidate.cellZ = cz;
			candidate.rank = rank;
			candidate.point.id = PcgMix64(identity ^ PcgMix64(static_cast<std::uint64_t>(cx)) ^
				PcgMix64(static_cast<std::uint64_t>(cz) + 0x9e3779b97f4a7c15ull) ^
				PcgMix64(static_cast<std::uint64_t>(rank) + 701));
			const auto coordinate = [&](std::uint32_t base, std::uint64_t channel) {
				return PcgRadicalInverse(static_cast<std::uint64_t>(rank) + 1, base) * (1 - settings.positionJitter) +
					PcgRandom01(candidate.point.id, channel) * settings.positionJitter;
			};
			candidate.anchorX = static_cast<float>(cx + coordinate(2, 0));
			candidate.anchorZ = static_cast<float>(cz + coordinate(3, 1));
			if (!settings.bounds.Contains(candidate.anchorX, candidate.anchorZ)) continue;
			const auto evaluated = EvaluateCandidate(settings, densityMask, exclusionMask, surface, variants, weightSum,
				std::min(1.0, static_cast<double>(settings.density) - rank), candidate, result);
			if (evaluated == Evaluation::Error) return result;
			if (evaluated == Evaluation::Rejected) continue;
			// 没有间距约束时无需保存第二份完整候选数组，大面积密度草直接输出。
			if (spacingDistance == 0) {
				if (!owned.Contains(candidate.anchorX, candidate.anchorZ)) continue;
				if (result.points.size() >= budget.maxInstances) return fail("PCG instance budget exceeded; no partial result was published");
				result.points.push_back(candidate.point);
			} else candidates.push_back(candidate);
		}
	const double bucketSize = std::max(spacingDistance, 0.0001);
	const auto bucketFor = [&](const Candidate& candidate) {
		return Bucket{ static_cast<std::int64_t>(std::floor(candidate.point.position[0] / bucketSize)),
			static_cast<std::int64_t>(std::floor(candidate.point.position[2] / bucketSize)) };
	};
	std::unordered_map<Bucket, std::vector<std::size_t>, BucketHash> buckets;
	if (spacingDistance > 0)
		for (std::size_t index = 0; index < candidates.size(); ++index) buckets[bucketFor(candidates[index])].push_back(index);
	for (const auto& candidate : candidates)
	{
		if (!owned.Contains(candidate.anchorX, candidate.anchorZ)) continue;
		bool blocked = false;
		if (spacingDistance > 0)
		{
			const auto center = bucketFor(candidate);
			for (int dz = -1; dz <= 1 && !blocked; ++dz) for (int dx = -1; dx <= 1 && !blocked; ++dx)
			{
				const auto found = buckets.find({ center.x + dx, center.z + dz });
				if (found == buckets.end()) continue;
				for (const auto index : found->second)
				{
					const auto& other = candidates[index];
					++result.stats.neighborQueries;
					if (!Precedes(other, candidate)) continue;
					const double minimum = std::max(static_cast<double>(settings.minimumSpacing),
						static_cast<double>(candidate.radius) + other.radius);
					const double x = static_cast<double>(candidate.point.position[0]) - other.point.position[0];
					const double z = static_cast<double>(candidate.point.position[2]) - other.point.position[2];
					if (x * x + z * z < minimum * minimum) { blocked = true; break; }
				}
			}
		}
		if (blocked) { ++result.stats.spacingRejected; continue; }
		if (result.points.size() >= budget.maxInstances) return fail("PCG instance budget exceeded; no partial result was published");
		result.points.push_back(candidate.point);
	}
	// 一跳优先级冲突判定不依赖遍历或作业完成顺序，邻域宽度即可覆盖所有依赖。
	std::sort(result.points.begin(), result.points.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	result.stats.generatedCount = result.points.size();
	return result;
}

VansPcgGenerationResult VansPcgPointGenerator::GenerateCount(const VansPcgDistributionSettings& settings,
	const VansPcgMask& densityMask, const VansPcgMask* exclusionMask, std::uint32_t targetCount,
	const VansPcgBounds& outputBounds, const VansPcgSurfaceSampler& surface, const VansPcgGenerationBudget& budget)
{
	VansPcgGenerationResult result;
	result.stats.requestedCount = targetCount;
	const auto fail = [&](const char* error) { result.error = error; result.points.clear(); return result; };
	std::vector<std::size_t> variants;
	double weightSum = 0, largestRadius = 0;
	if (!PrepareGeneration(settings, densityMask, exclusionMask, outputBounds, surface, result, variants, weightSum, largestRadius)) return result;
	if (!targetCount) return result;
	if (!budget.maxCandidates || targetCount > budget.maxInstances)
		return fail("PCG target count requires a candidate budget and must fit the hard instance budget");
	if (largestRadius > 1073741824.0 || weightSum <= 0 || !std::isfinite(weightSum))
		return fail("PCG count generation requires valid weighted variants and bounded footprints");
	const double spacingDistance = PcgSpacingHalo(settings, largestRadius);
	const double bucketSize = std::max(spacingDistance, 0.0001);
	const auto bucketFor = [&](const Candidate& candidate) {
		return Bucket{ static_cast<std::int64_t>(std::floor(candidate.point.position[0] / bucketSize)),
			static_cast<std::int64_t>(std::floor(candidate.point.position[2] / bucketSize)) };
	};
	std::unordered_map<Bucket, std::vector<std::size_t>, BucketHash> buckets;
	std::vector<Candidate> accepted;
	const std::uint64_t identity = PcgMix64(settings.seed) ^ PcgMix64(PcgTextHash(settings.regionId)) ^
		PcgMix64(PcgTextHash(settings.layerId) + 17) ^ 0xe7037ed1a0b428dbull;
	for (std::uint64_t ordinal = 0; ordinal < budget.maxCandidates && accepted.size() < targetCount; ++ordinal)
	{
		++result.stats.candidates;
		Candidate candidate;
		candidate.point.id = PcgMix64(identity ^ PcgMix64(ordinal + 701));
		const auto coordinate = [&](std::size_t axis, std::uint32_t base, std::uint64_t channel) {
			const double t = PcgRadicalInverse(ordinal + 1, base) * (1 - settings.positionJitter) +
				PcgRandom01(candidate.point.id, channel) * settings.positionJitter;
			return static_cast<float>(settings.bounds.min[axis] +
				(static_cast<double>(settings.bounds.max[axis]) - settings.bounds.min[axis]) * t);
		};
		candidate.anchorX = coordinate(0, 2, 0);
		candidate.anchorZ = coordinate(1, 3, 1);
		if (!settings.bounds.Contains(candidate.anchorX, candidate.anchorZ)) continue;
		const auto evaluated = EvaluateCandidate(settings, densityMask, exclusionMask, surface, variants, weightSum, 1, candidate, result);
		if (evaluated == Evaluation::Error) return result;
		if (evaluated == Evaluation::Rejected) continue;
		bool blocked = false;
		const auto center = bucketFor(candidate);
		if (spacingDistance > 0)
			for (int dz = -1; dz <= 1 && !blocked; ++dz) for (int dx = -1; dx <= 1 && !blocked; ++dx)
			{
				const auto found = buckets.find({ center.x + dx, center.z + dz });
				if (found == buckets.end()) continue;
				for (const auto index : found->second)
				{
					++result.stats.neighborQueries;
					const auto& other = accepted[index];
					const double minimum = std::max(static_cast<double>(settings.minimumSpacing), static_cast<double>(candidate.radius) + other.radius);
					const double x = static_cast<double>(candidate.point.position[0]) - other.point.position[0];
					const double z = static_cast<double>(candidate.point.position[2]) - other.point.position[2];
					if (x * x + z * z < minimum * minimum) { blocked = true; break; }
				}
			}
		if (blocked) { ++result.stats.spacingRejected; continue; }
		if (spacingDistance > 0) buckets[center].push_back(accepted.size());
		accepted.push_back(std::move(candidate));
	}
	result.stats.generatedCount = accepted.size();
	result.stats.candidateBudgetExhausted = accepted.size() < targetCount;
	for (const auto& candidate : accepted)
		if (outputBounds.Contains(candidate.anchorX, candidate.anchorZ)) result.points.push_back(candidate.point);
	std::sort(result.points.begin(), result.points.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	return result;
}

std::uint64_t VansPcgPointGenerator::AuthoredInstanceId(const std::string& region, const std::string& layer, const std::string& instance)
{
	return PcgMix64(PcgTextHash(region) ^ PcgMix64(PcgTextHash(layer)) ^
		PcgMix64(PcgTextHash(instance)) ^ 0x8ebc6af09c88c6e3ull);
}

std::string VansPcgPointGenerator::PointIdText(std::uint64_t id)
{
	std::string result(16, '0');
	constexpr char digits[] = "0123456789abcdef";
	for (int i = 15; i >= 0; --i) { result[i] = digits[id & 15]; id >>= 4; }
	return result;
}

bool VansPcgPointGenerator::ReadPointIdText(const std::string& text, std::uint64_t& id)
{
	if (text.size() != 16) return false;
	std::uint64_t parsed = 0;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
	if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0) return false;
	id = parsed;
	return true;
}
}
