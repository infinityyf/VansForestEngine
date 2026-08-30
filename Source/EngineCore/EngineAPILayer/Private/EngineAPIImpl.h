#pragma once

#include "EngineCommandContext.h"

#include "../Public/IEngineEditorAPI.h"
#include "../../GameplayActionDebug/VansGameplayActionDebug.h"
#include "../../RuntimeUI/Public/VansUIRuntimeHandles.h"
#include "../../RenderCore/VulkanCore/VansVKImage.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Vans::EditorAPI
{
	class EngineAPIImpl;
}

class VansScriptContext;
namespace VansRuntime
{
	class VansUIDocument;
}

namespace VansGraphics
{
	class VansRenderSystem;
}

namespace Vans
{
	class VansGameplayTraceRecorder;
	class VansGameplayReplaySession;
}

namespace Vans::EditorAPI
{
	class EngineAPIImpl final : public IEngineEditorAPI
	{
	public:
		EngineAPIImpl() = default;
		EngineAPIImpl(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device);

		void BindRuntime(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device);
		void BindGlobalRuntime(RuntimeRenderDeviceHandle device);
		void BindRenderSystem(VansGraphics::VansRenderSystem* renderSystem);
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
		ProjectAssetCreateResult CreateProjectAsset(const ProjectAssetCreateRequest& request) override;
		AssetRefreshResult RefreshProjectAsset(const std::string& assetPath, bool importIfMissing) override;
		GAFEditorDocumentSnapshot OpenGAFAsset(const std::string& sourcePath) override;
		GAFEditorOperationResult SetGAFAssetField(const GAFEditorFieldEditRequest& request) override;
		GAFEditorOperationResult ResetGAFAssetField(
			const std::string& sourcePath, const std::string& fieldPath) override;
		GAFEditorOperationResult EditGAFAssetArray(const GAFEditorArrayEditRequest& request) override;
		std::vector<GAFGraphNodeTypeSnapshot> GetGAFGraphNodeCatalog() const override;
		GAFEditorOperationResult EditGAFGraph(const GAFGraphEditRequest& request) override;
		GAFEditorOperationResult UndoGAFAsset(const std::string& sourcePath) override;
		GAFEditorOperationResult RedoGAFAsset(const std::string& sourcePath) override;
		GAFEditorOperationResult RevertGAFAsset(const std::string& sourcePath) override;
		GAFEditorOperationResult SaveGAFAsset(const std::string& sourcePath) override;
		GAFSemanticDiffResult DiffGAFAsset(
			const std::string& sourcePath, const std::string& baselineCanonicalJson) override;
		GAFProjectConfigurationSnapshot GetGAFProjectConfiguration() const override;
		std::vector<std::string> GetGAFTagCatalog() const override;
		GAFProjectConfigurationResult ApplyGAFProjectConfiguration(
			const GAFProjectConfigurationSnapshot& configuration) override;
		GAFRuntimeDebugSnapshot GetGAFRuntimeDebugSnapshot() override;
		GAFDebugCommandResult ControlGAFDebugger(const GAFDebugCommand& command) override;
		GAFTraceCommandResult ControlGAFTrace(const GAFTraceCommand& command) override;
		GAFSimulationResult SimulateGAFAction(const GAFSimulationRequest& request) override;
		std::vector<RecentProjectEntry> GetRecentProjects() const override;
		ProjectOpenResult OpenProject(const ProjectOpenRequest& request) override;
		void CloseProject() override;
		ProjectConfigSnapshot GetProjectConfigSnapshot() const override;
		ProjectConfigEditResult SetProjectDefaultScene(const std::string& sceneRelativePath) override;
		ProjectConfigEditResult SetProjectPathField(ProjectPathField field, const std::string& relativePath) override;
		ProjectConfigEditResult SetProjectScriptSearchPaths(const std::vector<std::string>& paths) override;
		ProjectConfigEditResult SetProjectAssetDirectory(const std::string& key, const std::string& relativePath) override;
		ProjectConfigEditResult SaveProjectConfig() override;
		float GetProjectPhysicsFixedTimeStep() const override;
		ProjectConfigEditResult SetProjectPhysicsFixedTimeStep(float fixedTimeStep) override;
		bool SetCurrentProjectScenePath(const std::string& scenePath) override;
		void ScanProjectAssets() override;

