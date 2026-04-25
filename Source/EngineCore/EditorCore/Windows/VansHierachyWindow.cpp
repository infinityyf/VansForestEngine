#include "VansHierachyWindow.h"
#include "VansAnimGraphEditorWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../PhysicsCore/VansClothNode.h"
#include "../../PhysicsCore/VansPhysicsVehicle.h"

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
    const char* typeNames[] = { "PBR", "Coat", "Transparent", "PostProcess", "SkyBox", "Deferred", "SSAO", "SSR", "Shadow", "Skin", "Cloth" };
    int typeIdx = (int)material.m_MaterialType;
    if (typeIdx >= 0 && typeIdx < 11)
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

    ImGui::Separator();

	switch (material.m_MaterialType)
	{
	case VansMaterialType::VAN_PBR:
		DrawPBRMaterialParameters(static_cast<VansPBRMaterial&>(material).m_BasePBRParam, index);
		break;
	case VansMaterialType::VAN_SKY_BOX:
        DrawAtmosphereParameters(static_cast<VansSkyBoxMaterial&>(material).m_AtmospherePBRParam);
		break;
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
        if (obj->GetComponent<VansScriptDirectionalLightComponent>()) typeHint += "DirLight ";
        if (obj->GetComponent<VansScriptPointLightComponent>())       typeHint += "PointLight ";
        if (obj->GetComponent<VansScriptSpotLightComponent>())        typeHint += "SpotLight ";

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
