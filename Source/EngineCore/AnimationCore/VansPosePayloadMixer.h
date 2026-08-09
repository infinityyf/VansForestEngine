#pragma once

#include "VansPoseTypes.h"

namespace VansGraphics
{
	class VansPosePayloadMixer
	{
	public:
		static VansPosePayload BlendOverride(const VansPosePayload& first,
		                                    const VansPosePayload& second,
		                                    float alpha);
		static VansPosePayload ApplyAdditive(const VansPosePayload& base,
		                                     const VansPosePayload& additive,
		                                     float weight);
	};
}
