#include "VansEditorRuntimeHost.h"

#include "../EngineCore/AudioCore/VansAudioDeviceConfig.h"
#include "../EngineCore/EditorCore/VansEditorWindow.h"
#include "../EngineCore/EditorCore/VansEditorAssetSaveService.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/PhysicsCore/VansPhysics.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/ProjectSystem/VansEnginePaths.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneProjectResourceBuilder.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/RenderCore/VansRenderSystem.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RuntimeUI/Public/VansUISystem.h"
#include "../EngineCore/Util/VansLog.h"
#include "../EngineCore/VansTimer.h"

#include <cstdlib>
#include <string>
#include <utility>

using namespace VansGraphics;

namespace VansEngine
{
VansEditorRuntimeHost::VansEditorRuntimeHost() = default;

VansEditorRuntimeHost::~VansEditorRuntimeHost()
{
	Stop();
}

Vans::EditorAPI::IEngineEditorAPI& VansEditorRuntimeHost::AccessEditorAPI(
	Vans::VansSceneDocument* sceneDocument,
	Vans::VansSceneEditService* sceneEditService)
{
	m_AuthoringSceneDocument = sceneDocument;
	m_AuthoringSceneEdits = sceneEditService;
	m_EditorAPI->BindSceneAuthoring(
		m_AuthoringSceneDocument && m_AuthoringSceneEdits ? this : nullptr);
	return *m_EditorAPI;
}

Vans::VansAuthoringSaveResult VansEditorRuntimeHost::SaveAssetDocument(
	const std::shared_ptr<Vans::VansOpenAssetDocument>& document)
{
	if (!m_EditorAPI)
		return { false, "Editor API is unavailable during authoring save." };
	const Vans::VansAssetSaveResult saved =
		Vans::VansEditorAssetSaveService::Get().SaveAsset(*m_EditorAPI, document);
	return { static_cast<bool>(saved), saved.message };
}

Vans::VansSceneDocument* VansEditorRuntimeHost::SceneDocument() const
{
	return m_AuthoringSceneDocument;
}

Vans::VansSceneAuthoringResult VansEditorRuntimeHost::SetSceneValue(
	const Vans::DocumentPropertyPath& path,
	Vans::VansSerializedValue value)
{
	if (!m_AuthoringSceneEdits)
		return { false, "Editor scene authoring service is unavailable." };
	const Vans::SceneEditResult edited = m_AuthoringSceneEdits->Set(path, std::move(value));
	return { edited.success, edited.message };
}

bool VansEditorRuntimeHost::Start()
{
	if (m_State != Vans::VansRuntimeLifecycleState::Stopped)
		return m_State == Vans::VansRuntimeLifecycleState::GraphicsReady;

	m_State = Vans::VansRuntimeLifecycleState::Starting;
	std::string engineRoot;
	std::string engineRootError;
	if (!Vans::VansEnginePaths::DiscoverEngineRoot(engineRoot, engineRootError) ||
		!Vans::VansProjectManager::Get().ConfigureEngineRoot(engineRoot, engineRootError))
	{
		VANS_LOG_ERROR("[ForestEngine] " << engineRootError);
		ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState::Failed);
		return false;
	}
	if (!StartCoreSystems())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize core systems!");
		ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState::Failed);
		return false;
	}

	if (!StartGraphicsSystem())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize graphics systems!");
		ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState::Failed);
		return false;
	}

	m_State = Vans::VansRuntimeLifecycleState::GraphicsReady;
	return true;
}

bool VansEditorRuntimeHost::StartCoreSystems()
{
	VANS_LOG("[ForestEngine] Initializing core systems...");
	std::string error;
	if (!m_Services.Start(VansAudioDeviceConfig{}, error))
	{
		VANS_LOG_ERROR("[ForestEngine] " << error);
		return false;
	}
	VANS_LOG("[ForestEngine] Job system initialized");
	VANS_LOG("[ForestEngine] Physics system initialized successfully");
	if (!m_Services.IsAudioAvailable())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize audio system (OpenAL)! 空间音效将无法播放");
		// 音频不可用不会阻止编辑器启动。
	}

	return true;
}

