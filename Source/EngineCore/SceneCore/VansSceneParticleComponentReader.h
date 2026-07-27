#pragma once

#include "VansSceneParticleComponentConfig.h"

#include <optional>

namespace Vans
{
struct VansSerializedValue;

class VansSceneParticleComponentReader
{
public:
	static std::optional<VansSceneParticleComponentConfig> ReadComponents(
		const VansSerializedValue& components);
	static std::optional<VansSceneParticleComponentConfig> ReadParticle(
		const VansSerializedValue& particleNode);
};
}
