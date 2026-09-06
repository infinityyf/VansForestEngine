#include "ModelAssetPlacementPreparationService.h"

#include "ScenePropertyValueBuilders.h"
#include "../Public/IEngineEditorAPI.h"
#include "../../AssetCore/Importers/VansModelImportReport.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/VansAssetMeta.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansScene.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneEntityFactory.h"
#include "../../Util/VansLog.h"

#include <../../GLM/glm.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace Vans::EditorAPI
{
namespace
{
std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string TextureFileKey(const std::string& path)
{
    if (path.empty())
        return {};
    return LowerAscii(std::filesystem::path(path).filename().string());
}

struct GeneratedMaterialLookup
{
    std::vector<std::string> byIndex;
    std::unordered_map<std::string, std::string> byTextureName;
};

GeneratedMaterialLookup BuildGeneratedMaterialLookup(const std::string& modelGuid)
{
    GeneratedMaterialLookup lookup;
    auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
    if (database == nullptr || modelGuid.empty())
        return lookup;

    Vans::VansAssetGuid parsedGuid;
    if (!Vans::VansAssetGuid::TryParse(modelGuid, parsedGuid))
        return lookup;

    const auto record = database->Find(parsedGuid);
    if (!record || record->metaPath.empty())
        return lookup;

    const auto meta = Vans::VansProjectManager::Get().GetAssetObjectRepository()
        .ResolveLatest<Vans::VansAssetMeta>(record->guid);
    if (!meta)
        return lookup;

    const Vans::VansModelImportReport report = Vans::ReadModelImportReport(*meta);
    std::unordered_map<std::string, std::string> textureGuidToName;
    for (const Vans::VansModelImportReportTexture& texture : report.textures)
    {
        std::string name = texture.name.empty()
            ? TextureFileKey(texture.path)
            : TextureFileKey(texture.name);
        if (!texture.guid.empty() && !name.empty())
            textureGuidToName[texture.guid] = name;
    }

    for (const Vans::VansModelImportReportGeneratedMaterial& material : report.generatedMaterials)
    {
        if (material.guid.empty())
            continue;

        lookup.byIndex.push_back(material.guid);
        const auto texIt = textureGuidToName.find(material.textureGuid);
        if (texIt != textureGuidToName.end())
            lookup.byTextureName[texIt->second] = material.guid;
    }

    return lookup;
}

std::string ResolveGeneratedMaterialOverride(
    const GeneratedMaterialLookup& lookup,
    const ProjectSubmeshInfo& submeshInfo,
    std::uint32_t submeshIndex)
{
    const std::string textureKey = TextureFileKey(submeshInfo.diffuseTexturePath);
    if (!textureKey.empty())
    {
        const auto found = lookup.byTextureName.find(textureKey);
        if (found != lookup.byTextureName.end())
            return found->second;
    }
    if (submeshIndex < lookup.byIndex.size())
        return lookup.byIndex[submeshIndex];
    return {};
}

Vans::SceneModelEntityFactoryRequest BuildSceneRequest(
    const std::string& entityName,
    const std::string& modelGuid,
    const Vec3& worldPosition)
{
    Vans::SceneModelEntityFactoryRequest request;
    request.entityName = entityName;
    request.modelGuid = modelGuid;
    request.position = { worldPosition.x, worldPosition.y, worldPosition.z };
    return request;
}

std::string MakeUniqueEntityName(VansGraphics::VansScene* scene, const std::string& baseName)
{
    if (!scene || baseName.empty())
        return baseName;

    std::string uniqueName = baseName;
    int suffix = 1;
    while (scene->FindObjectByName(uniqueName))
        uniqueName = baseName + "_" + std::to_string(suffix++);
    return uniqueName;
}

}

ModelAssetPlacementPayload ModelAssetPlacementPreparationService::Prepare(
    const ModelAssetPlacementRequest& request,
    IEngineEditorAPI& editorAPI,
    RuntimeSceneHandle sceneHandle)
{
    ModelAssetPlacementPayload payload;
    auto* scene = static_cast<VansGraphics::VansScene*>(sceneHandle);

    Vans::VansAssetGuid guid;
    if (!Vans::VansAssetGuid::TryParse(request.assetGuid, guid))
    {
        payload.message = "Dropped payload is not a valid asset guid";
        return payload;
    }

    auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
    if (!database)
    {
        payload.message = "Project asset database is not available";
        return payload;
    }

    const auto record = database->Find(guid);
    if (!record)
    {
        payload.message = "Dropped asset was not found in the asset database";
        return payload;
    }
    if (record->type != Vans::VansAssetType::Model)
    {
        payload.message = "Dropped asset is not a model";
        return payload;
    }

	const std::string meshName = request.assetGuid;
	const std::string entityBaseName = record->sourcePath.stem().string();
    MeshLoadRequest loadRequest;
    loadRequest.meshName = meshName;
    loadRequest.sourcePath = record->sourcePath.string();
	const MeshLoadResult loadResult = editorAPI.EnsureProjectMeshLoaded(loadRequest);
	if (!loadResult.available)
	{
		payload.message = "Model mesh could not be uploaded on the RenderThread";
		return payload;
	}
    if (loadResult.loaded)
        VANS_LOG("[EngineAPI] Mesh loaded for placement: "
            << record->sourcePath.string() << " as '" << meshName << "'");
	else
		VANS_LOG("[EngineAPI] Reusing resident model mesh for placement: "
			<< meshName);

	const ProjectMeshInfoSnapshot droppedMesh = editorAPI.GetProjectMeshInfo(meshName);
	const std::string uniqueName = MakeUniqueEntityName(scene, entityBaseName);

    Vans::SceneModelEntityFactoryResult sceneEntities;
    if (droppedMesh.available && droppedMesh.isMultiMesh)
    {
        const GeneratedMaterialLookup generatedMaterials = BuildGeneratedMaterialLookup(request.assetGuid);
        Vans::SceneModelEntityFactoryRequest sceneRequest =
            BuildSceneRequest(uniqueName, request.assetGuid, request.worldPosition);
        for (std::uint32_t index = 0; index < droppedMesh.submeshes.size(); ++index)
        {
            const ProjectSubmeshInfo& submesh = droppedMesh.submeshes[index];
            if (submesh.vertexCount == 0 || submesh.indexCount == 0)
                continue;

            Vans::SceneSubmeshEntityDescriptor descriptor;
            descriptor.sourceNodeName = submesh.sourceNodeName;
            descriptor.materialName = submesh.materialName;
            descriptor.materialGuid = ResolveGeneratedMaterialOverride(generatedMaterials, submesh, index);
            descriptor.vertexCount = submesh.vertexCount;
            descriptor.indexCount = submesh.indexCount;
            sceneRequest.submeshes.push_back(std::move(descriptor));
        }

        sceneEntities = Vans::VansSceneEntityFactory::BuildMultiMeshEntityHierarchy(sceneRequest);
        payload.runtimeEntityGuid = sceneEntities.rootEntityId;
    }
    else
    {
        const std::string entityGuid = Vans::VansAssetGuid::New().ToString();
        const std::string transformComponentGuid = Vans::VansAssetGuid::New().ToString();
        const std::string renderComponentGuid = Vans::VansAssetGuid::New().ToString();
        Vans::SceneModelEntityFactoryRequest sceneRequest =
            BuildSceneRequest(uniqueName, request.assetGuid, request.worldPosition);
		sceneRequest.defaultMaterialGuid = editorAPI.GetDefaultMaterialAssetName();
        sceneRequest.transformComponentGuid = transformComponentGuid;
        sceneRequest.modelRendererComponentGuid = renderComponentGuid;
        sceneEntities = Vans::VansSceneEntityFactory::BuildSingleModelEntity(
            sceneRequest,
            entityGuid);
        payload.runtimeEntityGuid = sceneEntities.rootEntityId;
    }

    payload.sceneEntities.reserve(sceneEntities.entities.size());
    for (const Vans::VansSerializedValue& entity : sceneEntities.entities)
        payload.sceneEntities.push_back(ScenePropertyValues::FromSerializedValue(entity));
    payload.prepared = !payload.sceneEntities.empty();
    if (!payload.prepared)
        payload.message = "Model asset placement produced no scene entities";
    return payload;
}
}
