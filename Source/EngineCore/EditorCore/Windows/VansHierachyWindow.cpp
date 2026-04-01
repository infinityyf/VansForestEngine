#include "VansHierachyWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../PhysicsCore/VansClothNode.h"
#include "../../PhysicsCore/VansPhysicsVehicle.h"

#include "imgui.h"
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

    VansAnimationNode* anim = m_SelectedAnimationNode;

    ImGui::Begin("Animation Inspector");

    // ── Node name ────────────────────────────────────────────────────
    ImGui::Text("Animation Node: %s", anim->GetName().c_str());
    ImGui::Separator();

    // ── Clip name ────────────────────────────────────────────────────
    const std::string& clipName = anim->GetCurrentClipName();
    ImGui::Text("Clip:  %s", clipName.empty() ? "(none)" : clipName.c_str());

    // ── Playback state label ─────────────────────────────────────────
    AnimationState state = anim->GetState();
    const char* stateLabel = "Unknown";
    ImVec4 stateColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    switch (state)
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
    ImGui::TextColored(stateColor, "State: %s", stateLabel);

    ImGui::Separator();

    // ── Progress bar ─────────────────────────────────────────────────
    float progress  = anim->GetNormalizedTime();
    float curTime   = anim->GetCurrentTime();
    float duration  = anim->GetDuration();
    char progressLabel[64];
    std::snprintf(progressLabel, sizeof(progressLabel), "%.2fs / %.2fs", curTime, duration);
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressLabel);

    // ── Speed ─────────────────────────────────────────────────────────
    float speed = anim->GetSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 0.0f, 4.0f, "%.2fx"))
        anim->SetSpeed(speed);

    // ── Root Motion ──────────────────────────────────────────────────
    bool rootMotion = anim->IsRootMotionEnabled();
    if (ImGui::Checkbox("Root Motion", &rootMotion))
        anim->EnableRootMotion(rootMotion);

    ImGui::Spacing();

    // ── Clip list / selector ─────────────────────────────────────────
    std::vector<std::string> clipNames = anim->GetClipNames();
    if (!clipNames.empty())
    {
        if (ImGui::BeginCombo("Clip", clipName.empty() ? "(none)" : clipName.c_str()))
        {
            for (const auto& cn : clipNames)
            {
                bool isCurrent = (cn == clipName);
                if (ImGui::Selectable(cn.c_str(), isCurrent))
                {
                    AnimationPlaySettings settings;
                    settings.loop  = true;
                    settings.speed = speed;
                    anim->Play(cn, settings);
                }
                if (isCurrent)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Playback controls ─────────────────────────────────────────────
    bool isPlaying = (state == AnimationState::Playing || state == AnimationState::Blending);
    bool isPaused  = (state == AnimationState::Paused);

    if (isPlaying)
    {
        if (ImGui::Button("Pause"))
            anim->Pause();
    }
    else
    {
        if (ImGui::Button("Play"))
        {
            if (isPaused)
                anim->Resume();
            else
            {
                AnimationPlaySettings settings;
                settings.loop  = true;
                settings.speed = speed;
                anim->Play(clipName.empty() ? (clipNames.empty() ? "" : clipNames[0]) : clipName, settings);
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        anim->Stop();

    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        anim->SetTime(0.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Bone Hierarchy ───────────────────────────────────────────
    const Skeleton& skeleton = anim->GetSkeleton();
    if (!skeleton.bones.empty())
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.7f, 1.0f), "Bone Hierarchy  (%d bones)", (int)skeleton.bones.size());
        ImGui::Spacing();

        // Get the current clip for keyframe display
        const VansAnimationClip* currentClip = anim->GetClip(anim->GetCurrentClipName());
        float currentTime = anim->GetCurrentTime();

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
        if (obj->GetComponent<VansScriptRenderComponent>())   typeHint += "Render ";
        if (obj->GetComponent<VansScriptPhysicsComponent>())  typeHint += "Physics ";
        if (obj->GetComponent<VansScriptClothComponent>())    typeHint += "Cloth ";
        if (obj->GetComponent<VansScriptVehicleComponent>())  typeHint += "Vehicle ";

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
