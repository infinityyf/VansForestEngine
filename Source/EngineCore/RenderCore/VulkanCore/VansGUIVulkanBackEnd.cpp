#include "VansGUIVulkanBackEnd.h"
#include "VansRenderPass.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../Util/VansLog.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "../../../Graphics/Vulkan/VansVKFunctions.h"

#include <memory>
#include <vector>

namespace
{
	class VansImGuiFrameOverlay final : public VansGraphics::IVansRenderFrameOverlay
	{
	public:
		explicit VansImGuiFrameOverlay(const ImDrawData& source)
		{
			m_DrawData.Valid = source.Valid;
			m_DrawData.DisplayPos = source.DisplayPos;
			m_DrawData.DisplaySize = source.DisplaySize;
			m_DrawData.FramebufferScale = source.FramebufferScale;
            // Vulkan ImGui backend uses OwnerViewport to locate its per-viewport
            // upload buffers. Multi-viewport rendering is disabled for the
            // threaded path, therefore the main viewport object has stable
            // lifetime until GUI shutdown (which happens after RT quiesce).
            m_DrawData.OwnerViewport = source.OwnerViewport;
			m_DrawLists.reserve(static_cast<std::size_t>(source.CmdListsCount));
			for (int index = 0; index < source.CmdListsCount; ++index)
			{
				ImDrawList* clone = source.CmdLists[index]->CloneOutput();
				m_DrawLists.push_back(clone);
				m_DrawData.CmdLists.push_back(clone);
				m_DrawData.TotalVtxCount += clone->VtxBuffer.Size;
				m_DrawData.TotalIdxCount += clone->IdxBuffer.Size;
			}
			m_DrawData.CmdListsCount = m_DrawData.CmdLists.Size;
		}

		~VansImGuiFrameOverlay() override
		{
			for (ImDrawList* drawList : m_DrawLists)
				IM_DELETE(drawList);
		}

		bool Record(VansGraphics::VansGraphicsDevice& device) override
		{
			if (!device.CanRecordCurrentFrame())
				return true;
			auto* commandBuffer = static_cast<VkCommandBuffer*>(device.GetNativeCommandBuffer());
			if (commandBuffer == nullptr || *commandBuffer == VK_NULL_HANDLE)
				return false;
			device.BeginUIRenderPass();
			ImGui_ImplVulkan_RenderDrawData(&m_DrawData, *commandBuffer);
			device.EndUIRenderPass();
			return true;
		}

	private:
		ImDrawData m_DrawData{};
		std::vector<ImDrawList*> m_DrawLists;
	};
}

VansGraphics::VansGraphicsGUIBackEnd::~VansGraphicsGUIBackEnd()
{
	ShutdownBackEnd();
}

void VansGraphics::VansGraphicsGUIBackEnd::InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window)
{
	ShutdownBackEnd();

	m_VkDevice = dynamic_cast<VansVKDevice*>(&device);
	if (!m_VkDevice || ImGui::GetCurrentContext() == nullptr || window == nullptr)
	{
		m_VkDevice = nullptr;
		return;
	}
	m_PlatformInitialized = ImGui_ImplGlfw_InitForVulkan(window, true);
}

class VansGraphics::VansGraphicsGUIBackEnd::RendererInitializationTransaction final
	: public IVansRenderThreadTransaction
{
public:
	explicit RendererInitializationTransaction(VansGraphicsGUIBackEnd& owner)
		: m_Owner(owner) {}

	bool Execute(VansGraphicsDevice& backend) override
	{
		return m_Owner.InitializeRendererOnRenderThread(backend);
	}

private:
	VansGraphicsGUIBackEnd& m_Owner;
};

class VansGraphics::VansGraphicsGUIBackEnd::RendererShutdownTransaction final
	: public IVansRenderThreadTransaction
{
public:
	explicit RendererShutdownTransaction(VansGraphicsGUIBackEnd& owner)
		: m_Owner(owner) {}

	bool Execute(VansGraphicsDevice& backend) override
	{
		return m_Owner.ShutdownRendererOnRenderThread(backend);
	}

private:
	VansGraphicsGUIBackEnd& m_Owner;
};

