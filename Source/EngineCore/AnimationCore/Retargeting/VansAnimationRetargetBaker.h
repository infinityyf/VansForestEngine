#pragma once

#include "VansRetargetProcessor.h"

namespace VansGraphics
{
	struct VansRetargetBakeSettings
	{
		const VansAnimationClip* referenceClip = nullptr;
		float referenceTime = 0.0f;
		std::vector<std::uint8_t> nativeDeltaChannels;
		std::vector<float> nativeDeltaWeights;
		bool normalizeMotionRoot = true;
	};
	// 纯 CPU 转换；文件、GUID 和原子发布由工具入口负责。
	class VansAnimationRetargetBaker
	{
	public:
		static bool Bake(const VansAnimationClip& sourceClip, const Skeleton& sourceSkeleton,
			const Skeleton& targetSkeleton, const VansCompiledAnimationRig& targetRig,
			const VansRetargetRuntimeDesc& profile, VansAnimationClip& output, std::string& error,
			const VansRetargetBakeSettings& settings = {});
	};
}
