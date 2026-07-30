#pragma once

#include "VansRenderNode.h"

#include "VansShaderManager.h"

#include "VansSceneAssetRegistry.h"

#include "VansMainCameraVisibility.h"

#include "WaterCore/VansWaterConfig.h"





namespace VansGraphics { class VansWaterMaterial; }



namespace VansGraphics { class VansWaterSystem; }

namespace VansGraphics { class VansMesh; }

namespace VansGraphics { class VansTexture; }

namespace Vans { struct VansSceneAudioResourceRequest; struct VansSceneVideoResourceRequest; }
namespace Vans { struct VansSceneObjectBuildPlan; }

namespace VansGraphics { class VansCamera; }

namespace VansGraphics { class VansAnimationNode; }

namespace VansGraphics { class VansAnimationController; }

namespace VansGraphics { class VansVegetationSystem; }

namespace VansGraphics { class VansParticleRenderNode; }
namespace VansGraphics { class VansVKCommandBuffer; }

class VansScriptObject;

#include "BRDFData/VansLight.h"

#include "BRDFData/VansIESProfile.h"

#include "../PhysicsCore/VansPhysicsVehicle.h"

#include "VulkanCore/VansDescriptorSetLayouts.h"

#include "VansVideoManager.h"

#include "../AudioCore/VansAudioManager.h"

#include "ReflectionProbeCore/VansReflectionProbeSystem.h"

#include "VansTransformSlotAllocator.h"

#include <vector>

#include <map>

#include <set>

#include <unordered_map>



namespace VansEngine

{

	class VansTerrainPhysicsNode;

}



namespace Vans

{

	class VansAssetDatabase;

}

namespace VansEngine
{
	class VansPhysicsNode;
	class VansClothNode;
	class VansCharacterControllerNode;
}



namespace VansGraphics

{

	enum class VansSceneState

	{

        Empty,       // 鏃犲満鏅?

        Unloading,   // 姝ｅ湪鍗歌浇鏃у満鏅?

		Loading,

		Ready

	};









	enum class VansSceneLoadMode

	{

		Editor,

        Runtime   // 杩愯鏃舵ā寮?

	};



	// Scene-owned settings for the ray-traced diffuse GI probe volume.

	struct VansGISettings

	{

		glm::uvec3 gridDimensions = glm::uvec3(80u);

		glm::vec3 probeSpacingAxes = glm::vec3(0.5f);

		glm::vec3 regionCenter = glm::vec3(0.0f, 6.0f, 0.0f);

		uint32_t raysPerProbe = 256;

		uint32_t spatialUpdateDivisor = 2;

		uint32_t directionUpdateSlices = 16;

		float maxRayDistance = 100.0f;

		float normalBias = 0.25f;

		float environmentIntensity = 5.0f;

		float maxIndirectRadiance = 2.0f;

		float maxSHL0 = 8.0f;

		float volumeFadeDistance = 1.0f;

		bool showProbeGizmos = false;

		bool showProbeVolume = false;

		uint32_t debugView = 0;

		float debugExposure = 1.0f;

		uint32_t gizmoStride = 8;

	};



	// A logical group representing a multi-mesh parent and all its auto-expanded child render nodes.

	struct MultiMeshGroup

	{

		std::string parentName;

		std::string parentEntityGuid;

		VansMesh* sourceMesh = nullptr;              // non-owning project asset reference

		glm::vec3 position = glm::vec3(0);

		glm::vec3 rotation = glm::vec3(0);

		glm::vec3 scale    = glm::vec3(1);

		uint32_t sharedTransformID = 0;            // transform owned by the first child

		std::vector<VansRenderNode*> childNodes;   // opaque + transparent children

	};

	//struct TLASInstanceData

	//{

	//	glm::mat4x4 m_MVMatrix;

	//	int m_BufferIndex;

	//};



	class VansScene

	{

	public:

		~VansScene();



		void CreateNodeDescriptorSets();

	private:



		VansCamera* m_Camera;



	VansLightManager m_LightManager;

	struct PunctualShadowCasterRecord
	{
		VansRenderBounds bounds;
		uint32_t shadowCasterMask = 0xffffffffu;
		bool dynamic = false;
		bool hasBounds = false;
	};
	std::unordered_map<uint64_t, PunctualShadowCasterRecord> m_PunctualShadowCasters;
	void UpdatePunctualShadowCasterCache();
	void BuildPunctualShadowCasterLists();



