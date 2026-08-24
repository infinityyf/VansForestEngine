#pragma once

#include "EngineDTOs.h"
#include "IEngineCommand.h"

#include <memory>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class IEngineEditorAPI
	{
	public:
		virtual ~IEngineEditorAPI() = default;

		virtual SceneDataSnapshot GetSceneSnapshot() const = 0;
		virtual EntityDataSnapshot GetEntitySnapshot(EntityId id) const = 0;
		virtual ComponentDataSnapshot GetComponentSnapshot(EntityId entityId, ComponentId componentId) const = 0;

		virtual void SubmitCommand(std::unique_ptr<IEngineCommand> command) = 0;
		virtual void BreakCommandMergeGroup() = 0;

		virtual std::vector<AssetEntry> QueryAssets(AssetTypeFilter filter) const = 0;
		virtual AssetMetaSnapshot GetAssetMeta(AssetId id) const = 0;
		virtual ProjectBrowserRootSnapshot GetProjectBrowserRoot() const = 0;
		virtual AssetDragPayload CreateAssetDragPayload(const std::string& assetPath) = 0;
		virtual AssetGuidResolution ResolveAssetGuid(const std::string& assetGuid) const = 0;
		virtual ProjectAssetCreateResult CreateProjectAsset(const ProjectAssetCreateRequest& request) = 0;
		virtual AssetRefreshResult RefreshProjectAsset(const std::string& assetPath, bool importIfMissing) = 0;
		virtual GAFEditorDocumentSnapshot OpenGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult SetGAFAssetField(const GAFEditorFieldEditRequest& request) = 0;
		virtual GAFEditorOperationResult ResetGAFAssetField(
			const std::string& sourcePath, const std::string& fieldPath) = 0;
		virtual GAFEditorOperationResult EditGAFAssetArray(const GAFEditorArrayEditRequest& request) = 0;
		virtual std::vector<GAFGraphNodeTypeSnapshot> GetGAFGraphNodeCatalog() const = 0;
		virtual GAFEditorOperationResult EditGAFGraph(const GAFGraphEditRequest& request) = 0;
		virtual GAFEditorOperationResult UndoGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult RedoGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult RevertGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult SaveGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFSemanticDiffResult DiffGAFAsset(
			const std::string& sourcePath, const std::string& baselineCanonicalJson) = 0;
		virtual GAFProjectConfigurationSnapshot GetGAFProjectConfiguration() const = 0;
		virtual std::vector<std::string> GetGAFTagCatalog() const = 0;
		virtual GAFProjectConfigurationResult ApplyGAFProjectConfiguration(
			const GAFProjectConfigurationSnapshot& configuration) = 0;
		virtual GAFRuntimeDebugSnapshot GetGAFRuntimeDebugSnapshot() = 0;
		virtual GAFDebugCommandResult ControlGAFDebugger(const GAFDebugCommand& command) = 0;
		virtual GAFTraceCommandResult ControlGAFTrace(const GAFTraceCommand& command) = 0;
		virtual GAFSimulationResult SimulateGAFAction(const GAFSimulationRequest& request) = 0;
		virtual std::vector<RecentProjectEntry> GetRecentProjects() const = 0;
		virtual ProjectOpenResult OpenProject(const ProjectOpenRequest& request) = 0;
		virtual void CloseProject() = 0;
		virtual ProjectConfigSnapshot GetProjectConfigSnapshot() const = 0;
		virtual ProjectConfigEditResult SetProjectDefaultScene(const std::string& sceneRelativePath) = 0;
		virtual ProjectConfigEditResult SetProjectPathField(ProjectPathField field, const std::string& relativePath) = 0;
		virtual ProjectConfigEditResult SetProjectScriptSearchPaths(const std::vector<std::string>& paths) = 0;
		virtual ProjectConfigEditResult SetProjectAssetDirectory(const std::string& key, const std::string& relativePath) = 0;
		virtual ProjectConfigEditResult SaveProjectConfig() = 0;
		virtual float GetProjectPhysicsFixedTimeStep() const = 0;
		virtual ProjectConfigEditResult SetProjectPhysicsFixedTimeStep(float fixedTimeStep) = 0;
		virtual bool SetCurrentProjectScenePath(const std::string& scenePath) = 0;
		virtual void ScanProjectAssets() = 0;

		virtual EditorTextureHandle GetViewportTexture(ViewportId id) const = 0;
		virtual RenderTexturePreview GetViewportPreview(ViewportId id) const = 0;
		virtual UpscalerSettingsSnapshot GetUpscalerSettings() const = 0;
		virtual std::vector<UpscalerCapabilitiesSnapshot> GetUpscalerCapabilities() const = 0;
		virtual ApplyUpscalerSettingsResult ApplyUpscalerSettings(
			const UpscalerSettingsSnapshot& settings) = 0;
		virtual CommandRecordingSettingsSnapshot GetCommandRecordingSettings() const = 0;
		virtual void SetCommandRecordingSettings(const CommandRecordingSettingsSnapshot& settings) = 0;
		virtual std::vector<RenderTexturePreview> QueryRenderTexturePreviews(RenderTextureFilter filter) const = 0;
		virtual void RequestPunctualShadowDebugPreview() = 0;
		virtual PunctualShadowDebugSnapshot GetPunctualShadowDebugSnapshot() const = 0;
		virtual void ApplyPunctualScreenSpaceShadowSettings(
			const PunctualScreenSpaceShadowSettingsSnapshot& settings) = 0;
		virtual RenderBackendDiagnostics GetRenderBackendDiagnostics(bool includeRenderGraphSummary = false) const = 0;
		virtual PipelineRegistryStatsSnapshot GetPipelineRegistryStats() const = 0;
		virtual RenderDocStatusSnapshot GetRenderDocStatus() const = 0;
		virtual void SetRenderDocAPIValidationEnabled(bool enabled) = 0;
		virtual void SetRenderDocReferenceAllResources(bool enabled) = 0;
		virtual void CaptureNextRenderDocFrame() = 0;
		virtual void OpenRenderDocUI() = 0;
		virtual UIDocumentOpenResult OpenUIDocument(const std::string& path) = 0;
		virtual void CloseUIDocument(UIDocumentId documentId) = 0;
		virtual void SetUIDocumentVisible(UIDocumentId documentId, bool visible) = 0;
		virtual UIDocumentSnapshot GetUIDocumentSnapshot(UIDocumentId documentId) const = 0;
		virtual UIDiagnosticsSnapshot GetUIDiagnostics(UIDocumentId documentId) const = 0;
		virtual UIPreviewResult RequestUIPreview(const UIPreviewRequest& request) = 0;
		virtual EditorTextureHandle GetUIPreviewTexture(UIPreviewId id) const = 0;
		virtual std::vector<ShaderProgramSourceSnapshot> QueryShaderProgramSources() const = 0;
		virtual ShaderCandidateApplyResult ApplyShaderCandidateAtRenderSafePoint(
			const ShaderCandidatePackage& package) = 0;
		virtual void RebuildReflectionProbeResources() = 0;
		virtual void BakeQueuedReflectionProbesNow() = 0;
		virtual ReflectionProbeSettingsSnapshot GetReflectionProbeSettings() const = 0;
		virtual void ApplyReflectionProbeSettings(const ReflectionProbeSettingsSnapshot& settings) = 0;
		virtual GIInspectorSettingsSnapshot GetGISettings() const = 0;
		virtual void ApplyGISettings(const GIInspectorSettingsSnapshot& settings) = 0;
		virtual GIProbeDebugSnapshot CaptureGIProbeDebugSnapshot(std::uint32_t stride, float exposure) = 0;
		virtual GIProbeDebugSnapshot GetGIProbeDebugSnapshot() const = 0;
		virtual MainCameraHiZCullDebugSnapshot GetMainCameraHiZCullDebugSnapshot() const = 0;
		virtual std::vector<RenderTexturePreview> RequestGIRTPreviews(
			std::uint32_t zSlice,
			std::uint32_t rayIndex,
			float exposure,
			float positionScale) = 0;
		virtual void GenerateAutoReflectionProbes() = 0;
		virtual void ClearAutoReflectionProbes() = 0;
		virtual void RequestReflectionProbeBakeAll() = 0;
		virtual void RequestReflectionProbeBake(std::uint32_t probeIndex) = 0;
		virtual void SaveReflectionProbeConfiguration() = 0;
		virtual void ConvertReflectionProbeToManual(std::uint32_t probeIndex) = 0;
		virtual WaterSettingsSnapshot GetWaterSettings() const = 0;
		virtual void ApplyWaterSettings(const WaterSettingsSnapshot& settings) = 0;
		virtual WaterRuntimeStats GetWaterRuntimeStats() const = 0;
		virtual MeshLoadResult EnsureProjectMeshLoaded(const MeshLoadRequest& request) = 0;
		virtual ProjectMeshInfoSnapshot GetProjectMeshInfo(const std::string& meshName) const = 0;
		virtual void RegisterProjectMeshAlias(const ProjectMeshAliasRequest& request) = 0;
		virtual std::string GetDefaultMaterialAssetName() const = 0;
		virtual RuntimeSceneEntitiesCreateResult CreateRuntimeSceneEntities(const RuntimeSceneEntitiesCreateRequest& request) = 0;
		virtual ModelAssetPlacementPayload PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request) = 0;
		virtual RuntimeEntityDestroyResult DestroyRuntimeEntity(const RuntimeEntityDestroyRequest& request) = 0;
		virtual RuntimeEntityReparentResult ReparentRuntimeEntity(const RuntimeEntityReparentRequest& request) = 0;
		virtual std::string MakeUniqueRuntimeEntityName(const std::string& baseName) const = 0;
		virtual std::string GetProjectRootPath() const = 0;
		virtual bool IsRuntimeSceneReady() const = 0;
		virtual bool IsRuntimeSceneSwitching() const = 0;
		virtual RuntimeSceneLoadResult LoadRuntimeScene(const std::string& scenePath, RuntimeSceneLoadMode mode) = 0;
		virtual void UnloadRuntimeScene() = 0;
		virtual bool AreRuntimeProjectResourcesLoaded() const = 0;
		virtual void UnloadRuntimeProjectResources() = 0;
		virtual bool LoadRuntimeProjectAssetsForScene(const std::string& scenePath) = 0;
		virtual VehicleDebugSnapshot GetVehicleDebugSnapshot() const = 0;
		virtual bool HasAnimationDebugNodes() const = 0;
		virtual AnimationAssetBindingSnapshot GetAnimationAssetBinding(const std::string& entityGuid) const = 0;
		virtual MotionMatchingDebugSnapshot GetMotionMatchingDebugSnapshot() const = 0;
		virtual SceneSkeletonHierarchySnapshot GetSceneSkeletonHierarchy(
			const std::string& entityGuidFilter) const = 0;
		virtual SceneSkeletonNodePoseSnapshot GetSceneSkeletonNodePose(
			const SceneSkeletonNodePoseRequest& request) const = 0;
		virtual SkeletonDebugSnapshot GetSkeletonDebugSnapshot(const std::string& entityGuidFilter) const = 0;
		virtual AssetSkeletonSnapshot GetAssetSkeletonSnapshot(const std::string& assetGuid) const = 0;
		virtual AnimatorDocumentDecodeResult DecodeAnimatorDocument(
			const std::string& canonicalJson) const = 0;
		virtual AnimatorDocumentEncodeResult EncodeAnimatorDocument(
			const AnimatorDocumentDTO& document) const = 0;
		virtual BoneMaskDocumentDecodeResult DecodeBoneMaskDocument(
			const std::string& canonicalJson) const = 0;
		virtual BoneMaskDocumentEncodeResult EncodeBoneMaskDocument(
			const BoneMaskDocumentDTO& document) const = 0;
		virtual BoneMaskCompileResult CompileBoneMaskDocument(
			const BoneMaskDocumentDTO& document,
			const AssetSkeletonSnapshot& skeleton) const = 0;
		virtual AnimationPreviewCreateResult CreateAnimationPreview(
			const AnimationPreviewCreateRequest& request) = 0;
		virtual AnimationPreviewUpdateResult UpdateAnimationPreviewDefinition(
			const AnimationPreviewDefinitionUpdate& update) = 0;
		virtual bool SetAnimationPreviewPlayback(const AnimationPreviewPlaybackRequest& request) = 0;
		virtual bool SetAnimationPreviewParameter(const AnimationPreviewParameterValue& value) = 0;
		virtual bool SwitchAnimationPreviewGraphSet(const AnimationPreviewGraphSetRequest& request) = 0;
		virtual bool TriggerAnimationPreviewSlot(const AnimationPreviewSlotRequest& request) = 0;
		virtual bool SetAnimationPreviewViewport(const AnimationPreviewViewportRequest& request) = 0;
		virtual void TickAnimationPreview(AnimationPreviewSessionId sessionId, float deltaTime) = 0;
		virtual AnimationPreviewSnapshot GetAnimationPreviewSnapshot(
			AnimationPreviewSessionId sessionId) const = 0;
		virtual void DestroyAnimationPreview(AnimationPreviewSessionId sessionId) = 0;
		virtual TimelinePreviewResult StartTimelinePreview(const TimelinePreviewStartRequest& request) = 0;
		virtual TimelinePreviewResult ConfigureTimelinePreviewPlayback(
			const TimelinePreviewPlaybackRequest& request) = 0;
		virtual TimelinePreviewResult PlayTimelinePreview(const std::string& previewId) = 0;
		virtual TimelinePreviewResult PauseTimelinePreview(const std::string& previewId) = 0;
		virtual TimelinePreviewResult SeekTimelinePreview(
			const std::string& previewId, std::int64_t tick, bool safeEdges) = 0;
		virtual TimelinePreviewResult StopTimelinePreview(const std::string& previewId) = 0;
		virtual TimelinePreviewResult GetTimelinePreview(const std::string& previewId) const = 0;
		virtual TerrainSettingsSnapshot GetTerrainSettings() const = 0;
		virtual void ApplyTerrainSettings(const TerrainSettingsSnapshot& settings) = 0;
		virtual bool ApplyRuntimeEntityPreviewChange(const RuntimeEntityPreviewChange& change) = 0;
		virtual bool ApplyRuntimeMaterialPreviewChange(const RuntimeMaterialPreviewChange& change) = 0;

		virtual void CommitLightingChanges() = 0;
		virtual LightingSettingsSnapshot GetLightingSettings() const = 0;
		virtual void ApplyLightingSettings(const LightingSettingsSnapshot& settings) = 0;
		virtual PostProcessSettingsSnapshot GetPostProcessSettings() const = 0;
		virtual void ApplyPostProcessSettings(const PostProcessSettingsSnapshot& settings) = 0;
		virtual void CommitPostProcessSettings() = 0;
		virtual FogSettings GetFogSettings() const = 0;
		virtual void ApplyFogSettings(const FogSettings& settings) = 0;
		virtual void CommitHeightFogSettings() = 0;
		virtual FogVolumeSettings GetFogVolumeSettings() const = 0;
		virtual void ApplyFogVolumeSettings(const FogVolumeSettings& settings) = 0;
		virtual void CommitVolumetricFogSettings() = 0;
		virtual CloudSettings GetCloudSettings() const = 0;
		virtual void ApplyCloudSettings(const CloudSettings& settings) = 0;
		virtual void ResetCloudSettings() = 0;
		virtual void CommitCloudSettings() = 0;
		virtual std::vector<ScenePropertyEdit> ConsumeScenePropertyEdits() = 0;

		virtual EnginePlayState GetPlayState() const = 0;
		virtual void SetPlayState(EnginePlayState state) = 0;
		virtual EntityId RaycastScene(const Ray& ray) const = 0;
		virtual std::string PickRuntimeEntity(const Ray& ray) const = 0;
		virtual RuntimeTransformSnapshot GetRuntimeTransform(const std::string& entityGuid) const = 0;
		virtual void ApplyRuntimeTransform(const RuntimeTransformEdit& edit) = 0;
		virtual std::vector<RuntimeMultiMeshGroupSnapshot> BuildRuntimeMultiMeshExpansionSnapshot() = 0;
		virtual AudioBusDebugSnapshot GetAudioBusDebugSnapshot() const = 0;
		virtual void SetAudioBusGain(const std::string& busName, float gain) = 0;
		virtual void SetAudioBusMuted(const std::string& busName, bool muted) = 0;
		virtual void SetAudioBusSoloed(const std::string& busName, bool soloed) = 0;
		virtual void SetAudioMaxActiveVoices(int maxActiveVoices) = 0;
		virtual void SetRuntimePhysicsFixedTimeStep(float deltaTimeSeconds) = 0;
		virtual std::vector<std::string> GetRuntimeCollisionLayerNames() const = 0;
		virtual void InstallRuntimeVehiclePhysicsStepCallback() = 0;
		virtual void ClearRuntimePhysicsStepCallback() = 0;
		virtual bool IsRuntimePhysicsRunning() const = 0;
		virtual void StartRuntimePhysicsIfNeeded() = 0;
		virtual void PauseRuntimePhysics() = 0;
		virtual void ResumeRuntimePhysics() = 0;
		virtual void StepRuntimeVehicle(float deltaTimeSeconds) = 0;
		virtual void SetRuntimeVehicleInput(float throttle, float brake, float steer, float handbrake) = 0;
		virtual void SyncRuntimePhysicsTransforms() = 0;
		virtual void PrepareRuntimeCharacterLocomotion(double deltaSeconds) = 0;
		virtual void FlushRuntimeCharacterControllerTransforms() = 0;
		virtual void UpdateRuntimeNonCameraScripts() = 0;
		virtual void UpdateRuntimeActionsEarly(double deltaSeconds) = 0;
		virtual void RunRuntimeActionLateContinuation() = 0;
		virtual void UpdateRuntimeTimelinesPostScript(double deltaSeconds) = 0;
		virtual void BeginRuntimeCameraControlFrame() = 0;
		virtual void UpdateRuntimeCameraScripts() = 0;
		virtual void CaptureRuntimeCameraControlBase() = 0;
		virtual void UpdateRuntimeTimelinesCamera(double deltaSeconds) = 0;
		virtual void UpdateTimelinePreviewsPostScript(double deltaSeconds) = 0;
		virtual void UpdateTimelinePreviewsCamera(double deltaSeconds) = 0;
		virtual void ResolveRuntimeCameraControlFrame() = 0;
		virtual void InitializeRuntimeScripts() = 0;
		virtual void SetupRuntimeScriptProjectVenv(const std::string& projectRootPath) = 0;
		virtual void ReloadRuntimeScripts() = 0;
		virtual void ReloadRuntimeScriptModule() = 0;

		virtual bool CanUndo() const = 0;
		virtual bool CanRedo() const = 0;
		virtual void Undo() = 0;
		virtual void Redo() = 0;

	};
}
