#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include "VansDescriptorSetLayouts.h"
#include <vector>


namespace VansGraphics
{
	//用于updating descriptor sets，这里将desc和具体的资源进行关联
	//desc只是一个和pipeline打交道的接口，将GPU和具体的资源绑定解耦
	//sampler and images descriptors
	struct ImageDescriptorInfo 
	{
		//包含这个desc的set
		VkDescriptorSet TargetDescriptorSet;
		uint32_t TargetDescriptorBinding;
		//需要被更新的desc在set中的索引
		uint32_t TargetArrayElement;
		VkDescriptorType TargetDescriptorType;
		std::vector<VkDescriptorImageInfo> ImageInfos;
	};

	//uniform and storage buffrt
	struct BufferDescriptorInfo 
	{
		VkDescriptorSet TargetDescriptorSet;
		uint32_t TargetDescriptorBinding;
		uint32_t TargetArrayElement;
		VkDescriptorType TargetDescriptorType;
		std::vector<VkDescriptorBufferInfo> BufferInfos;
	};

	//uniform and storage texture buffer
	struct TexelBufferDescriptorInfo 
	{
		VkDescriptorSet TargetDescriptorSet;
		uint32_t TargetDescriptorBinding;
		uint32_t TargetArrayElement;
		VkDescriptorType TargetDescriptorType;
		std::vector<VkBufferView> TexelBufferViews;
	};

	struct RayTraceASDescritorInfo
	{
		VkDescriptorSet TargetDescriptorSet;
		uint32_t TargetDescriptorBinding;
		uint32_t TargetArrayElement;
		VkDescriptorType TargetDescriptorType;
		VkAccelerationStructureKHR TargetAS;
	};

	struct CopyDescriptorInfo 
	{
		VkDescriptorSet TargetDescriptorSet;
		uint32_t TargetDescriptorBinding;
		uint32_t TargetArrayElement;
		VkDescriptorSet SourceDescriptorSet;
		uint32_t SourceDescriptorBinding;
		uint32_t SourceArrayElement;
		uint32_t DescriptorCount;
	};

	class VansVKDescriptorManager
	{
	public:

	private:

		//各个类似的描述符在这个pool中的最大数量，不是在一个set中的
		int m_MaxSetsCount = 800 * 100;
		uint32_t m_MaxSamplerDescCount = 20000;
		uint32_t m_MaxCombinedSamplerDescCount = 20000;
		uint32_t m_MaxSampledImageDescCount = 20000;
		uint32_t m_MaxStorageImageDescCount = 20000;
		uint32_t m_MaxUniformTexelDescCount = 20000;
		uint32_t m_MaxStorageTexelDescCount = 20000;
		uint32_t m_MaxUniformBufferDescCount = 20000;
		uint32_t m_MaxStorageBufferDescCount = 20000;
		uint32_t m_MaxUniformBufferDynamicDescCount = 20000;
		uint32_t m_MaxStorageBufferDynamicDescCount = 20000;
		uint32_t m_MaxInputAttachDescCount = 800;
		uint32_t m_MaxAccelerationStructureDescCount = 16;

