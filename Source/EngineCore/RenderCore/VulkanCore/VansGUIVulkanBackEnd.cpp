#include "VansGUIVulkanBackEnd.h"
#include "VansRenderPass.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "../../../Graphics/Vulkan/VansVKFunctions.h"

VansGraphics::VansGraphicsGUIBackEnd::~VansGraphicsGUIBackEnd()
{
	ShutdownBackEnd();
}

void VansGraphics::VansGraphicsGUIBackEnd::InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window)
{
	ShutdownBackEnd();

	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(&device);
	if (!vkDevice)
	{
		return;
	}

	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	m_Device = vkDevice->GetLogicDevice();
	VkInstance imguiInstance = vkDevice->GetInstance();
	VkPhysicalDevice imguiPhysicalDevice = vkDevice->GetPhysicalDevice();
	VkQueue imguiGraphicsQueue = vkDevice->GetGraphicsQueue();
	VkRenderPass imguiRenderPass = VansRenderPassManager::GetInstance()->GetVansRenderPass().GetRenderPass();
	
	vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_ImGUIPool);


	// 2: initialize imgui library

	//this initializes the core structures of imgui
	ImGui::CreateContext();

	ImGui_ImplGlfw_InitForVulkan(window,true);

	//this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = imguiInstance;
	init_info.PhysicalDevice = imguiPhysicalDevice;
	init_info.Device = m_Device;
	init_info.Queue = imguiGraphicsQueue;
	init_info.DescriptorPool = m_ImGUIPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.RenderPass = imguiRenderPass;
	init_info.Subpass = 1;

	ImGui_ImplVulkan_Init(&init_info);

	//execute a gpu command to upload imgui font textures
	ImGui_ImplVulkan_CreateFontsTexture();
	m_Initialized = true;
}

void VansGraphics::VansGraphicsGUIBackEnd::ShutdownBackEnd()
{
	if (m_Initialized)
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		m_Initialized = false;
	}

	if (m_Device != VK_NULL_HANDLE && m_ImGUIPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(m_Device, m_ImGUIPool, nullptr);
		m_ImGUIPool = VK_NULL_HANDLE;
	}

	m_Device = VK_NULL_HANDLE;
}
