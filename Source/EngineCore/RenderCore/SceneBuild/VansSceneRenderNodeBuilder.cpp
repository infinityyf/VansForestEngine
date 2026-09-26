#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "VansSceneRenderNodeBuilder.h"

#include "VansSceneMaterialBuilder.h"
#include "../VulkanCore/VansMesh.h"
#include "../../Util/VansLog.h"

#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/matrix_decompose.hpp>
#include <../../GLM/gtx/quaternion.hpp>

namespace VansGraphics
{
namespace
{
bool DecomposeMatrixToPRS(const glm::mat4& matrix,
                          glm::vec3& outPosition,
                          glm::vec3& outRotationDegrees,
                          glm::vec3& outScale)
{
	glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 skew;
	glm::vec4 perspective;
	if (!glm::decompose(matrix, outScale, rotation, outPosition, skew, perspective))
		return false;

	rotation = glm::normalize(rotation);
	outRotationDegrees = glm::degrees(glm::eulerAngles(rotation));
	return true;
}

glm::vec3 ToVec3(const std::array<float, 3>& value)
{
	return glm::vec3(value[0], value[1], value[2]);
}

bool TryParseRenderNodeType(
	const std::string& typeValue,
	VansGraphics::RenderNodeType& outType,
	std::string& error)
{
	const std::string& s = typeValue;
	if (s == "opaque") outType = VansGraphics::OPAQUE_NODE;
	else if (s == "forward_opaque_pre_atmosphere")
		outType = VansGraphics::FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE;
	else if (s == "transparent") outType = VansGraphics::TRANSPARENT_NODE;
	else if (s == "post_process") outType = VansGraphics::POSTPROCESS_NODE;
	else if (s == "decal") outType = VansGraphics::DECAL_NODE;
	else
	{
		error = "Unsupported render type '" + s + "'";
		return false;
	}
	return true;
}

static VansGraphics::RenderNodeType ResolveMaterialRenderNodeType(
    const VansGraphics::VansMaterial* material,
    VansGraphics::RenderNodeType serializedType)
{
    if (!material || material->m_MaterialType != VansGraphics::VansMaterialType::VAN_CUSTOM_SHADER)
        return serializedType;

    if (material->HasPass(VansGraphics::VansPass::GBUFFER))
        return VansGraphics::RenderNodeType::OPAQUE_NODE;

    return material->m_CustomShaderDepthWrite
        ? VansGraphics::RenderNodeType::FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE
        : VansGraphics::RenderNodeType::TRANSPARENT_NODE;
}

static bool HasMeshAssetName(const VansGraphics::VansScene& scene, const std::string& name)
{
    for (auto* mesh : scene.GetMeshAssets())
    {
        if (mesh && mesh->m_AssetName == name)
            return true;
    }
    return false;
}

static bool HasMaterialAssetName(const VansGraphics::VansScene& scene, const std::string& name)
{
    for (auto* material : scene.GetMaterialAssets())
    {
        if (material && material->m_AssetName == name)
            return true;
    }
    return false;
}

static std::string MakeUniqueMultiMeshGroupName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (scene.HasMultiMeshGroup(candidate))
    {
        candidate = baseName + "_grp" + std::to_string(suffix++);
    }
    return candidate;
}

static std::string MakeUniqueMaterialName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (HasMaterialAssetName(scene, candidate))
    {
        candidate = baseName + "_mat" + std::to_string(suffix++);
    }
    return candidate;
}

static std::string MakeUniqueRenderNodeName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (scene.FindRenderNodeByName(candidate) != nullptr)
    {
        candidate = baseName + "_node" + std::to_string(suffix++);
    }
    return candidate;
}

static std::string MakeUniqueMeshName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (HasMeshAssetName(scene, candidate))
    {
        candidate = baseName + "_mesh" + std::to_string(suffix++);
    }
    return candidate;
}


}