		std::vector<VkDescriptorPoolSize> m_DescriptorPoolSizes = 
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, m_MaxSamplerDescCount},
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_MaxCombinedSamplerDescCount },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_MaxSampledImageDescCount },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_MaxStorageImageDescCount },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, m_MaxUniformTexelDescCount },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, m_MaxStorageTexelDescCount },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_MaxUniformBufferDescCount },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_MaxStorageBufferDescCount },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, m_MaxUniformBufferDynamicDescCount },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, m_MaxStorageBufferDynamicDescCount },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, m_MaxInputAttachDescCount },
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, m_MaxAccelerationStructureDescCount },
		};
	public:
		//待更新的各种类型的descinfo
		std::vector<BufferDescriptorInfo> m_BufferDescInfos;
		std::vector<ImageDescriptorInfo> m_ImageDescInfos;
		std::vector<TexelBufferDescriptorInfo> m_TexelBufferDescInfos;
		std::vector<CopyDescriptorInfo> m_CopyDescInfos;
		std::vector<RayTraceASDescritorInfo> m_RayTraceASInfos;

	private:
		static VansVKDescriptorManager* instance;

		VansVKDescriptorManager();

	private:
		//device info
		VkPhysicalDevice m_PhysicalDevice;

		VkDevice m_LogicalDevice;

		VkCommandBuffer m_CommandBuffer;

	public:
		static VansVKDescriptorManager* GetInstance()
		{
			if (instance == nullptr)
			{
				instance = new VansVKDescriptorManager();
			}
			return instance;
		}

		void ResetState()
		{
			m_BufferDescInfos.clear();
			m_ImageDescInfos.clear();
			m_TexelBufferDescInfos.clear();
			m_CopyDescInfos.clear();
			m_RayTraceASInfos.clear();
		}

		void BindDevice(VkPhysicalDevice& physicDevice, VkDevice& logicalDevice, VkCommandBuffer& commandBuffer)
		{
			m_PhysicalDevice = physicDevice;
			m_LogicalDevice = logicalDevice;
			m_CommandBuffer = commandBuffer;
		}

		void CreateDescriptorPool(bool free_individual_sets);
	
		//释放梭有这个pool里的sets
		//如果pool创建的flag包含：VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
		//除了free这个pool，只能通过reset来释放其中的set
		bool ResetDescriptorPool();

		void DestroyDescriptorPool();

		VkDescriptorPool GetDescriptorPool()
		{
			return m_DescriptorPool;
		}
	private:

		VkDescriptorPool m_DescriptorPool;

	public:

		//对于combined sampler image,shader里市sampler2D
		//一般sampler就声明为sampler
		//image则为texture2D
		
		//对于strorage images，不能使用sampler,shader中对应为image2D

		//对于texturebuffer，可以使用比image更大的内存空间，shader中使用samplerBufffer
		/*
		  Vulkan specification requires every
		driver to support 1D images of at least 4,096 texels. But for texel buffers, this minimal
		required limit goes up to 65,536 elements.
		*/
		//texture buufer分为uniform和storage,uniform是只读的，storage是可读写的
		//uniform 使用samplerbuffer,storage使用imagebuffer

		//对于uniform buffer,也存在dynamic的版本，在descriptor set更新是，可以更新部分uniform buffer
		//shader中使用uniform修饰一个blovk

		//storaget buffer 在shader中就是buffer，但是也有dynamic的版本

		//inputattachemnt则是之前的attachemtn作为输入，通过subpass input在shader中使用

		bool CreateDesciptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayout& descriptor_set_layout);

		// 支持 per-binding 标志位的布局创建接口，用于需要 UPDATE_AFTER_BIND 的 bindless 数组。
		// bindingFlags 长度须与 bindings 相同，不需要特殊标志的 binding 填 0。
		// layoutFlags 通常为 VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT。
		bool CreateDesciptorSetLayoutWithFlags(
			const std::vector<VkDescriptorSetLayoutBinding>& bindings,
			const std::vector<VkDescriptorBindingFlags>&     bindingFlags,
			VkDescriptorSetLayoutCreateFlags                 layoutFlags,
			VkDescriptorSetLayout&                           descriptor_set_layout);
	
		void DestroyDescriptorSetLayout(VkDescriptorSetLayout& descriptor_set_layout);

		bool AllocateDescriptorSet(const std::vector<VkDescriptorSetLayout>& discriptor_set_layout, std::vector<VkDescriptorSet>& descriptor_sets);
		
		bool DestroyDescriptorSet(std::vector<VkDescriptorSet>& descriptor_sets);

		void UpdateDescriptorSets();

		// 立即更新已存在的描述符集中的指定 Image 槽位，无需通过批处理队列。
		// 用于运行时切换（如视频源切换）。
		// 目标描述符集须使用 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT 创建，
		// 以确保在 GPU 执行期间更新是合法的 Vulkan 操作。
		void DirectUpdateImageDescriptors(VkDescriptorSet dstSet,
		                                  uint32_t        binding,
		                                  uint32_t        firstElement,
		                                  const std::vector<VkDescriptorImageInfo>& imageInfos,
		                                  VkDescriptorType type);
	};
}