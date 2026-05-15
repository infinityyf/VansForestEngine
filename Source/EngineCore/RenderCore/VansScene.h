#pragma once
#include "VansRenderNode.h"
#include "VansShaderRegistry.h"
#include "VansCamera.h"
#include "BRDFData/VansLight.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansAnimationController.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "../ScriptCore/VansScriptContext.h"
#include "VansVideoManager.h"
#include "../AudioCore/VansAudioManager.h"
#include "VansParticleRenderNode.h"
#include <vector>
#include <map>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace VansGraphics
{
	// ── 场景加载状态枚举 ──────────────────────────────────────────────────
	enum class VansSceneState
	{
		Empty,       // 无场景
		Unloading,   // 正在卸载旧场景
		Loading,     // 正在加载新场景
		Ready        // 场景就绪，可渲染
	};

	// ── 场景加载模式枚举 ──────────────────────────────────────────────────
	// Editor：编辑器模式，启用编辑器相机控制，时间默认冻结
	// Runtime：运行时模式，使用场景配置相机，时间正常推进
	enum class VansSceneLoadMode
	{
		Editor,   // 编辑器模式
		Runtime   // 运行时模式
	};

	// A logical group representing a multi-mesh parent and all its auto-expanded child render nodes.
	struct MultiMeshGroup
	{
		std::string parentName;
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
		void CreateNodeDescriptorSets();
	private:

		VansCamera* m_Camera;

		VansLightManager m_LightManager;

		VansMaterialManager m_MaterialManager;

		VansAsset* GetMeshAsset(const std::string& name);

		VansAsset* GetShaderAsset(const std::string& name);

		VansAsset* GetTextureAsset(const std::string& name);

		VansAsset* GetMaterialAsset(const std::string& name);

		void RegistRenderNode(VansRenderNode* renderNode, RenderNodeType type);


	public:

		//记录所有资产
		std::vector<VansAsset*> m_Meshes;

		std::vector<VansAsset*> m_Textures;

		std::vector<VansAsset*> m_Shaders;

		//记录运行时数据
		std::vector<VansAsset*> m_Materials;

	public:
		VansRenderNode* m_SkyBoxNode = nullptr;

		VansRenderNode* m_DeferredNode = nullptr;

		std::vector<VansRenderNode*> m_OpaqueRenderNodes;

		VansRenderNode* m_TerrainRenderNode = nullptr;

		VansRenderNode* m_VegetationRenderNode = nullptr;

		VansVegetationSystem* m_VegetationSystem = nullptr;

		std::vector<VansRenderNode*> m_TransParentRenderNodes;

		std::vector<VansRenderNode*> m_PostProcessRenderNodes;

		std::vector<VansRenderNode*> m_ScreenSpaceRenderNodes;

		// OBB 贴花节点，在 GBuffer pass 之后、Deferred pass 之前执行
		std::vector<VansRenderNode*> m_DecalRenderNodes;

		// 粒子渲染节点，在 Transparent Pass 末尾执行实例化 Billboard 绘制
		std::vector<VansParticleRenderNode*> m_ParticleRenderNodes;

		// Multi-mesh parent groups for hierarchical display in the editor.
		// Key = parent group name, Value = group info with child node pointers.
		std::unordered_map<std::string, MultiMeshGroup> m_MultiMeshGroups;

		// ── Skeletal animation nodes ────────────────────────────────────────────────
		// Created automatically in ExpandMultiMeshToRenderNodes().
		std::vector<VansAnimationNode*> m_AnimationNodes;

		// ── Animation controllers (每个 AnimationNode 绑定一个)────────────────
		std::vector<VansAnimationController*> m_AnimationControllers;

		// Shared dummy buffers for non-animated render nodes (64 bytes, device-local-ish).
		// Bound at Object descriptor set bindings 1 & 2 instead of real bone data.
		VansVKBuffer m_DummyBoneIDBuffer;
		VansVKBuffer m_DummyBoneBuffer;
		VansVKBuffer m_DummyWeightBuffer;

		// Physics nodes
		std::vector<VansEngine::VansPhysicsNode*> m_PhysicsNodes;

		// Cloth simulation nodes
		std::vector<VansEngine::VansClothNode*> m_ClothNodes;

		// Character Controller nodes
		std::vector<VansEngine::VansCharacterControllerNode*> m_CharControllerNodes;

		// Per-cloth HOST_VISIBLE staging buffers owned by the scene.
		// Indexed parallel to m_ClothNodes.  Written from the CPU cloth results
		// each frame and copied to the device-local mesh vertex buffer via
		// vkCmdCopyBuffer inside the scene's GPU upload pass.
		std::vector<VansVKBuffer> m_ClothStagingBuffers;

		// Vehicle
		VansEngine::VansPhysicsVehicle* m_Vehicle = nullptr;
		// Initialize the vehicle in the scene from JSON-specified parameters.
		// Render node name bindings are stored on the vehicle itself.
		void InitVehicle(VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,
			const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames);

		// ── ScriptableObject layer ──────────────────────────────────────────
		// Each VansScriptObject groups render / physics / cloth components for
		// one logical scene entity.  Objects only hold non-owning pointers to
		// Nodes — the Nodes themselves are still managed by the flat lists above.
		std::vector<VansScriptObject*> m_SceneObjects;

		// Find an object by its logical name.
		VansScriptObject* FindObjectByName(const std::string& name) const;

	public:

		VansVKBuffer m_InstanceTransformDataBuffer;
		std::vector<ModelDataStruct> m_InstanceTransformData;
		VkDescriptorSetLayout m_GlobalTransformDataSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalTransformDataDescriptorSets;

		// ===== New: Reorganized descriptor set system =====
		// Global descriptor set (Set 0): Camera + Lights + Materials + IBL + Bindless
		VkDescriptorSetLayout m_GlobalDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

		// Object descriptor set (Set 2): Transform SSBO — shared by all geometry nodes
		VkDescriptorSetLayout m_ObjectDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_ObjectDescriptorSet = VK_NULL_HANDLE;

		// Animation descriptor set (Set 3): Bone Matrices SSBO + Bone Weight SSBO
		// Per-node for animated VansCommonRenderNodes; static nodes bind this shared dummy set.
		VkDescriptorSetLayout m_AnimationDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_AnimationDescriptorSet = VK_NULL_HANDLE;  // shared dummy for static nodes

		// Empty pass layout (Set 1) for passes that have no per-pass resources
		VkDescriptorSetLayout m_EmptyPassLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_EmptyPassDescriptorSet = VK_NULL_HANDLE;

		// Creates the global Set 0 descriptor set and writes all global resources into it
		void CreateGlobalDescriptorSet(VkDevice device);
		void UpdateGlobalDescriptorSet();		// Writes only TileLight bindings (9, 10) into the global descriptor set.
		// Called after PrepareTileLightData() creates the TileLight SSBO buffers.
		void UpdateGlobalTileLightDescriptors();
	public:
		//editor
		VansRenderNode* m_SelectedNode = nullptr;
		VansScriptObject* m_SelectedObject = nullptr;

		// ── Transform parenting system ──────────────────────────────────────────
		VansTransformParentSystem m_TransformParentSystem;

	public:

		// ── 场景 / 资源状态查询 ──────────────────────────────────────────

		/// Are project resources loaded? (mesh/texture/shader)
		bool AreResourcesLoaded() const { return m_ResourcesLoaded; }

		/// Is a scene currently loaded and ready for rendering?
		bool IsSceneReady() const { return m_SceneState == VansSceneState::Ready; }

		/// 获取当前场景状态
		VansSceneState GetSceneState() const { return m_SceneState; }

		/// 是否正处于场景切换过程中（卸载或加载）
		bool IsSceneSwitching() const
		{
			return m_SceneState == VansSceneState::Unloading ||
				   m_SceneState == VansSceneState::Loading;
		}

		// ── 项目资源 / 场景加载入口 ────────────────────────────────────────

		/// Load project-wide resources (mesh, texture, shader) from a
		/// resource.json file.  Called once after opening a project,
		/// before any scene is loaded.
		void LoadProjectResources(const char* resourceJsonPath, VansVKDevice* device);

		/// Load a scene file and prepare all GPU resources (PBR, transform,
		/// descriptor sets, ray tracing).  Safe to call multiple times;
		/// will unload the previous scene first.
		void LoadSceneForRendering(const char* scenePath, VansVKDevice* device, VansSceneLoadMode mode = VansSceneLoadMode::Editor);

		/// Load only project-wide resources (mesh, texture, shaders) from a
		/// parsed resource JSON.  Called once per project, before any scene load.
		void LoadResources(json& resourceData);

		/// Load scene content (materials, nodes, terrain, vegetation, etc.)
		/// from a scene file.  Assumes resources are already loaded via
		/// LoadResources().
		bool LoadSceneContent(const char* path);

		void LoadRenderNodes(VkDevice& device, json& render_node);

		// ── ScriptableObject-based loading (new JSON "objects" format) ───────
		// Parses the "objects" array from scene JSON: creates VansScriptObjects
		// with render / physics / cloth / vehicle / animation components.
		void LoadSceneObjects(VkDevice& device, json& objectsArray, const std::string& projectRoot);

		// 单个 animation component 加载（供 LoadSceneObjects 第四阶段调用）
		VansAnimationNode* LoadSingleAnimationComponent(const json& animJson,
		                                                const std::string& objectName,
		                                                const std::string& projectRoot);

		// ── Single-node loading helpers (extracted from batch loaders) ────────
		VansRenderNode* LoadSingleRenderNode(VkDevice& device, const json& renderNodeJson);
		VansEngine::VansPhysicsNode* LoadSinglePhysicsNode(const json& physicsNodeJson, VansRenderNode* associatedRenderNode);
		VansEngine::VansClothNode*   LoadSingleClothNode(const json& clothNodeJson, VansRenderNode* associatedRenderNode);

		// Find a render node by name across all render node lists
		VansRenderNode* FindRenderNodeByName(const std::string& name) const;

		void AddTerrainNode(VansVKDevice* device, json& terrainData);

		void AddVegetationNode(VkDevice& device, json& vegetationData);

		void AddDeferredNode(VkDevice& device);

		void AddScreenSpaceFeatureNode(VkDevice& device);

		void UnLoadScene();

		void UpdateSceneData();

		// 每帧在更新灯光阴影矩阵前，将 ScriptObject 的 Transform 同步到灯光结构体。
		// 方向光/聚光灯同步旋转 Z 轴为 m_Direction；点光源/聚光灯同步 m_Position。
		void SyncLightTransforms();

		// 每帧将拥有 spatial=true 的 AudioComponent 的世界坐标同步到 OpenAL source。
		void SyncAudioSourcePositions();

		// Per-frame skeletal animation CPU update + GPU bone matrix upload.
		void UpdateAnimations(float deltaTime);

		// Update per-node GPU data once per frame before command buffer recording.
		void UpdateRenderNodesDataBeforeRecord();

		void UpdatePhysicsTransforms();

		// 每帧：提交所有 CCT 的待执行位移，并将物理位置同步回 Transform
		// 必须在 UpdatePhysicsTransforms() 之后调用
		void UpdateCharControllerTransforms();

		// 从 JSON 加载单个 CharController（由 LoadSceneObjects Pass1 调用）
		VansEngine::VansCharacterControllerNode* LoadSingleCharControllerNode(
			const json& charCtrlJson,
			VansRenderNode* associatedRenderNode);

		// Cloth simulation: CPU advance + write results to staging buffers
		void UpdateClothSimulation(float dt);
		void WriteClothResultsToStagingBuffers();
		void RecordClothVertexUploads(VkCommandBuffer cmd);

		// Vegetation: dispatch bone-sim + skinning compute passes on the given command buffer.
		// Must be called after UpdateSceneData() and before the deferred render pass begins.
		void RecordVegetationCompute(VansVKCommandBuffer& cmd);

	private:

		void UpdateTransformRenderData();

	private:

		void ImportDefaultTextures(const std::string& path, const std::string& name, VansVKDevice* vkDevice, bool isSRGB);

		/// Load materials from a JSON array.  Resources (meshes, textures,
		/// shaders) must already be loaded.  Used by LoadSceneContent().
		void LoadMaterialsFromJson(const json& materialData);

		// ── Multi-mesh auto-splitting ─────────────────────────────────────────
		// Given a multi-mesh asset, automatically creates one render node per
		// submesh with auto-generated materials + textures based on FBX data.
		// parentTransform carries the transform from the JSON render node entry.
		// supportShadow controls whether shadow nodes are also created.
		void ExpandMultiMeshToRenderNodes(
			VkDevice& device,
			VansMesh* multiMesh,
			const std::string& parentName,
			const glm::vec3& position,
			const glm::vec3& rotation,
			const glm::vec3& scale,
			bool supportShadow);

		// Helper: loads or reuses a texture by its absolute file path.
		// Returns the existing VansTexture* if one with the same name was already loaded.
		VansTexture* LoadOrGetTexture(const std::string& absPath, bool isSRGB);

		// Helper: creates and registers one shader from a registry entry.
		// No-op if the shader is already loaded. Used by LoadShadersFromRegistry.
		void LoadShaderFromEntry(const VansShaderRegistryEntry& entry,
			                     const std::string& pathPrefix, VkDevice& device);

		// Loads all scene meshes from the JSON 'mesh' array.
		void LoadMeshesFromJson(const json& meshData, const std::string& pathPrefix,
			                    VkDevice& device, VansVKDevice* vkDevice);

		// Loads all shaders from the C++ shader registry.
		void LoadShadersFromRegistry(const std::string& pathPrefix, VkDevice& device);

		// Loads all scene textures from the JSON 'texture' array and imports engine defaults.
		// pathPrefix    = project root (for scene-relative asset paths)
		// enginePrefix  = engine root  (for default textures in EngineAssets/)
		void LoadTexturesFromJson(const json& textureData, const std::string& pathPrefix,
			                      const std::string& enginePrefix, VansVKDevice* vkDevice);

	public:

		void BuildRayTracingAS(VansVKDevice* vans_device, VansVKCommandBuffer* vans_commandBuffer);

		void ReleaseASTempBuffer(VansVKDevice* vans_device);

	public:

		void DrawShadowNodes();

		void DrawMotionVectorNodes();

		void DrawPointShadow(int lightIndex);

		void DrawSpotShadow(int pointCount, int lightIndex);

		void DrawRectShadow(int pointCount, int spotCount, int lightIndex);

		void DrawSkyBoxNode();

		void DrawOpaqueNodes();

		void DrawTerrainNode(bool shadowPass = false, bool motionVectorPass = false);

		void DrawVegetationNode();

		void DrawTransParentNodes();

		// 绘制所有粒子节点（在 Transparent Pass 末尾调用）
		void DrawParticleNodes();

		void DrawPostProcessNodes();

		void DrawScreenSpaceFeatureNode();

		// 绘制所有贴花节点（在 GBuffer pass 之后、Deferred pass 之前调用）
		void DrawDecalNodes();

		void DeferredShading();

	public:

		void InjectCamera(VansCamera* camera) { m_Camera = camera; }

		VansMaterialManager* GetMaterialManager() { return &m_MaterialManager; }

		VansLightManager* GetLightManager() { return &m_LightManager; }

		VansCamera* GetCamera() { return m_Camera; }

		// 获取场景加载模式（Editor / Runtime）
		VansSceneLoadMode GetLoadMode() const { return m_LoadMode; }

		VkAccelerationStructureKHR& GetTopAS() { return m_TopLevelAS; }

		std::vector<VansVKBuffer>& GetBLASVertexBuffers() { return m_BLASVertexData; }

		std::vector<VansVKBuffer>& GetBLASIndexBuffers() { return m_BLASIndexData; }

		std::vector<uint32_t>& GetTLASInstanceData() { return m_TLASInstaneData; }

		std::vector<VansVKImage>& GetTLASInstanceTextures() { return m_TlasInstanceTextures; }

		std::vector<uint32_t>& GetTLASInstanceTextureIndex() { return m_TlasInstanceTextureIndex; }

		// 是否存在贴花节点（用于 renderer 决策是否插入 Decal Pass）
		bool HasDecalNodes() const { return !m_DecalRenderNodes.empty(); }

	private:

		//光线追踪加速结构
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

		//bindless 贴图的索引
		//每个instance都有一个索引
		std::vector<uint32_t> m_TlasInstanceTextureIndex;
		//global的贴图资源，会绑定到bindless descriptor set
		std::vector<VansVKImage> m_TlasInstanceTextures;
		std::map<std::string,int> m_TlasInstanceMaterialToIndex;

	private:
		// ── 场景 / 资源加载状态 ────────────────────────────────────────────
		VansSceneState   m_SceneState    = VansSceneState::Empty;
		VansSceneLoadMode m_LoadMode     = VansSceneLoadMode::Editor;
		bool m_ResourcesLoaded = false;

		// ── 视频纹理管理器 ────────────────────────────────────────────────────
		// 管理当前场景内所有视频纹理的生命周期与每帧更新。
		// LoadSceneContent() 中从 JSON 加载，UnLoadScene() 中清理。
	public:
		VansVideoManager m_VideoManager;

		// ── 音频管理器 ────────────────────────────────────────────────────
		// 管理当前场景内所有音频节点的生命周期与每帧播放驱动。
		// LoadResources() 中从 resource.json 加载，UnLoadProject() 中清理。
		VansEngine::VansAudioManager m_AudioManager;
	};
}

extern VansGraphics::VansScene* m_Scene;

class VansAssetsFileWatcher;
extern VansAssetsFileWatcher* m_SceneFileWatcher;
