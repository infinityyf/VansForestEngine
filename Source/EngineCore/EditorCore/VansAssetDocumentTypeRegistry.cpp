#include "VansAssetDocumentTypeRegistry.h"

#include "VansEditorPropertyDescriptorRegistry.h"

#include "../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../TimelineCore/VansTimelineSerialization.h"
#include "../TimelineCore/VansTimelineValidator.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Vans
{
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
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansGraphics::VansAnimationRigAsset asset;
		std::string error;
		const nlohmann::json json = EncodeSerializedValueJson<nlohmann::json>(root);
		if (!VansGraphics::VansAnimationRigStorage::DeserializeFromJsonObject(
			json, asset, error))
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
		return result;
	};
	animationRig.collectDependencies = [](
		const std::filesystem::path&, const VansSerializedValue& root)
	{
		VansGraphics::VansAnimationRigAsset asset;
		std::string error;
		const nlohmann::json json = EncodeSerializedValueJson<nlohmann::json>(root);
		if (!VansGraphics::VansAnimationRigStorage::DeserializeFromJsonObject(
			json, asset, error) || asset.skeletonGuid.empty())
			return std::vector<std::string>{};
		std::vector<std::string> dependencies{ asset.skeletonGuid };
		for (const auto& profile : asset.attachmentProfiles)
			if (std::find(dependencies.begin(), dependencies.end(), profile.modelGuid)
				== dependencies.end())
				dependencies.push_back(profile.modelGuid);
		return dependencies;
	};
	Register(VansAssetType::AnimationRig, std::move(animationRig), ignored);

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
				std::string ignored;
				hasConfiguration = VansGAFProjectConfiguration::LoadForProject(
					projectManager.GetProjectRootPath(),
					projectManager.GetPathResolver().GetEngineRoot(), configuration, ignored);
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
