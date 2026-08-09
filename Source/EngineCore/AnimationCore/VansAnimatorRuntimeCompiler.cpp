#include "VansAnimatorRuntimeCompiler.h"

#include "Storage/VansBoneMaskStorage.h"
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
			std::vector<VansAnimationLayerGraphSetup> layers;
			layers.reserve(asset.layers.size());
			for (const VansAnimationLayerDefinition& definition : asset.layers)
			{
				const VansAnimGraph* sourceGraph = asset.FindGraph(definition.graphId);
				if (!sourceGraph)
				{
					error = "Layer '" + definition.name + "' references a missing Graph";
					return nullptr;
				}
				AnimGraphJson graphJson;
				sourceGraph->SerializeToJsonObject(graphJson);
				auto graph = VansAnimGraph::DeserializeFromJsonObject(graphJson);
				if (!graph)
				{
					error = "Failed to instantiate Graph for Layer '" + definition.name + "'";
					return nullptr;
				}

				VansAnimationLayerGraphSetup setup;
				setup.definition = definition;
				setup.graph = std::move(graph);
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

			if (!controller->SetLayerStack(std::move(layers), error)
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