bool VansSceneRenderNodeBuilder::BuildDeferredNode(
    VansScene& scene,
    VkDevice& device,
    std::string& error)
{
    VansMesh* mesh = static_cast<VansMesh*>(scene.FindMeshAsset("fullScreenQuad"));
	if (mesh == nullptr)
	{
		error = "Required engine mesh 'fullScreenQuad' is missing";
		return false;
	}

    // Build material directly from the already-loaded "Deferred" shader — no JSON material entry needed.
    VansGraphicsShader* deferredShader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset("Deferred"));
    if (deferredShader == nullptr)
    {
        error = "Required shader 'Deferred' is missing";
        return false;
    }
    VansDeferredMaterial* material = new VansDeferredMaterial();
    material->m_MaterialType = VansMaterialType::VAN_DEFERRED;
    material->m_PassShaders[VansPass::DEFERRED] = deferredShader;
    material->SetName("DeferredMaterial");
    scene.AddMaterialAsset(material);

    RenderNodeType type = RenderNodeType::DEFERRED_NODE;
    VansRenderNode* renderNode = new VansDeferredRenderNode(device, type);

    renderNode->m_Mesh = mesh;
    renderNode->m_Material = material;

    //renderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);

    renderNode->SetName("DeferredNode");

    scene.RegistRenderNode(renderNode, type);
	return true;
}

bool VansSceneRenderNodeBuilder::BuildScreenSpaceFeatureNodes(
    VansScene& scene,
    VkDevice& device,
    std::string& error)
{
    VansMesh* mesh = static_cast<VansMesh*>(scene.FindMeshAsset("fullScreenQuad"));
	if (mesh == nullptr)
	{
		error = "Required engine mesh 'fullScreenQuad' is missing";
		return false;
	}

    // Each entry: { node/material name, shader name, material type }.
    // Materials are built internally — no JSON material entries needed.
    struct FeatureEntry { const char* name; const char* shaderName; VansMaterialType matType; };
    static const FeatureEntry features[] =
    {
        { "SSAO", "SSAO", VansMaterialType::VAN_SCREEN_SPACE_AO },
    };
	for (const auto& feature : features)
	{
		if (!scene.FindShaderAsset(feature.shaderName))
		{
			error = "Required screen-space shader '" + std::string(feature.shaderName) +
				"' is missing";
			return false;
		}
	}

    for (const auto& feature : features)
    {
        VansMaterial* material = VansSceneMaterialBuilder::CreateMaterialForType(feature.matType);
        material->m_MaterialType = feature.matType;
        VansSceneMaterialBuilder::PopulateMaterialPassShaders(scene, material, feature.matType);
		if (!material->HasPass(VansPass::SCREEN_SPACE))
		{
			delete material;
			error = "Screen-space material '" + std::string(feature.name) +
				"' has no SCREEN_SPACE shader binding";
			return false;
		}
        material->SetName(feature.name);
        scene.AddMaterialAsset(material);

        RenderNodeType type = RenderNodeType::SCREEN_SPACE_NODE;
        VansRenderNode* renderNode = new VansScreenSpaceRenderNode(device, type);

        renderNode->m_Mesh = mesh;
        renderNode->m_Material = material;

        //renderNode->CreateDescriptorSets(m_Camera, m_LightManager,m_MaterialManager);

        renderNode->SetName(feature.name);

        scene.RegistRenderNode(renderNode, type);
    }
	return true;
}

