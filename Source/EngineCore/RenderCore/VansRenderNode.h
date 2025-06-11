#pragma once

//#include "VulkanCore/VansMesh.h"
#include "VansMaterial.h"
#include "VansTransform.h"
#include "BRDFData/VansLight.h"

namespace VansGraphics
{
	enum  RenderNodeType
	{
		NONE_NODE = 0,
		OPAQUE_NODE = 1 << 0,
		TRANSPARENT_NODE = 1 << 1,
		POSTPROCESS_NODE = 1 << 2,
		SKY_BOX_NODE = 1 << 3,
		DEFERRED_NODE = 1 << 4,
		SCREEN_SPACE_NODE = 1 << 5,
	};

	struct alignas(16) ModelDataStruct
	{
		glm::mat4x4 ModelMatrix;
		alignas(16) glm::vec3 Postion;
		alignas(16) glm::vec3 Scale;
	};

	class VansCamera;
	class VansRenderNode
	{
	public:

		VansRenderNode(VkDevice& device, RenderNodeType type);

		virtual ~VansRenderNode();

		std::string m_NodeName;

		VansMesh* m_Mesh;

		VansMaterial* m_Material;

	private:

		//GPU 数据
		ModelDataStruct m_ModelData;

		//transform数据
		VansTransform m_Transform;

	protected:

		RenderNodeType m_NodeType;

		//描述符相关
		VkDescriptorSetLayout modelBufferLayout;
		std::vector<VkDescriptorSet> modelBufferDescriptorSets;

		//sampler imgae 描述符
		VkDescriptorSetLayout textureResourceLayout;
		std::vector<VkDescriptorSet> textureResourceDescriptorSets;

		VkDescriptorSetLayout frameBufferInputLayout;
		std::vector<VkDescriptorSet> frameBufferInputDescriptorSets;

		//uniform buffer
		VansVKBuffer m_RenderNodeDataBuffer;

		void DestroyDescriptorSets();


		std::vector<VkDescriptorSetLayout> m_UsedDescSetLayouts;

		std::vector<VkDescriptorSet> m_UsedDescSets;

		//统一被CreateDescriptorSets调用
		void RegistCameraDescriptor(VansCamera* camera);

		void RegistLightDescriptor(VansLightManager& lightManager);

	public:
		void virtual CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) {};

		void virtual UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) {};

		void BeforeDrawCall();

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void DrawWithMaterial(VansMaterial* material ,VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void SetName(const std::string& name)
		{
			m_NodeName = name;
		}

		void SetTransformData(glm::vec3 postion = glm::vec3(0,0,0), glm::vec3 rotation = glm::vec3(0, 0, 0), glm::vec3 scale = glm::vec3(1, 1, 1))
		{
			m_Transform.m_Position = postion;
			m_Transform.m_Rotation = rotation;
			m_Transform.m_Scale = scale;
		}

		glm::vec3 GetTransformPosition()
		{
			return m_Transform.m_Position;
		}

		glm::vec3 GetTransformRotation()
		{
			return m_Transform.m_Rotation;
		}

		glm::vec3 GetTransformScale()
		{
			return m_Transform.m_Scale;
		}
	};

	//更新材质参数,更新全局数据
	//1. 预计算环境漫反射
	//2. 高光lut
	//3. 大气

	class VansCommonRenderNode : public VansRenderNode
	{
	public:
		bool m_SupportShadow;

		VansCommonRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type), m_SupportShadow(false) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
	};

	class VansSkyBoxRenderNode : public VansRenderNode
	{
	public:

		VansSkyBoxRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
	};

	class VansPostProcessRenderNode : public VansRenderNode
	{
	public:

		VansPostProcessRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
	};

	class VansDeferredRenderNode : public VansRenderNode
	{
	public:

		VansDeferredRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
	};

	class VansScreenSpaceRenderNode : public VansRenderNode
	{
	public:

		VansScreenSpaceRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
	};

	class VansShadowRenderNode : public VansRenderNode
	{
	public:

		VansShadowRenderNode(VkDevice& device) : VansRenderNode(device, NONE_NODE) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
	};
}