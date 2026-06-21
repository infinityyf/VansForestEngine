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
};

struct VansModelAsset
{
    VansAssetGuid guid;
    std::filesystem::path sourcePath;
    VansModelImportSettings importSettings;
    std::vector<VansModelNodeAsset> nodes;
    std::vector<VansMeshAsset> meshes;
    std::vector<VansImportedMaterialSlot> materialSlots;
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
