#include "VansRenderNode.h"
#include "VansCamera.h"
#include "VansScene.h"
#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansRenderPass.h"
#include <iostream>
using namespace VansGraphics;

VansGraphics::VansRenderNode::VansRenderNode(VkDevice& device, RenderNodeType typee)
{
	m_NodeType = typee;

	m_RenderNodeDataBuffer.CreatVulkanBuffer(device, sizeof(ModelDataStruct), VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	//初始化 transform数据
	SetTransformData();

	//用于标记是否需要更新描述符
	m_DescriptorsetsDirty = true;
}

VansGraphics::VansRenderNode::~VansRenderNode()
{
	DestroyDescriptorSets();

	//m_RenderNodeDataBuffer.DestroyVulkanBuffer();
}

void VansGraphics::VansRenderNode::RegistCameraDescriptor(VansCamera* camera)
{
	m_UsedDescSetLayouts.push_back(camera->m_CameraBufferLayout);
	m_UsedDescSets.push_back(camera->m_CameraBufferDescriptorSets[0]);
}


void VansGraphics::VansRenderNode::RegistLightDescriptor(VansLightManager& lightManager)
{
	m_UsedDescSetLayouts.push_back(lightManager.m_LightDataDescriptorSetLayout);
	m_UsedDescSets.push_back(lightManager.m_LightDataDescriptorSets[0]);
}

bool VansGraphics::VansRenderNode::CheckRenderNodeState()
{
	auto shader = m_Material->m_Shader;
	if (shader!= nullptr && m_SceneFileWatcher->ConsumeUpdated(shader->GetShaderFolder()))
	{
		std::cout << "pipe update: " << shader->GetShaderFolder() << std::endl;
		//重新构建pipeline
		shader->RefreshShaderMoudle();
		shader->TriggerReCreateGraphicsPipeline();
		return false;
	}

	return true;
}

void VansGraphics::VansRenderNode::DestroyDescriptorSets()
{
	//销毁描述符poo
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(modelBufferLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(modelBufferDescriptorSets);

	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(textureResourceDescriptorSets);

	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(frameBufferInputLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(frameBufferInputDescriptorSets);
}

void VansGraphics::VansRenderNode::BeforeDrawCall()
{
	//更新CPU
	m_ModelData.ModelMatrix = glm::translate(glm::mat4x4(1.0f), m_Transform.m_Position);

	glm::vec3 radians = glm::radians(m_Transform.m_Rotation);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), radians.x, glm::vec3(1, 0, 0));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), radians.y, glm::vec3(0, 1, 0));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), radians.z, glm::vec3(0, 0, 1));

	// XYZ order: first X, then Y, then Z
	glm::mat4 rotationMatrix = rotZ * rotY * rotX;
	m_ModelData.ModelMatrix = m_ModelData.ModelMatrix * rotationMatrix;

	m_ModelData.ModelMatrix = glm::scale(m_ModelData.ModelMatrix, m_Transform.m_Scale);

	m_ModelData.Postion = m_Transform.m_Position;
	m_ModelData.Scale = m_Transform.m_Scale;

	//更新模型数据到PUG
	m_RenderNodeDataBuffer.SetBufferData(&m_ModelData,0, sizeof(m_ModelData));
}

void VansGraphics::VansRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
	{
		return;
	}

	BeforeDrawCall();

	//apply mesh
	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	//apply shader，确认pipeline以及创建完毕
	cmd.EnsureGraphicsShader(*(m_Material->m_Shader), globalStateData, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(m_Material->m_Shader), 0, m_UsedDescSets, {});

	cmd.DrawMesh(*m_Mesh, *(m_Material->m_Shader), 1);
}

void VansGraphics::VansRenderNode::DrawPunctualShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state, int lightIndex, int shadowIndex)
{
	BeforeDrawCall();

	//apply mesh
	cmd.BindMesh(*m_Mesh, 0, global_state);

	//apply shader，确认pipeline以及创建完毕
	cmd.EnsureGraphicsShader(*(m_Material->m_Shader), global_state, m_UsedDescSetLayouts);

	int data[2] = { lightIndex, shadowIndex };
	cmd.UpdatePushConstants(*(m_Material->m_Shader->GetGraphicsPipeline()), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, m_Material->m_Shader->GetPushConstantSize(), data);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(m_Material->m_Shader), 0, m_UsedDescSets, {});

	cmd.DrawMesh(*m_Mesh, *(m_Material->m_Shader), 1);
}

