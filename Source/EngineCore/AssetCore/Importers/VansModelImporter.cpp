#include "VansModelImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <unordered_map>

namespace Vans
{
namespace
{
VansSubAssetId StableId(VansAssetMeta& meta, const std::string& fingerprint)
{
    if (const auto existing = meta.subAssets.find(fingerprint); existing != meta.subAssets.end())
        return existing->second;
    const VansSubAssetId created = VansSubAssetId::New();
    meta.subAssets.emplace(fingerprint, created);
    return created;
}

std::array<float, 16> Matrix(const aiMatrix4x4& value)
{
    return { value.a1, value.a2, value.a3, value.a4,
             value.b1, value.b2, value.b3, value.b4,
             value.c1, value.c2, value.c3, value.c4,
             value.d1, value.d2, value.d3, value.d4 };
}

std::array<float, 3> Color3(const aiColor3D& value)
{
    return { value.r, value.g, value.b };
}

std::array<float, 4> Color4(const aiColor3D& value, float alpha)
{
    return { value.r, value.g, value.b, alpha };
}

float Clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

float RoughnessFromShininess(float shininess)
{
    if (shininess <= 0.0f)
        return 0.5f;
    return std::max(0.045f, std::min(1.0f, std::sqrt(2.0f / (shininess + 2.0f))));
}

bool ContainsToken(std::string text, const char* token)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text.find(token) != std::string::npos;
}

std::filesystem::path ResolveTexturePath(const aiMaterial* material, aiTextureType type,
    const std::filesystem::path& sourceDirectory)
{
    if (material->GetTextureCount(type) == 0)
        return {};

    aiString path;
    if (material->GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0)
        return {};

    std::filesystem::path resolved(path.C_Str());
    if (resolved.is_relative())
        resolved = sourceDirectory / resolved;
    return std::filesystem::absolute(resolved).lexically_normal();
}

void AddTextureReference(VansModelAsset& asset, const VansImportedMaterialSlot& slot,
    const char* semantic, const std::filesystem::path& path, bool srgb)
{
    if (path.empty())
        return;

    VansImportedTextureRef ref;
    ref.materialSlotId = slot.id;
    ref.materialName = slot.name;
    ref.semantic = semantic;
    ref.sourcePath = path;
    ref.srgb = srgb;
    asset.textureReferences.push_back(std::move(ref));
}

VansImportedMaterialSlot ParseMaterialSlot(const aiMaterial* material, unsigned materialIndex,
    VansAssetMeta& meta)
{
    aiString materialName;
    material->Get(AI_MATKEY_NAME, materialName);
    const std::string name = materialName.length > 0 ? materialName.C_Str() : "Default";

    VansImportedMaterialSlot slot;
    slot.id = StableId(meta, "material:" + name + ":" + std::to_string(materialIndex));
    slot.name = name;

    aiColor3D color;
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
        slot.baseColor = Color4(color, slot.opacity);
    if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
        slot.specularColor = Color3(color);
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
        slot.emissiveColor = Color3(color);

    float value = 0.0f;
    if (material->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS)
        slot.opacity = Clamp01(value);
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS)
        slot.metallic = Clamp01(value);
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS)
        slot.roughness = std::max(0.045f, Clamp01(value));
    if (material->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS)
    {
        slot.shininess = value;
        if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) != AI_SUCCESS)
            slot.roughness = RoughnessFromShininess(slot.shininess);
    }
    if (material->Get(AI_MATKEY_SHININESS_STRENGTH, value) == AI_SUCCESS)
        slot.specularFactor = Clamp01(value);
    if (material->Get(AI_MATKEY_REFLECTIVITY, value) == AI_SUCCESS)
        slot.reflectionFactor = Clamp01(value);

    if (slot.metallic <= 0.0f &&
        (ContainsToken(name, "metal") || ContainsToken(name, "chrome") || ContainsToken(name, "metallic")))
    {
        slot.metallic = 1.0f;
        slot.roughness = std::min(slot.roughness, 0.35f);
    }

    slot.baseColor[3] = slot.opacity;
    slot.transparent = slot.opacity < 0.99f || material->GetTextureCount(aiTextureType_OPACITY) > 0;
    return slot;
}

nlohmann::ordered_json MaterialReportJson(const VansModelAsset& asset)
{
    nlohmann::ordered_json report;
    report["materials"] = nlohmann::ordered_json::array();
    for (const VansImportedMaterialSlot& slot : asset.materialSlots)
    {
        report["materials"].push_back({
            { "id", slot.id.ToString() },
            { "name", slot.name },
            { "baseColor", slot.baseColor },
            { "specularColor", slot.specularColor },
            { "emissiveColor", slot.emissiveColor },
            { "opacity", slot.opacity },
            { "metallic", slot.metallic },
            { "roughness", slot.roughness },
            { "specularFactor", slot.specularFactor },
            { "shininess", slot.shininess },
            { "reflectionFactor", slot.reflectionFactor },
            { "transparent", slot.transparent }
        });
    }
    report["textures"] = nlohmann::ordered_json::array();
    for (const VansImportedTextureRef& ref : asset.textureReferences)
    {
        report["textures"].push_back({
            { "materialSlotId", ref.materialSlotId.ToString() },
            { "materialName", ref.materialName },
            { "semantic", ref.semantic },
            { "sourcePath", ref.sourcePath.string() },
            { "srgb", ref.srgb }
        });
    }
    return report;
}

