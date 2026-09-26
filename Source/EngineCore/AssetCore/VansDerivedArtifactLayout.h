#pragma once

#include "VansAssetGuid.h"

#include <filesystem>
#include <string_view>

namespace Vans
{
enum class VansAssetType;

enum class VansDerivedArtifactClass
{
    AuthoringAssetSet,
    RegenerableCache
};

struct VansDerivedArtifactLocation
{
    std::filesystem::path path;
    VansDerivedArtifactClass artifactClass=VansDerivedArtifactClass::RegenerableCache;

    explicit operator bool() const { return !path.empty(); }
};

// 派生产物路径与持久性类别的单一策略。只做路径计算，不拥有各格式的指纹和 codec。
class VansDerivedArtifactLayout
{
public:
    static VansDerivedArtifactLocation ModelLodAuthoringAssetSet(
        const std::filesystem::path& assetsRoot,std::string_view buildKey);
    static VansDerivedArtifactLocation PcgSplineRegenerableCache(
        const std::filesystem::path& projectRoot,VansAssetGuid splineGuid);
    static VansDerivedArtifactLocation ImportedRuntimeCache(
        const std::filesystem::path& artifactRoot,VansAssetType type,VansAssetGuid assetGuid);
    static VansDerivedArtifactLocation ProjectBuiltInArtifactRoot(
        const std::filesystem::path& artifactRoot);
    static VansDerivedArtifactLocation ProjectShaderCache(
        const std::filesystem::path& artifactRoot);
    static VansDerivedArtifactLocation EngineShaderCache(
        const std::filesystem::path& engineRoot);
    static VansDerivedArtifactLocation ProjectGameplayCookedAsset(
        const std::filesystem::path& artifactRoot,VansAssetGuid assetGuid,
        const std::filesystem::path& sourceFileName);
    static VansDerivedArtifactLocation PackagedSourceCache(
        VansAssetGuid assetGuid,const std::filesystem::path& sourceFileName,bool directory);
    static VansDerivedArtifactLocation PackagedMetadata(VansAssetGuid assetGuid);
    static VansDerivedArtifactLocation PackagedShaderArtifacts();
    static VansDerivedArtifactLocation PackagedResourcePlanReport();
};
}
