#include "VansStreamlineRuntime.h"

#include "../../EngineCore/Util/VansLog.h"

#if defined(VANS_HAS_STREAMLINE)
#include <sl.h>
#include <sl_dlss.h>
#include <sl_security.h>
#endif

#include <array>
#include <filesystem>
#include <sstream>
#include <vector>

namespace
{
#if defined(VANS_HAS_STREAMLINE)
	template<typename T>
	T* Resolve(HMODULE module, const char* name)
	{
		return reinterpret_cast<T*>(GetProcAddress(module, name));
	}

	sl::DLSSMode ToNativeMode(VansGraphics::VansStreamlineDLSSMode mode)
	{
		switch (mode)
		{
		case VansGraphics::VansStreamlineDLSSMode::DLAA: return sl::DLSSMode::eDLAA;
		case VansGraphics::VansStreamlineDLSSMode::Quality: return sl::DLSSMode::eMaxQuality;
		case VansGraphics::VansStreamlineDLSSMode::Balanced: return sl::DLSSMode::eBalanced;
		case VansGraphics::VansStreamlineDLSSMode::Performance: return sl::DLSSMode::eMaxPerformance;
		case VansGraphics::VansStreamlineDLSSMode::UltraPerformance: return sl::DLSSMode::eUltraPerformance;
		default: return sl::DLSSMode::eOff;
		}
	}

	void StreamlineLogMessage(sl::LogType type, const char* message)
	{
		if (message == nullptr || *message == '\0')
			return;
		switch (type)
		{
		case sl::LogType::eError:
			VANS_LOG_ERROR("[Streamline] " << message);
			break;
		case sl::LogType::eWarn:
			VANS_LOG_WARN("[Streamline] " << message);
			break;
		default:
			// Streamline emits substantial per-context informational output at its
			// default level. Engine diagnostics already expose context and dispatch
			// state, so only surface actionable SDK warnings and errors here.
			break;
		}
	}

	sl::float4x4 ToRowMajor(const glm::mat4& value)
	{
		sl::float4x4 result{};
		for (std::uint32_t row = 0; row < 4; ++row)
		{
			result[row] = sl::float4(
				value[0][row], value[1][row], value[2][row], value[3][row]);
		}
		return result;
	}

	sl::Resource ToResource(
		const VansGraphics::VansStreamlineImageResource& source)
	{
		sl::Resource resource(
			sl::ResourceType::eTex2d,
			reinterpret_cast<void*>(source.image),
			nullptr,
			reinterpret_cast<void*>(source.view),
			static_cast<std::uint32_t>(source.layout));
		resource.width = source.createInfo.extent.width;
		resource.height = source.createInfo.extent.height;
		resource.nativeFormat = static_cast<std::uint32_t>(source.createInfo.format);
		resource.mipLevels = source.createInfo.mipLevels;
		resource.arrayLayers = source.createInfo.arrayLayers;
		resource.flags = source.createInfo.flags;
		resource.usage = source.createInfo.usage;
		return resource;
	}
#endif
}

namespace VansGraphics
{
	VansStreamlineRuntime& VansStreamlineRuntime::Get()
	{
		static VansStreamlineRuntime runtime;
		return runtime;
	}

	bool VansStreamlineRuntime::ResolveCoreFunctions()
	{
#if defined(VANS_HAS_STREAMLINE)
		m_Init = Resolve<PFun_slInit>(m_Module, "slInit");
		m_Shutdown = Resolve<PFun_slShutdown>(m_Module, "slShutdown");
		m_IsFeatureLoaded = Resolve<PFun_slIsFeatureLoaded>(m_Module, "slIsFeatureLoaded");
		m_GetFeatureFunction = Resolve<PFun_slGetFeatureFunction>(m_Module, "slGetFeatureFunction");
		m_GetFeatureVersion = Resolve<PFun_slGetFeatureVersion>(m_Module, "slGetFeatureVersion");
		m_SetTagForFrame = Resolve<PFun_slSetTagForFrame>(m_Module, "slSetTagForFrame");
		m_SetConstants = Resolve<PFun_slSetConstants>(m_Module, "slSetConstants");
		m_EvaluateFeature = Resolve<PFun_slEvaluateFeature>(m_Module, "slEvaluateFeature");
		m_GetNewFrameToken = Resolve<PFun_slGetNewFrameToken>(m_Module, "slGetNewFrameToken");
		m_FreeResources = Resolve<PFun_slFreeResources>(m_Module, "slFreeResources");
		return m_Init != nullptr && m_Shutdown != nullptr &&
			m_IsFeatureLoaded != nullptr && m_GetFeatureFunction != nullptr &&
			m_GetFeatureVersion != nullptr && m_SetTagForFrame != nullptr &&
			m_SetConstants != nullptr && m_EvaluateFeature != nullptr &&
			m_GetNewFrameToken != nullptr && m_FreeResources != nullptr;
#else
		return false;
#endif
	}

