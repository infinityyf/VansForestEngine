#include "../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../GameplayActionSchema/VansGameplayAssetCompiler.h"

#include "Camera/VansCameraGameplayAssetCompiler.h"
#include "../GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"

namespace Vans
{
const VansGameplayAssetSchemaRegistry& VansGameplayAssetSchemaRegistry::BuiltIns()
{
	static const VansGameplayAssetSchemaRegistry registry = []
	{
		VansGameplayAssetSchemaRegistry value;
		std::string error;
		VansRegisterDefaultGameplayAssetSchemas(value, error);
		VansRegisterCameraGameplayAssetSchemas(value, error);
		value.Seal(error);
		return value;
	}();
	return registry;
}

bool VansRegisterDefaultEngineGAFTypes(VansGAFTypeRegistry& registry, std::string& error)
{
	return VansRegisterCoreGAFTypes(registry, error) &&
		VansRegisterGameplayPrimitiveGAFTypes(registry, error) &&
		VansRegisterTimelineGAFTypes(registry, error);
}

bool VansRegisterDefaultEngineGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error)
{
	return VansRegisterCoreGAFSchemas(registry, error) &&
		VansRegisterGameplayPrimitiveGAFSchemas(registry, error) &&
		VansRegisterTimelineGAFSchemas(registry, error);
}

bool VansRegisterDefaultGameplayAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error)
{
	return VansRegisterCoreGameplayAssetCompilers(registry, error) &&
		VansRegisterGameplayPrimitiveAssetCompilers(registry, error) &&
		VansRegisterCameraGameplayAssetCompilers(registry, error);
}
}
