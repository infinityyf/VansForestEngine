#pragma once

#include "EngineCommandContext.h"
#include "IVansEditorRuntimeCommand.h"
#include "ModelAssetPlacementPreparationService.h"
#include "VansLocalFogFieldPreviewService.h"

#include "../Public/IEngineEditorAPI.h"
#include "../../AuthoringCore/IVansAuthoringSaveHost.h"
#include "../../AuthoringCore/IVansSceneAuthoringHost.h"
#include "../../AuthoringCore/VansAuthoringHistory.h"
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
	class VansEditorPreviewRegistry;
	class VansAnimationPreviewSessionOwner;
	class VansEditorSceneQuery;
}

class VansScriptContext;
namespace VansRuntime
{
	class VansUIDocument;
}

namespace VansGraphics
{
	class VansRenderSystem;
	class VansPcgSplinePreviewSession;
	struct GIProbeLayoutSnapshot;
}

namespace Vans
{
	class VansGameplayTraceRecorder;
	class VansGameplayReplaySession;
	class VansTerrainAuthoringSession;
	class VansPcgMaskAuthoringSession;
	class VansPcgSplineAuthoringSession;
	class VansSceneDocument;
	struct VansPcgBatchUpdate;
	struct VansTerrainAsset;
}

namespace Vans::EditorAPI
{
	class EngineAPIImpl final : public IEngineEditorAPI
	{
	public:
		EngineAPIImpl();
		EngineAPIImpl(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device);
		~EngineAPIImpl() override;

		void BindRuntime(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device);
		void BindGlobalRuntime(RuntimeRenderDeviceHandle device);
		void BindRenderSystem(VansGraphics::VansRenderSystem* renderSystem);
		void BindScriptContext(VansScriptContext* scriptContext);
		void BindAuthoringSaveHost(Vans::IVansAuthoringSaveHost* host);
		void ReleaseRuntimePreviewResources();

		void BreakCommandMergeGroup() override;