		VansMaterialManager m_MaterialManager;



		VansReflectionProbeSystem m_ReflectionProbeSystem;





		VansIESProfileManager m_IESProfileManager;



		VansAsset* GetMeshAsset(const std::string& name);



		VansAsset* GetShaderAsset(const std::string& name);



		VansAsset* GetMaterialAsset(const std::string& name);



		VansTexture* ResolveTextureOrDefault(VansTexture* texture, const char* fallbackName);



		void RegisterMeshAsset(VansAsset* asset);

		void RegisterSceneSubMeshAsset(VansAsset* asset);

		void RegisterShaderAsset(VansAsset* asset);

		void RegisterTextureAsset(VansAsset* asset);

		void RegisterMaterialAsset(VansAsset* asset);

		void RebuildAssetLookup();

		void ClearSceneAssetLookup();



	public:

		VansAsset* GetTextureAsset(const std::string& name);



		void AddMeshAsset(VansAsset* asset);

		void AddSceneSubMeshAsset(VansAsset* asset);

		void AddShaderAsset(VansAsset* asset);

		void AddTextureAsset(VansAsset* asset);

		void AddMaterialAsset(VansAsset* asset);

		VansVKDevice* GetRuntimeResourceDevice() const { return m_RuntimeResourceDevice; }

		void LoadProjectAudioResources(const std::vector<Vans::VansSceneAudioResourceRequest>& audios, const std::string& assetPrefix);

		void LoadProjectVideoResources(const std::vector<Vans::VansSceneVideoResourceRequest>& videos, const std::string& assetPrefix);

		void SyncShaderAssetsFromShaderManager();

		void FinalizeProjectResourceBatch();

		bool HasProjectMeshAlias(const std::string& name) const;

		void SetProjectMeshAlias(const std::string& name, VansAsset* asset);

		VansAsset* FindMeshAsset(const std::string& name);

		VansAsset* FindShaderAsset(const std::string& name);

		VansAsset* FindMaterialAsset(const std::string& name) { return GetMaterialAsset(name); }

		VansTexture* FindOrLoadTexture(const std::string& absPath, bool isSRGB);

		VansTexture* ResolveTextureAssetOrDefault(VansTexture* texture, const char* fallbackName) { return ResolveTextureOrDefault(texture, fallbackName); }

		const std::vector<VansAsset*>& GetMeshAssets() const { return m_AssetRegistry.GetMeshes(); }

		const std::vector<VansAsset*>& GetSceneSubMeshAssets() const { return m_AssetRegistry.GetSceneSubMeshes(); }

		const std::vector<VansAsset*>& GetShaderAssets() const { return m_AssetRegistry.GetShaders(); }

		const std::vector<VansAsset*>& GetTextureAssets() const { return m_AssetRegistry.GetTextures(); }

		const std::vector<VansAsset*>& GetMaterialAssets() const { return m_AssetRegistry.GetMaterials(); }



	private:



		VansSceneAssetRegistry m_AssetRegistry;

