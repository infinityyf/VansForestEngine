#include "VansAssetDocumentTypeRegistry.h"

#include "VansEditorPropertyDescriptorRegistry.h"

#include "../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../TimelineCore/VansTimelineSerialization.h"
#include "../TimelineCore/VansTimelineValidator.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../EngineAPILayer/Public/AnimationAuthoringDocumentAnalysis.h"
#include "../AICore/Serialization/VansAIBehaviorJsonCodec.h"
#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../AssetCore/VansShaderAuthoringAsset.h"
#include "../AssetCore/Serialization/VansClothProfileJsonCodec.h"
#include "../AssetCore/Serialization/VansSkinProfileJsonCodec.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AudioCore/VansAudioBusSnapshotAsset.h"
#include "../AudioCore/VansAudioDuckingRulesAsset.h"
#include "../AudioCore/VansAudioReverbPresetAsset.h"
#include "../ParticleCore/Serialization/VansParticleAssetJsonCodec.h"
#include "../PhysicsCore/Serialization/VansRagdollProfileJsonCodec.h"
#include "../RenderCore/Serialization/VansPostProcessProfileJsonCodec.h"
#include "../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../RuntimeUI/Serialization/VansUIComponentConfigReader.h"
#include "../RuntimeUI/Serialization/VansUIDocumentValidator.h"
#include "../RuntimeUI/Serialization/VansUILocalizationReader.h"
#include "../RuntimeUI/Serialization/VansUIScreenConfigReader.h"
#include "../RuntimeUI/Serialization/VansUIThemeTokensReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>

namespace Vans
{
namespace
{
std::vector<VansAssetDocumentDiagnostic> ToDocumentErrors(
	const std::vector<std::string>& messages)
{
	std::vector<VansAssetDocumentDiagnostic> result;
	result.reserve(messages.size());
	for (const std::string& message : messages)
		result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, message });
	return result;
}
}