bool VansEditorRuntimeHost::StartGraphicsSystem()
{
	VANS_LOG("[ForestEngine] Initializing graphics system...");
	if (!m_EditorAPI)
		m_EditorAPI = std::make_unique<Vans::EditorAPI::EngineAPIImpl>();
	m_EditorAPI->BindAuthoringSaveHost(this);
	VansEditorWindow::AttachEditorAPIHost(*this);
	m_EditorAPIAttached = true;

	if (!VansEditorWindow::CreateVansEditorWindow(1280 * 2, 720 * 2, VULKAN))
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to create editor window!");
		return false;
	}
	m_WindowCreated = true;
	VANS_LOG("[ForestEngine] Editor window created");

	if (const char* autoProject = std::getenv("FORESTENGINE_AUTOPEN_PROJECT"))
		VansEditorWindow::QueueProjectOpenForAutomation(autoProject);
	if (std::getenv("FORESTENGINE_AUTOPEN_SKELETON_DEBUG"))
		VansEditorWindow::EnableSkeletonDebugForAutomation();

	auto device = std::make_unique<VansVKDevice>(VkExtent2D{1280, 720}, &VansEditorWindow::NativeWindow());
	if (!device->IsInitialized())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize Vulkan device!");
		return false;
	}
	m_Device = std::move(device);
	::m_GraphicsDevice = m_Device.get();

	m_GuiBackend = std::make_unique<VansGraphicsGUIBackEnd>();
	::m_GUIBackEnd = m_GuiBackend.get();
	VANS_LOG("[ForestEngine] Vulkan backend initialized");

	m_Scene = std::make_unique<VansScene>();
	::m_Scene = m_Scene.get();
	m_EditorAPI->BindRuntime(m_Scene.get(), m_Device.get());
	VANS_LOG("[ForestEngine] Scene system initialized");

	RegisterEngineShaders();
	m_ShadersRegistered = true;
	if (!VansSceneProjectResourceBuilder::LoadShadersFromRegistry(
			*m_Scene, Vans::VansProjectManager::Get().GetPathResolver().GetEngineRoot(),
			m_Device->GetLogicDevice()))
	{
		VANS_LOG_ERROR("[ForestEngine] One or more required engine shaders failed to load");
		return false;
	}
	VANS_LOG("[ForestEngine] Engine shaders registered and loaded");

	if (const char* cookedOutput = std::getenv("FORESTENGINE_COOKED_SHADER_OUTPUT"))
	{
		std::string error;
		if (!VansShaderManager::Get().ExportCookedShaderArtifacts(cookedOutput, error))
		{
			VANS_LOG_ERROR("[ShaderCook] " << error);
			return false;
		}
		VANS_LOG("[ShaderCook] Exported active shader artifacts to " << cookedOutput);
	}

	return true;
}

void VansEditorRuntimeHost::Run()
{
	if (m_State != Vans::VansRuntimeLifecycleState::GraphicsReady)
		return;
	if (std::getenv("FORESTENGINE_COOK_SHADERS_AND_EXIT") != nullptr)
	{
		VANS_LOG("[ShaderCook] Cook-only launch completed; skipping main loop");
		return;
	}

	m_Camera = std::make_unique<VansCamera>(m_Device.get());
	m_State = Vans::VansRuntimeLifecycleState::Running;
	if (!RunEditorLoop())
	{
		ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState::Failed);
		return;
	}
	m_State = Vans::VansRuntimeLifecycleState::Quiesced;
}

