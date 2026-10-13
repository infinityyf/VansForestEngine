#pragma once
#include "../Public/EngineDTOs.h"
#include "VansEditorTextureBridge.h"
#include "../../RenderCore/VansRenderSystem.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../Util/VansLog.h"
#include <array>
#include <functional>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Vans::EditorAPI
{
	class VansEditorPreviewRegistry final
	{
	public:
		using RegistrationId = std::uint64_t;
		void BindRenderSystem(VansGraphics::VansRenderSystem* renderSystem)
		{
			m_RenderSystem = renderSystem;
			m_MainThreadId = std::this_thread::get_id();
		}

		struct PreviewTextureCache
		{
			RenderTextureId id = 0;
			VkImage image = VK_NULL_HANDLE;
			VkImageView imageView = VK_NULL_HANDLE;
			VkImageView opaqueView = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			EditorTextureHandle texture = nullptr;
			RegistrationId registration = 0;
		};

		struct FramePreviewTextureCache
		{
			RenderTextureId id = 0;
			std::array<PreviewTextureCache, 2> targets;
		};

		struct LayerPreviewCache
		{
			RenderTextureId id = 0;
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			std::uint32_t layer = UINT32_MAX;
			std::uint32_t mipLevel = UINT32_MAX;
			EditorTextureHandle texture = nullptr;
			RegistrationId registration = 0;
		};

		struct SinglePreviewCache
		{
			VansGraphics::VansTexture* sourceTexture = nullptr;
			VkImage image = VK_NULL_HANDLE;
			VkImageView imageView = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			EditorTextureHandle texture = nullptr;
			RegistrationId registration = 0;
		};

		RegistrationId ReserveRegistrationId()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return m_NextRegistrationId++;
		}

		RegistrationId RegisterTexture(
			std::string owner,
			VkSampler sampler,
			VkImageView imageView,
			VkImageLayout layout,
			VkImage image,
			EditorTextureHandle& texture)
		{
			texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
				sampler, imageView, layout, image);
			if (!texture)
				return 0;

			std::lock_guard<std::mutex> lock(m_Mutex);
			const RegistrationId id = m_NextRegistrationId++;
			m_Registrations.emplace(id, Registration{ std::move(owner), texture });
			return id;
		}

		void ReleaseTextureNow(RegistrationId& id)
		{
			const EditorTextureHandle texture = TakeTexture(id);
			id = 0;
			if (texture)
				Vans::Editor::VansEditorTextureBridge::RemoveTexture(texture);
		}

		void RetireTexture(
			RegistrationId& id,
			VansGraphics::VansVKDevice* device)
		{
			const EditorTextureHandle texture = TakeTexture(id);
			id = 0;
			if (!texture)
				return;
			RetireResource([texture]()
				{
					Vans::Editor::VansEditorTextureBridge::RemoveTexture(texture);
				}, device);
		}

		void RetireResource(std::function<void()> destroy, VansGraphics::VansVKDevice* device)
		{
			if (m_RenderSystem && std::this_thread::get_id() == m_MainThreadId)
			{
				if (m_RenderSystem->QueueFrameRetirement(destroy))
					return;
				if (m_RenderSystem->WaitForIdle())
				{
					destroy();
					return;
				}
				// Fatal cleanup still runs through the RenderThread operation port. A
				// failed wait must never drop the callback and release its captured GPU owner.
				class RetirementTransaction final : public VansGraphics::IVansRenderThreadTransaction
				{
				public:
					explicit RetirementTransaction(std::function<void()> release) : m_Release(std::move(release)) {}
					bool CanExecuteAfterFatal() const override { return true; }
					bool Execute(VansGraphics::VansGraphicsDevice& backend) override
					{
						if (!backend.WaitForIdle() && !backend.HasDeviceLost()) return false;
						m_Release(); return true;
					}
				private:
					std::function<void()> m_Release;
				};
				if (m_RenderSystem->ExecuteRenderThreadTransaction(std::make_unique<RetirementTransaction>(destroy))) return;
				VANS_LOG_ERROR("[EditorPreview] Resource retirement could not establish a GPU completion boundary.");
				std::terminate();
			}
			if (device)
				device->EnqueueDeferredDelete(std::move(destroy));
			else
				destroy();
		}

		void RetireAll(VansGraphics::VansVKDevice* device)
		{
			std::vector<EditorTextureHandle> textures;
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				textures.reserve(m_Registrations.size());
				for (const auto& [id, registration] : m_Registrations)
				{
					(void)id;
					if (registration.texture)
						textures.push_back(registration.texture);
				}
				m_Registrations.clear();
			}
			for (EditorTextureHandle texture : textures)
			{
				RetireResource([texture]()
					{
						Vans::Editor::VansEditorTextureBridge::RemoveTexture(texture);
					}, device);
			}
		}

		std::size_t RegistrationCount() const
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return m_Registrations.size();
		}

		std::vector<PreviewTextureCache> imageCaches;
		std::vector<FramePreviewTextureCache> frameImageCaches;
		std::vector<LayerPreviewCache> layerCaches;
		SinglePreviewCache viewportCache;
		SinglePreviewCache reflectionProbeCache;

	private:
		struct Registration
		{
			std::string owner;
			EditorTextureHandle texture = nullptr;
		};

		EditorTextureHandle TakeTexture(RegistrationId id)
		{
			if (id == 0)
				return nullptr;
			std::lock_guard<std::mutex> lock(m_Mutex);
			const auto found = m_Registrations.find(id);
			if (found == m_Registrations.end())
				return nullptr;
			const EditorTextureHandle texture = found->second.texture;
			m_Registrations.erase(found);
			return texture;
		}

		mutable std::mutex m_Mutex;
		RegistrationId m_NextRegistrationId = 1;
		std::unordered_map<RegistrationId, Registration> m_Registrations;
		VansGraphics::VansRenderSystem* m_RenderSystem = nullptr;
		std::thread::id m_MainThreadId;
	};

}
