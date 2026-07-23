#pragma once

#include "EngineCommandContext.h"

#include "../Public/IEngineEditorAPI.h"

#include <memory>
#include <vector>

namespace Vans::EditorAPI
{
	class EngineAPIImpl;
}

class VansScriptContext;

namespace Vans::EditorAPI
{
	class EngineAPIImpl final : public IEngineEditorAPI
	{
	public:
		EngineAPIImpl() = default;
		EngineAPIImpl(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device);

		void BindRuntime(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device);
		void BindGlobalRuntime(RuntimeRenderDeviceHandle device);
		void BindScriptContext(VansScriptContext* scriptContext);

		SceneDataSnapshot GetSceneSnapshot() const override;
		EntityDataSnapshot GetEntitySnapshot(EntityId id) const override;
		ComponentDataSnapshot GetComponentSnapshot(EntityId entityId, ComponentId componentId) const override;

		void SubmitCommand(std::unique_ptr<IEngineCommand> command) override;
		void BreakCommandMergeGroup() override;

		std::vector<AssetEntry> QueryAssets(AssetTypeFilter filter) const override;
		AssetMetaSnapshot GetAssetMeta(AssetId id) const override;
		ProjectBrowserRootSnapshot GetProjectBrowserRoot() const override;
		AssetDragPayload CreateAssetDragPayload(const std::string& assetPath) override;
		AssetGuidResolution ResolveAssetGuid(const std::string& assetGuid) const override;
		AssetRefreshResult RefreshProjectAsset(const std::string& assetPath, bool importIfMissing) override;
		std::vector<RecentProjectEntry> GetRecentProjects() const override;
		ProjectOpenResult OpenProject(const ProjectOpenRequest& request) override;
		void CloseProject() override;
		float GetProjectPhysicsFixedTimeStep() const override;
		bool SetCurrentProjectScenePath(const std::string& scenePath) override;
		void ScanProjectAssets() override;

		EditorTextureHandle GetViewportTexture(ViewportId id) const override;
		RenderTexturePreview GetViewportPreview(ViewportId id) const override;
		FSRSettingsSnapshot GetFSRSettings() const override;
		void SetFSRSettings(FSRUpscaleMode mode, float sharpness) override;
		void SetSceneViewportExtent(std::uint32_t width, std::uint32_t height) override;
		std::vector<RenderTexturePreview> QueryRenderTexturePreviews(RenderTextureFilter filter) const override;
		void RequestPunctualShadowDebugPreview() override;
		PunctualShadowDebugSnapshot GetPunctualShadowDebugSnapshot() const override;
		void ApplyPunctualScreenSpaceShadowSettings(
			const PunctualScreenSpaceShadowSettingsSnapshot& settings) override;
		RenderBackendDiagnostics GetRenderBackendDiagnostics() const override;
		std::vector<ShaderProgramSourceSnapshot> QueryShaderProgramSources() const override;
		ShaderCandidateApplyResult ApplyShaderCandidateAtRenderSafePoint(
			const ShaderCandidatePackage& package) override;
		void RebuildReflectionProbeResources() override;
		void BakeQueuedReflectionProbesNow() override;
		ReflectionProbeSettingsSnapshot GetReflectionProbeSettings() const override;
		void ApplyReflectionProbeSettings(const ReflectionProbeSettingsSnapshot& settings) override;
		GIInspectorSettingsSnapshot GetGISettings() const override;
		void ApplyGISettings(const GIInspectorSettingsSnapshot& settings) override;
		GIProbeDebugSnapshot CaptureGIProbeDebugSnapshot(std::uint32_t stride, float exposure) override;
		GIProbeDebugSnapshot GetGIProbeDebugSnapshot() const override;
		RenderTexturePreview RequestGIRTPreview(
			std::uint32_t mode,
			std::uint32_t zSlice,
			std::uint32_t rayIndex,
			float exposure,
			float positionScale) override;
		void GenerateAutoReflectionProbes() override;
		void ClearAutoReflectionProbes() override;
		void RequestReflectionProbeBakeAll() override;
		void RequestReflectionProbeBake(std::uint32_t probeIndex) override;
		void SaveReflectionProbeConfiguration() override;
		void ConvertReflectionProbeToManual(std::uint32_t probeIndex) override;
		WaterSettingsSnapshot GetWaterSettings() const override;
		void ApplyWaterSettings(const WaterSettingsSnapshot& settings) override;
		WaterRuntimeStats GetWaterRuntimeStats() const override;
		MeshLoadResult EnsureProjectMeshLoaded(const MeshLoadRequest& request) override;
		ProjectMeshInfoSnapshot GetProjectMeshInfo(const std::string& meshName) const override;
		void RegisterProjectMeshAlias(const ProjectMeshAliasRequest& request) override;
		std::string GetDefaultMaterialAssetName() const override;
		RuntimeModelEntityCreateResult CreateRuntimeModelEntity(const RuntimeModelEntityCreateRequest& request) override;
		ModelAssetPlacementPayload PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request) override;
		RuntimeEntityDestroyResult DestroyRuntimeEntityByName(const RuntimeEntityDestroyRequest& request) override;
		std::string MakeUniqueRuntimeEntityName(const std::string& baseName) const override;
		std::string GetProjectRootPath() const override;
		bool IsRuntimeSceneReady() const override;
		bool IsRuntimeSceneSwitching() const override;
		bool LoadRuntimeScene(const std::string& scenePath, RuntimeSceneLoadMode mode) override;
		void UnloadRuntimeScene() override;
		bool AreRuntimeProjectResourcesLoaded() const override;
		void UnloadRuntimeProjectResources() override;
		bool LoadRuntimeProjectAssetsForScene(const std::string& scenePath) override;
		VehicleDebugSnapshot GetVehicleDebugSnapshot() const override;
		bool HasAnimationDebugNodes() const override;
		VansGraphics::VansAnimationNode* FindRuntimeAnimationNodeByEntityGuid(const std::string& entityGuid) const override;
		MotionMatchingDebugSnapshot GetMotionMatchingDebugSnapshot() const override;
		void SetFootIKDebugVisualization(bool enabled) override;
		FootIKDebugSnapshot GetFootIKDebugSnapshot() const override;
		TerrainSettingsSnapshot GetTerrainSettings() const override;
		void ApplyTerrainSettings(const TerrainSettingsSnapshot& settings) override;
		void ApplyRuntimeEntityPatchJson(const std::string& entityJson) override;
		void SetRuntimeComponentEnabled(const std::string& entityGuid, const std::string& componentType, bool enabled) override;
		bool ApplyRuntimeMaterialAssetPatch(
			const std::string& assetPath,
			const std::string& assetRootJson,
			const std::string& changedPointer) override;

