#pragma once

namespace Vans::EditorAPI
{
	class IRuntimePhysicsEditorAPI
	{
	public:
		virtual ~IRuntimePhysicsEditorAPI() = default;
		virtual void InstallRuntimeVehiclePhysicsStepCallback() = 0;
		virtual void ClearRuntimePhysicsStepCallback() = 0;
		virtual bool IsRuntimePhysicsRunning() const = 0;
		virtual void StartRuntimePhysicsIfNeeded() = 0;
		virtual void PauseRuntimePhysics() = 0;
		virtual void ResumeRuntimePhysics() = 0;
		virtual void SyncRuntimePhysicsTransforms() = 0;
		virtual void PrepareRuntimeCharacterLocomotion(double deltaSeconds) = 0;
		virtual void FlushRuntimeCharacterControllerTransforms() = 0;
	};
}
