#pragma once

namespace Vans
{
struct VansRuntimeFrameContext
{
	double m_DeltaSeconds = 0.0;
};

struct VansRuntimeFramePolicy
{
	bool m_IsSceneReady = false;
	bool m_IsSimulationRunning = false;
	bool m_IsGameplayActive = false;
	bool m_IsCameraControlActive = false;
};

class IVansRuntimeFramePort
{
  public:
	virtual ~IVansRuntimeFramePort() = default;

	virtual void SyncPhysicsTransforms(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateNonCameraScripts(const VansRuntimeFrameContext& context) = 0;
	virtual void AdvanceCameraRuntime(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateActionsEarly(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateAI(const VansRuntimeFrameContext& context) = 0;
	virtual void PrepareCharacterLocomotion(const VansRuntimeFrameContext& context) = 0;
	virtual void FlushCharacterControllerTransforms(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateTimelinesPostScript(const VansRuntimeFrameContext& context) = 0;
	virtual void RunActionLateContinuation(const VansRuntimeFrameContext& context) = 0;
	virtual void BeginCameraControlFrame(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateCameraScripts(const VansRuntimeFrameContext& context) = 0;
	virtual void CaptureCameraControlBase(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateTimelinesCamera(const VansRuntimeFrameContext& context) = 0;
	virtual void ResolveCameraControlFrame(const VansRuntimeFrameContext& context) = 0;
};

class IVansRuntimeFramePreviewPort
{
  public:
	virtual ~IVansRuntimeFramePreviewPort() = default;

	virtual void UpdatePostScriptControllers(const VansRuntimeFrameContext& context) = 0;
	virtual void UpdateCameraControllers(const VansRuntimeFrameContext& context) = 0;
};

class VansRuntimeFrameScheduler
{
  public:
	static void RunGameplay(
		IVansRuntimeFramePort& runtimePort,
		IVansRuntimeFramePreviewPort* previewPort,
		const VansRuntimeFramePolicy& policy,
		const VansRuntimeFrameContext& context);
};
} // namespace Vans
