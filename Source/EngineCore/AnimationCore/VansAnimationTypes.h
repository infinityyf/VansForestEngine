#pragma once

#include "../ScriptCore/VansCommonUtils.h"
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <string>
#include <string_view>
#include <string_view>
#include <vector>
#include <queue>
#include <unordered_map>
#include <variant>
#include <cstdint>

namespace VansGraphics
{
	// Constants

	// Paragon rigs commonly contain more than 128 deformation/helper bones.
	// Keep this in sync with the GPU bone-matrix storage capacity; the shaders
	// use a runtime-sized SSBO, so raising the CPU-side allocation is sufficient.
	constexpr uint32_t MAX_BONES          = 256;
	constexpr uint32_t MAX_BONE_INFLUENCE = 4;
	constexpr uint32_t VCLIP_VERSION      = 1;
	constexpr char     VCLIP_MAGIC[]      = "VCLIP";

	// Enums

	enum class AnimationState
	{
		Stopped,
		Playing,
		Paused,
		Blending
	};

	// Keyframe

	struct BoneKeyframe
	{
		float     time;       // seconds
		glm::vec3 position;   // local translation
		glm::quat rotation;   // local rotation (quaternion xyzw)
		glm::vec3 scale;      // local scale
	};

	struct TransformKeyframe
	{
		float     time     = 0.0f;  // seconds
		glm::vec3 position = glm::vec3(0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 scale    = glm::vec3(1.0f);
	};

	struct NodeTransformChannel
	{
		std::string nodeName;
		std::string nodePath;
		int         parentChannelIndex = -1;
		glm::mat4   bindLocalTransform = glm::mat4(1.0f);
		glm::mat4   bindModelTransform = glm::mat4(1.0f);
		std::vector<TransformKeyframe> keyframes;
	};

	inline std::uint64_t VansAnimationStableId(std::string_view value)
	{
		// 64-bit FNV-1a. IDs are deterministic across editor, cook and runtime.
		std::uint64_t hash = 14695981039346656037ull;
		for (const unsigned char character : value)
		{
			hash ^= character;
			hash *= 1099511628211ull;
		}
		return hash;
	}

	struct AnimationCurveKey
	{
		float time = 0.0f;
		float value = 0.0f;
	};

	struct AnimationCurveTrack
	{
		std::uint64_t id = 0;
		std::string name;
		std::vector<AnimationCurveKey> keys;
	};

	using AnimationEventValue = std::variant<std::monostate, bool, std::int64_t,
	                                         double, std::string, glm::vec3>;

	struct AnimationClipEvent
	{
		std::uint64_t id = 0;
		float time = 0.0f;
		std::string name;
		AnimationEventValue payload;
	};

	struct AnimationSyncMarker
	{
		std::uint64_t id = 0;
		float time = 0.0f;
		std::string name;
	};

	struct AnimationRootMotionTrack
	{
		bool enabled = true;
		std::string boneName;
		bool extractTranslation = true;
		bool extractRotation = true;
		bool extractScale = false;
	};

	struct SampledNodeTransform
	{
		uint32_t    channelIndex = 0;
		std::string_view nodeName;
		std::string_view nodePath;
		glm::mat4   modelTransform = glm::mat4(1.0f);
	};

	// Bone Info

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

	// Skeleton

	struct Skeleton
	{
		std::vector<BoneInfo>                    bones;
		std::unordered_map<std::string, int>     boneNameToIndex;
		glm::mat4                                globalInverseTransform = glm::mat4(1.0f);

		// Bone indices in topological order. Parents are guaranteed to appear before children.
		// UpdateHierarchy must iterate in this order; otherwise parent indices greater than child
		// indices can produce incorrect transforms.
		std::vector<int> topologicalOrder;

		// Recompute topological traversal order from bones[].parentIndex and children using BFS.
		void BuildTopologicalOrder()
		{
			const size_t N = bones.size();
			topologicalOrder.clear();
			topologicalOrder.reserve(N);

			// Start BFS from every root bone.
			std::queue<int> q;
			for (size_t i = 0; i < N; i++)
			{
				if (bones[i].parentIndex == static_cast<int>(i) ||
				    bones[i].parentIndex >= static_cast<int>(N))
				{
					bones[i].parentIndex = -1;
				}

				if (bones[i].parentIndex < 0)
					q.push(static_cast<int>(i));
			}

			std::vector<bool> visited(N, false);
			while (!q.empty())
			{
				int idx = q.front();
				q.pop();
				if (idx < 0 || idx >= static_cast<int>(N) || visited[idx])
					continue;

				visited[idx] = true;
				topologicalOrder.push_back(idx);
				for (int child : bones[idx].children)
				{
					if (child >= 0 && child < static_cast<int>(N) && child != idx)
						q.push(child);
				}
			}

			// Safety check: append any isolated bones that were not reached by traversal.
			if (topologicalOrder.size() < N)
			{
				for (size_t i = 0; i < N; i++)
				{
					if (!visited[i])
					{
						bones[i].parentIndex = -1;
						topologicalOrder.push_back(static_cast<int>(i));
					}
				}
			}
		}
	};

	// Animation Clip

	struct VansAnimationClip
	{
		std::uint64_t                            stableId       = 0;
		std::string                                clipName;
		float                                      duration       = 0.0f;   // seconds
		float                                      ticksPerSecond = 60.0f;  // sample rate
		// Per-bone keyframe channels: [boneIndex][keyframeIndex]
		std::vector<std::vector<BoneKeyframe>>     boneKeyframes;
		std::vector<NodeTransformChannel>          nodeTransformChannels;
		std::vector<AnimationCurveTrack>           curves;
		std::vector<AnimationClipEvent>            events;
		std::vector<AnimationSyncMarker>           syncMarkers;
		std::string                                syncGroupName;
		AnimationRootMotionTrack                   rootMotion;
	};

	// Clip Info (lightweight, from Peek)

	struct VansAnimationClipInfo
	{
		std::string clipName;
		float       duration   = 0.0f;
		uint32_t    boneCount  = 0;
		uint32_t    nodeTransformChannelCount = 0;
		uint32_t    curveCount = 0;
		uint32_t    eventCount = 0;
		uint32_t    syncMarkerCount = 0;
		uint32_t    version    = 0;
	};

	// Play Settings

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

	// Bone Matrices SSBO (uploaded to GPU)

	struct BoneMatricesSSBO
	{
		glm::mat4 boneMatrices[MAX_BONES];
	};

	// Vertex Bone Data (per-vertex, 4 influences max)
	// Full struct used during import/extraction on CPU side.

	struct VertexBoneData
	{
		int   boneIDs[MAX_BONE_INFLUENCE]  = { -1, -1, -1, -1 };
		float weights[MAX_BONE_INFLUENCE]  = { 0.0f, 0.0f, 0.0f, 0.0f };

		void AddBoneInfluence(int boneID, float weight);
		void Normalize();
	};

	// GPU-side split structs (separate SSBO per submesh, no offset needed)
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

	// Import Result (returned by VansSkinnedMeshLoader)

	struct VansAnimationImportResult
	{
		bool                             hasAnimation = false;
		Skeleton                         skeleton;
		std::vector<VertexBoneData>      vertexBoneData;
		std::vector<VansAnimationClip>   clips;
	};

}  // namespace VansGraphics
