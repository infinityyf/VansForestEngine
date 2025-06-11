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

	//初始化 transform数据
	SetTransformData();
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
	m_ModelData.ModelMatrix = glm::scale(m_ModelData.ModelMatrix, m_Transform.m_Scale);
	//m_ModelData.ModelMatrix = glm::rotate(m_ModelData.ModelMatrix, m_Transform.m_Rotation);
	m_ModelData.Postion = m_Transform.m_Position;
	m_ModelData.Scale = m_Transform.m_Scale;

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


	m_UsedDescSetLayouts.push_back(modelBufferLayout);
	m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);
	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);

	RegistLightDescriptor(lightManager);

	//PBR参数 
	m_UsedDescSetLayouts.push_back(materialManager.m_MaterialPBRBaseDataLayout);
	m_UsedDescSets.push_back(materialManager.m_MaterialPBRBaseDataDescriptorSets[0]);

	//预卷积 _ lut
	m_UsedDescSetLayouts.push_back(materialManager.m_BRDFInterationTexSetLayout);
	m_UsedDescSets.push_back(materialManager.m_BRDFInterationTextDescriptorSets[0]);
}

void VansGraphics::VansCommonRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
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

	//更新pbr参数
	m_Material->UpdatePBRUniformData(materialManager);
	m_Material->UpdatePBRLutData(materialManager);
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
	VkDescriptorSetLayoutBinding textureInput0 =
	{
		VansVKDescriptorManager::m_SampleTexture0SetBinding,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
		{
			textureInput0,
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
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
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

	m_Material->UpdatePBRLutData(materialManager);
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
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
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

	VkDescriptorSetLayoutBinding stmosphereUnifomBuffer =
	{
		VansVKDescriptorManager::m_AtmosphereBufferSetBinding,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ stmosphereUnifomBuffer }, materialManager.m_MaterialAtmosphereDataLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ materialManager.m_MaterialAtmosphereDataLayout }, materialManager.m_MaterialAtmosphereDataDescriptorSets);

	m_UsedDescSetLayouts.push_back(materialManager.m_MaterialAtmosphereDataLayout);
	m_UsedDescSets.push_back(materialManager.m_MaterialAtmosphereDataDescriptorSets[0]);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	m_Material->UpdateAtmosphereMaterialData(materialManager, lightManager);
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
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}
