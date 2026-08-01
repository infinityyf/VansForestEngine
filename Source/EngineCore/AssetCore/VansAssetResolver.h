#pragma once

#include "VansAssetDatabase.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansAssetAccessMode
{
	Editor,
	Package
};

struct VansResolvedAsset
{
	VansAssetGuid guid;
	VansAssetType type = VansAssetType::Unknown;
	std::filesystem::path sourcePath;
	std::filesystem::path authoringPath;
	std::filesystem::path artifactPath;
	std::filesystem::path readPath;
	VansAssetArtifactFormat artifactFormat = VansAssetArtifactFormat::None;
	bool indexed = false;
	bool sourceAvailable = false;
	bool artifactAvailable = false;
	bool valid = false;
	std::string error;
};

// 资源读取的统一边界：编辑器可从索引中的源文件导入，包运行时只允许读取索引中的缓存。
class VansAssetResolver
{
public:
	VansAssetResolver(VansAssetAccessMode mode, const std::vector<VansAssetRecord>& records);

	VansAssetAccessMode Mode() const { return m_Mode; }
	VansResolvedAsset Resolve(const std::string& assetGuid, VansAssetType expectedType) const;

private:
	VansAssetAccessMode m_Mode = VansAssetAccessMode::Editor;
	std::unordered_map<std::string, VansAssetRecord> m_RecordsByGuid;
};
}
