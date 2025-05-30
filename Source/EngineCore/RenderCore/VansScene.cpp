#include "VansScene.h"
#include "BRDFData/VansLight.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include <iostream>
#include <fstream>


VansAsset* VansGraphics::VansScene::GetMeshAsset(const std::string& name)
{
    //搜索对应的mesh
    for (auto mesh : m_Meshes)
    {
        if (mesh->m_AssetName== name)
        {
			return mesh;
		}
	}
    return nullptr;
}

VansAsset* VansGraphics::VansScene::GetShaderAsset(const std::string& name)
{
    //搜索对应shader
    for (auto shader : m_Shaders)
    {
        if (shader->m_AssetName == name)
        {
			return shader;
		}
	}
    return nullptr;
}

VansAsset* VansGraphics::VansScene::GetTextureAsset(const std::string& name)
{
    //搜索texture
    for (auto texture : m_Textures)
    {
        if (texture->m_AssetName == name)
        {
            return texture;
        }
    }

    return nullptr;
}

VansAsset* VansGraphics::VansScene::GetMaterialAsset(const std::string& name)
{
    //搜索material
    for (auto material : m_Materials)
    {
        if (material->m_AssetName == name)
        {
			return material;
		}
	}
    return nullptr;
}

void VansGraphics::VansScene::RegistRenderNode(VansRenderNode* renderNode, RenderNodeType type)
{
    //将rendernode记录到对应类型的vector中
    switch (type)
    {
	case SKY_BOX_NODE:
		m_SkyBoxNode = renderNode;
		break;
	case OPAQUE_NODE:
		m_OpaqueRenderNodes.push_back(renderNode);
		break;
    case TRANSPARENT_NODE:
		m_TransParentRenderNodes.push_back(renderNode);
		break;
    case POSTPROCESS_NODE:
		m_PostProcessRenderNodes.push_back(renderNode);
		break;
	default:
		break;
    }
}

bool VansGraphics::VansScene::LoadScene(const char* path)
{
    std::string pathPrefix = "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/";
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice->GetLogicDevice();

    //解析json
    std::ifstream jsonFile(path);
    json sceneData = json::parse(jsonFile);


    //搜索所有资源进行创建
    json sceneMeshes = sceneData["mesh"];
    json sceneShaders = sceneData["shader"];
    json sceneTextures = sceneData["texture"];
    json sceneMaterials = sceneData["material"];


    //加载并import资产，记录到scene中
    for (const auto& sceneMesh : sceneMeshes) 
    {
        std::string meshPath = pathPrefix+ std::string(sceneMesh["path"]);
        VansMesh* mesh = new VansMesh();
        mesh->LoadMesh(nativeDevice, meshPath.c_str());
        m_Meshes.push_back(mesh);
        mesh->SetName(sceneMesh["name"]);
    }

    //加载并import资产，记录到scene中
    for (const auto& sceneShader : sceneShaders)
    {
        std::string shaderPath = pathPrefix + std::string(sceneShader["path"]);
        VkBool32 depthTest = sceneShader["depthTest"];
        VkBool32 depthWrite = sceneShader["depthWrite"];
        VkCompareOp depthCompareOp = sceneShader["depthCompareOp"];
        VkCullModeFlags cullMode = sceneShader["cullMode"];

        VansGraphicsShader* shader = new VansGraphicsShader();
        shader->InitShader(nativeDevice, shaderPath);
        shader->SetDrawStateData(depthTest, depthWrite, depthCompareOp, cullMode);
        m_Shaders.push_back(shader);
        shader->SetName(sceneShader["name"]);
    }

    //加载并import资产，记录到scene中
    for (const auto& sceneTexture : sceneTextures)
    {
        std::string texturePath = pathPrefix + std::string(sceneTexture["path"]);
        VansTexture* texture = new VansTexture();
        texture->m_TextureType = sceneTexture["type"];
        switch (texture->m_TextureType)
        {
        case TEXTURE_2D:
            texture->LoadTexture(vkDevice->GetCommandBuffer(), texturePath);
            break;
        case TEXTURE_CUBE:
            texture->LoadCubeTexture(vkDevice->GetCommandBuffer(), texturePath);
			break;
        default:
            break;
        }
        
        m_Textures.push_back(texture);
        texture->SetName(sceneTexture["name"]);
    }

    for (const auto& sceneMaterial : sceneMaterials)
    {
        VansMaterial* material = new VansMaterial();

        std::string shaderName = sceneMaterial["shader"];
        VansGraphicsShader* shader =static_cast<VansGraphicsShader*>(GetShaderAsset(shaderName));
        
        material->m_Shader = shader;
        material->m_MaterialType = sceneMaterial["type"];
        if (sceneMaterial.contains("texture"))
        {
            for (const auto& textureName : sceneMaterial["texture"])
            {
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                material->m_Texture.push_back(texture);
            }
        }
        //创建材质参数GPU数据
        if (material->m_MaterialType == VansMaterialType::VAN_PBR)
        {
            material->m_BasePBRParam.m_albedo = glm::vec3(sceneMaterial["albedo"][0], sceneMaterial["albedo"][1], sceneMaterial["albedo"][2]);
            material->m_BasePBRParam.m_metallic = sceneMaterial["metallic"];
            material->m_BasePBRParam.m_roughness = sceneMaterial["roughness"];
            material->m_BasePBRParam.m_ao = sceneMaterial["ao"];
            material->CreatePBRMaterialDataBuffer(nativeDevice);
        }
        
        //大气材质
        if (material->m_MaterialType == VansMaterialType::VAN_SKY_BOX)
        {
            material->m_AtmospherePBRParam.m_PlanetRadius = 6340000;
            material->m_AtmospherePBRParam.m_InitSeaLevel = 200;
            material->m_AtmospherePBRParam.m_AtmosphereWidth = 80000;
            material->m_AtmospherePBRParam.m_RayleighScalarHeight = 8500;
            material->m_AtmospherePBRParam.m_MieScalarHeight = 1200;
            material->m_AtmospherePBRParam.m_MieAnisotropy = 0.78;
            material->m_AtmospherePBRParam.m_OzoneLevelCenterHeight = 25000;
            material->m_AtmospherePBRParam.m_OzoneLevelWidth = 15000;
            material->m_AtmospherePBRParam.m_SunLuminance = 10;
            material->CreateAtmosphereMaterialDataBuffer(nativeDevice);
        }
        m_Materials.push_back(material);
        material->SetName(sceneMaterial["name"]);
    }


    //找到scene 节点，包含rendernode， camera，light数据
    json sceneNode = sceneData["scene"];


    //loadLightsData
    LoadLights(nativeDevice, sceneNode[0]["light"]);

    //根据引用关系创建render node
    LoadRenderNodes(nativeDevice, sceneNode[0]["rendernode"]);

    return true;
}

