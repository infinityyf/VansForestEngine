#pragma once
#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>

namespace VansGraphics
{
    // ── 水质预设（设计文档 §9.3，W-19）─────────────────────────
    struct WaterPreset
    {
        const char* name;
        const char* description;
        glm::vec4   deepColor;
        glm::vec4   shallowColor;
        glm::vec3   absorption;
        glm::vec3   scattering;
        float       ior;
        float       fresnelPower;
        float       specularIntensity;
    };

    // 水面编辑器面板（Phase 3 扩展：W-17 纹理预览 + W-18 LOD 调试 + W-19 预设管理）
    class VansWaterWindow : public VansBaseWindowComponent
    {
    private:
        void ShowWindow(VansVKDevice& device) override;

        // 预设应用（W-19）
        void ApplyPreset(const WaterPreset& preset);
    };

    // 4 个内置水质预设（定义在 VansWaterWindow.cpp）
    extern const WaterPreset kWaterPresets[];
    extern const int kWaterPresetCount;
}
