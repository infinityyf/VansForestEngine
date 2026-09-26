#include "VansEngineTimelineRegistry.h"

#include "VansAnimationTimelineIntegration.h"
#include "../AudioCore/Timeline/VansAudioTimelineIntegration.h"
#include "../GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"
#include "../ParticleCore/Timeline/VansParticleTimelineIntegration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RenderCore/Timeline/VansCameraTimelineIntegration.h"
#include "../RenderCore/Timeline/VansMediaTimelineIntegration.h"
#include "../RenderCore/Timeline/VansPostProcessTimelineIntegration.h"
#include "../RenderCore/Timeline/VansRenderPropertyTimelineIntegration.h"
#include "../RenderCore/VansScene.h"
#include "../RuntimeUI/Timeline/VansUITimelineIntegration.h"
#include "../SceneRuntime/Timeline/VansActivationTimelineIntegration.h"
#include "../SceneRuntime/Timeline/VansPropertyTimelineIntegration.h"
#include "../SceneRuntime/Timeline/VansTransformTimelineAccess.h"
#include "../SceneRuntime/Timeline/VansTransformTimelineIntegration.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../TimelineRuntime/VansTimelineApplierRegistry.h"
#include "../TimelineRuntime/VansTimelineBuiltInRegistry.h"
#include "../TimelineRuntime/VansTimelineClockRegistry.h"
#include "../TimelineRuntime/VansTimelinePropertyAccessRegistry.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace Vans
{
namespace
{
using ExtensionRegistration = bool(*)(VansTimelineTrackExtensionRegistry&, std::string&);
using PropertyExtensionRegistration = bool(*)(
	VansTimelineTrackExtensionRegistry&,
	const VansTimelinePropertyAccessRegistry&,
	std::string&);
using PropertyRegistration = bool(*)(VansTimelinePropertyAccessRegistry&, std::string&);
using ClockRegistration = bool(*)(VansTimelineClockRegistry&, std::string&);
struct VansTimelineBuildContext
{
	const VansEngineTimelineContext& engine;
	const VansTimelinePropertyAccessRegistry& propertyAccess;
};
using ApplierRegistration = bool(*)(
	const VansTimelineBuildContext&, VansTimelineApplierRegistry&, std::string&);

struct VansTimelineModule
{
	std::string_view name;
	ExtensionRegistration extensions = nullptr;
	PropertyExtensionRegistration propertyExtension = nullptr;
	PropertyRegistration properties = nullptr;
	ClockRegistration clocks = nullptr;
	ApplierRegistration appliers = nullptr;
	std::uint8_t applierOrder = 0;
};

bool RegisterRuntimeClocks(VansTimelineClockRegistry& registry, std::string& error)
{
	for (std::string_view name : { TimelineClockNames::GameTime, TimelineClockNames::Manual })
		if (!registry.Register(VansMakeStableId<VansTimelineClockTag>(name), std::string(name),
			std::make_shared<VansTimelineOwnedClockSource>(), error)) return false;
	return true;
}

bool RegisterSceneAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	auto transformAccess = VansGraphics::VansCreateTimelineTransformAccess(
		context.engine.scene, context.engine.world);
	return VansRegisterTransformTimelineIntegration(
		context.engine.world, transformAccess, registry, error) &&
		VansRegisterActivationTimelineIntegration(context.engine.world, registry, error) &&
		VansRegisterPropertyTimelineIntegration(
			context.engine.world, context.propertyAccess, std::move(transformAccess), registry, error);
}

bool RegisterAnimationAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansRegisterAnimationTimelineIntegration(
		context.engine.world, VansProjectManager::Get().GetAssetObjectRepository(), registry, error);
}

bool RegisterAudioAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansRegisterAudioTimelineIntegration(
		context.engine.world, *context.engine.scene.GetAudioManager(), registry, error);
}

bool RegisterParticleAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansRegisterParticleTimelineIntegration(
		context.engine.world, context.engine.scene.GetParticleManager(), registry, error);
}

