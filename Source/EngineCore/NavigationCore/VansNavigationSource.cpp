#include "VansNavigationSource.h"

#include <algorithm>
#include <cstring>

namespace Vans
{
namespace
{
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

class StableHash
{
public:
	template <typename T>
	void Append(const T& value)
	{
		AppendBytes(&value, sizeof(value));
	}

	void Append(const std::string& value)
	{
		const std::uint64_t size = static_cast<std::uint64_t>(value.size());
		Append(size);
		AppendBytes(value.data(), value.size());
	}

	std::uint64_t Value() const { return m_Value; }

private:
	void AppendBytes(const void* data, std::size_t size)
	{
		const auto* bytes = static_cast<const unsigned char*>(data);
		for (std::size_t index = 0; index < size; ++index)
		{
			m_Value ^= static_cast<std::uint64_t>(bytes[index]);
			m_Value *= kFnvPrime;
		}
	}

	std::uint64_t m_Value = kFnvOffsetBasis;
};
}

std::uint64_t HashNavigationSettings(const VansNavigationSettings& settings)
{
	StableHash hash;
	const VansNavigationBakeSettings& bake = settings.bake;
	hash.Append(bake.cellSize);
	hash.Append(bake.cellHeight);
	hash.Append(bake.agentHeight);
	hash.Append(bake.agentRadius);
	hash.Append(bake.agentMaxClimb);
	hash.Append(bake.agentMaxSlopeDegrees);
	hash.Append(bake.regionMinSize);
	hash.Append(bake.regionMergeSize);
	hash.Append(bake.edgeMaxLength);
	hash.Append(bake.edgeMaxError);
	hash.Append(bake.maximumVerticesPerPolygon);
	const std::uint8_t detailSamplingEnabled =
		bake.detailSamplingEnabled ? 1u : 0u;
	hash.Append(detailSamplingEnabled);
	hash.Append(bake.detailSampleDistance);
	hash.Append(bake.detailSampleMaxError);
	hash.Append(settings.areas.defaultArea);

	std::vector<VansNavigationAreaDefinition> areas = settings.areas.definitions;
	std::sort(areas.begin(), areas.end(), [](const auto& left, const auto& right)
	{
		if (left.id != right.id) return left.id < right.id;
		return left.name < right.name;
	});
	const std::uint64_t areaCount = static_cast<std::uint64_t>(areas.size());
	hash.Append(areaCount);
	for (const VansNavigationAreaDefinition& area : areas)
	{
		hash.Append(area.name);
		hash.Append(area.id);
	}
	return hash.Value();
}

std::uint64_t HashNavigationColliders(
	std::vector<VansNavigationColliderSource> sources)
{
	std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right)
	{
		return left.guid < right.guid;
	});
	sources.erase(std::unique(sources.begin(), sources.end(),
		[](const auto& left, const auto& right) { return left.guid == right.guid; }),
		sources.end());

	StableHash hash;
	const std::uint64_t count = static_cast<std::uint64_t>(sources.size());
	hash.Append(count);
	for (const VansNavigationColliderSource& source : sources)
	{
		hash.Append(source.guid);
		hash.Append(source.sourceHash);
		hash.Append(source.metaHash);
	}
	return hash.Value();
}
}
