#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include "VansDescriptorSetLayouts.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
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

	struct VansDescriptorPoolDiagnostics
	{
		uint32_t standardPoolCount = 0;
		uint32_t updateAfterBindPoolCount = 0;
		uint32_t trackedDescriptorSetCount = 0;
		uint32_t updateAfterBindLayoutCount = 0;
		uint32_t globalPersistentSetCount = 0;
		uint32_t scenePersistentSetCount = 0;
		uint32_t frameTransientSetCount = 0;
		uint32_t passPersistentSetCount = 0;
		uint32_t uploadScratchSetCount = 0;
		uint32_t rayTracingPersistentSetCount = 0;
	};

	enum class VansDescriptorLifetimeRole : uint8_t
	{
		GlobalPersistent,
		ScenePersistent,
		FrameTransient,
		PassPersistent,
		UploadScratch,
		RayTracingPersistent
	};

	class VansVKDescriptorManager
	{
	public:

	private:

		//各个类似的描述符在这个pool中的最大数量，不是在一个set中的
		uint32_t m_MaxSetsCount = 800 * 100;
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
	private:
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

		void BeginDescriptorUpdate() { ResetState(); }
		void WriteImageDescriptor(
			VkDescriptorSet dstSet,
			uint32_t binding,
			VkDescriptorType type,
			const std::vector<VkDescriptorImageInfo>& imageInfos,
			uint32_t firstElement = 0);
		void WriteBufferDescriptor(
			VkDescriptorSet dstSet,
			uint32_t binding,
			VkDescriptorType type,
			const std::vector<VkDescriptorBufferInfo>& bufferInfos,
			uint32_t firstElement = 0);
		void WriteTexelBufferDescriptor(
			VkDescriptorSet dstSet,
			uint32_t binding,
			VkDescriptorType type,
			const std::vector<VkBufferView>& texelBufferViews,
			uint32_t firstElement = 0);
		void WriteAccelerationStructureDescriptor(
			VkDescriptorSet dstSet,
			uint32_t binding,
			VkAccelerationStructureKHR accelerationStructure,
			uint32_t firstElement = 0);
		void CopyDescriptor(
			VkDescriptorSet dstSet,
			uint32_t dstBinding,
			VkDescriptorSet srcSet,
			uint32_t srcBinding,
			uint32_t descriptorCount,
			uint32_t dstArrayElement = 0,
			uint32_t srcArrayElement = 0);
		void CommitDescriptorUpdates();

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
		VansDescriptorPoolDiagnostics GetDiagnostics() const;
	private:

		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		VkDescriptorPool m_UpdateAfterBindDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorPoolCreateFlags m_DescriptorPoolFlags = 0;
		std::vector<VkDescriptorPool> m_DescriptorPools;
		std::vector<VkDescriptorPool> m_UpdateAfterBindDescriptorPools;
		std::unordered_set<VkDescriptorSetLayout> m_UpdateAfterBindLayouts;
		std::unordered_map<VkDescriptorSet, VkDescriptorPool> m_DescriptorSetPools;
		std::unordered_map<VkDescriptorSet, VansDescriptorLifetimeRole> m_DescriptorSetRoles;

		bool CreateDescriptorPoolHandle(VkDescriptorPoolCreateFlags flags, VkDescriptorPool& outPool);
		bool UsesUpdateAfterBindPool(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts) const;
		bool AllocateDescriptorSetFromPool(
			VkDescriptorPool descriptorPool,
			const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
			std::vector<VkDescriptorSet>& descriptorSets,
			VkResult& outResult,
			VansDescriptorLifetimeRole lifetimeRole);

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

		bool AllocateDescriptorSet(
			const std::vector<VkDescriptorSetLayout>& discriptor_set_layout,
			std::vector<VkDescriptorSet>& descriptor_sets,
			VansDescriptorLifetimeRole lifetimeRole = VansDescriptorLifetimeRole::ScenePersistent);
		
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