bool RegisterCameraAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansGraphics::VansRegisterCameraTimelineIntegration(
		context.engine.world, context.engine.camera, context.engine.cameraControl,
		context.engine.virtualCameraParameters, registry, error);
}

bool RegisterPostProcessAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansGraphics::VansRegisterPostProcessTimelineIntegration(
		context.engine.scene.GetMaterialManager()->m_PostProcessProfile,
		VansProjectManager::Get().GetAssetObjectRepository(), registry, error);
}

bool RegisterRenderPropertyAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansGraphics::VansRegisterRenderPropertyTimelineIntegration(
		context.engine.scene, context.engine.world, registry, error);
}

bool RegisterMediaAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansGraphics::VansRegisterMediaTimelineIntegration(
		context.engine.world, *context.engine.scene.GetVideoManager(), registry, error);
}

bool RegisterUIAppliers(
	const VansTimelineBuildContext&,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansRegisterUITimelineIntegration(registry, error);
}

bool RegisterGameplayActionAppliers(
	const VansTimelineBuildContext& context,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return VansRegisterGameplayActionTimelineIntegration(context.engine.gameplay, registry, error);
}

constexpr std::array<VansTimelineModule, 11> TimelineModules{{
	{ "Timeline.Runtime", VansRegisterTimelineRuntimeExtensions, nullptr, nullptr,
		RegisterRuntimeClocks, nullptr, 0 },
	{ "Timeline.Scene", VansRegisterSceneTimelineExtensions, VansRegisterPropertyTimelineExtension,
		VansRegisterSceneTimelinePropertyAccessors, nullptr, RegisterSceneAppliers, 1 },
	{ "Timeline.Animation", VansRegisterAnimationTimelineExtensions, nullptr, nullptr,
		nullptr, RegisterAnimationAppliers, 5 },
	{ "Timeline.Audio", VansRegisterAudioTimelineExtensions, nullptr,
		VansRegisterAudioTimelinePropertyAccessors, nullptr, RegisterAudioAppliers, 2 },
	{ "Timeline.Particle", VansRegisterParticleTimelineExtensions, nullptr, nullptr,
		nullptr, RegisterParticleAppliers, 3 },
	{ "Timeline.Camera", VansGraphics::VansRegisterCameraTimelineExtensions, nullptr, nullptr,
		nullptr, RegisterCameraAppliers, 10 },
	{ "Timeline.PostProcess", VansGraphics::VansRegisterPostProcessTimelineExtensions, nullptr, nullptr,
		nullptr, RegisterPostProcessAppliers, 8 },
	{ "Timeline.RenderProperty", VansGraphics::VansRegisterRenderPropertyTimelineExtensions, nullptr,
		VansGraphics::VansRegisterRenderTimelinePropertyAccessors, nullptr,
		RegisterRenderPropertyAppliers, 7 },
	{ "Timeline.Media", VansGraphics::VansRegisterMediaTimelineExtensions, nullptr, nullptr,
		nullptr, RegisterMediaAppliers, 6 },
	{ "Timeline.UI", VansRegisterUITimelineExtensions, nullptr, nullptr,
		nullptr, RegisterUIAppliers, 4 },
	{ "Timeline.GameplayAction", VansRegisterGameplayActionTimelineExtensions, nullptr, nullptr,
		nullptr, RegisterGameplayActionAppliers, 9 }
}};

std::string ModuleError(const VansTimelineModule& module, const std::string& error)
{
	return std::string(module.name) + ": " + error;
}

struct VansEngineTimelineCatalogState
{
	VansTimelinePropertyAccessRegistry properties;
	VansTimelineTrackExtensionRegistry extensions;
	VansTimelineClockRegistry clocks;
	std::string error;
	bool valid = false;

