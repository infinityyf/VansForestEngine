#include "VansSceneMaterialBuilder.h"

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../Util/VansLog.h"

#include <algorithm>

namespace VansGraphics
{
	namespace
	{
		const json& UnwrapMaterialValue(const json& value)
		{
			if (value.is_object())
			{
				const auto valueIt = value.find("value");
				if (valueIt != value.end())
					return *valueIt;

				const auto defaultIt = value.find("default");
				if (defaultIt != value.end())
					return *defaultIt;
			}
			return value;
		}

		const json* FindMaterialField(const json& object, const char* key)
		{
			if (!object.is_object())
				return nullptr;

			const auto direct = object.find(key);
			if (direct != object.end())
				return &(*direct);

			const auto params = object.find("parameters");
			if (params != object.end() && params->is_object())
			{
				const auto parameter = params->find(key);
				if (parameter != params->end())
					return &(*parameter);
			}

			return nullptr;
		}

		float ReadMaterialFloatField(const json& object, const char* key, float fallback)
		{
			const json* found = FindMaterialField(object, key);
			if (!found)
				return fallback;
			const json& raw = UnwrapMaterialValue(*found);
			return raw.is_number() ? raw.get<float>() : fallback;
		}

		std::string ReadMaterialStringField(const json& object, const char* key, const std::string& fallback)
		{
			const json* found = FindMaterialField(object, key);
			if (!found)
				return fallback;
			const json& raw = UnwrapMaterialValue(*found);
			return raw.is_string() ? raw.get<std::string>() : fallback;
		}

		glm::vec3 ReadMaterialVec3Field(const json& object, const char* key, const glm::vec3& fallback)
		{
			const json* found = FindMaterialField(object, key);
			if (!found)
				return fallback;
			const json& raw = UnwrapMaterialValue(*found);
			if (!raw.is_array() || raw.size() < 3 ||
				!raw[0].is_number() || !raw[1].is_number() || !raw[2].is_number())
				return fallback;
			return glm::vec3(raw[0].get<float>(), raw[1].get<float>(), raw[2].get<float>());
		}
	}

	VansMaterialType VansSceneMaterialBuilder::ParseMaterialType(
		const json& typeValue,
		const std::string& materialName)
	{
		if (typeValue.is_string())
		{
			const std::string type = typeValue.get<std::string>();
			if (type == "pbr") return VansMaterialType::VAN_PBR;
			if (type == "tree") return VansMaterialType::VAN_PBR;
			if (type == "coat") return VansMaterialType::VAN_COAT;
			if (type == "transparent") return VansMaterialType::VAN_TRANSPARENT;
			if (type == "glass" || type == "transmission" || type == "pbr_transmission") return VansMaterialType::VAN_PBR_TRANSMISSION;
			if (type == "post_process") return VansMaterialType::VAN_POST_PROCESS;
			if (type == "sky_box") return VansMaterialType::VAN_SKY_BOX;
			if (type == "deferred") return VansMaterialType::VAN_DEFERRED;
			if (type == "ssao") return VansMaterialType::VAN_SCREEN_SPACE_AO;
			if (type == "skin") return VansMaterialType::VAN_SKIN;
			if (type == "cloth") return VansMaterialType::VAN_CLOTH;
			if (type == "hair") return VansMaterialType::VAN_HAIR;
			if (type == "subsurface") return VansMaterialType::VAN_SUBSURFACE;
			if (type == "grass") return VansMaterialType::VAN_GRASS;
			if (type == "emissive") return VansMaterialType::VAN_EMISSIVE;
			if (type == "decal") return VansMaterialType::VAN_DECAL;
			if (type == "customShader" || type == "custom") return VansMaterialType::VAN_CUSTOM_SHADER;
			VANS_LOG_WARN("[ParseMaterialType] Material '" << materialName
				<< "': unknown type string '" << type << "', defaulting to pbr.");
		}
		return VansMaterialType::VAN_PBR;
	}

	VansMaterial* VansSceneMaterialBuilder::CreateMaterialForType(VansMaterialType materialType)
	{
		switch (materialType)
		{
		case VansMaterialType::VAN_PBR:
		case VansMaterialType::VAN_COAT: return new VansPBRMaterial();
		case VansMaterialType::VAN_TRANSPARENT: return new VansTransparentMaterial();
		case VansMaterialType::VAN_PBR_TRANSMISSION: return new VansTransmissionMaterial();
		case VansMaterialType::VAN_POST_PROCESS: return new VansPostProcessMaterial();
		case VansMaterialType::VAN_SKY_BOX: return new VansSkyBoxMaterial();
		case VansMaterialType::VAN_DEFERRED: return new VansDeferredMaterial();
		case VansMaterialType::VAN_SCREEN_SPACE_AO: return new VansSSAOMaterial();
		case VansMaterialType::VAN_SKIN: return new VansSkinMaterial();
		case VansMaterialType::VAN_CLOTH: return new VansClothMaterial();
		case VansMaterialType::VAN_HAIR: return new VansHairMaterial();
		case VansMaterialType::VAN_SUBSURFACE: return new VansSubsurfaceMaterial();
		case VansMaterialType::VAN_GRASS: return new VansGrassMaterial();
		case VansMaterialType::VAN_EMISSIVE: return new VansEmissiveMaterial();
		case VansMaterialType::VAN_DECAL: return new VansDecalMaterial();
		case VansMaterialType::VAN_CUSTOM_SHADER: return new VansMaterial();
		default: return new VansMaterial();
		}
	}

