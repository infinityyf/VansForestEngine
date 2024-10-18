#include "VansRenderNode.h"
#include "VansCamera.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansRenderPass.h"
using namespace VansVulkan;

VansGraphics::VansRenderNode::VansRenderNode(VkDevice& device, RenderNodeType typee)
{
	m_NodeType = typee;

	m_RenderNodeDataBuffer.CreatVulkanBuffer(device, sizeof(ModelDataStruct), VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

VansGraphics::VansRenderNode::~VansRenderNode()
{
	DestroyDescriptorSets();

	//m_RenderNodeDataBuffer.DestroyVulkanBuffer();
}

void VansGraphics::VansRenderNode::RegistCameraDescriptor(VansCamera* camera)
{
	if ((m_NodeType & OPAQUE_NODE) != NONE_NODE)
	{
		m_UsedDescSetLayouts.push_back(camera->m_CameraBufferLayout);
		m_UsedDescSets.push_back(camera->m_CameraBufferDescriptorSets[0]);
	}

	if ((m_NodeType & SKY_BOX_NODE) != NONE_NODE)
	{
		m_UsedDescSetLayouts.push_back(camera->m_CameraBufferLayout);
		m_UsedDescSets.push_back(camera->m_CameraBufferDescriptorSets[0]);
	}
}


void VansGraphics::VansRenderNode::CreateDescriptorSets()
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

	//创建资源给fs采样
	VkDescriptorSetLayoutBinding samplerBinding =
	{
		VansVKDescriptorManager::m_SampleTexture0SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerBinding }, textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ textureResourceLayout }, textureResourceDescriptorSets);

	
	if ((m_NodeType & OPAQUE_NODE) != NONE_NODE)
	{
		m_UsedDescSetLayouts.push_back(modelBufferLayout);
		m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);
		m_UsedDescSetLayouts.push_back(textureResourceLayout);
		m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);
	}

	if ((m_NodeType & SKY_BOX_NODE) != NONE_NODE)
	{
		m_UsedDescSetLayouts.push_back(textureResourceLayout);
		m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);
	}

	if ((m_NodeType & POSTPROCESS_NODE) != NONE_NODE)
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
}

void VansGraphics::VansRenderNode::RegistLightDescriptor(VansLightManager& lightManager)
{
	if ((m_NodeType & OPAQUE_NODE) != NONE_NODE)
	{
		m_UsedDescSetLayouts.push_back(lightManager.m_LightDataDescriptorSetLayout);
		m_UsedDescSets.push_back(lightManager.m_LightDataDescriptorSets[0]);
	}
}

void VansGraphics::VansRenderNode::RegistMaterialDescriptor(VansMaterialManager& materialManager)
{
	if (m_Material->m_MaterialType == VAN_PBR)
	{
		//PBR参数 
		m_UsedDescSetLayouts.push_back(materialManager.m_MaterialPBRBaseDataLayout);
		m_UsedDescSets.push_back(materialManager.m_MaterialPBRBaseDataDescriptorSets[0]);

		//预卷积 _ lut
		m_UsedDescSetLayouts.push_back(materialManager.m_BRDFInterationTexSetLayout);
		m_UsedDescSets.push_back(materialManager.m_BRDFInterationTextDescriptorSets[0]);
	}
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

void VansGraphics::VansRenderNode::UpdateDescriptorSets(VansVKDevice* device, VansMaterialManager& materialManager, VansCamera* camera)
{
	//这里需要根据每个node用到的资源进行更新
	if ((m_NodeType & OPAQUE_NODE) != NONE_NODE)
	{
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				modelBufferDescriptorSets[0],
				VansVKDescriptorManager::m_ModelBufferSetBinding,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						m_RenderNodeDataBuffer.GetMativeBuffer(),
						0,
						m_RenderNodeDataBuffer.GetBufferSize()
					}
				}
			}
		);

		if (m_Material->m_Texture.size() > 0)
		{
			VansVKImage image = m_Material->m_Texture[0]->GetImage();
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					textureResourceDescriptorSets[0],
					VansVKDescriptorManager::m_SampleTexture0SetBinding,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							image.GetSampler(),
							image.GetImageView(),
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
						}
					}
				}
			);
		}


		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	if ((m_NodeType & SKY_BOX_NODE) != NONE_NODE)
	{
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
		if (m_Material->m_Texture.size() > 0)
		{
			VansVKImage image = m_Material->m_Texture[0]->GetImage();
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					textureResourceDescriptorSets[0],
					VansVKDescriptorManager::m_SampleTexture0SetBinding,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							image.GetSampler(),
							image.GetImageView(),
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
						}
					}
				}
			);
		}
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}


	if ((m_NodeType & POSTPROCESS_NODE )!= NONE_NODE)
	{
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
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


	//更新材质参数
	//更新全局数据
	//1. 预计算环境漫反射
	//2. 高光lut
	m_Material->UpdateMaterialDescriporSet(materialManager);
}

void VansGraphics::VansRenderNode::BeforeDrawCall()
{
	//更新CPU
	m_ModelData.ModelMatrix = glm::mat4x4(1.0f);

	//更新模型数据到PUG
	m_RenderNodeDataBuffer.SetBufferData(&m_ModelData,0, sizeof(m_ModelData));
}

void VansGraphics::VansRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	BeforeDrawCall();

	//apply mesh
	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	//apply shader，确认pipeline以及创建完毕
	cmd.EnsureGraphicsShader(*(m_Material->m_Shader), globalStateData, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(m_Material->m_Shader), 0, m_UsedDescSets, {});

	cmd.DrawMesh(*m_Mesh, *(m_Material->m_Shader), 1);
}
