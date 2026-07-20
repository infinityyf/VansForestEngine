#pragma once

#include "../VansMaterial.h"
#include "VansWaterConfig.h"

namespace VansGraphics
{
    // V2 has one authoring source. GPU data is derived by VansWaterSystem per frame.
    class VansWaterMaterial final : public VansMaterial
    {
    public:
        VansWaterMaterial() = default;
        ~VansWaterMaterial() override = default;

        VansWaterConfig m_Config;
    };
}