	void VansSceneMaterialBuilder::PopulateMaterialPassShaders(
		VansScene& scene,
		VansMaterial* material,
		VansMaterialType materialType)
	{
		if (!material)
			return;

		auto& shaderManager = VansShaderManager::Get();
		const auto& passMap = shaderManager.GetMaterialPassMap(materialType);
		for (const auto& [passName, shaderName] : passMap)
		{
			VansGraphicsShader* passShader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset(shaderName));
			if (passShader)
				material->m_PassShaders[passName] = passShader;
		}
	}

	void VansSceneMaterialBuilder::ApplyMaterialShaderOverrides(VansScene& scene, VansMaterial* material)
	{
		if (!material)
			return;

		const auto bindAutomaticCustomPass = [&](const std::string& shaderName,
			VansGraphicsShader* shader)
		{
			if (material->m_MaterialType != VansMaterialType::VAN_CUSTOM_SHADER || !shader)
				return;

			const VansShaderEntry* entry = VansShaderManager::Get().FindShaderEntry(shaderName);
			material->m_CustomShaderDepthWrite = entry == nullptr || entry->depthWrite == VK_TRUE;
			const bool declaresGBuffer = entry &&
				std::find(entry->materialPasses.begin(), entry->materialPasses.end(), VansPass::GBUFFER) !=
				entry->materialPasses.end();
			if (declaresGBuffer)
			{
				material->m_PassShaders[VansPass::GBUFFER] = shader;
				VANS_LOG("[LoadMaterials] Custom material '" << material->m_AssetName
					<< "' routed to '" << VansPass::GBUFFER << "' from declared shader pass");
				return;
			}
			const char* automaticPass = material->m_CustomShaderDepthWrite
				? VansPass::FORWARD_OPAQUE_AFTER_DEFERRED
				: VansPass::FORWARD_TRANSPARENT;
			material->m_PassShaders[automaticPass] = shader;

			VANS_LOG("[LoadMaterials] Custom material '" << material->m_AssetName
				<< "' automatically routed to '" << automaticPass
				<< "' from shader depthWrite=" << (material->m_CustomShaderDepthWrite ? "true" : "false"));
		};

		for (const auto& [passName, shaderName] : material->m_PassShaderOverrides)
		{
			if (passName.empty() || shaderName.empty())
				continue;

			if (passName == "*")
			{
				if (material->m_MaterialType != VansMaterialType::VAN_CUSTOM_SHADER)
				{
					VANS_LOG_WARN("[LoadMaterials] Material '" << material->m_AssetName
						<< "' ignored wildcard shader override on non-custom material.");
					continue;
				}

				const VansShaderEntry* entry = VansShaderManager::Get().FindShaderEntry(shaderName);
				VansGraphicsShader* declaredPassShader =
					static_cast<VansGraphicsShader*>(scene.FindShaderAsset(shaderName));
				const std::vector<std::string> passes = entry && !entry->materialPasses.empty()
					? entry->materialPasses
					: std::vector<std::string>{ VansPass::GBUFFER };
				for (const std::string& declaredPass : passes)
				{
					if (declaredPassShader)
						material->m_PassShaders[declaredPass] = declaredPassShader;
					else
					{
						VANS_LOG_WARN("[LoadMaterials] Material '" << material->m_AssetName
							<< "' shader for pass '" << declaredPass
							<< "' not found: " << shaderName << ". Keeping default shader.");
					}
				}
				bindAutomaticCustomPass(shaderName, declaredPassShader);
				continue;
			}

			VansGraphicsShader* passShader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset(shaderName));
			if (!passShader)
			{
				VANS_LOG_WARN("[LoadMaterials] Material '" << material->m_AssetName
					<< "' shader override for pass '" << passName
					<< "' not found: " << shaderName << ". Keeping default shader.");
				continue;
			}
			material->m_PassShaders[passName] = passShader;
			bindAutomaticCustomPass(shaderName, passShader);
		}

	}

