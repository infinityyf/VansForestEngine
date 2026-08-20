#include "VansPackageResourcePrewarmer.h"

namespace Vans
{
VansPackageResourcePrewarmResult VansPackageResourcePrewarmer::Prewarm(
    const std::filesystem::path& projectRoot,
    VansAssetDatabase& database,
    VansAssetDatabase& builtInAssetDatabase,
    const VansSceneResourceBuildPlan& resourcePlan)
{
    return VansGraphics::VansSceneResourceArtifactPrewarmer::Prewarm(
        projectRoot,
        database,
        builtInAssetDatabase,
        resourcePlan);
}
}
