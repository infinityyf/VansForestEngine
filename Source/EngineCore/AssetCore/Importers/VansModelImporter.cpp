#include "VansModelImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <limits>

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

    result.asset.materialSlots.reserve(std::max(1u, scene->mNumMaterials));
    for (unsigned materialIndex = 0; materialIndex < std::max(1u, scene->mNumMaterials); ++materialIndex)
    {
        aiString materialName;
        if (materialIndex < scene->mNumMaterials)
            scene->mMaterials[materialIndex]->Get(AI_MATKEY_NAME, materialName);
        const std::string name = materialName.length > 0 ? materialName.C_Str() : "Default";
        result.asset.materialSlots.push_back({ StableId(meta, "material:" + name + ":" + std::to_string(materialIndex)), name });
    }

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
        { "importAnimations", settings.importAnimations },
        { "keepCpuMeshData", settings.keepCpuMeshData },
        { "buildRayTracingData", settings.buildRayTracingData }
    };
    std::string metaError;
    if (!meta.SaveAtomic(VansAssetMeta::MetaPathFor(sourcePath), metaError))
        result.messages.push_back({ VansImportMessageSeverity::Error, sourcePath.string(), "Failed to publish import metadata: " + metaError });
    return result;
}
}