VansTexture* VansSceneMaterialBuilder::ResolveMaterialTexture(VansScene& scene, const json& sceneMaterial, const char* key)
{
    if (sceneMaterial.contains(key) && sceneMaterial[key].is_string())
    {
        const std::string textureName = sceneMaterial[key].get<std::string>();
        VansTexture* texture = static_cast<VansTexture*>(scene.GetTextureAsset(textureName));
        if (!texture)
        {
            Vans::VansAssetGuid textureGuid;
            if (Vans::VansAssetGuid::TryParse(textureName, textureGuid))
            {
                auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
                const auto record = database ? database->Find(textureGuid) : std::nullopt;
                if (record && record->type == Vans::VansAssetType::Texture &&
                    record->state != Vans::VansAssetState::Missing)
                {
                    const std::string keyName = key ? std::string(key) : std::string{};
                    const bool isSRGB = keyName == "basecolor_texture" ||
                        keyName == "diffuse_texture" ||
                        keyName == "albedo_texture";
                    texture = scene.FindOrLoadTexture(record->sourcePath.string(), isSRGB);
                }
            }
        }
        if (std::string(key) == "basecolor_texture" ||
            sceneMaterial.value("generatedFor", std::string{}) == "runtimeMultiMeshExpansion")
        {
            VANS_LOG("[LoadMaterials] Resolve texture material="
                << sceneMaterial.value("name", std::string{"<unnamed>"})
                << " key=" << key
                << " request=" << textureName
                << " result=" << (texture ? texture->m_AssetName : std::string{"<missing>"}));
        }
        return texture;
    }
    return nullptr;
}

VansTexture* VansSceneMaterialBuilder::ResolveMaterialTextureWithFallback(
    VansScene& scene,
    const json& sceneMaterial,
    const char* key,
    const char* fallback)
{
    return scene.ResolveTextureAssetOrDefault(ResolveMaterialTexture(scene, sceneMaterial, key), fallback);
}

VansTexture* VansSceneMaterialBuilder::ResolveMaterialTextureOrDefault(
    VansScene& scene,
    const json& sceneMaterial,
    const char* key,
    const char* fallback)
{
    return scene.ResolveTextureAssetOrDefault(ResolveMaterialTexture(scene, sceneMaterial, key), fallback);
}

namespace
{
glm::vec4 ReadCustomMaterialVec4(const json& value)
{
    if (value.is_number())
        return glm::vec4(value.get<float>(), 0.0f, 0.0f, 0.0f);
    if (value.is_array())
    {
        glm::vec4 result(0.0f);
        const std::size_t count = std::min<std::size_t>(value.size(), 4);
        for (std::size_t i = 0; i < count; ++i)
            if (value[i].is_number())
                result[static_cast<int>(i)] = value[i].get<float>();
        return result;
    }
    if (value.is_object())
    {
        if (value.contains("value"))
            return ReadCustomMaterialVec4(value["value"]);
        if (value.contains("default"))
            return ReadCustomMaterialVec4(value["default"]);
    }
    return glm::vec4(0.0f);
}

void PopulateCustomMaterialData(VansGraphics::VansScene& scene, VansGraphics::VansMaterial* material, const json& sceneMaterial)
{
    if (!material)
        return;

    material->m_CustomParameterSlots.clear();
    material->m_CustomTextureSlots.clear();
    material->m_CustomTextures.clear();
    material->m_CustomMaterialPayload = VansGraphics::VansCustomMaterialPayload{};

    if (sceneMaterial.contains("customParameters") && sceneMaterial["customParameters"].is_object())
    {
        int nextSlot = 0;
        for (const auto& [name, parameter] : sceneMaterial["customParameters"].items())
        {
            int slot = nextSlot;
            if (parameter.is_object() && parameter.contains("slot") && parameter["slot"].is_number_integer())
                slot = parameter["slot"].get<int>();
            if (slot >= VansGraphics::VANS_CUSTOM_MATERIAL_VEC4_COUNT)
            {
                VANS_LOG_WARN("[LoadMaterials] Custom material parameter limit reached; skipping '" << name << "'");
                continue;
            }
            if (slot < 0)
                continue;
            material->m_CustomParameterSlots[name] = slot;
            material->m_CustomMaterialPayload.values[slot] = ReadCustomMaterialVec4(parameter);
            nextSlot = std::max(nextSlot, slot + 1);
        }
    }

    if (sceneMaterial.contains("customTextures") && sceneMaterial["customTextures"].is_object())
    {
        int nextSlot = 0;
        for (const auto& [name, textureNameJson] : sceneMaterial["customTextures"].items())
        {
            int slot = nextSlot;
            const json* textureReference = &textureNameJson;
            if (textureNameJson.is_object())
            {
                if (textureNameJson.contains("slot") && textureNameJson["slot"].is_number_integer())
                    slot = textureNameJson["slot"].get<int>();
                if (textureNameJson.contains("value"))
                    textureReference = &textureNameJson["value"];
            }
            if (slot >= VansGraphics::VANS_CUSTOM_MATERIAL_TEXTURE_COUNT)
            {
                VANS_LOG_WARN("[LoadMaterials] Custom material texture limit reached; skipping '" << name << "'");
                continue;
            }
            if (slot < 0 || !textureReference->is_string())
                continue;
            const std::string textureName = textureReference->get<std::string>();
            VansGraphics::VansTexture* texture = static_cast<VansGraphics::VansTexture*>(scene.GetTextureAsset(textureName));
            if (!texture)
            {
                VANS_LOG_WARN("[LoadMaterials] Custom texture '" << name << "' not found: " << textureName);
                continue;
            }
            material->m_CustomTextureSlots[name] = slot;
            material->m_CustomTextures[name] = texture;
            nextSlot = std::max(nextSlot, slot + 1);
        }
    }
}
}

