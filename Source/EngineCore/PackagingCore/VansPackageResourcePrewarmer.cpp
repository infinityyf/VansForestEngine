#include "VansPackageResourcePrewarmer.h"

#include "../AssetCore/Importers/VansTextureCooker.h"
#include "../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../Util/VansLog.h"

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
		Vans::VansPackageResourcePrewarmResult& result,
		const std::string& message)
	{
		result.errors.push_back(message);
		VANS_LOG_WARN("[PackageResourcePrewarm] " << message);
	}

	std::optional<Vans::VansAssetRecord> FindIndexedAsset(
		const Vans::VansAssetDatabase& projectDatabase,
		const Vans::VansAssetDatabase& builtInDatabase,
		Vans::VansAssetGuid guid,
		const Vans::VansAssetDatabase*& owner)
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

namespace Vans
{
	VansPackageResourcePrewarmResult VansPackageResourcePrewarmer::Prewarm(
		const fs::path& projectRoot,
		const VansAssetDatabase& database,
		const VansAssetDatabase& builtInAssetDatabase,
		const VansSceneResourceBuildPlan& resourcePlan)
	{
		VansPackageResourcePrewarmResult result;

		for (const VansSceneMeshResourceRequest& request : resourcePlan.meshes)
		{
			++result.meshChecked;
			VansAssetGuid guid;
			if (!VansAssetGuid::TryParse(request.assetGuid, guid))
			{
				++result.meshFailed;
				PushError(result, "Cannot parse mesh guid: " + request.name);
				continue;
			}

			const VansAssetDatabase* owner = nullptr;
			const std::optional<VansAssetRecord> record = FindIndexedAsset(database, builtInAssetDatabase, guid, owner);
			if (!record || record->state == VansAssetState::Missing)
			{
				++result.meshFailed;
				PushError(result, "Mesh asset record is missing: " + request.name);
				continue;
			}

			const fs::path sourcePath = record->sourcePath;
			const fs::path cachePath = request.artifactPath.empty()
				? owner->ArtifactRoot() / "Meshes" / (request.assetGuid + ".vmesh")
				: ResolveProjectPath(projectRoot, request.artifactPath);

			if (VansGraphics::VansMesh::IsMeshCacheCurrent(
				cachePath.string(),
				sourcePath.string(),
				request.needTangent,
				request.loadMultiMesh,
				request.scaleFactor))
			{
				++result.meshUpToDate;
				continue;
			}

			std::string error;
			const VansGraphics::VansMeshCacheBuildStatus buildStatus = VansGraphics::VansMesh::BuildMeshCache(
				sourcePath.string(),
				request.needTangent,
				request.loadMultiMesh,
				request.scaleFactor,
				request.rebuildIdentityBoneOffsetsFromHierarchy,
				request.remapWeaponAttachmentBonesToHands,
				cachePath.string(),
				error);

			switch (buildStatus)
			{
			case VansGraphics::VansMeshCacheBuildStatus::Current:
				++result.meshUpToDate;
				break;
			case VansGraphics::VansMeshCacheBuildStatus::Cooked:
				++result.meshCooked;
				break;
			case VansGraphics::VansMeshCacheBuildStatus::NotEligible:
				++result.meshNotEligible;
				break;
			case VansGraphics::VansMeshCacheBuildStatus::Failed:
			default:
				++result.meshFailed;
				PushError(result, error.empty()
					? "Failed to cook mesh cache: " + sourcePath.string()
					: error);
				break;
			}
		}

		for (const VansSceneTextureResourceRequest& request : resourcePlan.textures)
		{
			++result.textureChecked;
			if (request.textureType != kSceneTexture2D)
			{
				++result.textureNotEligible;
				continue;
			}

			VansAssetGuid guid;
			if (!VansAssetGuid::TryParse(request.assetGuid, guid))
			{
				++result.textureFailed;
				PushError(result, "Cannot parse texture guid: " + request.name);
				continue;
			}

			const VansAssetDatabase* owner = nullptr;
			const std::optional<VansAssetRecord> record = FindIndexedAsset(database, builtInAssetDatabase, guid, owner);
			if (!record || record->state == VansAssetState::Missing)
			{
				++result.textureFailed;
				PushError(result, "Texture asset record is missing: " + request.name);
				continue;
			}

			VansAssetMeta meta;
			std::string metaError;
			if (!VansAssetMetaStorage::Load(record->metaPath, meta, metaError))
			{
				++result.textureFailed;
				PushError(result, metaError);
				continue;
			}

			const VansTextureCookResult cook = VansTextureCooker::CookIfNeeded(
				record->sourcePath,
				record->metaPath,
				meta,
				owner->ArtifactRoot());

			switch (cook.status)
			{
			case VansTextureCookStatus::Cooked:
				++result.textureCooked;
				VANS_LOG("[PackageResourcePrewarm] Cooked texture "
					<< record->sourcePath.string() << " -> " << cook.artifactPath.string());
				break;
			case VansTextureCookStatus::UpToDate:
				++result.textureUpToDate;
				break;
			case VansTextureCookStatus::NotEligible:
				++result.textureNotEligible;
				break;
			case VansTextureCookStatus::Failed:
				++result.textureFailed;
				PushError(result, cook.error.empty()
					? "Failed to cook texture cache: " + record->sourcePath.string()
					: cook.error);
				break;
			default:
				++result.textureFailed;
				PushError(result, "Unknown texture cook status: " + record->sourcePath.string());
				break;
			}
		}

		VANS_LOG("[PackageResourcePrewarm] meshes checked=" << result.meshChecked
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
