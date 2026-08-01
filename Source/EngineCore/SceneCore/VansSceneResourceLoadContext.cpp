#include "VansSceneResourceLoadContext.h"

namespace Vans
{
namespace
{
	std::filesystem::path NormalizeRoot(std::filesystem::path root)
	{
		return std::filesystem::absolute(std::move(root)).lexically_normal();
	}

	std::filesystem::path ResolveUnderRoot(
		const std::filesystem::path& root,
		const std::string& path)
	{
		std::filesystem::path fsPath(path);
		if (fsPath.empty())
			return {};
		if (fsPath.is_absolute())
			return fsPath.lexically_normal();
		return (root / fsPath).lexically_normal();
	}
}

VansSceneResourceLoadContext::VansSceneResourceLoadContext(
	VansSceneResourceLoadMode mode,
	std::filesystem::path projectRoot,
	std::filesystem::path engineRoot,
	const std::vector<VansAssetRecord>& assetRecords)
	: m_Mode(mode)
	, m_ProjectRoot(NormalizeRoot(std::move(projectRoot)))
	, m_EngineRoot(NormalizeRoot(std::move(engineRoot)))
	, m_AssetResolver(
		mode == VansSceneResourceLoadMode::Packaged
			? VansAssetAccessMode::Package
			: VansAssetAccessMode::Editor,
		assetRecords)
{
}

VansSceneResourceLoadContext VansSceneResourceLoadContext::ForEditor(
	std::filesystem::path projectRoot,
	std::filesystem::path engineRoot,
	const std::vector<VansAssetRecord>& assetRecords)
{
	return VansSceneResourceLoadContext(
		VansSceneResourceLoadMode::Editor,
		std::move(projectRoot),
		std::move(engineRoot),
		assetRecords);
}

VansSceneResourceLoadContext VansSceneResourceLoadContext::ForPackagedRuntime(
	std::filesystem::path packageContentRoot,
	std::filesystem::path engineRoot,
	const std::vector<VansAssetRecord>& assetRecords)
{
	return VansSceneResourceLoadContext(
		VansSceneResourceLoadMode::Packaged,
		std::move(packageContentRoot),
		std::move(engineRoot),
		assetRecords);
}

std::filesystem::path VansSceneResourceLoadContext::ResolveProjectPath(const std::string& path) const
{
	return ResolveUnderRoot(m_ProjectRoot, path);
}

std::filesystem::path VansSceneResourceLoadContext::ResolveEnginePath(const std::string& path) const
{
	return ResolveUnderRoot(m_EngineRoot, path);
}

VansResolvedSceneResourcePath VansSceneResourceLoadContext::ResolveIndexedPath(
	const std::string& assetGuid,
	const std::string& editorSourcePath,
	const std::string& artifactPath,
	VansAssetType expectedType) const
{
	VansResolvedSceneResourcePath resolved;
	const VansResolvedAsset indexed = m_AssetResolver.Resolve(assetGuid, expectedType);
	resolved.sourcePath = IsPackagedRuntime() ? indexed.readPath : indexed.sourcePath;
	if (!IsPackagedRuntime() && !editorSourcePath.empty())
		resolved.sourcePath = ResolveProjectPath(editorSourcePath);
	resolved.artifactPath = indexed.artifactFormat == VansAssetArtifactFormat::Imported
		? indexed.artifactPath
		: std::filesystem::path{};
	if (!IsPackagedRuntime() && resolved.artifactPath.empty() && !artifactPath.empty())
		resolved.artifactPath = ResolveProjectPath(artifactPath);
	resolved.cookedOnly = IsPackagedRuntime() &&
		indexed.artifactFormat == VansAssetArtifactFormat::Imported;
	resolved.sourceFallbackAllowed = !IsPackagedRuntime();
	resolved.artifactAvailable = indexed.artifactAvailable;
	resolved.sourceAvailable = IsPackagedRuntime() ? indexed.valid : indexed.sourceAvailable;
	resolved.valid = indexed.valid;
	resolved.error = indexed.error;
	return resolved;
}

VansResolvedSceneResourcePath VansSceneResourceLoadContext::ResolveMesh(
	const VansSceneMeshResourceRequest& request) const
{
	return ResolveIndexedPath(request.assetGuid, {}, request.artifactPath, VansAssetType::Model);
}

VansResolvedSceneResourcePath VansSceneResourceLoadContext::ResolveTexture(
	const VansSceneTextureResourceRequest& request) const
{
	return ResolveIndexedPath(request.assetGuid, request.path, request.artifactPath, VansAssetType::Texture);
}

VansResolvedSceneResourcePath VansSceneResourceLoadContext::ResolveShader(
	const VansSceneShaderResourceRequest& request) const
{
	return ResolveIndexedPath(request.assetGuid, request.source, {}, VansAssetType::Shader);
}

VansResolvedSceneResourcePath VansSceneResourceLoadContext::ResolveAudio(
	const VansSceneAudioResourceRequest& request) const
{
	return ResolveIndexedPath(request.assetGuid, {}, {}, VansAssetType::Audio);
}

VansResolvedSceneResourcePath VansSceneResourceLoadContext::ResolveVideo(
	const VansSceneVideoResourceRequest& request) const
{
	return ResolveIndexedPath(request.assetGuid, {}, {}, VansAssetType::Video);
}
}
