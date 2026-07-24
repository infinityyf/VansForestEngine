#include "VansWaterWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace VansGraphics
{
namespace
{
    bool EditVec2(const char* label, Vans::EditorAPI::Vec2& value, float speed, float minValue, float maxValue, const char* format)
    {
        float values[2] = { value.x, value.y };
        if (!ImGui::DragFloat2(label, values, speed, minValue, maxValue, format))
            return false;

        value = { values[0], values[1] };
        return true;
    }

    bool EditVec3(const char* label, Vans::EditorAPI::Vec3& value, float speed, float minValue, float maxValue, const char* format)
    {
        float values[3] = { value.x, value.y, value.z };
        if (!ImGui::DragFloat3(label, values, speed, minValue, maxValue, format))
            return false;

        value = { values[0], values[1], values[2] };
        return true;
    }

    bool EditColor4(const char* label, Vans::EditorAPI::Vec4& value)
    {
        float values[4] = { value.x, value.y, value.z, value.w };
        if (!ImGui::ColorEdit4(label, values))
            return false;

        value = { values[0], values[1], values[2], values[3] };
        return true;
    }

    void DisplayWaterTexture(
        Vans::EditorAPI::IEngineEditorAPI& editorAPI,
        const char* label,
        const char* textureName,
        std::uint32_t requestedLayer = 0u)
    {
        ImGui::Text("%s", label);
        ImGui::SameLine();
        ImGui::TextDisabled("layer %u", requestedLayer);

        Vans::EditorAPI::RenderTextureFilter filter;
        filter.category = "water";
        filter.name = textureName;
        filter.layer = requestedLayer;
        std::vector<Vans::EditorAPI::RenderTexturePreview> previews =
            editorAPI.QueryRenderTexturePreviews(filter);
        const Vans::EditorAPI::RenderTexturePreview preview =
            previews.empty() ? Vans::EditorAPI::RenderTexturePreview{} : previews.front();
        if (!preview.texture || preview.width == 0 || preview.height == 0)
        {
            ImGui::TextDisabled("(not created)");
            return;
        }

        const float aspect = static_cast<float>(preview.width) / static_cast<float>(preview.height);
        float width = ImGui::GetContentRegionAvail().x * 0.95f;
        float height = width / aspect;
        constexpr float MAX_PREVIEW_HEIGHT = 220.0f;
        if (height > MAX_PREVIEW_HEIGHT)
        {
            height = MAX_PREVIEW_HEIGHT;
            width = height * aspect;
        }
        ImGui::Image(preview.texture, ImVec2(width, height));
    }
}

const WaterPreset kWaterPresets[] = {
    {
        "Tropical Ocean", "Warm clear ocean",
        { 0.0f, 0.08f, 0.28f, 1.0f }, { 0.05f, 0.35f, 0.55f, 1.0f },
        { 0.35f, 0.12f, 0.03f }, { 0.02f, 0.05f, 0.08f },
        1.33f, 5.5f, 1.2f
    },
    {
        "Temperate Lake", "Calm inland water",
        { 0.02f, 0.05f, 0.10f, 1.0f }, { 0.08f, 0.25f, 0.35f, 1.0f },
        { 0.20f, 0.10f, 0.04f }, { 0.03f, 0.05f, 0.07f },
        1.34f, 4.5f, 0.7f
    },
    {
        "Arctic Sea", "Cold bright water",
        { 0.0f, 0.03f, 0.08f, 1.0f }, { 0.12f, 0.45f, 0.60f, 1.0f },
        { 0.30f, 0.08f, 0.02f }, { 0.01f, 0.02f, 0.04f },
        1.33f, 6.0f, 1.5f
    },
    {
        "Muddy River", "Sediment-heavy river",
        { 0.08f, 0.06f, 0.02f, 1.0f }, { 0.15f, 0.12f, 0.06f, 1.0f },
        { 0.10f, 0.08f, 0.22f }, { 0.08f, 0.06f, 0.02f },
        1.34f, 3.0f, 0.3f
    }
};
const int kWaterPresetCount = 4;

void VansWaterWindow::ApplyPreset(Vans::EditorAPI::WaterSettingsSnapshot& settings, const WaterPreset& preset)
{
    settings.medium.deepColor = preset.deepColor;
    settings.medium.shallowColor = preset.shallowColor;
    settings.medium.absorptionCoeff = preset.absorption;
    settings.medium.scatteringCoeff = preset.scattering;
    settings.medium.ior = preset.ior;
    settings.medium.fresnelPower = preset.fresnelPower;
    settings.specularIntensity = preset.specularIntensity;
}

void VansWaterWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!VansEditorWindow::m_WaterWindowOpen)
        return;

    Vans::EditorAPI::WaterSettingsSnapshot settings = editorAPI.GetWaterSettings();
    Vans::EditorAPI::WaterRuntimeStats stats = editorAPI.GetWaterRuntimeStats();

    if (!settings.available)
    {
        ImGui::Begin("Water");
        ImGui::TextDisabled("Scene has no water config. Add a water block to Scene JSON and reload.");
        ImGui::End();
        return;
    }

    bool changed = false;
    ImGui::Begin("Water");

    if (ImGui::BeginTabBar("WaterTabs"))
    {
        if (ImGui::BeginTabItem("Parameters"))
        {
            if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (int i = 0; i < kWaterPresetCount; ++i)
                {
                    const WaterPreset& preset = kWaterPresets[i];
                    if (ImGui::Button(preset.name))
                    {
                        ApplyPreset(settings, preset);
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", preset.description);
                }
                ImGui::Separator();
            }

            changed |= ImGui::DragFloat("Specular Intensity", &settings.specularIntensity, 0.01f, 0.0f, 10.0f, "%.3f");
            changed |= ImGui::DragFloat("Water Roughness", &settings.medium.waterRoughness, 0.001f, 0.001f, 1.0f, "%.4f");

            if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("Geometry: Infinite Ocean (V2)");
                changed |= ImGui::DragFloat("Water Level (Y)", &settings.waterLevel, 0.1f, -10000.0f, 10000.0f, "%.2f");
            }

            if (ImGui::CollapsingHeader("Medium"))
            {
                changed |= EditVec3("Absorption (RGB)", settings.medium.absorptionCoeff, 0.001f, 0.0f, 10.0f, "%.4f");
                changed |= EditVec3("Scattering (RGB)", settings.medium.scatteringCoeff, 0.001f, 0.0f, 10.0f, "%.4f");
                changed |= ImGui::DragFloat("IOR", &settings.medium.ior, 0.001f, 1.0f, 3.0f, "%.4f");
                changed |= ImGui::DragFloat("Fresnel Power", &settings.medium.fresnelPower, 0.1f, 0.1f, 20.0f, "%.2f");
                changed |= ImGui::DragFloat("Anisotropy", &settings.medium.anisotropy, 0.01f, 0.0f, 1.0f, "%.3f");
                changed |= EditColor4("Deep Color", settings.medium.deepColor);
                changed |= EditColor4("Shallow Color", settings.medium.shallowColor);
            }

            if (ImGui::CollapsingHeader("Waves"))
            {
                const char* modeNames[] = { "Gerstner", "FFT", "Wave Particle" };
                int modeIndex = std::clamp(settings.spectrum.mode, 0, 2);
                if (ImGui::Combo("Wave Mode", &modeIndex, modeNames, 3))
                {
                    settings.spectrum.mode = modeIndex;
                    changed = true;
                }

                changed |= ImGui::SliderInt("Spectrum Cascades", &settings.spectrum.cascadeCount, 1, 4);
                changed |= ImGui::DragFloat("Base Coverage (m)", &settings.spectrum.baseCoverage, 1.0f, 4.0f, 2048.0f, "%.1f");
                changed |= ImGui::DragFloat("Cascade Scale", &settings.spectrum.cascadeScale, 0.1f, 2.0f, 8.0f, "%.2f");
                ImGui::TextDisabled("FFT resolution: 256 (runtime invariant)");
                changed |= EditVec2("Wind Dir (XZ)", settings.spectrum.windDirection, 0.01f, -1.0f, 1.0f, "%.3f");
                changed |= ImGui::DragFloat("Wind Speed", &settings.spectrum.windSpeed, 0.1f, 0.0f, 100.0f, "%.2f");
                changed |= ImGui::DragFloat("Choppiness", &settings.spectrum.choppiness, 0.01f, 0.0f, 3.0f, "%.3f");

                if (settings.spectrum.mode == 0)
                {
                    ImGui::SeparatorText("Gerstner");
                    changed |= ImGui::DragFloat("Swell Amplitude", &settings.spectrum.swellAmplitude, 0.01f, 0.0f, 20.0f, "%.3f");
                    changed |= ImGui::SliderInt("Gerstner Components", &settings.spectrum.gerstnerWaveCount, 0, 64);
                }

                if (settings.spectrum.mode == 1)
                {
                    ImGui::SeparatorText("Tessendorf Spectrum");
                    changed |= ImGui::DragFloat("Spectrum Amplitude", &settings.spectrum.spectrumAmplitude, 0.00005f, 0.0f, 0.02f, "%.6f");
                    changed |= ImGui::DragFloat("Macro Min Wavelength", &settings.spectrum.minWavelength, 0.01f, 0.05f, 8.0f, "%.3f m");
                    changed |= ImGui::DragFloat("Small Wave Damping", &settings.spectrum.smallWaveDamping, 0.0001f, 0.0f, 0.1f, "%.5f");
                    changed |= ImGui::DragFloat("Wind Dependency", &settings.spectrum.windDependency, 0.01f, 0.0f, 1.0f, "%.3f");
                    changed |= ImGui::DragFloat("Water Depth", &settings.spectrum.depth, 1.0f, 0.1f, 10000.0f, "%.1f");
                    changed |= ImGui::DragFloat("Repeat Period", &settings.spectrum.repeatPeriod, 1.0f, 0.0f, 600.0f, "%.1f");
                    int spectrumSeed = static_cast<int>(settings.spectrum.randomSeed);
                    if (ImGui::DragInt("FFT Seed", &spectrumSeed, 1.0f, 0, 0x7fffffff))
                    {
                        settings.spectrum.randomSeed = static_cast<std::uint32_t>((std::max)(spectrumSeed, 0));
                        changed = true;
                    }
                }

                if (settings.spectrum.mode == 2)
                {
                    ImGui::SeparatorText("Wave Particle");
                    const char* profileNames[] = { "Gaussian", "Compact Ripple", "Sharp Crest" };
                    int profileIndex = std::clamp(settings.waveParticle.profile, 0, 2);
                    if (ImGui::Combo("Wave Profile", &profileIndex, profileNames, 3))
                    {
                        settings.waveParticle.profile = profileIndex;
                        changed = true;
                    }
                    changed |= ImGui::SliderInt("Particle Count", &settings.waveParticle.particleCount, 0, 1024);
                    changed |= ImGui::SliderInt("Octave Layers", &settings.waveParticle.octaveCount, 1, 8);
                    changed |= ImGui::DragFloat("Particle Domain (m)", &settings.waveParticle.domainSize, 1.0f, 16.0f, 4096.0f, "%.1f");
                    changed |= ImGui::DragFloat("Particle Amplitude", &settings.waveParticle.amplitude, 0.005f, 0.0f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Min Radius (m)", &settings.waveParticle.minRadius, 0.05f, 0.05f, 512.0f, "%.2f");
                    changed |= ImGui::DragFloat("Max Radius (m)", &settings.waveParticle.maxRadius, 0.1f, 0.05f, 4096.0f, "%.2f");
                    changed |= ImGui::DragFloat("Phase Velocity", &settings.waveParticle.phaseVelocity, 0.01f, 0.0f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Damping", &settings.waveParticle.damping, 0.001f, 0.0f, 2.0f, "%.4f");
                    changed |= ImGui::DragFloat("Direction Spread", &settings.waveParticle.directionSpread, 0.01f, 0.0f, 3.1416f, "%.3f rad");
                    changed |= ImGui::DragFloat("Lacunarity", &settings.waveParticle.lacunarity, 0.01f, 1.01f, 4.0f, "%.3f");
                    changed |= ImGui::DragFloat("Persistence", &settings.waveParticle.persistence, 0.01f, 0.0f, 1.0f, "%.3f");
                    changed |= ImGui::DragFloat("Radius Falloff", &settings.waveParticle.radiusFalloff, 0.01f, 0.1f, 1.0f, "%.3f");
                    changed |= ImGui::DragFloat("Profile Sharpness", &settings.waveParticle.profileSharpness, 0.01f, 0.25f, 8.0f, "%.3f");
                    changed |= ImGui::DragFloat("Foam Threshold", &settings.waveParticle.foamThreshold, 0.01f, 0.0f, 2.0f, "%.3f");
                    changed |= ImGui::DragFloat("Foam Softness", &settings.waveParticle.foamSoftness, 0.01f, 0.01f, 2.0f, "%.3f");
                    changed |= ImGui::DragFloat("Particle Lifetime", &settings.waveParticle.lifetime, 0.1f, 0.1f, 600.0f, "%.1f s");
                    int particleSeed = static_cast<int>(settings.waveParticle.randomSeed);
                    if (ImGui::DragInt("Particle Seed", &particleSeed, 1.0f, 0, 0x7fffffff))
                    {
                        settings.waveParticle.randomSeed = static_cast<std::uint32_t>((std::max)(particleSeed, 0));
                        changed = true;
                    }
                }
            }

            if (ImGui::CollapsingHeader("Geometry LOD"))
            {
                changed |= ImGui::SliderInt("LOD Count", &settings.geometry.lodCount, 1, 10);
                changed |= ImGui::DragFloat("Base Patch Size", &settings.geometry.basePatchSize, 0.5f, 1.0f, 512.0f, "%.1f");
                changed |= ImGui::DragFloat("Morph Start Ratio", &settings.geometry.morphStartRatio, 0.01f, 0.05f, 0.95f, "%.2f");
                ImGui::TextDisabled("Topology ratio: 2:1; mesh: immutable after initialization");
            }

            if (ImGui::CollapsingHeader("Flow Map"))
            {
                changed |= ImGui::Checkbox("Enable##FlowMap", &settings.flowMap.enabled);
                changed |= ImGui::DragFloat("Strength (m)", &settings.flowMap.strength, 0.1f, 0.0f, 256.0f, "%.2f");
                changed |= ImGui::DragFloat("Speed", &settings.flowMap.speed, 0.01f, 0.0f, 16.0f, "%.3f");
                changed |= ImGui::DragFloat("Phase Length", &settings.flowMap.phaseLength, 0.01f, 0.05f, 32.0f, "%.3f");
                changed |= ImGui::DragFloat("Noise Amount", &settings.flowMap.noiseAmount, 0.01f, 0.0f, 2.0f, "%.3f");
                changed |= EditVec2("World Origin", settings.flowMap.worldOrigin, 0.5f, -100000.0f, 100000.0f, "%.1f");
                changed |= EditVec2("World Size", settings.flowMap.worldSize, 1.0f, 1.0f, 100000.0f, "%.1f");
                changed |= EditVec2("Fallback Direction", settings.flowMap.fallbackDirection, 0.01f, -1.0f, 1.0f, "%.3f");
            }

            if (ImGui::CollapsingHeader("SSS (Subsurface Scattering)"))
            {
                changed |= ImGui::Checkbox("Enable##SSS", &settings.sssEnabled);
                changed |= ImGui::DragFloat("Max Thickness (m)", &settings.maxThicknessDistance, 0.1f, 1.0f, 50.0f, "%.1f");
                changed |= ImGui::DragFloat("Deep Water Fallback", &settings.deepWaterThicknessFallback, 0.01f, 0.0f, 1.0f, "%.2f");
                ImGui::Separator();
                ImGui::TextDisabled("Scattering params are shared with Medium.");
                ImGui::Text("Anisotropy: %.3f", settings.medium.anisotropy);
                ImGui::Text("Absorption: R=%.3f G=%.3f B=%.3f",
                    settings.medium.absorptionCoeff.x,
                    settings.medium.absorptionCoeff.y,
                    settings.medium.absorptionCoeff.z);
                ImGui::Text("Scattering: R=%.3f G=%.3f B=%.3f",
                    settings.medium.scatteringCoeff.x,
                    settings.medium.scatteringCoeff.y,
                    settings.medium.scatteringCoeff.z);
            }

            if (ImGui::CollapsingHeader("Caustics"))
            {
                changed |= ImGui::Checkbox("Enable##Caustics", &settings.causticsEnabled);
                changed |= ImGui::DragFloat("Intensity##Caustics", &settings.causticsIntensity, 0.01f, 0.0f, 3.0f, "%.3f");
                changed |= ImGui::DragFloat("Scale##Caustics", &settings.causticsScale, 0.01f, 0.01f, 2.0f, "%.3f");
            }

            if (ImGui::CollapsingHeader("Refraction"))
            {
                changed |= ImGui::Checkbox("Enable##Refraction", &settings.refractionEnabled);
                changed |= ImGui::DragFloat("UV Distortion", &settings.refractionDistortionStrength,
                    0.0005f, 0.0f, 0.1f, "%.4f");
                ImGui::TextDisabled("Continuous view-normal offset scaled by water thickness.");
            }

            if (ImGui::CollapsingHeader("Screen-Space Reflection"))
            {
                changed |= ImGui::Checkbox("Enable##SSR", &settings.ssrEnabled);
                changed |= ImGui::DragFloat("Max Distance (m)", &settings.ssrMaxDistance, 1.0f, 10.0f, 2000.0f, "%.0f");
                changed |= ImGui::DragFloat("Max Roughness", &settings.ssrMaxRoughness, 0.01f, 0.0f, 1.0f, "%.3f");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("LOD Debug"))
        {
            if (stats.systemInitialized)
            {
                ImGui::Text("Patches: %u", stats.patchCount);
                ImGui::Text("Mesh Dim: %d", stats.meshDim);
                ImGui::Text("Base Patch Size: %.1f m", stats.basePatchSize);
                ImGui::Text("Index Count: %u", stats.indexCount);

                if (ImGui::TreeNode("Ring Patch Sizes"))
                {
                    float patchSize = stats.basePatchSize;
                    for (int i = 0; i < stats.lodLevels; ++i)
                    {
                        ImGui::Text("LOD %d: %.1f m", i, patchSize);
                        patchSize *= stats.geometryLodRatio;
                    }
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::TextDisabled("Water system not initialized.");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Textures"))
        {
            if (!stats.systemInitialized)
            {
                ImGui::TextDisabled("Water system not initialized.");
            }
            else
            {
                static int waterLayer = 0;
                waterLayer = std::clamp(waterLayer, 0, stats.maxSpectrumCascade);
                ImGui::SliderInt("Spectrum Cascade", &waterLayer, 0, stats.maxSpectrumCascade);
                DisplayWaterTexture(editorAPI, "Water Displacement", "displacement", static_cast<std::uint32_t>(waterLayer));

                ImGui::Separator();
                DisplayWaterTexture(editorAPI, "Surface dPdx", "derivative", static_cast<std::uint32_t>(waterLayer * 2));

                ImGui::Separator();
                DisplayWaterTexture(editorAPI, "Flow Map", "flow_map");

                if (stats.fftAvailable)
                {
                    ImGui::SeparatorText("FFT Internal");

                    static int fftLod = 0;
                    fftLod = std::clamp(fftLod, 0, stats.maxSpectrumCascade);
                    ImGui::SliderInt("FFT H0 Cascade", &fftLod, 0, stats.maxSpectrumCascade);
                    DisplayWaterTexture(editorAPI, "FFT H0 Spectrum", "fft_h0", static_cast<std::uint32_t>(fftLod));

                    const char* fieldNames[] = { "Height", "Displacement X", "Displacement Z" };
                    static int fftField = 0;
                    ImGui::Combo("FFT Spatial Field", &fftField, fieldNames, IM_ARRAYSIZE(fieldNames));
                    const std::uint32_t fieldLayer = static_cast<std::uint32_t>(fftLod * stats.fftFieldCount + fftField);

                    if (ImGui::BeginTable("FFTInternalTexTable", 2, ImGuiTableFlags_Borders))
                    {
                        ImGui::TableNextColumn();
                        DisplayWaterTexture(editorAPI, "FFT Ping-Pong 0", "fft_ping0", fieldLayer);

                        ImGui::TableNextColumn();
                        DisplayWaterTexture(editorAPI, "FFT Ping-Pong 1", "fft_ping1", fieldLayer);

                        ImGui::EndTable();
                    }
                }

                ImGui::Separator();
                if (ImGui::BeginTable("WaterTexTable", 2, ImGuiTableFlags_Borders))
                {
                    ImGui::TableNextColumn();
                    DisplayWaterTexture(editorAPI, "Reflection", "reflection");

                    ImGui::TableNextColumn();
                    DisplayWaterTexture(editorAPI, "Refraction", "refraction");

                    ImGui::TableNextColumn();
                    DisplayWaterTexture(editorAPI, "Caustics", "caustics");

                    ImGui::TableNextColumn();
                    DisplayWaterTexture(editorAPI, "Thickness", "thickness");

                    ImGui::EndTable();
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (changed)
    {
        editorAPI.ApplyWaterSettings(settings);
    }

    ImGui::End();
}

} // namespace VansGraphics
