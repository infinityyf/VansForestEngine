#pragma once

#include "../VansAudioBus.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <string>

namespace VansEngine
{
class VansAudioMixValueCodec final
{
public:
    static bool DecodeSnapshot(
        const Vans::VansSerializedValue& value,
        AudioBusSnapshot& snapshot,
        std::string& error);
    static Vans::VansSerializedValue EncodeSnapshot(const AudioBusSnapshot& snapshot);

    static bool DecodeDuckingRule(
        const Vans::VansSerializedValue& value,
        AudioDuckingRule& rule,
        std::string& error);
    static Vans::VansSerializedValue EncodeDuckingRule(const AudioDuckingRule& rule);
};
}
