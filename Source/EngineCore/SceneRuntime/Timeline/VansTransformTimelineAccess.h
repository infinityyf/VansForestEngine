#pragma once

#include "../../TimelineRuntime/VansTimelineEvaluation.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Vans
{
class VansRuntimeWorld;
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