		std::vector<AssetEntry> QueryAssets(AssetTypeFilter filter) const override;
		PcgEditorSnapshot GetPcgEditorSnapshot() const override;
		PcgSplineSnapshot GetPcgSplineSnapshot() override;
		PcgEditorOperationResult CreatePcgSplineAsset(const std::string& name) override;
		PcgEditorOperationResult BindPcgSplineAsset(const std::string& guid) override;
		PcgEditorOperationResult SelectPcgSpline(const std::string& spline,const std::string& point,bool toolEnabled) override;
		PcgEditorOperationResult ApplyPcgSplineEdit(const PcgSplineEditRequest& request) override;
		PcgEditorOperationResult ExecutePcgSplineCommand(const PcgSplineCommandRequest& request) override;
		PcgEditorOperationResult AppendPcgSplinePoint(const Ray& ray) override;
		void BindSceneAuthoring(Vans::IVansSceneAuthoringHost* host);
		PcgLayerCreateResult CreatePcgLayer(const PcgLayerCreateRequest& request) override;
		PcgEditorOperationResult RemovePcgLayer(const PcgBrushTarget& target) override;
		PcgEditorOperationResult BindPcgRecipeToScene(const std::string& guid) override;
		ModelLodBuildResult BuildModelLods(const ModelLodBuildRequest& request) override;
        PcgEditorOperationResult BuildPcgPlantLods(const std::string& guid) override;
        PcgPlantConfiguration GetPcgPlantConfiguration(const std::string& guid) override;
		PcgLayerConfiguration GetPcgLayerConfiguration(const PcgBrushTarget& target) override;
		PcgEditorOperationResult ApplyPcgPlantConfiguration(const PcgPlantConfiguration& configuration) override;
		PcgEditorOperationResult ApplyPcgLayerConfiguration(const PcgBrushTarget& target,const PcgLayerConfiguration& configuration) override;
		PcgEditorOperationResult EditPcgConfiguration(const std::string& guid,PcgConfigurationAction action) override;
		PcgMaskPreviewSnapshot GetPcgMaskPreview(const std::string& maskGuid) const override;
		PcgBrushSnapshot GetPcgBrushSnapshot() const override;
		PcgEditorOperationResult SelectPcgBrushTarget(const PcgBrushTarget& target, bool enabled) override;
		PcgEditorOperationResult ConfigurePcgBrush(const PcgBrushSettings& settings) override;
		PcgBrushResult ApplyPcgBrushInput(const PcgBrushInput& input) override;
		PcgEditorOperationResult EditPcgMaskDocument(PcgMaskDocumentAction action) override;
		PcgEditorOperationResult EditPcgMaskData(const PcgMaskDataRequest& request) override;
		PcgEditorOperationResult CreatePcgExclusionMask(const PcgBrushTarget& target) override;
		PcgInstanceSnapshot GetPcgInstances(const PcgBrushTarget& target,uint64_t offset) override;
		PcgEditorOperationResult EditPcgInstance(const PcgInstanceEditRequest& request) override;
		ProjectBrowserRootSnapshot GetProjectBrowserRoot() const override;
		AssetDragPayload CreateAssetDragPayload(const std::string& assetPath) override;
		AssetGuidResolution ResolveAssetGuid(const std::string& assetGuid) const override;
        ParticleAuthoringSchemaSnapshot GetParticleAuthoringSchema() const override;
		ShaderAuthoringSchemaSnapshot GetShaderAuthoringSchema(
			const std::string& shaderAssetGuid) const override;
		LocalFogFieldPreviewSnapshot GetLocalFogFieldPreview(
			const LocalFogFieldPreviewRequest& request) const override;
		ProjectAssetCreateResult CreateProjectAsset(const ProjectAssetCreateRequest& request) override;
        EditorViewportCameraState CaptureEditorViewportCamera() const override;
        void RestoreEditorViewportCamera(const EditorViewportCameraState& state) override;
		Vans::VansSerializedValue QueryPrefabAsset(const std::string& guid) const override;
		AssetRefreshResult RefreshProjectAsset(const std::string& assetPath, bool importIfMissing) override;
		AssetWorkingCopyPublishResult PublishAssetWorkingCopy(
			const AssetWorkingCopyPublishRequest& request) override;
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
		GAFCombatDebugSnapshot GetGAFCombatDebugSnapshot() const override;
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
		ProjectConfigEditResult SaveProjectDocuments() override;
		VansProjectPhysicsTiming GetProjectPhysicsTiming() const override;
		ProjectConfigEditResult SetProjectPhysicsTiming(const VansProjectPhysicsTiming& timing) override;
		bool SetCurrentProjectScenePath(const std::string& scenePath) override;
		RenderTexturePreview GetViewportPreview(ViewportId id) const override;
		UpscalerSettingsSnapshot GetUpscalerSettings() const override;
		std::vector<UpscalerCapabilitiesSnapshot> GetUpscalerCapabilities() const override;
		ApplyUpscalerSettingsResult ApplyUpscalerSettings(
			const UpscalerSettingsSnapshot& settings) override;
		CommandRecordingSettingsSnapshot GetCommandRecordingSettings() const override;
		void SetCommandRecordingSettings(const CommandRecordingSettingsSnapshot& settings) override;
		std::vector<RenderTexturePreview> QueryRenderTexturePreviews(RenderTextureFilter filter) const override;
		std::uint32_t GetAmbientSkyCacheDebugMode() const override;
		void SetAmbientSkyCacheDebugMode(std::uint32_t mode) override;
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
		void BakeQueuedReflectionProbesNow() override;
		ReflectionProbeSettingsSnapshot GetReflectionProbeSettings() const override;
		bool ApplyReflectionProbeSettings(const ReflectionProbeSettingsSnapshot& settings) override;
		GIInspectorSettingsSnapshot GetGISettings() const override;
		bool ApplyGISettings(const GIInspectorSettingsSnapshot& settings) override;
		void SaveGIConfiguration() override;
		void SetGIProbeVisualization(bool showPositions, bool showVolume, std::uint32_t stride) override;
		std::shared_ptr<const GIProbeDebugSnapshot> GetGIProbeDebugSnapshot() const override;
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
		RuntimeSceneEntitiesCreateResult CreateRuntimeSceneEntities(const RuntimeSceneEntitiesCreateRequest& request) override;
		ModelAssetPlacementPayload PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request) override;
		RuntimeEntityDestroyResult DestroyRuntimeEntity(const RuntimeEntityDestroyRequest& request) override;
		RuntimeEntityReparentResult ReparentRuntimeEntity(const RuntimeEntityReparentRequest& request) override;
		std::string GetProjectRootPath() const override;
		bool IsRuntimeSceneReady() const override;
		bool IsRuntimeSceneSwitching() const override;
		RuntimeSceneLoadResult LoadRuntimeScene(const RuntimeSceneLoadRequest& request) override;
		void UnloadRuntimeScene() override;
		void UnloadRuntimeProjectResources() override;
		VehicleDebugSnapshot GetVehicleDebugSnapshot() const override;
		AnimationAssetBindingSnapshot GetAnimationAssetBinding(const std::string& entityGuid) const override;
		MotionMatchingDebugSnapshot GetMotionMatchingDebugSnapshot() const override;
		VansAIDiagnosticsSnapshot GetAIDiagnosticsSnapshot() const override;
		SceneSkeletonHierarchySnapshot GetSceneSkeletonHierarchy(
			const std::string& entityGuidFilter) const override;
		SceneSkeletonNodePoseSnapshot GetSceneSkeletonNodePose(
			const SceneSkeletonNodePoseRequest& request) const override;
		SkeletonDebugSnapshot GetSkeletonDebugSnapshot(const std::string& entityGuidFilter) const override;
		ParticleDebugSnapshot GetParticleDebugSnapshot() const override;
		AssetSkeletonSnapshot GetAssetSkeletonSnapshot(const std::string& assetGuid) const override;
		AnimatorDocumentDecodeResult DecodeAnimatorDocument(
			const std::string& canonicalJson) const override;
		AnimatorDocumentEncodeResult EncodeAnimatorDocument(
			const AnimatorDocumentDTO& document) const override;
		BoneMaskDocumentDecodeResult DecodeBoneMaskDocument(
			const std::string& canonicalJson) const override;
		BoneMaskDocumentEncodeResult EncodeBoneMaskDocument(
			const VansBoneMaskDocumentDTO& document) const override;
		AnimationRigDocumentDecodeResult DecodeAnimationRigDocument(
			const std::string& canonicalJson) const override;
		AnimationRigDocumentEncodeResult EncodeAnimationRigDocument(
			const AnimationRigDocumentDTO& document) const override;
		BoneMaskCompileResult CompileBoneMaskDocument(
			const VansBoneMaskDocumentDTO& document,
			const AssetSkeletonSnapshot& skeleton) const override;
		AnimationPreviewCreateResult CreateAnimationPreview(
			const AnimationPreviewCreateRequest& request) override;
		AnimationPreviewUpdateResult UpdateAnimationPreviewDefinition(
			const AnimationPreviewDefinitionUpdate& update) override;
		bool SetAnimationPreviewPlayback(const AnimationPreviewPlaybackRequest& request) override;
		bool SetAnimationPreviewParameter(const AnimationPreviewParameterValue& value) override;
		bool SwitchAnimationPreviewGraphSet(const AnimationPreviewGraphSetRequest& request) override;
		bool TriggerAnimationPreviewSlot(const AnimationPreviewSlotRequest& request) override;
		void TickAnimationPreview(AnimationPreviewSessionId sessionId, float deltaTime) override;
		AnimationPreviewSnapshot GetAnimationPreviewSnapshot(
			AnimationPreviewSessionId sessionId) const override;
		AnimationPreviewRigSnapshot GetAnimationPreviewRigSnapshot(
			AnimationPreviewSessionId sessionId) const override;
		std::vector<AnimationPreviewSceneEntitySnapshot>
			QueryAnimationPreviewSceneEntities(AnimationPreviewSessionId sessionId) const override;
		AnimationRigDocumentDecodeResult GetAnimationPreviewWorkingRigDocument(
			AnimationPreviewSessionId sessionId) const override;
		AnimationPreviewRigEditResult SetAnimationPreviewRigDefinition(const AnimationPreviewRigDefinitionRequest& request) override;
		AnimationPreviewRigEditResult SetAnimationPreviewRigSocketTransform(
			const AnimationPreviewRigSocketTransformRequest& request) override;
		AnimationPreviewRigEditResult SetAnimationPreviewRigAttachmentProfile(
			const AnimationPreviewRigAttachmentProfileRequest& request) override;
		AnimationPreviewRigEditResult SetAnimationPreviewTargetBindings(const AnimationPreviewTargetBindingsRequest& request) override;
		bool AdoptAnimationPreviewSceneChanges(const AnimationPreviewSceneAdoptRequest& request) override;
		AnimationPreviewAttachmentEditResult SetAnimationPreviewAttachmentTransform(
			const AnimationPreviewAttachmentTransformRequest& request) override;
		AnimationPreviewAttachmentEditResult SetAnimationPreviewAttachmentBinding(
			const AnimationPreviewAttachmentBindingRequest& request) override;
		AnimationPreviewRigEditResult AdoptAnimationPreviewRig(
			const AnimationPreviewRigAdoptRequest& request) override;
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
		TerrainEditorOperationResult ApplyTerrainSettings(
			const TerrainSettingsSnapshot& settings) override;
		TerrainEditorSnapshot GetTerrainEditorSnapshot() const override;
		TerrainEditorOperationResult ConfigureTerrainBrush(
			const TerrainBrushConfiguration& configuration) override;
		TerrainBrushInputResult ApplyTerrainBrushInput(
			const TerrainBrushInput& input) override;
		TerrainEditorOperationResult UndoTerrainEdit() override;
		TerrainEditorOperationResult RedoTerrainEdit() override;
		TerrainEditorOperationResult RevertTerrainEdits() override;
		TerrainEditorOperationResult SaveTerrainAsset() override;
		bool ApplyRuntimeEntityPreviewChange(const RuntimeEntityPreviewChange& change) override;
		bool ApplyRuntimeMaterialPreviewChange(const RuntimeMaterialPreviewChange& change) override;