		VansRenderNode* m_SkyBoxNode = nullptr;
		VansRenderNode* m_DeferredNode = nullptr;
		std::vector<VansRenderNode*> m_OpaqueRenderNodes;
		std::vector<VansRenderNode*> m_HairRenderNodes;
		VansRenderNode* m_TerrainRenderNode = nullptr;
		VansRenderNode* m_VegetationRenderNode = nullptr;
		VansVegetationSystem* m_VegetationSystem = nullptr;
		VansRenderNode* m_WaterRenderNode = nullptr;
		VansWaterMaterial* m_WaterMaterial = nullptr;
		bool m_HasWater = false;
		VansWaterSystem* m_WaterSystem = nullptr;
		std::vector<VansRenderNode*> m_TransParentRenderNodes;
		std::vector<VansRenderNode*> m_ForwardOpaqueAfterDeferredRenderNodes;
		std::vector<VansRenderNode*> m_PostProcessRenderNodes;
		std::vector<VansRenderNode*> m_ScreenSpaceRenderNodes;
		std::vector<VansRenderNode*> m_DecalRenderNodes;
		std::vector<VansParticleRenderNode*> m_ParticleRenderNodes;
		std::unordered_map<std::string, MultiMeshGroup> m_MultiMeshGroups;
		std::vector<VansAnimationNode*> m_AnimationNodes;
		std::vector<VansAnimationController*> m_AnimationControllers;
		VansMainCameraHiZCullSettings m_MainCameraHiZCullSettings;
		VansMainCameraHiZHistoryState m_MainCameraHiZHistory;
		VansMainCameraVisibilityStats m_MainCameraVisibilityStats;
		std::vector<VansMainCameraCullCandidate> m_MainCameraCullCandidates;
		std::vector<VansMainCameraHiZCulledNodeDebug> m_MainCameraHiZCulledDebugNodes;
		std::vector<VansMainCameraCullObjectGPU> m_MainCameraCullObjectsGPU;
		std::vector<uint64_t> m_MainCameraLastDispatchedNodeIds;
		std::unordered_map<uint64_t, uint32_t> m_MainCameraCullIndexByNodeId;
		std::unordered_map<uint64_t, bool> m_MainCameraDrawVisibilityByNodeId;
		std::unordered_map<uint64_t, VansRenderBounds> m_MainCameraPreviousCullBounds;
		std::unordered_map<uint64_t, uint32_t> m_MainCameraForceVisibleFramesByNodeId;
		VansVKBuffer m_MainCameraCullObjectBuffer;
		VansVKBuffer m_MainCameraVisibilityBuffer;
		uint32_t m_MainCameraCullBufferCapacity = 0;
		uint32_t m_MainCameraFrameIndex = 0;
		bool m_MainCameraHasPendingVisibilityReadback = false;
		VansVKBuffer m_DummyBoneIDBuffer;
		VansVKBuffer m_DummyBoneBuffer;
		VansVKBuffer m_DummyWeightBuffer;
		std::vector<VansEngine::VansPhysicsNode*> m_PhysicsNodes;
		VansEngine::VansTerrainPhysicsNode* m_TerrainPhysicsNode = nullptr;
		std::vector<VansEngine::VansClothNode*> m_ClothNodes;
		std::vector<VansEngine::VansCharacterControllerNode*> m_CharControllerNodes;
		std::vector<VansVKBuffer> m_ClothStagingBuffers;
		VansEngine::VansPhysicsVehicle* m_Vehicle = nullptr;
		std::vector<VansScriptObject*> m_SceneObjects;


	public:

		bool HasWaterNodes() const  { return m_HasWater && m_WaterSystem != nullptr; }





		VansWaterSystem* GetWaterSystem() const { return m_WaterSystem; }

		void SetWaterRuntimeConfig(const VansWaterConfig& config, VansWaterMaterial* material);

		void SetWaterSystem(VansWaterSystem* waterSystem) { m_WaterSystem = waterSystem; }

		VansRenderNode* GetTerrainRenderNode() const { return m_TerrainRenderNode; }

		VansRenderNode* GetWaterRenderNode() const { return m_WaterRenderNode; }

		VansWaterMaterial* GetWaterMaterial() const { return m_WaterMaterial; }

		void SetTerrainPhysicsNode(VansEngine::VansTerrainPhysicsNode* terrainPhysicsNode);

		void SetVegetationSystem(VansVegetationSystem* vegetationSystem) { m_VegetationSystem = vegetationSystem; }

		VansVegetationSystem* GetVegetationSystem() const { return m_VegetationSystem; }

		const std::vector<VansRenderNode*>& GetOpaqueRenderNodes() const { return m_OpaqueRenderNodes; }

		const std::vector<VansRenderNode*>& GetHairRenderNodes() const { return m_HairRenderNodes; }

		const std::vector<VansRenderNode*>& GetTransparentRenderNodes() const { return m_TransParentRenderNodes; }

		const std::vector<VansRenderNode*>& GetForwardOpaqueAfterDeferredRenderNodes() const { return m_ForwardOpaqueAfterDeferredRenderNodes; }

		const std::vector<VansRenderNode*>& GetPostProcessRenderNodes() const { return m_PostProcessRenderNodes; }

		const std::vector<VansRenderNode*>& GetScreenSpaceRenderNodes() const { return m_ScreenSpaceRenderNodes; }

