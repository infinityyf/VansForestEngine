#include "VansRenderDocCapture.h"

#include "../../Util/VansLog.h"

#include <Windows.h>
#include <vector>

namespace
{
	// Minimal ABI-compatible subset of renderdoc_app.h (RenderDoc API 1.6.0).
	// Keeping it private avoids a build-time dependency on a RenderDoc install.
	enum RenderDocCaptureOption
	{
		RenderDocOptionAPIValidation = 2,
		RenderDocOptionReferenceAllResources = 8,
		RenderDocOptionDebugOutputMute = 11,
	};

	using RenderDocGetAPIVersion = void(__cdecl*)(int*, int*, int*);
	using RenderDocSetCaptureOptionU32 = int(__cdecl*)(RenderDocCaptureOption, std::uint32_t);
	using RenderDocSetCaptureOptionF32 = int(__cdecl*)(RenderDocCaptureOption, float);
	using RenderDocGetCaptureOptionU32 = std::uint32_t(__cdecl*)(RenderDocCaptureOption);
	using RenderDocGetCaptureOptionF32 = float(__cdecl*)(RenderDocCaptureOption);
	using RenderDocSetKeys = void(__cdecl*)(void*, int);
	using RenderDocGetOverlayBits = std::uint32_t(__cdecl*)();
	using RenderDocMaskOverlayBits = void(__cdecl*)(std::uint32_t, std::uint32_t);
	using RenderDocVoidFunction = void(__cdecl*)();
	using RenderDocSetCaptureFilePathTemplate = void(__cdecl*)(const char*);
	using RenderDocGetCaptureFilePathTemplate = const char* (__cdecl*)();
	using RenderDocGetNumCaptures = std::uint32_t(__cdecl*)();
	using RenderDocGetCapture = std::uint32_t(__cdecl*)(std::uint32_t, char*, std::uint32_t*, std::uint64_t*);
	using RenderDocTriggerCapture = void(__cdecl*)();
	using RenderDocIsTargetControlConnected = std::uint32_t(__cdecl*)();
	using RenderDocLaunchReplayUI = std::uint32_t(__cdecl*)(std::uint32_t, const char*);
	using RenderDocSetActiveWindow = void(__cdecl*)(void*, void*);
	using RenderDocStartFrameCapture = void(__cdecl*)(void*, void*);
	using RenderDocIsFrameCapturing = std::uint32_t(__cdecl*)();
	using RenderDocEndFrameCapture = std::uint32_t(__cdecl*)(void*, void*);
	using RenderDocTriggerMultiFrameCapture = void(__cdecl*)(std::uint32_t);
	using RenderDocSetCaptureFileComments = void(__cdecl*)(const char*, const char*);
	using RenderDocDiscardFrameCapture = std::uint32_t(__cdecl*)(void*, void*);
	using RenderDocShowReplayUI = std::uint32_t(__cdecl*)();
	using RenderDocSetCaptureTitle = void(__cdecl*)(const char*);

	struct RenderDocAPI
	{
		RenderDocGetAPIVersion GetAPIVersion;
		RenderDocSetCaptureOptionU32 SetCaptureOptionU32;
		RenderDocSetCaptureOptionF32 SetCaptureOptionF32;
		RenderDocGetCaptureOptionU32 GetCaptureOptionU32;
		RenderDocGetCaptureOptionF32 GetCaptureOptionF32;
		RenderDocSetKeys SetFocusToggleKeys;
		RenderDocSetKeys SetCaptureKeys;
		RenderDocGetOverlayBits GetOverlayBits;
		RenderDocMaskOverlayBits MaskOverlayBits;
		RenderDocVoidFunction RemoveHooks;
		RenderDocVoidFunction UnloadCrashHandler;
		RenderDocSetCaptureFilePathTemplate SetCaptureFilePathTemplate;
		RenderDocGetCaptureFilePathTemplate GetCaptureFilePathTemplate;
		RenderDocGetNumCaptures GetNumCaptures;
		RenderDocGetCapture GetCapture;
		RenderDocTriggerCapture TriggerCapture;
		RenderDocIsTargetControlConnected IsTargetControlConnected;
		RenderDocLaunchReplayUI LaunchReplayUI;
		RenderDocSetActiveWindow SetActiveWindow;
		RenderDocStartFrameCapture StartFrameCapture;
		RenderDocIsFrameCapturing IsFrameCapturing;
		RenderDocEndFrameCapture EndFrameCapture;
		RenderDocTriggerMultiFrameCapture TriggerMultiFrameCapture;
		RenderDocSetCaptureFileComments SetCaptureFileComments;
		RenderDocDiscardFrameCapture DiscardFrameCapture;
		RenderDocShowReplayUI ShowReplayUI;
		RenderDocSetCaptureTitle SetCaptureTitle;
	};

	using RenderDocGetAPI = int(__cdecl*)(int, void**);
	constexpr int RenderDocAPIVersion160 = 10600;

	RenderDocAPI* API(void* api)
	{
		return static_cast<RenderDocAPI*>(api);
	}
}

VansGraphics::VansRenderDocCapture& VansGraphics::VansRenderDocCapture::Get()
{
	static VansRenderDocCapture capture;
	return capture;
}

