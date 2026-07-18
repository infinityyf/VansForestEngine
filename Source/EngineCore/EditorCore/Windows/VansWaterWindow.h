#pragma once

#include "VansBaseWindowComponent.h"
#include "../../EngineAPILayer/Public/EngineDTOs.h"

namespace VansGraphics
{
    struct WaterPreset
    {
        const char* name;
        const char* description;
        Vans::EditorAPI::Vec4 deepColor;
        Vans::EditorAPI::Vec4 shallowColor;
        Vans::EditorAPI::Vec3 absorption;
        Vans::EditorAPI::Vec3 scattering;
        float ior;
        float fresnelPower;
        float specularIntensity;
    };

    class VansWaterWindow : public VansBaseWindowComponent
    {
    private:
        void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
        void ApplyPreset(Vans::EditorAPI::WaterSettingsSnapshot& settings, const WaterPreset& preset);
    };

    extern const WaterPreset kWaterPresets[];
    extern const int kWaterPresetCount;
}
