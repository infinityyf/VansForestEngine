#pragma once

#include <Windows.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace VansGraphics
{
	enum class VansStreamlineDLSSMode : std::uint8_t
	{
		DLAA,
		Quality,
		Balanced,
		Performance,
		UltraPerformance
	};

	struct VansStreamlineOptimalSettings
	{
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t minRenderWidth = 0;
		std::uint32_t minRenderHeight = 0;
		std::uint32_t maxRenderWidth = 0;
		std::uint32_t maxRenderHeight = 0;
	};

	struct VansStreamlineImageResource
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkImageCreateInfo createInfo{};
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	struct VansStreamlineDLSSDispatch
	{
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VansStreamlineImageResource color;
		VansStreamlineImageResource depth;
		VansStreamlineImageResource motionVectors;
		VansStreamlineImageResource output;
		VansStreamlineImageResource exposure;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t frameIndex = 0;
		glm::vec2 jitterPixels{ 0.0f };
		// ForestEngine stores previous-to-current motion in normalized UV units.
		// DLSS consumes current-to-previous normalized motion.
		glm::vec2 motionVectorScale{ -1.0f, -1.0f };
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::mat4 previousViewProjection{ 1.0f };
		glm::vec3 cameraPosition{ 0.0f };
		glm::vec3 cameraUp{ 0.0f, 1.0f, 0.0f };
		glm::vec3 cameraRight{ 1.0f, 0.0f, 0.0f };
		glm::vec3 cameraForward{ 0.0f, 0.0f, -1.0f };
		float cameraNear = 0.1f;
		float cameraFar = 1000.0f;
		float cameraFovRadians = 1.0f;
		bool reset = false;
	};

	class VansStreamlineRuntime
	{
	public:
		static VansStreamlineRuntime& Get();

		// Must run before any Vulkan instance/device creation. On failure the caller
		// continues with the system Vulkan loader and DLSS remains unavailable.
		HMODULE TryInitializeVulkanLoader();
		void RefreshDeviceCapabilities();
		void ShutdownBeforeVulkan();

		bool IsInitialized() const { return m_Initialized; }
		bool IsDLSSAvailable() const { return m_DLSSAvailable; }
		const std::string& GetUnavailableReason() const { return m_UnavailableReason; }
		const std::string& GetFeatureVersion() const { return m_FeatureVersion; }

		bool QueryOptimalSettings(
			VansStreamlineDLSSMode mode,
			std::uint32_t outputWidth,
			std::uint32_t outputHeight,
			VansStreamlineOptimalSettings& settings);
		bool ConfigureDLSS(
			VansStreamlineDLSSMode mode,
			std::uint32_t outputWidth,
			std::uint32_t outputHeight,
			bool useExternalExposure);
		bool EvaluateDLSS(const VansStreamlineDLSSDispatch& dispatch);
		void ReleaseDLSSResources();

		HMODULE GetModule() const { return m_Module; }

	private:
		VansStreamlineRuntime() = default;
		bool ResolveCoreFunctions();
		bool ResolveDLSSFunctions();

		HMODULE m_Module = nullptr;
		bool m_Initialized = false;
		bool m_DLSSAvailable = false;
		std::string m_UnavailableReason = "Streamline has not been initialized";
		std::string m_FeatureVersion;

		void* m_Init = nullptr;
		void* m_Shutdown = nullptr;
		void* m_IsFeatureLoaded = nullptr;
		void* m_GetFeatureFunction = nullptr;
		void* m_GetFeatureVersion = nullptr;
		void* m_SetTagForFrame = nullptr;
		void* m_SetConstants = nullptr;
		void* m_EvaluateFeature = nullptr;
		void* m_GetNewFrameToken = nullptr;
		void* m_FreeResources = nullptr;
		void* m_DLSSGetOptimalSettings = nullptr;
		void* m_DLSSSetOptions = nullptr;
	};
}