		LightingSettingsSnapshot GetLightingSettings() const override;
		void ApplyLightingSettings(const LightingSettingsSnapshot& settings) override;
		PostProcessSettingsSnapshot GetPostProcessSettings() const override;
		void ApplyPostProcessSettings(const PostProcessSettingsSnapshot& settings) override;
		void CommitPostProcessSettings() override;
		EnvironmentSettings GetEnvironmentSettings() const override;
		void ApplyEnvironmentSettings(const EnvironmentSettings& settings) override;
		void CommitEnvironmentSettings() override;

        void UpdateGameCursorViewport(bool interactive) override;
        bool IsGameCursorHidden() const override;
		EnginePlayState GetPlayState() const override;
		void SetPlayState(EnginePlayState state) override;
		EditorScenePickResult PickEditorScene(const EditorScenePickRequest& request) override;
		EditorSceneBounds QueryEditorSceneBounds(const std::vector<std::string>& entityGuids) override;
		RuntimeTransformSnapshot GetRuntimeTransform(
			const std::string& entityGuid, RuntimeTransformSpace space) const override;
		std::vector<RuntimeMultiMeshGroupSnapshot> BuildRuntimeMultiMeshExpansionSnapshot() override;
		AudioBusDebugSnapshot GetAudioBusDebugSnapshot() const override;
		void SetAudioBusGain(const std::string& busName, float gain) override;
		void SetAudioBusMuted(const std::string& busName, bool muted) override;
		void SetAudioBusSoloed(const std::string& busName, bool soloed) override;
		void SetAudioMaxActiveVoices(int maxActiveVoices) override;
		void SetAudioSourceLimit(int sourceLimit) override;
		std::vector<std::string> GetRuntimeCollisionLayerNames() const override;
		void InstallRuntimeVehiclePhysicsStepCallback() override;
		void ClearRuntimePhysicsStepCallback() override;
		bool IsRuntimePhysicsRunning() const override;
		void StartRuntimePhysicsIfNeeded() override;
		void PauseRuntimePhysics() override;
		void ResumeRuntimePhysics() override;
		void SyncRuntimePhysicsTransforms() override;
		void PrepareRuntimeCharacterLocomotion(double deltaSeconds) override;
		void FlushRuntimeCharacterControllerTransforms() override;
		void UpdateRuntimeNonCameraScripts() override;
		void AdvanceCameraRuntime(double deltaSeconds) override;
		void UpdateRuntimeActionsEarly(double deltaSeconds) override;
		void UpdateRuntimeAI(double deltaSeconds) override;
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