std::unique_ptr<VansGraphics::IVansRenderThreadTransaction>
VansGraphics::VansGraphicsGUIBackEnd::CreateRenderThreadInitialization()
{
	return std::make_unique<RendererInitializationTransaction>(*this);
}

std::unique_ptr<VansGraphics::IVansRenderThreadTransaction>
VansGraphics::VansGraphicsGUIBackEnd::CreateRenderThreadShutdown()
{
	return std::make_unique<RendererShutdownTransaction>(*this);
}

bool VansGraphics::VansGraphicsGUIBackEnd::InitializeRendererOnRenderThread(
	VansGraphicsDevice& backend)
{
	VANS_ASSERT_RENDER_THREAD();
	if (m_RendererInitialized)
		return true;
	auto* vkDevice = dynamic_cast<VansVKDevice*>(&backend);
	if (!m_PlatformInitialized || !vkDevice || vkDevice != m_VkDevice ||
		ImGui::GetCurrentContext() == nullptr)
	{
		return false;
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
	VkRenderPass imguiRenderPass = VansRenderPassManager::GetInstance()->GetVansUIRenderPass().GetRenderPass();
	if (VansGraphics::vkCreateDescriptorPool(
		m_Device, &pool_info, nullptr, &m_ImGUIPool) != VK_SUCCESS)
	{
		m_ImGUIPool = VK_NULL_HANDLE;
		return false;
	}

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
	init_info.Subpass = 0;

	if (!ImGui_ImplVulkan_Init(&init_info) ||
		!ImGui_ImplVulkan_CreateFontsTexture())
	{
		ImGui_ImplVulkan_Shutdown();
		VansGraphics::vkDestroyDescriptorPool(
			m_Device, m_ImGUIPool, nullptr);
		m_ImGUIPool = VK_NULL_HANDLE;
		m_Device = VK_NULL_HANDLE;
		return false;
	}
	m_RendererInitialized = true;
	return true;
}

bool VansGraphics::VansGraphicsGUIBackEnd::ShutdownRendererOnRenderThread(
	VansGraphicsDevice& backend)
{
	VANS_ASSERT_RENDER_THREAD();
	if (!m_RendererInitialized)
		return true;
	if (!backend.WaitForIdle())
		return false;

	ImGui_ImplVulkan_Shutdown();
	if (m_Device != VK_NULL_HANDLE && m_ImGUIPool != VK_NULL_HANDLE)
		VansGraphics::vkDestroyDescriptorPool(
			m_Device, m_ImGUIPool, nullptr);
	m_ImGUIPool = VK_NULL_HANDLE;
	m_Device = VK_NULL_HANDLE;
	m_RendererInitialized = false;
	return true;
}

void VansGraphics::VansGraphicsGUIBackEnd::BeginFrame()
{
	if (!m_RendererInitialized)
		return;

	ImGui_ImplVulkan_NewFrame();
}

std::unique_ptr<VansGraphics::IVansRenderFrameOverlay>
VansGraphics::VansGraphicsGUIBackEnd::CaptureDrawData(ImDrawData* drawData)
{
	if (!m_RendererInitialized || !drawData)
		return nullptr;
	return std::make_unique<VansImGuiFrameOverlay>(*drawData);
}

void VansGraphics::VansGraphicsGUIBackEnd::ShutdownBackEnd()
{
	if (m_RendererInitialized)
	{
		VANS_LOG_ERROR("[GUI] Vulkan backend must be destroyed by RenderThread before platform shutdown.");
		return;
	}
	if (m_PlatformInitialized)
	{
		ImGui_ImplGlfw_Shutdown();
		m_PlatformInitialized = false;
	}
	m_VkDevice = nullptr;
}