		EditorTextureHandle GetViewportTexture(ViewportId id) const override;
		RenderTexturePreview GetViewportPreview(ViewportId id) const override;
		UpscalerSettingsSnapshot GetUpscalerSettings() const override;
		std::vector<UpscalerCapabilitiesSnapshot> GetUpscalerCapabilities() const override;
		ApplyUpscalerSettingsResult ApplyUpscalerSettings(
			const UpscalerSettingsSnapshot& settings) override;
		CommandRecordingSettingsSnapshot GetCommandRecordingSettings() const override;
		void SetCommandRecordingSettings(const CommandRecordingSettingsSnapshot& settings) override;
		std::vector<RenderTexturePreview> QueryRenderTexturePreviews(RenderTextureFilter filter) const override;
		void RequestPunctualShadowDebugPreview() override;
		PunctualShadowDebugSnapshot GetPunctualShadowDebugSnapshot() const override;
		void ApplyPunctualScreenSpaceShadowSettings(
			const PunctualScreenSpaceShadowSettingsSnapshot& settings) override;
		RenderBackendDiagnostics GetRenderBackendDiagnostics(bool includeRenderGraphSummary = false) const override;
		PipelineRegistryStatsSnapshot GetPipelineRegistryStats() const override;
		RenderDocStatusSnapshot GetRenderDocStatus() const override;
		void SetRenderDocAPIValidationEnabled(bool enabled) override;
		void SetRenderDocReferenceAllResources(bool enabled) override;
		void CaptureNextRenderDocFrame() override;
		void OpenRenderDocUI() override;
		UIDocumentOpenResult OpenUIDocument(const std::string& path) override;
		void CloseUIDocument(UIDocumentId documentId) override;
		void SetUIDocumentVisible(UIDocumentId documentId, bool visible) override;
		UIDocumentSnapshot GetUIDocumentSnapshot(UIDocumentId documentId) const override;
		UIDiagnosticsSnapshot GetUIDiagnostics(UIDocumentId documentId) const override;
		UIPreviewResult RequestUIPreview(const UIPreviewRequest& request) override;
		EditorTextureHandle GetUIPreviewTexture(UIPreviewId id) const override;
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
		MainCameraHiZCullDebugSnapshot GetMainCameraHiZCullDebugSnapshot() const override;
		std::vector<RenderTexturePreview> RequestGIRTPreviews(
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
		RuntimeSceneEntitiesCreateResult CreateRuntimeSceneEntities(const RuntimeSceneEntitiesCreateRequest& request) override;
		ModelAssetPlacementPayload PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request) override;
		RuntimeEntityDestroyResult DestroyRuntimeEntity(const RuntimeEntityDestroyRequest& request) override;
		RuntimeEntityReparentResult ReparentRuntimeEntity(const RuntimeEntityReparentRequest& request) override;
		std::string MakeUniqueRuntimeEntityName(const std::string& baseName) const override;
		std::string GetProjectRootPath() const override;
		bool IsRuntimeSceneReady() const override;
		bool IsRuntimeSceneSwitching() const override;
		RuntimeSceneLoadResult LoadRuntimeScene(const std::string& scenePath, RuntimeSceneLoadMode mode) override;
		void UnloadRuntimeScene() override;
		bool AreRuntimeProjectResourcesLoaded() const override;
		void UnloadRuntimeProjectResources() override;
		bool LoadRuntimeProjectAssetsForScene(const std::string& scenePath) override;
		VehicleDebugSnapshot GetVehicleDebugSnapshot() const override;
		bool HasAnimationDebugNodes() const override;
		AnimationAssetBindingSnapshot GetAnimationAssetBinding(const std::string& entityGuid) const override;
		MotionMatchingDebugSnapshot GetMotionMatchingDebugSnapshot() const override;
		SceneSkeletonHierarchySnapshot GetSceneSkeletonHierarchy(
			const std::string& entityGuidFilter) const override;
		SceneSkeletonNodePoseSnapshot GetSceneSkeletonNodePose(
			const SceneSkeletonNodePoseRequest& request) const override;
		SkeletonDebugSnapshot GetSkeletonDebugSnapshot(const std::string& entityGuidFilter) const override;
		AssetSkeletonSnapshot GetAssetSkeletonSnapshot(const std::string& assetGuid) const override;
		AnimatorDocumentDecodeResult DecodeAnimatorDocument(
			const std::string& canonicalJson) const override;
		AnimatorDocumentEncodeResult EncodeAnimatorDocument(
			const AnimatorDocumentDTO& document) const override;
		BoneMaskDocumentDecodeResult DecodeBoneMaskDocument(
			const std::string& canonicalJson) const override;
		BoneMaskDocumentEncodeResult EncodeBoneMaskDocument(
			const BoneMaskDocumentDTO& document) const override;
		BoneMaskCompileResult CompileBoneMaskDocument(
			const BoneMaskDocumentDTO& document,
			const AssetSkeletonSnapshot& skeleton) const override;
		AnimationPreviewCreateResult CreateAnimationPreview(
			const AnimationPreviewCreateRequest& request) override;
		AnimationPreviewUpdateResult UpdateAnimationPreviewDefinition(
			const AnimationPreviewDefinitionUpdate& update) override;
		bool SetAnimationPreviewPlayback(const AnimationPreviewPlaybackRequest& request) override;
		bool SetAnimationPreviewParameter(const AnimationPreviewParameterValue& value) override;
		bool SwitchAnimationPreviewGraphSet(const AnimationPreviewGraphSetRequest& request) override;
		bool TriggerAnimationPreviewSlot(const AnimationPreviewSlotRequest& request) override;
		bool SetAnimationPreviewViewport(const AnimationPreviewViewportRequest& request) override;
		void TickAnimationPreview(AnimationPreviewSessionId sessionId, float deltaTime) override;
		AnimationPreviewSnapshot GetAnimationPreviewSnapshot(
			AnimationPreviewSessionId sessionId) const override;
		void DestroyAnimationPreview(AnimationPreviewSessionId sessionId) override;
		TimelinePreviewResult StartTimelinePreview(const TimelinePreviewStartRequest& request) override;
		TimelinePreviewResult ConfigureTimelinePreviewPlayback(
			const TimelinePreviewPlaybackRequest& request) override;
		TimelinePreviewResult PlayTimelinePreview(const std::string& previewId) override;
		TimelinePreviewResult PauseTimelinePreview(const std::string& previewId) override;
		TimelinePreviewResult SeekTimelinePreview(
			const std::string& previewId, std::int64_t tick, bool safeEdges) override;
		TimelinePreviewResult StopTimelinePreview(const std::string& previewId) override;
		TimelinePreviewResult GetTimelinePreview(const std::string& previewId) const override;
		TerrainSettingsSnapshot GetTerrainSettings() const override;
		void ApplyTerrainSettings(const TerrainSettingsSnapshot& settings) override;
		bool ApplyRuntimeEntityPreviewChange(const RuntimeEntityPreviewChange& change) override;
		bool ApplyRuntimeMaterialPreviewChange(const RuntimeMaterialPreviewChange& change) override;

