#pragma once

#include "VansPcgMask.h"
#include "../../SceneCore/VansSceneEnvironmentNodeConfig.h"

namespace VansGraphics
{
inline glm::vec2 ToPcgVec2(const Vans::VansSceneFloat2& value)
{
	return glm::vec2(value[0], value[1]);
}

inline PcgMaskConfig ToPcgMaskConfig(const Vans::VansScenePcgMaskConfig& source)
{
	PcgMaskConfig config;
	config.id = source.id.value_or(std::string());
	config.path = source.path.value_or(std::string());
	config.assetGuid = source.assetGuid.value_or(std::string());
	config.textureValue = source.textureValue.value_or(std::string());
	config.channel = source.channel.value_or(std::string());
	if (source.boundsMin) config.boundsMin = ToPcgVec2(*source.boundsMin);
	if (source.boundsMax) config.boundsMax = ToPcgVec2(*source.boundsMax);
	if (source.worldMin) config.worldMin = ToPcgVec2(*source.worldMin);
	if (source.worldMax) config.worldMax = ToPcgVec2(*source.worldMax);
	config.threshold = source.threshold;
	config.densityScale = source.densityScale;
	config.invert = source.invert;
	return config;
}

inline std::vector<PcgMaskConfig> ToPcgMaskConfigs(const std::vector<Vans::VansScenePcgMaskConfig>& sources)
{
	std::vector<PcgMaskConfig> configs;
	configs.reserve(sources.size());
	for (const Vans::VansScenePcgMaskConfig& source : sources)
		configs.push_back(ToPcgMaskConfig(source));
	return configs;
}

inline PcgMaskReference ToPcgMaskReference(const Vans::VansScenePcgMaskReferenceConfig& source)
{
	PcgMaskReference reference;
	reference.ref = source.ref.value_or(std::string());
	if (source.inlineMask)
		reference.inlineMask = ToPcgMaskConfig(*source.inlineMask);
	return reference;
}
}
