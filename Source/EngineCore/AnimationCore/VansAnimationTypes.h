#pragma once

#include "../ScriptCore/VansCommonUtils.h"
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <functional>

namespace VansGraphics
{
	// ─── Constants ───

	constexpr uint32_t MAX_BONES          = 128;
	constexpr uint32_t MAX_BONE_INFLUENCE = 4;
	constexpr uint32_t VCLIP_VERSION      = 1;
	constexpr char     VCLIP_MAGIC[]      = "VCLIP";

	// ─── Enums ───

	enum class AnimationState
	{
		Stopped,
		Playing,
		Paused,
		Blending
	};

	// ─── Keyframe ───

	struct BoneKeyframe
	{
		float     time;       // seconds
		glm::vec3 position;   // local translation
		glm::quat rotation;   // local rotation (quaternion xyzw)
		glm::vec3 scale;      // local scale
	};

	// ─── Bone Info ───

	struct BoneInfo
	{
		int              id            = -1;
		std::string      name;
		glm::mat4        offsetMatrix  = glm::mat4(1.0f);   // bind-pose inverse
		glm::mat4        localTransform  = glm::mat4(1.0f);  // runtime: computed per frame
		glm::mat4        globalTransform = glm::mat4(1.0f);  // runtime: propagated from parent
		int              parentIndex   = -1;                   // -1 = root bone
		std::vector<int> children;
	};

	// ─── Skeleton ───

	struct Skeleton
	{
		std::vector<BoneInfo>                    bones;
		std::unordered_map<std::string, int>     boneNameToIndex;
		glm::mat4                                globalInverseTransform = glm::mat4(1.0f);

		// 拓扑排序后的骨骼索引序列（父骨骼保证在子骨骼之前）
		// UpdateHierarchy 必须按此顺序遍历，否则 parent index > child index 时结果错误
		std::vector<int>                         topologicalOrder;

		// 根据 bones[].parentIndex / children 计算拓扑遍历顺序（BFS）
		void BuildTopologicalOrder()
		{
			const size_t N = bones.size();
			topologicalOrder.clear();
			topologicalOrder.reserve(N);

			// BFS 从所有根骨骼开始
			std::queue<int> q;
			for (size_t i = 0; i < N; i++)
			{
				if (bones[i].parentIndex < 0)
					q.push(static_cast<int>(i));
			}

			while (!q.empty())
			{
				int idx = q.front();
				q.pop();
				topologicalOrder.push_back(idx);
				for (int child : bones[idx].children)
					q.push(child);
			}

			// 安全检查：如果有骨骼未被遍历到（孤立骨骼），追加到末尾
			if (topologicalOrder.size() < N)
			{
				std::vector<bool> visited(N, false);
				for (int idx : topologicalOrder)
					visited[idx] = true;
				for (size_t i = 0; i < N; i++)
				{
					if (!visited[i])
						topologicalOrder.push_back(static_cast<int>(i));
				}
			}
		}
	};

	// ─── Animation Clip ───

	struct VansAnimationClip
	{
		std::string                                clipName;
		float                                      duration       = 0.0f;   // seconds
		float                                      ticksPerSecond = 60.0f;  // sample rate
		// Per-bone keyframe channels: [boneIndex][keyframeIndex]
		std::vector<std::vector<BoneKeyframe>>     boneKeyframes;
	};

	// ─── Clip Info (lightweight, from Peek) ───

	struct VansAnimationClipInfo
	{
		std::string clipName;
		float       duration   = 0.0f;
		uint32_t    boneCount  = 0;
		uint32_t    version    = 0;
	};

	// ─── Play Settings ───

	struct AnimationPlaySettings
	{
		bool  loop          = true;
		bool  autoPlay      = false;
		float speed         = 1.0f;
		float blendDuration = 0.2f;
		float startTime     = 0.0f;
		float endTime       = -1.0f;   // -1 = full clip
		bool  pingPong      = false;
	};

	// ─── Animation Event ───

	struct AnimationEvent
	{
		float                   triggerTime;   // seconds into clip
		std::string             eventName;
		std::function<void()>   callback;
	};

	// ─── Bone Matrices SSBO (uploaded to GPU) ───

	struct BoneMatricesSSBO
	{
		glm::mat4 boneMatrices[MAX_BONES];
	};

	// ─── Vertex Bone Data (per-vertex, 4 influences max) ───
	// Full struct used during import/extraction on CPU side.

	struct VertexBoneData
	{
		int   boneIDs[MAX_BONE_INFLUENCE]  = { -1, -1, -1, -1 };
		float weights[MAX_BONE_INFLUENCE]  = { 0.0f, 0.0f, 0.0f, 0.0f };

		void AddBoneInfluence(int boneID, float weight);
		void Normalize();
	};

	// ─── GPU-side split structs (separate SSBO per submesh, no offset needed) ───
	// Binding 0: Per-vertex bone IDs
	struct VertexBoneID
	{
		int boneIDs[MAX_BONE_INFLUENCE] = { -1, -1, -1, -1 };
	};

	// Binding 2: Per-vertex bone weights
	struct VertexBoneWeight
	{
		float weights[MAX_BONE_INFLUENCE] = { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	// ─── Import Result (returned by VansSkinnedMeshLoader) ───

	struct VansAnimationImportResult
	{
		bool                             hasAnimation = false;
		Skeleton                         skeleton;
		std::vector<VertexBoneData>      vertexBoneData;
		std::vector<VansAnimationClip>   clips;
	};

}  // namespace VansGraphics