VansRenderNodeBuildResult VansSceneRenderNodeBuilder::BuildRenderNode(
	VansScene& scene,
	VkDevice& device,
	const Vans::VansSceneRenderNodeConfig& sceneRenderNode)
{
	VansRenderNodeBuildResult result;
	RenderNodeType type = RenderNodeType::NONE_NODE;
	if (!TryParseRenderNodeType(sceneRenderNode.type, type, result.error))
		return result;
    std::string meshName = sceneRenderNode.mesh;

    // ── Resolve mesh ──────────────────────────────────────────────────────
    VansMesh* mesh = static_cast<VansMesh*>(scene.FindMeshAsset(meshName));
	if (!mesh)
	{
		result.error = "Render node '" + sceneRenderNode.name + "' could not resolve mesh '" +
			meshName + "'";
		return result;
	}
	std::string materialName = sceneRenderNode.material;
	VansMaterial* material = static_cast<VansMaterial*>(scene.FindMaterialAsset(materialName));
    VansMesh* sourceMesh = nullptr;
    uint32_t submeshIndex = UINT32_MAX;
    const bool hasSerializedSubmesh = sceneRenderNode.submesh.has_value();

    if (hasSerializedSubmesh)
    {
        sourceMesh = mesh;
        submeshIndex = *sceneRenderNode.submesh;
        if (!sourceMesh || !sourceMesh->m_IsMultiMesh)
        {
			result.error = "Render node '" + sceneRenderNode.name + "' references submesh " +
				std::to_string(submeshIndex) + " but mesh '" + meshName +
				"' is not a loaded multi-mesh";
			return result;
        }
        if (submeshIndex >= sourceMesh->m_SubMeshes.size() || sourceMesh->m_SubMeshes[submeshIndex] == nullptr)
        {
			result.error = "Render node '" + sceneRenderNode.name + "' has invalid submesh index " +
				std::to_string(submeshIndex) + " for mesh '" + meshName + "'";
			return result;
        }

        // 每个子网格都拥有独立的顶点/索引 GPU buffer；它和普通网格一样是
        // TLAS 的完整几何单元。不得因为来源是 multi-mesh 而关闭 RT，否则
        // 场景可见墙体会从 DDGI 相交中消失。
        mesh = sourceMesh->m_SubMeshes[submeshIndex];

        const FBXSubmeshMaterialInfo& fbxInfo = sourceMesh->m_SubmeshMaterialInfos.empty()
            ? FBXSubmeshMaterialInfo{}
            : (submeshIndex < sourceMesh->m_SubmeshMaterialInfos.size()
                ? sourceMesh->m_SubmeshMaterialInfos[submeshIndex]
                : sourceMesh->m_SubmeshMaterialInfos[0]);

        VansMaterialType matType = material ? material->m_MaterialType
            : (fbxInfo.IsTransparent() ? VansMaterialType::VAN_TRANSPARENT : VansMaterialType::VAN_PBR);

        if (!material)
        {
            const std::string slotName = sceneRenderNode.submeshSlotName;
            std::string materialBaseName = sceneRenderNode.name.empty() ? meshName : sceneRenderNode.name;
            if (!slotName.empty())
                materialBaseName += "_" + slotName;
            else if (!fbxInfo.materialName.empty())
                materialBaseName += "_" + fbxInfo.materialName;
            std::string matKey = MakeUniqueMaterialName(scene, materialBaseName);

            material = VansSceneMaterialBuilder::CreateMaterialForType(matType);
            material->m_MaterialType = matType;
            material->SetName(matKey);
            VansSceneMaterialBuilder::PopulateMaterialPassShaders(scene, material, matType);

            if (matType == VansMaterialType::VAN_PBR)
            {
                VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(material);
                VansTexture* diffTex  = scene.FindOrLoadTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* normTex  = scene.FindOrLoadTexture(fbxInfo.normalTexPath, false);
                VansTexture* metalTex = scene.FindOrLoadTexture(fbxInfo.metallicTexPath, false);
                VansTexture* roughTex = scene.FindOrLoadTexture(fbxInfo.roughnessTexPath, false);
                VansTexture* aoTex    = scene.FindOrLoadTexture(fbxInfo.aoTexPath, false);

                pbr->m_BaseColorTexture = scene.ResolveTextureAssetOrDefault(diffTex, "defaultAlbedo");
                pbr->m_NormalTexture    = scene.ResolveTextureAssetOrDefault(normTex, "defaultNormal");
                pbr->m_MetalTexture     = scene.ResolveTextureAssetOrDefault(metalTex, "defaultMetal");
                pbr->m_RoughnessTexture = scene.ResolveTextureAssetOrDefault(roughTex, "defaultRoughness");
                pbr->m_AoTexture        = scene.ResolveTextureAssetOrDefault(aoTex, "defaultAo");

                pbr->m_BasePBRParam.m_albedo    = glm::vec3(fbxInfo.diffuseColor[0], fbxInfo.diffuseColor[1], fbxInfo.diffuseColor[2]);
                pbr->m_BasePBRParam.m_metallic  = fbxInfo.metallic;
                pbr->m_BasePBRParam.m_roughness = fbxInfo.roughness;
                pbr->m_BasePBRParam.m_ao        = 1.0f;
            }
            else if (matType == VansMaterialType::VAN_TRANSPARENT)
            {
                VansTransparentMaterial* trans = static_cast<VansTransparentMaterial*>(material);
                VansTexture* diffTex    = scene.FindOrLoadTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* opacityTex = scene.FindOrLoadTexture(fbxInfo.opacityTexPath, false);

                if (diffTex)
                {
                    trans->m_TransparentTextureMap.push_back({ "diffuse", diffTex->m_AssetName });
                    trans->m_TransparentTextures.push_back(diffTex);
                }
                if (opacityTex)
                {
                    trans->m_TransparentTextureMap.push_back({ "opacity", opacityTex->m_AssetName });
                    trans->m_TransparentTextures.push_back(opacityTex);
                }
            }

            scene.AddMaterialAsset(material);
			result.createdMaterials.push_back(material);
            VANS_LOG("[BuildRenderNode] Auto-created submesh material: " << matKey
                << " for node '" << sceneRenderNode.name << "'");
        }

        type = (matType == VansMaterialType::VAN_TRANSPARENT ||
            matType == VansMaterialType::VAN_PBR_TRANSMISSION)
            ? RenderNodeType::TRANSPARENT_NODE
            : RenderNodeType::OPAQUE_NODE;

        const std::string meshAlias = MakeUniqueMeshName(scene, (sceneRenderNode.name.empty() ? meshName : sceneRenderNode.name) + "_mesh");
        mesh->SetName(meshAlias);
        scene.AddSceneSubMeshAsset(mesh);
		result.registeredSubMeshes.push_back(mesh);
    }

    // ── Multi-mesh auto-expansion ─────────────────────────────────────────
    if (mesh && mesh->m_IsMultiMesh && !hasSerializedSubmesh)
    {
        glm::vec3 position(0), rotation(0), scale(1);
        if (sceneRenderNode.transform.has_value())
        {
            position = ToVec3(sceneRenderNode.transform->position);
            rotation = ToVec3(sceneRenderNode.transform->rotation);
            scale = ToVec3(sceneRenderNode.transform->scale);
        }
        bool supportShadow = sceneRenderNode.supportShadow;
        uint32_t shadowCasterMask = sceneRenderNode.shadowCasterMask;
        std::string parentName = sceneRenderNode.name.empty() ? "MultiMesh" : sceneRenderNode.name;

		return VansSceneRenderNodeBuilder::ExpandMultiMeshToRenderNodes(scene,
			device, mesh, parentName, sceneRenderNode.entityGuid,
			position, rotation, scale, supportShadow, shadowCasterMask, material,
			sceneRenderNode.submeshMaterialOverrides);
	}
	if (!material)
	{
		result.error = "Render node '" + sceneRenderNode.name + "' could not resolve material '" +
			materialName + "'";
		return result;
	}

    // ── Standard render node creation ─────────────────────────────────────
	if (material && material->m_MaterialType == VansMaterialType::VAN_HAIR)
	{
		type = RenderNodeType::HAIR_NODE;
	}
	else
	{
		type = ResolveMaterialRenderNodeType(material, type);
	}

	VansRenderNode* renderNode = nullptr;
	switch (type)
	{
    case VansGraphics::OPAQUE_NODE:
	case VansGraphics::HAIR_NODE:
	case VansGraphics::FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE:
		renderNode = new VansCommonRenderNode(device, type);
		if (sceneRenderNode.supportShadow)
		{
			auto* node = static_cast<VansCommonRenderNode*>(renderNode);
            node->m_SupportShadow = sceneRenderNode.supportShadow;
            node->m_ShadowCasterMask = sceneRenderNode.shadowCasterMask;
        }
        break;
    case VansGraphics::TRANSPARENT_NODE:
		renderNode = new VansTransparentRenderNode(device, type);
        break;
    case VansGraphics::POSTPROCESS_NODE:
		renderNode = new VansPostProcessRenderNode(device, type);
        break;
    case VansGraphics::DECAL_NODE:
        // 贴花节点：OBB 投影贴花，写入 GBuffer Normal/GBuffer0/GBuffer1
		renderNode = new VansDecalRenderNode(device);
        break;
	default:
		result.error = "Render node '" + sceneRenderNode.name + "' has unsupported runtime type";
		return result;
	}

	if (renderNode == nullptr)
	{
		result.error = "Render node '" + sceneRenderNode.name + "' was not allocated";
		return result;
    }

    if (sceneRenderNode.transform.has_value())
    {
		glm::vec3 position = ToVec3(sceneRenderNode.transform->position);
        glm::vec3 rotation = ToVec3(sceneRenderNode.transform->rotation);
        glm::vec3 scale = ToVec3(sceneRenderNode.transform->scale);
		renderNode->SetTransformData(position, rotation, scale);
	}

	renderNode->m_Mesh = mesh;
	renderNode->m_SourceMesh = sourceMesh;
	renderNode->m_SubmeshIndex = submeshIndex;
	renderNode->m_EntityGuid = sceneRenderNode.entityGuid;
	renderNode->m_ParentEntityGuid = sceneRenderNode.parentEntityGuid;
	renderNode->m_Material = material;
	if (auto* decal = dynamic_cast<VansDecalRenderNode*>(renderNode))
		decal->m_ImpactPoolConfig = sceneRenderNode.impactPool;
	const std::string rayTracingMode = sceneRenderNode.rayTracingMode;
	const bool transparentForGI = material &&
		(material->m_MaterialType == VansMaterialType::VAN_TRANSPARENT ||
		 material->m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION);
	renderNode->m_RayTracingEnabled = rayTracingMode != "disabled" && !transparentForGI;
	renderNode->SetName(sceneRenderNode.name);

	scene.RegistRenderNode(renderNode, type);

	result.success = true;
	result.nodes.push_back(renderNode);
	return result;
}

