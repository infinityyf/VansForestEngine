#include "VansSceneResourceArtifactPrewarmer.h"

#include "../VulkanCore/VansMesh.h"
#include "../../Util/VansLog.h"

#include <optional>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
constexpr int kSceneTexture2D = 0;

fs::path ResolveProjectPath(const fs::path& projectRoot, const std::string& value)
{
    const fs::path path(value);
    if (path.is_absolute())
        return path.lexically_normal();
    return (projectRoot / path).lexically_normal();
}

void PushError(
    VansGraphics::VansSceneResourceArtifactPrewarmResult& result,
    const std::string& message)
{
    result.errors.push_back(message);
    VANS_LOG_WARN("[ResourceArtifactPrewarm] " << message);
}

std::optional<Vans::VansAssetRecord> FindIndexedAsset(
    Vans::VansAssetDatabase& projectDatabase,
    Vans::VansAssetDatabase& builtInDatabase,
    Vans::VansAssetGuid guid,
    Vans::VansAssetDatabase*& owner)
{
    if (const auto record = projectDatabase.Find(guid))
    {
        owner = &projectDatabase;
        return record;
    }
    if (const auto record = builtInDatabase.Find(guid))
    {
        owner = &builtInDatabase;
        return record;
    }
    owner = nullptr;
    return std::nullopt;
}
}

namespace VansGraphics
{
VansSceneResourceArtifactPrewarmResult VansSceneResourceArtifactPrewarmer::Prewarm(
    const fs::path& projectRoot,
    Vans::VansAssetDatabase& database,
    Vans::VansAssetDatabase& builtInAssetDatabase,
    const Vans::VansSceneResourceBuildPlan& resourcePlan)
{
    VansSceneResourceArtifactPrewarmResult result;

    for (const Vans::VansSceneMeshResourceRequest& request : resourcePlan.meshes)
    {
        ++result.meshChecked;
        Vans::VansAssetGuid guid;
        if (!Vans::VansAssetGuid::TryParse(request.assetGuid, guid))
        {
            ++result.meshFailed;
            PushError(result, "Cannot parse mesh guid: " + request.name);
            continue;
        }

        Vans::VansAssetDatabase* owner = nullptr;
        const std::optional<Vans::VansAssetRecord> record =
            FindIndexedAsset(database, builtInAssetDatabase, guid, owner);
        if (!record || record->state == Vans::VansAssetState::Missing)
        {
            ++result.meshFailed;
            PushError(result, "Mesh asset record is missing: " + request.name);
            continue;
        }

        const fs::path cachePath = request.artifactPath.empty()
            ? owner->ArtifactRoot() / "Meshes" / (request.assetGuid + ".vmesh")
            : ResolveProjectPath(projectRoot, request.artifactPath);
        const bool importTangents = Vans::RequiresMeshTangentImport(request);
        std::string error;
        const VansMeshCacheBuildStatus status = VansMesh::BuildMeshCache(
            record->sourcePath.string(),
            importTangents,
            request.loadMultiMesh,
            request.scaleFactor,
            request.skeletalImport,
            cachePath.string(),
            error);

        switch (status)
        {
        case VansMeshCacheBuildStatus::Current:
            ++result.meshUpToDate;
            owner->UpdateImportedArtifact(guid, cachePath);
            break;
        case VansMeshCacheBuildStatus::Cooked:
            ++result.meshCooked;
            owner->UpdateImportedArtifact(guid, cachePath);
            VANS_LOG("[ResourceArtifactPrewarm] Cooked mesh "
                << record->sourcePath.string() << " -> " << cachePath.string());
            break;
        case VansMeshCacheBuildStatus::NotEligible:
            ++result.meshNotEligible;
            owner->UpdateImportedArtifact(guid, {});
            break;
        case VansMeshCacheBuildStatus::Failed:
        default:
            ++result.meshFailed;
            owner->UpdateImportedArtifact(guid, {}, error);
            PushError(result, error.empty()
                ? "Failed to cook mesh cache: " + record->sourcePath.string()
                : error);
            break;
        }
    }

    for (const Vans::VansSceneTextureResourceRequest& request : resourcePlan.textures)
    {
        ++result.textureChecked;
        if (request.textureType != kSceneTexture2D)
        {
            ++result.textureNotEligible;
            continue;
        }

        Vans::VansAssetGuid guid;
        if (!Vans::VansAssetGuid::TryParse(request.assetGuid, guid))
        {
            ++result.textureFailed;
            PushError(result, "Cannot parse texture guid: " + request.name);
            continue;
        }

        Vans::VansAssetDatabase* owner = nullptr;
        const std::optional<Vans::VansAssetRecord> record =
            FindIndexedAsset(database, builtInAssetDatabase, guid, owner);
        if (!record || record->state == Vans::VansAssetState::Missing)
        {
            ++result.textureFailed;
            PushError(result, "Texture asset record is missing: " + request.name);
            continue;
        }

        const Vans::VansTextureArtifactEnsureResult ensured = owner->EnsureTextureArtifact(guid);
        switch (ensured.status)
        {
        case Vans::VansTextureArtifactEnsureStatus::Cooked:
            ++result.textureCooked;
            VANS_LOG("[ResourceArtifactPrewarm] Cooked texture "
                << record->sourcePath.string() << " -> " << ensured.artifactPath.string());
            break;
        case Vans::VansTextureArtifactEnsureStatus::UpToDate:
            ++result.textureUpToDate;
            break;
        case Vans::VansTextureArtifactEnsureStatus::NotEligible:
            ++result.textureNotEligible;
            break;
        case Vans::VansTextureArtifactEnsureStatus::Failed:
        default:
            ++result.textureFailed;
            PushError(result, ensured.error.empty()
                ? "Failed to cook texture cache: " + record->sourcePath.string()
                : ensured.error);
            break;
        }
    }

    VANS_LOG("[ResourceArtifactPrewarm] meshes checked=" << result.meshChecked
        << " cooked=" << result.meshCooked
        << " upToDate=" << result.meshUpToDate
        << " notEligible=" << result.meshNotEligible
        << " failed=" << result.meshFailed
        << ", textures checked=" << result.textureChecked
        << " cooked=" << result.textureCooked
        << " upToDate=" << result.textureUpToDate
        << " notEligible=" << result.textureNotEligible
        << " failed=" << result.textureFailed);
    return result;
}
}