		EditorCommandHistorySnapshot GetRuntimeCommandHistory() const override;
		void Undo() override;
		void Redo() override;

	private:
		friend class ModelAssetPlacementPreparationService;
		ProjectMeshLoadResult EnsureProjectMeshLoaded(
			const ProjectMeshLoadRequest& request);
		ProjectMeshSnapshot GetProjectMeshInfo(const std::string& meshName) const;
		std::string GetDefaultMaterialAssetName() const;
		Vans::VansAuthoringSaveResult SaveAuthoringDocument(
			const std::shared_ptr<Vans::VansOpenAssetDocument>& document) const;
		bool CommitScenePropertyValue(
			std::string propertyPointer,
			Vans::VansSerializedValue value) const;
		void SubmitCommand(std::unique_ptr<IVansEditorRuntimeCommand> command);
		void StepRuntimeVehicle(float deltaTimeSeconds);
		std::shared_ptr<VansEditorSceneQuery> m_EditorSceneQueryCache;
		std::unique_ptr<VansEditorPreviewRegistry> m_EditorPreviewRegistry;
		std::unique_ptr<VansAnimationPreviewSessionOwner> m_AnimationPreviewSessionOwner;
		RenderTexturePreview BuildReflectionProbePreview(RenderTextureFilter filter) const;
		RenderTexturePreview BuildWaterTexturePreview(RenderTextureFilter filter) const;
		bool SetRuntimeComponentEnabled(
			const std::string& entityGuid,
			const std::string& componentGuid,
			const std::string& componentType,
			bool enabled);
		bool ReloadSceneAnimationDefinitions(std::string& error);
		std::shared_ptr<Vans::VansTerrainAuthoringSession> EnsureTerrainAuthoringSession(
			std::string& error) const;
		void QueueTerrainPixelChange();
		void ApplyTerrainRuntimeSettings(const TerrainSettingsSnapshot& settings);
		struct PcgAuthoringState;
		std::unique_ptr<Vans::VansPcgSplineAuthoringSession> m_PcgSplineAuthoring;
		std::unique_ptr<VansGraphics::VansPcgSplinePreviewSession> m_PcgSplinePreview;
		void TickPcgSplineAuthoring();
		std::shared_ptr<const Vans::VansTerrainAsset> ResolvePcgSplineTerrainOverride() const;
		PcgEditorOperationResult RequestPcgSplinePreview();
		PcgEditorOperationResult FinishPcgSplineEdit();
		void EnsurePcgAuthoringContext() const;
		PcgEditorOperationResult FinishPcgStroke(bool cancel);
		PcgEditorOperationResult RefreshPcgMaskVegetation(bool force);
		PcgEditorOperationResult RefreshPcgRecipePreview();
		mutable std::shared_ptr<PcgAuthoringState> m_PcgAuthoring;
		std::string m_PcgSceneRecipeGuid;
		PcgEditorOperationResult CommitPcgPreview(std::shared_ptr<const VansPcgBatchUpdate> update);
		Vans::IVansSceneAuthoringHost* m_SceneAuthoringHost = nullptr;
		VansSceneDocument* m_PcgSceneDocument = nullptr;
		std::uint64_t m_PcgSceneAuthoringState = 0;