bool VansSceneRenderNodeBuilder::BuildRenderNodes(
	VansScene& scene,
	VkDevice& device,
	const Vans::VansSceneRenderNodeConfigs& renderNodes,
	std::string& error)
{
	for (const Vans::VansSceneRenderNodeConfig& sceneRenderNode : renderNodes)
	{
		VansRenderNodeBuildResult result = BuildRenderNode(scene, device, sceneRenderNode);
		if (!result.success)
		{
			error = std::move(result.error);
			return false;
		}
	}

	return true;
}

// ===========================================================================
// Multi-mesh expansion

VansRenderNodeBuildResult VansSceneRenderNodeBuilder::ExpandMultiMeshToRenderNodes(VansScene& scene,
    VkDevice& device,
    VansMesh* multiMesh,
    const std::string& parentName,
    const std::string& parentEntityGuid,
    const glm::vec3& position,
	const glm::vec3& rotation,
	const glm::vec3& scale,
	bool supportShadow,
	uint32_t shadowCasterMask,
	VansMaterial* materialOverride,
	const std::unordered_map<std::string, std::string>& submeshMaterialOverrides)
{
	VansRenderNodeBuildResult result;
    if (!multiMesh || !multiMesh->m_IsMultiMesh)
	{
		result.error = "Multi-mesh expansion requires a loaded multi-mesh";
		return result;
	}

	const auto& subMeshes = multiMesh->m_SubMeshes;
	const bool hasRenderableSubMesh = std::any_of(
		subMeshes.begin(),
		subMeshes.end(),
		[](VansMesh* subMesh)
		{
			return subMesh && subMesh->GetMeshVertexCount() > 0 && subMesh->GetIndexCount() >= 3;
		});
	if (!hasRenderableSubMesh)
	{
		result.error = "Multi-mesh '" + multiMesh->m_AssetName + "' produced no render nodes";
		return result;
	}

    const std::string resolvedParentName = parentEntityGuid.empty()
        ? MakeUniqueMultiMeshGroupName(scene, parentName) : parentName;
    const std::string groupKey = parentEntityGuid.empty() ? resolvedParentName : parentEntityGuid;
    if (resolvedParentName != parentName)
    {
        VANS_LOG_WARN("[ExpandMultiMesh] Parent group name conflict for '" << parentName
            << "', renamed to '" << resolvedParentName << "'.");
    }

    // ── Create or retrieve the multi-mesh group for hierarchy display ─────
    MultiMeshGroup& group = scene.GetOrCreateMultiMeshGroup(groupKey);
    group.parentName = resolvedParentName;
    group.parentEntityGuid = parentEntityGuid;
    group.sourceMesh = multiMesh;
    group.position   = position;
    group.rotation   = rotation;
    group.scale      = scale;
    const bool hasNodeTransformAnimation = multiMesh->m_HasNodeTransformAnimation;
    if (hasNodeTransformAnimation)
    {
        group.sharedTransformID = Vans::VansTransformStore::Allocate();
        group.ownsSharedTransform = true;
        Vans::VansTransform rootTransform = Vans::VansTransformStore::Read(group.sharedTransformID);
        rootTransform.m_Position = position;
        rootTransform.m_Rotation = rotation;
        rootTransform.m_Scale = scale;
        Vans::VansTransformStore::Write(group.sharedTransformID, rootTransform);
    }
    else
    {
        group.ownsSharedTransform = false;
    }

    const auto& matInfos   = multiMesh->m_SubmeshMaterialInfos;

    for (size_t i = 0; i < subMeshes.size(); ++i)
    {
        VansMesh* subMesh = subMeshes[i];
        if (subMesh == nullptr)
        {
            VANS_LOG_WARN("[ExpandMultiMesh] Submesh[" << i << "] is null in group '"
                << resolvedParentName << "'. Skipping.");
            continue;
        }

        const uint32_t vertexCount = subMesh->GetMeshVertexCount();
        const uint32_t indexCount = subMesh->GetIndexCount();
        const uint32_t triangleCount = indexCount / 3;

        if (vertexCount == 0 || triangleCount == 0)
        {
            VANS_LOG_WARN("[ExpandMultiMesh] Submesh[" << i << "] is invalid for group '"
                << resolvedParentName << "' (vertices=" << vertexCount
                << ", triangles=" << triangleCount << "). Skipping.");
            continue;
        }

        // Strict 1:1 submesh-to-material mapping.
        // If the material info array is shorter than the submesh array,
        // fall back to the first material info ("if has multi material use first one").
        const FBXSubmeshMaterialInfo& fbxInfo = matInfos.empty()
            ? FBXSubmeshMaterialInfo{}
            : (i < matInfos.size() ? matInfos[i] : matInfos[0]);

        // ── Determine material type ───────────────────────────────────────
        VansMaterialType matType = fbxInfo.IsTransparent()
            ? VansMaterialType::VAN_TRANSPARENT
            : VansMaterialType::VAN_PBR;

        // ── Unique material name: nodeName + materialName ───────────────────
        const std::string& nodeName = subMesh->m_SourceNodeName;
        const std::string materialBaseName = resolvedParentName + "_" + nodeName + "_" + fbxInfo.materialName;
        std::string matKey = MakeUniqueMaterialName(scene, materialBaseName);

		VansMaterial* material = materialOverride;
		const std::string submeshKey = std::to_string(i);
		if (const auto overrideIt = submeshMaterialOverrides.find(submeshKey);
			overrideIt != submeshMaterialOverrides.end())
		{
			const std::string& materialGuid = overrideIt->second;
			if (!materialGuid.empty())
			{
				if (auto* resolved = static_cast<VansMaterial*>(scene.FindMaterialAsset(materialGuid)))
					material = resolved;
				else
					VANS_LOG_WARN("[ExpandMultiMesh] Submesh[" << i
						<< "] material override not found: " << materialGuid);
			}
		}
		if (material)
			matType = material->m_MaterialType;
		else
        {
            material = VansSceneMaterialBuilder::CreateMaterialForType(matType);
            material->m_MaterialType = matType;
            material->SetName(matKey);
            VansSceneMaterialBuilder::PopulateMaterialPassShaders(scene, material, matType);

            if (matType == VansMaterialType::VAN_PBR)
            {
                VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(material);
                // ── PBR material: load textures from FBX info ─────────────────
                VansTexture* diffTex  = scene.FindOrLoadTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* normTex  = scene.FindOrLoadTexture(fbxInfo.normalTexPath, false);
                VansTexture* metalTex = scene.FindOrLoadTexture(fbxInfo.metallicTexPath, false);
                VansTexture* roughTex = scene.FindOrLoadTexture(fbxInfo.roughnessTexPath, false);
                VansTexture* aoTex    = scene.FindOrLoadTexture(fbxInfo.aoTexPath, false);

                pbr->m_BaseColorTexture = scene.ResolveTextureAssetOrDefault(diffTex, "defaultAlbedo");
                pbr->m_NormalTexture    = scene.ResolveTextureAssetOrDefault(normTex, "defaultNormal");
                pbr->m_MetalTexture     = scene.ResolveTextureAssetOrDefault(metalTex, "defaultMetal");
                pbr->m_RoughnessTexture = scene.ResolveTextureAssetOrDefault(roughTex, "defaultRoughness");
                pbr->m_AoTexture        = scene.ResolveTextureAssetOrDefault(aoTex, "defaultAo");

                pbr->m_BasePBRParam.m_albedo    = glm::vec3(fbxInfo.diffuseColor[0], fbxInfo.diffuseColor[1], fbxInfo.diffuseColor[2]);
                pbr->m_BasePBRParam.m_metallic  = fbxInfo.metallic;
                pbr->m_BasePBRParam.m_roughness = fbxInfo.roughness;
                pbr->m_BasePBRParam.m_ao        = 1.0f;
            }
            else if (matType == VansMaterialType::VAN_TRANSPARENT)
            {
                VansTransparentMaterial* trans = static_cast<VansTransparentMaterial*>(material);
                // ── Transparent material: load diffuse + opacity as texture slots ─
                VansTexture* diffTex    = scene.FindOrLoadTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* opacityTex = scene.FindOrLoadTexture(fbxInfo.opacityTexPath, false);

                if (diffTex)
                {
                    trans->m_TransparentTextureMap.push_back({ "diffuse", diffTex->m_AssetName });
                    trans->m_TransparentTextures.push_back(diffTex);
                }
                if (opacityTex)
                {
                    trans->m_TransparentTextureMap.push_back({ "opacity", opacityTex->m_AssetName });
                    trans->m_TransparentTextures.push_back(opacityTex);
                }
            }

            scene.AddMaterialAsset(material);
			result.createdMaterials.push_back(material);

            VANS_LOG("[ExpandMultiMesh] Auto-created material: " << matKey
                     << " (type=" << static_cast<int>(matType) << ")");
        }

        // ── Create render node for this submesh ──────────────────────────
		RenderNodeType nodeType = (matType == VansMaterialType::VAN_TRANSPARENT ||
			matType == VansMaterialType::VAN_PBR_TRANSMISSION)
			? RenderNodeType::TRANSPARENT_NODE
			: RenderNodeType::OPAQUE_NODE;
		if (!submeshMaterialOverrides.empty() && matType == VansMaterialType::VAN_HAIR)
			nodeType = RenderNodeType::HAIR_NODE;
		nodeType = ResolveMaterialRenderNodeType(material, nodeType);

        VansRenderNode* renderNode = nullptr;

		if (nodeType == RenderNodeType::OPAQUE_NODE ||
			nodeType == RenderNodeType::HAIR_NODE ||
			nodeType == RenderNodeType::FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE)
        {
            auto* opaque = new VansCommonRenderNode(device, nodeType);
            opaque->m_SupportShadow = supportShadow;
            opaque->m_ShadowCasterMask = shadowCasterMask;
            renderNode = opaque;
        }
        else
        {
            renderNode = new VansTransparentRenderNode(device, nodeType);
        }

        const std::string nodeBaseName = resolvedParentName + "_" + nodeName + "_" + fbxInfo.materialName;
        std::string renderNodeName = MakeUniqueRenderNodeName(scene, nodeBaseName);
        std::string meshName = MakeUniqueMeshName(scene, nodeBaseName);

        renderNode->m_Mesh     = subMesh;
        renderNode->m_SourceMesh = multiMesh;
        renderNode->m_SubmeshIndex = static_cast<uint32_t>(i);
        renderNode->m_Material = material;
        renderNode->m_ParentGroupKey = groupKey;
        renderNode->m_ParentEntityGuid = parentEntityGuid;
		renderNode->m_RayTracingEnabled =
			matType != VansMaterialType::VAN_TRANSPARENT &&
			matType != VansMaterialType::VAN_PBR_TRANSMISSION;

        if (hasNodeTransformAnimation)
        {
            glm::vec3 childPosition(0.0f);
            glm::vec3 childRotation(0.0f);
            glm::vec3 childScale(1.0f);
            const glm::mat4 rootWorld =
                Vans::VansTransformStore::Read(group.sharedTransformID).GetModelMatrix();
            if (DecomposeMatrixToPRS(rootWorld * subMesh->m_SourceNodeBindModelTransform,
                                     childPosition,
                                     childRotation,
                                     childScale))
            {
                renderNode->SetTransformData(childPosition, childRotation, childScale);
            }
            else
            {
                renderNode->SetTransformData(position, rotation, scale);
            }
        }
        else if (group.childNodes.empty())
        {
            // First child owns the transform; set position from JSON.
            renderNode->SetTransformData(position, rotation, scale);
            group.sharedTransformID = renderNode->m_TransformID;
        }
        else
        {
            renderNode->ShareTransform(group.sharedTransformID);
        }
        renderNode->SetName(renderNodeName);

        // Register the sub-mesh in the scene-level lookup list so it can be found by name.
        // 子网格对象由父级 multi-mesh 持有，不能混入项目级 m_Meshes 所有权列表。
        subMesh->SetName(meshName);
        scene.AddSceneSubMeshAsset(subMesh);
		result.registeredSubMeshes.push_back(subMesh);

        scene.RegistRenderNode(renderNode, nodeType);
        group.childNodes.push_back(renderNode);
		result.nodes.push_back(renderNode);

        // Shadow nodes are no longer created here — shadow passes now iterate
        // opaque nodes and use material->GetPassShader(VansPass::SHADOW).

		const char* nodeTypeName = nodeType == OPAQUE_NODE ? "OPAQUE"
			: (nodeType == HAIR_NODE ? "HAIR"
			: (nodeType == FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE ? "FORWARD_OPAQUE" : "TRANSPARENT"));
		VANS_LOG("[ExpandMultiMesh] Created render node: " << renderNodeName
				 << " (type=" << nodeTypeName << ")");
    }

	// ExpandMultiMesh 仅负责几何体到渲染节点的展开，动画由 animation component 创建。
	result.success = true;
	return result;
}

}