void VansGraphics::VansScene::LoadLights(VkDevice& device, json& light_node)
{
    for (const auto& light : light_node)
    {
        VansLightType type = light["type"];
        if (type == VansLightType::DIRECTIONAL)
        {
            VansDirectionalLight dirLight;
            dirLight.m_Direction = glm::vec3(light["direction"][0], light ["direction"][1], light["direction"][2]);
            dirLight.m_Direction = -glm::normalize(dirLight.m_Direction);
			dirLight.m_Color = glm::vec3(light["color"][0], light["color"][1], light["color"][2]);
            dirLight.m_Intensity = light["intensity"];
            m_LightManager.AddDirectionalLight(dirLight);
		}
        else if (type == VansLightType::POINT)
        {
            VansPointLight pointLight;
			pointLight.m_Position = glm::vec3(light["position"][0], light["position"][1], light["position"][2]);
			pointLight.m_Color = glm::vec3(light["color"][0], light["color"][1], light["color"][2]);
            pointLight.m_Intensity = light["intensity"];
			pointLight.m_Radius = light["radius"];
            m_LightManager.AddPointLight(pointLight);
		}
        else if (type == VansLightType::SPOT)
        {
            VansSpotLight spotLight;
			spotLight.m_Position = glm::vec3(light["position"][0], light["position"][1], light["position"][2]);
			spotLight.m_Direction = glm::vec3(light["direction"][0], light["direction"][1], light["direction"][2]);
            spotLight.m_Direction = -glm::normalize(spotLight.m_Direction);
            spotLight.m_Color = glm::vec3(light["color"][0], light["color"][1], light["color"][2]);
            spotLight.m_Intensity = light["intensity"];
            spotLight.m_InnerCutOff = light["innercutoff"];
			spotLight.m_OuterCutOff = light["outerCutoff"];
            spotLight.m_Radius = light["radius"];
            m_LightManager.AddSpotLight(spotLight);
		}
    }

    m_LightManager.CreateLightUniformData(device);
}

void VansGraphics::VansScene::LoadRenderNodes(VkDevice& device, json& render_node)
{
    for (const auto& sceneRenderNode : render_node)
    {
        std::string meshName = sceneRenderNode["mesh"];
        VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));
        std::string materialName = sceneRenderNode["material"];
        VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));

        RenderNodeType type = sceneRenderNode["type"];
        VansRenderNode* renderNode = new VansRenderNode(device, type);

        renderNode->m_Mesh = mesh;
        renderNode->m_Material = material;
        //绑定相机cb
        renderNode->RegistCameraDescriptor(m_Camera);

        renderNode->CreateDescriptorSets();

        //绑定灯光cb
        renderNode->RegistLightDescriptor(m_LightManager);
        //绑定材质cb
        renderNode->RegistMaterialDescriptor(m_MaterialManager);
        renderNode->SetName(sceneRenderNode["name"]);

        RegistRenderNode(renderNode, type);
    }
}

void VansGraphics::VansScene::UnLoadScene()
{
    //delete mesh;
    //delete shader;
    //delete fullScreenMesh;
    //delete fullScreenShader;
    //delete m_Texture;
}

void VansGraphics::VansScene::UpdateSceneData()
{
    m_LightManager.UpdateLightCPUData();
}

void VansGraphics::VansScene::DrawSkyBoxNode()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    //更新desc
    m_SkyBoxNode->UpdateRenderData(vkDevice, m_MaterialManager, m_LightManager, m_Camera);

    m_SkyBoxNode->Draw(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawOpaqueNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_OpaqueRenderNodes)
    {
        //更新desc
        node->UpdateRenderData(vkDevice, m_MaterialManager, m_LightManager, m_Camera);

        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawTransParentNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_TransParentRenderNodes)
    {

        ////apply mesh
        //cmd.BindMesh(*node.m_Mesh, 0, globalStateData);

        ////apply shader，确认pipeline以及创建完毕
        //cmd.BindShader(*(node.m_Material->m_Shader), globalStateData, { uniformBufferLayout,textureResourceLayout });

        //cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(node.m_Material->m_Shader), 0, { uniformBufferDescriptorSets[0],textureResourceDescriptorSets[0] }, {});

        //cmd.DrawMesh(*node.m_Mesh, *(node.m_Material->m_Shader), 1);
    }
}

void VansGraphics::VansScene::DrawPostProcessNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node  : m_PostProcessRenderNodes)
    {
        node->UpdateRenderData(vkDevice, m_MaterialManager, m_LightManager, m_Camera);

        //apply mesh
        node->Draw(cmd, globalStateData);
    }
}

VansGraphics::VansScene* m_Scene = nullptr;