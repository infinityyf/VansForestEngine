#include "VansProjectDocumentStorage.h"

#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../Serialization/VansProjectConfigJsonCodec.h"
#include "../Serialization/VansProjectSettingsJsonCodec.h"
#include "../VansProjectConfigValidator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Vans
{
namespace
{
std::string NormalizeDocumentPath(std::filesystem::path path)
{
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::absolute(path, ec);
	if (ec)
		normalized = std::move(path);
	std::string value = normalized.lexically_normal().generic_string();
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return value;
}

std::uint64_t HashDocumentBytes(const std::string& bytes)
{
	constexpr std::uint64_t kOffset = 14695981039346656037ull;
	constexpr std::uint64_t kPrime = 1099511628211ull;
	std::uint64_t hash = kOffset;
	for (const unsigned char value : bytes)
	{
		hash ^= value;
		hash *= kPrime;
	}
	return hash;
}

VansProjectDocumentFingerprint CaptureFingerprint(const fs::path& path)
{
	VansProjectDocumentFingerprint fingerprint;
	std::error_code ec;
	fingerprint.m_Exists = fs::exists(path, ec);
	if (ec)
		return fingerprint;
	if (!fingerprint.m_Exists)
	{
		fingerprint.m_Valid = true;
		return fingerprint;
	}

	std::string bytes;
	std::string error;
	if (!VansFileStorage::ReadAllBytes(path, bytes, error))
		return fingerprint;
	fingerprint.m_Size = bytes.size();
	fingerprint.m_ContentHash = HashDocumentBytes(bytes);
	fingerprint.m_WriteTime = fs::last_write_time(path, ec);
	fingerprint.m_Valid = !ec;
	return fingerprint;
}

void CaptureFingerprint(const fs::path& path,
	std::unordered_map<std::string, VansProjectDocumentFingerprint>& fingerprints)
{
	fingerprints[NormalizeDocumentPath(path)] = CaptureFingerprint(path);
}

bool IsUnchanged(const fs::path& path,
	const std::unordered_map<std::string, VansProjectDocumentFingerprint>& expectedFingerprints, std::string& error)
{
	const auto expected = expectedFingerprints.find(NormalizeDocumentPath(path));
	if (expected == expectedFingerprints.end())
	{
		std::error_code ec;
		if (!fs::exists(path, ec) && !ec)
			return true;
		error = "Project document target was not loaded and cannot overwrite an existing file: " + path.string();
		return false;
	}
	if (!expected->second.m_Valid)
	{
		error = "Project document has no valid load fingerprint: " + path.string();
		return false;
	}

	const VansProjectDocumentFingerprint current = CaptureFingerprint(path);
	const VansProjectDocumentFingerprint& loaded = expected->second;
	if (!current.m_Valid || current.m_Exists != loaded.m_Exists || current.m_Size != loaded.m_Size ||
		current.m_ContentHash != loaded.m_ContentHash ||
		(current.m_Exists && current.m_WriteTime != loaded.m_WriteTime))
	{
		error = "Project document changed on disk after it was loaded: " + path.string();
		return false;
	}
	return true;
}
} // namespace

std::unordered_map<std::string, VansProjectDocumentFingerprint> VansProjectDocumentStorage::CaptureFingerprints(
	const std::string& projectRootPath, const VansProjectConfig& config)
{
	std::unordered_map<std::string, VansProjectDocumentFingerprint> fingerprints;
	CaptureFingerprint(fs::path(projectRootPath) / "ForestProject.json", fingerprints);
	if (!config.renderSettings.empty())
		CaptureFingerprint(fs::path(projectRootPath) / config.renderSettings, fingerprints);
	if (!config.physicsSettings.empty())
		CaptureFingerprint(fs::path(projectRootPath) / config.physicsSettings, fingerprints);
	if (!config.navigationSettings.empty())
		CaptureFingerprint(fs::path(projectRootPath) / config.navigationSettings, fingerprints);
	if (!config.collisionLayerSettings.empty())
		CaptureFingerprint(fs::path(projectRootPath) / config.collisionLayerSettings, fingerprints);
	if (!config.audioSettings.empty())
		CaptureFingerprint(fs::path(projectRootPath) / config.audioSettings, fingerprints);
	for (const std::string_view fileName : VansGAFProjectConfiguration::DocumentFileNames())
		CaptureFingerprint(fs::path(projectRootPath) / "ProjectSettings" / fileName, fingerprints);
	return fingerprints;
}

bool VansProjectDocumentStorage::Save(const VansProjectDocumentSaveRequest& request,
	VansProjectDocumentSaveResult& result, std::string& error)
{
	result = {};
	const fs::path configPath = fs::path(request.m_ProjectRootPath) / "ForestProject.json";
	const fs::path renderSettingsPath = fs::path(request.m_ProjectRootPath) / request.m_Config.renderSettings;
	const fs::path physicsSettingsPath = fs::path(request.m_ProjectRootPath) / request.m_Config.physicsSettings;
	const fs::path navigationSettingsPath =
		fs::path(request.m_ProjectRootPath) / request.m_Config.navigationSettings;
	const fs::path collisionLayersPath =
		fs::path(request.m_ProjectRootPath) / request.m_Config.collisionLayerSettings;
	const fs::path audioMixPath = fs::path(request.m_ProjectRootPath) / request.m_Config.audioSettings;
	const fs::path projectSettingsDirectory = fs::path(request.m_ProjectRootPath) / "ProjectSettings";
	const auto gafFileNames = VansGAFProjectConfiguration::DocumentFileNames();
	std::array<fs::path, 4> gafPaths;
	for (std::size_t index = 0; index < gafPaths.size(); ++index)
		gafPaths[index] = projectSettingsDirectory / gafFileNames[index];

	std::vector<fs::path> dirtyPaths;
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::ProjectConfig))
		dirtyPaths.push_back(configPath);
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::RenderSettings))
		dirtyPaths.push_back(renderSettingsPath);
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::PhysicsSettings))
		dirtyPaths.push_back(physicsSettingsPath);
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::NavigationSettings))
		dirtyPaths.push_back(navigationSettingsPath);
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::CollisionLayers))
		dirtyPaths.push_back(collisionLayersPath);
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::AudioMix))
		dirtyPaths.push_back(audioMixPath);
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::GAFConfiguration))
		dirtyPaths.insert(dirtyPaths.end(), gafPaths.begin(), gafPaths.end());
	for (const fs::path& path : dirtyPaths)
	{
		if (!IsUnchanged(path, request.m_ExpectedFingerprints, error))
			return false;
	}

	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::ProjectConfig))
	{
		VansProjectConfigDiagnostics diagnostics;
		if (!VansProjectConfigValidator::ValidateForSave(request.m_Config, diagnostics, error))
			return false;
	}

	VansStagedFileTransaction transaction;
	const auto stageJson = [&](const fs::path& path, const nlohmann::json& root)
	{
		VansStagedFile stage;
		if (!VansJsonFileStorage::StageWrite(path, root, stage, error))
			return false;
		transaction.Add(std::move(stage));
		return true;
	};
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::ProjectConfig) &&
		!stageJson(configPath, VansProjectConfigJsonCodec::EncodeProjectConfig(request.m_Config)))
		return false;
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::RenderSettings) &&
		!stageJson(renderSettingsPath,
			VansProjectSettingsJsonCodec::EncodeRenderSettings(request.m_Documents.m_Settings.BuildRenderSettingsData())))
		return false;
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::PhysicsSettings) &&
		!stageJson(physicsSettingsPath,
			VansProjectSettingsJsonCodec::EncodePhysicsSettings(
				request.m_Documents.m_Settings.BuildPhysicsSettingsData())))
		return false;
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::NavigationSettings) &&
		!stageJson(navigationSettingsPath,
			VansProjectSettingsJsonCodec::EncodeNavigationSettings(
				request.m_Documents.m_Settings.GetNavigationSettings())))
		return false;
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::CollisionLayers) &&
		(!request.m_Documents.m_HasCollisionLayerDocument ||
			!stageJson(collisionLayersPath,
				EncodeSerializedValueJson<nlohmann::json>(request.m_Documents.m_CollisionLayerDocument))))
	{
		if (!request.m_Documents.m_HasCollisionLayerDocument)
			error = "Collision layer document is unavailable";
		return false;
	}
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::AudioMix) &&
		(!request.m_Documents.m_HasAudioMixDocument ||
			!stageJson(audioMixPath,
				EncodeSerializedValueJson<nlohmann::json>(request.m_Documents.m_AudioMixDocument))))
	{
		if (!request.m_Documents.m_HasAudioMixDocument)
			error = "Audio mix document is unavailable";
		return false;
	}
	if (HasProjectDocumentDomain(request.m_DirtyMask, VansProjectDocumentDomain::GAFConfiguration))
	{
		if (!request.m_Documents.m_HasGAFConfiguration ||
			!request.m_Documents.m_GAFConfiguration.Validate(error))
		{
			if (!request.m_Documents.m_HasGAFConfiguration)
				error = "GAF project configuration is unavailable";
			return false;
		}
		const VansGAFProjectConfigurationDocuments documents =
			VansGAFProjectConfiguration::EncodeDocuments(request.m_Documents.m_GAFConfiguration);
		const VansSerializedValue* roots[] = {&documents.settings, &documents.schemaRegistry,
			&documents.validationRules, &documents.templates};
		for (std::size_t index = 0; index < gafPaths.size(); ++index)
		{
			if (!stageJson(gafPaths[index], EncodeSerializedValueJson<nlohmann::json>(*roots[index])))
				return false;
		}
	}
	if (!transaction.Publish(error))
		return false;

	result.m_Fingerprints = request.m_ExpectedFingerprints;
	for (const fs::path& path : dirtyPaths)
		CaptureFingerprint(path, result.m_Fingerprints);
	error.clear();
	return true;
}
} // namespace Vans