		const std::vector<VansRenderNode*>& GetDecalRenderNodes() const { return m_DecalRenderNodes; }

		const std::vector<VansParticleRenderNode*>& GetParticleRenderNodes() const { return m_ParticleRenderNodes; }

		const std::vector<VansAnimationNode*>& GetAnimationNodes() const { return m_AnimationNodes; }

		const std::vector<VansScriptObject*>& GetSceneObjects() const { return m_SceneObjects; }

		VansScriptObject* FindSceneObjectByName(const std::string& name) const { return FindObjectByName(name); }

		bool HasMultiMeshGroup(const std::string& name) const { return m_MultiMeshGroups.find(name) != m_MultiMeshGroups.end(); }

		MultiMeshGroup& GetOrCreateMultiMeshGroup(const std::string& name) { return m_MultiMeshGroups[name]; }

		MultiMeshGroup* FindAnimationMultiMeshGroup(const std::string& meshGroupName, const std::string& objectName);

		uint32_t GetParentTransformID(uint32_t childTransformID) const { return m_TransformParentSystem.GetParent(childTransformID); }

		void SetTransformParentID(uint32_t childTransformID, uint32_t parentTransformID) { m_TransformParentSystem.SetParent(childTransformID, parentTransformID); }

		void ClearTransformParentID(uint32_t childTransformID) { m_TransformParentSystem.ClearParent(childTransformID); }



		// Initialize the vehicle in the scene from JSON-specified parameters.

		// Object transform bindings are preferred; render node names are kept as a legacy fallback.

		void InitVehicle(VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,

			const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames,

			uint32_t bodyTransformID = UINT32_MAX,

			const std::vector<uint32_t>& tireTransformIDs = std::vector<uint32_t>(),

			const VansEngine::VansVehicleTuning& tuning = VansEngine::VansVehicleTuning(),

			const std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>& wheelVisualBindings =

				std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>());





		// Find an object by its logical name.

		VansScriptObject* FindObjectByName(const std::string& name) const;



	public:

		VkDescriptorSetLayout GetGlobalTransformDataSetLayout() const { return m_GlobalTransformDataSetLayout; }
		const std::vector<VkDescriptorSet>& GetGlobalTransformDataDescriptorSets() const { return m_GlobalTransformDataDescriptorSets; }
		VansVKBuffer& GetInstanceTransformDataBuffer() { return m_InstanceTransformDataBuffer; }
		const VansVKBuffer& GetInstanceTransformDataBuffer() const { return m_InstanceTransformDataBuffer; }
		uint32_t AllocateTransformSlot();
		bool CreateInstanceTransformBuffer(
			VkDevice& device,
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkMemoryPropertyFlags memoryProperties);
		bool SetInstanceTransformData(const ModelDataStruct& data, uint32_t slot);
		void UpdateMappedInstanceTransformData(const ModelDataStruct& data, uint32_t slot);
		bool PersistentlyMapInstanceTransformBuffer();
		void CreateGlobalTransformDescriptorSet(VkDescriptorSetLayoutBinding binding);

	private:

		VansVKBuffer m_InstanceTransformDataBuffer;

		std::vector<ModelDataStruct> m_InstanceTransformData;

		VkDescriptorSetLayout m_GlobalTransformDataSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_GlobalTransformDataDescriptorSets;


	public:


		// ===== New: Reorganized descriptor set system =====

		// Global descriptor set (Set 0): Camera + Lights + Materials + IBL + Bindless
	private:

		VkDescriptorSetLayout m_GlobalDescriptorSetLayout = VK_NULL_HANDLE;

		VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

	public:

		VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const { return m_GlobalDescriptorSetLayout; }

		VkDescriptorSet GetGlobalDescriptorSet() const { return m_GlobalDescriptorSet; }



		// Object descriptor set (Set 2): Transform SSBO 閳?shared by all geometry nodes
	private:

		VkDescriptorSetLayout m_ObjectDescriptorSetLayout = VK_NULL_HANDLE;

		VkDescriptorSet m_ObjectDescriptorSet = VK_NULL_HANDLE;

	public:

		VkDescriptorSetLayout GetObjectDescriptorSetLayout() const { return m_ObjectDescriptorSetLayout; }

		VkDescriptorSet GetObjectDescriptorSet() const { return m_ObjectDescriptorSet; }