	VansEngineTimelineCatalogState()
	{
		for (const VansTimelineModule& module : TimelineModules)
			if (module.properties && !module.properties(properties, error))
			{
				error = ModuleError(module, error);
				return;
			}
		if (!properties.Seal(false, error)) return;

		for (const VansTimelineModule& module : TimelineModules)
		{
			if (module.extensions && !module.extensions(extensions, error))
			{
				error = ModuleError(module, error);
				return;
			}
			if (module.propertyExtension &&
				!module.propertyExtension(extensions, properties, error))
			{
				error = ModuleError(module, error);
				return;
			}
		}
		if (!extensions.Seal(error)) return;

		for (const VansTimelineModule& module : TimelineModules)
			if (module.clocks && !module.clocks(clocks, error))
			{
				error = ModuleError(module, error);
				return;
			}
		if (!clocks.Seal(error)) return;
		valid = true;
	}
};

const VansEngineTimelineCatalogState& GetEngineTimelineCatalogState()
{
	static const VansEngineTimelineCatalogState catalog;
	return catalog;
}
}

VansEngineTimelineCatalog VansGetEngineTimelineCatalog()
{
	const VansEngineTimelineCatalogState& state = GetEngineTimelineCatalogState();
	if (!state.valid) return { nullptr, nullptr, nullptr, state.error };
	return { &state.extensions, &state.properties, &state.clocks, {} };
}

bool VansValidateEngineTimelineRegistries(
	const VansTimelineTrackExtensionRegistry& extensions,
	const VansTimelineApplierRegistry& appliers,
	std::string& error)
{
	error.clear();
	for (const VansTimelineTrackExtensionDescriptor& extension : extensions.All())
	{
		for (const VansTimelineOutputDeclaration& output : extension.outputs)
		{
			const IVansTimelineOutputApplier* applier = appliers.Resolve(output.typeId);
			if (!applier)
			{
				if (output.applierRequired)
				{
					error = "Timeline.ApplierMissing: " + output.stableName;
					return false;
				}
				continue;
			}
			if (applier->PayloadSize() != output.payloadSize ||
				applier->PayloadAlignment() != output.payloadAlignment)
			{
				error = "Timeline.ApplierContractMismatch: " + output.stableName;
				return false;
			}
		}
	}

	const std::vector<VansTimelineOutputTypeId> declared = extensions.DeclaredOutputTypes();
	for (const VansTimelineOutputTypeId type : appliers.OutputTypes())
	{
		if (std::find(declared.begin(), declared.end(), type) != declared.end()) continue;
		const IVansTimelineOutputApplier* applier = appliers.Resolve(type);
		error = "Timeline.TrackOutputMissing: " +
			std::string(applier ? applier->StableName() : std::string_view("Unknown"));
		return false;
	}
	return true;
}

VansEngineTimelineRegistries VansBuildEngineTimelineRegistries(
	const VansEngineTimelineContext& context,
	std::string& error)
{
	error.clear();
	const VansEngineTimelineCatalog catalog = VansGetEngineTimelineCatalog();
	if (!catalog)
	{
		error = std::string(catalog.error);
		return {};
	}
	const VansTimelinePropertyAccessRegistry& propertyAccess = *catalog.propertyAccess;
	const VansTimelineBuildContext buildContext{ context, propertyAccess };
	auto registry = std::make_shared<VansTimelineApplierRegistry>();
	for (std::uint8_t order = 1; order <= 10; ++order)
	{
		const auto module = std::find_if(TimelineModules.begin(), TimelineModules.end(),
			[order](const VansTimelineModule& candidate) { return candidate.applierOrder == order; });
		if (module == TimelineModules.end() || !module->appliers)
		{
			error = "Timeline.ApplierModuleOrderInvalid";
			return {};
		}
		if (!module->appliers(buildContext, *registry, error))
		{
			error = ModuleError(*module, error);
			return {};
		}
	}
	if (!VansValidateEngineTimelineRegistries(
		*catalog.trackExtensions, *registry, error) ||
		!registry->Seal(false, error))
		return {};
	std::uint64_t manifest = VansStableHash64("Timeline.RuntimeRegistryManifest");
	manifest ^= propertyAccess.ManifestHash() + 0x9e3779b97f4a7c15ull + (manifest << 6) + (manifest >> 2);
	manifest ^= registry->ManifestHash() + 0x9e3779b97f4a7c15ull + (manifest << 6) + (manifest >> 2);
	return { std::move(registry), manifest };
}
}
