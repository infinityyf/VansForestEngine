#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Vans
{
class VansRuntimeWorld;

inline VansTimelineResourceId VansMakeTimelineTransformResource(VansEntityHandle entity)
{
	return {
		VansStableHash64("Scene.Transform"),
		(static_cast<std::uint64_t>(entity.generation) << 32) | (entity.index + 1ull)
	};
}

class IVansTimelineTransformAccess
{
public:
	virtual ~IVansTimelineTransformAccess() = default;
	virtual std::uint32_t ParentTransform(std::uint32_t child) const = 0;
	virtual bool CanWrite(
		const VansResolvedTimelineTarget& target,
		std::string_view physicsPolicy,
		std::string& error) const = 0;
	virtual void NotifyWritten(std::uint32_t transform) = 0;
};
}

namespace VansGraphics
{
class VansScene;
std::shared_ptr<Vans::IVansTimelineTransformAccess> VansCreateTimelineTransformAccess(
	VansScene& scene,
	Vans::VansRuntimeWorld& world);
}