bool VansEditorRuntimeHost::RunEditorLoop()
{
	VANS_LOG("[ForestEngine] Starting main engine loop...");
	m_Scene->InjectCamera(m_Camera.get());

	m_RenderSystem = std::make_unique<VansRenderSystem>(*m_Device, *m_Scene, true);
	if (!m_RenderSystem->InitializeFrameExecution())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize render-system frame execution");
		m_RenderSystem.reset();
		return false;
	}
	m_Scene->BindRenderThreadTransactionExecutor(m_RenderSystem.get());
	m_EditorAPI->BindRenderSystem(m_RenderSystem.get());

	VansRuntime::VansUIInitDesc uiDesc{};
	uiDesc.m_Width = m_RenderSystem->GetRenderWidth();
	uiDesc.m_Height = m_RenderSystem->GetRenderHeight();
	m_UiInitializationAttempted = true;
	if (!VansRuntime::VansUISystem::Get().InitializeWithDevice(uiDesc, m_Device.get()))
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize Runtime UI frontend");
		ResetRenderExecutionAfterFailure();
		return false;
	}

	if (Vans::VansProjectManager::Get().IsProjectLoaded())
	{
		std::string themeError;
		if (!VansRuntime::VansUISystem::Get().ApplyGlobalThemeFromMemory(themeError))
		{
			VANS_LOG_ERROR("[ForestEngine] Failed to apply Runtime UI global theme: " << themeError);
			ResetRenderExecutionAfterFailure();
			return false;
		}
	}

	if (Vans::VansProjectManager::Get().IsProjectLoaded())
	{
		const VansEngine::VansPhysicsTiming& timing =
			Vans::VansProjectManager::Get().GetProjectSettings().GetPhysicsTiming();
		if (!VansEngine::VansPhysicsSystem::GetInstance().SetTiming(timing))
		{
			VANS_LOG_ERROR("[ForestEngine] Failed to apply project physics timing");
			ResetRenderExecutionAfterFailure();
			return false;
		}
		VansGraphics::VansTimer::SetPhysicsDeltaTime(
			static_cast<double>(timing.fixedTimeStep));
	}
	m_Services.StartSimulation();
	VANS_LOG("[ForestEngine] Physics simulation started");

	VansEditorWindow::StartEditorLoop(*m_Camera, *m_RenderSystem);
	if (!m_RenderSystem->Quiesce())
		VANS_LOG_ERROR("[ForestEngine] Failed to quiesce render thread after main loop.");
	VANS_LOG("[ForestEngine] Main loop finished");
	return true;
}

void VansEditorRuntimeHost::ResetRenderExecutionAfterFailure()
{
	if (m_Scene)
		m_Scene->BindRenderThreadTransactionExecutor(nullptr);
	if (m_RenderSystem)
	{
		m_RenderSystem->ShutdownFrameExecution();
		m_RenderSystem.reset();
	}
}

bool VansEditorRuntimeHost::HasAcquiredResources() const
{
	return m_Services.HasActiveResources() || m_WindowCreated || m_ShadersRegistered ||
		m_UiInitializationAttempted || m_EditorAPIAttached || m_Device || m_GuiBackend || m_Scene || m_Camera || m_RenderSystem;
}

void VansEditorRuntimeHost::Stop()
{
	if (m_State == Vans::VansRuntimeLifecycleState::Stopped || m_State == Vans::VansRuntimeLifecycleState::Stopping)
		return;
	if (!HasAcquiredResources())
	{
		m_State = Vans::VansRuntimeLifecycleState::Stopped;
		return;
	}
	ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState::Stopped);
}