VansAssetDocumentTypeRegistry::VansAssetDocumentTypeRegistry()
{
	VansAssetDocumentTypeDescriptor timeline;
	timeline.validateBeforeSave = [](const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansTimelineAsset asset;
		std::string error;
		if (!VansTimelineSerialization::DecodeSerialized(root, asset, error))
		{
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
			return result;
		}
		VansTimelineValidationContext context;
		context.runtimeValidation = false;
		context.extensions = &VansTimelineTrackExtensionRegistry::BuiltIns();
		for (const VansTimelineDiagnostic& diagnostic : VansTimelineValidator::Validate(asset, context))
		{
			VansAssetDocumentDiagnosticSeverity severity = VansAssetDocumentDiagnosticSeverity::Info;
			if (diagnostic.severity == VansTimelineDiagnosticSeverity::Warning)
				severity = VansAssetDocumentDiagnosticSeverity::Warning;
			else if (diagnostic.severity == VansTimelineDiagnosticSeverity::Error)
				severity = VansAssetDocumentDiagnosticSeverity::Error;
			result.push_back({ severity, diagnostic.propertyPath, diagnostic.message });
		}
		return result;
	};
	std::string ignored;
	Register(VansAssetType::Timeline, std::move(timeline), ignored);

	VansAssetDocumentTypeDescriptor animationRig;
	animationRig.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		const auto analysis = EditorAPI::AnalyzeAnimationAuthoringDocument(
			path.string(), EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump());
		if (!analysis)
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, analysis.message });
		return result;
	};
	animationRig.collectDependencies = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		const auto analysis = EditorAPI::AnalyzeAnimationAuthoringDocument(
			path.string(), EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump());
		return analysis ? analysis.dependencies : std::vector<std::string>{};
	};
	Register(VansAssetType::AnimationRig, std::move(animationRig), ignored);

	VansAssetDocumentTypeDescriptor retargetProfile;
	retargetProfile.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		const auto analysis = EditorAPI::AnalyzeAnimationAuthoringDocument(
			path.string(), EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump());
		if (!analysis)
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, analysis.message });
		return result;
	};
	Register(VansAssetType::RetargetProfile, std::move(retargetProfile), ignored);

	VansAssetDocumentTypeDescriptor aiBehavior;
	aiBehavior.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansAIBehaviorAsset asset;
		std::string error;
		if (!VansAIBehaviorJsonCodec::Decode(
			EncodeSerializedValueJson<nlohmann::json>(root), asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::AIBehavior, std::move(aiBehavior), ignored);

	VansAssetDocumentTypeDescriptor ragdollProfile;
	ragdollProfile.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansEngine::RagdollProfile asset;
		std::string error;
		if (!VansEngine::VansRagdollProfileJsonCodec::Decode(
			EncodeSerializedValueJson<nlohmann::json>(root), asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::RagdollProfile, std::move(ragdollProfile), ignored);

	VansAssetDocumentTypeDescriptor material;
	material.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansMaterialAuthoringAsset asset;
		std::string error;
		if (!ReadMaterialAuthoringAsset(root, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::Material, std::move(material), ignored);

	VansAssetDocumentTypeDescriptor shader;
	shader.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansShaderAuthoringAsset asset;
		std::string error;
		if (!ReadShaderAuthoringAsset(root, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::Shader, std::move(shader), ignored);

	VansAssetDocumentTypeDescriptor animator;
	animator.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		const auto analysis = EditorAPI::AnalyzeAnimationAuthoringDocument(
			path.string(), EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump());
		if (!analysis)
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, analysis.message });
		return result;
	};
	Register(VansAssetType::AnimatorController, std::move(animator), ignored);

	VansAssetDocumentTypeDescriptor boneMask;
	boneMask.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		const auto analysis = EditorAPI::AnalyzeAnimationAuthoringDocument(
			path.string(), EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump());
		if (!analysis)
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, analysis.message });
		return result;
	};
	Register(VansAssetType::BoneMask, std::move(boneMask), ignored);

	VansAssetDocumentTypeDescriptor cloth;
	cloth.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansEngine::VansClothProfile asset;
		std::string error;
		if (!VansEngine::VansClothProfileJsonCodec::Decode(
			EncodeSerializedValueJson<nlohmann::json>(root), path, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::ClothProfile, std::move(cloth), ignored);

	VansAssetDocumentTypeDescriptor skin;
	skin.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansSkinProfile asset;
		std::string error;
		if (!VansSkinProfileJsonCodec::Decode(
			EncodeSerializedValueJson<nlohmann::json>(root), path, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::SkinProfile, std::move(skin), ignored);

	VansAssetDocumentTypeDescriptor postProcess;
	postProcess.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansGraphics::VansPostProcessProfile asset;
		std::string error;
		if (!VansGraphics::VansPostProcessProfileJsonCodec::Decode(
			EncodeSerializedValueJson<nlohmann::json>(root), path, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::PostProcessProfile, std::move(postProcess), ignored);

	VansAssetDocumentTypeDescriptor particle;
	particle.validateBeforeSave = [](
		const std::filesystem::path& path, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansGraphics::VansParticleAsset asset;
		std::string error;
		if (!VansGraphics::VansParticleAssetJsonCodec::Decode(
			EncodeSerializedValueJson<nlohmann::json>(root), path, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::Particle, std::move(particle), ignored);

	VansAssetDocumentTypeDescriptor audioReverb;
	audioReverb.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansAudioReverbPresetAsset asset;
		std::string error;
		if (!ReadAudioReverbPresetAsset(root, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::AudioReverbPreset, std::move(audioReverb), ignored);

	VansAssetDocumentTypeDescriptor audioSnapshot;
	audioSnapshot.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansAudioBusSnapshotAsset asset;
		std::string error;
		if (!ReadAudioBusSnapshotAsset(root, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::AudioBusSnapshot, std::move(audioSnapshot), ignored);

	VansAssetDocumentTypeDescriptor audioDucking;
	audioDucking.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansAudioDuckingRulesAsset asset;
		std::string error;
		if (!ReadAudioDuckingRulesAsset(root, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	Register(VansAssetType::AudioDuckingRules, std::move(audioDucking), ignored);

	VansAssetDocumentTypeDescriptor uiScreen;
	uiScreen.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansRuntime::VansUIScreenConfig config;
		std::vector<std::string> diagnostics;
		if (VansRuntime::VansUIScreenConfigReader::Read(root, config, diagnostics))
			VansRuntime::VansUIDocumentValidator::ValidateScreenConfig(config, diagnostics);
		return ToDocumentErrors(diagnostics);
	};
	uiScreen.collectDependencies = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansRuntime::VansUIScreenConfig config;
		std::vector<std::string> diagnostics;
		if (!VansRuntime::VansUIScreenConfigReader::Read(root, config, diagnostics))
			return std::vector<std::string>{};
		std::vector<std::string> dependencies;
		const auto append = [&dependencies](const std::string& dependency)
		{
			if (!dependency.empty() &&
				std::find(dependencies.begin(), dependencies.end(), dependency) ==
				dependencies.end())
				dependencies.push_back(dependency);
		};
		append(config.xamlAssetGuid);
		for (const std::string& dependency : config.themeAssetGuids) append(dependency);
		for (const std::string& dependency : config.tokenAssetGuids) append(dependency);
		for (const std::string& dependency : config.localizationAssetGuids) append(dependency);
		for (const std::string& dependency : config.dependencies) append(dependency);
		return dependencies;
	};
	Register(VansAssetType::UIScreen, std::move(uiScreen), ignored);

	VansAssetDocumentTypeDescriptor uiComponent;
	uiComponent.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansRuntime::VansUIComponentConfig config;
		std::vector<std::string> diagnostics;
		if (VansRuntime::VansUIComponentConfigReader::Read(root, config, diagnostics))
			VansRuntime::VansUIDocumentValidator::ValidateComponentConfig(config, diagnostics);
		return ToDocumentErrors(diagnostics);
	};
	uiComponent.collectDependencies = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansRuntime::VansUIComponentConfig config;
		std::vector<std::string> diagnostics;
		if (!VansRuntime::VansUIComponentConfigReader::Read(root, config, diagnostics) ||
			config.xamlAssetGuid.empty())
			return std::vector<std::string>{};
		return std::vector<std::string>{ config.xamlAssetGuid };
	};
	Register(VansAssetType::UIComponent, std::move(uiComponent), ignored);

	VansAssetDocumentTypeDescriptor uiThemeTokens;
	uiThemeTokens.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansRuntime::VansUIThemeTokensConfig config;
		std::vector<std::string> diagnostics;
		VansRuntime::VansUIThemeTokensReader::Read(root, config, diagnostics);
		return ToDocumentErrors(diagnostics);
	};
	Register(VansAssetType::UIThemeTokens, std::move(uiThemeTokens), ignored);

	VansAssetDocumentTypeDescriptor uiLocalization;
	uiLocalization.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansRuntime::VansUILocalizationConfig config;
		std::vector<std::string> diagnostics;
		VansRuntime::VansUILocalizationReader::Read(root, config, diagnostics);
		return ToDocumentErrors(diagnostics);
	};
	Register(VansAssetType::UILocalization, std::move(uiLocalization), ignored);

	VansAssetDocumentTypeDescriptor vegetation;
	vegetation.validateBeforeSave = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansVegetationConfigAsset asset;
		std::string error;
		if (VansVegetationConfigCodec::Decode(root, asset, error))
			return std::vector<VansAssetDocumentDiagnostic>{};
		return std::vector<VansAssetDocumentDiagnostic>{ {
			VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) } };
	};
	vegetation.collectDependencies = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansVegetationConfigAsset asset;
		std::string error;
		if (!VansVegetationConfigCodec::Decode(root, asset, error))
			return std::vector<std::string>{};
		std::vector<std::string> dependencies;
		const auto append = [&dependencies](const std::optional<std::string>& dependency)
		{
			if (dependency && !dependency->empty() &&
				std::find(dependencies.begin(), dependencies.end(), *dependency) ==
				dependencies.end())
				dependencies.push_back(*dependency);
		};
		append(asset.config.material);
		for (const VansSceneVegetationRenderConfig& render : asset.config.renderConfigs)
		{
			append(render.mesh);
			append(render.material);
		}
		for (const VansScenePcgMaskConfig& mask : asset.config.pcgMasks)
			append(mask.assetGuid);
		if (asset.config.trees)
		{
			for (const VansSceneVegetationTreeSpeciesConfig& species :
				asset.config.trees->species)
			{
				for (const VansSceneVegetationTreePartConfig& part : species.parts)
				{
					append(std::optional<std::string>(part.mesh));
					append(std::optional<std::string>(part.material));
				}
			}
		}
		return dependencies;
	};
	Register(VansAssetType::VegetationConfig, std::move(vegetation), ignored);

	const VansGameplayAssetSchemaRegistry& gameplaySchemas = VansGameplayAssetSchemaRegistry::BuiltIns();
	const VansAssetType gameplayAssetTypes[] = {
		VansAssetType::ActionDefinition,
		VansAssetType::ActionSet,
		VansAssetType::GameplayEffect,
		VansAssetType::GameplayCue,
		VansAssetType::AttributeSet,
		VansAssetType::TargetingPolicy,
		VansAssetType::GameplayTagTree,
		VansAssetType::PayloadSchema,
		VansAssetType::ActionGraph,
		VansAssetType::CameraRigProfile,
		VansAssetType::CameraShakeProfile,
		VansAssetType::GAFEditorLayout
	};
	for (const VansAssetType assetType : gameplayAssetTypes)
	{
		VansAssetDocumentTypeDescriptor descriptor;
		descriptor.validateBeforeSave = [assetType, &gameplaySchemas](
			const std::filesystem::path&, const VansSerializedValue& root)
		{
			std::vector<VansAssetDocumentDiagnostic> result;
			VansGameplayDiagnostics diagnostics = gameplaySchemas.Validate(assetType, root);
			VansGAFProjectConfiguration configuration;
			bool hasConfiguration = false;
			auto& projectManager = VansProjectManager::Get();
			if (projectManager.IsProjectLoaded())
			{
				if (const VansGAFProjectConfiguration* active =
					projectManager.GetGAFProjectConfiguration())
				{
					configuration = *active;
					hasConfiguration = true;
				}
				if (hasConfiguration)
				{
					VansGameplayAssetStorage::AppendProjectDiagnostics(
						assetType, root, configuration, diagnostics);
					configuration.ApplyValidationPolicy(diagnostics);
				}
			}
			for (const VansGameplayDiagnostic& diagnostic : diagnostics)
			{
				VansAssetDocumentDiagnosticSeverity severity = VansAssetDocumentDiagnosticSeverity::Info;
				if (diagnostic.severity == VansGameplayDiagnosticSeverity::Warning)
					severity = VansAssetDocumentDiagnosticSeverity::Warning;
				else if ((hasConfiguration && configuration.IsBlockingDiagnostic(
					diagnostic, VansGAFValidationStage::Save)) ||
					diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
					diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
					severity = VansAssetDocumentDiagnosticSeverity::Error;
				result.push_back({ severity, diagnostic.fieldPath,
					diagnostic.code + ": " + diagnostic.message });
			}
			return result;
		};
		descriptor.collectDependencies = [assetType, &gameplaySchemas](
			const std::filesystem::path&, const VansSerializedValue& root)
		{
			return gameplaySchemas.CollectDependencies(assetType, root);
		};
		Register(assetType, std::move(descriptor), ignored);
	}
}