		void CommitLightingChanges() override;
		LightingSettingsSnapshot GetLightingSettings() const override;
		void ApplyLightingSettings(const LightingSettingsSnapshot& settings) override;
		PostProcessSettingsSnapshot GetPostProcessSettings() const override;
		void ApplyPostProcessSettings(const PostProcessSettingsSnapshot& settings) override;
		FogSettings GetFogSettings() const override;
		void ApplyFogSettings(const FogSettings& settings) override;
		FogVolumeSettings GetFogVolumeSettings() const override;
		void ApplyFogVolumeSettings(const FogVolumeSettings& settings) override;
		CloudSettings GetCloudSettings() const override;
		void ApplyCloudSettings(const CloudSettings& settings) override;
		void ResetCloudSettings() override;
		void CommitCloudSettings() override;

		EnginePlayState GetPlayState() const override;
		void SetPlayState(EnginePlayState state) override;
		EntityId RaycastScene(const Ray& ray) const override;
		std::string PickRuntimeEntity(const Ray& ray) const override;
		RuntimeTransformSnapshot GetRuntimeTransform(const std::string& entityGuid) const override;
		void ApplyRuntimeTransform(const RuntimeTransformEdit& edit) override;
		std::vector<RuntimeMultiMeshGroupSnapshot> BuildRuntimeMultiMeshExpansionSnapshot() override;
		void SetRuntimePhysicsFixedTimeStep(float deltaTimeSeconds) override;
		std::vector<std::string> GetRuntimeCollisionLayerNames() const override;
		void InstallRuntimeVehiclePhysicsStepCallback() override;
		void ClearRuntimePhysicsStepCallback() override;
		bool IsRuntimePhysicsRunning() const override;
		void StartRuntimePhysicsIfNeeded() override;
		void PauseRuntimePhysics() override;
		void ResumeRuntimePhysics() override;
		void StepRuntimeVehicle(float deltaTimeSeconds) override;
		void SetRuntimeVehicleInput(float throttle, float brake, float steer, float handbrake) override;
		void SyncRuntimePhysicsTransforms() override;
		void FlushRuntimeCharacterControllerTransforms() override;
		void UpdateRuntimeNonCameraScripts() override;
		void UpdateRuntimeCameraScripts() override;
		void InitializeRuntimeScripts() override;
		void SetupRuntimeScriptProjectVenv(const std::string& projectRootPath) override;
		void ReloadRuntimeScripts() override;
		void ReloadRuntimeScriptModule() override;

		bool CanUndo() const override;
		bool CanRedo() const override;
		void Undo() override;
		void Redo() override;

		void Subscribe(IEngineEventListener* listener) override;
		void Unsubscribe(IEngineEventListener* listener) override;

	private:
		RenderTexturePreview BuildReflectionProbePreview(RenderTextureFilter filter) const;
		RenderTexturePreview BuildWaterTexturePreview(RenderTextureFilter filter) const;

		RuntimeSceneHandle m_Scene = nullptr;
		RuntimeRenderDeviceHandle m_Device = nullptr;
		VansScriptContext* m_ScriptContext = nullptr;
		EnginePlayState m_PlayState = EnginePlayState::Edit;
		std::vector<IEngineEventListener*> m_Listeners;
		std::vector<std::unique_ptr<IEngineCommand>> m_UndoStack;
		std::vector<std::unique_ptr<IEngineCommand>> m_RedoStack;
		bool m_AllowNextCommandMerge = true;
		GIProbeDebugSnapshot m_GIProbeDebugSnapshot;
	};
}
