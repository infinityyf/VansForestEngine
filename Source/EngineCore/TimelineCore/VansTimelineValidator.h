#pragma once

#include "VansTimelineAsset.h"

#include <functional>
#include <unordered_set>

namespace Vans
{
struct VansTimelineValidationContext
{
	bool runtimeValidation = true;
	std::unordered_set<VansTimelineCapability> capabilities{
		VansTimelineCapability::Runtime,
		VansTimelineCapability::Editor
	};
	std::function<bool(const std::string& customTypeId)> supportsCustomTrack;
	std::function<bool(
		std::uint16_t componentTypeId,
		const std::string& descriptorId,
		VansTimelineChannelType valueType)> supportsPropertyDescriptor;

	bool Supports(VansTimelineCapability capability) const
	{
		return capabilities.find(capability) != capabilities.end();
	}
};

class VansTimelineValidator
{
public:
	static VansTimelineDiagnostics Validate(
		const VansTimelineAsset& asset,
		const VansTimelineValidationContext& context = {});
	static bool HasErrors(const VansTimelineDiagnostics& diagnostics);
};
}
