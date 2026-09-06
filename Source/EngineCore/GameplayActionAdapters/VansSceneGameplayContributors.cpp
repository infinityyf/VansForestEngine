#include "VansSceneGameplayContributors.h"
#include "VansGameplayPrimitivesContributor.h"

#include "Camera/VansCameraActionService.h"
#include "Camera/VansCameraGameplayAssetCompiler.h"
#include "Character/VansCharacterActionServices.h"
#include "Character/VansAnimationEventActionService.h"
#include "Combat/VansCombatActionService.h"
#include "../GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"

#include <array>
#include <unordered_set>

namespace Vans
{
namespace
{
bool Enabled(const VansGAFProjectConfiguration& configuration, std::string_view moduleId)
{
	return configuration.allowlist.modules.find(std::string(moduleId)) !=
		configuration.allowlist.modules.end();
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

std::shared_ptr<const IVansGameplayModuleContributor> CameraContributor(
	const VansSceneGameplayContributorContext& context)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Gameplay.Camera", "Camera Action Adapter", { "Core" }),
		{}, {},
		[&camera = context.camera, resolver = context.resolveEntityPosition](
			VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.InstantiateService(
				[&camera, resolver](const VansGameplayAssetLibrary& assets,
					std::string& factoryError) -> std::shared_ptr<IVansActionService>
				{
					return VansCameraActionService::Create(
						camera, assets, factoryError, resolver);
				}, error);
		}, VansRegisterCameraGameplayAssetCompilers,
		VansRegisterCameraGameplayAssetSchemas);
}

std::shared_ptr<const IVansGameplayModuleContributor> CombatContributor(
	const VansSceneGameplayContributorContext& context)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Gameplay.Combat", "Combat Action Adapter", { "Core" }),
		{}, {},
		[&world = context.world, &gameplay = context.gameplay](
			VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.InstantiateService(
				[&world, &gameplay](const VansGameplayAssetLibrary&,
					std::string& factoryError) -> std::shared_ptr<IVansActionService>
				{
					return VansCombatActionService::Create(world, gameplay, factoryError);
				}, error);
		});
}

std::shared_ptr<const IVansGameplayModuleContributor> AnimationContributor(
	const VansSceneGameplayContributorContext& context)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor(
			"Gameplay.Animation", "Animation Action Adapter", { "Core" }),
		{}, {},
		[&world = context.world](VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.InstantiateService(
				[&world](const VansGameplayAssetLibrary&,
					std::string& factoryError) -> std::shared_ptr<IVansActionService>
				{
					return VansAnimationActionService::Create(world, factoryError);
				}, error);
		});
}

std::shared_ptr<const IVansGameplayModuleContributor> NavigationContributor(
	const VansSceneGameplayContributorContext& context)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor(
			"Gameplay.Navigation", "Navigation Action Adapter", { "Core" }),
		{}, {},
		[&world = context.world](VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.InstantiateService(
				[&world](const VansGameplayAssetLibrary&,
					std::string& factoryError) -> std::shared_ptr<IVansActionService>
				{
					return VansNavigationActionService::Create(world, factoryError);
				}, error);
		});
}
}

bool VansDiscoverSceneGameplayContributors(
	const VansGAFProjectConfiguration& configuration,
	const VansSceneGameplayContributorContext& context,
	VansGameplayRuntimeDependencies& dependencies,
	std::string& error)
{
	static const std::unordered_set<std::string> supportedModules{
		"Core", "Core.Graph", "Gameplay.Primitives", "Timeline", "Project.Script",
		"Gameplay.Camera", "Gameplay.Combat", "Gameplay.Animation", "Gameplay.Navigation",
		"Gameplay.AnimationEvents", "Gameplay.Projectile", "Gameplay.Attachment"
	};
	for (const std::string& moduleId : configuration.allowlist.modules)
		if (supportedModules.find(moduleId) == supportedModules.end())
		{
			error = "GAF project enables an undiscovered module: " + moduleId;
			return false;
		}
	for (const char* required : { "Core", "Core.Graph" })
		if (!Enabled(configuration, required))
		{
			error = std::string("GAF project is missing required module: ") + required;
			return false;
		}

	if (Enabled(configuration, "Gameplay.Primitives"))
		dependencies.contributors.push_back(VansMakeGameplayPrimitivesGAFContributor());
	if (Enabled(configuration, "Project.Script"))
		dependencies.contributors.push_back(ProjectContributor(configuration));
	if (Enabled(configuration, "Timeline"))
		dependencies.contributors.push_back(VansMakeTimelineGAFContributor(context.timeline));
	if (Enabled(configuration, "Gameplay.Camera"))
		dependencies.contributors.push_back(CameraContributor(context));
	if (Enabled(configuration, "Gameplay.Combat"))
		dependencies.contributors.push_back(CombatContributor(context));
	if (Enabled(configuration, "Gameplay.Animation"))
		dependencies.contributors.push_back(AnimationContributor(context));
	if (Enabled(configuration, "Gameplay.Navigation"))
		dependencies.contributors.push_back(NavigationContributor(context));
	if (Enabled(configuration, "Gameplay.AnimationEvents"))
		dependencies.contributors.push_back(VansMakeGAFModuleContributor(
			VansMakeGAFModuleDescriptor("Gameplay.AnimationEvents", "Clip Event Routing", { "Core" }), {}, {},
			[&world = context.world, &gameplay = context.gameplay](VansGAFRuntimeRegistry& registry, std::string& error)
			{
				return registry.InstantiateService([&world, &gameplay](const VansGameplayAssetLibrary&, std::string&)
					-> std::shared_ptr<IVansActionService> { return std::make_shared<VansAnimationEventActionService>(world, gameplay); }, error);
			}));
	if (Enabled(configuration, "Gameplay.Projectile"))
		dependencies.contributors.push_back(VansMakeGAFModuleContributor(
			VansMakeGAFModuleDescriptor("Gameplay.Projectile", "Physical Projectiles", { "Core" }), {}, {},
			[&world = context.world, &gameplay = context.gameplay, backend = context.projectileBackend](VansGAFRuntimeRegistry& registry, std::string& error)
			{
				return registry.InstantiateService([&world, &gameplay, backend](const VansGameplayAssetLibrary&, std::string&)
					-> std::shared_ptr<IVansActionService> { return std::make_shared<VansProjectileActionService>(world, gameplay, backend); }, error);
			}));
	if (Enabled(configuration, "Gameplay.Attachment"))
		dependencies.contributors.push_back(VansMakeGAFModuleContributor(
			VansMakeGAFModuleDescriptor("Gameplay.Attachment", "Socket Attachments", { "Core" }), {}, {},
			[backend = context.projectileBackend](VansGAFRuntimeRegistry& registry, std::string& error)
			{
				return registry.InstantiateService([backend](const VansGameplayAssetLibrary&, std::string&)
					{ return VansCreateAttachmentActionService(backend); }, error);
			}));
	return true;
}
}
