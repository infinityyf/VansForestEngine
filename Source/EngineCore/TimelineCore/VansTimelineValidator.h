#pragma once

#include "VansTimelineTrackExtensionRegistry.h"

#include <functional>
#include <unordered_set>

namespace Vans
{
struct VansTimelineValidationContext
{
	const VansTimelineTrackExtensionRegistry* extensions = nullptr;
	bool runtimeValidation = true;
	bool preview = false;
	bool rollbackCapable = false;
	std::function<bool(VansTimelineOutputTypeId)> hasOutputApplier;
	std::function<bool(VansTimelinePayloadTypeId)> hasPayloadSchema;
	std::function<bool(VansTimelinePayloadTypeId, const VansSerializedValue&, std::string&)> validatePayload;
};

class VansTimelineValidator
{
public:
	static VansTimelineDiagnostics Validate(
		const VansTimelineAsset& asset,
		const VansTimelineValidationContext& context);
	static bool HasErrors(const VansTimelineDiagnostics& diagnostics);
};
}
