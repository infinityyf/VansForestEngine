#include "../VansScene.h"

#include "VansSceneAnimationComponentBuilder.h"
#include "VansSceneCameraMediaComponentBuilder.h"
#include "VansSceneClothAnimationBindingExecutor.h"
#include "VansSceneLightComponentBuilder.h"
#include "VansSceneParticleComponentBuilder.h"
#include "VansScenePhysicsComponentBuilder.h"
#include "VansSceneRenderNodeBuilder.h"
#include "VansSceneScriptComponentBuilder.h"
#include "VansSceneVehicleComponentBuilder.h"
#include "../../SceneCore/VansSceneObjectBuildPlan.h"
#include "../../SceneCore/VansSceneRuntimeComponentKey.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansMesh.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
	return glm::vec3(value[0], value[1], value[2]);
}

void ApplyRuntimeComponentGuids(
	VansScriptObject& object,
	const std::unordered_map<std::string, std::string>& componentGuids)
{
	for (VansScriptComponent* component : object.m_Components)
	{
		if (!component)
			continue;
		const std::string key = Vans::CanonicalRuntimeComponentKeyForName(component->m_ComponentName);
		const auto found = componentGuids.find(key);
		if (found != componentGuids.end())
			component->m_ComponentGuid = found->second;
	}
}
}

