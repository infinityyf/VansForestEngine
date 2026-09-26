#pragma once

#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <string>

namespace Vans
{
class VansMotionMatchingConfigCodec
{
public:
	static bool Decode(
		const VansSerializedValue& value,
		VansGraphics::MotionMatchingSettings& settings,
		std::string& error);

	static bool Encode(
		const VansGraphics::MotionMatchingSettings& settings,
		VansSerializedValue& value,
		std::string& error);

	static bool DecodeMotionModel(
		const VansSerializedValue& value,
		VansCharacterMotionSettings& settings,
		std::string& error);
};
}
