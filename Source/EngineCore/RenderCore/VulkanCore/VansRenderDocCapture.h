#pragma once

#include <cstdint>
#include <string>

namespace VansGraphics
{
	struct VansRenderDocStatus
	{
		bool available = false;
		bool targetControlConnected = false;
		bool frameCapturing = false;
		bool apiValidationEnabled = false;
		bool referenceAllResources = false;
		int apiMajor = 0;
		int apiMinor = 0;
		int apiPatch = 0;
		std::uint32_t captureCount = 0;
		std::string capturePathTemplate;
		std::string lastCapturePath;
		std::string message;
	};

	// Optional bridge to RenderDoc's in-application API. The engine deliberately
	// does not link against or load renderdoc.dll: RenderDoc must inject it when
	// launching the process, which keeps ordinary engine launches dependency-free.
	class VansRenderDocCapture final
	{
	public:
		static VansRenderDocCapture& Get();

		bool Initialize();
		void BindVulkanWindow(void* vulkanInstance, void* nativeWindow);

		bool CaptureNextFrame();
		bool OpenReplayUI();
		bool SetAPIValidationEnabled(bool enabled);
		bool SetReferenceAllResources(bool enabled);
		VansRenderDocStatus QueryStatus();

	private:
		VansRenderDocCapture() = default;
		VansRenderDocCapture(const VansRenderDocCapture&) = delete;
		VansRenderDocCapture& operator=(const VansRenderDocCapture&) = delete;

		void* m_API = nullptr;
		void* m_VulkanDevicePointer = nullptr;
		void* m_NativeWindow = nullptr;
		bool m_MissingModuleLogged = false;
		std::string m_Message;
	};
}
