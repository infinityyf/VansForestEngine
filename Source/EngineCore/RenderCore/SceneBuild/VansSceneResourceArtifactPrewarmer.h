#pragma once

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../SceneCore/VansSceneResourcePlan.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace VansGraphics
{
struct VansSceneResourceArtifactPrewarmResult
{
    std::uint32_t meshChecked = 0;
    std::uint32_t meshCooked = 0;
    std::uint32_t meshUpToDate = 0;
    std::uint32_t meshNotEligible = 0;
    std::uint32_t meshFailed = 0;
    std::uint32_t textureChecked = 0;
    std::uint32_t textureCooked = 0;
    std::uint32_t textureUpToDate = 0;
    std::uint32_t textureNotEligible = 0;
    std::uint32_t textureFailed = 0;
    std::vector<std::string> errors;

    bool ChangedArtifacts() const
    {
        return meshCooked > 0 || textureCooked > 0;
    }

    bool Succeeded() const
    {
        return meshFailed == 0 && textureFailed == 0;
    }
};

class VansSceneResourceArtifactPrewarmer
{
public:
    static VansSceneResourceArtifactPrewarmResult Prewarm(
        const std::filesystem::path& projectRoot,
        Vans::VansAssetDatabase& database,
        Vans::VansAssetDatabase& builtInAssetDatabase,
        const Vans::VansSceneResourceBuildPlan& resourcePlan);
};
}