void VansEditorRuntimeHost::ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState finalState)
{
	m_State = Vans::VansRuntimeLifecycleState::Stopping;
	VANS_LOG("[ForestEngine] Shutting down engine systems...");

	if (m_Services.IsStarted())
	{
		m_Services.PauseSimulation();
		VANS_LOG("[ForestEngine] Physics simulation paused for shutdown");
	}

	auto* vkDevice = m_Device.get();
	if (m_RenderSystem)
	{
		if (!m_RenderSystem->WaitForIdle())
			VANS_LOG_ERROR("[ForestEngine] Render thread failed to reach idle before scene teardown.");
		else
			VANS_LOG("[ForestEngine] Render thread idle before scene teardown");
	}
	else if (vkDevice)
	{
		vkDevice->WaitForDevice();
		VANS_LOG("[ForestEngine] Vulkan device idle before scene teardown");
	}

	if (m_EditorAPI)
		m_EditorAPI->ReleaseRuntimePreviewResources();

	if (m_Scene)
	{
		m_Scene->UnloadScene(vkDevice);
		m_Scene->UnloadProjectResources(vkDevice);
		m_Scene->BindRenderThreadTransactionExecutor(nullptr);
		m_Scene.reset();
		::m_Scene = nullptr;
		VANS_LOG("[ForestEngine] Scene unloaded");
	}
	if (m_EditorAPI)
		m_EditorAPI->BindRuntime(nullptr, nullptr);

	m_Camera.reset();
	VANS_LOG("[ForestEngine] Camera released");

	// Scene and preview teardown can queue ImGui descriptor retirement. Execute
	// those callbacks while both Vulkan and the GUI backend are still alive.
	if (vkDevice)
	{
		if (!vkDevice->DrainDeferredDeletesAfterDeviceIdle())
			VANS_LOG_ERROR("[ForestEngine] Failed to drain deferred GPU deletes before GUI teardown");
		else
			VANS_LOG("[ForestEngine] Deferred GPU deletes drained before GUI teardown");
	}

	if (m_RenderSystem)
	{
		if (m_GuiBackend &&
			!m_RenderSystem->ExecuteRenderThreadTransaction(m_GuiBackend->CreateRenderThreadShutdown()))
		{
			VANS_LOG_ERROR("[ForestEngine] Render-thread GUI backend shutdown failed");
		}
		m_RenderSystem->ShutdownFrameExecution();
		m_RenderSystem.reset();
		VANS_LOG("[ForestEngine] Render thread stopped after Vulkan-dependent scene teardown");
	}
	if (m_EditorAPI)
		m_EditorAPI->BindRenderSystem(nullptr);

	if (m_UiInitializationAttempted)
	{
		VansRuntime::VansUISystem::Get().Shutdown();
		m_UiInitializationAttempted = false;
		VANS_LOG("[ForestEngine] Runtime UI system shutdown complete");
	}

	if (m_Services.HasActiveResources())
	{
		m_Services.ShutdownServices();
		VANS_LOG("[ForestEngine] Physics system shutdown complete");
		VANS_LOG("[ForestEngine] Audio system shutdown complete");
	}

	if (m_GuiBackend)
	{
		m_GuiBackend->ShutdownBackEnd();
		VANS_LOG("[ForestEngine] GUI backend shutdown complete");
	}

	if (m_WindowCreated)
	{
		if (m_EditorAPI)
			m_EditorAPI->BindSceneAuthoring(nullptr);
		m_AuthoringSceneDocument = nullptr;
		m_AuthoringSceneEdits = nullptr;
		VansEditorWindow::DestroyVansEditorWindow();
		m_WindowCreated = false;
		VANS_LOG("[ForestEngine] Editor window released");
	}
	if (m_EditorAPIAttached)
	{
		VansEditorWindow::DetachEditorAPIHost(*this);
		m_EditorAPIAttached = false;
	}
	if (m_EditorAPI)
		m_EditorAPI->BindAuthoringSaveHost(nullptr);
	m_EditorAPI.reset();

	if (m_ShadersRegistered)
	{
		VansShaderManager::Get().Clear();
		m_ShadersRegistered = false;
		VANS_LOG("[ForestEngine] Shader manager released");
	}

	if (m_Device)
	{
		m_Device.reset();
		::m_GraphicsDevice = nullptr;
		VANS_LOG("[ForestEngine] Graphics device released");
	}

	if (m_GuiBackend)
	{
		m_GuiBackend.reset();
		::m_GUIBackEnd = nullptr;
		VANS_LOG("[ForestEngine] GUI backend released");
	}

	if (m_Services.HasActiveResources())
	{
		m_Services.ShutdownJobs();
		VANS_LOG("[ForestEngine] Job system shutdown complete");
	}

	m_State = finalState;
	VANS_LOG("[ForestEngine] Engine shutdown complete!");
}
} // namespace VansEngine
