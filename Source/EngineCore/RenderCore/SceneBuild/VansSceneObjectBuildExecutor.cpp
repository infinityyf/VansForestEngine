#include "../VansScene.h"
#include "VansSceneAnimationComponentBuilder.h"
#include "VansSceneCameraMediaComponentBuilder.h"
#include "VansSceneClothAnimationBindingExecutor.h"
#include "VansSceneLightComponentBuilder.h"
#include "VansSceneParticleComponentBuilder.h"
#include "VansScenePhysicsComponentBuilder.h"
#include "VansSceneRenderNodeBuilder.h"
#include "VansSceneScriptComponentBuilder.h"
#include "VansSceneVehicleComponentBuilder.h"
#include "../VulkanCore/VansMesh.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <unordered_set>
#include <vector>
void VansGraphics::VansScene::LoadSceneObjects(VkDevice& device, json& objectsArray, const std::string& projectRoot)
{
    using namespace VansEngine;

    // === [VansSceneLoadPass::Pass1_ComponentInstantiation] ===
    // 依赖：项目资源与场景材质已加载；输出：对象、组件、渲染/物理等场景列表。
    // ── First pass: create all Objects and component instances ────────────
    // animation component 需要等待所有 render 节点创建完毕后再解析。
    struct ParentLink { std::string childName; std::string parentName; };
    struct ParentEntityLink { uint32_t childTransformID; std::string childName; std::string parentEntityGuid; };

    std::vector<ParentLink>    parentLinks;
    std::vector<ParentEntityLink> parentEntityLinks;
    std::vector<VansSceneAnimationComponentBuilder::PendingAnimationComponent> pendingAnimComps;
    std::unordered_set<uint32_t> vehicleDrivenTransformIDs;

    // ── 对象级 Transform 解析 helper ─────────────────────────────────────
    auto parseObjTransform = [](const json& objJson,
                                glm::vec3& outPos,
                                glm::vec3& outRot,
                                glm::vec3& outScl) -> bool
    {
        if (!objJson.contains("transform")) return false;
        const auto& t = objJson["transform"];
        if (t.contains("position") && t["position"].is_array())
            outPos = glm::vec3(t["position"][0].get<float>(),
                               t["position"][1].get<float>(),
                               t["position"][2].get<float>());
        if (t.contains("rotation") && t["rotation"].is_array())
            outRot = glm::vec3(t["rotation"][0].get<float>(),
                               t["rotation"][1].get<float>(),
                               t["rotation"][2].get<float>());
        if (t.contains("scale") && t["scale"].is_array())
            outScl = glm::vec3(t["scale"][0].get<float>(),
                               t["scale"][1].get<float>(),
                               t["scale"][2].get<float>());
        return true;
    };

    for (const auto& objJson : objectsArray)
    {
        VansScriptObject* obj = new VansScriptObject();
		obj->m_EntityGuid = objJson.value("entityGuid", "");
        obj->m_ObjectName = objJson.value("name", "");

        // ── 读取对象级 transform（新格式：与 components 并列）────────────
        glm::vec3 objPos(0.0f), objRot(0.0f), objScl(1.0f);
        bool hasObjTransform = parseObjTransform(objJson, objPos, objRot, objScl);

        auto& components = objJson["components"];

        // ── Non-render TransformID 分配（灯光 / 相机 / 逻辑根节点等无 render 组件的对象）──
		bool objectTransformAllocated = obj->m_OwnsTransform;

        auto ensureObjectTransform = [&]()
        {
            if (!objectTransformAllocated &&
                obj->GetComponent<VansScriptRenderComponent>() == nullptr)
            {
                obj->m_TransformID = VansTransformStore::AllocateTransform();
                obj->m_OwnsTransform = true;
                if (objJson.contains("transform"))
                {
                    const auto& tJson = objJson["transform"];
                    auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                    if (tJson.contains("position") && tJson["position"].is_array())
                    {
                        t.m_Position = glm::vec3(
                            tJson["position"][0].get<float>(),
                            tJson["position"][1].get<float>(),
                            tJson["position"][2].get<float>());
                    }
                    if (tJson.contains("rotation") && tJson["rotation"].is_array())
                    {
                        t.m_Rotation = glm::vec3(
                            tJson["rotation"][0].get<float>(),
                            tJson["rotation"][1].get<float>(),
                            tJson["rotation"][2].get<float>());
                    }
                    if (tJson.contains("scale") && tJson["scale"].is_array())
                    {
                        t.m_Scale = glm::vec3(
                            tJson["scale"][0].get<float>(),
                            tJson["scale"][1].get<float>(),
                            tJson["scale"][2].get<float>());
                    }
                }
                objectTransformAllocated = true;
            }
        };

        if (components.contains("render"))
        {
            const auto& renderJson = components["render"];

            // multi-mesh 展开在 LoadSingleRenderNode 内完成，需要将对象级 transform 传入副本。
            VansRenderNode* rn = nullptr;
            if (hasObjTransform)
            {
                // 为 multi-mesh 展开传递对象级 transform：构造一个带 transform 的副本
                json renderJsonWithTransform = renderJson;
                renderJsonWithTransform["transform"] = objJson["transform"];
                rn = VansSceneRenderNodeBuilder::LoadSingleRenderNode(*this, device, renderJsonWithTransform);
            }
            else
            {
                rn = VansSceneRenderNodeBuilder::LoadSingleRenderNode(*this, device, renderJson);
            }

            // 多网格对象（multi-mesh）经由 ExpandMultiMeshToRenderNodes 展开，
            // LoadSingleRenderNode 会返回 nullptr 而不创建单个节点。
            // 此处回退为取该 MultiMeshGroup 第一个子节点作为代理，
            // 使 physics / CCT / cloth 组件能获取到合法的 TransformID。
            // 所有子节点共享 MultiMeshGroup::sharedTransformID，
            // 对该 TransformID 的任何写入都会同步移动整个角色。
            if (!rn)
            {
                std::string nodeName = renderJson.value("name", "");
                auto groupIt = m_MultiMeshGroups.find(nodeName);
                if (groupIt != m_MultiMeshGroups.end() && !groupIt->second.childNodes.empty())
                    rn = groupIt->second.childNodes[0];
            }

            if (rn)
            {
                if (hasObjTransform)
                    rn->SetTransformData(objPos, objRot, objScl);

                auto* rc = new VansScriptRenderComponent();
                rc->m_ComponentName = "render";
                rc->m_RenderNode = rn;

                // ── 恢复 enabled 状态（场景保存时写入 JSON 的 enabled 字段）──
                bool renderEnabled = components["render"].value("enabled", true);
                if (!renderEnabled && rc->m_RenderNode)
                    rc->m_RenderNode->SetEnabled(false);
                rc->m_Enabled = renderEnabled;

                obj->AddComponent(rc);
                obj->m_TransformID = rn->m_TransformID;

                // Collect parent link if present
                if (renderJson.contains("parent"))
                {
                    ParentLink link;
                    link.childName  = renderJson.value("name", "");
                    link.parentName = renderJson["parent"].get<std::string>();
                    parentLinks.push_back(link);
                }
                if (renderJson.contains("parentEntityGuid") && renderJson["parentEntityGuid"].is_string())
                {
                    ParentEntityLink link;
                    link.childTransformID = rn->m_TransformID;
                    link.childName = renderJson.value("name", "");
                    link.parentEntityGuid = renderJson["parentEntityGuid"].get<std::string>();
                    if (!link.parentEntityGuid.empty())
                        parentEntityLinks.push_back(link);
                }
            }
        }

        VansScenePhysicsComponentBuilder::BuildPhysicsClothAndCharacter(
            *this, *obj, components, projectRoot, hasObjTransform, ensureObjectTransform);

        VansSceneVehicleComponentBuilder::AddVehiclePlaceholder(*obj, components);

        if (components.contains("multiMeshRoot") ||
            (components.contains("animation") && obj->GetComponent<VansScriptRenderComponent>() == nullptr))
        {
            ensureObjectTransform();
        }

        VansSceneLightComponentBuilder::BuildLights(
            *this, *obj, components, projectRoot, ensureObjectTransform);

        VansSceneCameraMediaComponentBuilder::BuildCameraAudioVideo(
            *this, *obj, components, ensureObjectTransform);

        VansSceneAnimationComponentBuilder::AddAnimationPlaceholder(*obj, components, pendingAnimComps);

        VansSceneParticleComponentBuilder::BuildParticle(
            *this, device, *obj, components, projectRoot, hasObjTransform, objPos, objRot, objScl);

        VansSceneScriptComponentBuilder::BuildPythonScripts(*obj, objJson);

        VansSceneLightComponentBuilder::BindExplicitVideoComponentToRectLight(*this, *obj);

        m_SceneObjects.push_back(obj);
    }

    // === [VansSceneLoadPass::Pass2_VehicleReference] ===
    // 依赖：Pass1 已创建对象与 render 组件；输出：Vehicle body/tire 引用完成绑定。
    // ── Second pass: resolve Vehicle component references ─────────────────
    vehicleDrivenTransformIDs = VansSceneVehicleComponentBuilder::ResolveVehicles(*this, objectsArray);

    // === [VansSceneLoadPass::Pass3_TransformParent] ===
    // 依赖：Pass1 已分配 TransformID；输出：TransformParentSystem 父子关系。
    // ── Third pass: resolve transform parent links ────────────────────────
    for (const auto& link : parentLinks)
    {
        if (link.childName.empty() || link.parentName.empty()) continue;
        VansRenderNode* childNode  = FindRenderNodeByName(link.childName);
        VansRenderNode* parentNode = FindRenderNodeByName(link.parentName);
        if (childNode && parentNode)
        {
            if (vehicleDrivenTransformIDs.count(childNode->m_TransformID) > 0)
                continue;
            m_TransformParentSystem.SetParent(childNode->m_TransformID, parentNode->m_TransformID);
        }
    }
    for (const auto& link : parentEntityLinks)
    {
        if (link.parentEntityGuid.empty())
            continue;
        VansScriptObject* parentObj = FindObjectByGuid(link.parentEntityGuid);
        if (parentObj && parentObj->m_TransformID != UINT32_MAX)
        {
            if (vehicleDrivenTransformIDs.count(link.childTransformID) > 0)
                continue;
            m_TransformParentSystem.SetParent(link.childTransformID, parentObj->m_TransformID);
        }
        else
        {
            VANS_LOG_WARN("[TransformParent] Could not resolve parent entity for child='"
                << link.childName << "' parentGuid='" << link.parentEntityGuid << "'");
        }
    }

    // === [VansSceneLoadPass::Pass3.5_MultiMeshGroupRebuild] ===
    // New object-hierarchy multi-mesh scenes serialize one child ModelRenderer per submesh.
    // Rebuild the runtime group from entity parent links so animation can bind by submesh index.
    for (size_t parentIndex = 0; parentIndex < objectsArray.size(); ++parentIndex)
    {
        const json& parentJson = objectsArray[parentIndex];
        const json& components = parentJson.value("components", json::object());
        if (!components.contains("multiMeshRoot") || !components["multiMeshRoot"].is_object())
            continue;

        const std::string parentGuid = parentJson.value("entityGuid", "");
        const std::string parentName = parentJson.value("name", "");
        if (parentGuid.empty() || parentName.empty())
            continue;

        const json& rootJson = components["multiMeshRoot"];
        const std::string modelGuid = rootJson.value("model", json::object()).value("guid", "");
        VansMesh* sourceMesh = static_cast<VansMesh*>(GetMeshAsset(modelGuid));
        if (!sourceMesh || !sourceMesh->m_IsMultiMesh)
        {
            VANS_LOG_WARN("[MultiMeshGroup] Root '" << parentName << "' references missing/non-multi mesh '" << modelGuid << "'");
            continue;
        }

        MultiMeshGroup& group = m_MultiMeshGroups[parentName];
        group.parentName = parentName;
        group.parentEntityGuid = parentGuid;
        group.sourceMesh = sourceMesh;
        group.childNodes.clear();

        VansScriptObject* parentObj = FindObjectByGuid(parentGuid);
        if (parentObj && parentObj->m_TransformID != UINT32_MAX)
            group.sharedTransformID = parentObj->m_TransformID;

        std::unordered_set<uint32_t> usedIndices;
        for (auto* childObj : m_SceneObjects)
        {
            if (!childObj || childObj->m_EntityGuid.empty())
                continue;
            auto* rc = childObj->GetComponent<VansScriptRenderComponent>();
            if (!rc || !rc->m_RenderNode)
                continue;
            VansRenderNode* node = rc->m_RenderNode;
            if (node->m_ParentEntityGuid != parentGuid)
                continue;
            if (node->m_SourceMesh != sourceMesh)
                continue;
            if (node->m_SubmeshIndex == UINT32_MAX)
                continue;
            if (!usedIndices.insert(node->m_SubmeshIndex).second)
            {
                VANS_LOG_WARN("[MultiMeshGroup] Duplicate submesh index " << node->m_SubmeshIndex
                    << " under root '" << parentName << "', keeping first node.");
                continue;
            }

            // Serialized submesh children are logical pieces of the MultiMeshRoot.
            // They must use the root transform directly; TransformParentSystem is
            // not part of VansRenderNode::ComputeModelDataFromTransform().
            const uint32_t oldTransformID = node->m_TransformID;
            if (vehicleDrivenTransformIDs.count(oldTransformID) > 0)
            {
                if (m_TransformParentSystem.HasParent(oldTransformID))
                    m_TransformParentSystem.ClearParent(oldTransformID);
                node->m_ParentGroupName = parentName;
                group.childNodes.push_back(node);
                continue;
            }
            if (oldTransformID != group.sharedTransformID)
            {
                if (m_TransformParentSystem.HasParent(oldTransformID))
                    m_TransformParentSystem.ClearParent(oldTransformID);
                node->ShareTransform(group.sharedTransformID);
            }
            childObj->m_TransformID = group.sharedTransformID;
            childObj->m_OwnsTransform = false;

            node->m_ParentGroupName = parentName;
            group.childNodes.push_back(node);
        }

        std::sort(group.childNodes.begin(), group.childNodes.end(),
            [](const VansRenderNode* lhs, const VansRenderNode* rhs)
            {
                return lhs->m_SubmeshIndex < rhs->m_SubmeshIndex;
            });
    }

    // === [VansSceneLoadPass::Pass4_AnimationRagdoll] ===
    // 依赖：Pass1/Pass3 已创建 render 与 transform；输出：动画节点与 ragdoll 绑定。
    // ── Fourth pass: resolve animation components ─────────────────────────
    // 此时所有 render 组件（及对应 MultiMeshGroup）均已创建完毕
    VansSceneAnimationComponentBuilder::ResolveAnimations(*this, pendingAnimComps, projectRoot);

    // === [VansSceneLoadPass::Pass5_ClothAnimationBinding] ===
    // 依赖：Pass4 已完全填充 m_AnimationNodes。
    // 为启用骨骼跟随（followBones）的布料节点绑定 AnimationNode，
    // 并通过 LateBindBonesFromProfile() 解析骨骼名称→索引映射。
    VansSceneClothAnimationBindingExecutor::Execute(*this);

    // ── 场景加载完成后，重新触发 auto_play 音频 ──────────────────────────
    // 原因：场景切换时 StopAll() 会停止所有播放；资源级 auto_play 只在
    // LoadFromJson 中触发一次，Runtime 重载后需在此补充调用。
    m_AudioManager.PlayAutoPlay();
}
