#pragma once

#include "VansSceneResourcePlan.h"
#include "../AssetCore/VansAssetResolver.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
enum class VansSceneResourceLoadMode
{
	Editor,
	Packaged
};

struct VansResolvedSceneResourcePath
{
	std::filesystem::path sourcePath;
	std::filesystem::path artifactPath;
	bool cookedOnly = false;
	bool artifactAvailable = false;
	bool sourceFallbackAllowed = true;
	bool sourceAvailable = false;
	bool valid = false;
	std::string error;
};

class VansSceneResourceLoadContext
{
public:
	static VansSceneResourceLoadContext ForEditor(
		std::filesystem::path projectRoot,
		std::filesystem::path engineRoot,
		const std::vector<VansAssetRecord>& assetRecords);

	static VansSceneResourceLoadContext ForPackagedRuntime(
		std::filesystem::path packageContentRoot,
		std::filesystem::path engineRoot,
		const std::vector<VansAssetRecord>& assetRecords);

	VansSceneResourceLoadMode Mode() const { return m_Mode; }
	bool IsPackagedRuntime() const { return m_Mode == VansSceneResourceLoadMode::Packaged; }

	std::filesystem::path ResolveProjectPath(const std::string& path) const;
	std::filesystem::path ResolveEnginePath(const std::string& path) const;

	VansResolvedSceneResourcePath ResolveMesh(const VansSceneMeshResourceRequest& request) const;
	VansResolvedSceneResourcePath ResolveTexture(const VansSceneTextureResourceRequest& request) const;
	VansResolvedSceneResourcePath ResolveShader(const VansSceneShaderResourceRequest& request) const;
	VansResolvedSceneResourcePath ResolveAudio(const VansSceneAudioResourceRequest& request) const;
	VansResolvedSceneResourcePath ResolveVideo(const VansSceneVideoResourceRequest& request) const;

private:
	VansSceneResourceLoadContext(
		VansSceneResourceLoadMode mode,
		std::filesystem::path projectRoot,
		std::filesystem::path engineRoot,
		const std::vector<VansAssetRecord>& assetRecords);

	VansResolvedSceneResourcePath ResolveIndexedPath(
		const std::string& assetGuid,
		const std::string& editorSourcePath,
		const std::string& artifactPath,
		VansAssetType expectedType) const;

	VansSceneResourceLoadMode m_Mode = VansSceneResourceLoadMode::Editor;
	std::filesystem::path m_ProjectRoot;
	std::filesystem::path m_EngineRoot;
	VansAssetResolver m_AssetResolver;
};
}
