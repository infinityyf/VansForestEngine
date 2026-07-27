#pragma once

#include <string>
#include <vector>

namespace Vans
{
struct VansAssetMeta;
struct VansModelAsset;
struct VansModelImportSettings;

struct VansModelImportReportTexture
{
    std::string guid;
    std::string name;
    std::string path;
};

struct VansModelImportReportGeneratedMaterial
{
    std::string guid;
    std::string textureGuid;
};

struct VansModelImportReport
{
    std::vector<VansModelImportReportTexture> textures;
    std::vector<VansModelImportReportGeneratedMaterial> generatedMaterials;
};

VansModelImportReport ReadModelImportReport(const VansAssetMeta& meta);
void WriteModelImportMetaSettings(
    VansAssetMeta& meta,
    const VansModelImportSettings& settings,
    const VansModelAsset& asset);
}
