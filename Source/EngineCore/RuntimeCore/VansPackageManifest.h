#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Vans
{
struct VansPackageManifest
{
    std::string platform = "Windows";
    std::string startupMode = "play";
    std::string generatedAt;
    std::string binaryRoot = "Binaries/Windows";
    std::string scene;
    std::string resourcePlan;
    std::string resourcePlanReport;
	std::string shaderArtifacts = "Library/Artifacts/Shaders";
    std::uint64_t copiedFileCount = 0;
};

struct VansPackageManifestLoadResult
{
    VansPackageManifest manifest;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

class VansPackageManifestIO
{
public:
    static VansPackageManifestLoadResult Load(const std::filesystem::path& path);
    static bool Save(
        const std::filesystem::path& path,
        const VansPackageManifest& manifest,
        std::string& error);
    static bool Validate(const VansPackageManifest& manifest, std::string& error);
};
}
