#pragma once

#include "../VansParticleEmitter.h"
#include "../VansParticleFieldRule.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <string>
#include <vector>

namespace VansGraphics
{
class VansParticleAssetJsonCodec;

enum class VansParticleModulePhase
{
    Initialize,
    Update
};

struct VansParticleModuleSchema
{
    std::string name;
    VansParticleModulePhase phase = VansParticleModulePhase::Initialize;
    std::vector<VansParticleFieldRule> fields;
};

class VansParticleEmitterJsonCodec
{
public:
    static const std::vector<VansParticleModuleSchema>& ModuleSchemas();
    static Vans::VansSerializedValue ModuleDefaults();
    static Vans::VansSerializedValue EncodeEmitter(const VansParticleEmitter& emitter);

private:
    friend class VansParticleAssetJsonCodec;
    static void DecodeEmitter(const Vans::VansSerializedValue& root, VansParticleEmitter& emitter);
};
}