void VansGraphics::VansScene::LoadSceneObjects(
	VkDevice& device,
	const Vans::VansSceneObjectBuildPlan& objectBuildPlan,
	const std::string& projectRoot)
{
	using namespace VansEngine;

	struct ParentLink
	{
		std::string childName;
		std::string parentName;
	};

	struct ParentEntityLink
	{
		uint32_t childTransformID = UINT32_MAX;
		std::string childName;
		std::string parentEntityGuid;
	};

	std::vector<ParentLink> parentLinks;
	std::vector<ParentEntityLink> parentEntityLinks;
	std::vector<VansSceneAnimationComponentBuilder::PendingAnimationComponent> pendingAnimComps;
	std::vector<Vans::VansSceneVehicleObjectConfig> vehicleObjectConfigs;
	std::unordered_set<uint32_t> vehicleDrivenTransformIDs;

	vehicleObjectConfigs.reserve(objectBuildPlan.objects.size());

	// === [VansSceneLoadPass::Pass1_ComponentInstantiation] ===
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		VansScriptObject* obj = new VansScriptObject();
		obj->m_EntityGuid = objectConfig.entityGuid;
		obj->m_ObjectName = objectConfig.name;

		const bool hasObjTransform = objectConfig.transform.has_value();
		glm::vec3 objPos(0.0f), objRot(0.0f), objScl(1.0f);
		if (objectConfig.transform)
		{
			objPos = ToVec3(objectConfig.transform->position);
			objRot = ToVec3(objectConfig.transform->rotation);
			objScl = ToVec3(objectConfig.transform->scale);
		}

		bool objectTransformAllocated = obj->m_OwnsTransform;
		auto ensureObjectTransform = [&]()
		{
			if (!objectTransformAllocated &&
				obj->GetComponent<VansScriptRenderComponent>() == nullptr)
			{
				obj->m_TransformID = VansTransformStore::AllocateTransform();
				obj->m_OwnsTransform = true;
				if (objectConfig.transform)
				{
					auto& transform = VansTransformStore::GetTransform(obj->m_TransformID);
					transform.m_Position = objPos;
					transform.m_Rotation = objRot;
					transform.m_Scale = objScl;
				}
				objectTransformAllocated = true;
			}
		};

		if (objectConfig.render)
		{
			Vans::VansSceneRenderNodeConfig renderConfig = *objectConfig.render;
			if (objectConfig.transform)
				renderConfig.transform = objectConfig.transform;

			VansRenderNode* rn = VansSceneRenderNodeBuilder::LoadSingleRenderNode(*this, device, renderConfig);

			if (!rn)
			{
				auto groupIt = m_MultiMeshGroups.find(renderConfig.name);
				if (groupIt != m_MultiMeshGroups.end() && !groupIt->second.childNodes.empty())
					rn = groupIt->second.childNodes[0];
			}

			if (rn)
			{
				if (hasObjTransform)
					rn->SetTransformData(objPos, objRot, objScl);

				auto* rc = new VansScriptRenderComponent();
				rc->m_ComponentName = "render";
				rc->m_RenderNode = rn;

				if (!objectConfig.renderEnabled && rc->m_RenderNode)
					rc->m_RenderNode->SetEnabled(false);
				rc->m_Enabled = objectConfig.renderEnabled;

				obj->AddComponent(rc);
				obj->m_TransformID = rn->m_TransformID;

				if (!renderConfig.parent.empty())
				{
					ParentLink link;
					link.childName = renderConfig.name;
					link.parentName = renderConfig.parent;
					parentLinks.push_back(std::move(link));
				}

			}
		}

		VansScenePhysicsComponentBuilder::BuildPhysicsClothAndCharacter(
			*this,
			*obj,
			objectConfig.physicsComponents,
			projectRoot,
			hasObjTransform,
			ensureObjectTransform);

		VansSceneVehicleComponentBuilder::AddVehiclePlaceholder(*obj, objectConfig.vehicleObject);
		vehicleObjectConfigs.push_back(objectConfig.vehicleObject);

		if (objectConfig.multiMeshRoot ||
			(objectConfig.animation && obj->GetComponent<VansScriptRenderComponent>() == nullptr))
		{
			ensureObjectTransform();
		}

		VansSceneLightComponentBuilder::BuildLights(
			*this,
			*obj,
			objectConfig.lightComponents,
			projectRoot,
			ensureObjectTransform);

		VansSceneCameraMediaComponentBuilder::BuildCameraAudioVideo(
			*this,
			*obj,
			objectConfig.cameraMediaComponents,
			ensureObjectTransform);

		if (objectConfig.animation)
		{
			VansSceneAnimationComponentBuilder::AddAnimationPlaceholder(
				*obj,
				*objectConfig.animation,
				pendingAnimComps);
		}

		if (objectConfig.particle)
		{
			VansSceneParticleComponentBuilder::BuildParticle(
				*this,
				device,
				*obj,
				*objectConfig.particle,
				projectRoot,
				hasObjTransform,
				objPos,
				objRot,
				objScl);
		}

		VansSceneScriptComponentBuilder::BuildUIControllers(*obj, objectConfig.uiComponents);
		VansSceneScriptComponentBuilder::BuildScripts(*obj, objectConfig.scriptComponents);
		VansSceneLightComponentBuilder::BindExplicitVideoComponentToRectLight(*this, *obj);
		ApplyRuntimeComponentGuids(*obj, objectConfig.componentGuids);

		if (!objectConfig.parentEntityGuid.empty())
		{
			ensureObjectTransform();
			if (obj->m_TransformID != UINT32_MAX)
			{
				ParentEntityLink link;
				link.childTransformID = obj->m_TransformID;
				link.childName = obj->m_ObjectName;
				link.parentEntityGuid = objectConfig.parentEntityGuid;
				parentEntityLinks.push_back(std::move(link));
			}
		}

		m_SceneObjects.push_back(obj);
	}

	// === [VansSceneLoadPass::Pass2_VehicleReference] ===
	vehicleDrivenTransformIDs = VansSceneVehicleComponentBuilder::ResolveVehicles(*this, vehicleObjectConfigs);

	// === [VansSceneLoadPass::Pass3_TransformParent] ===
	for (const auto& link : parentLinks)
	{
		if (link.childName.empty() || link.parentName.empty())
			continue;

		VansRenderNode* childNode = FindRenderNodeByName(link.childName);
		VansRenderNode* parentNode = FindRenderNodeByName(link.parentName);
		if (childNode && parentNode)
		{
			if (vehicleDrivenTransformIDs.count(childNode->m_TransformID) > 0)
				continue;
			m_TransformParentSystem.SetParent(childNode->m_TransformID, parentNode->m_TransformID);
		}
	}

	for (const auto& link : parentEntityLinks)
	{
		if (link.parentEntityGuid.empty())
			continue;

		VansScriptObject* parentObj = FindObjectByGuid(link.parentEntityGuid);
		if (parentObj && parentObj->m_TransformID != UINT32_MAX)
		{
			if (vehicleDrivenTransformIDs.count(link.childTransformID) > 0)
				continue;
			m_TransformParentSystem.SetParent(link.childTransformID, parentObj->m_TransformID);
		}
		else
		{
			VANS_LOG_WARN("[TransformParent] Could not resolve parent entity for child='"
				<< link.childName << "' parentGuid='" << link.parentEntityGuid << "'");
		}
	}

	// === [VansSceneLoadPass::Pass3.5_MultiMeshGroupRebuild] ===
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		if (!objectConfig.multiMeshRoot)
			continue;

		const std::string& parentGuid = objectConfig.entityGuid;
		const std::string& parentName = objectConfig.name;
		if (parentGuid.empty() || parentName.empty())
			continue;

		const std::string& modelGuid = objectConfig.multiMeshRoot->modelGuid;
		VansMesh* sourceMesh = static_cast<VansMesh*>(GetMeshAsset(modelGuid));
		if (!sourceMesh || !sourceMesh->m_IsMultiMesh)
		{
			VANS_LOG_WARN("[MultiMeshGroup] Root '" << parentName
				<< "' references missing/non-multi mesh '" << modelGuid << "'");
			continue;
		}

		MultiMeshGroup& group = m_MultiMeshGroups[parentName];
		group.parentName = parentName;
		group.parentEntityGuid = parentGuid;
		group.sourceMesh = sourceMesh;
		group.childNodes.clear();

		VansScriptObject* parentObj = FindObjectByGuid(parentGuid);
		if (parentObj && parentObj->m_TransformID != UINT32_MAX)
			group.sharedTransformID = parentObj->m_TransformID;

		std::unordered_set<uint32_t> usedIndices;
		for (auto* childObj : m_SceneObjects)
		{
			if (!childObj || childObj->m_EntityGuid.empty())
				continue;
			auto* rc = childObj->GetComponent<VansScriptRenderComponent>();
			if (!rc || !rc->m_RenderNode)
				continue;

			VansRenderNode* node = rc->m_RenderNode;
			if (node->m_ParentEntityGuid != parentGuid)
				continue;
			if (node->m_SourceMesh != sourceMesh)
				continue;
			if (node->m_SubmeshIndex == UINT32_MAX)
				continue;
			if (!usedIndices.insert(node->m_SubmeshIndex).second)
			{
				VANS_LOG_WARN("[MultiMeshGroup] Duplicate submesh index " << node->m_SubmeshIndex
					<< " under root '" << parentName << "', keeping first node.");
				continue;
			}

			const uint32_t oldTransformID = node->m_TransformID;
			if (vehicleDrivenTransformIDs.count(oldTransformID) > 0)
			{
				if (m_TransformParentSystem.HasParent(oldTransformID))
					m_TransformParentSystem.ClearParent(oldTransformID);
				node->m_ParentGroupName = parentName;
				group.childNodes.push_back(node);
				continue;
			}

			if (oldTransformID != group.sharedTransformID)
			{
				if (m_TransformParentSystem.HasParent(oldTransformID))
					m_TransformParentSystem.ClearParent(oldTransformID);
				node->ShareTransform(group.sharedTransformID);
			}
			childObj->m_TransformID = group.sharedTransformID;
			childObj->m_OwnsTransform = false;

			node->m_ParentGroupName = parentName;
			group.childNodes.push_back(node);
		}

		std::sort(group.childNodes.begin(), group.childNodes.end(),
			[](const VansRenderNode* lhs, const VansRenderNode* rhs)
			{
				return lhs->m_SubmeshIndex < rhs->m_SubmeshIndex;
			});
	}

	// === [VansSceneLoadPass::Pass4_AnimationRagdoll] ===
	VansSceneAnimationComponentBuilder::ResolveAnimations(*this, pendingAnimComps, projectRoot);

	// === [VansSceneLoadPass::Pass5_ClothAnimationBinding] ===
	VansSceneClothAnimationBindingExecutor::Execute(*this);

	m_AudioManager.PlayAutoPlay();
}
