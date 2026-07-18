#include "ModelAssetPlacementPreparationService.h"

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneEntityFactory.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

#include <../../GLM/glm.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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

    std::ifstream input(record->metaPath);
    if (!input)
        return lookup;

    const auto meta = Vans::SceneJson::parse(input, nullptr, false);
    if (meta.is_discarded() || !meta.is_object())
        return lookup;

    const auto settingsIt = meta.find("settings");
    if (settingsIt == meta.end() || !settingsIt->is_object())
        return lookup;
    const auto reportIt = settingsIt->find("importReport");
    if (reportIt == settingsIt->end() || !reportIt->is_object())
        return lookup;

    std::unordered_map<std::string, std::string> textureGuidToName;
    const auto texturesIt = reportIt->find("textures");
    if (texturesIt != reportIt->end() && texturesIt->is_array())
    {
        for (const auto& texture : *texturesIt)
        {
            if (!texture.is_object())
                continue;
            const std::string guid = texture.value("guid", "");
            std::string name = texture.value("name", "");
            if (name.empty())
                name = TextureFileKey(texture.value("path", ""));
            else
                name = TextureFileKey(name);
            if (!guid.empty() && !name.empty())
                textureGuidToName[guid] = name;
        }
    }

    const auto matsIt = reportIt->find("generatedMaterials");
    if (matsIt != reportIt->end() && matsIt->is_array())
    {
        for (const auto& material : *matsIt)
        {
            if (!material.is_object())
                continue;
            const std::string matGuid = material.value("guid", "");
            if (matGuid.empty())
                continue;

            lookup.byIndex.push_back(matGuid);
            const std::string textureGuid = material.value("texture", "");
            const auto texIt = textureGuidToName.find(textureGuid);
            if (texIt != textureGuidToName.end())
                lookup.byTextureName[texIt->second] = matGuid;
        }
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

MeshLoadResult EnsureProjectMeshLoaded(
    VansGraphics::VansScene* scene,
    VansGraphics::VansVKDevice* device,
    const MeshLoadRequest& request)
{
    MeshLoadResult result;
    if (!scene || !device || request.meshName.empty() || request.sourcePath.empty())
        return result;

    if (scene->HasProjectMeshAlias(request.meshName))
    {
        result.available = scene->FindMeshAsset(request.meshName) != nullptr;
        return result;
    }

    auto* mesh = new VansGraphics::VansMesh(false, false);
    mesh->LoadMesh(
        device->GetLogicDevice(),
        device->GetGraphicsQueue(),
        &device->GetCommandBuffer(),
        request.sourcePath,
        false);
    mesh->SetName(request.meshName);
    scene->AddMeshAsset(mesh);
    scene->SetProjectMeshAlias(request.meshName, mesh);

    result.loaded = true;
    result.available = true;
    return result;
}

ProjectMeshInfoSnapshot GetProjectMeshInfo(VansGraphics::VansScene* scene, const std::string& meshName)
{
    ProjectMeshInfoSnapshot snapshot;
    if (!scene || meshName.empty())
        return snapshot;

    auto* mesh = static_cast<VansGraphics::VansMesh*>(scene->FindMeshAsset(meshName));
    if (!mesh)
        return snapshot;

    snapshot.available = true;
    snapshot.isMultiMesh = mesh->m_IsMultiMesh;

    const auto& materialInfos = mesh->m_SubmeshMaterialInfos;
    snapshot.submeshes.reserve(mesh->m_SubMeshes.size());
    for (std::size_t i = 0; i < mesh->m_SubMeshes.size(); ++i)
    {
        VansGraphics::VansMesh* submesh = mesh->m_SubMeshes[i];
        ProjectSubmeshInfo info;
        if (submesh)
        {
            info.sourceNodeName = submesh->m_SourceNodeName;
            info.vertexCount = static_cast<std::uint32_t>(submesh->GetMeshVertexCount());
            info.indexCount = submesh->GetIndexCount();
        }

        if (!materialInfos.empty())
        {
            const auto& materialInfo = i < materialInfos.size() ? materialInfos[i] : materialInfos[0];
            info.materialName = materialInfo.materialName;
            info.diffuseTexturePath = materialInfo.diffuseTexPath;
        }
        snapshot.submeshes.push_back(info);
    }

    return snapshot;
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

std::string GetDefaultMaterialAssetName(VansGraphics::VansScene* scene)
{
    if (!scene)
        return {};

    const auto& materials = scene->GetMaterialAssets();
    if (materials.empty() || !materials[0])
        return {};

    return materials[0]->m_AssetName;
}

RuntimeModelEntityCreateResult CreateRuntimeModelEntity(
    VansGraphics::VansScene* scene,
    VansGraphics::VansVKDevice* device,
    const RuntimeModelEntityCreateRequest& request)
{
    RuntimeModelEntityCreateResult result;
    if (!scene || !device || request.entityName.empty() || request.meshName.empty())
        return result;

    const glm::vec3 position(request.position.x, request.position.y, request.position.z);
    VansScriptObject* object = scene->CreateEntity(
        device->GetLogicDevice(),
        request.entityName,
        request.meshName,
        request.materialName,
        position);
    if (!object)
        return result;

    result.created = true;
    result.entityGuid = object->m_EntityGuid;
    return result;
}
}

ModelAssetPlacementPayload ModelAssetPlacementPreparationService::Prepare(
    const ModelAssetPlacementRequest& request,
    RuntimeSceneHandle sceneHandle,
    RuntimeRenderDeviceHandle deviceHandle)
{
    ModelAssetPlacementPayload payload;
    auto* scene = static_cast<VansGraphics::VansScene*>(sceneHandle);
    auto* device = static_cast<VansGraphics::VansVKDevice*>(deviceHandle);

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

    const std::string meshName = record->sourcePath.stem().string();
    MeshLoadRequest loadRequest;
    loadRequest.meshName = meshName;
    loadRequest.sourcePath = record->sourcePath.string();
    const MeshLoadResult loadResult = EnsureProjectMeshLoaded(scene, device, loadRequest);
    if (loadResult.loaded)
        VANS_LOG("[EngineAPI] Mesh loaded for placement: "
            << record->sourcePath.string() << " as '" << meshName << "'");

    const ProjectMeshInfoSnapshot droppedMesh = GetProjectMeshInfo(scene, meshName);
    const std::string uniqueName = MakeUniqueEntityName(scene, meshName);
    if (scene)
        scene->SetProjectMeshAlias(request.assetGuid, scene->FindMeshAsset(meshName));

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
    }
    else
    {
        RuntimeModelEntityCreateRequest createRequest;
        createRequest.entityName = uniqueName;
        createRequest.meshName = meshName;
        createRequest.materialName = "DefaultPBR";
        createRequest.position = request.worldPosition;
        const RuntimeModelEntityCreateResult createResult =
            CreateRuntimeModelEntity(scene, device, createRequest);
        if (!createResult.created)
        {
            payload.message = "Runtime model entity creation failed";
            return payload;
        }

        Vans::SceneModelEntityFactoryRequest sceneRequest =
            BuildSceneRequest(uniqueName, request.assetGuid, request.worldPosition);
        sceneRequest.defaultMaterialGuid = GetDefaultMaterialAssetName(scene);
        sceneEntities = Vans::VansSceneEntityFactory::BuildSingleModelEntity(
            sceneRequest,
            createResult.entityGuid);
        payload.runtimeEntityGuid = createResult.entityGuid;
    }

    payload.sceneEntityJsons.reserve(sceneEntities.entities.size());
    for (const Vans::SceneJson& entity : sceneEntities.entities)
        payload.sceneEntityJsons.push_back(entity.dump());
    payload.prepared = !payload.sceneEntityJsons.empty();
    if (!payload.prepared)
        payload.message = "Model asset placement produced no scene entities";
    return payload;
}
}
