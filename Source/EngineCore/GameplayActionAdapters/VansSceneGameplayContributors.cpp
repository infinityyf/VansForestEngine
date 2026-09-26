#include "VansSceneGameplayContributors.h"

#include "VansActionServiceAdapter.h"
#include "VansGameplayPrimitivesContributor.h"
#include "Audio/VansAudioActionCapability.h"
#include "Audio/VansAudioActionService.h"
#include "Camera/VansCameraActionService.h"
#include "Camera/VansCameraGameplayAssetCompiler.h"
#include "Character/VansAnimationEventActionService.h"
#include "Character/VansCharacterActionServices.h"
#include "Combat/VansCombatActionService.h"
#include "Combat/VansDamageProfile.h"
#include "Decal/VansDecalActionService.h"
#include "Projectile/VansProjectileActionCapability.h"
#include "Projectile/VansProjectileActionService.h"
#include "VFX/VansVFXActionCapability.h"
#include "VFX/VansVFXActionService.h"
#include "../GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"

#include <array>
#include <string_view>

namespace Vans
{
namespace
{
using CapabilityProvider = const VansActionServiceCapability& (*)();
using SceneServiceFactory = std::shared_ptr<IVansActionService> (*)(
	const VansSceneGameplayContributorContext&,
	const VansGameplayAssetLibrary&,
	std::string&);

struct ServiceModule
{
	std::string_view moduleId;
	std::string_view displayName;
	CapabilityProvider capability = nullptr;
	SceneServiceFactory createSceneService = nullptr;
	VansGameplayModuleAssetCompilerContribution registerAssetCompilers;
	VansGameplayModuleAssetSchemaContribution registerAssetSchemas;
};

bool Enabled(const VansGAFProjectConfiguration& configuration, std::string_view moduleId)
{
	return configuration.allowlist.modules.find(std::string(moduleId)) !=
		configuration.allowlist.modules.end();
}

std::shared_ptr<IVansActionService> CreateCameraService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary& assets,
	std::string& error)
{
	return VansCameraActionService::Create(
		context.camera, assets, error, context.resolveEntityPosition);
}

std::shared_ptr<IVansActionService> CreateCombatService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary& assets,
	std::string& error)
{
	return VansCombatActionService::Create(
		context.world, context.gameplay, assets, error, context.combatBackend);
}

std::shared_ptr<IVansActionService> CreateAudioService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string& error)
{
	if (!context.audio)
	{
		error = "Gameplay.Audio requires the scene audio manager";
		return {};
	}
	return std::make_shared<VansAudioActionService>(
		context.world, *context.audio, context.resolveEntityPosition);
}

std::shared_ptr<IVansActionService> CreateVFXService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string& error)
{
	if (!context.vfxBackend.spawn || !context.vfxBackend.stop ||
		!context.vfxBackend.finished || !context.vfxBackend.destroy)
	{
		error = "Gameplay.VFX requires the scene particle backend";
		return {};
	}
	return std::make_shared<VansVFXActionService>(context.gameplay, context.vfxBackend);
}

std::shared_ptr<IVansActionService> CreateDecalService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string&)
{
	return std::make_shared<VansDecalActionService>(context.decalBackend);
}

std::shared_ptr<IVansActionService> CreateAnimationService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string& error)
{
	return VansAnimationActionService::Create(context.world, error);
}

std::shared_ptr<IVansActionService> CreateNavigationService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string& error)
{
	return VansNavigationActionService::Create(context.world, error);
}

std::shared_ptr<IVansActionService> CreateAnimationEventService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string&)
{
	return std::make_shared<VansAnimationEventActionService>(
		context.world, context.gameplay);
}

std::shared_ptr<IVansActionService> CreateProjectileService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string&)
{
	return std::make_shared<VansProjectileActionService>(
		context.world, context.gameplay, context.projectileBackend);
}

std::shared_ptr<IVansActionService> CreateAttachmentService(
	const VansSceneGameplayContributorContext& context,
	const VansGameplayAssetLibrary&,
	std::string&)
{
	return VansCreateAttachmentActionService(context.projectileBackend);
}

const std::array<ServiceModule, 10>& ServiceModules()
{
	static const std::array<ServiceModule, 10> modules{{
		{ "Gameplay.Camera", "Camera Action Adapter", VansCameraActionCapability,
			CreateCameraService, VansRegisterCameraGameplayAssetCompilers,
			VansRegisterCameraGameplayAssetSchemas },
		{ "Gameplay.Combat", "Combat Action Adapter", VansCombatActionCapability,
			CreateCombatService, VansRegisterCombatGameplayAssetCompilers,
			VansRegisterCombatGameplayAssetSchemas },
		{ "Gameplay.Audio", "Audio Action Adapter", VansAudioActionCapability,
			CreateAudioService, {}, {} },
		{ "Gameplay.VFX", "Particle Effect Adapter", VansVFXActionCapability,
			CreateVFXService, {}, {} },
		{ "Gameplay.Decal", "Impact Decal Adapter", VansDecalActionCapability,
			CreateDecalService, {}, {} },
		{ "Gameplay.Animation", "Animation Action Adapter", VansAnimationActionCapability,
			CreateAnimationService, {}, {} },
		{ "Gameplay.Navigation", "Navigation Action Adapter", VansNavigationActionCapability,
			CreateNavigationService, {}, {} },
		{ "Gameplay.AnimationEvents", "Clip Event Routing", VansAnimationEventActionCapability,
			CreateAnimationEventService, {}, {} },
		{ "Gameplay.Projectile", "Physical Projectiles", VansProjectileActionCapability,
			CreateProjectileService, {}, {} },
		{ "Gameplay.Attachment", "Socket Attachments", VansAttachmentActionCapability,
			CreateAttachmentService, {}, {} }
	}};
	return modules;
}

