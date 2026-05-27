#include "VansHierachyWindow.h"
#include "VansAnimGraphEditorWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../PhysicsCore/VansClothNode.h"
#include "../../PhysicsCore/VansPhysicsVehicle.h"
#include "../../AudioCore/VansAudioNode.h"
#include "../../Util/VansLog.h"

#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

// ── Helper: draw a node list, grouping multi-mesh children under a tree node ──
void VansGraphics::VansHierachuWindow::DrawNodeListWithGroups(const std::vector<VansRenderNode*>& nodes)
{
    // Track which groups we have already drawn in this pass
    std::set<std::string> drawnGroups;

    for (auto& node : nodes)
    {
        if (node->m_ParentGroupName.empty())
        {
            // Regular (non-grouped) node – use pointer as unique ID
            ImGui::PushID(node);
            if (ImGui::Selectable(node->m_NodeName.c_str(), m_Scene->m_SelectedNode == node))
            {
                m_Scene->m_SelectedNode = node;
            }
            ImGui::PopID();
        }
        else
        {
            // This node belongs to a multi-mesh group.
            // Only draw the tree once per group.
            const std::string& groupName = node->m_ParentGroupName;
            if (drawnGroups.count(groupName))
                continue;
            drawnGroups.insert(groupName);

            auto groupIt = m_Scene->m_MultiMeshGroups.find(groupName);
            if (groupIt == m_Scene->m_MultiMeshGroups.end())
                continue;

            const MultiMeshGroup& group = groupIt->second;

            // Tree node for the parent group – use group name pointer as stable ID
            ImGui::PushID(groupName.c_str());
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            bool treeOpen = ImGui::TreeNodeEx(groupName.c_str(), flags);

            if (treeOpen)
            {
                for (auto* child : group.childNodes)
                {
                    ImGui::PushID(child);
                    if (ImGui::Selectable(child->m_NodeName.c_str(), m_Scene->m_SelectedNode == child))
                    {
                        m_Scene->m_SelectedNode = child;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

void VansGraphics::VansHierachuWindow::DrawRenderNodeList()
{
    if (ImGui::CollapsingHeader("Sky Node"))
    {
        if (ImGui::Selectable(m_Scene->m_SkyBoxNode->m_NodeName.c_str(), m_Scene->m_SelectedNode == m_Scene->m_SkyBoxNode))
        {
            m_Scene->m_SelectedNode = m_Scene->m_SkyBoxNode;
        }
    }

    if (ImGui::CollapsingHeader("Opaque Nodes"))
    {
        DrawNodeListWithGroups(m_Scene->m_OpaqueRenderNodes);
    }

    if (ImGui::CollapsingHeader("Transparent Nodes"))
    {
        DrawNodeListWithGroups(m_Scene->m_TransParentRenderNodes);
    }

    if (ImGui::CollapsingHeader("Post Process Nodes"))
    {
        for (auto& node : m_Scene->m_PostProcessRenderNodes)
        {
            if (ImGui::Selectable(node->m_NodeName.c_str(), m_Scene->m_SelectedNode == node))
            {
                m_Scene->m_SelectedNode = node;
            }
        }
    }
}

void VansGraphics::VansHierachuWindow::DrawRenderNodeDetail()
{
    if (m_Scene->m_SelectedNode!=nullptr)
    {
        ImGui::Begin("Render Node Inspector");

        VansRenderNode* node = m_Scene->m_SelectedNode;
        ImGui::Text("Node: %s", node->m_NodeName.c_str());
        if (!node->m_ParentGroupName.empty())
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Parent Group: %s", node->m_ParentGroupName.c_str());
        }
        ImGui::Separator();

        // --- Transform ---
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawTransformDetail(*node);
        }

        // --- Material ---
        if (node->m_Material != nullptr)
        {
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawMaterialDetail(*node->m_Material);
            }
        }

        // --- Post Process Profile ---
        if (node->GetNodeType() & POSTPROCESS_NODE)
        {
            VansMaterialManager* ppManager = m_Scene->GetMaterialManager();
            if (ppManager)
            {
                if (ImGui::CollapsingHeader("Post Process", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    DrawPostProcessProfile(ppManager->m_PostProcessProfile);
                }
            }
        }

        ImGui::End();
    }
}

void VansGraphics::VansHierachuWindow::DrawTransformDetail(VansRenderNode& node)
{
    glm::vec3 pos = node.GetTransformPosition();
    glm::vec3 rot = node.GetTransformRotation();
    glm::vec3 scl = node.GetTransformScale();

    bool changed = false;

    if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
        changed = true;
    if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f))
        changed = true;
    if (ImGui::DragFloat3("Scale", &scl.x, 0.01f, 0.001f, 100.0f))
        changed = true;

    if (changed)
    {
        node.SetTransformData(pos, rot, scl);
    }
}

void VansGraphics::VansHierachuWindow::DrawMaterialDetail(VansMaterial& material, int index)
{
    // Show material type label
    const char* typeNames[] = { "PBR", "Coat", "Transparent", "PostProcess", "SkyBox", "Deferred", "SSAO", "SSR", "Shadow", "Skin", "Cloth", "Hair", "Subsurface", "Grass", "Emissive" };
    int typeIdx = (int)material.m_MaterialType;
    if (typeIdx >= 0 && typeIdx < (int)(sizeof(typeNames) / sizeof(typeNames[0])))
        ImGui::Text("Type: %s", typeNames[typeIdx]);

    // Show texture info
    auto showTex = [](const char* label, VansTexture* tex) {
        if (tex != nullptr)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "  %s: bound", label);
        else
            ImGui::TextDisabled("  %s: (none)", label);
    };

    if (material.m_MaterialType == VansMaterialType::VAN_PBR)
    {
        VansPBRMaterial& pbr = static_cast<VansPBRMaterial&>(material);
        if (ImGui::TreeNode("Textures"))
        {
            showTex("BaseColor", pbr.m_BaseColorTexture);
            showTex("Normal",    pbr.m_NormalTexture);
            showTex("Metallic",  pbr.m_MetalTexture);
            showTex("Roughness", pbr.m_RoughnessTexture);
            showTex("AO",        pbr.m_AoTexture);
            ImGui::TreePop();
        }
    }
    else if (material.m_MaterialType == VansMaterialType::VAN_SKIN)
    {
        VansSkinMaterial& skin = static_cast<VansSkinMaterial&>(material);
        if (ImGui::TreeNode("Textures"))
        {
            showTex("BaseColor", skin.m_BaseColorTexture);
            showTex("Normal",    skin.m_NormalTexture);
            ImGui::TreePop();
        }
    }
    else if (material.m_MaterialType == VansMaterialType::VAN_CLOTH)
    {
        VansClothMaterial& cloth = static_cast<VansClothMaterial&>(material);
        if (ImGui::TreeNode("Textures"))
        {
            showTex("BaseColor",  cloth.m_BaseColorTexture);
            showTex("Normal",     cloth.m_NormalTexture);
            showTex("Roughness",  cloth.m_RoughnessTexture);
            showTex("AO",         cloth.m_AoTexture);
            ImGui::TreePop();
        }
        ImGui::SliderFloat("Sheen Roughness", &cloth.m_SheenRoughness, 0.0f, 1.0f);
    }
    else if (material.m_MaterialType == VansMaterialType::VAN_HAIR)
    {
        VansHairMaterial& hair = static_cast<VansHairMaterial&>(material);
        if (ImGui::TreeNode("Textures"))
        {
            showTex("AlbedoAlpha", hair.m_AlbedoAlphaTexture);
            showTex("Normal",      hair.m_NormalTexture);
            showTex("Roughness",   hair.m_RoughnessTexture);
            showTex("AO",          hair.m_AoTexture);
            showTex("Shift",       hair.m_ShiftTexture);
            showTex("Alpha",       hair.m_AlphaTexture);
            showTex("Flow",        hair.m_FlowTexture);
            ImGui::TreePop();
        }
    }
    else if (material.m_MaterialType == VansMaterialType::VAN_EMISSIVE)
    {
        VansEmissiveMaterial& emissive = static_cast<VansEmissiveMaterial&>(material);
        if (ImGui::TreeNode("Textures"))
        {
            showTex("Emissive", emissive.m_EmissiveTexture);
            ImGui::TreePop();
        }
    }

    ImGui::Separator();

	switch (material.m_MaterialType)
	{
	case VansMaterialType::VAN_PBR:
		DrawPBRMaterialParameters(static_cast<VansPBRMaterial&>(material).m_BasePBRParam, index);
		break;
	case VansMaterialType::VAN_SKY_BOX:
        DrawAtmosphereParameters(static_cast<VansSkyBoxMaterial&>(material).m_AtmospherePBRParam);
		break;
	case VansMaterialType::VAN_EMISSIVE:
	{
		VansEmissiveMaterial& emissive = static_cast<VansEmissiveMaterial&>(material);
		ImGui::PushID(index);
		ImGui::ColorEdit3("Emissive Color", &emissive.m_BasePBRParam.m_albedo.x);
		ImGui::DragFloat("Emissive Intensity", &emissive.m_BasePBRParam.m_roughness, 0.1f, 0.0f, 1000.0f);
		ImGui::PopID();
		break;
	}
	default:
		break;
	}
}

void VansGraphics::VansHierachuWindow::DrawPBRMaterialParameters(VansBasePBRParam& param, int id)
{
    // Use unique ImGui IDs when multiple materials exist on one node
    ImGui::PushID(id);
    ImGui::ColorEdit3("Albedo", &param.m_albedo.x);
    ImGui::SliderFloat("Metallic", &param.m_metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &param.m_roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("AO", &param.m_ao, 0.0f, 1.0f);
    ImGui::PopID();
}

void VansGraphics::VansHierachuWindow::DrawAtmosphereParameters(VansAtmospherePBRParam& param)
{
    ImGui::InputFloat("planet radius", &param.m_PlanetRadius);
    ImGui::InputFloat("init sea level", &param.m_InitSeaLevel);
    ImGui::InputFloat("sun luminace", &param.m_SunLuminance);
    ImGui::InputFloat("atmosphere width", &param.m_AtmosphereWidth);
    ImGui::InputFloat("rayleigh scalar height", &param.m_RayleighScalarHeight);
    ImGui::InputFloat("mie scalar height", &param.m_MieScalarHeight);
    ImGui::InputFloat("mie anisotropy", &param.m_MieAnisotropy);
    ImGui::InputFloat("ozone center height", &param.m_OzoneLevelCenterHeight);
    ImGui::InputFloat("ozone width", &param.m_OzoneLevelWidth);
}

// 宏定义：ImGui 控件值改变时标记 Profile 为脏（每帧实时触发，确保拖拽中也能立刻生效）
#define PP_DIRTY_IF_CHANGED if (ImGui::IsItemEdited()) profile.m_IsDirty = true;

void VansGraphics::VansHierachuWindow::DrawPostProcessProfile(VansPostProcessProfile& profile)
{
    // ---------- General ----------
    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 全局开关暂未连接到 GPU Shader，显示为灰色提示
        ImGui::BeginDisabled(true);
        if (ImGui::Checkbox("Enable Post Process", &profile.m_EnablePostProcess)) profile.m_IsDirty = true;
        if (ImGui::Checkbox("Enable HDR",          &profile.m_EnableHDR))         profile.m_IsDirty = true;
        ImGui::EndDisabled();
        ImGui::TextDisabled("  (global switches not yet wired to GPU)");
    }

    // ---------- Exposure ----------
    if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Auto Exposure", &profile.m_EnableAutoExposure)) profile.m_IsDirty = true;
        ImGui::DragFloat("Exposure Compensation", &profile.m_ExposureCompensation, 0.1f, -10.0f, 10.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Min EV100", &profile.m_MinEV100, 0.1f, -20.0f, 20.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Max EV100", &profile.m_MaxEV100, 0.1f, -20.0f, 20.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Adapt Speed Up",   &profile.m_AdaptationSpeedUp,   0.05f, 0.01f, 20.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Adapt Speed Down", &profile.m_AdaptationSpeedDown, 0.05f, 0.01f, 20.0f);
        PP_DIRTY_IF_CHANGED
    }

    // ---------- Bloom ----------
    if (ImGui::CollapsingHeader("Bloom"))
    {
        if (ImGui::Checkbox("Enable Bloom", &profile.m_EnableBloom)) profile.m_IsDirty = true;
        ImGui::DragFloat("Threshold",  &profile.m_BloomThreshold, 0.05f, 0.0f, 20.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Knee",       &profile.m_BloomKnee,      0.01f, 0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Intensity",  &profile.m_BloomIntensity, 0.01f, 0.0f, 4.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Scatter",    &profile.m_BloomScatter,   0.01f, 0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Clamp",      &profile.m_BloomClamp,     1.0f,  0.0f, 256.0f);
        PP_DIRTY_IF_CHANGED
    }

    // ---------- Tone Mapping ----------
    if (ImGui::CollapsingHeader("Tone Mapping"))
    {
        const char* toneMappers[] = { "Linear", "ACES", "Reinhard" };
        if (ImGui::Combo("Tone Mapper", &profile.m_ToneMapperType, toneMappers, 3))
            profile.m_IsDirty = true;
        ImGui::DragFloat("White Point", &profile.m_WhitePoint, 0.1f, 0.1f, 50.0f);
        PP_DIRTY_IF_CHANGED
    }

    // ---------- Color Grading ----------
    if (ImGui::CollapsingHeader("Color Grading"))
    {
        if (ImGui::Checkbox("Enable Color Grading", &profile.m_EnableColorGrading)) profile.m_IsDirty = true;
        ImGui::DragFloat("Contrast",    &profile.m_Contrast,    0.01f, 0.0f, 4.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Saturation",  &profile.m_Saturation,  0.01f, 0.0f, 4.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Hue Shift",   &profile.m_HueShift,    0.5f, -180.0f, 180.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Temperature", &profile.m_Temperature, 0.01f, -1.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Tint",        &profile.m_Tint,        0.01f, -1.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
    }

    // ---------- Vignette ----------
    if (ImGui::CollapsingHeader("Vignette"))
    {
        if (ImGui::Checkbox("Enable Vignette", &profile.m_EnableVignette)) profile.m_IsDirty = true;
        ImGui::SliderFloat("Intensity##Vign",   &profile.m_VignetteIntensity,  0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::SliderFloat("Smoothness##Vign",  &profile.m_VignetteSmoothness, 0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
    }

    // ---------- Film Grain ----------
    if (ImGui::CollapsingHeader("Film Grain"))
    {
        if (ImGui::Checkbox("Enable Film Grain", &profile.m_EnableFilmGrain)) profile.m_IsDirty = true;
        ImGui::SliderFloat("Intensity##FG", &profile.m_FilmGrainIntensity, 0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
    }

    // ---------- Chromatic Aberration ----------
    if (ImGui::CollapsingHeader("Chromatic Aberration"))
    {
        ImGui::BeginDisabled(true);
        if (ImGui::Checkbox("Enable CA", &profile.m_EnableChromaticAberration)) profile.m_IsDirty = true;
        ImGui::SliderFloat("Intensity##CA", &profile.m_ChromaticAberrationIntensity, 0.0f, 0.1f);
        PP_DIRTY_IF_CHANGED
        ImGui::EndDisabled();
        ImGui::TextDisabled("  (not yet implemented)");
    }

    // ---------- Depth of Field ----------
    if (ImGui::CollapsingHeader("Depth of Field"))
    {
        ImGui::BeginDisabled(true);
        if (ImGui::Checkbox("Enable DOF", &profile.m_EnableDOF)) profile.m_IsDirty = true;
        ImGui::DragFloat("Focus Distance", &profile.m_FocusDistance, 0.1f, 0.1f, 500.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Focus Range",    &profile.m_FocusRange,    0.1f, 0.1f, 100.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Aperture",       &profile.m_Aperture,      0.1f, 0.1f, 64.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::DragFloat("Max CoC",        &profile.m_MaxCoC,        0.5f, 1.0f, 50.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::EndDisabled();
        ImGui::TextDisabled("  (not yet implemented)");
    }

    // ---------- Motion Blur ----------
    if (ImGui::CollapsingHeader("Motion Blur"))
    {
        ImGui::BeginDisabled(true);
        if (ImGui::Checkbox("Enable Motion Blur", &profile.m_EnableMotionBlur)) profile.m_IsDirty = true;
        ImGui::SliderFloat("Shutter Scale", &profile.m_ShutterScale, 0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::SliderInt("Samples##MB", &profile.m_MotionBlurSamples, 4, 32);
        PP_DIRTY_IF_CHANGED
        ImGui::EndDisabled();
        ImGui::TextDisabled("  (not yet implemented)");
    }

    // ---------- Lens Dirt ----------
    if (ImGui::CollapsingHeader("Lens Dirt"))
    {
        ImGui::BeginDisabled(true);
        if (ImGui::Checkbox("Enable Lens Dirt", &profile.m_EnableLensDirt)) profile.m_IsDirty = true;
        ImGui::SliderFloat("Intensity##LD", &profile.m_LensDirtIntensity, 0.0f, 2.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::EndDisabled();
        ImGui::TextDisabled("  (not yet implemented)");
    }

    // ---------- AA / Sharpen ----------
    if (ImGui::CollapsingHeader("AA / Sharpen"))
    {
        ImGui::BeginDisabled(true);
        if (ImGui::Checkbox("Enable FXAA",    &profile.m_EnableFXAA))    profile.m_IsDirty = true;
        if (ImGui::Checkbox("Enable Sharpen", &profile.m_EnableSharpen)) profile.m_IsDirty = true;
        ImGui::SliderFloat("Sharpen Intensity", &profile.m_SharpenIntensity, 0.0f, 1.0f);
        PP_DIRTY_IF_CHANGED
        ImGui::EndDisabled();
        ImGui::TextDisabled("  (not yet implemented)");
    }

    // ---------- Dithering ----------
    if (ImGui::CollapsingHeader("Dithering"))
    {
        if (ImGui::Checkbox("Enable Dithering", &profile.m_EnableDithering)) profile.m_IsDirty = true;
    }

    // 重置按钮
    ImGui::Separator();
    if (ImGui::Button("Reset to Defaults"))
    {
        profile.ResetToDefaults();
        profile.m_IsDirty = true;
    }
}

#undef PP_DIRTY_IF_CHANGED

void VansGraphics::VansHierachuWindow::DrawAnimationList()
{
    const auto& animNodes = m_Scene->m_AnimationNodes;
    if (animNodes.empty())
    {
        ImGui::TextDisabled("(no animation nodes in scene)");
        return;
    }

    for (auto* animNode : animNodes)
    {
        if (!animNode) continue;

        ImGui::PushID(animNode);

        bool isSelected = (m_SelectedAnimationNode == animNode);
        if (ImGui::Selectable(animNode->GetName().c_str(), isSelected))
            m_SelectedAnimationNode = animNode;

        ImGui::PopID();
    }
}

// ── Recursive bone tree helper for animation node detail view ──────────────
void VansGraphics::VansHierachuWindow::DrawAnimBoneTree(const Skeleton& skeleton,
    const VansAnimationClip* clip, int boneIndex, float time)
{
    if (boneIndex < 0 || boneIndex >= (int)skeleton.bones.size()) return;
    const BoneInfo& bone = skeleton.bones[boneIndex];

    bool isLeaf = bone.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (boneIndex == m_SelectedBone)
        flags |= ImGuiTreeNodeFlags_Selected;

    char label[256];
    snprintf(label, sizeof(label), "%s  [%d]", bone.name.c_str(), bone.id);

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)boneIndex, flags, "%s", label);

    if (ImGui::IsItemClicked())
        m_SelectedBone = boneIndex;

    // Show detail for the selected bone
    if (boneIndex == m_SelectedBone)
    {
        ImGui::Indent(20.0f);

        if (clip && boneIndex < (int)clip->boneKeyframes.size() && !clip->boneKeyframes[boneIndex].empty())
        {
            const auto& keyframes = clip->boneKeyframes[boneIndex];
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Keyframes: %d", (int)keyframes.size());

            // Interpolate TRS at current time
            glm::vec3 pos(0.0f);
            glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scl(1.0f);

            if (keyframes.size() == 1 || time <= keyframes.front().time)
            {
                pos = keyframes.front().position;
                rot = keyframes.front().rotation;
                scl = keyframes.front().scale;
            }
            else if (time >= keyframes.back().time)
            {
                pos = keyframes.back().position;
                rot = keyframes.back().rotation;
                scl = keyframes.back().scale;
            }
            else
            {
                for (size_t k = 0; k + 1 < keyframes.size(); k++)
                {
                    if (time >= keyframes[k].time && time <= keyframes[k + 1].time)
                    {
                        float seg = keyframes[k + 1].time - keyframes[k].time;
                        float alpha = (seg > 0.0001f) ? (time - keyframes[k].time) / seg : 0.0f;
                        pos = glm::mix(keyframes[k].position, keyframes[k + 1].position, alpha);
                        rot = glm::slerp(keyframes[k].rotation, keyframes[k + 1].rotation, alpha);
                        scl = glm::mix(keyframes[k].scale, keyframes[k + 1].scale, alpha);
                        break;
                    }
                }
            }

            ImGui::Text("Position: (%.3f, %.3f, %.3f)", pos.x, pos.y, pos.z);
            ImGui::Text("Rotation (quat): (%.3f, %.3f, %.3f, %.3f)", rot.w, rot.x, rot.y, rot.z);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(rot));
            ImGui::Text("Rotation (euler): (%.1f, %.1f, %.1f)", euler.x, euler.y, euler.z);
            ImGui::Text("Scale:    (%.3f, %.3f, %.3f)", scl.x, scl.y, scl.z);
            ImGui::TextDisabled("Time range: %.3f - %.3f s", keyframes.front().time, keyframes.back().time);
        }
        else
        {
            ImGui::TextDisabled("No keyframes for this bone");
        }

        // Offset matrix
        if (ImGui::TreeNode("Offset Matrix"))
        {
            const glm::mat4& m = bone.offsetMatrix;
            for (int row = 0; row < 4; row++)
                ImGui::Text("  [%.3f  %.3f  %.3f  %.3f]", m[0][row], m[1][row], m[2][row], m[3][row]);
            ImGui::TreePop();
        }

        ImGui::Unindent(20.0f);
    }

    if (nodeOpen && !isLeaf)
    {
        for (int childIdx : bone.children)
            DrawAnimBoneTree(skeleton, clip, childIdx, time);
        ImGui::TreePop();
    }
}

void VansGraphics::VansHierachuWindow::DrawAnimationNodeDetail()
{
    if (!m_SelectedAnimationNode) return;

    // 场景卸载后 m_SelectedAnimationNode 可能悬空，需验证指针仍在当前场景中
    {
        const auto& nodes = m_Scene->m_AnimationNodes;
        if (std::find(nodes.begin(), nodes.end(), m_SelectedAnimationNode) == nodes.end())
        {
            m_SelectedAnimationNode = nullptr;
            return;
        }
    }

    VansAnimationNode* anim = m_SelectedAnimationNode;
    VansAnimationController* ctrl = anim->GetController();

    ImGui::Begin("Animation Inspector");

    // ── Node 名称 ────────────────────────────────────────────────────
    ImGui::Text("Animation Node: %s", anim->GetName().c_str());
    if (ctrl)
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "Controller: %s", ctrl->GetName().c_str());
    else
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Controller: (none)");
    ImGui::Separator();

    if (!ctrl)
    {
        ImGui::TextDisabled("No controller bound to this animation node.");
        ImGui::End();
        return;
    }

    // ── 打开节点图编辑器按钮 ─────────────────────────────────────────
    if (m_AnimGraphEditorRef)
    {
        if (ImGui::Button("Open Graph Editor"))
        {
            m_AnimGraphEditorRef->Open(ctrl, anim);
        }
    }
    ImGui::Separator();

    // ── 当前状态 ─────────────────────────────────────────────────────
    std::string stateName = ctrl->GetCurrentStateName();
    ImGui::Text("State: %s", stateName.empty() ? "(none)" : stateName.c_str());

    // ── 播放状态标签 ─────────────────────────────────────────────────
    AnimationState playbackState = ctrl->GetPlaybackState();
    const char* stateLabel = "Unknown";
    ImVec4 stateColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    switch (playbackState)
    {
    case AnimationState::Playing:
        stateLabel = "Playing";
        stateColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
        break;
    case AnimationState::Paused:
        stateLabel = "Paused";
        stateColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
        break;
    case AnimationState::Stopped:
        stateLabel = "Stopped";
        stateColor = ImVec4(0.7f, 0.3f, 0.3f, 1.0f);
        break;
    case AnimationState::Blending:
        stateLabel = "Blending";
        stateColor = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);
        break;
    }
    ImGui::TextColored(stateColor, "Playback: %s", stateLabel);

    if (playbackState == AnimationState::Blending)
    {
        float blendAlpha = ctrl->GetBlendAlpha();
        ImGui::ProgressBar(blendAlpha, ImVec2(-1.0f, 0.0f), "Blend");
    }

    ImGui::Separator();

    // ── 进度条 ───────────────────────────────────────────────────────
    float progress = ctrl->GetNormalizedTime();
    float curTime  = ctrl->GetCurrentPlayTime();
    float duration = ctrl->GetCurrentDuration();
    char progressLabel[64];
    std::snprintf(progressLabel, sizeof(progressLabel), "%.2fs / %.2fs", curTime, duration);
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressLabel);

    // ── 速度 ─────────────────────────────────────────────────────────
    float speed = ctrl->GetSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 0.0f, 4.0f, "%.2fx"))
        ctrl->SetSpeed(speed);

    // ── Root Motion ──────────────────────────────────────────────────
    bool rootMotion = ctrl->IsRootMotionEnabled();
    if (ImGui::Checkbox("Root Motion", &rootMotion))
        ctrl->EnableRootMotion(rootMotion);

    ImGui::Spacing();

    // ── 状态选择器 ───────────────────────────────────────────────────
    std::vector<std::string> stateNames = ctrl->GetStateNames();
    if (!stateNames.empty())
    {
        if (ImGui::BeginCombo("State", stateName.empty() ? "(none)" : stateName.c_str()))
        {
            for (const auto& sn : stateNames)
            {
                bool isCurrent = (sn == stateName);
                if (ImGui::Selectable(sn.c_str(), isCurrent))
                    ctrl->Play(sn);
                if (isCurrent)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── 播放控制 ─────────────────────────────────────────────────────
    bool isPlaying = (playbackState == AnimationState::Playing || playbackState == AnimationState::Blending);
    bool isPaused  = (playbackState == AnimationState::Paused);

    if (isPlaying)
    {
        if (ImGui::Button("Pause"))
            ctrl->Pause();
    }
    else
    {
        if (ImGui::Button("Play"))
        {
            if (isPaused)
                ctrl->Resume();
            else
                ctrl->Play();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        ctrl->Stop();

    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        ctrl->Reset();

    ImGui::Spacing();
    ImGui::Separator();

    // ── 参数面板 ─────────────────────────────────────────────────────
    const auto& params = ctrl->GetParameters();
    if (!params.empty())
    {
        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (auto& [pName, param] : params)
            {
                ImGui::PushID(pName.c_str());
                switch (param.type)
                {
                case AnimatorParamType::Float:
                {
                    float val = ctrl->GetFloat(pName);
                    if (ImGui::DragFloat(pName.c_str(), &val, 0.01f))
                        ctrl->SetFloat(pName, val);
                    break;
                }
                case AnimatorParamType::Bool:
                {
                    bool val = ctrl->GetBool(pName);
                    if (ImGui::Checkbox(pName.c_str(), &val))
                        ctrl->SetBool(pName, val);
                    break;
                }
                case AnimatorParamType::Int:
                {
                    int val = ctrl->GetInt(pName);
                    if (ImGui::DragInt(pName.c_str(), &val))
                        ctrl->SetInt(pName, val);
                    break;
                }
                case AnimatorParamType::Trigger:
                {
                    if (ImGui::Button(pName.c_str()))
                        ctrl->SetTrigger(pName);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(trigger)");
                    break;
                }
                }
                ImGui::PopID();
            }
        }
        ImGui::Separator();
    }

    // ── 状态列表 ─────────────────────────────────────────────────────
    if (!stateNames.empty() && ImGui::CollapsingHeader("States"))
    {
        for (const auto& sn : stateNames)
        {
            const AnimatorState* st = ctrl->GetState(sn);
            if (!st) continue;

            bool isCurrent = (sn == stateName);
            ImVec4 color = isCurrent ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

            ImGui::TextColored(color, "%s%s", st->name.c_str(),
                               (st->name == ctrl->GetDefaultStateName()) ? "  [Default]" : "");
            ImGui::SameLine(0.0f, 20.0f);
            ImGui::TextDisabled("clip=%s  speed=%.2f  loop=%s",
                                st->clipName.c_str(), st->speed,
                                st->loop ? "yes" : "no");
        }
    }

    // ── 过渡列表 ─────────────────────────────────────────────────────
    const auto& transitions = ctrl->GetTransitions();
    if (!transitions.empty() && ImGui::CollapsingHeader("Transitions"))
    {
        for (size_t ti = 0; ti < transitions.size(); ti++)
        {
            const auto& t = transitions[ti];
            ImGui::Text("[%d]  %s -> %s  (blend=%.2fs%s)",
                        (int)ti, t.fromState.c_str(), t.toState.c_str(),
                        t.blendDuration,
                        t.hasExitTime ? "  exitTime" : "");
            if (!t.conditions.empty())
            {
                ImGui::Indent(20.0f);
                for (const auto& cond : t.conditions)
                    ImGui::TextDisabled("  %s", cond.paramName.c_str());
                ImGui::Unindent(20.0f);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── 骨骼层级 ─────────────────────────────────────────────────────
    const Skeleton& skeleton = anim->GetSkeleton();
    if (!skeleton.bones.empty())
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.7f, 1.0f), "Bone Hierarchy  (%d bones)", (int)skeleton.bones.size());
        ImGui::Spacing();

        // 从 controller 获取当前 clip 以显示 keyframe 信息
        const VansAnimationClip* currentClip = nullptr;
        float currentTime = ctrl->GetCurrentPlayTime();
        const AnimatorState* curState = ctrl->GetState(stateName);
        if (curState && curState->clip)
            currentClip = curState->clip;

        ImGui::BeginChild("##animBoneTree", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (int b = 0; b < (int)skeleton.bones.size(); b++)
        {
            if (skeleton.bones[b].parentIndex < 0)
                DrawAnimBoneTree(skeleton, currentClip, b, currentTime);
        }
        ImGui::EndChild();
    }
    else
    {
        ImGui::TextDisabled("(no skeleton data)");
    }

    ImGui::End();
}

// ════════════════════════════════════════════════════════════════════════════
// 灯光组件面板 — Object Inspector 中的各灯光类型详情
// ════════════════════════════════════════════════════════════════════════════
void VansGraphics::VansHierachuWindow::DrawDirectionalLightComponent(VansScriptDirectionalLightComponent* comp)
{
    if (!comp || !comp->m_LightManager || comp->m_LightIndex < 0) return;
    auto& lights = comp->m_LightManager->GetDirectionLights();
    if (comp->m_LightIndex >= (int)lights.size()) return;

    VansDirectionalLight& light = lights[comp->m_LightIndex];
    ImGui::PushID("DirLight");

    ImGui::ColorEdit3("Color", &light.m_Color.x);
    ImGui::DragFloat("Intensity", &light.m_Intensity, 0.1f, 0.0f, 500.0f);

    // 方向由 Transform 驱动，只读显示
    ImGui::TextDisabled("Direction (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Direction.x, light.m_Direction.y, light.m_Direction.z);

    ImGui::PopID();
}

void VansGraphics::VansHierachuWindow::DrawPointLightComponent(VansScriptPointLightComponent* comp)
{
    if (!comp || !comp->m_LightManager || comp->m_LightIndex < 0) return;
    auto& lights = comp->m_LightManager->GetPointLights();
    if (comp->m_LightIndex >= (int)lights.size()) return;

    VansPointLight& light = lights[comp->m_LightIndex];
    ImGui::PushID("PointLight");

    ImGui::ColorEdit3("Color", &light.m_Color.x);
    ImGui::DragFloat("Intensity", &light.m_Intensity, 0.1f, 0.0f, 500.0f);
    ImGui::DragFloat("Radius",    &light.m_Radius,    0.1f, 0.01f, 500.0f);

    // 位置由 Transform 驱动，只读显示
    ImGui::TextDisabled("Position (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Position.x, light.m_Position.y, light.m_Position.z);

    ImGui::PopID();
}

void VansGraphics::VansHierachuWindow::DrawSpotLightComponent(VansScriptSpotLightComponent* comp)
{
    if (!comp || !comp->m_LightManager || comp->m_LightIndex < 0) return;
    auto& lights = comp->m_LightManager->GetSpotLight();
    if (comp->m_LightIndex >= (int)lights.size()) return;

    VansSpotLight& light = lights[comp->m_LightIndex];
    ImGui::PushID("SpotLight");

    ImGui::ColorEdit3("Color", &light.m_Color.x);
    ImGui::DragFloat("Intensity",  &light.m_Intensity,  0.1f,  0.0f,  500.0f);
    ImGui::DragFloat("Radius",     &light.m_Radius,     0.1f,  0.01f, 500.0f);

    // cutoff 存储为弧度，但显示和编辑为角度
    float innerDeg = glm::degrees(light.m_InnerCutOff);
    float outerDeg = glm::degrees(light.m_OuterCutOff);
    if (ImGui::DragFloat("Inner Cutoff (°)", &innerDeg, 0.5f, 0.0f, 89.0f))
        light.m_InnerCutOff = glm::radians(innerDeg);
    if (ImGui::DragFloat("Outer Cutoff (°)", &outerDeg, 0.5f, 0.0f, 89.0f))
        light.m_OuterCutOff = glm::radians(outerDeg);

    // 位置和方向由 Transform 驱动，只读显示
    ImGui::TextDisabled("Position (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Position.x, light.m_Position.y, light.m_Position.z);
    ImGui::TextDisabled("Direction (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Direction.x, light.m_Direction.y, light.m_Direction.z);

    ImGui::PopID();
}

void VansGraphics::VansHierachuWindow::DrawRectLightComponent(VansScriptRectLightComponent* comp)
{
    if (!comp || !comp->m_LightManager || comp->m_LightIndex < 0) return;
    auto& lights = comp->m_LightManager->GetRectLights();
    if (comp->m_LightIndex >= (int)lights.size()) return;

    VansRectLight& light = lights[comp->m_LightIndex];
    ImGui::PushID("RectLight");

    ImGui::ColorEdit3("Color",       &light.m_Color.x);
    ImGui::DragFloat ("Intensity",   &light.m_Intensity,    0.1f, 0.0f, 500.0f);

    // 宽/高在 GPU 以 half-extent 存储，Inspector 中以全宽/全高展示以便理解
    float fullW = light.m_HalfWidth  * 2.0f;
    float fullH = light.m_HalfHeight * 2.0f;
    if (ImGui::DragFloat("Width",  &fullW, 0.05f, 0.001f, 500.0f)) light.m_HalfWidth  = fullW * 0.5f;
    if (ImGui::DragFloat("Height", &fullH, 0.05f, 0.001f, 500.0f)) light.m_HalfHeight = fullH * 0.5f;

    ImGui::DragFloat ("Range",          &light.m_Range,         0.1f, 0.01f, 500.0f);
    ImGui::DragFloat ("Attenuation Exp",&light.m_AttenuationExp,0.01f, 0.5f, 4.0f);

    bool twoSided = light.m_TwoSided > 0.5f;
    if (ImGui::Checkbox("Two Sided", &twoSided)) light.m_TwoSided = twoSided ? 1.0f : 0.0f;

    bool castShadow = light.m_ShadowIndex >= 0.0f;
    if (ImGui::Checkbox("Cast Shadow", &castShadow))
        light.m_ShadowIndex = castShadow ? 0.0f : -1.0f; // 真正的 shadow slot 索引下一帧重新分配

    // 位置/基底向量由 Transform 驱动，只读显示
    ImGui::TextDisabled("Position (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Position.x, light.m_Position.y, light.m_Position.z);
    ImGui::TextDisabled("Normal   (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Normal.x,   light.m_Normal.y,   light.m_Normal.z);
    ImGui::TextDisabled("Right    (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Right.x,    light.m_Right.y,    light.m_Right.z);
    ImGui::TextDisabled("Up       (from transform):");
    ImGui::Text("  (%.3f, %.3f, %.3f)", light.m_Up.x,       light.m_Up.y,       light.m_Up.z);

    ImGui::PopID();
}

// ════════════════════════════════════════════════════════════════════════════
// Objects tab — list VansScriptObjects
// ════════════════════════════════════════════════════════════════════════════
void VansGraphics::VansHierachuWindow::DrawObjectList()
{
    const auto& objects = m_Scene->m_SceneObjects;
    if (objects.empty())
    {
        ImGui::TextDisabled("(no scene objects)");
        return;
    }

    for (auto* obj : objects)
    {
        if (!obj) continue;

        ImGui::PushID(obj);

        // Build a short type summary  e.g. "[Render | Physics]"
        std::string typeHint;
        if (obj->GetComponent<VansScriptRenderComponent>())          typeHint += "Render ";
        if (obj->GetComponent<VansScriptPhysicsComponent>())         typeHint += "Physics ";
        if (obj->GetComponent<VansScriptClothComponent>())           typeHint += "Cloth ";
        if (obj->GetComponent<VansScriptVehicleComponent>())         typeHint += "Vehicle ";
        if (obj->GetComponent<VansScriptRagdollComponent>())         typeHint += "Ragdoll ";
        if (obj->GetComponent<VansScriptDirectionalLightComponent>()) typeHint += "DirLight ";
        if (obj->GetComponent<VansScriptPointLightComponent>())       typeHint += "PointLight ";
        if (obj->GetComponent<VansScriptSpotLightComponent>())        typeHint += "SpotLight ";
        if (obj->GetComponent<VansScriptRectLightComponent>())        typeHint += "RectLight ";
        if (obj->GetComponent<VansScriptAudioComponent>())            typeHint += "Audio ";
        if (obj->GetComponent<VansScriptVideoComponent>())            typeHint += "Video ";

        char label[256];
        snprintf(label, sizeof(label), "%s  [%s]", obj->m_ObjectName.c_str(), typeHint.c_str());

        bool isSelected = (m_Scene->m_SelectedObject == obj);
        if (ImGui::Selectable(label, isSelected))
        {
            m_Scene->m_SelectedObject = obj;

            // Sync m_SelectedNode so that gizmos are drawn for this object
            auto* rc = obj->GetComponent<VansScriptRenderComponent>();
            m_Scene->m_SelectedNode = (rc && rc->m_RenderNode) ? rc->m_RenderNode : nullptr;
        }

        ImGui::PopID();
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Object Inspector — shows components of the selected VansScriptObject
// ════════════════════════════════════════════════════════════════════════════
void VansGraphics::VansHierachuWindow::DrawObjectDetail()
{
    if (!m_Scene->m_SelectedObject) return;

    VansScriptObject* obj = m_Scene->m_SelectedObject;

    ImGui::Begin("Object Inspector");

    // ── Header ─────────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Object: %s", obj->m_ObjectName.c_str());
    ImGui::Text("Transform ID: %u", obj->m_TransformID);
    ImGui::Text("Components: %d", (int)obj->m_Components.size());
    ImGui::Separator();

    // ── Transform (top-level, editable — drives gizmos via m_SelectedNode) ──
    auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
    if (renderComp && renderComp->m_RenderNode)
    {
        VansRenderNode* node = renderComp->m_RenderNode;
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawTransformDetail(*node);
        }
    }
    else if (obj->m_TransformID != 0)
    {
        // 灯光等无 RenderNode 的对象：直接编辑 VansTransformStore
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
            bool changed = false;

            if (ImGui::DragFloat3("Position", &t.m_Position.x, 0.1f))   changed = true;
            if (ImGui::DragFloat3("Rotation", &t.m_Rotation.x, 0.5f))   changed = true;
            if (ImGui::DragFloat3("Scale",    &t.m_Scale.x,    0.01f, 0.001f, 100.0f)) changed = true;

            (void)changed; // SyncLightTransforms 每帧都读 transform，无需额外标记
        }
    }

    // ── Render Component ──────────────────────────────────────────────
    if (renderComp && renderComp->m_RenderNode)
    {
        VansRenderNode* node = renderComp->m_RenderNode;
        if (ImGui::CollapsingHeader("Render Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Node Name: %s", node->m_NodeName.c_str());

            // Node type
            const char* nodeTypeStr = "Unknown";
            RenderNodeType ntype = node->GetNodeType();
            if (ntype & OPAQUE_NODE)       nodeTypeStr = "Opaque";
            if (ntype & TRANSPARENT_NODE)  nodeTypeStr = "Transparent";
            if (ntype & POSTPROCESS_NODE)  nodeTypeStr = "PostProcess";
            if (ntype & SKY_BOX_NODE)      nodeTypeStr = "SkyBox";
            ImGui::Text("Type: %s", nodeTypeStr);

            if (!node->m_ParentGroupName.empty())
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Group: %s", node->m_ParentGroupName.c_str());

            // Material summary
            if (node->m_Material)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Material");
                DrawMaterialDetail(*node->m_Material);
            }

            // Skeleton info
            if (node->m_HasSkeletonBone)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Has Skeleton Bones");
                ImGui::Text("Animation Enabled: %s", node->m_AnimationEnabled ? "Yes" : "No");
            }
        }
    }

    // ── Physics Component ─────────────────────────────────────────────
    auto* physicsComp = obj->GetComponent<VansScriptPhysicsComponent>();
    if (physicsComp && physicsComp->m_PhysicsNode)
    {
        VansEngine::VansPhysicsNode* pNode = physicsComp->m_PhysicsNode;
        const auto& props = pNode->GetProperties();

        if (ImGui::CollapsingHeader("Physics Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Name: %s", pNode->GetName().c_str());
            ImGui::Text("Enabled: %s", pNode->IsEnabled() ? "Yes" : "No");

            // Body type
            const char* bodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };
            int bodyIdx = (int)props.bodyType;
            if (bodyIdx >= 0 && bodyIdx < 3)
                ImGui::Text("Body Type: %s", bodyTypeNames[bodyIdx]);

            // Collider type
            const char* colliderNames[] = { "None", "Box", "Sphere", "Capsule", "Mesh", "ConvexMesh" };
            int colliderIdx = (int)props.colliderType;
            if (colliderIdx >= 0 && colliderIdx < 6)
                ImGui::Text("Collider: %s", colliderNames[colliderIdx]);

            // Shape-specific params
            switch (props.colliderType)
            {
            case VansEngine::PhysicsColliderType::Box:
                ImGui::Text("Box Extents: (%.2f, %.2f, %.2f)", props.boxExtents.x, props.boxExtents.y, props.boxExtents.z);
                break;
            case VansEngine::PhysicsColliderType::Sphere:
                ImGui::Text("Sphere Radius: %.2f", props.sphereRadius);
                break;
            case VansEngine::PhysicsColliderType::Capsule:
                ImGui::Text("Capsule Radius: %.2f  Half-Height: %.2f", props.capsuleRadius, props.capsuleHalfHeight);
                break;
            default:
                break;
            }

            ImGui::Text("Mass: %.2f", props.mass);

            // Material
            if (ImGui::TreeNode("Physics Material"))
            {
                ImGui::Text("Static Friction:  %.2f", props.material.staticFriction);
                ImGui::Text("Dynamic Friction: %.2f", props.material.dynamicFriction);
                ImGui::Text("Restitution:      %.2f", props.material.restitution);
                ImGui::TreePop();
            }

            // Runtime velocities (for dynamic bodies)
            if (props.bodyType == VansEngine::PhysicsBodyType::Dynamic)
            {
                glm::vec3 linVel = pNode->GetLinearVelocity();
                glm::vec3 angVel = pNode->GetAngularVelocity();
                ImGui::Text("Linear Vel:  (%.2f, %.2f, %.2f)", linVel.x, linVel.y, linVel.z);
                ImGui::Text("Angular Vel: (%.2f, %.2f, %.2f)", angVel.x, angVel.y, angVel.z);
            }
        }
    }

    // ── Cloth Component ───────────────────────────────────────────────
    auto* clothComp = obj->GetComponent<VansScriptClothComponent>();
    if (clothComp && clothComp->m_ClothNode)
    {
        VansEngine::VansClothNode* cNode = clothComp->m_ClothNode;

        if (ImGui::CollapsingHeader("Cloth Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Name: %s", cNode->GetName().c_str());
            ImGui::Text("Enabled: %s", cNode->IsEnabled() ? "Yes" : "No");

            const auto& sphereRefs = cNode->GetCollisionSphereRefs();
            ImGui::Text("Collision Spheres: %d", (int)sphereRefs.size());

            if (!sphereRefs.empty() && ImGui::TreeNode("Collision Spheres"))
            {
                for (size_t i = 0; i < sphereRefs.size(); i++)
                {
                    const auto& sph = sphereRefs[i];
                    ImGui::BulletText("%s  r=%.3f", sph.renderNodeName.c_str(), sph.radius);
                }
                ImGui::TreePop();
            }
        }
    }

    // ── Vehicle Component ─────────────────────────────────────────────
    auto* vehicleComp = obj->GetComponent<VansScriptVehicleComponent>();
    if (vehicleComp && vehicleComp->m_Vehicle)
    {
        VansEngine::VansPhysicsVehicle* vehicle = vehicleComp->m_Vehicle;

        if (ImGui::CollapsingHeader("Vehicle Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Body Render Node: %s", vehicle->GetBodyRenderNodeName().c_str());

            const auto& tireNames = vehicle->GetTireRenderNodeNames();
            ImGui::Text("Wheels: %u", vehicle->GetNumWheels());
            if (!tireNames.empty() && ImGui::TreeNode("Tire Render Nodes"))
            {
                for (size_t i = 0; i < tireNames.size(); i++)
                    ImGui::BulletText("[%d] %s", (int)i, tireNames[i].c_str());
                ImGui::TreePop();
            }
        }
    }

    // ── Ragdoll Component ─────────────────────────────────────────────
    auto* ragdollComp = obj->GetComponent<VansScriptRagdollComponent>();
    if (ragdollComp)
    {
        if (ImGui::CollapsingHeader("Ragdoll Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Profile: %s", ragdollComp->m_ProfileName.empty() ? "(none)" : ragdollComp->m_ProfileName.c_str());
            ImGui::TextDisabled("Path: %s", ragdollComp->m_ProfilePath.empty() ? "(none)" : ragdollComp->m_ProfilePath.c_str());
            ImGui::Text("Configured Bodies: %d", ragdollComp->m_ConfiguredBodyCount);
            ImGui::Text("Configured Joints: %d", ragdollComp->m_ConfiguredJointCount);
            ImGui::Separator();

            ImGui::Text("Runtime Bound: %s", ragdollComp->HasRuntimeRagdoll() ? "Yes" : "No");
            ImGui::Text("Runtime Bodies: %d", ragdollComp->GetRuntimeBodyCount());
            ImGui::Text("Runtime Joints: %d", ragdollComp->GetRuntimeJointCount());

            static const char* s_DriveModeNames[] = { "Animation", "Physics", "Blend" };
            int driveMode = ragdollComp->GetDriveMode();
            if (driveMode < 0 || driveMode > 2)
                driveMode = 0;
            if (ImGui::Combo("Drive Mode", &driveMode, s_DriveModeNames, IM_ARRAYSIZE(s_DriveModeNames)))
                ragdollComp->SetDriveMode(driveMode);

            float blendWeight = ragdollComp->GetBlendWeight();
            if (ImGui::SliderFloat("Blend Weight", &blendWeight, 0.0f, 1.0f, "%.2f"))
                ragdollComp->SetBlendWeight(blendWeight);

            if (ImGui::Button("Animation Mode##ragdoll"))
                ragdollComp->SetDriveMode(0);
            ImGui::SameLine();
            if (ImGui::Button("Physics Mode##ragdoll"))
                ragdollComp->SetDriveMode(1);
            ImGui::SameLine();
            if (ImGui::Button("Blend Mode##ragdoll"))
                ragdollComp->SetDriveMode(2);

            ImGui::Separator();
            if (ImGui::Button("Impulse Pelvis Up##ragdoll"))
                ragdollComp->ApplyImpulse("pelvis", 0.0f, 35.0f, 0.0f);
            ImGui::SameLine();
            if (ImGui::Button("Impulse Spine Forward##ragdoll"))
                ragdollComp->ApplyImpulse("spine_03", 0.0f, 0.0f, -25.0f);
        }
    }

    // ── Character Controller Component ───────────────────────────────────
    auto* cctComp = obj->GetComponent<VansScriptCharacterControllerComponent>();
    if (cctComp && cctComp->m_ControllerNode)
    {
        auto* cctNode = cctComp->m_ControllerNode;
        if (ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // 基础信息展示
            const auto& props = cctNode->GetProperties();
            ImGui::Text("Radius: %.3f  Height: %.3f", props.m_Radius, props.m_Height);
            glm::vec3 pos = cctNode->GetPosition();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text("Grounded: %s", cctNode->IsGrounded() ? "Yes" : "No");
            ImGui::Separator();

            // Follow Ragdoll 开关
            bool followEnabled = cctNode->IsFollowRagdollEnabled();
            if (ImGui::Checkbox("Follow Ragdoll##cct", &followEnabled))
            {
                if (followEnabled)
                {
                    auto* rdComp = obj->GetComponent<VansScriptRagdollComponent>();
                    if (rdComp)
                        cctComp->BindFollowRagdoll(rdComp, cctNode->GetFollowRagdollBone());
                    else
                        VANS_LOG_WARN("[Editor] CharController FollowRagdoll: 该对象无 Ragdoll 组件");
                }
                else
                {
                    cctComp->ClearFollowRagdoll();
                }
            }

            // 当 Follow Ragdoll 开启时显示/编辑根骨骼名称
            if (followEnabled)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(ragdoll drives CCT)");
                // 用局部 buffer 避免多对象共享静态变量
                char boneBuf[64] = {};
                snprintf(boneBuf, sizeof(boneBuf), "%s", cctNode->GetFollowRagdollBone().c_str());
                if (ImGui::InputText("Root Bone##cct", boneBuf, sizeof(boneBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    auto* rdComp = obj->GetComponent<VansScriptRagdollComponent>();
                    if (rdComp) cctComp->BindFollowRagdoll(rdComp, std::string(boneBuf));
                }
            }
        }
    }

    // ── Directional Light Component ───────────────────────────────────
    auto* dirLightComp = obj->GetComponent<VansScriptDirectionalLightComponent>();
    if (dirLightComp && dirLightComp->m_LightManager && dirLightComp->m_LightIndex >= 0)
    {
        auto& dirLights = dirLightComp->m_LightManager->GetDirectionLights();
        if (dirLightComp->m_LightIndex < (int)dirLights.size())
        {
            if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
                DrawDirectionalLightComponent(dirLightComp);
        }
    }

    // ── Point Light Component ─────────────────────────────────────────
    auto* pointLightComp = obj->GetComponent<VansScriptPointLightComponent>();
    if (pointLightComp && pointLightComp->m_LightManager && pointLightComp->m_LightIndex >= 0)
    {
        auto& pointLights = pointLightComp->m_LightManager->GetPointLights();
        if (pointLightComp->m_LightIndex < (int)pointLights.size())
        {
            if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
                DrawPointLightComponent(pointLightComp);
        }
    }

    // ── Spot Light Component ──────────────────────────────────────────
    auto* spotLightComp = obj->GetComponent<VansScriptSpotLightComponent>();
    if (spotLightComp && spotLightComp->m_LightManager && spotLightComp->m_LightIndex >= 0)
    {
        auto& spotLights = spotLightComp->m_LightManager->GetSpotLight();
        if (spotLightComp->m_LightIndex < (int)spotLights.size())
        {
            if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen))
                DrawSpotLightComponent(spotLightComp);
        }
    }

    // ── Rect Light Component (LTC area light) ────────────────────────
    auto* rectLightComp = obj->GetComponent<VansScriptRectLightComponent>();
    if (rectLightComp && rectLightComp->m_LightManager && rectLightComp->m_LightIndex >= 0)
    {
        auto& rectLights = rectLightComp->m_LightManager->GetRectLights();
        if (rectLightComp->m_LightIndex < (int)rectLights.size())
        {
            if (ImGui::CollapsingHeader("Rect Light", ImGuiTreeNodeFlags_DefaultOpen))
                DrawRectLightComponent(rectLightComp);
        }
    }

    // ── Camera Component ──────────────────────────────────────────────
    auto* camComp = obj->GetComponent<VansScriptCameraComponent>();
    if (camComp && camComp->m_Camera != nullptr)
    {
        VansCamera* cam = camComp->m_Camera;
        if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // FOV
            float fov = cam->GetFov();
            if (ImGui::SliderFloat("FOV", &fov, 10.0f, 170.0f, "%.1f deg"))
                cam->SetFov(fov);

            // Near / Far clip
            float nearClip = cam->GetNearClip();
            float farClip  = cam->GetFarClip();
            if (ImGui::InputFloat("Near Clip", &nearClip, 0.01f, 0.1f, "%.3f"))
                cam->SetNearClip((std::max)(0.001f, nearClip));
            if (ImGui::InputFloat("Far Clip", &farClip, 1.0f, 10.0f, "%.1f"))
                cam->SetFarClip((std::max)(nearClip + 0.1f, farClip));

            // Read-only position / rotation from Transform
            if (cam->HasTransform())
            {
                auto& t = VansGraphics::VansTransformStore::GetTransform(cam->GetTransformID());
                ImGui::Separator();
                ImGui::TextDisabled("Transform (read-only)");
                ImGui::Text("Position  %.2f  %.2f  %.2f",
                    t.m_Position.x, t.m_Position.y, t.m_Position.z);
                ImGui::Text("Pitch/Yaw  %.1f deg  /  %.1f deg",
                    t.m_Rotation.x, t.m_Rotation.y);
            }
        }
    }

    // ── Audio Component ───────────────────────────────────────────────
    auto* audioComp = obj->GetComponent<VansScriptAudioComponent>();
    if (audioComp && audioComp->m_AudioNode)
    {
        VansEngine::VansAudioNode* node = audioComp->m_AudioNode;
        if (ImGui::CollapsingHeader("Audio Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // 文件路径（只读）
            ImGui::TextDisabled("%s", node->GetFilePath().c_str());

            // 播放状态
            const char* stateStr = node->IsPlaying() ? "Playing"
                                 : node->IsPaused()  ? "Paused"
                                                     : "Stopped";
            ImGui::Text("State: %s", stateStr);

            ImGui::Separator();

            // 播放控制按钮
            if (ImGui::Button("Play"))   node->Play();
            ImGui::SameLine();
            if (ImGui::Button("Pause"))  node->Pause();
            ImGui::SameLine();
            if (ImGui::Button("Stop"))   node->Stop();
            ImGui::SameLine();
            if (ImGui::Button("Resume")) node->Resume();

            ImGui::Separator();

            // 音量滑道
            float vol = node->GetVolume();
            if (ImGui::SliderFloat("Volume##audio", &vol, 0.0f, 1.0f, "%.2f"))
                node->SetVolume(vol);

            // 音调滑道
            float pitch = node->GetPitch();
            if (ImGui::SliderFloat("Pitch##audio", &pitch, 0.1f, 4.0f, "%.2f"))
                node->SetPitch(pitch);

            // Loop 开关
            bool loop = node->GetLoop();
            if (ImGui::Checkbox("Loop##audio", &loop))
                node->SetLoop(loop);

            // 空间化开关
            bool spatial = node->GetSpatial();
            ImGui::SameLine();
            if (ImGui::Checkbox("Spatial##audio", &spatial))
                node->SetSpatial(spatial);
        }
    }

    // ── Video Component ─────────────────────────────────────────────
    auto* videoComp = obj->GetComponent<VansScriptVideoComponent>();
    if (videoComp && videoComp->m_VideoTex)
    {
        if (ImGui::CollapsingHeader("Video Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // 资源名（只读）
            ImGui::TextDisabled("%s", videoComp->m_VideoName.c_str());

            // 播放状态
            const char* stateStr = videoComp->m_VideoTex->IsPlaying() ? "Playing" : "Paused";
            ImGui::Text("State: %s", stateStr);

            ImGui::Separator();

            // 播放控制按鈕
            if (ImGui::Button("Play##video"))  videoComp->m_VideoTex->Play();
            ImGui::SameLine();
            if (ImGui::Button("Pause##video")) videoComp->m_VideoTex->Pause();
        }
    }

    // ── Particle Component ───────────────────────────────────────────
    auto* particleComp = obj->GetComponent<VansScriptParticleComponent>();
    if (particleComp)
    {
        if (ImGui::CollapsingHeader("Particle Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // 资产路径（只读）
            ImGui::TextDisabled("Asset: %s",
                particleComp->m_ParticleAssetPath.empty()
                    ? "(none)"
                    : particleComp->m_ParticleAssetPath.c_str());

            // ── 资产参数 ─────────────────────────────────────────
            VansGraphics::VansParticleAsset* asset = particleComp->m_ParticleAsset.get();
            if (asset)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Asset: %s", asset->m_Name.c_str());
                ImGui::Text("Duration:  %.2f s", asset->m_Duration);
                ImGui::Text("Loop:      %s",     asset->m_Loop    ? "Yes" : "No");
                ImGui::Text("Prewarm:   %s",     asset->m_Prewarm ? "Yes" : "No");
                ImGui::Text("Sim Space: %s",     asset->m_SimSpace.c_str());
                ImGui::Text("Emitters:  %d",     (int)asset->m_Emitters.size());
            }

            // ── 运行时状态 ───────────────────────────────────────
            ImGui::Separator();
            VansGraphics::VansParticleRuntime* rt = particleComp->m_Runtime.get();
            if (rt)
            {
                const char* playStateStr = rt->m_IsPlaying ? "Playing" : "Stopped";
                ImGui::Text("State:      %s",  playStateStr);
                ImGui::Text("Play Time:  %.2f s", rt->m_PlayTime);
                ImGui::Text("Alive:      %u",  rt->m_AliveInstanceCount.load());
            }
            else
            {
                ImGui::TextDisabled("Runtime not initialized");
            }

            ImGui::Text("Play On Awake: %s", particleComp->m_PlayOnAwake ? "Yes" : "No");

            // ── 播放控制按钮 ─────────────────────────────────────
            ImGui::Separator();
            if (ImGui::Button("Play##particle"))    particleComp->Play();
            ImGui::SameLine();
            if (ImGui::Button("Stop##particle"))    particleComp->Stop();
            ImGui::SameLine();
            if (ImGui::Button("Pause##particle"))   particleComp->Pause();
            ImGui::SameLine();
            if (ImGui::Button("Restart##particle")) particleComp->Restart();

            // ── 发射器列表 ───────────────────────────────────────
            if (asset && !asset->m_Emitters.empty())
            {
                ImGui::Spacing();
                if (ImGui::TreeNode("Emitters"))
                {
                    static const char* s_SpawnTypeNames[]  = { "RateOverTime", "Burst", "RateOverDistance" };
                    static const char* s_BlendModeNames[]  = { "Alpha", "Additive", "Multiply" };
                    static const char* s_RendererTypeNames[] = { "Billboard", "StretchedBillboard", "Mesh" };

                    for (size_t i = 0; i < asset->m_Emitters.size(); ++i)
                    {
                        const VansGraphics::VansParticleEmitter& em = *asset->m_Emitters[i];
                        char label[128];
                        std::snprintf(label, sizeof(label), "[%d] %s##em%d",
                            (int)i, em.m_Name.empty() ? "Emitter" : em.m_Name.c_str(), (int)i);

                        if (ImGui::TreeNode(label))
                        {
                            ImGui::Text("Enabled:      %s", em.m_Enabled ? "Yes" : "No");
                            ImGui::Text("Max Particles: %u", em.m_MaxParticles);

                            // 发射模式
                            int spawnIdx = (int)em.m_SpawnConfig.m_Type;
                            if (spawnIdx >= 0 && spawnIdx < 3)
                                ImGui::Text("Spawn Type:   %s", s_SpawnTypeNames[spawnIdx]);
                            if (em.m_SpawnConfig.m_Type == VansGraphics::VansSpawnType::RateOverTime)
                                ImGui::Text("Rate:         %.1f /s", em.m_SpawnConfig.m_Rate);
                            if (!em.m_SpawnConfig.m_Bursts.empty())
                                ImGui::Text("Bursts:       %d", (int)em.m_SpawnConfig.m_Bursts.size());

                            // 渲染配置
                            const VansGraphics::VansParticleRendererConfig& rc = em.m_RendererConfig;
                            int rendIdx  = (int)rc.m_Type;
                            int blendIdx = (int)rc.m_BlendMode;
                            if (rendIdx  >= 0 && rendIdx  < 3)
                                ImGui::Text("Renderer:     %s", s_RendererTypeNames[rendIdx]);
                            if (blendIdx >= 0 && blendIdx < 3)
                                ImGui::Text("Blend Mode:   %s", s_BlendModeNames[blendIdx]);
                            if (!rc.m_Texture.empty())
                                ImGui::Text("Texture:      %s", rc.m_Texture.c_str());
                            if (rc.m_SpriteSheetEnabled)
                                ImGui::Text("Sprite Sheet: %d x %d",
                                    rc.m_SpriteColumns, rc.m_SpriteRows);

                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
    }

    ImGui::End();
}

void VansGraphics::VansHierachuWindow::ShowWindow(VansVKDevice& device)
{
    // ── Main hierarchy window with tab bar ───────────────────────────
    ImGui::Begin("Hierarchy");

    if (ImGui::BeginTabBar("HierarchyTabs"))
    {
        if (ImGui::BeginTabItem("Objects"))
        {
            DrawObjectList();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Scene"))
        {
            DrawRenderNodeList();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Animations"))
        {
            DrawAnimationList();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    // ── Detail panels (drawn outside the tab window) ─────────────────
    DrawObjectDetail();
    DrawRenderNodeDetail();
    DrawAnimationNodeDetail();
}
