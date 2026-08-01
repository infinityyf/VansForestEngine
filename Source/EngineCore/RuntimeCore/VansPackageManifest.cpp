#include "VansPackageManifest.h"

#include "../AssetCore/Storage/VansStagedFileTransaction.h"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace Vans
{
namespace
{
constexpr const char* PackageFormat = "ForestGamePackage";

bool IsPortableRelativePath(const std::string& value, bool allowEmpty)
{
    if (value.empty())
        return allowEmpty;

    const std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    for (const std::filesystem::path& part : path)
    {
        if (part == "..")
            return false;
    }
    return true;
}

std::filesystem::path MakeTemporaryPath(const std::filesystem::path& target)
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return target.parent_path() / (target.filename().string() + ".tmp." + std::to_string(nonce));
}
}

bool VansPackageManifestIO::Validate(const VansPackageManifest& manifest, std::string& error)
{
    if (manifest.platform != "Windows")
    {
        error = "Unsupported package platform: " + manifest.platform;
        return false;
    }
    if (manifest.startupMode != "play")
    {
        error = "Unsupported package startup mode: " + manifest.startupMode;
        return false;
    }
    if (!IsPortableRelativePath(manifest.binaryRoot, false))
    {
        error = "Package binaryRoot must be a portable relative path";
        return false;
    }
    if (!IsPortableRelativePath(manifest.scene, false))
    {
        error = "Package scene must be a portable relative path";
        return false;
    }
    if (!IsPortableRelativePath(manifest.resourcePlan, true))
    {
        error = "Package resourcePlan must be a portable relative path";
        return false;
    }
    if (!IsPortableRelativePath(manifest.resourcePlanReport, true))
    {
        error = "Package resourcePlanReport must be a portable relative path";
        return false;
    }
	if (!IsPortableRelativePath(manifest.shaderArtifacts, false))
	{
		error = "Package shaderArtifacts must be a portable relative path";
		return false;
	}
    error.clear();
    return true;
}

VansPackageManifestLoadResult VansPackageManifestIO::Load(const std::filesystem::path& path)
{
    VansPackageManifestLoadResult result;
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            result.error = "Cannot read package manifest: " + path.string();
            return result;
        }

        nlohmann::json root;
        file >> root;
        if (root.value("format", std::string{}) != PackageFormat)
        {
            result.error = "Unsupported package manifest format";
            return result;
        }
        result.manifest.platform = root.value("platform", std::string{});
        result.manifest.startupMode = root.value("startupMode", std::string{ "play" });
        result.manifest.generatedAt = root.value("generatedAt", std::string{});
        result.manifest.binaryRoot = root.value("binaryRoot", std::string{ "Binaries/Windows" });
        result.manifest.scene = root.value("scene", std::string{});
        result.manifest.resourcePlan = root.value("resourcePlan", std::string{});
        result.manifest.resourcePlanReport = root.value("resourcePlanReport", std::string{});
		result.manifest.shaderArtifacts = root.value("shaderArtifacts", std::string{});
        result.manifest.copiedFileCount = root.value("copiedFileCount", std::uint64_t{ 0 });
        Validate(result.manifest, result.error);
        return result;
    }
    catch (const std::exception& exception)
    {
        result.error = "Cannot parse package manifest '" + path.string() + "': " + exception.what();
        return result;
    }
}

bool VansPackageManifestIO::Save(
    const std::filesystem::path& path,
    const VansPackageManifest& manifest,
    std::string& error)
{
    if (!Validate(manifest, error))
        return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "Cannot create package manifest directory: " + ec.message();
        return false;
    }

    nlohmann::ordered_json root;
    root["format"] = PackageFormat;
    root["platform"] = manifest.platform;
    root["startupMode"] = manifest.startupMode;
    root["generatedAt"] = manifest.generatedAt;
    root["binaryRoot"] = manifest.binaryRoot;
    root["scene"] = manifest.scene;
    if (!manifest.resourcePlan.empty())
        root["resourcePlan"] = manifest.resourcePlan;
    if (!manifest.resourcePlanReport.empty())
        root["resourcePlanReport"] = manifest.resourcePlanReport;
	root["shaderArtifacts"] = manifest.shaderArtifacts;
    root["copiedFileCount"] = manifest.copiedFileCount;

    const std::filesystem::path temporaryPath = MakeTemporaryPath(path);
    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "Cannot write temporary package manifest: " + temporaryPath.string();
            return false;
        }
        file << root.dump(2) << '\n';
        if (!file)
        {
            error = "Cannot flush temporary package manifest: " + temporaryPath.string();
            return false;
        }
    }

    VansStagedFileTransaction transaction;
    transaction.Add({ path, temporaryPath });
    return transaction.Publish(error);
}
}