		void CommitLightingChanges() override;
		LightingSettingsSnapshot GetLightingSettings() const override;
		void ApplyLightingSettings(const LightingSettingsSnapshot& settings) override;
		PostProcessSettingsSnapshot GetPostProcessSettings() const override;
		void ApplyPostProcessSettings(const PostProcessSettingsSnapshot& settings) override;
		void CommitPostProcessSettings() override;
		EnvironmentSettings GetEnvironmentSettings() const override;
		void ApplyEnvironmentSettings(const EnvironmentSettings& settings) override;
		void CommitEnvironmentSettings() override;
		std::vector<ScenePropertyEdit> ConsumeScenePropertyEdits() override;

		EnginePlayState GetPlayState() const override;
		void SetPlayState(EnginePlayState state) override;
		EntityId RaycastScene(const Ray& ray) const override;
		std::string PickRuntimeEntity(const Ray& ray) const override;
		RuntimeTransformSnapshot GetRuntimeTransform(const std::string& entityGuid) const override;
		void ApplyRuntimeTransform(const RuntimeTransformEdit& edit) override;
		std::vector<RuntimeMultiMeshGroupSnapshot> BuildRuntimeMultiMeshExpansionSnapshot() override;
		AudioBusDebugSnapshot GetAudioBusDebugSnapshot() const override;
		void SetAudioBusGain(const std::string& busName, float gain) override;
		void SetAudioBusMuted(const std::string& busName, bool muted) override;
		void SetAudioBusSoloed(const std::string& busName, bool soloed) override;
		void SetAudioMaxActiveVoices(int maxActiveVoices) override;
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
		void PrepareRuntimeCharacterLocomotion(double deltaSeconds) override;
		void FlushRuntimeCharacterControllerTransforms() override;
		void UpdateRuntimeNonCameraScripts() override;
		void UpdateRuntimeActionsEarly(double deltaSeconds) override;
		void RunRuntimeActionLateContinuation() override;
		void UpdateRuntimeTimelinesPostScript(double deltaSeconds) override;
		void BeginRuntimeCameraControlFrame() override;
		void UpdateRuntimeCameraScripts() override;
		void CaptureRuntimeCameraControlBase() override;
		void UpdateRuntimeTimelinesCamera(double deltaSeconds) override;
		void UpdateTimelinePreviewsPostScript(double deltaSeconds) override;
		void UpdateTimelinePreviewsCamera(double deltaSeconds) override;
		void ResolveRuntimeCameraControlFrame() override;
		void InitializeRuntimeScripts() override;
		void SetupRuntimeScriptProjectVenv(const std::string& projectRootPath) override;
		void ReloadRuntimeScripts() override;
		void ReloadRuntimeScriptModule() override;