		RuntimeSceneHandle m_Scene = nullptr;
		RuntimeRenderDeviceHandle m_Device = nullptr;
		VansGraphics::VansRenderSystem* m_RenderSystem = nullptr;
		VansScriptContext* m_ScriptContext = nullptr;
		Vans::IVansAuthoringSaveHost* m_AuthoringSaveHost = nullptr;
		EnginePlayState m_PlayState = EnginePlayState::Edit;
		std::uint64_t m_SceneContentRevision = 0;
		struct RuntimeHistoryEntry
		{
			std::unique_ptr<IVansEditorRuntimeCommand> command;
			Vans::VansHistorySequence sequence = 0;
		};
		void DiscardStaleRuntimeRedo() const;
		std::vector<RuntimeHistoryEntry> m_UndoStack;
		mutable std::vector<RuntimeHistoryEntry> m_RedoStack;
		mutable Vans::VansHistorySequence m_RedoRevision = 0;
		std::shared_ptr<Vans::VansGameplayTraceRecorder> m_GAFTraceRecorder;
		std::shared_ptr<Vans::VansGameplayReplaySession> m_GAFReplaySession;
		Vans::VansGameplayActionBreakpointSet m_GAFBreakpoints;
		std::optional<Vans::VansGameplayDebugSnapshot> m_GAFPreviousDebugSnapshot;
		std::vector<Vans::VansActionBreakpointHit> m_GAFBreakpointHits;
		std::string m_GAFTracePath;
		std::uint64_t m_GAFDebugFrame = 0;
		bool m_AllowNextCommandMerge = true;
		mutable std::shared_ptr<const VansGraphics::GIProbeLayoutSnapshot> m_GIProbeDebugLayout;
		mutable std::shared_ptr<const GIProbeDebugSnapshot> m_GIProbeDebugSnapshot;
		UIDocumentId m_NextUIDocumentId = 1;
		UIPreviewId m_NextUIPreviewId = 1;
		std::unordered_map<UIDocumentId, std::shared_ptr<VansRuntime::VansUIDocument>> m_UIDocuments;
		std::unordered_map<UIDocumentId, std::string> m_UIDocumentSourcePaths;
		std::unordered_map<UIDocumentId, VansRuntime::VansUIHandleId> m_UIScreenPreviewHandles;
		mutable VansLocalFogFieldPreviewService m_LocalFogFieldPreviewService;
		mutable std::shared_ptr<Vans::VansTerrainAuthoringSession> m_TerrainAuthoringSession;
		TerrainBrushConfiguration m_TerrainBrushConfiguration;
		bool m_TerrainStrokeActive = false;
		TerrainBrushTool m_ActiveTerrainStrokeTool = TerrainBrushTool::Raise;
		std::uint32_t m_TerrainStrokeSeed = 1;
		float m_TerrainStrokeLastPixelX = 0.0f;
		float m_TerrainStrokeLastPixelY = 0.0f;

		struct UIPreviewGpuResource
		{
			UIDocumentId documentId = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			VansGraphics::VansVKImage colorImage;
			VkRenderPass renderPass = VK_NULL_HANDLE;
			VkFramebuffer framebuffer = VK_NULL_HANDLE;
			EditorTextureHandle texture = nullptr;
			std::uint64_t textureRegistration = 0;
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
