#pragma once

#include "../RenderCore/SceneBuild/VansSceneResourceArtifactPrewarmer.h"

namespace Vans
{
using VansPackageResourcePrewarmResult =
    VansGraphics::VansSceneResourceArtifactPrewarmResult;

class VansPackageResourcePrewarmer
{
public:
    static VansPackageResourcePrewarmResult Prewarm(
        const std::filesystem::path& projectRoot,
        VansAssetDatabase& database,
        VansAssetDatabase& builtInAssetDatabase,
        const VansSceneResourceBuildPlan& resourcePlan);
};
}
