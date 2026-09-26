#include "VansSceneClothAnimationBindingExecutor.h"

#include "../../AnimationCore/VansClothBindingResolver.h"
#include "../../PhysicsCore/VansClothNode.h"
#include "../../AssetCore/VansClothProfile.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../AnimationCore/VansAnimationNode.h"

namespace VansGraphics
{
namespace
{
	VansAnimationNode* FindAnimationNodeForCloth(
		const std::vector<VansAnimationNode*>& animationNodes,
		const std::string& parentName,
		uint32_t parentTransformID)
	{
		if (parentTransformID != UINT32_MAX)
		{
			for (auto* animationNode : animationNodes)
			{
				if (animationNode && animationNode->GetTransformID() == parentTransformID)
					return animationNode;
			}
		}

		for (auto* animationNode : animationNodes)
		{
			if (!animationNode)
				continue;

			for (auto* ownedRenderNode : animationNode->GetRenderNodes())
			{
				if (!ownedRenderNode)
					continue;

				if (!parentName.empty()
					&& (ownedRenderNode->m_NodeName == parentName
						|| ownedRenderNode->m_ParentGroupKey == parentName))
					return animationNode;

				if (parentTransformID != UINT32_MAX
					&& ownedRenderNode->m_TransformID == parentTransformID)
					return animationNode;
			}
		}

		if (animationNodes.size() == 1)
			return animationNodes[0];

		return nullptr;
	}

}

	VansSceneClothAnimationBindingResult VansSceneClothAnimationBindingExecutor::Execute(
		VansScene& scene)
	{
		VansSceneClothAnimationBindingResult result;
		const auto& animationNodes = scene.GetAnimationNodes();
		const auto& sceneObjects = scene.GetSceneObjects();

		for (auto* object : sceneObjects)
		{
			if (!object)
				continue;

			auto* clothComponent = object->GetComponent<VansScriptClothComponent>();
			if (!clothComponent || !clothComponent->m_ClothNode)
				continue;

			if (clothComponent->m_ProfileAssetGuid.empty())
			{
				result.error = "Cloth component has no Profile GUID for object '" +
					object->m_ObjectName + "'";
				return result;
			}

			VansEngine::VansClothNode* clothNode = clothComponent->m_ClothNode;
			if (!clothNode->IsFollowBones())
				continue;
			if (clothNode->GetAnimationNode())
				continue;

			auto* renderComponent = object->GetComponent<VansScriptRenderComponent>();
			if (!renderComponent || !renderComponent->m_RenderNode)
			{
				result.error = "Cloth bone binding has no Render component for object '" +
					object->m_ObjectName + "'";
				return result;
			}

			const std::string& parentName = renderComponent->m_RenderNode->m_ParentGroupKey;
			const uint32_t clothTransformID = renderComponent->m_RenderNode->m_TransformID;
			const uint32_t parentTransformID = scene.GetParentTransformID(clothTransformID);

			VansAnimationNode* foundAnimationNode = FindAnimationNodeForCloth(
				animationNodes,
				parentName,
				parentTransformID);
			if (!foundAnimationNode)
			{
				result.error = "Cloth bone binding could not resolve an AnimationNode for object '" +
					object->m_ObjectName + "'";
				return result;
			}

			Vans::VansAssetGuid profileGuid;
			if (!Vans::VansAssetGuid::TryParse(clothComponent->m_ProfileAssetGuid, profileGuid))
			{
				result.error = "Cloth bone binding has an invalid Profile GUID: " +
					clothComponent->m_ProfileAssetGuid;
				return result;
			}
			const auto profile = Vans::VansProjectManager::Get().GetAssetObjectRepository()
				.ResolveLatest<VansEngine::VansClothProfile>(profileGuid);
			if (!profile)
			{
				result.error = "Cloth Profile is not loaded in memory: " +
					clothComponent->m_ProfileAssetGuid;
				return result;
			}
			std::vector<VansClothPinSkinData> resolvedBindings;
			std::string bindError;
			if (!VansClothBindingResolver::Resolve(
				*profile, foundAnimationNode->GetSkeleton(), resolvedBindings, bindError) ||
				!clothNode->BindAnimation(resolvedBindings, foundAnimationNode, bindError))
			{
				result.error = "Could not bind Cloth bones for object '" +
					object->m_ObjectName + "': " + bindError;
				return result;
			}
			++result.boundCount;
		}
		result.success = true;
		return result;
	}
}