		bool CanUndo() const override;
		bool CanRedo() const override;
		void Undo() override;
		void Redo() override;

	private:
		RenderTexturePreview BuildReflectionProbePreview(RenderTextureFilter filter) const;
		RenderTexturePreview BuildWaterTexturePreview(RenderTextureFilter filter) const;
		bool SetRuntimeComponentEnabled(
			const std::string& entityGuid,
			const std::string& componentGuid,
			const std::string& componentType,
			bool enabled);
		bool ReloadSceneAnimationDefinitions(std::string& error);

		RuntimeSceneHandle m_Scene = nullptr;
		RuntimeRenderDeviceHandle m_Device = nullptr;
		VansGraphics::VansRenderSystem* m_RenderSystem = nullptr;
		VansScriptContext* m_ScriptContext = nullptr;
		EnginePlayState m_PlayState = EnginePlayState::Edit;
		std::uint64_t m_SceneContentRevision = 0;
		std::vector<std::unique_ptr<IEngineCommand>> m_UndoStack;
		std::vector<std::unique_ptr<IEngineCommand>> m_RedoStack;
		std::vector<ScenePropertyEdit> m_PendingScenePropertyEdits;
		std::shared_ptr<Vans::VansGameplayTraceRecorder> m_GAFTraceRecorder;
		std::shared_ptr<Vans::VansGameplayReplaySession> m_GAFReplaySession;
		Vans::VansGameplayActionBreakpointSet m_GAFBreakpoints;
		std::optional<Vans::VansGameplayDebugSnapshot> m_GAFPreviousDebugSnapshot;
		std::vector<Vans::VansActionBreakpointHit> m_GAFBreakpointHits;
		std::string m_GAFTracePath;
		std::uint64_t m_GAFDebugFrame = 0;
		bool m_AllowNextCommandMerge = true;
		GIProbeDebugSnapshot m_GIProbeDebugSnapshot;
		UIDocumentId m_NextUIDocumentId = 1;
		UIPreviewId m_NextUIPreviewId = 1;
		std::unordered_map<UIDocumentId, std::shared_ptr<VansRuntime::VansUIDocument>> m_UIDocuments;
		std::unordered_map<UIDocumentId, std::string> m_UIDocumentSourcePaths;
		std::unordered_map<UIDocumentId, VansRuntime::VansUIHandleId> m_UIScreenPreviewHandles;

		struct UIPreviewGpuResource
		{
			UIDocumentId documentId = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			VansGraphics::VansVKImage colorImage;
			VkRenderPass renderPass = VK_NULL_HANDLE;
			VkFramebuffer framebuffer = VK_NULL_HANDLE;
			EditorTextureHandle texture = nullptr;
		};
		struct UIPreviewTransactionState;
		class UIPreviewRenderTransaction;

		std::unordered_map<UIPreviewId, UIPreviewGpuResource> m_UIPreviewResources;

		void DestroyUIPreviewResource(UIPreviewGpuResource& resource);
		void DestroyUIPreviewsForDocument(UIDocumentId documentId);
		void DestroyAllUIPreviewResources();
		void CloseAllUIDocuments();
	};
}
