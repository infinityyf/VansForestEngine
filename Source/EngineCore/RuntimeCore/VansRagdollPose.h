#pragma once

#include <../../GLM/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Vans
{
	struct VansRagdollKey
	{
		std::uint32_t transformId = (std::numeric_limits<std::uint32_t>::max)();

		bool IsValid() const
		{
			return transformId != (std::numeric_limits<std::uint32_t>::max)();
		}

		bool operator==(const VansRagdollKey& other) const
		{
			return transformId == other.transformId;
		}
	};

	// 创建期从 Animation skeleton 复制一次的纯值绑定。
	struct VansRagdollSkeletonBinding
	{
		std::vector<std::string> boneNames;
		std::vector<int> parentIndices;
		std::vector<int> topologicalOrder;
	};

	// 帧内只读视图；调用结束后 Physics 不保存 modelTransforms 指针。
	struct VansRagdollPoseView
	{
		glm::mat4 rootWorld{ 1.0f };
		const glm::mat4* modelTransforms = nullptr;
		std::size_t modelTransformCount = 0;

		bool IsValid() const
		{
			return modelTransforms != nullptr && modelTransformCount > 0;
		}
	};

	// Physics 返回的完整 model-space pose，由 AnimationNode 负责提交。
	struct VansRagdollPose
	{
		std::vector<glm::mat4> modelTransforms;
	};
}