		// Animation descriptor set (Set 3): Bone Matrices SSBO + Bone Weight SSBO

		// Per-node for animated VansCommonRenderNodes; static nodes bind this shared dummy set.
	private:

		VkDescriptorSetLayout m_AnimationDescriptorSetLayout = VK_NULL_HANDLE;

		VkDescriptorSet m_AnimationDescriptorSet = VK_NULL_HANDLE;  // shared dummy for static nodes

	public:

		VkDescriptorSetLayout GetAnimationDescriptorSetLayout() const { return m_AnimationDescriptorSetLayout; }

		VkDescriptorSet GetAnimationDescriptorSet() const { return m_AnimationDescriptorSet; }



		// Empty pass layout (Set 1) for passes that have no per-pass resources
	private:

		VkDescriptorSetLayout m_EmptyPassLayout = VK_NULL_HANDLE;

		VkDescriptorSet m_EmptyPassDescriptorSet = VK_NULL_HANDLE;

	public:

		VkDescriptorSetLayout GetEmptyPassLayout() const { return m_EmptyPassLayout; }

		VkDescriptorSet GetEmptyPassDescriptorSet() const { return m_EmptyPassDescriptorSet; }



		// Creates the global Set 0 descriptor set and writes all global resources into it

		void CreateGlobalDescriptorSet(VkDevice device);

		void UpdateGlobalDescriptorSet();		// Writes only TileLight bindings (9, 10) into the global descriptor set.

		// Called after PrepareTileLightData() creates the TileLight SSBO buffers.

		void UpdateGlobalTileLightDescriptors();

		void PrepareReflectionProbeRuntime(VansVKDevice& device);

		void BindWaterSystemGlobalDescriptors();

		void BindMaterialVideoDescriptorSet();

		void CreateSceneNodeDescriptorSets() { CreateNodeDescriptorSets(); }

		void DeferInitialReflectionProbeBake();

		void PlayAllSceneVideos();


	private:

		VansTransformParentSystem m_TransformParentSystem;



	public:







		/// Are project resources loaded? (mesh/texture/shader)

		bool AreResourcesLoaded() const { return m_ResourcesLoaded; }

		VansEngine::VansPhysicsVehicle* GetVehicle() const { return m_Vehicle; }

		void RegisterPhysicsNode(VansEngine::VansPhysicsNode* physicsNode);

		void RegisterClothNode(VansEngine::VansClothNode* clothNode, VansRenderNode* renderNodeForStaging);

		void RegisterCharacterControllerNode(VansEngine::VansCharacterControllerNode* controllerNode);

		const std::vector<VansEngine::VansPhysicsNode*>& GetPhysicsNodes() const { return m_PhysicsNodes; }

		void RegisterAnimationRuntime(VansAnimationNode* animNode, VansAnimationController* controller)

		{

			if (animNode)

				m_AnimationNodes.push_back(animNode);

			if (controller)

				m_AnimationControllers.push_back(controller);

		}

		VansEngine::VansPhysicsVehicle* BuildVehicleRuntime(

			VansEngine::VansPhysicsSystem* physicsSystem,

			const glm::vec3& position,

			const std::string& bodyRenderNodeName,

			const std::vector<std::string>& tireRenderNodeNames,

			uint32_t bodyTransformID = UINT32_MAX,

			const std::vector<uint32_t>& tireTransformIDs = std::vector<uint32_t>(),

			const VansEngine::VansVehicleTuning& tuning = VansEngine::VansVehicleTuning(),

			const std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>& wheelVisualBindings =

				std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>())

		{

			InitVehicle(physicsSystem, position, bodyRenderNodeName, tireRenderNodeNames,

				bodyTransformID, tireTransformIDs, tuning, wheelVisualBindings);

			return m_Vehicle;

		}



		/// Is a scene currently loaded and ready for rendering?

		bool IsSceneReady() const { return m_SceneState == VansSceneState::Ready; }





		VansSceneState GetSceneState() const { return m_SceneState; }





		bool IsSceneSwitching() const

		{

			return m_SceneState == VansSceneState::Unloading ||

				   m_SceneState == VansSceneState::Loading;

		}







		/// Load project-wide resources (mesh, texture, shader) from a

		/// AssetDatabase dependency closure. Called once after opening a project,

