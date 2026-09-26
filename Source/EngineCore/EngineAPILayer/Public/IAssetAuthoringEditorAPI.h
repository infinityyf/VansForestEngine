#pragma once

#include "EngineDTOs.h"
#include "ModelLodDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class IAssetAuthoringEditorAPI
	{
	public:
		virtual ~IAssetAuthoringEditorAPI() = default;
		virtual ModelLodBuildResult BuildModelLods(const ModelLodBuildRequest& request) = 0;
		virtual ProjectBrowserRootSnapshot GetProjectBrowserRoot() const = 0;
		virtual ShaderAuthoringSchemaSnapshot GetShaderAuthoringSchema(
			const std::string& shaderAssetGuid) const = 0;
		virtual LocalFogFieldPreviewSnapshot GetLocalFogFieldPreview(
			const LocalFogFieldPreviewRequest& request) const = 0;
		virtual ProjectAssetCreateResult CreateProjectAsset(const ProjectAssetCreateRequest& request) = 0;
		virtual Vans::VansSerializedValue QueryPrefabAsset(const std::string& guid) const = 0;
		virtual AssetRefreshResult RefreshProjectAsset(
			const std::string& assetPath, bool importIfMissing) = 0;
		virtual AssetWorkingCopyPublishResult PublishAssetWorkingCopy(
			const AssetWorkingCopyPublishRequest& request) = 0;
	};
}