VansAssetDocumentTypeRegistry& VansAssetDocumentTypeRegistry::Get()
{
	static VansAssetDocumentTypeRegistry registry;
	return registry;
}

bool VansAssetDocumentTypeRegistry::Register(
	VansAssetType type,
	VansAssetDocumentTypeDescriptor descriptor,
	std::string& error)
{
	error.clear();
	if (type == VansAssetType::Unknown || !descriptor.validateBeforeSave)
	{
		error = "Asset document type registration requires a concrete type and save validator";
		return false;
	}
	if (!m_Descriptors.emplace(type, std::move(descriptor)).second)
	{
		error = "Asset document type is already registered";
		return false;
	}
	return true;
}

const VansAssetDocumentTypeDescriptor* VansAssetDocumentTypeRegistry::Find(VansAssetType type) const
{
	const auto found = m_Descriptors.find(type);
	return found == m_Descriptors.end() ? nullptr : &found->second;
}

std::vector<VansAssetDocumentDiagnostic> VansAssetDocumentTypeRegistry::ValidateBeforeSave(
	VansAssetType type,
	const std::filesystem::path& path,
	const VansSerializedValue& root) const
{
	const VansAssetDocumentTypeDescriptor* descriptor = Find(type);
	return descriptor && descriptor->validateBeforeSave
		? descriptor->validateBeforeSave(path, root)
		: std::vector<VansAssetDocumentDiagnostic>{};
}
}