		/// before any scene is loaded.

		bool LoadProjectAssets(Vans::VansAssetDatabase& database,

			const std::filesystem::path& scenePath, VansVKDevice* device);



		/// Load a scene file and prepare all GPU resources (PBR, transform,

		/// descriptor sets, ray tracing).  Safe to call multiple times;

		/// will unload the previous scene first.

		void LoadSceneForRendering(const char* scenePath, VansVKDevice* device, VansSceneLoadMode mode = VansSceneLoadMode::Editor);



		/// Load scene content (materials, nodes, terrain, vegetation, etc.)

		/// from a scene file.  Assumes resources are already loaded via

		bool LoadSceneContent(const char* path);





		// Creates VansScriptObjects from a typed scene object build plan.
		void LoadSceneObjects(VkDevice& device, const Vans::VansSceneObjectBuildPlan& objectBuildPlan, const std::string& projectRoot);





		// Find a render node by name across all render node lists

		VansRenderNode* FindRenderNodeByName(const std::string& name) const;

		VansScriptObject* FindObjectByGuid(const std::string& guid) const;

		VansRenderNode* FindPrimaryRenderNodeByEntityGuid(const std::string& guid) const;





		const VansWaterConfig& GetWaterConfig() const;
		VansWaterConfig& EditWaterConfig();



		void RegistRenderNode(VansRenderNode* renderNode, RenderNodeType type);



        // =====================================================================

        // Runtime Dynamic Entity API锛堣繍琛屾椂鍔ㄦ€佸疄浣撳鍒狅級

        // =====================================================================





		VansScriptObject* CreateEntity(

			VkDevice& device, const std::string& entityName,

			const std::string& meshName, const std::string& materialName,

			const glm::vec3& position, const glm::vec3& rotation = glm::vec3(0.0f),

			const glm::vec3& scale = glm::vec3(1.0f),

			const std::string& parentName = "");





		bool DestroyEntity(const std::string& entityName);

		bool DestroyEntity(VansScriptObject* obj);





		bool CanCreateEntity() const;





		bool GrowTransformBuffer(VkDevice& device, uint32_t newCapacity);





		void UpdateTransformDescriptorSet();





		size_t GetTransformSlotCount()    const;

		size_t GetTransformSlotCapacity() const;

		float  GetTransformSlotUsage()    const;





	private:
		TransformSlotAllocator m_TransformSlotAllocator;

	public:



		void UnLoadScene();





		void UnloadProjectResources(VansVKDevice* device);



		void UpdateSceneData();







		void SyncLightTransforms();



		// spatial=true ? AudioComponent ????? OpenAL source?

		void SyncAudioSourcePositions();



		// Per-frame skeletal animation CPU update + GPU bone matrix upload.

		void UpdateAnimations(float deltaTime);



		// Update per-node GPU data once per frame before command buffer recording.

		void UpdateRenderNodesDataBeforeRecord();

		void SetMainCameraHiZCullSettings(const VansMainCameraHiZCullSettings& settings);
		const VansMainCameraHiZCullSettings& GetMainCameraHiZCullSettings() const { return m_MainCameraHiZCullSettings; }
		const VansMainCameraVisibilityStats& GetMainCameraVisibilityStats() const { return m_MainCameraVisibilityStats; }
		const std::vector<VansMainCameraHiZCulledNodeDebug>& GetMainCameraHiZCulledDebugNodes() const { return m_MainCameraHiZCulledDebugNodes; }
		void BuildMainCameraCullCandidates(VkExtent2D extent);
		bool UploadMainCameraCullCandidates(VansVKDevice& device);
		bool IsMainCameraNodeVisible(VansRenderNode* node) const;
		bool HasMainCameraHiZCullCandidates() const { return !m_MainCameraCullCandidates.empty(); }
		uint32_t GetMainCameraHiZCullCandidateCount() const { return static_cast<uint32_t>(m_MainCameraCullCandidates.size()); }
		VansVKBuffer& GetMainCameraCullObjectBuffer() { return m_MainCameraCullObjectBuffer; }
		VansVKBuffer& GetMainCameraVisibilityBuffer() { return m_MainCameraVisibilityBuffer; }
		void MarkMainCameraHiZCullDispatched();
		void ResetMainCameraHiZVisibility();
		void ReleaseMainCameraHiZGpuResources(VkDevice device);

