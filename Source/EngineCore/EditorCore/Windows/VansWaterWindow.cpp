#include "VansWaterWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/WaterCore/VansWaterConfig.h"
#include "../../RenderCore/WaterCore/VansWaterMaterial.h"
#include "../../RenderCore/WaterCore/VansWaterSystem.h"
#include "../../RenderCore/WaterCore/VansWaterLOD.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_vulkan.h"

namespace VansGraphics
{

// 4 个内置水质预设（设计文档 §9.3，W-19）
const WaterPreset kWaterPresets[] = {
    {
        "Tropical Ocean", "热带海洋",
        {0.0f, 0.08f, 0.28f, 1.0f}, {0.05f, 0.35f, 0.55f, 1.0f},
        {0.35f, 0.12f, 0.03f}, {0.02f, 0.05f, 0.08f},   // 消光 R:0.37 G:0.17 B:0.11, 蓝光穿透最深
        1.33f, 5.5f, 1.2f
    },
    {
        "Temperate Lake", "温带湖泊",
        {0.02f, 0.05f, 0.10f, 1.0f}, {0.08f, 0.25f, 0.35f, 1.0f},
        {0.20f, 0.10f, 0.04f}, {0.03f, 0.05f, 0.07f},   // 消光 R:0.23 G:0.15 B:0.11
        1.34f, 4.5f, 0.7f
    },
    {
        "Arctic Sea", "极地海域",
        {0.0f, 0.03f, 0.08f, 1.0f}, {0.12f, 0.45f, 0.60f, 1.0f},
        {0.30f, 0.08f, 0.02f}, {0.01f, 0.02f, 0.04f},   // 极清澈，消光 R:0.31 G:0.10 B:0.06
        1.33f, 6.0f, 1.5f
    },
    {
        "Muddy River", "浑浊河流",
        {0.08f, 0.06f, 0.02f, 1.0f}, {0.15f, 0.12f, 0.06f, 1.0f},
        {0.10f, 0.08f, 0.22f}, {0.08f, 0.06f, 0.02f},   // 浑浊水体蓝光被吸收，消光 R:0.18 G:0.14 B:0.24
        1.34f, 3.0f, 0.3f
    }
};
const int kWaterPresetCount = 4;

void VansWaterWindow::ApplyPreset(const WaterPreset& preset)
{
    if (!m_Scene) return;
    VansWaterConfig& cfg = const_cast<VansWaterConfig&>(m_Scene->GetWaterConfig());
    VansWaterMaterial* mat = m_Scene->m_WaterMaterial;

    cfg.m_Medium.m_DeepColor    = preset.deepColor;
    cfg.m_Medium.m_ShallowColor = preset.shallowColor;
    cfg.m_Medium.m_AbsorptionCoeff = preset.absorption;
    cfg.m_Medium.m_ScatteringCoeff = preset.scattering;
    cfg.m_Medium.m_IOR          = preset.ior;
    cfg.m_Medium.m_FresnelPower = preset.fresnelPower;
    cfg.m_SpecularIntensity     = preset.specularIntensity;

    if (mat)
    {
        mat->m_DeepWaterColor    = preset.deepColor;
        mat->m_ShallowWaterColor = preset.shallowColor;
        mat->m_AbsorptionCoeffs  = preset.absorption;
        mat->m_ScatteringCoeffs  = preset.scattering;
        mat->m_WaterIOR          = preset.ior;
        mat->m_FresnelPower      = preset.fresnelPower;
        mat->m_SpecularIntensity = preset.specularIntensity;
    }
}

void VansGraphics::VansWaterWindow::ShowWindow(VansVKDevice& device)
{
    if (!m_Scene || !m_Scene->HasWaterNodes())
    {
        ImGui::Begin("Water");
        ImGui::TextDisabled("Scene has no water config. Add \"water\" block to Scene JSON and reload.");
        ImGui::End();
        return;
    }

    ImGui::Begin("Water");

    VansWaterConfig& cfg = const_cast<VansWaterConfig&>(m_Scene->GetWaterConfig());
    VansWaterMaterial* mat = m_Scene->m_WaterMaterial;
    VansWaterSystem* waterSys = m_Scene->GetWaterSystem();

    // ── Tab bar ───────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("WaterTabs"))
    {
        // ============================================================
        // Tab 1: 参数编辑
        // ============================================================
        if (ImGui::BeginTabItem("Parameters"))
        {
            // ── W-19: 预设管理 ─────────────────────────────────────────
            if (ImGui::CollapsingHeader("Presets (W-19)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (int i = 0; i < kWaterPresetCount; ++i)
                {
                    const WaterPreset& p = kWaterPresets[i];
                    if (ImGui::Button(p.name))
                        ApplyPreset(p);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", p.description);
                }
                ImGui::Separator();
            }

            if (mat)
            {
ImGui::DragFloat("Specular Intensity", &mat->m_SpecularIntensity, 0.01f, 0.0f, 10.0f, "%.3f");
				ImGui::DragFloat("Water Roughness", &mat->m_WaterRoughness, 0.001f, 0.001f, 1.0f, "%.4f");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("水面微面元粗糙度: 0=镜面反射, 1=漫反射");
            }

            // ── 基础 ─────────────────────────────────────────────────
            if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const char* typeNames[] = { "Ocean", "Lake", "River", "Pool" };
                int typeIdx = static_cast<int>(cfg.m_Type);
                if (ImGui::Combo("Type", &typeIdx, typeNames, 4))
                {
                    cfg.m_Type = static_cast<VansWaterType>(typeIdx);
                }

                float level = cfg.m_WaterLevel;
                if (ImGui::DragFloat("Water Level (Y)", &level, 0.1f, -10000.0f, 10000.0f, "%.2f"))
                {
                    cfg.m_WaterLevel = level;
                    if (waterSys) waterSys->SetWaterLevel(level);
                    if (m_Scene->m_WaterRenderNode)
                    {
                        glm::vec3 pos = m_Scene->m_WaterRenderNode->GetTransformPosition();
                        pos.y = level;
                        m_Scene->m_WaterRenderNode->SetTransformData(
                            pos, m_Scene->m_WaterRenderNode->GetTransformRotation(),
                            m_Scene->m_WaterRenderNode->GetTransformScale());
                    }
                }
            }

            // ── 介质参数 ─────────────────────────────────────────────
            if (ImGui::CollapsingHeader("Medium"))
            {
                float abs3[3] = { cfg.m_Medium.m_AbsorptionCoeff.x, cfg.m_Medium.m_AbsorptionCoeff.y, cfg.m_Medium.m_AbsorptionCoeff.z };
                if (ImGui::DragFloat3("Absorption (RGB)", abs3, 0.001f, 0.0f, 10.0f, "%.4f"))
                { cfg.m_Medium.m_AbsorptionCoeff = {abs3[0], abs3[1], abs3[2]}; if (mat) mat->m_AbsorptionCoeffs = cfg.m_Medium.m_AbsorptionCoeff; }
                float sca3[3] = { cfg.m_Medium.m_ScatteringCoeff.x, cfg.m_Medium.m_ScatteringCoeff.y, cfg.m_Medium.m_ScatteringCoeff.z };
                if (ImGui::DragFloat3("Scattering (RGB)", sca3, 0.001f, 0.0f, 10.0f, "%.4f"))
                { cfg.m_Medium.m_ScatteringCoeff = {sca3[0], sca3[1], sca3[2]}; if (mat) mat->m_ScatteringCoeffs = cfg.m_Medium.m_ScatteringCoeff; }
ImGui::DragFloat("IOR", &cfg.m_Medium.m_IOR, 0.001f, 1.0f, 3.0f, "%.4f"); if (mat) mat->m_WaterIOR = cfg.m_Medium.m_IOR;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("F0 = ((IOR-1)/(IOR+1))^2, realtime to Composite UBO");
ImGui::DragFloat("Fresnel Power", &cfg.m_Medium.m_FresnelPower, 0.1f, 0.1f, 20.0f, "%.2f"); if (mat) mat->m_FresnelPower = cfg.m_Medium.m_FresnelPower;
ImGui::DragFloat("Anisotropy", &cfg.m_Medium.m_Anisotropy, 0.01f, 0.0f, 1.0f, "%.3f"); if (mat) mat->m_Anisotropy = cfg.m_Medium.m_Anisotropy;
                float dc[4] = { cfg.m_Medium.m_DeepColor.x, cfg.m_Medium.m_DeepColor.y, cfg.m_Medium.m_DeepColor.z, cfg.m_Medium.m_DeepColor.w };
                if (ImGui::ColorEdit4("Deep Color", dc)) { cfg.m_Medium.m_DeepColor = {dc[0], dc[1], dc[2], dc[3]}; if (mat) mat->m_DeepWaterColor = cfg.m_Medium.m_DeepColor; }
                float sc[4] = { cfg.m_Medium.m_ShallowColor.x, cfg.m_Medium.m_ShallowColor.y, cfg.m_Medium.m_ShallowColor.z, cfg.m_Medium.m_ShallowColor.w };
                if (ImGui::ColorEdit4("Shallow Color", sc)) { cfg.m_Medium.m_ShallowColor = {sc[0], sc[1], sc[2], sc[3]}; if (mat) mat->m_ShallowWaterColor = cfg.m_Medium.m_ShallowColor; }
            }

            // ── 波形 ─────────────────────────────────────────────────
            if (ImGui::CollapsingHeader("Waves"))
            {
                const char* modeNames[] = { "Gerstner", "FFT", "Hybrid" };
                int modeIdx = static_cast<int>(cfg.m_Waves.m_Mode);
                if (ImGui::Combo("Wave Mode", &modeIdx, modeNames, 3)) { cfg.m_Waves.m_Mode = static_cast<VansWaveMode>(modeIdx); }
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gerstner: ready. FFT/Hybrid: not implemented yet");
ImGui::DragFloat("Base Scale", &cfg.m_Waves.m_BaseScale, 1.0f, 1.0f, 4096.0f, "%.1f"); if (mat) mat->m_OceanBaseScale = cfg.m_Waves.m_BaseScale;
                int maxLod = cfg.m_Waves.m_MaxLOD;
                if (ImGui::SliderInt("Max LOD", &maxLod, 1, 10)) { cfg.m_Waves.m_MaxLOD = maxLod; if (mat) mat->m_MaxLODCount = maxLod; }
                float wd[2] = { cfg.m_Waves.m_WindDirection.x, cfg.m_Waves.m_WindDirection.y };
                if (ImGui::DragFloat2("Wind Dir (XZ)", wd, 0.01f, -1.0f, 1.0f, "%.3f")) { cfg.m_Waves.m_WindDirection = {wd[0], wd[1]}; if (mat) mat->m_WindDirection = cfg.m_Waves.m_WindDirection; if (waterSys) waterSys->UpdateWaveSSBO(); }
	                if (ImGui::DragFloat("Wind Speed", &cfg.m_Waves.m_WindSpeed, 0.1f, 0.0f, 100.0f, "%.2f")) { if (mat) mat->m_WindSpeed = cfg.m_Waves.m_WindSpeed; if (waterSys) waterSys->UpdateWaveSSBO(); }
	                if (ImGui::DragFloat("Swell Amplitude", &cfg.m_Waves.m_SwellAmplitude, 0.01f, 0.0f, 20.0f, "%.3f")) { if (mat) mat->m_SwellAmplitude = cfg.m_Waves.m_SwellAmplitude; if (waterSys) waterSys->UpdateWaveSSBO(); }
	                if (ImGui::DragFloat("Chop Scale", &cfg.m_Waves.m_ChopScale, 0.01f, 0.0f, 2.0f, "%.3f")) { if (mat) mat->m_ChopScale = cfg.m_Waves.m_ChopScale; }
                int gc = cfg.m_Waves.m_GerstnerWaveCount;
                if (ImGui::SliderInt("Gerstner Waves", &gc, 1, 64)) { cfg.m_Waves.m_GerstnerWaveCount = gc; if (mat) mat->m_GerstnerWaveCount = gc; if (waterSys) waterSys->UpdateWaveSSBO(); }
            }

            // ── N-01: Detail Normal ──────────────────────────────────
            if (ImGui::CollapsingHeader("Detail Normal (N-01)"))
            {
                if (mat)
                {
                    bool dnEnabled = mat->m_DetailNormalEnabled;
                    if (ImGui::Checkbox("Enable##DetailNormal", &dnEnabled))
                    {
                        mat->m_DetailNormalEnabled = dnEnabled;
                        cfg.m_Waves.m_DetailNormal.m_Enabled = dnEnabled;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable compute-generated detail normal (multi-octave capillary waves)");

                    if (ImGui::DragFloat("Intensity##DetailNormal", &mat->m_DetailNormalIntensity, 0.01f, 0.0f, 3.0f, "%.3f"))
                        cfg.m_Waves.m_DetailNormal.m_Intensity = mat->m_DetailNormalIntensity;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Detail normal XZ perturbation strength. 0 = flat normal (0,1,0), 1 = full detail");

                    if (ImGui::DragFloat("Scale##DetailNormal", &mat->m_DetailNormalScale, 0.01f, 0.1f, 5.0f, "%.2f"))
                        cfg.m_Waves.m_DetailNormal.m_Scale = mat->m_DetailNormalScale;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gradient scaling factor [0.1, 5]");

                    int octaves = mat->m_DetailNormalOctaves;
                    if (ImGui::SliderInt("Octaves", &octaves, 1, 4))
                    { mat->m_DetailNormalOctaves = octaves; cfg.m_Waves.m_DetailNormal.m_OctaveCount = octaves; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Octave count [1, 4]: more = richer detail but higher compute cost");

                    if (ImGui::DragFloat("World Coverage (m)", &mat->m_DetailNormalBaseScale, 1.0f, 32.0f, 1024.0f, "%.0f"))
                        cfg.m_Waves.m_DetailNormal.m_DetailBaseScale = mat->m_DetailNormalBaseScale;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("World-space tiling distance. Larger = less visible repetition. 256m default");

                    if (ImGui::DragFloat("Time Offset", &mat->m_DetailNormalTimeOffset, 0.01f, 0.0f, 10.0f, "%.2f"))
                        cfg.m_Waves.m_DetailNormal.m_TimeOffset = mat->m_DetailNormalTimeOffset;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time phase offset to desync from macro waves");
                }
            }

            // ── Inspector optimization: Caustics section ──────────────
            if (ImGui::CollapsingHeader("Caustics"))
            {
                if (mat)
                {
                    bool cauEnable = mat->m_EnableCaustics;
                    if (ImGui::Checkbox("Enable##Caustics", &cauEnable))
                    { mat->m_EnableCaustics = cauEnable; cfg.m_Caustics.m_Enabled = cauEnable; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable/disable caustics compute dispatch");

                    if (ImGui::DragFloat("Intensity##Caustics", &mat->m_CausticsIntensity, 0.01f, 0.0f, 3.0f, "%.3f"))
                        cfg.m_Caustics.m_Intensity = mat->m_CausticsIntensity;

                    if (ImGui::DragFloat("Scale##Caustics", &mat->m_CausticsScale, 0.01f, 0.01f, 2.0f, "%.3f"))
                        cfg.m_Caustics.m_Scale = mat->m_CausticsScale;
                }
            }

            // ── Inspector optimization: Refraction section ────────────
            if (ImGui::CollapsingHeader("Refraction"))
            {
                if (mat)
                {
                    bool refrEnable = mat->m_EnableRefraction;
                    if (ImGui::Checkbox("Enable##Refraction", &refrEnable))
                    { mat->m_EnableRefraction = refrEnable; cfg.m_Refraction.m_Enabled = refrEnable; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable/disable refraction compute dispatch");

                    if (ImGui::DragFloat("Max Distance (m)", &mat->m_RefractionMaxDist, 1.0f, 1.0f, 500.0f, "%.1f"))
                        cfg.m_Refraction.m_MaxDistance = mat->m_RefractionMaxDist;

                    if (ImGui::DragFloat("Scale##Refraction", &mat->m_RefractionScale, 0.01f, 0.0f, 2.0f, "%.3f"))
                        cfg.m_Refraction.m_Scale = mat->m_RefractionScale;
                }
            }

            // ── Inspector optimization: SSR section ───────────────────
            if (ImGui::CollapsingHeader("Screen-Space Reflection"))
            {
                if (mat)
                {
                    bool ssrEnable = mat->m_EnableSSR;
                    if (ImGui::Checkbox("Enable##SSR", &ssrEnable))
                    { mat->m_EnableSSR = ssrEnable; cfg.m_SSR.m_Enabled = ssrEnable; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable/disable SSR compute dispatch");

                    if (ImGui::DragFloat("Max Distance (m)", &mat->m_SSRMaxDistance, 1.0f, 10.0f, 2000.0f, "%.0f"))
                        cfg.m_SSR.m_MaxDistance = mat->m_SSRMaxDistance;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("SSR ray march max trace distance. 500m default, increase for distant reflections");

                    if (ImGui::DragFloat("Max Roughness", &mat->m_SSRMaxRoughness, 0.01f, 0.0f, 1.0f, "%.3f"))
                        cfg.m_SSR.m_MaxRoughness = mat->m_SSRMaxRoughness;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Above this roughness, fallback to IBL");
                }
            }

            // ── Inspector optimization: Foam section ──────────────────
            if (ImGui::CollapsingHeader("Foam"))
            {
                if (mat)
                {
                    bool foamEnable = mat->m_EnableFoam;
                    if (ImGui::Checkbox("Enable##Foam", &foamEnable))
                    { mat->m_EnableFoam = foamEnable; cfg.m_Foam.m_Enabled = foamEnable; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foam rendering (shader code commented out, WIP)");

                    if (ImGui::DragFloat("Intensity##Foam", &mat->m_FoamIntensity, 0.01f, 0.0f, 3.0f, "%.3f"))
                        cfg.m_Foam.m_Intensity = mat->m_FoamIntensity;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foam blend intensity (WIP: shader foam code commented out)");
                }
            }

            ImGui::EndTabItem();
        }

        // ============================================================
        // Tab 2: LOD Debug (W-18)
        // ============================================================
        if (ImGui::BeginTabItem("LOD Debug"))
        {
            // LOD stats
            if (waterSys && waterSys->GetLOD())
            {
                auto* lod = waterSys->GetLOD();
                ImGui::Text("Patches: %zu", lod->GetPatchCount());
                ImGui::Text("Mesh Dim: %d", lod->GetMeshDim());
                ImGui::Text("Base Patch Size: %.1f m", lod->GetBasePatchSize());
                ImGui::Text("Index Count: %u", lod->GetIndexCount());

                // LOD ranges table
                if (ImGui::TreeNode("LOD Distance Ranges"))
                {
                    for (int i = 0; i < 10; ++i)
                    {
                        float range = lod->GetLodRange(i);
                        ImGui::Text("LOD %d: %.1f m", i, range);
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::EndTabItem();
        }

        // ============================================================
        // Tab 3: Texture Preview (W-17)
        // ============================================================
        if (ImGui::BeginTabItem("Textures"))
        {
            if (!waterSys)
            {
                ImGui::TextDisabled("Water system not initialized.");
                ImGui::EndTabItem();
                ImGui::EndTabBar();
                ImGui::End();
                return;
            }

            // 纹理预览辅助 lambda（与 VansGBufferWindow 使用相同模式）
            static VkDescriptorSet dsDisp = VK_NULL_HANDLE, dsRefl = VK_NULL_HANDLE, dsRefr = VK_NULL_HANDLE;
            static VkDescriptorSet dsCaus = VK_NULL_HANDLE, dsThck = VK_NULL_HANDLE, dsDetail = VK_NULL_HANDLE;
            static VkImageView ivDisp = VK_NULL_HANDLE, ivRefl = VK_NULL_HANDLE, ivRefr = VK_NULL_HANDLE;
            static VkImageView ivCaus = VK_NULL_HANDLE, ivThck = VK_NULL_HANDLE, ivDetail = VK_NULL_HANDLE;

            auto DisplayTex = [](const char* label, VansVKImage& image,
                                  VkDescriptorSet& cachedDS, VkImageView& cachedIV)
            {
                ImGui::Text("%s", label);
                VkImageView iv = image.GetImageView();
                if (iv == VK_NULL_HANDLE) { ImGui::TextDisabled("(not created)"); return; }
                if (cachedDS == VK_NULL_HANDLE || cachedIV != iv)
                {
                    if (cachedDS != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(cachedDS);
                    cachedDS = ImGui_ImplVulkan_AddTexture(image.GetSampler(), iv, VK_IMAGE_LAYOUT_GENERAL);
                    cachedIV = iv;
                }
                if (cachedDS != VK_NULL_HANDLE)
                {
                    float w = ImGui::GetContentRegionAvail().x * 0.95f;
                    float aspect = (float)image.GetImageDimension().width / (float)image.GetImageDimension().height;
                    ImGui::Image((ImTextureID)cachedDS, ImVec2(w, w / aspect));
                }
            };

            ImGui::TextWrapped("水面渲染管线中间纹理预览：");

            ImGui::Separator();
            DisplayTex("Displacement Texture2DArray (layer 0)", waterSys->GetDisplacementImage(), dsDisp, ivDisp);

            ImGui::Separator();
            DisplayTex("Detail Normal Texture2DArray (layer 0)", waterSys->GetDetailNormalImage(), dsDetail, ivDetail);

            ImGui::Separator();
            if (ImGui::BeginTable("WaterTexTable", 2, ImGuiTableFlags_Borders))
            {
                ImGui::TableNextColumn();
                DisplayTex("Reflection", waterSys->GetReflectionImage(), dsRefl, ivRefl);

                ImGui::TableNextColumn();
                DisplayTex("Refraction", waterSys->GetRefractionImage(), dsRefr, ivRefr);

                ImGui::TableNextColumn();
                DisplayTex("Caustics", waterSys->GetCausticsImage(), dsCaus, ivCaus);

                ImGui::TableNextColumn();
                DisplayTex("Thickness", waterSys->GetThicknessImage(), dsThck, ivThck);

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace VansGraphics