void VansGraphics::VansRenderNode::DrawWithMaterial(VansMaterial* material, VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	BeforeDrawCall();

	//apply mesh
	cmd.BindMesh(*m_Mesh, 0, global_state);

	cmd.EnsureGraphicsShader(*(material->m_Shader), global_state, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(material->m_Shader), 0, m_UsedDescSets, {});

	cmd.DrawMesh(*m_Mesh, *(material->m_Shader), 1);
}

void VansGraphics::VansCommonRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	RegistCameraDescriptor(camera);

	//创建uniform buffer以及对应的描述符,可以同时包含多个类型的desc
	VkDescriptorSetLayoutBinding modelBufferBinding =
	{
		VansVKDescriptorManager::m_ModelBufferSetBinding,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ modelBufferBinding }, modelBufferLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ modelBufferLayout }, modelBufferDescriptorSets);

	//创建资源给fs采样
	VkDescriptorSetLayoutBinding baseColorSamplerBinding =
	{
		VansVKDescriptorManager::m_SampleTexture0SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding normalSamplerBinding =
	{
		VansVKDescriptorManager::m_SampleTexture1SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding metalSamplerBinding =
	{
		VansVKDescriptorManager::m_SampleTexture2SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding roughnessSamplerBinding =
	{
		VansVKDescriptorManager::m_SampleTexture3SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding aoSamplerBinding =
	{
		VansVKDescriptorManager::m_SampleTexture4SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ baseColorSamplerBinding,normalSamplerBinding, metalSamplerBinding,roughnessSamplerBinding,aoSamplerBinding }, textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ textureResourceLayout }, textureResourceDescriptorSets);


	m_UsedDescSetLayouts.push_back(modelBufferLayout);
	m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);
	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);

	RegistLightDescriptor(lightManager);

	//PBR参数 
	VkDescriptorSetLayoutBinding basePBRDataBinding =
	{
		VansVKDescriptorManager::m_MaterialBufferSetBinding,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ basePBRDataBinding }, m_MaterialPBRBaseDataLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_MaterialPBRBaseDataLayout }, m_MaterialPBRBaseDataDescriptorSets);
	m_UsedDescSetLayouts.push_back(m_MaterialPBRBaseDataLayout);
	m_UsedDescSets.push_back(m_MaterialPBRBaseDataDescriptorSets[0]);

	//预卷积 _ lut
	m_UsedDescSetLayouts.push_back(materialManager.m_BRDFInterationTexSetLayout);
	m_UsedDescSets.push_back(materialManager.m_BRDFInterationTextDescriptorSets[0]);
}

void VansGraphics::VansCommonRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	//更新pbr参数
	m_Material->UpdatePBRUniformData();

	//更新描述符集
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansCommonRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	//更新描述符
	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			modelBufferDescriptorSets[0],
			VansVKDescriptorManager::m_ModelBufferSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_RenderNodeDataBuffer.GetNativeBuffer(),
					0,
					m_RenderNodeDataBuffer.GetBufferSize()
				}
			}
		}
	);

	VansVKImage baseColorImage = m_Material->m_BaseColorTexture->GetImage();
	VansVKImage normalImage = m_Material->m_NormalTexture->GetImage();
	VansVKImage metalImage = m_Material->m_MetalTexture->GetImage();
	VansVKImage roughnessImage = m_Material->m_RoughnessTexture->GetImage();
	VansVKImage aoImage = m_Material->m_AoTexture->GetImage();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					baseColorImage.GetSampler(),
					baseColorImage.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					normalImage.GetSampler(),
					normalImage.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					metalImage.GetSampler(),
					metalImage.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture3SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					roughnessImage.GetSampler(),
					roughnessImage.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture4SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					aoImage.GetSampler(),
					aoImage.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			m_MaterialPBRBaseDataDescriptorSets[0],
			VansVKDescriptorManager::m_MaterialBufferSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_Material->GetPBRDataBuffer().GetNativeBuffer(),
					0,
					m_Material->GetPBRDataBuffer().GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansPostProcessRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	//后处理
	VkDescriptorSetLayoutBinding inputAttachmentBinding =
	{
		VansVKDescriptorManager::m_InputAttachment0SetBinding,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ inputAttachmentBinding }, frameBufferInputLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ frameBufferInputLayout }, frameBufferInputDescriptorSets);

	m_UsedDescSetLayouts.push_back(frameBufferInputLayout);
	m_UsedDescSets.push_back(frameBufferInputDescriptorSets[0]);
}