std::shared_ptr<const IVansGameplayModuleContributor> ProjectContributor(
	const VansGAFProjectConfiguration& configuration)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Project.Script", "Project Script GAF",
			{ "Core" }, {}, VansGAFModuleSource::Project),
		[&configuration](VansGAFTypeRegistry& registry, std::string& error)
		{
			return configuration.RegisterConfiguredTypes(registry, error);
		},
		[&configuration](VansGAFSchemaRegistry& registry, std::string& error)
		{
			return configuration.RegisterConfiguredSchemas(registry, error);
		},
		{});
}

std::shared_ptr<const IVansGameplayModuleContributor> ServiceContributor(
	const ServiceModule& module,
	VansGAFRuntimeRegistry::ServiceFactory serviceFactory)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor(
			std::string(module.moduleId), std::string(module.displayName), { "Core" }),
		{}, {},
		[factory = std::move(serviceFactory)](
			VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.InstantiateService(factory, error);
		},
		module.registerAssetCompilers,
		module.registerAssetSchemas);
}

bool IsKnownModule(std::string_view moduleId)
{
	if (moduleId == "Core" || moduleId == "Core.Graph" ||
		moduleId == "Gameplay.Primitives" || moduleId == "Project.Script" ||
		moduleId == "Timeline") return true;
	for (const ServiceModule& module : ServiceModules())
		if (module.moduleId == moduleId) return true;
	return false;
}

bool BeginDiscovery(
	const VansGAFProjectConfiguration& configuration,
	VansGameplayRuntimeDependencies& dependencies,
	std::string& error)
{
	for (const std::string& moduleId : configuration.allowlist.modules)
		if (!IsKnownModule(moduleId))
		{
			error = "GAF project enables an undiscovered module: " + moduleId;
			return false;
		}
	for (std::string_view required : { std::string_view("Core"), std::string_view("Core.Graph") })
		if (!Enabled(configuration, required))
		{
			error = "GAF project is missing required module: " + std::string(required);
			return false;
		}
	dependencies.projectConfiguration = &configuration;
	return true;
}

void AddSharedContributors(
	const VansGAFProjectConfiguration& configuration,
	VansTimelineRuntimeSystem& timeline,
	VansGameplayRuntimeDependencies& dependencies)
{
	if (Enabled(configuration, "Gameplay.Primitives"))
		dependencies.contributors.push_back(VansMakeGameplayPrimitivesGAFContributor());
	if (Enabled(configuration, "Project.Script"))
		dependencies.contributors.push_back(ProjectContributor(configuration));
	if (Enabled(configuration, "Timeline"))
		dependencies.contributors.push_back(VansMakeTimelineGAFContributor(timeline));
}
}

bool VansDiscoverSceneGameplayContributors(
	const VansGAFProjectConfiguration& configuration,
	const VansSceneGameplayContributorContext& context,
	VansGameplayRuntimeDependencies& dependencies,
	std::string& error)
{
	if (!BeginDiscovery(configuration, dependencies, error)) return false;
	AddSharedContributors(configuration, context.timeline, dependencies);
	for (const ServiceModule& module : ServiceModules())
	{
		if (!Enabled(configuration, module.moduleId)) continue;
		const ServiceModule* descriptor = &module;
		dependencies.contributors.push_back(ServiceContributor(module,
			[descriptor, &context](const VansGameplayAssetLibrary& assets,
				std::string& factoryError)
			{
				return descriptor->createSceneService(context, assets, factoryError);
			}));
	}
	return true;
}

bool VansDiscoverSimulationGameplayContributors(
	const VansGAFProjectConfiguration& configuration,
	VansTimelineRuntimeSystem& timeline,
	VansGameplayRuntimeDependencies& dependencies,
	std::vector<std::shared_ptr<VansFakeActionService>>& services,
	std::string& error)
{
	if (!BeginDiscovery(configuration, dependencies, error)) return false;
	services.clear();
	AddSharedContributors(configuration, timeline, dependencies);
	for (const ServiceModule& module : ServiceModules())
	{
		if (!Enabled(configuration, module.moduleId)) continue;
		auto service = std::make_shared<VansFakeActionService>(module.capability());
		services.push_back(service);
		dependencies.contributors.push_back(ServiceContributor(module,
			[service](const VansGameplayAssetLibrary&, std::string&)
			{
				return service;
			}));
	}
	return true;
}
}