	private:
		bool EnsureMainCameraHiZGpuResources(VansVKDevice& device, uint32_t candidateCount);
		void ConsumeMainCameraHiZReadback();
		void UpdateMainCameraHiZHistory(VkExtent2D extent);
		bool ShouldMainCameraCullClassRunHiZ(VansMainCameraCullClass cullClass) const;
		void AppendMainCameraCullCandidate(VansRenderNode* node, VansMainCameraCullClass cullClass, const glm::mat4& viewProjection);

	public:





		void RecordVideoUploads(VansVKCommandBuffer& cmd);



		void UpdatePhysicsTransforms();







		void UpdateCharControllerTransforms();



		// Cloth simulation: CPU advance + write results to staging buffers

		void UpdateClothSimulation(float dt);

		void WriteClothResultsToStagingBuffers();

		void RecordClothVertexUploads(VansVKCommandBuffer& cmd);



		// Vegetation: dispatch bone-sim + skinning compute passes on the given command buffer.

		// Must be called after UpdateSceneData() and before the deferred render pass begins.

		void RecordVegetationCompute(VansVKCommandBuffer& cmd);



	private:



		void UpdateTransformRenderData();



	public:



		void RemoveRenderNodeFromVector(VansRenderNode* node);

		std::vector<VansRenderNode*> CollectSSBOManagedRenderNodes() const;

		const std::unordered_map<std::string, MultiMeshGroup>& GetMultiMeshGroups() const { return m_MultiMeshGroups; }





		void UpdateLightComponentIndex(int oldIndex, int newIndex, VansLightType type);



	private:



		// Helper: loads or reuses a texture by its absolute file path.

		// Returns the existing VansTexture* if one with the same name was already loaded.

		VansTexture* LoadOrGetTexture(const std::string& absPath, bool isSRGB);



	public:



		void BuildRayTracingAS(VansVKDevice* vans_device, VansVKCommandBuffer* vans_commandBuffer);



		void ReleaseASTempBuffer(VansVKDevice* vans_device);



	public:



