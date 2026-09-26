#include "VansDerivedArtifactLayout.h"
#include "VansAssetDatabase.h"

namespace Vans
{
VansDerivedArtifactLocation VansDerivedArtifactLayout::ModelLodAuthoringAssetSet(
    const std::filesystem::path& assetsRoot,std::string_view buildKey)
{
    const auto leaf=std::filesystem::u8path(buildKey);
    if(assetsRoot.empty()||leaf.empty()||leaf!=leaf.filename()||leaf=="."||leaf=="..")return {};
    return {(assetsRoot/"Generated"/"ModelLOD"/leaf).lexically_normal(),
        VansDerivedArtifactClass::AuthoringAssetSet};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::PcgSplineRegenerableCache(
    const std::filesystem::path& projectRoot,VansAssetGuid splineGuid)
{
    if(projectRoot.empty()||!splineGuid.IsValid())return {};
    return {(projectRoot/"Library"/"PCG"/"Splines"/
        (splineGuid.ToString()+".pcgfields")).lexically_normal(),
        VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::ImportedRuntimeCache(
    const std::filesystem::path& artifactRoot,VansAssetType type,VansAssetGuid assetGuid)
{
    if(artifactRoot.empty()||!assetGuid.IsValid())return {};
    switch(type)
    {
    case VansAssetType::Texture:
        return {(artifactRoot/"Textures"/(assetGuid.ToString()+".vtex")).lexically_normal(),
            VansDerivedArtifactClass::RegenerableCache};
    case VansAssetType::Model:
        return {(artifactRoot/"Meshes"/(assetGuid.ToString()+".vmesh")).lexically_normal(),
            VansDerivedArtifactClass::RegenerableCache};
    default:
        return {};
    }
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::ProjectBuiltInArtifactRoot(
    const std::filesystem::path& artifactRoot)
{
    if(artifactRoot.empty())return {};
    return {(artifactRoot/"Engine").lexically_normal(),
        VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::ProjectShaderCache(
    const std::filesystem::path& artifactRoot)
{
    if(artifactRoot.empty())return {};
    return {(artifactRoot/"Shaders").lexically_normal(),
        VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::EngineShaderCache(
    const std::filesystem::path& engineRoot)
{
    if(engineRoot.empty())return {};
    return {(engineRoot/"Library"/"Artifacts"/"Shaders").lexically_normal(),
        VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::ProjectGameplayCookedAsset(
    const std::filesystem::path& artifactRoot,VansAssetGuid assetGuid,
    const std::filesystem::path& sourceFileName)
{
    if(artifactRoot.empty()||!assetGuid.IsValid()||sourceFileName.empty()||
        sourceFileName!=sourceFileName.filename())return {};
    return {(artifactRoot/"GAF"/assetGuid.ToString()/
        (sourceFileName.string()+".gafcooked")).lexically_normal(),
        VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::PackagedSourceCache(
    VansAssetGuid assetGuid,const std::filesystem::path& sourceFileName,bool directory)
{
    if(!assetGuid.IsValid())return {};
    const std::filesystem::path root=std::filesystem::path("Library")/"Artifacts"/
        "Resources"/assetGuid.ToString();
    if(directory)return {root,VansDerivedArtifactClass::RegenerableCache};
    if(sourceFileName.empty()||sourceFileName!=sourceFileName.filename())return {};
    return {root/sourceFileName,VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::PackagedMetadata(VansAssetGuid assetGuid)
{
    if(!assetGuid.IsValid())return {};
    return {std::filesystem::path("Library")/"Artifacts"/"Metadata"/
        (assetGuid.ToString()+".meta"),VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::PackagedShaderArtifacts()
{
    return {std::filesystem::path("Library")/"Artifacts"/"Shaders",
        VansDerivedArtifactClass::RegenerableCache};
}

VansDerivedArtifactLocation VansDerivedArtifactLayout::PackagedResourcePlanReport()
{
    return {std::filesystem::path("Library")/"Package"/"ResourcePlanReport.json",
        VansDerivedArtifactClass::RegenerableCache};
}
}
