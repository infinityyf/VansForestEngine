#include "VansGUIVulkanBackEnd.h"
#include "VansRenderPass.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../Util/VansLog.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "../../../Graphics/Vulkan/VansVKFunctions.h"

#include <memory>
#include <utility>
#include <vector>

namespace
{
	class VansImGuiFrameOverlay final : public VansGraphics::IVansRenderFrameOverlay
	{
	public:
		VansImGuiFrameOverlay(const ImDrawData& source,
			std::unordered_map<void*, void*> frameTextureVariants,
			std::unordered_map<void*, VansGraphics::VansGUITextureResource> textureResources,
			std::unordered_map<void*, std::shared_ptr<VansGraphics::IVansRenderFrameTextureBinding>> frameTextureBindings)
			: m_FrameTextureVariants(std::move(frameTextureVariants)),
			m_TextureResources(std::move(textureResources)),m_FrameTextureBindings(std::move(frameTextureBindings))
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
			auto* vkDevice = dynamic_cast<VansGraphics::VansVKDevice*>(&device);
			if (vkDevice == nullptr || vkDevice->GetCurrentGBufferTargetSlot() > 1)
				return false;
			if (vkDevice->GetCurrentGBufferTargetSlot() == 1)
			{
				for (ImDrawList* drawList : m_DrawLists)
					for (ImDrawCmd& drawCommand : drawList->CmdBuffer)
					{
						const auto variant = m_FrameTextureVariants.find(drawCommand.TextureId);
						if (variant != m_FrameTextureVariants.end())
							drawCommand.TextureId = variant->second;
					}
			}
			if (!m_FrameTextureBindings.empty())
			{
				vkDevice->RefreshRuntimeRenderTextureCatalog();
				for (auto* drawList : m_DrawLists) for (int index = drawList->CmdBuffer.Size - 1; index >= 0; --index)
				{
					auto& command = drawList->CmdBuffer[index];
					const auto binding = m_FrameTextureBindings.find(command.TextureId);
					if (binding == m_FrameTextureBindings.end() || command.UserCallback) continue;
					auto* texture = command.ElemCount ? binding->second->Resolve(device) : nullptr;
					if (texture) command.TextureId = texture;
					// ImGui 后端即使 ElemCount 为零仍绑定 descriptor，失效 token 必须移除整个命令。
					else drawList->CmdBuffer.erase(&command);
				}
			}
			for (const ImDrawList* drawList : m_DrawLists)
				for (const ImDrawCmd& drawCommand : drawList->CmdBuffer)
				{
					if (drawCommand.ElemCount == 0)
						continue;
					const auto resource = m_TextureResources.find(drawCommand.TextureId);
					if (resource != m_TextureResources.end())
						vkDevice->RecordOverlaySampledImage(
							resource->second.image, resource->second.layout);
				}
			auto* commandBuffer = static_cast<VkCommandBuffer*>(device.GetNativeCommandBuffer());
			if (commandBuffer == nullptr || *commandBuffer == VK_NULL_HANDLE)
				return false;
			device.BeginUIRenderPass();
			ImGui_ImplVulkan_RenderDrawData(&m_DrawData, *commandBuffer);
			device.EndUIRenderPass();
			for (auto& [token,binding] : m_FrameTextureBindings) binding->Finish(device);
			return true;
		}

	private:
		ImDrawData m_DrawData{};
		std::vector<ImDrawList*> m_DrawLists;
		std::unordered_map<void*, void*> m_FrameTextureVariants;
		std::unordered_map<void*, VansGraphics::VansGUITextureResource> m_TextureResources;
		std::unordered_map<void*, std::shared_ptr<VansGraphics::IVansRenderFrameTextureBinding>> m_FrameTextureBindings;
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
	bool CanExecuteAfterFatal() const override { return true; }

	bool Execute(VansGraphicsDevice&) override
	{
		return m_Owner.ShutdownRendererOnRenderThread();
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

bool VansGraphics::VansGraphicsGUIBackEnd::ShutdownRendererOnRenderThread()
{
	VANS_ASSERT_RENDER_THREAD();
	if (!m_RendererInitialized)
		return true;

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
	std::unordered_map<void*, void*> frameTextureVariants;
	std::unordered_map<void*, VansGUITextureResource> textureResources;
	std::unordered_map<void*, std::shared_ptr<IVansRenderFrameTextureBinding>> frameTextureBindings;
	{
		std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
		const auto captureResource = [this, &textureResources](void* texture)
		{
			const auto resource = m_TextureResources.find(texture);
			if (resource != m_TextureResources.end())
				textureResources.emplace(texture, resource->second);
		};
		for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
			for (const ImDrawCmd& command : drawData->CmdLists[listIndex]->CmdBuffer)
			{
				const auto binding = m_FrameTextureBindings.find(command.TextureId);
				if (binding != m_FrameTextureBindings.end()) frameTextureBindings.emplace(binding->first,binding->second);
				if (command.ElemCount == 0)
					continue;
				captureResource(command.TextureId);
				const auto variant = m_FrameTextureVariants.find(command.TextureId);
				if (variant != m_FrameTextureVariants.end())
				{
					frameTextureVariants.emplace(variant->first, variant->second);
					captureResource(variant->second);
				}

			}
		// 同一 UI 帧较早的窗口可能已经写入旧 token；先捕获其 binding，再取消注册。
		for (auto* token : m_FrameTextureBindingsPendingRemoval)
			m_FrameTextureBindings.erase(token);
		m_FrameTextureBindingsPendingRemoval.clear();
	}
	return std::make_unique<VansImGuiFrameOverlay>(
		*drawData, std::move(frameTextureVariants), std::move(textureResources),std::move(frameTextureBindings));
}
bool VansGraphics::VansGraphicsGUIBackEnd::RegisterFrameTextureBinding(void* texture, std::shared_ptr<IVansRenderFrameTextureBinding> binding)
{
	if (!texture || !binding) return false;
	std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
	m_FrameTextureBindingsPendingRemoval.erase(texture);
	m_FrameTextureBindings[texture] = std::move(binding);
	return true;
}
void VansGraphics::VansGraphicsGUIBackEnd::UnregisterFrameTextureBinding(void* texture)
{
	std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
	if (m_FrameTextureBindings.count(texture)) m_FrameTextureBindingsPendingRemoval.insert(texture);
}

void VansGraphics::VansGraphicsGUIBackEnd::RegisterTextureResource(
	void* texture, void* image, int layout)
{
	if (texture == nullptr || image == nullptr)
		return;
	std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
	m_TextureResources[texture] = {
		reinterpret_cast<VkImage>(image), static_cast<VkImageLayout>(layout) };
}

void VansGraphics::VansGraphicsGUIBackEnd::UnregisterTextureResource(void* texture)
{
	std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
	m_TextureResources.erase(texture);
}

bool VansGraphics::VansGraphicsGUIBackEnd::RegisterFrameTextureVariant(
	void* primary, void* alternate)
{
	if (primary == nullptr || alternate == nullptr)
		return false;
	std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
	m_FrameTextureVariants[primary] = alternate;
	return true;
}

void VansGraphics::VansGraphicsGUIBackEnd::UnregisterFrameTextureVariant(void* primary)
{
	std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
	m_FrameTextureVariants.erase(primary);
}

void VansGraphics::VansGraphicsGUIBackEnd::ShutdownBackEnd()
{
	{
		std::lock_guard<std::mutex> lock(m_FrameTextureMutex);
		m_FrameTextureVariants.clear();
		m_TextureResources.clear();
		m_FrameTextureBindings.clear();
		m_FrameTextureBindingsPendingRemoval.clear();
	}
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
