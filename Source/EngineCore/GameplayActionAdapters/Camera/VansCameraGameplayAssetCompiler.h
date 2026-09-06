#pragma once

#include "../../GameplayActionSchema/VansGameplayAssetCompiler.h"

namespace Vans
{
inline constexpr std::string_view VansCameraRigGameplayAssetType = "Camera.Asset.RigProfile";
inline constexpr std::string_view VansCameraShakeGameplayAssetType = "Camera.Asset.ShakeProfile";

bool VansRegisterCameraGameplayAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error);
bool VansRegisterCameraGameplayAssetSchemas(
	VansGameplayAssetSchemaRegistry& registry,
	std::string& error);
}
