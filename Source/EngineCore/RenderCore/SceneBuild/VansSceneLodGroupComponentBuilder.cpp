#include "VansSceneLodGroupComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"
#include "../VansScene.h"
#include "../VulkanCore/VansMesh.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace VansGraphics
{
VansSceneLodGroupDependencies VansSceneLodGroupComponentBuilder::ResolveDependencies(
	VansScene& scene,
	const Vans::VansSceneLodGroupComponentConfig& config)
{
	VansSceneLodGroupDependencies dependencies;
	dependencies.levelMeshes.reserve(config.levels.size());
	for (std::size_t levelIndex = 0; levelIndex < config.levels.size(); ++levelIndex)
	{
		const Vans::VansSceneLodGroupLevelConfig& level = config.levels[levelIndex];
		std::vector<VansMesh*> meshes;
		meshes.reserve(level.modelGuids.size());
		for (std::size_t meshIndex = 0; meshIndex < level.modelGuids.size(); ++meshIndex)
		{
			const std::string& guid = level.modelGuids[meshIndex];
			if (guid.empty())
			{
				meshes.push_back(nullptr);
				continue;
			}

			auto* mesh = static_cast<VansMesh*>(scene.FindMeshAsset(guid));
			if (!mesh)
			{
				dependencies.success = false;
				dependencies.error = "LODGroup level " + std::to_string(levelIndex + 1) +
					" mesh " + std::to_string(meshIndex) + " is unavailable: '" + guid + "'";
				return dependencies;
			}
			if (mesh->m_SubMeshes.size() == 1)
				mesh = mesh->m_SubMeshes.front();
			if (!mesh)
			{
				dependencies.success = false;
				dependencies.error = "LODGroup level " + std::to_string(levelIndex + 1) +
					" mesh " + std::to_string(meshIndex) + " has no renderable mesh: '" + guid + "'";
				return dependencies;
			}
			meshes.push_back(mesh);
		}
		dependencies.levelMeshes.push_back(std::move(meshes));
	}
	return dependencies;
}

VansSceneLodGroupBuildResult VansSceneLodGroupComponentBuilder::Build(
	VansScriptObject& object,
	const Vans::VansSceneLodGroupComponentConfig& config,
	const VansSceneLodGroupDependencies& dependencies,
	VansScriptRenderComponent* renderComponent,
	const std::string& componentGuid)
{
	VansSceneLodGroupBuildResult result;
	if (!dependencies.success)
	{
		result.error = dependencies.error;
		return result;
	}
	if (!renderComponent || renderComponent->m_RenderNodes.empty())
	{
		result.error = "LODGroup requires a built ModelRenderer";
		return result;
	}
	if (dependencies.levelMeshes.size() != config.levels.size())
	{
		result.error = "LODGroup dependency level count does not match its configuration";
		return result;
	}
	for (std::size_t levelIndex = 0; levelIndex < dependencies.levelMeshes.size(); ++levelIndex)
	{
		if (dependencies.levelMeshes[levelIndex].size() > renderComponent->m_RenderNodes.size())
		{
			result.error = "LODGroup level " + std::to_string(levelIndex + 1) +
				" declares more meshes than the ModelRenderer has render nodes";
			return result;
		}
	}

	auto component = std::make_unique<VansScriptLodGroupComponent>();
	component->m_ComponentName = "LODGroup";
	component->m_ComponentGuid = componentGuid;
	component->m_SelectionMode = config.mode;
	component->m_PixelErrorBudget = config.pixelErrorBudget;
	component->m_QualityBias = config.qualityBias;
	component->m_Hysteresis = config.hysteresis;
	component->m_RenderNodes = renderComponent->m_RenderNodes;

	component->m_LevelMeshes.push_back({});
	component->m_LevelMeshes.front().reserve(component->m_RenderNodes.size());
	for (VansRenderNode* node : component->m_RenderNodes)
		component->m_LevelMeshes.front().push_back(node ? node->m_Mesh : nullptr);
	component->m_LevelErrors.push_back(0.0f);
	component->m_LevelScreenHeights.push_back(0.0f);

	for (std::size_t levelIndex = 0; levelIndex < config.levels.size(); ++levelIndex)
	{
		std::vector<VansMesh*> meshes = dependencies.levelMeshes[levelIndex];
		meshes.resize(component->m_RenderNodes.size(), nullptr);
		component->m_LevelMeshes.push_back(std::move(meshes));
		const Vans::VansSceneLodGroupLevelConfig& level = config.levels[levelIndex];
		const float levelError = level.errors.empty()
			? 0.0f : *std::max_element(level.errors.begin(), level.errors.end());
		component->m_LevelErrors.push_back(levelError);
		component->m_LevelScreenHeights.push_back(level.screenHeight);
	}

	result.component = component.release();
	object.AddComponent(result.component);
	result.success = true;
	return result;
}
}