void VansSceneMaterialBuilder::PopulateMaterialFromJson(
    VansScene& scene,
    VansMaterial* material,
    VansMaterialType matType,
    const json& sceneMaterial)
{
    if (!material)
        return;

    if (matType == VansMaterialType::VAN_CUSTOM_SHADER)
        PopulateCustomMaterialData(scene, material, sceneMaterial);

    if (sceneMaterial.contains("shader"))
    {
        const json& shaderOverride = sceneMaterial["shader"];
        if (shaderOverride.is_string())
        {
            if (matType == VansMaterialType::VAN_CUSTOM_SHADER)
            {
                material->m_PassShaderOverrides["*"] = shaderOverride.get<std::string>();
            }
            else
            {
                VANS_LOG_WARN("[LoadMaterials] Material '" << material->m_AssetName
                    << "' ignored string shader override on non-custom material. Use shaderPasses for explicit pass overrides.");
            }
        }
        else if (shaderOverride.is_object())
        {
            for (const auto& [passName, shaderName] : shaderOverride.items())
            {
                if (shaderName.is_string())
                    material->m_PassShaderOverrides[passName] = shaderName.get<std::string>();
            }
        }
    }

    if (sceneMaterial.contains("shaderPasses") && sceneMaterial["shaderPasses"].is_object())
    {
        for (const auto& [passName, shaderName] : sceneMaterial["shaderPasses"].items())
        {
            if (shaderName.is_string())
                material->m_PassShaderOverrides[passName] = shaderName.get<std::string>();
        }
    }

    switch (matType)
    {
    case VansMaterialType::VAN_PBR:
    {
        auto* pbr = static_cast<VansPBRMaterial*>(material);
        pbr->m_BaseColorTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        pbr->m_NormalTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "normal_texture", "defaultNormal");
        pbr->m_MetalTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "metal_texture", "defaultMetal");
        pbr->m_RoughnessTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        pbr->m_AoTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "ao_texture", "defaultAo");
        glm::vec3 pbrColor = ReadMaterialVec3Field(sceneMaterial, "color", glm::vec3(1.0f));
        pbrColor = ReadMaterialVec3Field(sceneMaterial, "basecolor", pbrColor);
        pbrColor = ReadMaterialVec3Field(sceneMaterial, "baseColor", pbrColor);
        pbr->m_BasePBRParam.m_albedo = ReadMaterialVec3Field(sceneMaterial, "albedo", pbrColor);
        pbr->m_BasePBRParam.m_metallic = ReadMaterialFloatField(sceneMaterial, "metallic", 0.0f);
        pbr->m_BasePBRParam.m_roughness = ReadMaterialFloatField(sceneMaterial, "roughness", 0.5f);
        pbr->m_BasePBRParam.m_ao = ReadMaterialFloatField(sceneMaterial, "ao", 1.0f);
        break;
    }
    case VansMaterialType::VAN_CLOTH:
    {
        auto* cloth = static_cast<VansClothMaterial*>(material);
        cloth->m_BaseColorTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        cloth->m_NormalTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "normal_texture", "defaultNormal");
        cloth->m_RoughnessTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        cloth->m_AoTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "ao_texture", "defaultAo");
        cloth->m_SheenRoughness = std::clamp(ReadMaterialFloatField(sceneMaterial, "sheenRoughness", 0.5f), 0.045f, 1.0f);
        glm::vec3 clothColor = ReadMaterialVec3Field(sceneMaterial, "color", glm::vec3(1.0f));
        clothColor = ReadMaterialVec3Field(sceneMaterial, "basecolor", clothColor);
        clothColor = ReadMaterialVec3Field(sceneMaterial, "baseColor", clothColor);
        cloth->m_BasePBRParam.m_albedo = ReadMaterialVec3Field(sceneMaterial, "albedo", clothColor);
        cloth->m_BasePBRParam.m_roughness = cloth->m_SheenRoughness;
        cloth->m_SheenStrength = std::clamp(ReadMaterialFloatField(sceneMaterial, "sheenStrength", 0.5f), 0.0f, 1.0f);
        cloth->m_Translucency = std::clamp(ReadMaterialFloatField(sceneMaterial, "translucency", 0.35f), 0.0f, 1.0f);
        cloth->m_Anisotropy = std::clamp(ReadMaterialFloatField(sceneMaterial, "anisotropy", 0.0f), -0.95f, 0.95f);
        cloth->m_Thickness = std::clamp(ReadMaterialFloatField(sceneMaterial, "thickness", 1.0f), 0.0f, 1.0f);
        cloth->m_TransmissionColor = glm::max(
            ReadMaterialVec3Field(sceneMaterial, "transmissionColor", glm::vec3(1.0f)), glm::vec3(0.0f));

        if (FindMaterialField(sceneMaterial, "sheenColor"))
        {
            cloth->m_SheenColor = glm::max(
                ReadMaterialVec3Field(sceneMaterial, "sheenColor", glm::vec3(1.0f)), glm::vec3(0.0f));
            cloth->m_ClothFlags &= ~VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT;
        }
        else
        {
            cloth->m_SheenColor = glm::vec3(1.0f);
            cloth->m_ClothFlags |= VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT;
        }

        const std::string clothModel = ReadMaterialStringField(sceneMaterial, "clothModel", "fuzz");
        if (clothModel == "silk" || clothModel == "satin")
            cloth->m_ClothModel = VansClothModel::Silk;
        else if (clothModel == "thin")
            cloth->m_ClothModel = VansClothModel::Thin;
        else
        {
            cloth->m_ClothModel = VansClothModel::Fuzz;
            if (clothModel != "fuzz" && clothModel != "cotton" && clothModel != "wool" && clothModel != "velvet")
            {
                VANS_LOG_WARN("[LoadMaterials] Cloth material '" << material->m_AssetName
                    << "': unknown clothModel '" << clothModel << "', falling back to fuzz.");
            }
        }

        cloth->m_BasePBRParam.m_metallic = cloth->m_Translucency;
        cloth->m_BasePBRParam.m_ao = ReadMaterialFloatField(sceneMaterial, "ao", 1.0f);
        cloth->m_BasePBRParam.padding = cloth->m_SheenStrength;
        break;
    }
    case VansMaterialType::VAN_SKIN:
    {
        auto* skin = static_cast<VansSkinMaterial*>(material);
        skin->m_BaseColorTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        skin->m_NormalTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "normal_texture", "defaultNormal");
        break;
    }
    case VansMaterialType::VAN_HAIR:
    {
        auto* hair = static_cast<VansHairMaterial*>(material);
        hair->m_AlbedoTexture = ResolveMaterialTexture(scene, sceneMaterial, "albedo_texture");
		if (hair->m_AlbedoTexture == nullptr)
			hair->m_AlbedoTexture = ResolveMaterialTexture(scene, sceneMaterial, "basecolor_texture");
		hair->m_AlbedoTexture = scene.ResolveTextureAssetOrDefault(hair->m_AlbedoTexture, "defaultAlbedo");
        hair->m_AlphaTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "alpha_texture", "defaultAlbedo");
        hair->m_NormalTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "normal_texture", "defaultNormal");
        hair->m_RoughnessTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        hair->m_AOTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "ao_texture", "defaultAo");
        hair->m_ShiftTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "shift_texture", "defaultRoughness");
        hair->m_FlowTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "flow_texture", "defaultAlbedo");
        hair->m_IDTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "id_texture", "defaultAlbedo");
		if (sceneMaterial.contains("params") && sceneMaterial["params"].is_object())
		{
			const auto& params = sceneMaterial["params"];
			auto readVec4 = [](const json& obj, const char* key, glm::vec4 fallback) {
				if (!obj.contains(key) || !obj[key].is_array() || obj[key].size() < 4)
					return fallback;
				return glm::vec4(obj[key][0], obj[key][1], obj[key][2], obj[key][3]);
			};
			hair->m_Params.absorption = readVec4(params, "absorption", hair->m_Params.absorption);
			hair->m_Params.roughnessScale = readVec4(params, "roughness_scale", hair->m_Params.roughnessScale);
			hair->m_Params.shiftParams = readVec4(params, "shift_params", hair->m_Params.shiftParams);
			hair->m_Params.coverageParams = readVec4(params, "coverage_params", hair->m_Params.coverageParams);
		}
        break;
    }
    case VansMaterialType::VAN_SUBSURFACE:
    {
        auto* sss = static_cast<VansSubsurfaceMaterial*>(material);
        sss->m_BaseColorTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        sss->m_NormalTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "normal_texture", "defaultNormal");
        sss->m_ThicknessTexture = ResolveMaterialTexture(scene, sceneMaterial, "thickness_texture");
        sss->m_RoughnessTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        sss->m_SubsurfacePower = ReadMaterialFloatField(sceneMaterial, "subsurfacePower", 12.234f);
        sss->m_Thickness = ReadMaterialFloatField(sceneMaterial, "thickness", 0.5f);
        sss->m_SubsurfaceAmount = ReadMaterialFloatField(sceneMaterial, "subsurfaceAmount", 1.0f);
        sss->m_CurvatureInfluence = ReadMaterialFloatField(sceneMaterial, "curvatureInfluence", 0.35f);
        glm::vec3 sssColor = ReadMaterialVec3Field(sceneMaterial, "albedo", sss->m_SubsurfaceColor);
        sssColor = ReadMaterialVec3Field(sceneMaterial, "basecolor", sssColor);
        sssColor = ReadMaterialVec3Field(sceneMaterial, "baseColor", sssColor);
        sssColor = ReadMaterialVec3Field(sceneMaterial, "color", sssColor);
        sss->m_SubsurfaceColor = ReadMaterialVec3Field(sceneMaterial, "subsurfaceColor", sssColor);
        sss->m_BasePBRParam.m_albedo = sss->m_SubsurfaceColor;
        sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
        sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
        sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
        sss->m_BasePBRParam.padding = sss->m_CurvatureInfluence;
        break;
    }
    case VansMaterialType::VAN_TRANSPARENT:
    {
        auto* trans = static_cast<VansTransparentMaterial*>(material);
        if (sceneMaterial.contains("textures") && sceneMaterial["textures"].is_array())
        {
            for (const auto& entry : sceneMaterial["textures"])
            {
                std::string slotName = entry.value("slot", "");
                std::string textureName = entry.value("texture", "");
                VansTexture* tex = nullptr;
                if (!textureName.empty())
                {
                    tex = static_cast<VansTexture*>(scene.GetTextureAsset(textureName));
                    if (!tex)
                    {
                        Vans::VansAssetGuid textureGuid;
                        if (Vans::VansAssetGuid::TryParse(textureName, textureGuid))
                        {
                            auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
                            const auto record = database ? database->Find(textureGuid) : std::nullopt;
                            if (record && record->type == Vans::VansAssetType::Texture &&
                                record->state != Vans::VansAssetState::Missing)
                            {
                                const bool isSRGB = slotName == "diffuse" || slotName == "basecolor" || slotName == "baseColor";
                                tex = scene.FindOrLoadTexture(record->sourcePath.string(), isSRGB);
                            }
                        }
                    }
                }
                if (tex == nullptr)
                    VANS_LOG_WARN("[LoadMaterials] Transparent material '" << sceneMaterial.value("name", "<unnamed>") << "': could not resolve texture for slot '" << slotName << "'");
                trans->m_TransparentTextureMap.push_back({ slotName, textureName });
                trans->m_TransparentTextures.push_back(tex);
            }
        }
        break;
    }
    case VansMaterialType::VAN_PBR_TRANSMISSION:
    {
        auto* glass = static_cast<VansTransmissionMaterial*>(material);
        glass->m_BaseColorTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        glass->m_NormalTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "normal_texture", "defaultNormal");
        glass->m_RoughnessTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        glass->m_ThicknessTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "thickness_texture", "defaultAo");
        glass->m_ReflectionTexture = ResolveMaterialTexture(scene, sceneMaterial, "reflection_texture");
        if (glass->m_ReflectionTexture == nullptr)
            glass->m_ReflectionTexture = ResolveMaterialTexture(scene, sceneMaterial, "reflectionTexture");
        glass->m_ReflectionTexture = scene.ResolveTextureAssetOrDefault(glass->m_ReflectionTexture, "defaultAo");

        glm::vec3 baseColor = ReadMaterialVec3Field(sceneMaterial, "color", glm::vec3(1.0f));
        baseColor = ReadMaterialVec3Field(sceneMaterial, "basecolor", baseColor);
        baseColor = ReadMaterialVec3Field(sceneMaterial, "baseColor", baseColor);
        baseColor = ReadMaterialVec3Field(sceneMaterial, "albedo", baseColor);

        const float alphaCoverage = ReadMaterialFloatField(sceneMaterial, "alphaCoverage", 1.0f);
        const float roughness = ReadMaterialFloatField(sceneMaterial, "roughness", 0.02f);
        const float transmission = ReadMaterialFloatField(sceneMaterial, "transmission", 1.0f);
        const float ior = ReadMaterialFloatField(sceneMaterial, "ior", 1.5f);
        const float thickness = ReadMaterialFloatField(sceneMaterial, "thickness", 0.01f);
        glm::vec3 attenuationColor = ReadMaterialVec3Field(sceneMaterial, "attenuationColor", glm::vec3(1.0f));
        const float attenuationDistance = ReadMaterialFloatField(sceneMaterial, "attenuationDistance", 0.0f);
        const float normalScale = ReadMaterialFloatField(sceneMaterial, "normalScale", 1.0f);
        const float refractionStrength = std::clamp(ReadMaterialFloatField(sceneMaterial, "refractionStrength", 1.0f), 0.0f, 1.0f);
        const float reflectionStrength = std::clamp(ReadMaterialFloatField(sceneMaterial, "reflectionStrength", 1.0f), 0.0f, 1.0f);
        const float refractionMode = std::clamp(ReadMaterialFloatField(sceneMaterial, "refractionMode", 1.0f), 0.0f, 2.0f);
        const glm::vec3 scatteringColor = ReadMaterialVec3Field(sceneMaterial, "scatteringColor", glm::vec3(1.0f));
        const float scatteringStrength = std::max(ReadMaterialFloatField(sceneMaterial, "scatteringStrength", 0.0f), 0.0f);

        glass->m_CustomMaterialPayload = VansCustomMaterialPayload{};
        glass->m_CustomMaterialPayload.values[0] = glm::vec4(baseColor, alphaCoverage);
        glass->m_CustomMaterialPayload.values[1] = glm::vec4(roughness, transmission, ior, thickness);
        glass->m_CustomMaterialPayload.values[2] = glm::vec4(attenuationColor, attenuationDistance);
        glass->m_CustomMaterialPayload.values[3] = glm::vec4(normalScale, refractionStrength, reflectionStrength, refractionMode);
        glass->m_CustomMaterialPayload.values[4] = glm::vec4(scatteringColor, scatteringStrength);
        // Slot 4 is encoded into the first unused scalar so the shared custom-material
        // SSBO keeps its existing ABI while glass gains a reflection-mask texture.
        glass->m_CustomMaterialPayload.values[5].x = -1.0f;

        glass->m_CustomTextureSlots["baseColor"] = 0;
        glass->m_CustomTextureSlots["normal"] = 1;
        glass->m_CustomTextureSlots["roughness"] = 2;
        glass->m_CustomTextureSlots["thickness"] = 3;
        glass->m_CustomTextureSlots["reflection"] = 4;
        glass->m_CustomTextures["baseColor"] = glass->m_BaseColorTexture;
        glass->m_CustomTextures["normal"] = glass->m_NormalTexture;
        glass->m_CustomTextures["roughness"] = glass->m_RoughnessTexture;
        glass->m_CustomTextures["thickness"] = glass->m_ThicknessTexture;
        glass->m_CustomTextures["reflection"] = glass->m_ReflectionTexture;
        break;
    }
    case VansMaterialType::VAN_GRASS:
    {
        auto* grass = static_cast<VansGrassMaterial*>(material);
        grass->m_AlbedoTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        grass->m_NormalTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "normal_texture", "defaultNormal");
        grass->m_RoughnessTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        grass->m_TranslucencyTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "translucency_texture", "defaultAo");
        grass->m_AOTexture = ResolveMaterialTextureWithFallback(scene, sceneMaterial, "ao_texture", "defaultAo");
        break;
    }
    case VansMaterialType::VAN_EMISSIVE:
    {
        auto* emissive = static_cast<VansEmissiveMaterial*>(material);
        glm::vec3 emissiveColor = ReadMaterialVec3Field(sceneMaterial, "albedo", glm::vec3(1.0f));
        emissiveColor = ReadMaterialVec3Field(sceneMaterial, "color", emissiveColor);
        emissiveColor = ReadMaterialVec3Field(sceneMaterial, "emissive", emissiveColor);
        emissive->m_BasePBRParam.m_albedo = ReadMaterialVec3Field(sceneMaterial, "emissive_color", emissiveColor);
        const float emissiveIntensity = ReadMaterialFloatField(sceneMaterial, "intensity", 1.0f);
        emissive->m_BasePBRParam.m_roughness = ReadMaterialFloatField(sceneMaterial, "emissive_intensity", emissiveIntensity);
        emissive->m_BasePBRParam.m_metallic = 0.0f;
        emissive->m_BasePBRParam.m_ao = 1.0f;
        emissive->m_EmissiveTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "emissive_texture", "defaultAlbedo");

        if (sceneMaterial.contains("emissive_video"))
        {
            const std::string videoName = sceneMaterial["emissive_video"];
            VansVideoTexture* videoTex = scene.GetVideoManager()->Get(videoName);
            if (videoTex && videoTex->IsReady())
            {
                emissive->m_EmissiveTexture = videoTex->GetTexture();
                emissive->m_VideoName = videoName;
                VANS_LOG("[VansScene] Emissive 材质绑定视频纹理: " << videoName);
            }
            else
            {
                VANS_LOG_WARN("[VansScene] emissive_video 未找到或未就绪: " << videoName);
            }
        }
        break;
    }
    case VansMaterialType::VAN_DECAL:
    {
        auto* decal = static_cast<VansDecalMaterial*>(material);
        glm::vec3 decalColor = ReadMaterialVec3Field(sceneMaterial, "color", glm::vec3(1.0f));
        decalColor = ReadMaterialVec3Field(sceneMaterial, "basecolor", decalColor);
        decalColor = ReadMaterialVec3Field(sceneMaterial, "baseColor", decalColor);
        decal->m_BasePBRParam.m_albedo = ReadMaterialVec3Field(sceneMaterial, "albedo", decalColor);
        decal->m_BasePBRParam.m_metallic = ReadMaterialFloatField(sceneMaterial, "metallic", 0.0f);
        decal->m_BasePBRParam.m_roughness = ReadMaterialFloatField(sceneMaterial, "roughness", 0.5f);
        decal->m_BasePBRParam.m_ao = ReadMaterialFloatField(sceneMaterial, "ao", 1.0f);
        decal->m_BaseColorTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "basecolor_texture", "defaultAlbedo");
        decal->m_NormalTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "normal_texture", "defaultNormal");
        decal->m_MetalTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "metal_texture", "defaultMetal");
        decal->m_RoughnessTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "roughness_texture", "defaultRoughness");
        decal->m_AoTexture = ResolveMaterialTextureOrDefault(scene, sceneMaterial, "ao_texture", "defaultAo");
        break;
    }
    case VansMaterialType::VAN_SKY_BOX:
    {
        auto* sky = static_cast<VansSkyBoxMaterial*>(material);
        sky->m_AtmospherePBRParam.m_PlanetRadius = 6340000;
        sky->m_AtmospherePBRParam.m_InitSeaLevel = 200;
        sky->m_AtmospherePBRParam.m_AtmosphereWidth = 80000;
        sky->m_AtmospherePBRParam.m_RayleighScalarHeight = 8500;
        sky->m_AtmospherePBRParam.m_MieScalarHeight = 1200;
        sky->m_AtmospherePBRParam.m_MieAnisotropy = 0.78f;
        sky->m_AtmospherePBRParam.m_OzoneLevelCenterHeight = 25000;
        sky->m_AtmospherePBRParam.m_OzoneLevelWidth = 15000;
        sky->m_AtmospherePBRParam.m_SunLuminance = 10;
        if (sceneMaterial.contains("celestial") && sceneMaterial["celestial"].is_object())
        {
            const auto& celestial = sceneMaterial["celestial"];
            if (celestial.contains("sun") && celestial["sun"].is_object())
            {
                const auto& sun = celestial["sun"];
                sky->m_SunDiskEnabled = sun.value("enabled", sky->m_SunDiskEnabled);
                sky->m_SunDiskAngularRadius = sun.value("angularRadius", sky->m_SunDiskAngularRadius);
                sky->m_SunDiskFeather = sun.value("feather", sky->m_SunDiskFeather);
                sky->m_SunDiskRadianceScale = sun.value("radianceScale", sky->m_SunDiskRadianceScale);
                sky->m_SunDiskOcclusionStrength = sun.value("occlusionStrength", sky->m_SunDiskOcclusionStrength);
            }
            if (celestial.contains("moon") && celestial["moon"].is_object())
            {
                const auto& moon = celestial["moon"];
                sky->m_MoonDiskEnabled = moon.value("enabled", sky->m_MoonDiskEnabled);
                sky->m_MoonDiskAngularRadius = moon.value("angularRadius", sky->m_MoonDiskAngularRadius);
                sky->m_MoonDiskFeather = moon.value("feather", sky->m_MoonDiskFeather);
                sky->m_MoonDiskRadianceScale = moon.value("radianceScale", sky->m_MoonDiskRadianceScale);
                sky->m_MoonDiskOcclusionStrength = moon.value("occlusionStrength", sky->m_MoonDiskOcclusionStrength);
            }
        }
        break;
    }
    default:
        break;
    }
}

	void VansSceneMaterialBuilder::LoadMaterialsFromJson(VansScene& scene, const json& materialData)
	{
		for (const auto& sceneMaterial : materialData)
		{
			VansMaterialType materialType = ParseMaterialType(
				sceneMaterial["type"],
				sceneMaterial.value("name", "<unnamed>"));
			VansMaterial* material = CreateMaterialForType(materialType);
			material->m_MaterialType = materialType;
			PopulateMaterialPassShaders(scene, material, materialType);
			PopulateMaterialFromJson(scene, material, materialType, sceneMaterial);
			material->SetName(sceneMaterial["name"]);
			ApplyMaterialShaderOverrides(scene, material);
			scene.AddMaterialAsset(material);
		}
	}
}
