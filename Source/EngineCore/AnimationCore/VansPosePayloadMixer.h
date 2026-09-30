#pragma once

#include "VansPoseTypes.h"

namespace VansGraphics
{
	class VansPosePayloadMixer
	{
	public:
		// 缺失的一侧按零参与插值；端点只保留对应输入的有效曲线。
		static VansAnimationFrameVector<VansAnimationCurveSample> BlendCurveSamples(
			const VansAnimationFrameVector<VansAnimationCurveSample>& first,
			const VansAnimationFrameVector<VansAnimationCurveSample>& second, float alpha);
		// Weights are already normalized by the graph node. Missing curves contribute zero.
		static VansPosePayload BlendWeighted(
			const VansAnimationFrameVector<VansPosePayload>& poses,
			const VansAnimationFrameVector<float>& weights);
		static VansPosePayload BlendOverride(const VansPosePayload& first,
		                                    const VansPosePayload& second,
		                                    float alpha);
		static VansPosePayload ApplyAdditive(const VansPosePayload& base,
		                                     const VansPosePayload& additive,
		                                     float weight);
	};
}