		void DrawShadowNodes();
		void DrawShadowNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);
		void DrawShadowNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end);
		void DrawHairShadowNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);
		void DrawVegetationShadowNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);



		void DrawMotionVectorNodes();
		void DrawMotionVectorNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);
		void DrawMotionVectorNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end);



		void DrawPunctualShadowJob(const VansPunctualShadowRenderJob& job);



		void DrawSkyBoxNode();



		void DrawOpaqueNodes();
		void DrawOpaqueNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);
		void DrawOpaqueNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end);

		void DrawHairVisibilityNodes();

		void DrawHairDeepOpacityNodes(VansGraphicsShader* shader);



		void DrawTerrainNode(bool shadowPass = false, bool motionVectorPass = false);
		void DrawTerrainNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, bool shadowPass = false, bool motionVectorPass = false);





		void DrawWaterGBufferNode();





		void DrawWaterCompositeNode();



		void DrawWaterNode();



		void DrawVegetationNode();
		void DrawVegetationNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);



		void DrawForwardOpaqueAfterDeferredNodes();



		void DrawTransParentNodes();








		void DrawPostProcessNodes();



		void DrawScreenSpaceFeatureNode();





		void DrawDecalNodes();
		void DrawDecalNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData);
		void DrawDecalNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end);



		void DeferredShading();



	public:



		void InjectCamera(VansCamera* camera) { m_Camera = camera; }



		VansMaterialManager* GetMaterialManager() { return &m_MaterialManager; }



		VansReflectionProbeSystem* GetReflectionProbeSystem() { return &m_ReflectionProbeSystem; }

		const VansReflectionProbeSystem* GetReflectionProbeSystem() const { return &m_ReflectionProbeSystem; }

		const VansGISettings& GetGISettings() const { return m_GISettings; }

		void SetGISettings(const VansGISettings& settings)
		{
			const bool resourceLayoutChanged =
				m_GISettings.gridDimensions != settings.gridDimensions ||
				m_GISettings.raysPerProbe != settings.raysPerProbe ||
				m_GISettings.probeSpacingAxes != settings.probeSpacingAxes ||
				m_GISettings.maxRayDistance != settings.maxRayDistance ||
				m_GISettings.regionCenter.x != settings.regionCenter.x ||
				m_GISettings.regionCenter.y != settings.regionCenter.y ||
				m_GISettings.regionCenter.z != settings.regionCenter.z;

			m_GISettings = settings;
			if (resourceLayoutChanged)
			{
				m_GIProbeResourcesDirty = true;
			}
			m_GIParametersDirty = true;
		}

		bool AreGIProbeResourcesDirty() const { return m_GIProbeResourcesDirty; }
		bool AreGIParametersDirty() const { return m_GIParametersDirty; }
		void ClearGIProbeResourcesDirty() { m_GIProbeResourcesDirty = false; }
		void ClearGIParametersDirty() { m_GIParametersDirty = false; }



		VansLightManager* GetLightManager() { return &m_LightManager; }
		const VansLightManager* GetLightManager() const { return &m_LightManager; }
		bool HasPunctualShadowJobs() const
		{
			return !m_LightManager.GetPunctualShadowManager().GetRenderJobs().empty();
		}



		VansIESProfileManager* GetIESProfileManager() { return &m_IESProfileManager; }



		VansCamera* GetCamera() { return m_Camera; }

		const VansCamera* GetCamera() const { return m_Camera; }



		VansVideoManager* GetVideoManager() { return &m_VideoManager; }

		const VansVideoManager* GetVideoManager() const { return &m_VideoManager; }



		VansEngine::VansAudioManager* GetAudioManager() { return &m_AudioManager; }

		const VansEngine::VansAudioManager* GetAudioManager() const { return &m_AudioManager; }



		VansSceneLoadMode GetLoadMode() const { return m_LoadMode; }



		VkAccelerationStructureKHR& GetTopAS() { return m_TopLevelAS; }



		std::vector<VansVKBuffer>& GetBLASVertexBuffers() { return m_BLASVertexData; }



		std::vector<VansVKBuffer>& GetBLASIndexBuffers() { return m_BLASIndexData; }



		std::vector<uint32_t>& GetTLASInstanceData() { return m_TLASInstaneData; }



		std::vector<VansVKImage>& GetTLASInstanceTextures() { return m_TlasInstanceTextures; }



		std::vector<uint32_t>& GetTLASInstanceTextureIndex() { return m_TlasInstanceTextureIndex; }





		bool HasDecalNodes() const { return !m_DecalRenderNodes.empty(); }



		bool HasForwardOpaqueAfterDeferredNodes() const { return !m_ForwardOpaqueAfterDeferredRenderNodes.empty(); }



	private:





		VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;



		VansVKBuffer m_TopLevelASBuffer;



		VansVKBuffer m_InstancesBuffer;



		VansVKBuffer m_TLASScratchBuffer;



		std::vector<VkAccelerationStructureInstanceKHR> m_TlasInstancesInfos;



		// Collection of geometries for the acceleration structure.

		std::vector<VkAccelerationStructureGeometryKHR> m_AsGeometry;



		// Build range information corresponding to each geometry.

		std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_AsBuildRangeInfo;



		std::vector<VansVKBuffer> m_BLASVertexData;



		std::vector<VansVKBuffer> m_BLASIndexData;



		std::vector<uint32_t> m_TLASInstaneData;



		// bindless ????



		std::vector<uint32_t> m_TlasInstanceTextureIndex;



		std::vector<VansVKImage> m_TlasInstanceTextures;

		std::map<std::string,int> m_TlasInstanceMaterialToIndex;



	private:



		VansSceneState   m_SceneState    = VansSceneState::Empty;

		VansSceneLoadMode m_LoadMode     = VansSceneLoadMode::Editor;

		bool m_ResourcesLoaded = false;

		VansVKDevice* m_RuntimeResourceDevice = nullptr;







	private:

		VansVideoManager m_VideoManager;







		// AssetDatabase records are uploaded by the legacy resource batch executor and released with the project.

		VansEngine::VansAudioManager m_AudioManager;



	private:

		// Keep newly added scene data at the end to preserve existing member offsets.

		VansGISettings m_GISettings;
		bool m_GIProbeResourcesDirty = false;
		bool m_GIParametersDirty = false;

	};

}



extern VansGraphics::VansScene* m_Scene;

