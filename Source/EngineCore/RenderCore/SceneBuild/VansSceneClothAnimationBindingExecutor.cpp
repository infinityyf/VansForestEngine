#include "VansSceneClothAnimationBindingExecutor.h"

#include "../../PhysicsCore/VansClothNode.h"
#include "../../AssetCore/VansClothProfile.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
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
						|| ownedRenderNode->m_ParentGroupName == parentName))
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

	void LogAnimationNodeCandidates(const std::vector<VansAnimationNode*>& animationNodes)
	{
		VANS_LOG("[Pass5] 共有 " << animationNodes.size() << " 个 AnimationNode：");
		for (auto* animationNode : animationNodes)
		{
			if (!animationNode)
				continue;

			VANS_LOG("[Pass5]   AnimNode='" << animationNode->GetName()
				<< "'，RenderNode 数=" << animationNode->GetRenderNodes().size()
				<< "，TransformID=" << animationNode->GetTransformID());
			for (auto* ownedRenderNode : animationNode->GetRenderNodes())
			{
				if (!ownedRenderNode)
					continue;
				VANS_LOG("[Pass5]     RenderNode='" << ownedRenderNode->m_NodeName
					<< "' tid=" << ownedRenderNode->m_TransformID);
			}
		}
	}
}

	void VansSceneClothAnimationBindingExecutor::Execute(VansScene& scene)
	{
		const auto& animationNodes = scene.GetAnimationNodes();
		const auto& sceneObjects = scene.GetSceneObjects();
		VANS_LOG("[Pass5] 开始布料骨骼绑定，场景对象数=" << sceneObjects.size()
			<< "，AnimationNode 数=" << animationNodes.size());

		for (auto* object : sceneObjects)
		{
			if (!object)
				continue;

			auto* clothComponent = object->GetComponent<VansScriptClothComponent>();
			if (!clothComponent || !clothComponent->m_ClothNode)
				continue;

			VANS_LOG("[Pass5] 找到布料对象: '" << object->m_ObjectName
				<< "'，profilePath='" << clothComponent->m_ProfilePath << "'");

			if (clothComponent->m_ProfilePath.empty())
			{
				VANS_LOG_WARN("[Pass5] profilePath 为空，跳过对象 '" << object->m_ObjectName << "'");
				continue;
			}

			VansEngine::VansClothNode* clothNode = clothComponent->m_ClothNode;
			VANS_LOG("[Pass5] ClothNode FollowBones=" << clothNode->IsFollowBones()
				<< "，已有 AnimNode=" << (clothNode->GetAnimationNode() != nullptr ? "是" : "否"));

			if (!clothNode->IsFollowBones())
			{
				VANS_LOG_WARN("[Pass5] followBones=false，跳过。检查 clothprofile 中 followBones 字段。");
				continue;
			}
			if (clothNode->GetAnimationNode())
			{
				VANS_LOG("[Pass5] AnimNode 已绑定，跳过。");
				continue;
			}

			auto* renderComponent = object->GetComponent<VansScriptRenderComponent>();
			if (!renderComponent || !renderComponent->m_RenderNode)
			{
				VANS_LOG_WARN("[Pass5] 对象 '" << object->m_ObjectName << "' 无 RenderComponent，跳过。");
				continue;
			}

			const std::string& nodeName = renderComponent->m_RenderNode->m_NodeName;
			const std::string& parentName = renderComponent->m_RenderNode->m_ParentGroupName;
			const uint32_t clothTransformID = renderComponent->m_RenderNode->m_TransformID;
			const uint32_t parentTransformID = scene.GetParentTransformID(clothTransformID);

			VANS_LOG("[Pass5] RenderNode.m_NodeName='" << nodeName
				<< "'，m_ParentGroupName='" << parentName
				<< "'，clothTransformID=" << clothTransformID
				<< "，parentTransformID=" << parentTransformID);

			LogAnimationNodeCandidates(animationNodes);

			VansAnimationNode* foundAnimationNode = FindAnimationNodeForCloth(
				animationNodes,
				parentName,
				parentTransformID);
			if (!foundAnimationNode)
			{
				VANS_LOG_WARN("[Pass5] 未找到匹配的 AnimNode（parentName='" << parentName
					<< "' parentTransformID=" << parentTransformID << "）");
				continue;
			}

			VANS_LOG("[Pass5] 匹配成功：AnimNode='" << foundAnimationNode->GetName() << "'");

			VansEngine::VansClothProfile profile;
			if (profile.LoadFromFile(clothComponent->m_ProfilePath))
			{
				clothNode->LateBindBonesFromProfile(profile, foundAnimationNode);
				VANS_LOG("[Pass5] 骨骼绑定完成：Cloth '" << clothNode->GetName()
					<< "' → AnimNode '" << foundAnimationNode->GetName() << "'");
			}
			else
			{
				VANS_LOG_ERROR("[Pass5] 无法加载 profile '" << clothComponent->m_ProfilePath << "'");
			}
		}
	}
}
