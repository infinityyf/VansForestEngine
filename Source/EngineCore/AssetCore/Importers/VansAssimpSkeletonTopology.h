#pragma once

#include <assimp/scene.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
	// Assimp 的 aiBone 只保存名称，而 FBX 节点树允许非骨骼节点重名。
	// 这里把蒙皮骨骼解析到唯一的源节点，并用节点身份收集真实祖先链；
	// 禁止再用名称集合扫描整棵树，否则同名 Mesh/Armature 包装节点会污染变形骨架。
	struct VansAssimpSkeletonTopology
	{
		std::unordered_map<std::string, const aiNode*> weightedNodeByName;
		std::unordered_set<const aiNode*> requiredNodes;
		std::vector<std::string> unresolvedWeightedNames;
		std::vector<std::string> ambiguousWeightedNames;

		const aiNode* FindWeightedNode(const std::string& name) const
		{
			const auto found = weightedNodeByName.find(name);
			return found != weightedNodeByName.end() ? found->second : nullptr;
		}

		bool IsWeightedNode(const aiNode* node, const std::string& name) const
		{
			return node && FindWeightedNode(name) == node;
		}
	};

	inline VansAssimpSkeletonTopology BuildAssimpSkeletonTopology(const aiScene* scene)
	{
		VansAssimpSkeletonTopology result;
		if (!scene || !scene->mRootNode)
			return result;

		std::unordered_map<std::string, std::vector<const aiNode*>> nodesByName;
		std::vector<const aiNode*> stack{ scene->mRootNode };
		while (!stack.empty())
		{
			const aiNode* node = stack.back();
			stack.pop_back();
			if (!node)
				continue;
			const std::string name = node->mName.C_Str();
			if (!name.empty())
				nodesByName[name].push_back(node);
			for (unsigned child = 0; child < node->mNumChildren; ++child)
				stack.push_back(node->mChildren[child]);
		}

		std::unordered_set<std::string> weightedNames;
		for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!mesh)
				continue;
			for (unsigned boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
			{
				const aiBone* bone = mesh->mBones[boneIndex];
				if (bone && bone->mName.length > 0)
					weightedNames.emplace(bone->mName.C_Str());
			}
		}

		for (const std::string& name : weightedNames)
		{
			const auto candidates = nodesByName.find(name);
			if (candidates == nodesByName.end() || candidates->second.empty())
			{
				result.unresolvedWeightedNames.push_back(name);
				continue;
			}
			if (candidates->second.size() != 1)
			{
				// aiBone 没有 canonical path，重名时不能安全猜测。调用方必须
				// 拒绝这类资产；静默选择任一节点会把顶点权重绑定到错误骨骼。
				result.ambiguousWeightedNames.push_back(name);
				continue;
			}

			const aiNode* weightedNode = candidates->second.front();
			result.weightedNodeByName.emplace(name, weightedNode);
			for (const aiNode* current = weightedNode;
				 current && current != scene->mRootNode;
				 current = current->mParent)
			{
				result.requiredNodes.insert(current);
			}
		}

		std::sort(result.unresolvedWeightedNames.begin(), result.unresolvedWeightedNames.end());
		std::sort(result.ambiguousWeightedNames.begin(), result.ambiguousWeightedNames.end());
		return result;
	}
}
