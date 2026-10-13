#include "VansEditorTextureBridge.h"
#include "../../RenderCore/VansGraphicsDevice.h"

#include "backends/imgui_impl_vulkan.h"

#include <mutex>

namespace
{
	std::mutex g_EditorTextureRegistrationMutex;
}

namespace Vans::Editor
{
	EditorAPI::EditorTextureHandle VansEditorTextureBridge::RegisterTexture(
		void* sampler, void* imageView, int imageLayout, void* image)
	{
		if (!sampler || !imageView || !image || m_GUIBackEnd == nullptr)
			return nullptr;
		std::lock_guard<std::mutex> lock(g_EditorTextureRegistrationMutex);

		VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
			reinterpret_cast<VkSampler>(sampler),
			reinterpret_cast<VkImageView>(imageView),
			static_cast<VkImageLayout>(imageLayout));

		if (descriptor == VK_NULL_HANDLE)
			return nullptr;
		EditorAPI::EditorTextureHandle texture =
			reinterpret_cast<EditorAPI::EditorTextureHandle>(descriptor);
		m_GUIBackEnd->RegisterTextureResource(texture, image, imageLayout);
		return texture;
	}

	bool VansEditorTextureBridge::RegisterFrameTextureVariant(
		EditorAPI::EditorTextureHandle primary,
		EditorAPI::EditorTextureHandle alternate)
	{
		return m_GUIBackEnd != nullptr &&
			m_GUIBackEnd->RegisterFrameTextureVariant(primary, alternate);
	}

	void VansEditorTextureBridge::UnregisterFrameTextureVariant(
		EditorAPI::EditorTextureHandle primary)
	{
		if (m_GUIBackEnd != nullptr && primary != nullptr)
			m_GUIBackEnd->UnregisterFrameTextureVariant(primary);
	}

	void VansEditorTextureBridge::RemoveTexture(EditorAPI::EditorTextureHandle texture)
	{
		if (!texture)
			return;
		std::lock_guard<std::mutex> lock(g_EditorTextureRegistrationMutex);
		UnregisterFrameTextureVariant(texture);
		if (m_GUIBackEnd != nullptr)
			m_GUIBackEnd->UnregisterTextureResource(texture);

		ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(texture));
	}
}
