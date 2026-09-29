#pragma once

#include "VansPoseTypes.h"

namespace VansGraphics
{
	class VansPosePayloadMixer
	{
	public:
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
