#pragma once

#include "VansAssetGuid.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
struct VansModelImportSettings
{
    float scaleFactor = 1.0f;
    bool generateNormalsIfMissing = true;
    bool generateTangents = true;
    bool flipUV = true;
    bool importMaterials = false;
    bool redirectTextures = false;
    std::string materialMode = "none";
    std::string textureRedirection = "none";
    std::string defaultShader = "PBR";
    bool importAnimations = true;
    bool keepCpuMeshData = false;
    bool buildRayTracingData = true;
};

struct VansModelVertex
{
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 4> tangent{};
    std::array<float, 2> uv{};
};

struct VansMeshPrimitiveAsset
{
    VansSubAssetId id;
    std::string name;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
    VansSubAssetId materialSlotId;
    VansSubAssetId sourceNodeId;
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    bool hasCpuCollisionData = false;
    bool blasCapable = false;
};

struct VansMeshAsset
{
    VansSubAssetId id;
    std::vector<VansModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<VansMeshPrimitiveAsset> primitives;
};

struct VansModelNodeAsset
{
    VansSubAssetId id;
    std::string name;
    std::int32_t parentIndex = -1;
    std::array<float, 16> localTransform{};
    std::vector<std::uint32_t> meshIndices;
};

struct VansImportedMaterialSlot
{
    VansSubAssetId id;
    std::string name;
    std::array<float, 4> baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 3> specularColor{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> emissiveColor{ 0.0f, 0.0f, 0.0f };
    float opacity = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specularFactor = 0.0f;
    float shininess = 0.0f;
    float reflectionFactor = 0.0f;
    bool transparent = false;
};

struct VansImportedTextureRef
{
    VansSubAssetId materialSlotId;
    std::string materialName;
    std::string semantic;
    std::filesystem::path sourcePath;
    VansAssetGuid redirectedTextureGuid;
    bool srgb = false;
};

struct VansModelAsset
{
    VansAssetGuid guid;
    std::filesystem::path sourcePath;
    VansModelImportSettings importSettings;
    std::vector<VansModelNodeAsset> nodes;
    std::vector<VansMeshAsset> meshes;
    std::vector<VansImportedMaterialSlot> materialSlots;
    std::vector<VansImportedTextureRef> textureReferences;
    bool hasSkeleton = false;
    std::uint32_t animationClipCount = 0;
};

enum class VansImportMessageSeverity { Info, Warning, Error };

struct VansImportMessage
{
    VansImportMessageSeverity severity = VansImportMessageSeverity::Info;
    std::string subject;
    std::string message;
};

struct VansModelImportResult
{
    VansModelAsset asset;
    std::vector<VansImportMessage> messages;

    bool Succeeded() const;
};
}