void CollectNodes(const aiNode* source, std::int32_t parent, const std::string& parentPath,
    VansAssetMeta& meta, VansModelAsset& asset)
{
    const std::string name = source->mName.C_Str();
    const std::string path = parentPath.empty() ? name : parentPath + "/" + name;
    VansModelNodeAsset node;
    node.id = StableId(meta, "node:" + path);
    node.name = name;
    node.parentIndex = parent;
    node.localTransform = Matrix(source->mTransformation);
    if (source->mNumMeshes > 0)
        node.meshIndices.assign(source->mMeshes, source->mMeshes + source->mNumMeshes);
    const std::int32_t index = static_cast<std::int32_t>(asset.nodes.size());
    asset.nodes.push_back(std::move(node));
    for (unsigned child = 0; child < source->mNumChildren; ++child)
        CollectNodes(source->mChildren[child], index, path, meta, asset);
}
}

bool VansModelImportResult::Succeeded() const
{
    return std::none_of(messages.begin(), messages.end(), [](const VansImportMessage& message) {
        return message.severity == VansImportMessageSeverity::Error;
    });
}

VansModelImportResult VansModelImporter::Import(const std::filesystem::path& sourcePath,
    VansAssetMeta& meta, const VansModelImportSettings& settings) const
{
    VansModelImportResult result;
    result.asset.guid = meta.guid;
    result.asset.sourcePath = std::filesystem::absolute(sourcePath).lexically_normal();
    result.asset.importSettings = settings;

    unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality | aiProcess_SortByPType | aiProcess_ValidateDataStructure;
    if (settings.generateNormalsIfMissing) flags |= aiProcess_GenSmoothNormals;
    if (settings.generateTangents) flags |= aiProcess_CalcTangentSpace;
    if (settings.flipUV) flags |= aiProcess_FlipUVs;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath.string(), flags);
    if (!scene || !scene->mRootNode)
    {
        result.messages.push_back({ VansImportMessageSeverity::Error, sourcePath.string(), importer.GetErrorString() });
        return result;
    }

    const std::filesystem::path sourceDirectory = result.asset.sourcePath.parent_path();
    result.asset.materialSlots.reserve(std::max(1u, scene->mNumMaterials));
    for (unsigned materialIndex = 0; materialIndex < std::max(1u, scene->mNumMaterials); ++materialIndex)
    {
        if (materialIndex < scene->mNumMaterials)
        {
            const aiMaterial* material = scene->mMaterials[materialIndex];
            VansImportedMaterialSlot slot = ParseMaterialSlot(material, materialIndex, meta);
            AddTextureReference(result.asset, slot, "baseColor",
                ResolveTexturePath(material, aiTextureType_BASE_COLOR, sourceDirectory), true);
            AddTextureReference(result.asset, slot, "baseColor",
                ResolveTexturePath(material, aiTextureType_DIFFUSE, sourceDirectory), true);
            AddTextureReference(result.asset, slot, "normal",
                ResolveTexturePath(material, aiTextureType_NORMALS, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "normal",
                ResolveTexturePath(material, aiTextureType_HEIGHT, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "metallic",
                ResolveTexturePath(material, aiTextureType_METALNESS, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "roughness",
                ResolveTexturePath(material, aiTextureType_DIFFUSE_ROUGHNESS, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "roughness",
                ResolveTexturePath(material, aiTextureType_SHININESS, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "ao",
                ResolveTexturePath(material, aiTextureType_AMBIENT_OCCLUSION, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "ao",
                ResolveTexturePath(material, aiTextureType_LIGHTMAP, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "opacity",
                ResolveTexturePath(material, aiTextureType_OPACITY, sourceDirectory), false);
            AddTextureReference(result.asset, slot, "emissive",
                ResolveTexturePath(material, aiTextureType_EMISSIVE, sourceDirectory), true);
            result.asset.materialSlots.push_back(std::move(slot));
        }
        else
        {
            VansImportedMaterialSlot slot;
            slot.id = StableId(meta, "material:Default:0");
            slot.name = "Default";
            result.asset.materialSlots.push_back(std::move(slot));
        }
    }

    std::unordered_map<unsigned, VansSubAssetId> nodeByMeshIndex;
    std::function<void(const aiNode*, const std::string&)> collectMeshNodes = [&](const aiNode* node, const std::string& parentPath)
    {
        const std::string name = node->mName.C_Str();
        const std::string path = parentPath.empty() ? name : parentPath + "/" + name;
        for (unsigned mesh = 0; mesh < node->mNumMeshes; ++mesh)
        {
            const unsigned meshIndex = node->mMeshes[mesh];
            nodeByMeshIndex[meshIndex] = StableId(meta, "node:" + path);
        }
        for (unsigned child = 0; child < node->mNumChildren; ++child)
            collectMeshNodes(node->mChildren[child], path);
    };
    collectMeshNodes(scene->mRootNode, {});

    result.asset.meshes.reserve(scene->mNumMeshes);
    for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const aiMesh* source = scene->mMeshes[meshIndex];
        const std::string meshName = source->mName.length > 0 ? source->mName.C_Str() : "Mesh" + std::to_string(meshIndex);
        const std::string fingerprint = "mesh:" + meshName + ":v" + std::to_string(source->mNumVertices) + ":f" + std::to_string(source->mNumFaces);
        VansMeshAsset mesh;
        mesh.id = StableId(meta, fingerprint);
        mesh.vertices.reserve(source->mNumVertices);

        std::array<float, 3> minimum{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        std::array<float, 3> maximum{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
        for (unsigned vertexIndex = 0; vertexIndex < source->mNumVertices; ++vertexIndex)
        {
            const aiVector3D position = source->mVertices[vertexIndex] * settings.scaleFactor;
            VansModelVertex vertex;
            vertex.position = { position.x, position.y, position.z };
            if (source->HasNormals()) vertex.normal = { source->mNormals[vertexIndex].x, source->mNormals[vertexIndex].y, source->mNormals[vertexIndex].z };
            if (source->HasTangentsAndBitangents()) vertex.tangent = { source->mTangents[vertexIndex].x, source->mTangents[vertexIndex].y, source->mTangents[vertexIndex].z, 1.0f };
            if (source->HasTextureCoords(0)) vertex.uv = { source->mTextureCoords[0][vertexIndex].x, source->mTextureCoords[0][vertexIndex].y };
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
                maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
            }
            mesh.vertices.push_back(vertex);
        }
        for (unsigned faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex)
        {
            const aiFace& face = source->mFaces[faceIndex];
            if (face.mNumIndices != 3)
            {
                result.messages.push_back({ VansImportMessageSeverity::Warning, meshName, "Skipped a non-triangle face after triangulation" });
                continue;
            }
            mesh.indices.insert(mesh.indices.end(), face.mIndices, face.mIndices + 3);
        }
        VansMeshPrimitiveAsset primitive;
        primitive.id = StableId(meta, "primitive:" + fingerprint + ":material:" + std::to_string(source->mMaterialIndex));
        primitive.name = meshName;
        primitive.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        primitive.materialSlot = std::min(source->mMaterialIndex, static_cast<unsigned>(result.asset.materialSlots.size() - 1));
        primitive.materialSlotId = result.asset.materialSlots[primitive.materialSlot].id;
        if (const auto nodeIt = nodeByMeshIndex.find(meshIndex); nodeIt != nodeByMeshIndex.end())
            primitive.sourceNodeId = nodeIt->second;
        primitive.boundsMin = minimum;
        primitive.boundsMax = maximum;
        primitive.hasCpuCollisionData = settings.keepCpuMeshData;
        primitive.blasCapable = settings.buildRayTracingData;
        mesh.primitives.push_back(std::move(primitive));
        result.asset.hasSkeleton |= source->HasBones();
        result.asset.meshes.push_back(std::move(mesh));
    }

    CollectNodes(scene->mRootNode, -1, {}, meta, result.asset);
    result.asset.animationClipCount = settings.importAnimations ? scene->mNumAnimations : 0;
    if (scene->mNumTextures > 0)
        result.messages.push_back({ VansImportMessageSeverity::Warning, sourcePath.string(), "Embedded textures were skipped; use TextureAsset references" });
    if (scene->mNumCameras > 0 || scene->mNumLights > 0)
        result.messages.push_back({ VansImportMessageSeverity::Warning, sourcePath.string(), "Imported cameras and lights are not part of ModelAsset" });

    meta.importer = "ModelImporter";
    meta.version = Version;
    meta.settings = {
        { "scaleFactor", settings.scaleFactor },
        { "generateNormals", settings.generateNormalsIfMissing ? "ifMissing" : "never" },
        { "generateTangents", settings.generateTangents },
        { "flipUV", settings.flipUV },
        { "importMaterials", settings.importMaterials },
        { "materialMode", settings.materialMode },
        { "textureRedirection", settings.textureRedirection },
        { "defaultShader", settings.defaultShader },
        { "redirectTextures", settings.redirectTextures },
        { "importAnimations", settings.importAnimations },
        { "keepCpuMeshData", settings.keepCpuMeshData },
        { "buildRayTracingData", settings.buildRayTracingData },
        { "importReport", MaterialReportJson(result.asset) }
    };
    std::string metaError;
    if (!meta.SaveAtomic(VansAssetMeta::MetaPathFor(sourcePath), metaError))
        result.messages.push_back({ VansImportMessageSeverity::Error, sourcePath.string(), "Failed to publish import metadata: " + metaError });
    return result;
}
}
