#include "VansSceneProjectResourceBuilder.h"

#include "../VansGraphicsDevice.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../Util/VansLog.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../ProjectSystem/VansProjectManager.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
namespace
{
TexturePrecision ParseTexturePrecision(const std::string& value, TexturePrecision fallback)
{
    if (value == "low8" || value == "8" || value == "rgba8") return LOW_PRES_8;
    if (value == "mid16" || value == "16" || value == "rgba16") return MID_PRES_16;
    if (value == "high32" || value == "32" || value == "rgba32") return HIGH_PRES_32;
    if (value == "hdr16" || value == "float16" || value == "rgba16f") return HDR_PRES_16;
    return fallback;
}

VkSamplerAddressMode ParseSamplerAddressMode(const std::string& value, VkSamplerAddressMode fallback)
{
    if (value == "repeat") return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (value == "mirroredRepeat" || value == "mirror") return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    if (value == "clampToEdge" || value == "clamp") return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (value == "clampToBorder" || value == "border") return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    return fallback;
}

VansTexture::TextureLoadDesc BuildTextureLoadDesc(
    const std::string& path,
    bool isSRGB,
    bool useCompress = true,
    bool needMip = true,
    const std::string& precision = "low8",
    int importChannel = 4,
    const std::string& addressMode = "repeat",
    const std::string& cookedPath = {})
{
    VansTexture::TextureLoadDesc desc{};
    desc.path = path;
    desc.cookedPath = cookedPath;
    desc.isSRGB = isSRGB;
    desc.useCompress = useCompress;
    desc.needMip = needMip;
    desc.precision = ParseTexturePrecision(precision, LOW_PRES_8);
    desc.importChannel = importChannel;
    desc.addressMode = ParseSamplerAddressMode(addressMode, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    return desc;
}

void LoadTexture2DFromDesc(VansTexture& texture, VansVKDevice& device, const VansTexture::TextureLoadDesc& desc)
{
    texture.LoadTexture(device.GetCommandBuffer(), desc);
}

std::filesystem::path ResolvePathUnderPrefix(const std::string& pathPrefix, const std::string& path)
{
    std::filesystem::path fsPath(path);
    if (fsPath.is_absolute())
        return fsPath;
    return std::filesystem::path(pathPrefix) / fsPath;
}

VkShaderStageFlagBits ParseShaderStageName(const std::string& stageName)
{
    static const std::unordered_map<std::string, VkShaderStageFlagBits> stages = {
        { "vertex", VK_SHADER_STAGE_VERTEX_BIT },
        { "vert", VK_SHADER_STAGE_VERTEX_BIT },
        { "fragment", VK_SHADER_STAGE_FRAGMENT_BIT },
        { "frag", VK_SHADER_STAGE_FRAGMENT_BIT },
        { "compute", VK_SHADER_STAGE_COMPUTE_BIT },
        { "comp", VK_SHADER_STAGE_COMPUTE_BIT },
        { "geometry", VK_SHADER_STAGE_GEOMETRY_BIT },
        { "geom", VK_SHADER_STAGE_GEOMETRY_BIT },
        { "tessControl", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT },
        { "tesc", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT },
        { "tessEval", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT },
        { "tese", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT },
        { "rayGen", VK_SHADER_STAGE_RAYGEN_BIT_KHR },
        { "rgen", VK_SHADER_STAGE_RAYGEN_BIT_KHR },
        { "miss", VK_SHADER_STAGE_MISS_BIT_KHR },
        { "rmiss", VK_SHADER_STAGE_MISS_BIT_KHR },
        { "closestHit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
        { "rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
        { "anyHit", VK_SHADER_STAGE_ANY_HIT_BIT_KHR },
        { "rahit", VK_SHADER_STAGE_ANY_HIT_BIT_KHR },
        { "intersection", VK_SHADER_STAGE_INTERSECTION_BIT_KHR },
        { "rint", VK_SHADER_STAGE_INTERSECTION_BIT_KHR },
    };
    auto it = stages.find(stageName);
    return it != stages.end() ? it->second : static_cast<VkShaderStageFlagBits>(0);
}

VkCompareOp ParseCompareOp(const std::string& value, VkCompareOp fallback)
{
    static const std::unordered_map<std::string, VkCompareOp> ops = {
        { "never", VK_COMPARE_OP_NEVER },
        { "less", VK_COMPARE_OP_LESS },
        { "equal", VK_COMPARE_OP_EQUAL },
        { "lessOrEqual", VK_COMPARE_OP_LESS_OR_EQUAL },
        { "greater", VK_COMPARE_OP_GREATER },
        { "notEqual", VK_COMPARE_OP_NOT_EQUAL },
        { "greaterOrEqual", VK_COMPARE_OP_GREATER_OR_EQUAL },
        { "always", VK_COMPARE_OP_ALWAYS },
    };
    auto it = ops.find(value);
    return it != ops.end() ? it->second : fallback;
}

VkCullModeFlags ParseCullMode(const std::string& value, VkCullModeFlags fallback)
{
    if (value == "none") return VK_CULL_MODE_NONE;
    if (value == "front") return VK_CULL_MODE_FRONT_BIT;
    if (value == "back") return VK_CULL_MODE_BACK_BIT;
    if (value == "frontAndBack") return VK_CULL_MODE_FRONT_AND_BACK;
    return fallback;
}

VkPolygonMode ParsePolygonMode(const std::string& value, VkPolygonMode fallback)
{
    if (value == "fill") return VK_POLYGON_MODE_FILL;
    if (value == "line" || value == "wireframe") return VK_POLYGON_MODE_LINE;
    if (value == "point") return VK_POLYGON_MODE_POINT;
    return fallback;
}

VkFrontFace ParseFrontFace(const std::string& value, VkFrontFace fallback)
{
    if (value == "clockwise" || value == "cw") return VK_FRONT_FACE_CLOCKWISE;
    if (value == "counterClockwise" || value == "ccw") return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    return fallback;
}

VkPrimitiveTopology ParsePrimitiveTopology(const std::string& value, VkPrimitiveTopology fallback)
{
    static const std::unordered_map<std::string, VkPrimitiveTopology> topologies = {
        { "pointList", VK_PRIMITIVE_TOPOLOGY_POINT_LIST },
        { "lineList", VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        { "lineStrip", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP },
        { "triangleList", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
        { "triangleStrip", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP },
        { "triangleFan", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN },
        { "lineListWithAdjacency", VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY },
        { "lineStripWithAdjacency", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY },
        { "triangleListWithAdjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY },
        { "triangleStripWithAdjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY },
        { "patchList", VK_PRIMITIVE_TOPOLOGY_PATCH_LIST },
    };
    auto it = topologies.find(value);
    return it != topologies.end() ? it->second : fallback;
}

}

}

namespace VansGraphics
{
bool VansSceneProjectResourceBuilder::LoadMeshes(VansScene& scene,
    const std::vector<Vans::VansSceneMeshResourceRequest>& meshes,
    const Vans::VansSceneResourceLoadContext& loadContext,
    VkDevice& device,
    VansVKDevice* vkDevice)
{
	bool success = true;
    for (const auto& sceneMesh : meshes)
    {
        const Vans::VansResolvedSceneResourcePath resolved = loadContext.ResolveMesh(sceneMesh);
		if ((resolved.cookedOnly && !resolved.artifactAvailable) ||
			(!resolved.artifactAvailable && !resolved.sourceAvailable))
		{
			VANS_LOG_ERROR("[SceneResource] Mesh '" << sceneMesh.name
				<< "' cannot be resolved from asset index. guid=" << sceneMesh.assetGuid
				<< " source=" << resolved.sourcePath.string()
				<< " artifact=" << resolved.artifactPath.string());
			success = false;
			continue;
		}
        std::string meshPath = resolved.sourcePath.string();
        bool import_tangent = sceneMesh.needTangent;
        bool loadMultiMesh = sceneMesh.loadMultiMesh;
        float scaleFactor = sceneMesh.scaleFactor;

		if (loadMultiMesh)
		{
			bool generate_as = sceneMesh.supportRayTracing;
			bool needCpuData = sceneMesh.needCpuData;
			VansMesh* mesh   = new VansMesh(needCpuData, /*supportRayTracing=*/false);

            // 动画配置由 object.components.animation 统一处理。

            mesh->LoadMultiMesh(device, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath,
                import_tangent, generate_as, needCpuData, scaleFactor,
                sceneMesh.skeletalImport,
                resolved.artifactPath.string(),
                resolved.cookedOnly);
			if (mesh->m_SubMeshes.empty())
			{
				VANS_LOG_ERROR("[SceneResource] Multi-mesh load produced no renderable submeshes: "
					<< sceneMesh.assetGuid);
				delete mesh;
				success = false;
				continue;
			}
            mesh->SetName(sceneMesh.name);
            scene.AddMeshAsset(mesh);
        }

		else
		{
            bool generate_as = sceneMesh.supportRayTracing;
            bool needCpuData = sceneMesh.needCpuData;
            VansMesh* mesh   = new VansMesh(needCpuData, generate_as);
            mesh->LoadMesh(device, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath.c_str(), import_tangent, resolved.artifactPath.string(), resolved.cookedOnly);
			if (mesh->GetMeshVertexCount() == 0 || mesh->GetIndexCount() == 0)
			{
				VANS_LOG_ERROR("[SceneResource] Mesh load produced empty geometry: "
					<< sceneMesh.assetGuid);
				delete mesh;
				success = false;
				continue;
			}
            mesh->SetName(sceneMesh.name);
            scene.AddMeshAsset(mesh);
        }
    }
	return success;
}

void VansSceneProjectResourceBuilder::LoadShadersFromRegistry(VansScene& scene,
    const std::string& pathPrefix,
    VkDevice& device)
{
    auto& manager = VansGraphics::VansShaderManager::Get();

    // Load all registered shaders through the manager.
    // This correctly handles Graphics / Compute / RayTracing shader types
    // and populates the manager's internal records so that FindGraphicsShader /
    // FindComputeShader / FindRayTracingShader return valid pointers.
    manager.LoadAll(pathPrefix, device);

    // Populate VansScene shader assets for backward compatibility with
    // scene shader lookups used by material-pass binding.
    scene.SyncShaderAssetsFromShaderManager();
}

void VansSceneProjectResourceBuilder::RegisterShaders(VansScene& scene,
    const std::vector<Vans::VansSceneShaderResourceRequest>& shaders,
    const Vans::VansSceneResourceLoadContext& loadContext,
    VkDevice& device,
    bool loadRegisteredShaders)
{
    auto& manager = VansGraphics::VansShaderManager::Get();

    for (const Vans::VansSceneShaderResourceRequest& shaderRequest : shaders)
    {
        VansShaderEntry entry{};
        entry.name = shaderRequest.name;
        if (entry.name.empty())
        {
            VANS_LOG_WARN("[VansScene] Skipping shader asset without name");
            continue;
        }

		const Vans::VansResolvedSceneResourcePath resolved = loadContext.ResolveShader(shaderRequest);
		if (!resolved.valid)
		{
			VANS_LOG_ERROR("[VansScene] Shader asset '" << entry.name
				<< "' cannot be resolved from asset index: " << resolved.error);
			continue;
		}

		entry.relativePath = resolved.sourcePath.string();
        entry.kind = shaderRequest.kind == "compute"
            ? VansManagedShaderKind::Compute
            : (shaderRequest.kind == "rayTracing" || shaderRequest.kind == "raytracing"
                ? VansManagedShaderKind::RayTracing
                : VansManagedShaderKind::Graphics);
        entry.pushConstantSize = std::max(shaderRequest.pushConstantSize, 0);
        if (entry.kind == VansManagedShaderKind::Graphics && shaderRequest.pushConstantSize < 0)
            entry.pushConstantSize = sizeof(VansDrawPushConstant);
        entry.depthTest = shaderRequest.depthTest ? VK_TRUE : VK_FALSE;
        entry.depthWrite = shaderRequest.depthWrite ? VK_TRUE : VK_FALSE;
        entry.depthCompareOp = ParseCompareOp(shaderRequest.depthCompare, VK_COMPARE_OP_LESS_OR_EQUAL);
        entry.cullMode = ParseCullMode(shaderRequest.cull, VK_CULL_MODE_BACK_BIT);
        entry.enableAlphaBlend = shaderRequest.alphaBlend;
        entry.enableDecalBlend = shaderRequest.decalBlend;
        entry.enableAdditiveBlend = shaderRequest.additiveBlend;
        entry.additiveBlendAttachmentMask = shaderRequest.additiveBlendAttachmentMask;
        entry.enablePremultipliedAlphaBlend = shaderRequest.premultipliedAlphaBlend;
        entry.colorAttachmentCount = shaderRequest.colorAttachmentCount;
        if (entry.colorAttachmentCount < 0 && shaderRequest.renderPath == "deferredSurface")
            entry.colorAttachmentCount = 4;
        entry.polygonMode = ParsePolygonMode(shaderRequest.polygonMode, VK_POLYGON_MODE_FILL);
        entry.frontFace = ParseFrontFace(shaderRequest.frontFace, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        entry.primitiveTopology = ParsePrimitiveTopology(shaderRequest.primitiveTopology, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        entry.patchControlPoints = std::max(shaderRequest.patchControlPoints, 1u);
        entry.materialPasses = shaderRequest.materialPasses;

        for (const auto& [stageName, stageFile] : shaderRequest.stages)
        {
            VkShaderStageFlagBits stage = ParseShaderStageName(stageName);
            if (stage == 0)
            {
                VANS_LOG_WARN("[VansScene] Shader '" << entry.name
                    << "' has unknown stage '" << stageName << "'");
                continue;
            }
            entry.explicitStageFiles[stage] = stageFile;
        }

        manager.RegisterShader(std::move(entry));
    }

    if (loadRegisteredShaders)
        manager.LoadAll("", device);
    scene.SyncShaderAssetsFromShaderManager();
}

bool VansSceneProjectResourceBuilder::LoadTextures(VansScene& scene,
    const std::vector<Vans::VansSceneTextureResourceRequest>& textures,
    const Vans::VansSceneResourceLoadContext& loadContext,
    VansVKDevice* vkDevice,
    bool includeDefaultTextureSet)
{
    struct PendingCookedTexture
    {
        VansTexture* texture = nullptr;
        std::vector<std::uint8_t> storage;
        VansTextureMipChainUpload upload{};
    };
    std::vector<std::unique_ptr<PendingCookedTexture>> pendingCookedTextures;
    pendingCookedTextures.reserve(textures.size());
    std::size_t cookedBatchPreparedCount = 0;
    std::size_t sourceFallbackCount = 0;

	for (const auto& sceneTexture : textures)
	{
		const Vans::VansResolvedSceneResourcePath resolved = loadContext.ResolveTexture(sceneTexture);
		if (!resolved.valid)
		{
			VANS_LOG_ERROR("[SceneResource] Texture '" << sceneTexture.name
				<< "' cannot be resolved from asset index: " << resolved.error);
			return false;
		}
        std::string texturePath = resolved.sourcePath.string();
        VansTexture* texture    = new VansTexture();
        texture->m_TextureType  = static_cast<TextureType>(sceneTexture.textureType);
        bool isSRGB             = sceneTexture.srgb;
        switch (texture->m_TextureType)
        {
        case TEXTURE_2D:
        {
            const VansTexture::TextureLoadDesc desc = BuildTextureLoadDesc(
                texturePath,
                isSRGB,
                sceneTexture.useCompress,
                sceneTexture.needMip,
                sceneTexture.precision,
                sceneTexture.importChannel,
                sceneTexture.addressMode,
                resolved.artifactPath.string());
            auto pending = std::make_unique<PendingCookedTexture>();
            pending->texture = texture;
			if (resolved.artifactAvailable &&
				texture->TryPrepareCookedBatchUpload(*vkDevice, desc, pending->upload, pending->storage))
            {
                texture->SetName(sceneTexture.name);
                pendingCookedTextures.push_back(std::move(pending));
                ++cookedBatchPreparedCount;
                continue;
            }
            LoadTexture2DFromDesc(*texture, *vkDevice, desc);
            ++sourceFallbackCount;
            break;
        }
        case TEXTURE_CUBE:
            texture->LoadCubeTexture(vkDevice->GetCommandBuffer(), texturePath, isSRGB);
            break;
        default:
            break;
        }
        texture->SetName(sceneTexture.name);
        scene.AddTextureAsset(texture);
    }

    if (!pendingCookedTextures.empty())
    {
        std::vector<VansTextureMipChainUpload> uploads;
        uploads.reserve(pendingCookedTextures.size());
        for (const auto& pending : pendingCookedTextures)
        {
            pending->upload.data = pending->storage.data();
            uploads.push_back(pending->upload);
        }

        if (!vkDevice->SubmitTextureMipChainUploadBatch(vkDevice->GetCommandBuffer(), uploads))
        {
            VANS_LOG_ERROR("[TextureBatchUpload] Batch upload failed; prepared textures may be incomplete");
        }

        for (const auto& pending : pendingCookedTextures)
            scene.AddTextureAsset(pending->texture);
    }

    VANS_LOG("[TextureBatchUpload] Prepared " << cookedBatchPreparedCount
        << " cooked 2D textures, fallback=" << sourceFallbackCount);

	if (includeDefaultTextureSet)
    {
        // Default textures are always loaded from the engine's EngineAssets directory.
        VansSceneProjectResourceBuilder::ImportDefaultTexture(scene, loadContext.ResolveEnginePath("EngineAssets/Textures/Default/defaultAlbedo.png").string(),    "defaultAlbedo",    vkDevice, false);
        VansSceneProjectResourceBuilder::ImportDefaultTexture(scene, loadContext.ResolveEnginePath("EngineAssets/Textures/Default/defaultMetal.png").string(),     "defaultMetal",     vkDevice, false);
        VansSceneProjectResourceBuilder::ImportDefaultTexture(scene, loadContext.ResolveEnginePath("EngineAssets/Textures/Default/defaultRoughness.png").string(), "defaultRoughness", vkDevice, false);
        VansSceneProjectResourceBuilder::ImportDefaultTexture(scene, loadContext.ResolveEnginePath("EngineAssets/Textures/Default/defaultAo.png").string(),        "defaultAo",        vkDevice, false);
		VansSceneProjectResourceBuilder::ImportDefaultTexture(scene, loadContext.ResolveEnginePath("EngineAssets/Textures/Default/defaultNormal.png").string(),    "defaultNormal",    vkDevice, false);
	}
	return true;
}

void VansSceneProjectResourceBuilder::ImportDefaultTexture(VansScene& scene, const std::string& path, const std::string& name, VansVKDevice* vkDevice, bool isSRGB)
{
    VansTexture* defaultMetalTexture = new VansTexture();
    defaultMetalTexture->m_TextureType = TEXTURE_2D;
    std::uint8_t pixel[4] = { 255, 255, 255, 255 };
    if (name == "defaultMetal")
    {
        pixel[0] = 0;
        pixel[1] = 0;
        pixel[2] = 0;
    }
    else if (name == "defaultNormal")
    {
        pixel[0] = 128;
        pixel[1] = 128;
        pixel[2] = 255;
    }
    defaultMetalTexture->LoadFromMemory(
        vkDevice->GetCommandBuffer(),
        pixel,
        sizeof(pixel),
        1,
        1,
        isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);
    defaultMetalTexture->SetName(name);
    scene.AddTextureAsset(defaultMetalTexture);
}

VansTexture* VansSceneProjectResourceBuilder::LoadOrGetTexture(VansScene& scene, const std::string& absPath, bool isSRGB)
{
    if (absPath.empty())
        return nullptr;

    // Derive a unique name from the file path
    std::string texName = std::filesystem::path(absPath).stem().string();

    // Check if already loaded
    VansTexture* existing = static_cast<VansTexture*>(scene.GetTextureAsset(texName));
    if (existing)
        return existing;

    // Check if the file exists on disk
    if (!std::filesystem::exists(absPath))
    {
        VANS_LOG_WARN("[LoadOrGetTexture] Texture file not found: " << absPath);
        return nullptr;
    }

    VansVKDevice* vkDevice = scene.GetRuntimeResourceDevice();

    if (!vkDevice)
    {
        VANS_LOG_WARN("[LoadOrGetTexture] Cannot load texture without runtime Vulkan device: " << absPath);
        return nullptr;
    }

    VansTexture* texture = new VansTexture();
    texture->m_TextureType = TEXTURE_2D;
    std::string cookedPath;
	std::string sourcePath = absPath;
	if (const auto record = Vans::VansProjectManager::Get().FindAssetRecordByPath(std::filesystem::path(absPath)))
	{
		cookedPath = record->artifactPath.string();
		if (!record->sourcePath.empty())
			sourcePath = record->sourcePath.string();
	}
	LoadTexture2DFromDesc(*texture, *vkDevice,
		BuildTextureLoadDesc(sourcePath, isSRGB, true, true, "low8", 4, "repeat", cookedPath));
    texture->SetName(texName);
    scene.AddTextureAsset(texture);

    VANS_LOG("[LoadOrGetTexture] Loaded texture: " << texName << " from " << absPath);
    return texture;
}

void VansSceneProjectResourceBuilder::LoadShaderFromEntry(VansScene& scene,
    const VansGraphics::VansShaderEntry& entry,
    const std::string& pathPrefix,
    VkDevice& device)
{
    if (scene.FindShaderAsset(entry.name) != nullptr)
        return; // already loaded

    std::string fullPath = pathPrefix + entry.relativePath;
    VansGraphicsShader* shader = new VansGraphicsShader();
    shader->InitShader(device, fullPath);
    VansShaderManager::Get().ConfigureGraphicsShader(*shader, entry, fullPath);
    scene.AddShaderAsset(shader);
}

}