	bool VansStreamlineRuntime::ResolveDLSSFunctions()
	{
#if defined(VANS_HAS_STREAMLINE)
		auto* getFeatureFunction =
			reinterpret_cast<PFun_slGetFeatureFunction*>(m_GetFeatureFunction);
		if (getFeatureFunction == nullptr)
			return false;
		void* optimal = nullptr;
		void* options = nullptr;
		if (getFeatureFunction(
			sl::kFeatureDLSS, "slDLSSGetOptimalSettings", optimal) != sl::Result::eOk ||
			getFeatureFunction(
			sl::kFeatureDLSS, "slDLSSSetOptions", options) != sl::Result::eOk)
		{
			return false;
		}
		m_DLSSGetOptimalSettings = optimal;
		m_DLSSSetOptions = options;
		return optimal != nullptr && options != nullptr;
#else
		return false;
#endif
	}

	HMODULE VansStreamlineRuntime::TryInitializeVulkanLoader()
	{
#if !defined(VANS_HAS_STREAMLINE)
		m_UnavailableReason = "Streamline SDK is not compiled in";
		return nullptr;
#else
		if (m_Initialized && m_Module != nullptr)
			return m_Module;

		std::array<wchar_t, 32768> executablePath{};
		const DWORD length = GetModuleFileNameW(
			nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (length == 0 || length >= executablePath.size())
		{
			m_UnavailableReason = "Cannot resolve executable directory for Streamline";
			return nullptr;
		}
		const std::filesystem::path runtimeDirectory =
			std::filesystem::path(executablePath.data()).parent_path();
		const std::array<const wchar_t*, 4> requiredModules =
		{
			L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss.dll", L"nvngx_dlss.dll"
		};
		for (const wchar_t* moduleName : requiredModules)
		{
			const std::filesystem::path modulePath = runtimeDirectory / moduleName;
			if (!std::filesystem::is_regular_file(modulePath))
			{
				m_UnavailableReason = "Required Streamline runtime binary is missing: " +
					modulePath.filename().string();
				return nullptr;
			}
		}
		// The official secure-loading contract applies the embedded Streamline
		// certificate check to sl.* modules. The interposer then performs secure
		// plugin loading itself. nvngx_dlss.dll is Authenticode-signed by NVIDIA,
		// but intentionally does not carry Streamline's secondary certificate.
		for (const wchar_t* moduleName :
			{ L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss.dll" })
		{
			const std::filesystem::path modulePath = runtimeDirectory / moduleName;
			if (!sl::security::verifyEmbeddedSignature(modulePath.c_str()))
			{
				m_UnavailableReason = "Streamline runtime signature verification failed: " +
					modulePath.filename().string();
				return nullptr;
			}
		}

		const std::filesystem::path interposerPath =
			runtimeDirectory / L"sl.interposer.dll";
		m_Module = LoadLibraryExW(
			interposerPath.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m_Module == nullptr || !ResolveCoreFunctions())
		{
			m_UnavailableReason = "Cannot load Streamline core API";
			if (m_Module != nullptr)
				FreeLibrary(m_Module);
			m_Module = nullptr;
			return nullptr;
		}

		const wchar_t* pluginPath = runtimeDirectory.c_str();
		const sl::Feature features[] = { sl::kFeatureDLSS };
		sl::Preferences preferences{};
		preferences.pathsToPlugins = &pluginPath;
		preferences.numPathsToPlugins = 1;
		preferences.featuresToLoad = features;
		preferences.numFeaturesToLoad = 1;
		preferences.flags =
			sl::PreferenceFlags::eDisableDebugText |
			sl::PreferenceFlags::eUseFrameBasedResourceTagging;
		preferences.logLevel = sl::LogLevel::eDefault;
		preferences.logMessageCallback = StreamlineLogMessage;
		preferences.engine = sl::EngineType::eCustom;
		preferences.engineVersion = "ForestEngine-1";
		preferences.projectId = "af65ac87-c163-4bc6-bc41-9e1ea730a9c2";
		preferences.renderAPI = sl::RenderAPI::eVulkan;
		auto* init = reinterpret_cast<PFun_slInit*>(m_Init);
		if (init(preferences, sl::kSDKVersion) != sl::Result::eOk)
		{
			m_UnavailableReason = "slInit rejected the Vulkan/DLSS configuration";
			FreeLibrary(m_Module);
			m_Module = nullptr;
			return nullptr;
		}

		m_Initialized = true;
		m_UnavailableReason = "DLSS device capability has not been queried";
		VANS_LOG("[Streamline] Initialized SDK "
			<< SL_VERSION_MAJOR << '.' << SL_VERSION_MINOR << '.' << SL_VERSION_PATCH);
		return m_Module;
#endif
	}

	void VansStreamlineRuntime::RefreshDeviceCapabilities()
	{
		m_DLSSAvailable = false;
#if defined(VANS_HAS_STREAMLINE)
		if (!m_Initialized)
			return;
		bool loaded = false;
		auto* isLoaded = reinterpret_cast<PFun_slIsFeatureLoaded*>(m_IsFeatureLoaded);
		if (isLoaded(sl::kFeatureDLSS, loaded) != sl::Result::eOk || !loaded)
		{
			m_UnavailableReason = "Streamline DLSS plugin is not supported by the active device/driver";
			VANS_LOG_WARN("[Streamline] " << m_UnavailableReason);
			return;
		}
		if (!ResolveDLSSFunctions())
		{
			m_UnavailableReason = "Streamline DLSS feature entry points are unavailable";
			VANS_LOG_WARN("[Streamline] " << m_UnavailableReason);
			return;
		}
		sl::FeatureVersion version{};
		auto* getVersion = reinterpret_cast<PFun_slGetFeatureVersion*>(m_GetFeatureVersion);
		if (getVersion(sl::kFeatureDLSS, version) == sl::Result::eOk)
		{
			std::ostringstream stream;
			stream << version.versionSL.toStr();
			m_FeatureVersion = stream.str();
		}
		else
		{
			m_FeatureVersion = "Streamline 2.11.1";
		}
		m_DLSSAvailable = true;
		m_UnavailableReason.clear();
		VANS_LOG("[Streamline] DLSS available, featureVersion=" << m_FeatureVersion);
#endif
	}

	bool VansStreamlineRuntime::QueryOptimalSettings(
		VansStreamlineDLSSMode mode,
		std::uint32_t outputWidth,
		std::uint32_t outputHeight,
		VansStreamlineOptimalSettings& settings)
	{
		settings = {};
#if !defined(VANS_HAS_STREAMLINE)
		return false;
#else
		if (!m_DLSSAvailable || m_DLSSGetOptimalSettings == nullptr ||
			outputWidth == 0 || outputHeight == 0)
		{
			return false;
		}
		sl::DLSSOptions options{};
		options.mode = ToNativeMode(mode);
		options.outputWidth = outputWidth;
		options.outputHeight = outputHeight;
		options.colorBuffersHDR = sl::Boolean::eTrue;
		sl::DLSSOptimalSettings native{};
		auto* query = reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(
			m_DLSSGetOptimalSettings);
		if (query(options, native) != sl::Result::eOk)
			return false;
		settings.renderWidth = native.optimalRenderWidth;
		settings.renderHeight = native.optimalRenderHeight;
		settings.minRenderWidth = native.renderWidthMin;
		settings.minRenderHeight = native.renderHeightMin;
		settings.maxRenderWidth = native.renderWidthMax;
		settings.maxRenderHeight = native.renderHeightMax;
		return settings.renderWidth > 0 && settings.renderHeight > 0;
#endif
	}

	bool VansStreamlineRuntime::ConfigureDLSS(
		VansStreamlineDLSSMode mode,
		std::uint32_t outputWidth,
		std::uint32_t outputHeight,
		bool useExternalExposure)
	{
#if !defined(VANS_HAS_STREAMLINE)
		return false;
#else
		if (!m_DLSSAvailable || m_DLSSSetOptions == nullptr)
			return false;
		sl::DLSSOptions options{};
		options.mode = ToNativeMode(mode);
		options.outputWidth = outputWidth;
		options.outputHeight = outputHeight;
		options.colorBuffersHDR = sl::Boolean::eTrue;
		options.useAutoExposure = useExternalExposure
			? sl::Boolean::eFalse : sl::Boolean::eTrue;
		options.alphaUpscalingEnabled = sl::Boolean::eFalse;
		options.dlaaPreset = sl::DLSSPreset::ePresetK;
		options.qualityPreset = sl::DLSSPreset::ePresetK;
		options.balancedPreset = sl::DLSSPreset::ePresetK;
		options.performancePreset = sl::DLSSPreset::ePresetM;
		options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
		auto* setOptions = reinterpret_cast<PFun_slDLSSSetOptions*>(m_DLSSSetOptions);
		return setOptions(sl::ViewportHandle(0u), options) == sl::Result::eOk;
#endif
	}

	bool VansStreamlineRuntime::EvaluateDLSS(
		const VansStreamlineDLSSDispatch& dispatch)
	{
#if !defined(VANS_HAS_STREAMLINE)
		return false;
#else
		if (!m_DLSSAvailable || dispatch.commandBuffer == VK_NULL_HANDLE ||
			dispatch.color.image == VK_NULL_HANDLE ||
			dispatch.depth.image == VK_NULL_HANDLE ||
			dispatch.motionVectors.image == VK_NULL_HANDLE ||
			dispatch.output.image == VK_NULL_HANDLE)
		{
			return false;
		}
		auto* getFrameToken = reinterpret_cast<PFun_slGetNewFrameToken*>(m_GetNewFrameToken);
		sl::FrameToken* frameToken = nullptr;
		if (getFrameToken(frameToken, &dispatch.frameIndex) != sl::Result::eOk ||
			frameToken == nullptr)
		{
			return false;
		}
		const sl::ViewportHandle viewport(0u);

		sl::Resource color = ToResource(dispatch.color);
		sl::Resource depth = ToResource(dispatch.depth);
		sl::Resource motion = ToResource(dispatch.motionVectors);
		sl::Resource output = ToResource(dispatch.output);
		sl::Resource exposure = ToResource(dispatch.exposure);
		const sl::Extent renderExtent{ 0, 0, dispatch.renderWidth, dispatch.renderHeight };
		const sl::Extent outputExtent{ 0, 0, dispatch.outputWidth, dispatch.outputHeight };
		const sl::Extent exposureExtent{ 0, 0, 1, 1 };
		std::vector<sl::ResourceTag> tags;
		tags.emplace_back(&color, sl::kBufferTypeScalingInputColor,
			sl::ResourceLifecycle::eOnlyValidNow, &renderExtent);
		tags.emplace_back(&output, sl::kBufferTypeScalingOutputColor,
			sl::ResourceLifecycle::eOnlyValidNow, &outputExtent);
		tags.emplace_back(&depth, sl::kBufferTypeDepth,
			sl::ResourceLifecycle::eOnlyValidNow, &renderExtent);
		tags.emplace_back(&motion, sl::kBufferTypeMotionVectors,
			sl::ResourceLifecycle::eOnlyValidNow, &renderExtent);
		if (dispatch.exposure.image != VK_NULL_HANDLE)
		{
			tags.emplace_back(&exposure, sl::kBufferTypeExposure,
				sl::ResourceLifecycle::eOnlyValidNow, &exposureExtent);
		}
		auto* commandBuffer = reinterpret_cast<sl::CommandBuffer*>(dispatch.commandBuffer);
		auto* setTags = reinterpret_cast<PFun_slSetTagForFrame*>(m_SetTagForFrame);
		if (setTags(*frameToken, viewport, tags.data(),
			static_cast<std::uint32_t>(tags.size()), commandBuffer) != sl::Result::eOk)
		{
			return false;
		}
		const glm::mat4 currentViewProjection = dispatch.projection * dispatch.view;
		const glm::mat4 clipToPrevious =
			dispatch.previousViewProjection * glm::inverse(currentViewProjection);
		sl::Constants constants{};
		constants.cameraViewToClip = ToRowMajor(dispatch.projection);
		constants.clipToCameraView = ToRowMajor(glm::inverse(dispatch.projection));
		constants.clipToPrevClip = ToRowMajor(clipToPrevious);
		constants.prevClipToClip = ToRowMajor(glm::inverse(clipToPrevious));
		constants.jitterOffset = sl::float2(
			dispatch.jitterPixels.x, dispatch.jitterPixels.y);
		constants.mvecScale = sl::float2(
			dispatch.motionVectorScale.x,
			dispatch.motionVectorScale.y);
		constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
		constants.cameraPos = sl::float3(
			dispatch.cameraPosition.x,
			dispatch.cameraPosition.y,
			dispatch.cameraPosition.z);
		constants.cameraUp = sl::float3(
			dispatch.cameraUp.x, dispatch.cameraUp.y, dispatch.cameraUp.z);
		constants.cameraRight = sl::float3(
			dispatch.cameraRight.x, dispatch.cameraRight.y, dispatch.cameraRight.z);
		constants.cameraFwd = sl::float3(
			dispatch.cameraForward.x,
			dispatch.cameraForward.y,
			dispatch.cameraForward.z);
		constants.cameraNear = dispatch.cameraNear;
		constants.cameraFar = dispatch.cameraFar;
		constants.cameraFOV = dispatch.cameraFovRadians;
		constants.cameraAspectRatio = static_cast<float>(dispatch.renderWidth) /
			static_cast<float>(dispatch.renderHeight);
		constants.depthInverted = sl::Boolean::eFalse;
		constants.cameraMotionIncluded = sl::Boolean::eTrue;
		constants.motionVectors3D = sl::Boolean::eFalse;
		constants.motionVectorsDilated = sl::Boolean::eFalse;
		constants.motionVectorsJittered = sl::Boolean::eFalse;
		constants.reset = dispatch.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		auto* setConstants = reinterpret_cast<PFun_slSetConstants*>(m_SetConstants);
		if (setConstants(constants, *frameToken, viewport) != sl::Result::eOk)
			return false;

		const sl::BaseStructure* inputs[] = { &viewport };
		auto* evaluate = reinterpret_cast<PFun_slEvaluateFeature*>(m_EvaluateFeature);
		return evaluate(
			sl::kFeatureDLSS, *frameToken, inputs, 1, commandBuffer) == sl::Result::eOk;
#endif
	}

	void VansStreamlineRuntime::ReleaseDLSSResources()
	{
#if defined(VANS_HAS_STREAMLINE)
		if (m_DLSSAvailable && m_FreeResources != nullptr)
		{
			auto* freeResources = reinterpret_cast<PFun_slFreeResources*>(m_FreeResources);
			freeResources(sl::kFeatureDLSS, sl::ViewportHandle(0u));
		}
#endif
	}

	void VansStreamlineRuntime::ShutdownBeforeVulkan()
	{
#if defined(VANS_HAS_STREAMLINE)
		if (m_Initialized && m_Shutdown != nullptr)
		{
			auto* shutdown = reinterpret_cast<PFun_slShutdown*>(m_Shutdown);
			shutdown();
		}
#endif
		m_Initialized = false;
		m_DLSSAvailable = false;
		m_DLSSGetOptimalSettings = nullptr;
		m_DLSSSetOptions = nullptr;
		m_Init = nullptr;
		m_Shutdown = nullptr;
		m_IsFeatureLoaded = nullptr;
		m_GetFeatureFunction = nullptr;
		m_GetFeatureVersion = nullptr;
		m_SetTagForFrame = nullptr;
		m_SetConstants = nullptr;
		m_EvaluateFeature = nullptr;
		m_GetNewFrameToken = nullptr;
		m_FreeResources = nullptr;
		m_UnavailableReason = "Streamline has been shut down";
	}
}
