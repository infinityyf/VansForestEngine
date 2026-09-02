#include "VansAnimatorRuntimeCompiler.h"

#include "Storage/VansBoneMaskStorage.h"
#include "Storage/VansAnimationRigStorage.h"
#include "VansAnimationClipLoader.h"

#include <nlohmann/json.hpp>
#include <unordered_map>

namespace VansGraphics
{
	std::unique_ptr<VansAnimationController> VansAnimatorRuntimeCompiler::Compile(
		const AnimatorAssetData& asset,
		const Skeleton& skeleton,
		const VansAnimatorClipResolver& clipResolver,
		const VansAnimatorMaskResolver& maskResolver,
		const VansAnimatorRuntimeCompileOptions& options,
		std::string& error)
	{
		error.clear();
		AnimGraphJson validationRoot;
		if (!VansAnimatorIO::SerializeToJsonObject(asset, validationRoot, error))
			return nullptr;
		const std::string rigGuid = options.animationRigGuidOverride.empty()
			? asset.animationRigGuid : options.animationRigGuidOverride;
		if (!options.rigResolver)
		{
			error = "Animator compilation requires an Animation Rig asset resolver";
			return nullptr;
		}
		std::filesystem::path rigPath;
		if (!options.rigResolver(rigGuid, rigPath, error))
			return nullptr;
		VansAnimationRigAsset rigAsset;
		if (!VansAnimationRigStorage::Load(rigPath, rigAsset, error))
		{
			error = "Failed to load Animation Rig '" + rigGuid + "': " + error;
			return nullptr;
		}
		VansCompiledAnimationRig compiledRig;
		if (!VansAnimationRigCompiler::Compile(rigAsset, skeleton, compiledRig, error))
		{
			error = "Animation Rig '" + rigGuid + "' failed compilation: " + error;
			return nullptr;
		}
		std::unordered_map<std::string, VansAnimationClip> clips;
		const bool compileFullGraph = options.mode == VansAnimatorRuntimeCompileMode::FullGraph;
		if (compileFullGraph)
		{
			if (!clipResolver)
			{
				error = "Animator full-graph compilation requires a Clip asset resolver";
				return nullptr;
			}
			if (!VansAnimationClipLoader::LoadClipsFromRefs(
				asset.clipRefs, clipResolver, &skeleton, clips, error))
				return nullptr;
		}

		auto controller = std::make_unique<VansAnimationController>();
		controller->SetName(asset.name);
		if (!controller->SetAnimationRig(
			std::move(compiledRig), options.queryProfileResolver, error))
			return nullptr;
		controller->SetAnimationRigAssetIdentity(rigGuid, rigPath.string());
		for (const AnimatorParameter& parameter : asset.parameters)
		{
			controller->AddParameter(parameter.name, parameter.type);
			switch (parameter.type)
			{
			case AnimatorParamType::Float: controller->SetFloat(parameter.name, parameter.floatVal); break;
			case AnimatorParamType::Bool: controller->SetBool(parameter.name, parameter.boolVal); break;
			case AnimatorParamType::Int: controller->SetInt(parameter.name, parameter.intVal); break;
			case AnimatorParamType::Trigger: break;
			case AnimatorParamType::Vector3: controller->SetVector3(parameter.name, parameter.vec3Val); break;
			case AnimatorParamType::Quaternion: controller->SetQuaternion(parameter.name, parameter.quatVal); break;
			}
		}
		for (auto& [name, clip] : clips)
			controller->AddClip(name, std::move(clip));

		if (compileFullGraph)
		{
			std::vector<VansAnimationLayerSetup> layers;
			layers.reserve(asset.layers.size());
			for (const VansAnimationLayerDefinition& definition : asset.layers)
			{
				VansAnimationLayerSetup setup;
				setup.definition = definition;
				if (definition.kind == VansAnimationLayerKind::Overlay)
				{
					if (!maskResolver)
					{
						error = "Animator full-graph compilation requires a Bone Mask asset resolver";
						return nullptr;
					}
					std::filesystem::path maskPath;
					if (!maskResolver(definition, maskPath, error))
						return nullptr;
					VansBoneMaskAsset mask;
					if (!VansBoneMaskStorage::Load(maskPath, mask, error))
					{
						error = "Failed to load Bone Mask for Layer '" + definition.name + "': " + error;
						return nullptr;
					}
					setup.mask = std::move(mask);
				}
				layers.push_back(std::move(setup));
			}

			std::vector<VansAnimationGraphSetSetup> graphSets;
			graphSets.reserve(asset.graphSets.size());
			for (const VansAnimationGraphSetDefinition& definition : asset.graphSets)
			{
				VansAnimationGraphSetSetup graphSet;
				graphSet.definition = definition;
				graphSet.bindings.reserve(definition.bindings.size());
				for (const VansAnimationGraphBindingDefinition& bindingDefinition : definition.bindings)
				{
					VansAnimationGraphBindingSetup binding;
					binding.definition = bindingDefinition;
					if (bindingDefinition.enabled)
					{
						const VansAnimGraph* sourceGraph = asset.FindGraph(bindingDefinition.graphId);
						if (!sourceGraph)
						{
							error = "Graph Set '" + definition.name
								+ "' references a missing Graph";
							return nullptr;
						}
						AnimGraphJson graphJson;
						sourceGraph->SerializeToJsonObject(graphJson);
						binding.graph = VansAnimGraph::DeserializeFromJsonObject(graphJson);
						if (!binding.graph)
						{
							error = "Failed to instantiate Graph '" + bindingDefinition.graphId
								+ "' for Graph Set '" + definition.name + "'";
							return nullptr;
						}
					}
					graphSet.bindings.push_back(std::move(binding));
				}
				graphSets.push_back(std::move(graphSet));
			}

			if (!controller->SetAnimationGraphSets(
					std::move(layers), std::move(graphSets), asset.defaultGraphSetId,
					asset.defaultGraphSetTransition, asset.graphSetTransitionRules, error)
				|| !controller->SetSlots(asset.slots, error))
				return nullptr;
		}

		if (options.enableTargetPostProcess)
		{
			if (const VansAnimGraph* sourcePostProcess = asset.FindTargetPostProcessGraph())
			{
				AnimGraphJson graphJson;
				sourcePostProcess->SerializeToJsonObject(graphJson);
				auto graph = VansAnimGraph::DeserializeFromJsonObject(graphJson);
				if (!graph)
				{
					error = "Failed to instantiate Target Post Process Graph";
					return nullptr;
				}
				if (!controller->SetTargetPostProcessGraph(std::move(graph), error))
					return nullptr;
			}
		}
		controller->EnableRootMotion(options.enableRootMotion);
		controller->EnableDebugMetrics(options.enableDebugMetrics);
		controller->Update(0.0f, skeleton);
		return controller;
	}
}
