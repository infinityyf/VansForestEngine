#pragma once

#include "../VansParticleEmitter.h"
#include "../VansParticleJson.h"

namespace VansGraphics
{
class VansParticleEmitterJsonCodec
{
public:
    static Vans::ParticleJson EncodeEmitter(const VansParticleEmitter& emitter);
    static void DecodeEmitter(const Vans::ParticleJson& root, VansParticleEmitter& emitter);
};
}
