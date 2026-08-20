#pragma once

#include "VansFSRTypes.h"

#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>

#include <memory>

namespace VansGraphics
{
	class VansVKImage;

	class VansFSR
	{
	public:
		VansFSR() = default;
		~VansFSR();
		VansFSR(const VansFSR&) = delete;
		VansFSR& operator=(const VansFSR&) = delete;

		bool InitializeContext(
			VkDevice device,
			VkPhysicalDevice physicalDevice,
			std::uint32_t renderWidth,
			std::uint32_t renderHeight,
			std::uint32_t displayWidth,
			std::uint32_t displayHeight,
			VansVKImage& outputImage);
		bool DispatchUpscale(VkCommandBuffer commandBuffer, const VansFSRFrameInput& input);
		bool GenerateReactiveMask(
			VkCommandBuffer commandBuffer,
			VkImage opaqueOnly,
			const VkImageCreateInfo& opaqueOnlyCreateInfo,
			VkImage colorPreUpscale,
			const VkImageCreateInfo& colorPreUpscaleCreateInfo);
		void Cleanup();

		VansVKImage& GetReactiveMaskImage() { return *m_ReactiveMaskImage; }
		VansVKImage& GetTransparencyAndCompositionImage() { return *m_TransparencyAndCompositionImage; }
		VkExtent2D GetDisplayExtent() const { return { m_DisplayWidth, m_DisplayHeight }; }

		void SetSharpness(float sharpness);
		float GetSharpness() const { return m_Sharpness; }
		int32_t GetJitterPhaseCount() const { return m_JitterPhaseCount; }
		bool GetJitterOffset(int32_t index, float& outX, float& outY);
		void SetDebugViewEnabled(bool enabled) { m_DebugViewEnabled = enabled; }
		bool IsDebugViewEnabled() const { return m_DebugViewEnabled; }
		const VansFSRDiagnostics& GetDiagnostics() const { return m_Diagnostics; }

	private:
		ffx::Context m_UpscalingContext = nullptr;
		std::uint32_t m_RenderWidth = 0;
		std::uint32_t m_RenderHeight = 0;
		std::uint32_t m_DisplayWidth = 0;
		std::uint32_t m_DisplayHeight = 0;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		int32_t m_JitterPhaseCount = 0;
		// The unified upscaler layer owns the stable output image. FSR only writes it.
		VansVKImage* m_OutputImage = nullptr;
		std::unique_ptr<VansVKImage> m_ReactiveMaskImage;
		std::unique_ptr<VansVKImage> m_TransparencyAndCompositionImage;
		VansFSRDiagnostics m_Diagnostics;
		bool m_DebugViewEnabled = false;
		float m_Sharpness = 0.2f;
	};
}
