#pragma once

#include "../EngineCore/RuntimeCore/VansRuntimeLifecycle.h"
#include "../EngineCore/RuntimeCore/VansRuntimeServices.h"
#include "../EngineCore/EditorCore/IVansEditorAPIHost.h"
#include "../EngineCore/AuthoringCore/IVansAuthoringSaveHost.h"
#include "../EngineCore/AuthoringCore/IVansSceneAuthoringHost.h"

#include <memory>

namespace Vans::EditorAPI
{
class EngineAPIImpl;
}

namespace VansGraphics
{
class VansCamera;
class VansGraphicsGUIBackEnd;
class VansRenderSystem;
class VansScene;
class VansVKDevice;
} // namespace VansGraphics

namespace VansEngine
{
class VansEditorRuntimeHost final :
	public Vans::IVansEditorAPIHost,
	public Vans::IVansAuthoringSaveHost,
	public Vans::IVansSceneAuthoringHost
{
  public:
	VansEditorRuntimeHost();
	~VansEditorRuntimeHost();

	VansEditorRuntimeHost(const VansEditorRuntimeHost&) = delete;
	VansEditorRuntimeHost& operator=(const VansEditorRuntimeHost&) = delete;

	bool Start();
	void Run();
	void Stop();

	Vans::VansRuntimeLifecycleState GetState() const
	{
		return m_State;
	}

	Vans::EditorAPI::IEngineEditorAPI& AccessEditorAPI(
		Vans::VansSceneDocument* sceneDocument,
		Vans::VansSceneEditService* sceneEditService) override;
	Vans::VansAuthoringSaveResult SaveAssetDocument(
		const std::shared_ptr<Vans::VansOpenAssetDocument>& document) override;
	Vans::VansSceneDocument* SceneDocument() const override;
	Vans::VansSceneAuthoringResult SetSceneValue(
		const Vans::DocumentPropertyPath& path,
		Vans::VansSerializedValue value) override;

  private:
	bool StartCoreSystems();
	bool StartGraphicsSystem();
	bool RunEditorLoop();
	bool HasAcquiredResources() const;
	void ReleaseAcquiredResources(Vans::VansRuntimeLifecycleState finalState);
	void ResetRenderExecutionAfterFailure();

	Vans::VansRuntimeLifecycleState m_State = Vans::VansRuntimeLifecycleState::Stopped;
	Vans::VansRuntimeServices m_Services;
	bool m_WindowCreated = false;
	bool m_ShadersRegistered = false;
	bool m_UiInitializationAttempted = false;
	bool m_EditorAPIAttached = false;
	Vans::VansSceneDocument* m_AuthoringSceneDocument = nullptr;
	Vans::VansSceneEditService* m_AuthoringSceneEdits = nullptr;
	std::unique_ptr<Vans::EditorAPI::EngineAPIImpl> m_EditorAPI;
	std::unique_ptr<VansGraphics::VansVKDevice> m_Device;
	std::unique_ptr<VansGraphics::VansGraphicsGUIBackEnd> m_GuiBackend;
	std::unique_ptr<VansGraphics::VansScene> m_Scene;
	std::unique_ptr<VansGraphics::VansCamera> m_Camera;
	std::unique_ptr<VansGraphics::VansRenderSystem> m_RenderSystem;
};
} // namespace VansEngine
