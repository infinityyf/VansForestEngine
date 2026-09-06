#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../GameplayActionCore/VansGameplayModuleContributor.h"
#include "../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansGameplayRuntime;
class VansTimelineRuntimeSystem;
class VansTimelineTrackExtensionRegistry;

VansTimelineSessionScope VansMakeExactActionTimelineScope(VansActionHandle action);
bool VansRegisterTimelineGAFTypes(VansGAFTypeRegistry& registry, std::string& error);
bool VansRegisterTimelineGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error);
std::shared_ptr<const IVansGameplayModuleContributor> VansMakeTimelineGAFContributor(
	VansTimelineRuntimeSystem& timelineRuntime);

bool VansRegisterGameplayActionTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterGameplayActionTimelineIntegration(
	VansGameplayRuntime& gameplay,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
