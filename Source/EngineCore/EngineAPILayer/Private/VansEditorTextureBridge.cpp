#include "VansEditorTextureBridge.h"

#include "backends/imgui_impl_vulkan.h"

namespace Vans::Editor
{
	EditorAPI::EditorTextureHandle VansEditorTextureBridge::RegisterTexture(void* sampler, void* imageView, int imageLayout)
	{
		if (!sampler || !imageView)
			return nullptr;

		VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
			reinterpret_cast<VkSampler>(sampler),
			reinterpret_cast<VkImageView>(imageView),
			static_cast<VkImageLayout>(imageLayout));

		return reinterpret_cast<EditorAPI::EditorTextureHandle>(descriptor);
	}

	void VansEditorTextureBridge::RemoveTexture(EditorAPI::EditorTextureHandle texture)
	{
		if (!texture)
			return;

		ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(texture));
	}
}