void VansGraphics::VansPostProcessRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansPostProcessRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			VansVKDescriptorManager::m_InputAttachment0SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetColor().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	//只需要变化时更新，有的时候会绑定其他资源，所以需要update
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansDeferredRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	RegistCameraDescriptor(camera);

	//延迟管线
	VkDescriptorSetLayoutBinding inputAttachment0Binding =
	{
		VansVKDescriptorManager::m_InputAttachment0SetBinding,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding inputAttachment1Binding =
	{
		VansVKDescriptorManager::m_InputAttachment1SetBinding,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding inputAttachment2Binding =
	{
		VansVKDescriptorManager::m_InputAttachment2SetBinding,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding inputAttachment3Binding =
	{
		VansVKDescriptorManager::m_InputAttachment3SetBinding,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding inputAttachment4Binding =
	{
		VansVKDescriptorManager::m_InputAttachment4SetBinding,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
		{
			inputAttachment0Binding,
			inputAttachment1Binding,
			inputAttachment2Binding,
			inputAttachment3Binding,
			inputAttachment4Binding
		},
		frameBufferInputLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ frameBufferInputLayout }, frameBufferInputDescriptorSets);

	m_UsedDescSetLayouts.push_back(frameBufferInputLayout);
	m_UsedDescSets.push_back(frameBufferInputDescriptorSets[0]);

	//非gbuffer输入
	VkDescriptorSetLayoutBinding ssaoInput =
	{
		VansVKDescriptorManager::m_UAVTextureSetBinding,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VkDescriptorSetLayoutBinding ssgiInput =
	{
		VansVKDescriptorManager::m_UAVTexture0SetBinding,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VkDescriptorSetLayoutBinding ssrInput =
	{
		VansVKDescriptorManager::m_UAVTexture1SetBinding,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	//shadow map
	VkDescriptorSetLayoutBinding mainShadowMapInput =
	{
		VansVKDescriptorManager::m_SampleTexture3SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding punctualShadowMapInput =
	{
		VansVKDescriptorManager::m_SampleTexture4SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	//GI texture
	VkDescriptorSetLayoutBinding SHRCoeffTexture =
	{
		VansVKDescriptorManager::m_SampleTexture5SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VkDescriptorSetLayoutBinding SHGCoeffTexture =
	{
		VansVKDescriptorManager::m_SampleTexture6SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VkDescriptorSetLayoutBinding SHBCoeffTexture =
	{
		VansVKDescriptorManager::m_SampleTexture7SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VkDescriptorSetLayoutBinding FogTexture =
	{
		VansVKDescriptorManager::m_SampleTexture8SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
		{
			ssaoInput,
			ssgiInput,
			ssrInput,
			mainShadowMapInput,
			punctualShadowMapInput,
			SHRCoeffTexture,
			SHGCoeffTexture,
			SHBCoeffTexture,
			FogTexture
		},
		textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ textureResourceLayout }, textureResourceDescriptorSets);
	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);


	RegistLightDescriptor(lightManager);

	//预卷积 _ lut
	m_UsedDescSetLayouts.push_back(materialManager.m_BRDFInterationTexSetLayout);
	m_UsedDescSets.push_back(materialManager.m_BRDFInterationTextDescriptorSets[0]);
}

void VansGraphics::VansDeferredRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansDeferredRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			VansVKDescriptorManager::m_InputAttachment0SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetNormal().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			VansVKDescriptorManager::m_InputAttachment1SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetGbuffer0().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			VansVKDescriptorManager::m_InputAttachment2SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetGbuffer1().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			VansVKDescriptorManager::m_InputAttachment3SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetGbuffer2().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			VansVKDescriptorManager::m_InputAttachment4SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetDepth().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_UAVTextureSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{
				{
					materialManager.m_SSAOFilterResult->GetImage().GetSampler(),
					materialManager.m_SSAOFilterResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_UAVTexture0SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{
				{
					materialManager.m_SSGIResult->GetImage().GetSampler(),
					materialManager.m_SSGIResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_UAVTexture1SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{
				{
					materialManager.m_SSRAAResult->GetImage().GetSampler(),
					materialManager.m_SSRAAResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture3SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetShadowMap().GetSampler(),
					VansRenderPassManager::GetInstance()->GetShadowMap().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture4SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetSampler(),
					VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	//设置GI数据
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture5SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_SHRResult->GetImage().GetSampler(),
					materialManager.m_SHRResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture6SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_SHGResult->GetImage().GetSampler(),
					materialManager.m_SHGResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture7SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_SHBResult->GetImage().GetSampler(),
					materialManager.m_SHBResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	//设置雾效
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture8SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_VolumetricFogResult->GetImage().GetSampler(),
					materialManager.m_VolumetricFogResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	//只需要变化时更新，有的时候会绑定其他资源，所以需要update
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansScreenSpaceRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	RegistCameraDescriptor(camera);

	VkDescriptorSetLayoutBinding textureInput0 =
	{
		VansVKDescriptorManager::m_SampleTexture0SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding textureInput1 =
	{
		VansVKDescriptorManager::m_SampleTexture1SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding textureInput2 =
	{
		VansVKDescriptorManager::m_SampleTexture2SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding textureInput3 =
	{
		VansVKDescriptorManager::m_SampleTexture3SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VkDescriptorSetLayoutBinding textureInput4 =
	{
		VansVKDescriptorManager::m_SampleTexture4SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};

	VkDescriptorSetLayoutBinding uavOutputBinding0 =
	{
		VansVKDescriptorManager::m_UAVTexture4SetBinding,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
		{
			textureInput0,
			textureInput1,
			textureInput2,
			textureInput3,
			textureInput4,
			uavOutputBinding0
		},
		textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ textureResourceLayout }, textureResourceDescriptorSets);

	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);
}

void VansGraphics::VansScreenSpaceRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansScreenSpaceRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetNormal().GetSampler(),
					VansRenderPassManager::GetInstance()->GetNormal().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetGbuffer0().GetSampler(),
					VansRenderPassManager::GetInstance()->GetGbuffer0().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetGbuffer1().GetSampler(),
					VansRenderPassManager::GetInstance()->GetGbuffer1().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture3SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetGbuffer2().GetSampler(),
					VansRenderPassManager::GetInstance()->GetGbuffer2().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture4SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetDepth().GetSampler(),
					VansRenderPassManager::GetInstance()->GetDepth().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			VansVKDescriptorManager::m_UAVTexture4SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{
				{
					materialManager.m_SSAOResult->GetImage().GetSampler(),
					materialManager.m_SSAOResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);
	//只需要变化时更新，有的时候会绑定其他资源，所以需要update
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansSkyBoxRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	RegistCameraDescriptor(camera);

	m_UsedDescSetLayouts.push_back(materialManager.m_MaterialAtmosphereDataLayout);
	m_UsedDescSets.push_back(materialManager.m_MaterialAtmosphereDataDescriptorSets[0]);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	m_Material->UpdateAtmosphereMaterialData(materialManager, lightManager);

	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	//描述符集已经统一更新过
}

void VansGraphics::VansShadowRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	//创建uniform buffer以及对应的描述符,可以同时包含多个类型的desc
	VkDescriptorSetLayoutBinding modelBufferBinding =
	{
		VansVKDescriptorManager::m_ModelBufferSetBinding,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ modelBufferBinding }, modelBufferLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ modelBufferLayout }, modelBufferDescriptorSets);

	m_UsedDescSetLayouts.push_back(modelBufferLayout);
	m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);

	RegistLightDescriptor(lightManager);
}

void VansGraphics::VansShadowRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansShadowRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			modelBufferDescriptorSets[0],
			VansVKDescriptorManager::m_ModelBufferSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_RenderNodeDataBuffer.GetNativeBuffer(),
					0,
					m_RenderNodeDataBuffer.GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}