bool VansGraphics::VansRenderDocCapture::Initialize()
{
	if (m_API)
	{
		return true;
	}

	const HMODULE renderDocModule = GetModuleHandleA("renderdoc.dll");
	if (!renderDocModule)
	{
		m_Message = "RenderDoc is not injected. Launch ForestEngine from RenderDoc to enable capture.";
		if (!m_MissingModuleLogged)
		{
			VANS_LOG("RenderDoc capture is inactive: renderdoc.dll was not injected");
			m_MissingModuleLogged = true;
		}
		return false;
	}

	const auto getAPI = reinterpret_cast<RenderDocGetAPI>(
		GetProcAddress(renderDocModule, "RENDERDOC_GetAPI"));
	if (!getAPI || getAPI(RenderDocAPIVersion160, &m_API) != 1 || !m_API)
	{
		m_API = nullptr;
		m_Message = "renderdoc.dll is loaded, but its in-application API 1.6 is unavailable.";
		VANS_LOG_WARN(m_Message);
		return false;
	}

	RenderDocAPI* api = API(m_API);
	api->SetCaptureFilePathTemplate("Captures/RenderDoc/ForestEngine");
	api->SetCaptureOptionU32(RenderDocOptionAPIValidation, 1);
	api->SetCaptureOptionU32(RenderDocOptionDebugOutputMute, 0);
	api->SetCaptureOptionU32(RenderDocOptionReferenceAllResources, 0);

	int major = 0;
	int minor = 0;
	int patch = 0;
	api->GetAPIVersion(&major, &minor, &patch);
	m_Message = "RenderDoc is ready.";
	VANS_LOG("RenderDoc in-application API " << major << "." << minor << "." << patch
		<< " initialized; capture path is Captures/RenderDoc/ForestEngine");
	return true;
}

void VansGraphics::VansRenderDocCapture::BindVulkanWindow(void* vulkanInstance, void* nativeWindow)
{
	if (!Initialize() || !vulkanInstance || !nativeWindow)
	{
		return;
	}

	// Vulkan dispatchable handles start with their loader dispatch table pointer;
	// RenderDoc uses that pointer to identify a Vulkan API instance.
	m_VulkanDevicePointer = *reinterpret_cast<void**>(vulkanInstance);
	m_NativeWindow = nativeWindow;
	API(m_API)->SetActiveWindow(m_VulkanDevicePointer, m_NativeWindow);
	VANS_LOG("RenderDoc capture bound to the main Vulkan window");
}

bool VansGraphics::VansRenderDocCapture::CaptureNextFrame()
{
	if (!Initialize())
	{
		return false;
	}

	if (m_VulkanDevicePointer && m_NativeWindow)
	{
		API(m_API)->SetActiveWindow(m_VulkanDevicePointer, m_NativeWindow);
	}
	API(m_API)->TriggerCapture();
	m_Message = "Capture requested. The next presented frame will be saved.";
	VANS_LOG("RenderDoc capture requested for the next frame");
	return true;
}

bool VansGraphics::VansRenderDocCapture::OpenReplayUI()
{
	if (!Initialize())
	{
		return false;
	}

	RenderDocAPI* api = API(m_API);
	if (api->IsTargetControlConnected() != 0 && api->ShowReplayUI() != 0)
	{
		return true;
	}
	return api->LaunchReplayUI(1, nullptr) != 0;
}

bool VansGraphics::VansRenderDocCapture::SetAPIValidationEnabled(bool enabled)
{
	return Initialize() &&
		API(m_API)->SetCaptureOptionU32(RenderDocOptionAPIValidation, enabled ? 1u : 0u) == 1;
}

bool VansGraphics::VansRenderDocCapture::SetReferenceAllResources(bool enabled)
{
	return Initialize() &&
		API(m_API)->SetCaptureOptionU32(RenderDocOptionReferenceAllResources, enabled ? 1u : 0u) == 1;
}

VansGraphics::VansRenderDocStatus VansGraphics::VansRenderDocCapture::QueryStatus()
{
	VansRenderDocStatus status;
	status.available = Initialize();
	status.message = m_Message;
	if (!status.available)
	{
		return status;
	}

	RenderDocAPI* api = API(m_API);
	api->GetAPIVersion(&status.apiMajor, &status.apiMinor, &status.apiPatch);
	status.targetControlConnected = api->IsTargetControlConnected() != 0;
	status.frameCapturing = api->IsFrameCapturing() != 0;
	status.apiValidationEnabled = api->GetCaptureOptionU32(RenderDocOptionAPIValidation) != 0;
	status.referenceAllResources = api->GetCaptureOptionU32(RenderDocOptionReferenceAllResources) != 0;
	status.captureCount = api->GetNumCaptures();

	if (const char* pathTemplate = api->GetCaptureFilePathTemplate())
	{
		status.capturePathTemplate = pathTemplate;
	}

	if (status.captureCount > 0)
	{
		std::uint32_t pathLength = 0;
		const std::uint32_t captureIndex = status.captureCount - 1;
		if (api->GetCapture(captureIndex, nullptr, &pathLength, nullptr) != 0 && pathLength > 0)
		{
			std::vector<char> path(pathLength);
			if (api->GetCapture(captureIndex, path.data(), &pathLength, nullptr) != 0)
			{
				status.lastCapturePath = path.data();
			}
		}
	}

	return status;
}
