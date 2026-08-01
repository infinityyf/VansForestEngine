#include "VansAssetResolver.h"

namespace Vans
{
VansAssetResolver::VansAssetResolver(
	VansAssetAccessMode mode,
	const std::vector<VansAssetRecord>& records)
	: m_Mode(mode)
{
	for (const VansAssetRecord& record : records)
		m_RecordsByGuid[record.guid.ToString()] = record;
}

VansResolvedAsset VansAssetResolver::Resolve(
	const std::string& assetGuid,
	VansAssetType expectedType) const
{
	VansResolvedAsset resolved;
	if (assetGuid.empty())
	{
		resolved.error = "Resource request has no asset guid";
		return resolved;
	}

	const auto found = m_RecordsByGuid.find(assetGuid);
	if (found == m_RecordsByGuid.end())
	{
		resolved.error = "Asset guid is not present in the resource index: " + assetGuid;
		return resolved;
	}

	const VansAssetRecord& record = found->second;
	resolved.guid = record.guid;
	resolved.type = record.type;
	resolved.indexed = true;
	if (record.state == VansAssetState::Missing)
	{
		resolved.error = "Indexed asset is marked missing: " + assetGuid;
		return resolved;
	}
	if (expectedType != VansAssetType::Unknown && record.type != expectedType)
	{
		resolved.error = "Indexed asset type does not match resource request: " + assetGuid;
		return resolved;
	}

	std::error_code ec;
	resolved.sourcePath = record.sourcePath;
	resolved.authoringPath = record.authoringPath;
	resolved.sourceAvailable = !record.sourcePath.empty() &&
		std::filesystem::exists(record.sourcePath, ec);
	ec.clear();
	resolved.artifactPath = record.artifactPath;
	resolved.artifactFormat = record.artifactFormat;
	resolved.artifactAvailable = !record.artifactPath.empty() &&
		std::filesystem::exists(record.artifactPath, ec);

	if (m_Mode == VansAssetAccessMode::Package)
	{
		if (!resolved.artifactAvailable)
		{
			resolved.error = "Packaged asset has no indexed cache artifact: " + assetGuid;
			return resolved;
		}
		resolved.readPath = resolved.artifactPath;
		resolved.valid = true;
		return resolved;
	}

	if (resolved.artifactAvailable)
		resolved.readPath = resolved.artifactPath;
	else if (resolved.sourceAvailable)
		resolved.readPath = resolved.sourcePath;
	else
		resolved.error = "Indexed editor asset has neither source nor cache: " + assetGuid;
	resolved.valid = !resolved.readPath.empty();
	return resolved;
}
}
