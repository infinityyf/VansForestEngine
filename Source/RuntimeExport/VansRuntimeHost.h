#pragma once

#include "../EngineCore/RuntimeCore/VansRuntimeLifecycle.h"
#include "../EngineCore/RuntimeCore/VansRuntimeServices.h"

#include <filesystem>
#include <memory>
#include <string>

namespace VansGraphics
{
class VansCamera;
class VansRenderSystem;
class VansScene;
class VansVKDevice;
} // namespace VansGraphics

class VansScriptContext;

namespace Vans
{
class VansRuntimeWindow;

class VansRuntimeHost final
{
  public:
	VansRuntimeHost();
	~VansRuntimeHost();

	VansRuntimeHost(const VansRuntimeHost&) = delete;
	VansRuntimeHost& operator=(const VansRuntimeHost&) = delete;

	bool LoadPackage(const char* manifestPath);
	bool OpenWindow(int width, int height, const char* title);
	bool LoadCurrentScene();
	bool Tick(float deltaTimeSeconds);
	bool RenderFrame();
	bool ShouldClose() const;
	void Shutdown();

	const std::string& GetLastError() const
	{
		return m_LastError;
	}
	const std::string& GetLoadedScene() const
	{
		return m_LoadedScene;
	}
	const std::string& GetProjectRoot() const
	{
		return m_ProjectRoot;
	}
	bool IsProjectLoaded() const
	{
		return IsProjectReady();
	}

  private:
	bool IsProjectReady() const;
	bool IsGraphicsReady() const;
	bool IsSceneReady() const
	{
		return m_State == VansRuntimeLifecycleState::SceneReady;
	}
	bool InitializeCore();
	void RollbackSceneSession();
	void ShutdownGraphics();
	void SetError(std::string message);

	VansRuntimeServices m_Services;
	VansRuntimeLifecycleState m_State = VansRuntimeLifecycleState::Stopped;
	std::filesystem::path m_ContentRoot;
	std::string m_LoadedScene;
	std::string m_ResourcePlan;
	std::string m_ProjectRoot;
	std::string m_LastError;
	std::unique_ptr<VansRuntimeWindow> m_Window;
	std::unique_ptr<VansGraphics::VansVKDevice> m_Device;
	std::unique_ptr<VansGraphics::VansRenderSystem> m_RenderSystem;
	std::unique_ptr<VansGraphics::VansScene> m_Scene;
	std::unique_ptr<VansGraphics::VansCamera> m_Camera;
	std::unique_ptr<VansScriptContext> m_ScriptContext;
	bool m_FrameExecutionStarted = false;
};
} // namespace Vans
