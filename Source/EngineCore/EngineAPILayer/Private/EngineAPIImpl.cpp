#include "EngineAPIImpl.h"

#include "EngineCommandContext.h"
#include "../Public/EngineEvents.h"
#include "ModelAssetPlacementPreparationService.h"
#include "RuntimeGeneratedMaterialAssetService.h"
#include "ScenePropertyValueBuilders.h"
#include "VansMaterialLiveEditService.h"
#include "VansEditorTextureBridge.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansCamera.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/ReflectionProbeCore/VansReflectionProbeSystem.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../RenderCore/VansShaderManager.h"
#include "../../RenderCore/TerrainCore/VansTerrain.h"
#include "../../RenderCore/WaterCore/VansWaterFFT.h"
#include "../../RenderCore/WaterCore/VansWaterSystem.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansVKImage.h"
#include "../../RenderCore/VulkanCore/VansVKMemoryManager.h"
#include "../../RenderCore/VulkanCore/VansPipelineRegistry.h"
#include "../../RenderCore/VulkanCore/VansRenderDocCapture.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansPhysicsVehicle.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneEntityFactory.h"
#include "../../SceneCore/VansSceneRuntimeProjection.h"
#include "../../SceneCore/VansSceneSchema.h"
#include "../../SceneCore/VansSceneRuntimeComponentKey.h"
#include "../../SceneCore/VansSceneContentBuildPlan.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../RuntimeUI/Public/VansUIDocument.h"
#include "../../RuntimeUI/Public/VansUIScreen.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include "../../RuntimeUI/Serialization/VansUIDocumentLoader.h"
#include "../../RuntimeUI/Serialization/VansUIDocumentMigrator.h"
#include "../../RuntimeUI/Serialization/VansUIDocumentValidator.h"
#include "../../RuntimeUI/Serialization/VansUIScreenConfigReader.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../EventCore/VansEventBus.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../AudioCore/VansAudioReverbEnvironment.h"
#include "../../AudioCore/VansAudioSystem.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../../Graphics/Vulkan/VansVKFunctions.h"

#include "../../Util/VansInputManager.h"
#include "../../Util/VansLog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans::EditorAPI
{
	namespace
	{
		struct PreviewTextureCache
		{
			RenderTextureId id = 0;
			VkImageView imageView = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			EditorTextureHandle texture = nullptr;
		};

		struct LayerPreviewCache
		{
			RenderTextureId id = 0;
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			std::uint32_t layer = UINT32_MAX;
			EditorTextureHandle texture = nullptr;
		};

		struct SinglePreviewCache
		{
			VansGraphics::VansTexture* sourceTexture = nullptr;
			VkImageView imageView = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			EditorTextureHandle texture = nullptr;
		};

		std::vector<PreviewTextureCache>& GetImagePreviewCaches()
		{
			static std::vector<PreviewTextureCache> caches;
			return caches;
		}

		std::vector<LayerPreviewCache>& GetLayerPreviewCaches()
		{
			static std::vector<LayerPreviewCache> caches;
			return caches;
		}

		SinglePreviewCache& GetViewportPreviewCache()
		{
			static SinglePreviewCache cache;
			return cache;
		}

		SinglePreviewCache& GetReflectionProbePreviewCache()
		{
			static SinglePreviewCache cache;
			return cache;
		}

		void RetireEditorTexture(VansGraphics::VansVKDevice* device, EditorTextureHandle texture)
		{
			if (!texture)
				return;

			if (device)
			{
				device->EnqueueDeferredDelete([texture]()
				{
					Vans::Editor::VansEditorTextureBridge::RemoveTexture(texture);
				});
				return;
			}

			Vans::Editor::VansEditorTextureBridge::RemoveTexture(texture);
		}

		void RetireImageView(VansGraphics::VansVKDevice* device, VkDevice logicalDevice, VkImageView view)
		{
			if (view == VK_NULL_HANDLE || logicalDevice == VK_NULL_HANDLE)
				return;

			if (device)
			{
				device->EnqueueDeferredDelete([logicalDevice, view]() mutable
				{
					VansGraphics::VansVKImage::DestroyImageView(logicalDevice, view);
				});
				return;
			}

			VansGraphics::VansVKImage::DestroyImageView(logicalDevice, view);
		}

		std::filesystem::path ResolveEditorUIAssetPath(const std::string& path)
		{
			std::filesystem::path candidate(path);
			if (candidate.is_absolute())
				return candidate;

			auto& projectManager = Vans::VansProjectManager::Get();
			if (projectManager.IsProjectLoaded())
				return std::filesystem::path(projectManager.ResolveAssetPath(path));

			if (auto* configuration = VansConfigration::GetInstance())
				return std::filesystem::path(configuration->GetProjectRootPath()) / path;

			return candidate;
		}

		const char* UIScreenLayerToString(VansRuntime::VansUIScreenLayer layer)
		{
			switch (layer)
			{
			case VansRuntime::VansUIScreenLayer::WorldSpaceUI: return "WorldSpaceUI";
			case VansRuntime::VansUIScreenLayer::HUD: return "HUD";
			case VansRuntime::VansUIScreenLayer::Screen: return "Screen";
			case VansRuntime::VansUIScreenLayer::ModalDim: return "ModalDim";
			case VansRuntime::VansUIScreenLayer::Modal: return "Modal";
			case VansRuntime::VansUIScreenLayer::Tooltip: return "Tooltip";
			case VansRuntime::VansUIScreenLayer::Toast: return "Toast";
			case VansRuntime::VansUIScreenLayer::DebugUI: return "DebugUI";
			default: return "Screen";
			}
		}

		void RecordPreviewImageBarrier(
			VkCommandBuffer cmd,
			VkImage image,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VkAccessFlags srcAccess,
			VkAccessFlags dstAccess,
			VkPipelineStageFlags srcStage,
			VkPipelineStageFlags dstStage)
		{
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			VansGraphics::vkCmdPipelineBarrier(
				cmd,
				srcStage,
				dstStage,
				0,
				0,
				nullptr,
				0,
				nullptr,
				1,
				&barrier);
		}

		bool CreateUIPreviewRenderPass(
			VkDevice device,
			VkFormat format,
			VkRenderPass& renderPass,
			std::string& error)
		{
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format = format;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorReference{};
			colorReference.attachment = 0;
			colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorReference;

			VkSubpassDependency dependencies[2]{};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = 0;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			VkRenderPassCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			createInfo.attachmentCount = 1;
			createInfo.pAttachments = &colorAttachment;
			createInfo.subpassCount = 1;
			createInfo.pSubpasses = &subpass;
			createInfo.dependencyCount = 2;
			createInfo.pDependencies = dependencies;

			const VkResult result = VansGraphics::vkCreateRenderPass(
				device,
				&createInfo,
				nullptr,
				&renderPass);
			if (result != VK_SUCCESS)
			{
				error = "Failed to create UI preview render pass.";
				return false;
			}
			return true;
		}

		bool ReadEditorUIScreenConfig(const std::string& path,
		                              VansRuntime::VansUIScreenConfig& config,
		                              std::vector<std::string>& diagnostics)
		{
			if (path.find(".vui.") == std::string::npos)
				return false;

			VansRuntime::VansUIAssetDocument document;
			std::string error;
			if (!VansRuntime::VansUIDocumentLoader::Load(ResolveEditorUIAssetPath(path), document, error))
			{
				diagnostics.push_back("Config: failed to read " + path + " (" + error + ")");
				return false;
			}

			if (!VansRuntime::VansUIDocumentMigrator::MigrateToCurrent(
				document,
				VansRuntime::VansUIDocumentKind::Screen,
				diagnostics))
				return false;

			return VansRuntime::VansUIScreenConfigReader::Read(
				document.root,
				config,
				diagnostics) &&
				VansRuntime::VansUIDocumentValidator::ValidateScreenConfig(config, diagnostics);
		}

		void AppendScreenConfigDiagnostics(const std::string& path, UIDiagnosticsSnapshot& diagnostics)
		{
			if (path.find(".vui.") == std::string::npos)
				return;

			VansRuntime::VansUIScreenConfig config;
			std::vector<std::string> configDiagnostics;
			const bool configOk = ReadEditorUIScreenConfig(path, config, configDiagnostics);

			diagnostics.messages.push_back(configOk ? "Config: valid screen asset." : "Config: invalid screen asset.");
			for (const std::string& message : configDiagnostics)
				diagnostics.messages.push_back(message);

			diagnostics.messages.push_back("XAML: " + config.xamlPath);
			diagnostics.messages.push_back("Budget: drawCalls <= " + std::to_string(config.performanceBudget.maxDrawCalls)
				+ ", textureMB <= " + std::to_string(config.performanceBudget.maxTextureMemoryMB)
				+ ", layoutMs <= " + std::to_string(config.performanceBudget.maxLayoutMs));
			for (const std::string& tokenPath : config.tokens)
				diagnostics.messages.push_back("Tokens: " + tokenPath);
			for (const std::string& localizationPath : config.localization)
				diagnostics.messages.push_back("Localization: " + localizationPath);
		}

		void ClearEditorRenderTexturePreviewCaches(VansGraphics::VansVKDevice* device)
		{
			const VkDevice logicalDevice = device ? device->GetLogicDevice() : VK_NULL_HANDLE;
			for (auto& cache : GetImagePreviewCaches())
			{
				RetireEditorTexture(device, cache.texture);
				cache = {};
			}
			GetImagePreviewCaches().clear();

			for (auto& cache : GetLayerPreviewCaches())
			{
				RetireEditorTexture(device, cache.texture);
				RetireImageView(device, logicalDevice, cache.view);
				cache = {};
			}
			GetLayerPreviewCaches().clear();

			auto& viewportCache = GetViewportPreviewCache();
			RetireEditorTexture(device, viewportCache.texture);
			viewportCache = {};

			auto& reflectionCache = GetReflectionProbePreviewCache();
			RetireEditorTexture(device, reflectionCache.texture);
			reflectionCache = {};
		}

		RenderTexturePreview BuildImagePreview(
			VansGraphics::VansVKDevice* device,
			RenderTextureId id,
			const char* name,
			VansGraphics::VansVKImage& image,
			VkImageLayout layout,
			VkSampler samplerOverride = VK_NULL_HANDLE)
		{
			RenderTexturePreview preview;
			preview.id = id;
			preview.name = name ? name : "";

			VkImageView imageView = image.GetImageView();
			VkSampler sampler = samplerOverride != VK_NULL_HANDLE ? samplerOverride : image.GetSampler();
			if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
				return preview;

			auto& caches = GetImagePreviewCaches();
			auto it = std::find_if(caches.begin(), caches.end(),
				[id](const PreviewTextureCache& cache)
				{
					return cache.id == id;
				});
			if (it == caches.end())
				it = caches.insert(caches.end(), PreviewTextureCache{ id });

			if (!it->texture || it->imageView != imageView || it->sampler != sampler || it->layout != layout)
			{
				RetireEditorTexture(device, it->texture);
				it->imageView = imageView;
				it->sampler = sampler;
				it->layout = layout;
				it->texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
					sampler,
					imageView,
					layout);
			}

			VkExtent3D extent = image.GetImageDimension();
			preview.texture = it->texture;
			preview.width = extent.width;
			preview.height = extent.height;
			return preview;
		}

		RenderTexturePreview BuildLayerImagePreview(
			VansGraphics::VansVKDevice* renderDevice,
			RenderTextureId id,
			const char* name,
			VansGraphics::VansVKImage& image,
			std::uint32_t requestedLayer,
			VkImageLayout layout)
		{
			RenderTexturePreview preview;
			preview.id = id;
			preview.name = name ? name : "";

			const VkDevice logicalDevice = renderDevice ? renderDevice->GetLogicDevice() : VK_NULL_HANDLE;
			if (logicalDevice == VK_NULL_HANDLE || image.GetImage() == VK_NULL_HANDLE || image.GetSampler() == VK_NULL_HANDLE)
				return preview;

			const uint32_t layerCount = std::max(image.GetImageCreateInfo().arrayLayers, 1u);
			const uint32_t layer = std::min(requestedLayer, layerCount - 1u);

			auto& caches = GetLayerPreviewCaches();

			auto it = std::find_if(caches.begin(), caches.end(),
				[id](const LayerPreviewCache& cache)
				{
					return cache.id == id;
				});
			if (it == caches.end())
				it = caches.insert(caches.end(), LayerPreviewCache{ id });

			if (!it->texture || it->image != image.GetImage() || it->layer != layer)
			{
				RetireEditorTexture(renderDevice, it->texture);
				it->texture = nullptr;
				if (it->view != VK_NULL_HANDLE)
				{
					RetireImageView(renderDevice, logicalDevice, it->view);
					it->view = VK_NULL_HANDLE;
				}

				it->view = image.CreateLayerMipView(logicalDevice, layer, 0);
				it->image = image.GetImage();
				it->layer = layer;
				if (it->view != VK_NULL_HANDLE)
				{
					it->texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
						image.GetSampler(),
						it->view,
						layout);
				}
			}

			VkExtent3D extent = image.GetImageDimension();
			preview.texture = it->texture;
			preview.width = extent.width;
			preview.height = extent.height;
			return preview;
		}

		VansGraphics::VansTerrain* GetRuntimeTerrain(VansGraphics::VansScene* scene)
		{
			if (!scene)
				return nullptr;

			auto* renderNode = scene->GetTerrainRenderNode();
			auto* terrainNode = dynamic_cast<VansGraphics::VansTerrainRenderNode*>(renderNode);
			return terrainNode ? terrainNode->GetTerrain() : nullptr;
		}

		VansScriptContext& GetDefaultScriptContext()
		{
			static VansScriptContext scriptContext;
			return scriptContext;
		}

		Vec3 ToEditorVec3(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		Vec3 ToEditorVec3(const physx::PxVec3& value)
		{
			return { value.x, value.y, value.z };
		}

		glm::vec3 ToRuntimeVec3(const Vec3& value)
		{
			return glm::vec3(value.x, value.y, value.z);
		}

		Vec2 ToEditorVec2(const glm::vec2& value)
		{
			return { value.x, value.y };
		}

		Vec4 ToEditorVec4(const glm::vec4& value)
		{
			return { value.x, value.y, value.z, value.w };
		}

		const char* ToMainCameraCullClassLabel(VansGraphics::VansMainCameraCullClass cullClass)
		{
			switch (cullClass)
			{
			case VansGraphics::VansMainCameraCullClass::Opaque:
				return "Opaque";
			case VansGraphics::VansMainCameraCullClass::Hair:
				return "Hair";
			case VansGraphics::VansMainCameraCullClass::Transparent:
				return "Transparent";
			case VansGraphics::VansMainCameraCullClass::ForwardOpaqueAfterDeferred:
				return "Forward Opaque";
			case VansGraphics::VansMainCameraCullClass::Decal:
				return "Decal";
			default:
				return "Unknown";
			}
		}

		glm::vec2 ToRuntimeVec2(const Vec2& value)
		{
			return glm::vec2(value.x, value.y);
		}

		glm::vec4 ToRuntimeVec4(const Vec4& value)
		{
			return glm::vec4(value.x, value.y, value.z, value.w);
		}

		AssetType ToEditorAssetType(Vans::VansAssetType type)
		{
			switch (type)
			{
			case Vans::VansAssetType::Model: return AssetType::Model;
			case Vans::VansAssetType::Texture: return AssetType::Texture;
			case Vans::VansAssetType::Material: return AssetType::Material;
			case Vans::VansAssetType::Shader: return AssetType::Shader;
			case Vans::VansAssetType::Audio: return AssetType::Audio;
			case Vans::VansAssetType::Video: return AssetType::Video;
			case Vans::VansAssetType::Scene: return AssetType::Scene;
			case Vans::VansAssetType::Particle: return AssetType::Particle;
			case Vans::VansAssetType::AnimationClip: return AssetType::AnimationClip;
			case Vans::VansAssetType::AnimatorController: return AssetType::AnimatorController;
			case Vans::VansAssetType::ClothProfile: return AssetType::ClothProfile;
			case Vans::VansAssetType::PostProcessProfile: return AssetType::PostProcessProfile;
			case Vans::VansAssetType::RagdollProfile: return AssetType::RagdollProfile;
			case Vans::VansAssetType::AudioReverbPreset: return AssetType::AudioReverbPreset;
			case Vans::VansAssetType::AudioBusSnapshot: return AssetType::AudioBusSnapshot;
			case Vans::VansAssetType::AudioDuckingRules: return AssetType::AudioDuckingRules;
			default: return AssetType::Unknown;
			}
		}

		using RuntimeLightPatch = RuntimeLightEdit;
		using RuntimeLightPatchType = RuntimePreviewLightType;

		struct RuntimeLightBinding
		{
			VansGraphics::VansLightManager* manager = nullptr;
			int index = -1;
		};

		std::uint16_t RuntimeLightComponentTypeForPatch(RuntimeLightPatchType type)
		{
			switch (type)
			{
			case RuntimeLightPatchType::Directional:
				return Vans::VansRuntimeComponentType_DirectionalLight;
			case RuntimeLightPatchType::Point:
				return Vans::VansRuntimeComponentType_PointLight;
			case RuntimeLightPatchType::Spot:
				return Vans::VansRuntimeComponentType_SpotLight;
			case RuntimeLightPatchType::Rect:
				return Vans::VansRuntimeComponentType_RectLight;
			}
			return Vans::VansInvalidComponentTypeId;
		}

		template <typename T>
		const T* GetRuntimeComponentPayload(
			const Vans::VansRuntimeWorld& runtimeWorld,
			Vans::VansComponentHandle component,
			std::uint16_t expectedType)
		{
			if (component.typeId != expectedType)
				return nullptr;
			const auto* storage = static_cast<const Vans::VansComponentStorage<T>*>(
				runtimeWorld.FindStorage(expectedType));
			return storage ? storage->Get(component) : nullptr;
		}

		std::uint32_t ResolveRuntimeEntityTransformId(
			const Vans::VansRuntimeWorld& runtimeWorld,
			Vans::VansEntityHandle entity)
		{
			for (Vans::VansComponentHandle component : runtimeWorld.CollectComponentsOwnedBy(entity))
			{
				if (const auto* transform = GetRuntimeComponentPayload<Vans::VansRuntimeTransformComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Transform))
				{
					return transform->transformStoreId;
				}
			}
			return UINT32_MAX;
		}

		std::uint32_t ResolveRuntimeTransformId(
			VansGraphics::VansScene* scene,
			const std::string& entityGuid)
		{
			if (!scene || entityGuid.empty())
				return UINT32_MAX;

			if (const auto* runtimeWorld = scene->GetRuntimeWorld())
			{
				const Vans::VansEntityHandle entity = runtimeWorld->Entities().FindByGuid(entityGuid);
				const std::uint32_t transformId = ResolveRuntimeEntityTransformId(*runtimeWorld, entity);
				if (transformId != UINT32_MAX)
					return transformId;
			}
			return UINT32_MAX;
		}

		bool ReadRuntimeTransformById(
			std::uint32_t transformId,
			const std::string& entityGuid,
			RuntimeTransformSnapshot& snapshot)
		{
			if (transformId >= VansGraphics::VansTransformStore::GlobalTransforms.size())
				return false;

			const auto& transform = VansGraphics::VansTransformStore::GetTransform(transformId);
			snapshot.available = true;
			snapshot.entityGuid = entityGuid;
			snapshot.position = ToEditorVec3(transform.m_Position);
			snapshot.rotationDegrees = ToEditorVec3(transform.m_Rotation);
			snapshot.scale = ToEditorVec3(transform.m_Scale);
			return true;
		}

		bool ApplyRuntimeTransformById(
			std::uint32_t transformId,
			const RuntimeTransformEdit& edit)
		{
			if (transformId >= VansGraphics::VansTransformStore::GlobalTransforms.size())
				return false;

			VansGraphics::VansTransform& transform = VansGraphics::VansTransformStore::GetTransform(transformId);
			if (edit.writePosition)
				transform.m_Position = ToRuntimeVec3(edit.position);
			if (edit.writeRotation)
				transform.m_Rotation = ToRuntimeVec3(edit.rotationDegrees);
			if (edit.writeScale)
				transform.m_Scale = ToRuntimeVec3(edit.scale);

			VansGraphics::VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			return true;
		}

		RuntimeLightBinding ResolveRuntimeLightBinding(
			VansGraphics::VansScene* scene,
			const RuntimeLightPatch& patch)
		{
			RuntimeLightBinding binding;
			if (!scene || patch.entityGuid.empty())
				return binding;

			const Vans::VansRuntimeWorld* runtimeWorld = scene->GetRuntimeWorld();
			if (!runtimeWorld)
				return binding;

			const Vans::VansEntityHandle entity =
				runtimeWorld->Entities().FindByGuid(patch.entityGuid);
			if (!entity.IsValid())
				return binding;

			const std::uint16_t runtimeType = RuntimeLightComponentTypeForPatch(patch.type);
			for (Vans::VansComponentHandle component : runtimeWorld->CollectComponentsOwnedBy(entity))
			{
				if (const auto* runtimeLight = GetRuntimeComponentPayload<Vans::VansRuntimeLightComponent>(
					*runtimeWorld,
					component,
					runtimeType))
				{
					binding = { runtimeLight->lightManager, runtimeLight->lightIndex };
					break;
				}
			}

			return binding;
		}

		template <typename LightT>
		bool ReadLightAt(std::vector<LightT>& lights, int index, LightT& out)
		{
			if (index < 0 || index >= static_cast<int>(lights.size()))
				return false;
			out = lights[static_cast<std::size_t>(index)];
			return true;
		}

		template <typename LightT>
		bool WriteLightAt(std::vector<LightT>& lights, int index, const LightT& value)
		{
			if (index < 0 || index >= static_cast<int>(lights.size()))
				return false;
			lights[static_cast<std::size_t>(index)] = value;
			return true;
		}

		bool CaptureRuntimeLight(
			const RuntimeLightPatch& patch,
			const RuntimeLightBinding& binding,
			VansGraphics::VansDirectionalLight& directional,
			VansGraphics::VansPointLight& point,
			VansGraphics::VansSpotLight& spot,
			VansGraphics::VansRectLight& rect)
		{
			if (!binding.manager)
				return false;

			switch (patch.type)
			{
			case RuntimeLightPatchType::Directional:
				return ReadLightAt(binding.manager->GetDirectionLights(), binding.index, directional);
			case RuntimeLightPatchType::Point:
				return ReadLightAt(binding.manager->GetPointLights(), binding.index, point);
			case RuntimeLightPatchType::Spot:
				return ReadLightAt(binding.manager->GetSpotLight(), binding.index, spot);
			case RuntimeLightPatchType::Rect:
				return ReadLightAt(binding.manager->GetRectLights(), binding.index, rect);
			}
			return false;
		}

		bool RestoreRuntimeLight(
			const RuntimeLightPatch& patch,
			const RuntimeLightBinding& binding,
			const VansGraphics::VansDirectionalLight& directional,
			const VansGraphics::VansPointLight& point,
			const VansGraphics::VansSpotLight& spot,
			const VansGraphics::VansRectLight& rect)
		{
			if (!binding.manager)
				return false;

			switch (patch.type)
			{
			case RuntimeLightPatchType::Directional:
				return WriteLightAt(binding.manager->GetDirectionLights(), binding.index, directional);
			case RuntimeLightPatchType::Point:
				return WriteLightAt(binding.manager->GetPointLights(), binding.index, point);
			case RuntimeLightPatchType::Spot:
				return WriteLightAt(binding.manager->GetSpotLight(), binding.index, spot);
			case RuntimeLightPatchType::Rect:
				return WriteLightAt(binding.manager->GetRectLights(), binding.index, rect);
			}
			return false;
		}

		bool ApplyRuntimeLightPatch(
			const RuntimeLightPatch& patch,
			const RuntimeLightBinding& binding)
		{
			if (!binding.manager)
				return false;

			switch (patch.type)
			{
			case RuntimeLightPatchType::Directional:
			{
				auto& lights = binding.manager->GetDirectionLights();
				if (binding.index < 0 || binding.index >= static_cast<int>(lights.size()))
					return false;
				auto& light = lights[static_cast<std::size_t>(binding.index)];
				if (patch.writeColor)
					light.m_Color = ToRuntimeVec3(patch.color);
				if (patch.writeIntensity)
					light.m_Intensity = patch.intensity;
				return true;
			}
			case RuntimeLightPatchType::Point:
			{
				auto& lights = binding.manager->GetPointLights();
				if (binding.index < 0 || binding.index >= static_cast<int>(lights.size()))
					return false;
				auto& light = lights[static_cast<std::size_t>(binding.index)];
				if (patch.writeColor)
					light.m_Color = ToRuntimeVec3(patch.color);
				if (patch.writeIntensity)
					light.m_Intensity = patch.intensity;
				if (patch.writeRadius)
					light.m_Radius = patch.radius;
				return true;
			}
			case RuntimeLightPatchType::Spot:
			{
				auto& lights = binding.manager->GetSpotLight();
				if (binding.index < 0 || binding.index >= static_cast<int>(lights.size()))
					return false;
				auto& light = lights[static_cast<std::size_t>(binding.index)];
				if (patch.writeColor)
					light.m_Color = ToRuntimeVec3(patch.color);
				if (patch.writeIntensity)
					light.m_Intensity = patch.intensity;
				if (patch.writeRadius)
					light.m_Radius = patch.radius;
				if (patch.writeInnerCutoff)
					light.m_InnerCutOff = patch.innerCutoffRadians;
				if (patch.writeOuterCutoff)
					light.m_OuterCutOff = patch.outerCutoffRadians;
				return true;
			}
			case RuntimeLightPatchType::Rect:
			{
				auto& lights = binding.manager->GetRectLights();
				if (binding.index < 0 || binding.index >= static_cast<int>(lights.size()))
					return false;
				auto& light = lights[static_cast<std::size_t>(binding.index)];
				if (patch.writeColor)
					light.m_Color = ToRuntimeVec3(patch.color);
				if (patch.writeIntensity)
					light.m_Intensity = patch.intensity;
				if (patch.writeRectWidth)
					light.m_HalfWidth = patch.rectWidth * 0.5f;
				if (patch.writeRectHeight)
					light.m_HalfHeight = patch.rectHeight * 0.5f;
				if (patch.writeRectRange)
					light.m_Range = patch.rectRange;
				if (patch.writeRectTwoSided)
					light.m_TwoSided = patch.rectTwoSided;
				if (patch.writeRectShadow)
				{
					auto& registrations = binding.manager->GetRectShadowRegistrations();
					if (static_cast<std::size_t>(binding.index) < registrations.size())
						registrations[static_cast<std::size_t>(binding.index)].settings.castShadows = patch.rectShadowIndex >= 0.0f;
				}
				return true;
			}
			}
			return false;
		}

		void CommitRuntimeLighting(VansGraphics::VansScene* scene)
		{
			if (!scene)
				return;

			VansGraphics::VansCamera* camera = scene->GetCamera();
			VansGraphics::VansLightManager* lightManager = scene->GetLightManager();
			if (!camera || !lightManager)
				return;

			lightManager->SyncLightGPUData(glm::vec3(camera->GetPosition()));
		}

		LightingSettingsSnapshot CaptureLightingSettings(VansGraphics::VansLightManager* lightManager)
		{
			LightingSettingsSnapshot snapshot;
			if (!lightManager)
				return snapshot;

			const auto& directionalLights = lightManager->GetDirectionLights();
			snapshot.directionalLights.reserve(directionalLights.size());
			for (const auto& source : directionalLights)
			{
				DirectionalLightSettings light;
				light.direction = ToEditorVec3(source.m_Direction);
				light.color = ToEditorVec3(source.m_Color);
				light.intensity = source.m_Intensity;
				snapshot.directionalLights.push_back(light);
			}

			auto captureShadow = [](const VansGraphics::VansPunctualShadowSettings& source,
				PointLightSettings::PunctualShadowSettings& target)
			{
				target.castShadows = source.castShadows;
				target.policy = static_cast<int>(source.policy);
				target.priority = source.priority;
				target.resolution = static_cast<int>(source.resolution);
				target.updateMode = static_cast<int>(source.updateMode);
				target.fallback = static_cast<int>(source.fallback);
				target.maxDistance = source.maxShadowDistance;
				target.nearPlane = source.nearPlaneOverride;
				target.depthBiasTexels = source.depthBiasTexels;
				target.normalBiasTexels = source.normalBiasTexels;
				target.sourceRadius = source.sourceRadius;
				target.affectsFog = source.affectsVolumetricFog;
				target.affectsGI = source.affectsGI;
				target.shadowCasterMask = source.shadowCasterMask;
			};

			const auto& pointLights = lightManager->GetPointLights();
			const auto& pointRegistrations = lightManager->GetPointShadowRegistrations();
			snapshot.pointLights.reserve(pointLights.size());
			for (std::size_t index = 0; index < pointLights.size(); ++index)
			{
				const auto& source = pointLights[index];
				PointLightSettings light;
				light.position = ToEditorVec3(source.m_Position);
				light.color = ToEditorVec3(source.m_Color);
				light.intensity = source.m_Intensity;
				light.radius = source.m_Radius;
				if (index < pointRegistrations.size())
					captureShadow(pointRegistrations[index].settings, light.shadow);
				snapshot.pointLights.push_back(light);
			}

			const auto& spotLights = lightManager->GetSpotLight();
			const auto& spotRegistrations = lightManager->GetSpotShadowRegistrations();
			snapshot.spotLights.reserve(spotLights.size());
			for (std::size_t index = 0; index < spotLights.size(); ++index)
			{
				const auto& source = spotLights[index];
				SpotLightSettings light;
				light.position = ToEditorVec3(source.m_Position);
				light.direction = ToEditorVec3(source.m_Direction);
				light.color = ToEditorVec3(source.m_Color);
				light.intensity = source.m_Intensity;
				light.radius = source.m_Radius;
				light.innerCutoffRadians = source.m_InnerCutOff;
				light.outerCutoffRadians = source.m_OuterCutOff;
				if (index < spotRegistrations.size())
					captureShadow(spotRegistrations[index].settings, light.shadow);
				snapshot.spotLights.push_back(light);
			}

			const auto& rectLights = lightManager->GetRectLights();
			const auto& rectRegistrations = lightManager->GetRectShadowRegistrations();
			snapshot.rectLights.reserve(rectLights.size());
			for (std::size_t index = 0; index < rectLights.size(); ++index)
			{
				const auto& source = rectLights[index];
				RectLightSettings light;
				light.position = ToEditorVec3(source.m_Position);
				light.normal = ToEditorVec3(source.m_Normal);
				light.color = ToEditorVec3(source.m_Color);
				light.intensity = source.m_Intensity;
				light.width = source.m_HalfWidth * 2.0f;
				light.height = source.m_HalfHeight * 2.0f;
				light.range = source.m_Range;
				light.twoSided = source.m_TwoSided > 0.5f;
				if (index < rectRegistrations.size())
					captureShadow(rectRegistrations[index].settings, light.shadow);
				snapshot.rectLights.push_back(light);
			}

			return snapshot;
		}

		void ApplyLightingSettingsToManager(
			VansGraphics::VansLightManager* lightManager,
			const LightingSettingsSnapshot& settings)
		{
			if (!lightManager)
				return;

			auto& directionalLights = lightManager->GetDirectionLights();
			const std::size_t directionalCount = std::min(directionalLights.size(), settings.directionalLights.size());
			for (std::size_t i = 0; i < directionalCount; ++i)
			{
				const DirectionalLightSettings& source = settings.directionalLights[i];
				directionalLights[i].m_Direction = ToRuntimeVec3(source.direction);
				directionalLights[i].m_Color = ToRuntimeVec3(source.color);
				directionalLights[i].m_Intensity = source.intensity;
			}

			auto applyShadow = [](const PointLightSettings::PunctualShadowSettings& source,
				VansGraphics::VansPunctualShadowSettings& target)
			{
				target.castShadows = source.castShadows;
				target.policy = static_cast<VansGraphics::VansShadowPolicy>(std::clamp(source.policy, 0, 3));
				target.priority = static_cast<uint8_t>(std::clamp(source.priority, 0, 255));
				switch (source.resolution)
				{
				case 128: target.resolution = VansGraphics::VansShadowResolution::R128; break;
				case 256: target.resolution = VansGraphics::VansShadowResolution::R256; break;
				case 512: target.resolution = VansGraphics::VansShadowResolution::R512; break;
				case 1024: target.resolution = VansGraphics::VansShadowResolution::R1024; break;
				default: target.resolution = VansGraphics::VansShadowResolution::Auto; break;
				}
				target.updateMode = static_cast<VansGraphics::VansShadowUpdateMode>(std::clamp(source.updateMode, 0, 2));
				target.fallback = static_cast<VansGraphics::VansShadowFallback>(std::clamp(source.fallback, 0, 1));
				target.maxShadowDistance = std::max(source.maxDistance, 0.01f);
				target.nearPlaneOverride = std::max(source.nearPlane, 0.0f);
				target.depthBiasTexels = std::max(source.depthBiasTexels, 0.0f);
				target.normalBiasTexels = std::max(source.normalBiasTexels, 0.0f);
				target.sourceRadius = std::max(source.sourceRadius, 0.0f);
				target.affectsVolumetricFog = source.affectsFog;
				target.affectsGI = source.affectsGI;
				target.shadowCasterMask = source.shadowCasterMask;
			};

			auto& pointLights = lightManager->GetPointLights();
			auto& pointRegistrations = lightManager->GetPointShadowRegistrations();
			const std::size_t pointCount = std::min(pointLights.size(), settings.pointLights.size());
			for (std::size_t i = 0; i < pointCount; ++i)
			{
				const PointLightSettings& source = settings.pointLights[i];
				pointLights[i].m_Position = ToRuntimeVec3(source.position);
				pointLights[i].m_Color = ToRuntimeVec3(source.color);
				pointLights[i].m_Intensity = source.intensity;
				pointLights[i].m_Radius = source.radius;
				if (i < pointRegistrations.size())
					applyShadow(source.shadow, pointRegistrations[i].settings);
			}

			auto& spotLights = lightManager->GetSpotLight();
			auto& spotRegistrations = lightManager->GetSpotShadowRegistrations();
			const std::size_t spotCount = std::min(spotLights.size(), settings.spotLights.size());
			for (std::size_t i = 0; i < spotCount; ++i)
			{
				const SpotLightSettings& source = settings.spotLights[i];
				spotLights[i].m_Position = ToRuntimeVec3(source.position);
				spotLights[i].m_Direction = ToRuntimeVec3(source.direction);
				spotLights[i].m_Color = ToRuntimeVec3(source.color);
				spotLights[i].m_Intensity = source.intensity;
				spotLights[i].m_Radius = source.radius;
				spotLights[i].m_InnerCutOff = source.innerCutoffRadians;
				spotLights[i].m_OuterCutOff = source.outerCutoffRadians;
				if (i < spotRegistrations.size())
					applyShadow(source.shadow, spotRegistrations[i].settings);
			}

			auto& rectLights = lightManager->GetRectLights();
			auto& rectRegistrations = lightManager->GetRectShadowRegistrations();
			const std::size_t rectCount = std::min(rectLights.size(), settings.rectLights.size());
			for (std::size_t i = 0; i < rectCount; ++i)
			{
				const RectLightSettings& source = settings.rectLights[i];
				rectLights[i].m_Position = ToRuntimeVec3(source.position);
				rectLights[i].m_Normal = ToRuntimeVec3(source.normal);
				rectLights[i].m_Color = ToRuntimeVec3(source.color);
				rectLights[i].m_Intensity = source.intensity;
				rectLights[i].m_HalfWidth = std::max(source.width * 0.5f, 0.001f);
				rectLights[i].m_HalfHeight = std::max(source.height * 0.5f, 0.001f);
				rectLights[i].m_Range = std::max(source.range, 0.01f);
				rectLights[i].m_TwoSided = source.twoSided ? 1.0f : 0.0f;
				if (i < rectRegistrations.size())
					applyShadow(source.shadow, rectRegistrations[i].settings);
			}
		}

		RuntimeTransformEdit MakeFullTransformEdit(const RuntimeTransformSnapshot& snapshot)
		{
			RuntimeTransformEdit edit;
			edit.entityGuid = snapshot.entityGuid;
			edit.position = snapshot.position;
			edit.rotationDegrees = snapshot.rotationDegrees;
			edit.scale = snapshot.scale;
			edit.writePosition = true;
			edit.writeRotation = true;
			edit.writeScale = true;
			return edit;
		}

		class SetRuntimeTransformCommand final : public IEngineCommand
		{
		public:
			explicit SetRuntimeTransformCommand(RuntimeTransformEdit edit)
				: m_Edit(std::move(edit))
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				const std::uint32_t transformId = ResolveRuntimeTransformId(scene, m_Edit.entityGuid);
				if (transformId == UINT32_MAX)
					return;

				if (!m_HasBefore)
				{
					m_HasBefore = ReadRuntimeTransformById(transformId, m_Edit.entityGuid, m_Before);
					if (!m_HasBefore)
						return;
				}

				ApplyRuntimeTransformById(transformId, m_Edit);
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				const std::uint32_t transformId = ResolveRuntimeTransformId(scene, m_Before.entityGuid);
				ApplyRuntimeTransformById(transformId, MakeFullTransformEdit(m_Before));
			}

			std::string GetDescription() const override
			{
				return "Set runtime transform";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				const auto* next = dynamic_cast<const SetRuntimeTransformCommand*>(&other);
				return next && next->m_Edit.entityGuid == m_Edit.entityGuid;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetRuntimeTransformCommand*>(&other);
				if (!next || next->m_Edit.entityGuid != m_Edit.entityGuid)
					return false;

				m_Edit = next->m_Edit;
				Execute(context);
				return true;
			}

		private:
			RuntimeTransformEdit m_Edit;
			RuntimeTransformSnapshot m_Before;
			bool m_HasBefore = false;
		};

		class SetRuntimeLightPropertiesCommand final : public IEngineCommand
		{
		public:
			explicit SetRuntimeLightPropertiesCommand(RuntimeLightPatch patch)
				: m_Patch(std::move(patch))
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				const RuntimeLightBinding binding = ResolveRuntimeLightBinding(scene, m_Patch);
				if (!binding.manager)
					return;

				if (!m_HasBefore)
				{
					m_HasBefore = CaptureRuntimeLight(
						m_Patch,
						binding,
						m_BeforeDirectional,
						m_BeforePoint,
						m_BeforeSpot,
						m_BeforeRect);
					if (!m_HasBefore)
						return;
				}

				ApplyRuntimeLightPatch(m_Patch, binding);
				CommitRuntimeLighting(scene);
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				const RuntimeLightBinding binding = ResolveRuntimeLightBinding(scene, m_Patch);
				RestoreRuntimeLight(
					m_Patch,
					binding,
					m_BeforeDirectional,
					m_BeforePoint,
					m_BeforeSpot,
					m_BeforeRect);
				CommitRuntimeLighting(scene);
			}

			std::string GetDescription() const override
			{
				return "Set runtime light properties";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				const auto* next = dynamic_cast<const SetRuntimeLightPropertiesCommand*>(&other);
				return next
					&& next->m_Patch.entityGuid == m_Patch.entityGuid
					&& next->m_Patch.type == m_Patch.type;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetRuntimeLightPropertiesCommand*>(&other);
				if (!next || next->m_Patch.entityGuid != m_Patch.entityGuid || next->m_Patch.type != m_Patch.type)
					return false;

				m_Patch = next->m_Patch;
				Execute(context);
				return true;
			}

		private:
			RuntimeLightPatch m_Patch;
			VansGraphics::VansDirectionalLight m_BeforeDirectional{};
			VansGraphics::VansPointLight m_BeforePoint{};
			VansGraphics::VansSpotLight m_BeforeSpot{};
			VansGraphics::VansRectLight m_BeforeRect{};
			bool m_HasBefore = false;
		};

		class SetLightingSettingsCommand final : public IEngineCommand
		{
		public:
			explicit SetLightingSettingsCommand(LightingSettingsSnapshot settings)
				: m_Settings(std::move(settings))
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* lightManager = scene ? scene->GetLightManager() : nullptr;
				if (!lightManager)
					return;

				if (!m_HasBefore)
				{
					m_Before = CaptureLightingSettings(lightManager);
					m_HasBefore = true;
				}

				ApplyLightingSettingsToManager(lightManager, m_Settings);
				CommitRuntimeLighting(scene);
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* lightManager = scene ? scene->GetLightManager() : nullptr;
				if (!lightManager)
					return;

				ApplyLightingSettingsToManager(lightManager, m_Before);
				CommitRuntimeLighting(scene);
			}

			std::string GetDescription() const override
			{
				return "Set lighting settings";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				return dynamic_cast<const SetLightingSettingsCommand*>(&other) != nullptr;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetLightingSettingsCommand*>(&other);
				if (!next)
					return false;

				m_Settings = next->m_Settings;
				Execute(context);
				return true;
			}

		private:
			LightingSettingsSnapshot m_Settings;
			LightingSettingsSnapshot m_Before;
			bool m_HasBefore = false;
		};

		float RaySphereIntersect(const glm::vec3& rayOrigin,
			const glm::vec3& rayDirection,
			const glm::vec3& center,
			float radius)
		{
			const glm::vec3 oc = rayOrigin - center;
			const float b = glm::dot(oc, rayDirection);
			const float c = glm::dot(oc, oc) - radius * radius;
			const float disc = b * b - c;
			if (disc < 0.0f)
				return -1.0f;
			const float sqrtDisc = glm::sqrt(disc);
			const float t0 = -b - sqrtDisc;
			const float t1 = -b + sqrtDisc;
			if (t0 > 0.0f)
				return t0;
			if (t1 > 0.0f)
				return t1;
			return -1.0f;
		}

		physx::PxVec3 VehicleAxisToPx(physx::vehicle2::PxVehicleAxes::Enum axis)
		{
			using physx::PxVec3;
			using physx::vehicle2::PxVehicleAxes;
			switch (axis)
			{
			case PxVehicleAxes::ePosX: return PxVec3(1.0f, 0.0f, 0.0f);
			case PxVehicleAxes::eNegX: return PxVec3(-1.0f, 0.0f, 0.0f);
			case PxVehicleAxes::ePosY: return PxVec3(0.0f, 1.0f, 0.0f);
			case PxVehicleAxes::eNegY: return PxVec3(0.0f, -1.0f, 0.0f);
			case PxVehicleAxes::ePosZ: return PxVec3(0.0f, 0.0f, 1.0f);
			case PxVehicleAxes::eNegZ: return PxVec3(0.0f, 0.0f, -1.0f);
			default: return PxVec3(0.0f, 1.0f, 0.0f);
			}
		}

		FootIKDebugSampleSnapshot ToFootIKDebugSample(const VansGraphics::FootPlacementDebugSample& source)
		{
			FootIKDebugSampleSnapshot sample;
			sample.rayStart = ToEditorVec3(source.rayStart);
			sample.rayEnd = ToEditorVec3(source.rayEnd);
			sample.hitPosition = ToEditorVec3(source.hitPosition);
			sample.hitNormal = ToEditorVec3(source.hitNormal);
			sample.hasHit = source.hasHit;
			sample.accepted = source.accepted;
			sample.hitLayer = source.hitLayer;
			sample.hitActorName = source.hitActorName;
			sample.status = source.status;
			return sample;
		}

		FootIKDebugLegSnapshot ToFootIKDebugLeg(const VansGraphics::FootPlacementDebugLeg& source)
		{
			FootIKDebugLegSnapshot leg;
			leg.hip = ToEditorVec3(source.hip);
			leg.knee = ToEditorVec3(source.knee);
			leg.animatedFoot = ToEditorVec3(source.animatedFoot);
			leg.solvedFoot = ToEditorVec3(source.solvedFoot);
			leg.target = ToEditorVec3(source.target);
			leg.contact = ToEditorVec3(source.contact);
			leg.normal = ToEditorVec3(source.normal);
			leg.hasContact = source.hasContact;
			leg.hasTarget = source.hasTarget;
			leg.targetWeight = source.targetWeight;
			leg.verticalOffset = source.verticalOffset;
			leg.samples.reserve(source.samples.size());
			for (const auto& sample : source.samples)
				leg.samples.push_back(ToFootIKDebugSample(sample));
			return leg;
		}

		WaterSettingsSnapshot ToWaterSettings(const VansGraphics::VansWaterConfig& source)
		{
			WaterSettingsSnapshot settings;
			settings.available = true;
			settings.waterLevel = source.m_WaterLevel;
			settings.specularIntensity = source.m_SpecularIntensity;

			settings.medium.absorptionCoeff = ToEditorVec3(source.m_Medium.m_AbsorptionCoeff);
			settings.medium.scatteringCoeff = ToEditorVec3(source.m_Medium.m_ScatteringCoeff);
			settings.medium.ior = source.m_Medium.m_IOR;
			settings.medium.anisotropy = source.m_Medium.m_Anisotropy;
			settings.medium.waterRoughness = source.m_Medium.m_WaterRoughness;

			settings.geometry.lodCount = source.m_Geometry.m_LodCount;
			settings.geometry.basePatchSize = source.m_Geometry.m_BasePatchSize;
			settings.geometry.morphStartRatio = source.m_Geometry.m_MorphStartRatio;

			settings.spectrum.mode = static_cast<int>(source.m_Spectrum.m_Mode);
			settings.spectrum.cascadeCount = source.m_Spectrum.m_CascadeCount;
			settings.spectrum.baseCoverage = source.m_Spectrum.m_BaseCoverage;
			settings.spectrum.cascadeScale = source.m_Spectrum.m_CascadeScale;
			settings.spectrum.windDirection = ToEditorVec2(source.m_Spectrum.m_WindDirection);
			settings.spectrum.windSpeed = source.m_Spectrum.m_WindSpeed;
			settings.spectrum.swellAmplitude = source.m_Spectrum.m_SwellAmplitude;
			settings.spectrum.choppiness = source.m_Spectrum.m_Choppiness;
			settings.spectrum.gerstnerWaveCount = source.m_Spectrum.m_GerstnerWaveCount;
			settings.spectrum.spectrumAmplitude = source.m_Spectrum.m_SpectrumAmplitude;
			settings.spectrum.minWavelength = source.m_Spectrum.m_MinWavelength;
			settings.spectrum.smallWaveDamping = source.m_Spectrum.m_SmallWaveDamping;
			settings.spectrum.windDependency = source.m_Spectrum.m_WindDependency;
			settings.spectrum.depth = source.m_Spectrum.m_Depth;
			settings.spectrum.repeatPeriod = source.m_Spectrum.m_RepeatPeriod;
			settings.spectrum.randomSeed = source.m_Spectrum.m_RandomSeed;

			settings.waveParticle.particleCount = source.m_WaveParticle.m_ParticleCount;
			settings.waveParticle.octaveCount = source.m_WaveParticle.m_OctaveCount;
			settings.waveParticle.profile = source.m_WaveParticle.m_Profile;
			settings.waveParticle.domainSize = source.m_WaveParticle.m_DomainSize;
			settings.waveParticle.amplitude = source.m_WaveParticle.m_Amplitude;
			settings.waveParticle.minRadius = source.m_WaveParticle.m_MinRadius;
			settings.waveParticle.maxRadius = source.m_WaveParticle.m_MaxRadius;
			settings.waveParticle.phaseVelocity = source.m_WaveParticle.m_PhaseVelocity;
			settings.waveParticle.damping = source.m_WaveParticle.m_Damping;
			settings.waveParticle.directionSpread = source.m_WaveParticle.m_DirectionSpread;
			settings.waveParticle.lacunarity = source.m_WaveParticle.m_Lacunarity;
			settings.waveParticle.persistence = source.m_WaveParticle.m_Persistence;
			settings.waveParticle.radiusFalloff = source.m_WaveParticle.m_RadiusFalloff;
			settings.waveParticle.profileSharpness = source.m_WaveParticle.m_ProfileSharpness;
			settings.waveParticle.foamThreshold = source.m_WaveParticle.m_FoamThreshold;
			settings.waveParticle.foamSoftness = source.m_WaveParticle.m_FoamSoftness;
			settings.waveParticle.lifetime = source.m_WaveParticle.m_Lifetime;
			settings.waveParticle.randomSeed = source.m_WaveParticle.m_RandomSeed;

			settings.flowMap.enabled = source.m_FlowMap.m_Enabled;
			settings.flowMap.strength = source.m_FlowMap.m_Strength;
			settings.flowMap.speed = source.m_FlowMap.m_Speed;
			settings.flowMap.phaseLength = source.m_FlowMap.m_PhaseLength;
			settings.flowMap.noiseAmount = source.m_FlowMap.m_NoiseAmount;
			settings.flowMap.worldOrigin = ToEditorVec2(source.m_FlowMap.m_WorldOrigin);
			settings.flowMap.worldSize = ToEditorVec2(source.m_FlowMap.m_WorldSize);
			settings.flowMap.fallbackDirection = ToEditorVec2(source.m_FlowMap.m_FallbackDirection);

			settings.optics.maxCrossDistance = source.m_Optics.m_MaxCrossDistance;
			settings.optics.maxRefractionCrossDistance = source.m_Optics.m_MaxRefractionCrossDistance;
			settings.optics.multiScatterScale = source.m_Optics.m_MultiScatterScale;
			settings.optics.waterDispersionStrength = source.m_Optics.m_WaterDispersionStrength;
			settings.optics.sssPathScale = source.m_Optics.m_SSSPathScale;
			settings.optics.sssNonlinearStrength = source.m_Optics.m_SSSNonlinearStrength;
			settings.optics.sssScatterBoost = source.m_Optics.m_SSSScatterBoost;
			settings.optics.backlitPathScale = source.m_Optics.m_BacklitPathScale;
			settings.optics.backlitPhaseG = source.m_Optics.m_BacklitPhaseG;

			settings.volume.resolutionScale = source.m_Volume.m_ResolutionScale;
			settings.volume.sampleCount = source.m_Volume.m_SampleCount;
			settings.volume.spatialFilterIterations = source.m_Volume.m_SpatialFilterIterations;
			settings.volume.spatialDepthSensitivity = source.m_Volume.m_SpatialDepthSensitivity;

			settings.thinSSSEnabled = source.m_SSS.m_Enabled;
			settings.maxThicknessDistance = source.m_SSS.m_MaxThicknessDistance;
			settings.deepWaterThicknessFallback = source.m_SSS.m_DeepWaterThicknessFallback;
			settings.causticsEnabled = source.m_Caustics.m_Enabled;
			settings.causticsIntensity = source.m_Caustics.m_Intensity;
			settings.causticsScale = source.m_Caustics.m_Scale;
			settings.refractionEnabled = source.m_Refraction.m_Enabled;
			settings.refractionDistortionStrength = source.m_Refraction.m_DistortionStrength;
			settings.ssrEnabled = source.m_SSR.m_Enabled;
			settings.ssrMaxDistance = source.m_SSR.m_MaxDistance;
			settings.ssrMaxRoughness = source.m_SSR.m_MaxRoughness;
			return settings;
		}

		void ApplyWaterSettingsToConfig(const WaterSettingsSnapshot& settings, VansGraphics::VansWaterConfig& destination)
		{
			destination.m_WaterLevel = settings.waterLevel;
			destination.m_SpecularIntensity = settings.specularIntensity;

			destination.m_Medium.m_AbsorptionCoeff = ToRuntimeVec3(settings.medium.absorptionCoeff);
			destination.m_Medium.m_ScatteringCoeff = ToRuntimeVec3(settings.medium.scatteringCoeff);
			destination.m_Medium.m_IOR = settings.medium.ior;
			destination.m_Medium.m_Anisotropy = settings.medium.anisotropy;
			destination.m_Medium.m_WaterRoughness = settings.medium.waterRoughness;

			destination.m_Geometry.m_LodCount = settings.geometry.lodCount;
			destination.m_Geometry.m_BasePatchSize = settings.geometry.basePatchSize;
			destination.m_Geometry.m_MorphStartRatio = settings.geometry.morphStartRatio;

			destination.m_Spectrum.m_Mode = static_cast<VansGraphics::VansWaveMode>(std::clamp(settings.spectrum.mode, 0, 2));
			destination.m_Spectrum.m_CascadeCount = settings.spectrum.cascadeCount;
			destination.m_Spectrum.m_BaseCoverage = settings.spectrum.baseCoverage;
			destination.m_Spectrum.m_CascadeScale = settings.spectrum.cascadeScale;
			destination.m_Spectrum.m_WindDirection = ToRuntimeVec2(settings.spectrum.windDirection);
			destination.m_Spectrum.m_WindSpeed = settings.spectrum.windSpeed;
			destination.m_Spectrum.m_SwellAmplitude = settings.spectrum.swellAmplitude;
			destination.m_Spectrum.m_Choppiness = settings.spectrum.choppiness;
			destination.m_Spectrum.m_GerstnerWaveCount = settings.spectrum.gerstnerWaveCount;
			destination.m_Spectrum.m_SpectrumAmplitude = settings.spectrum.spectrumAmplitude;
			destination.m_Spectrum.m_MinWavelength = settings.spectrum.minWavelength;
			destination.m_Spectrum.m_SmallWaveDamping = settings.spectrum.smallWaveDamping;
			destination.m_Spectrum.m_WindDependency = settings.spectrum.windDependency;
			destination.m_Spectrum.m_Depth = settings.spectrum.depth;
			destination.m_Spectrum.m_RepeatPeriod = settings.spectrum.repeatPeriod;
			destination.m_Spectrum.m_RandomSeed = settings.spectrum.randomSeed;

			destination.m_WaveParticle.m_ParticleCount = settings.waveParticle.particleCount;
			destination.m_WaveParticle.m_OctaveCount = settings.waveParticle.octaveCount;
			destination.m_WaveParticle.m_Profile = settings.waveParticle.profile;
			destination.m_WaveParticle.m_DomainSize = settings.waveParticle.domainSize;
			destination.m_WaveParticle.m_Amplitude = settings.waveParticle.amplitude;
			destination.m_WaveParticle.m_MinRadius = settings.waveParticle.minRadius;
			destination.m_WaveParticle.m_MaxRadius = settings.waveParticle.maxRadius;
			destination.m_WaveParticle.m_PhaseVelocity = settings.waveParticle.phaseVelocity;
			destination.m_WaveParticle.m_Damping = settings.waveParticle.damping;
			destination.m_WaveParticle.m_DirectionSpread = settings.waveParticle.directionSpread;
			destination.m_WaveParticle.m_Lacunarity = settings.waveParticle.lacunarity;
			destination.m_WaveParticle.m_Persistence = settings.waveParticle.persistence;
			destination.m_WaveParticle.m_RadiusFalloff = settings.waveParticle.radiusFalloff;
			destination.m_WaveParticle.m_ProfileSharpness = settings.waveParticle.profileSharpness;
			destination.m_WaveParticle.m_FoamThreshold = settings.waveParticle.foamThreshold;
			destination.m_WaveParticle.m_FoamSoftness = settings.waveParticle.foamSoftness;
			destination.m_WaveParticle.m_Lifetime = settings.waveParticle.lifetime;
			destination.m_WaveParticle.m_RandomSeed = settings.waveParticle.randomSeed;

			destination.m_FlowMap.m_Enabled = settings.flowMap.enabled;
			destination.m_FlowMap.m_Strength = settings.flowMap.strength;
			destination.m_FlowMap.m_Speed = settings.flowMap.speed;
			destination.m_FlowMap.m_PhaseLength = settings.flowMap.phaseLength;
			destination.m_FlowMap.m_NoiseAmount = settings.flowMap.noiseAmount;
			destination.m_FlowMap.m_WorldOrigin = ToRuntimeVec2(settings.flowMap.worldOrigin);
			destination.m_FlowMap.m_WorldSize = ToRuntimeVec2(settings.flowMap.worldSize);
			destination.m_FlowMap.m_FallbackDirection = ToRuntimeVec2(settings.flowMap.fallbackDirection);

			destination.m_Optics.m_MaxCrossDistance = settings.optics.maxCrossDistance;
			destination.m_Optics.m_MaxRefractionCrossDistance = settings.optics.maxRefractionCrossDistance;
			destination.m_Optics.m_MultiScatterScale = settings.optics.multiScatterScale;
			destination.m_Optics.m_WaterDispersionStrength = settings.optics.waterDispersionStrength;
			destination.m_Optics.m_SSSPathScale = settings.optics.sssPathScale;
			destination.m_Optics.m_SSSNonlinearStrength = settings.optics.sssNonlinearStrength;
			destination.m_Optics.m_SSSScatterBoost = settings.optics.sssScatterBoost;
			destination.m_Optics.m_BacklitPathScale = settings.optics.backlitPathScale;
			destination.m_Optics.m_BacklitPhaseG = settings.optics.backlitPhaseG;

			destination.m_Volume.m_ResolutionScale = settings.volume.resolutionScale;
			destination.m_Volume.m_SampleCount = settings.volume.sampleCount;
			destination.m_Volume.m_SpatialFilterIterations = settings.volume.spatialFilterIterations;
			destination.m_Volume.m_SpatialDepthSensitivity = settings.volume.spatialDepthSensitivity;

			destination.m_SSS.m_Enabled = settings.thinSSSEnabled;
			destination.m_SSS.m_MaxThicknessDistance = settings.maxThicknessDistance;
			destination.m_SSS.m_DeepWaterThicknessFallback = settings.deepWaterThicknessFallback;
			destination.m_Caustics.m_Enabled = settings.causticsEnabled;
			destination.m_Caustics.m_Intensity = settings.causticsIntensity;
			destination.m_Caustics.m_Scale = settings.causticsScale;
			destination.m_Refraction.m_Enabled = settings.refractionEnabled;
			destination.m_Refraction.m_DistortionStrength = settings.refractionDistortionStrength;
			destination.m_SSR.m_Enabled = settings.ssrEnabled;
			destination.m_SSR.m_MaxDistance = settings.ssrMaxDistance;
			destination.m_SSR.m_MaxRoughness = settings.ssrMaxRoughness;
			destination.Validate();
		}

		bool ShouldReinitializeWaterFFT(
			const VansGraphics::VansWaterConfig& previous,
			const VansGraphics::VansWaterConfig& current)
		{
			return previous.m_Spectrum.m_CascadeCount != current.m_Spectrum.m_CascadeCount
				|| previous.m_Spectrum.m_BaseCoverage != current.m_Spectrum.m_BaseCoverage
				|| previous.m_Spectrum.m_CascadeScale != current.m_Spectrum.m_CascadeScale
				|| previous.m_Spectrum.m_WindDirection != current.m_Spectrum.m_WindDirection
				|| previous.m_Spectrum.m_WindSpeed != current.m_Spectrum.m_WindSpeed
				|| previous.m_Spectrum.m_SpectrumAmplitude != current.m_Spectrum.m_SpectrumAmplitude
				|| previous.m_Spectrum.m_MinWavelength != current.m_Spectrum.m_MinWavelength
				|| previous.m_Spectrum.m_SmallWaveDamping != current.m_Spectrum.m_SmallWaveDamping
				|| previous.m_Spectrum.m_WindDependency != current.m_Spectrum.m_WindDependency
				|| previous.m_Spectrum.m_Depth != current.m_Spectrum.m_Depth
				|| previous.m_Spectrum.m_RandomSeed != current.m_Spectrum.m_RandomSeed;
		}

		bool ShouldRegenerateGerstnerSpectrum(
			const VansGraphics::VansWaterConfig& previous,
			const VansGraphics::VansWaterConfig& current)
		{
			return previous.m_Spectrum.m_GerstnerWaveCount != current.m_Spectrum.m_GerstnerWaveCount
				|| previous.m_Spectrum.m_WindDirection != current.m_Spectrum.m_WindDirection
				|| previous.m_Spectrum.m_WindSpeed != current.m_Spectrum.m_WindSpeed
				|| previous.m_Spectrum.m_SwellAmplitude != current.m_Spectrum.m_SwellAmplitude;
		}

		bool ShouldRegenerateWaveParticleSpectrum(
			const VansGraphics::VansWaterConfig& previous,
			const VansGraphics::VansWaterConfig& current)
		{
			return previous.m_Spectrum.m_WindDirection != current.m_Spectrum.m_WindDirection
				|| previous.m_WaveParticle.m_ParticleCount != current.m_WaveParticle.m_ParticleCount
				|| previous.m_WaveParticle.m_OctaveCount != current.m_WaveParticle.m_OctaveCount
				|| previous.m_WaveParticle.m_Profile != current.m_WaveParticle.m_Profile
				|| previous.m_WaveParticle.m_DomainSize != current.m_WaveParticle.m_DomainSize
				|| previous.m_WaveParticle.m_Amplitude != current.m_WaveParticle.m_Amplitude
				|| previous.m_WaveParticle.m_MinRadius != current.m_WaveParticle.m_MinRadius
				|| previous.m_WaveParticle.m_MaxRadius != current.m_WaveParticle.m_MaxRadius
				|| previous.m_WaveParticle.m_PhaseVelocity != current.m_WaveParticle.m_PhaseVelocity
				|| previous.m_WaveParticle.m_Damping != current.m_WaveParticle.m_Damping
				|| previous.m_WaveParticle.m_DirectionSpread != current.m_WaveParticle.m_DirectionSpread
				|| previous.m_WaveParticle.m_Lacunarity != current.m_WaveParticle.m_Lacunarity
				|| previous.m_WaveParticle.m_Persistence != current.m_WaveParticle.m_Persistence
				|| previous.m_WaveParticle.m_RadiusFalloff != current.m_WaveParticle.m_RadiusFalloff
				|| previous.m_WaveParticle.m_ProfileSharpness != current.m_WaveParticle.m_ProfileSharpness
				|| previous.m_WaveParticle.m_FoamThreshold != current.m_WaveParticle.m_FoamThreshold
				|| previous.m_WaveParticle.m_FoamSoftness != current.m_WaveParticle.m_FoamSoftness
				|| previous.m_WaveParticle.m_Lifetime != current.m_WaveParticle.m_Lifetime
				|| previous.m_WaveParticle.m_RandomSeed != current.m_WaveParticle.m_RandomSeed;
		}

		ReflectionProbeSettingsSnapshot ToReflectionProbeSettings(VansGraphics::VansReflectionProbeSystem& source)
		{
			ReflectionProbeSettingsSnapshot settings;
			settings.available = true;
			settings.arrayResolution = source.GetArrayResolution();
			settings.mipCount = source.GetMipCount();

			const auto& editor = source.GetEditorState();
			settings.editor.selectedProbeIndex = editor.selectedProbeIndex;
			settings.editor.showProbeGizmos = editor.showProbeGizmos;
			settings.editor.showInfluenceVolumes = editor.showInfluenceVolumes;
			settings.editor.showBlendVolumes = editor.showBlendVolumes;
			settings.editor.showPlacementGrid = editor.showPlacementGrid;
			settings.editor.showRegions = editor.showRegions;
			settings.editor.previewCubemap = editor.previewCubemap;
			settings.editor.previewFace = editor.previewFace;
			settings.editor.previewRoughness = editor.previewRoughness;
			settings.editor.debugView = static_cast<int>(editor.debugView);

			const auto& placement = source.GetPlacementSettings();
			settings.placement.enabled = placement.enabled;
			settings.placement.volumeMin = ToEditorVec3(placement.volumeMin);
			settings.placement.volumeMax = ToEditorVec3(placement.volumeMax);
			settings.placement.uniformSpacing = placement.uniformSpacing;
			settings.placement.uniformBoxSizeScale = placement.uniformBoxSizeScale;
			settings.placement.uniformProbeResolution = placement.uniformProbeResolution;
			settings.placement.maxProbeCount = placement.maxProbeCount;

			const auto& lighting = source.GetLightingSettings();
			settings.lighting.maxBlendCount = lighting.maxBlendCount;
			settings.lighting.ssrRoughnessFadeStart = lighting.ssrRoughnessFadeStart;
			settings.lighting.ssrRoughnessFadeEnd = lighting.ssrRoughnessFadeEnd;
			settings.lighting.skyIntensity = lighting.skyIntensity;

			const auto& probes = source.GetProbes();
			const auto& bakeResults = source.GetBakeResults();
			settings.probes.reserve(probes.size());
			for (std::size_t i = 0; i < probes.size(); ++i)
			{
				const auto& probe = probes[i];
				ReflectionProbeEntrySnapshot entry;
				entry.name = probe.name;
				entry.type = static_cast<int>(probe.type);
				entry.shape = static_cast<int>(probe.shape);
				entry.position = ToEditorVec3(probe.position);
				entry.capturePosition = ToEditorVec3(probe.capturePosition);
				entry.boxMin = ToEditorVec3(probe.boxMin);
				entry.boxMax = ToEditorVec3(probe.boxMax);
				entry.radius = probe.radius;
				entry.blendDistance = probe.blendDistance;
				entry.priority = probe.priority;
				entry.intensity = probe.intensity;
				entry.specularIntensity = probe.specularIntensity;
				entry.enabled = probe.enabled;
				entry.boxProjection = probe.boxProjection;
				entry.autoGenerated = probe.autoGenerated;
				entry.bakeStatus = i < bakeResults.size() ? bakeResults[i].status : "No bake result";
				settings.probes.push_back(entry);
			}

			settings.validationErrors = source.ValidatePlacement();
			return settings;
		}

		ScenePropertyValue Vec3ScenePropertyValue(const glm::vec3& value)
		{
			return ScenePropertyValues::Array({
				ScenePropertyValues::Float(value.x),
				ScenePropertyValues::Float(value.y),
				ScenePropertyValues::Float(value.z)
			});
		}

		const char* ReflectionProbeTypeName(VansGraphics::ReflectionProbeType type)
		{
			switch (type)
			{
			case VansGraphics::ReflectionProbeType::Realtime: return "realtime";
			case VansGraphics::ReflectionProbeType::Sky: return "sky";
			default: return "baked";
			}
		}

		const char* ReflectionProbeRefreshModeName(VansGraphics::ReflectionProbeRefreshMode mode)
		{
			switch (mode)
			{
			case VansGraphics::ReflectionProbeRefreshMode::EveryFrame: return "every_frame";
			case VansGraphics::ReflectionProbeRefreshMode::TimeSliced: return "time_sliced";
			case VansGraphics::ReflectionProbeRefreshMode::OnDemand: return "on_demand";
			default: return "on_load";
			}
		}

		ScenePropertyValue ReflectionProbePropertyValue(const VansGraphics::VansReflectionProbeDesc& probe)
		{
			return ScenePropertyValues::Object({
				{ "name", ScenePropertyValues::String(probe.name) },
				{ "type", ScenePropertyValues::String(ReflectionProbeTypeName(probe.type)) },
				{ "shape", ScenePropertyValues::String(
					probe.shape == VansGraphics::ReflectionProbeShape::Sphere ? "sphere" : "box") },
				{ "refreshMode", ScenePropertyValues::String(ReflectionProbeRefreshModeName(probe.refreshMode)) },
				{ "position", Vec3ScenePropertyValue(probe.position) },
				{ "capturePosition", Vec3ScenePropertyValue(probe.capturePosition) },
				{ "boxMin", Vec3ScenePropertyValue(probe.boxMin) },
				{ "boxMax", Vec3ScenePropertyValue(probe.boxMax) },
				{ "radius", ScenePropertyValues::Float(probe.radius) },
				{ "blendDistance", ScenePropertyValues::Float(probe.blendDistance) },
				{ "priority", ScenePropertyValues::Float(probe.priority) },
				{ "intensity", ScenePropertyValues::Float(probe.intensity) },
				{ "specularIntensity", ScenePropertyValues::Float(probe.specularIntensity) },
				{ "nearPlane", ScenePropertyValues::Float(probe.nearPlane) },
				{ "farPlane", ScenePropertyValues::Float(probe.farPlane) },
				{ "resolution", ScenePropertyValues::Int(static_cast<std::int64_t>(probe.resolution)) },
				{ "cullingMask", ScenePropertyValues::Int(static_cast<std::int64_t>(probe.cullingMask)) },
				{ "regionId", ScenePropertyValues::Int(static_cast<std::int64_t>(probe.regionId)) },
				{ "facesPerFrame", ScenePropertyValues::Int(static_cast<std::int64_t>(probe.realtimeFacesPerFrame)) },
				{ "enabled", ScenePropertyValues::Bool(probe.enabled) },
				{ "boxProjection", ScenePropertyValues::Bool(probe.boxProjection) },
				{ "autoGenerated", ScenePropertyValues::Bool(probe.autoGenerated) },
				{ "portal", ScenePropertyValues::Bool(probe.portal) },
				{ "cachePath", ScenePropertyValues::String(probe.cachePath) }
			});
		}

		ScenePropertyValue ReflectionProbeScenePropertyValue(
			const VansGraphics::VansReflectionProbeSystem& source)
		{
			const auto& lighting = source.GetLightingSettings();
			const auto& placement = source.GetPlacementSettings();
			std::vector<ScenePropertyValue> probes;
			probes.reserve(source.GetProbes().size());
			for (const VansGraphics::VansReflectionProbeDesc& probe : source.GetProbes())
				probes.push_back(ReflectionProbePropertyValue(probe));

			return ScenePropertyValues::Object({
				{ "lighting", ScenePropertyValues::Object({
					{ "maxBlendCount", ScenePropertyValues::Int(static_cast<std::int64_t>(lighting.maxBlendCount)) },
					{ "ssrRoughnessFadeStart", ScenePropertyValues::Float(lighting.ssrRoughnessFadeStart) },
					{ "ssrRoughnessFadeEnd", ScenePropertyValues::Float(lighting.ssrRoughnessFadeEnd) },
					{ "skyIntensity", ScenePropertyValues::Float(lighting.skyIntensity) }
				}) },
				{ "placement", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(placement.enabled) },
					{ "geometryOnly", ScenePropertyValues::Bool(true) },
					{ "volumeMin", Vec3ScenePropertyValue(placement.volumeMin) },
					{ "volumeMax", Vec3ScenePropertyValue(placement.volumeMax) },
					{ "cellSize", ScenePropertyValues::Float(placement.cellSize) },
					{ "indoorSpacing", ScenePropertyValues::Float(placement.indoorSpacing) },
					{ "corridorSpacing", ScenePropertyValues::Float(placement.corridorSpacing) },
					{ "outdoorSpacing", ScenePropertyValues::Float(placement.outdoorSpacing) },
					{ "solidThreshold", ScenePropertyValues::Float(placement.solidThreshold) },
					{ "refinementThreshold", ScenePropertyValues::Float(placement.refinementThreshold) },
					{ "maxProbeCount", ScenePropertyValues::Int(static_cast<std::int64_t>(placement.maxProbeCount)) },
					{ "uniformSpacing", ScenePropertyValues::Float(placement.uniformSpacing) },
					{ "uniformBoxSizeScale", ScenePropertyValues::Float(placement.uniformBoxSizeScale) },
					{ "uniformProbeResolution",
						ScenePropertyValues::Int(static_cast<std::int64_t>(placement.uniformProbeResolution)) }
				}) },
				{ "probes", ScenePropertyValues::Array(std::move(probes)) }
			});
		}

		void ApplyReflectionProbeSettingsToSystem(
			const ReflectionProbeSettingsSnapshot& settings,
			VansGraphics::VansReflectionProbeSystem& destination)
		{
			auto& editor = destination.GetEditorState();
			editor.selectedProbeIndex = settings.editor.selectedProbeIndex;
			editor.showProbeGizmos = settings.editor.showProbeGizmos;
			editor.showInfluenceVolumes = settings.editor.showInfluenceVolumes;
			editor.showBlendVolumes = settings.editor.showBlendVolumes;
			editor.showPlacementGrid = settings.editor.showPlacementGrid;
			editor.showRegions = settings.editor.showRegions;
			editor.previewCubemap = settings.editor.previewCubemap;
			editor.previewFace = std::clamp(settings.editor.previewFace, 0, 5);
			editor.previewRoughness = std::clamp(settings.editor.previewRoughness, 0.0f, 1.0f);
			editor.debugView = static_cast<VansGraphics::ReflectionProbeDebugView>(std::clamp(settings.editor.debugView, 0, 7));

			auto& placement = destination.GetPlacementSettings();
			placement.enabled = settings.placement.enabled;
			placement.volumeMin = ToRuntimeVec3(settings.placement.volumeMin);
			placement.volumeMax = ToRuntimeVec3(settings.placement.volumeMax);
			placement.uniformSpacing = settings.placement.uniformSpacing;
			placement.uniformBoxSizeScale = settings.placement.uniformBoxSizeScale;
			placement.uniformProbeResolution = std::clamp(settings.placement.uniformProbeResolution, 32u, 512u);
			placement.maxProbeCount = std::clamp(settings.placement.maxProbeCount, 1u, 4096u);

			auto& lighting = destination.GetLightingSettings();
			lighting.maxBlendCount = std::clamp(settings.lighting.maxBlendCount, 1u, 4u);
			lighting.ssrRoughnessFadeStart = settings.lighting.ssrRoughnessFadeStart;
			lighting.ssrRoughnessFadeEnd = settings.lighting.ssrRoughnessFadeEnd;
			lighting.skyIntensity = settings.lighting.skyIntensity;

			auto& probes = destination.GetProbes();
			const std::size_t probeCount = std::min(probes.size(), settings.probes.size());
			for (std::size_t i = 0; i < probeCount; ++i)
			{
				auto& probe = probes[i];
				const ReflectionProbeEntrySnapshot& entry = settings.probes[i];
				probe.position = ToRuntimeVec3(entry.position);
				probe.capturePosition = ToRuntimeVec3(entry.capturePosition);
				probe.boxMin = ToRuntimeVec3(entry.boxMin);
				probe.boxMax = ToRuntimeVec3(entry.boxMax);
				probe.radius = entry.radius;
				probe.blendDistance = entry.blendDistance;
				probe.priority = entry.priority;
				probe.intensity = entry.intensity;
				probe.specularIntensity = entry.specularIntensity;
				probe.enabled = entry.enabled;
				probe.boxProjection = entry.boxProjection;
				destination.MarkDirty(i);
			}

			destination.UploadMetadata();
		}

		CloudSettings ToCloudSettings(const VansGraphics::VansCloudParamsGPU& source)
		{
			CloudSettings settings;
			settings.planetRadius = source.planetRadius;
			settings.seaLevel = source.seaLevel;
			settings.cloudMinHeight = source.cloudMinHeight;
			settings.cloudMaxHeight = source.cloudMaxHeight;
			settings.density = source.density;
			settings.coverage = source.coverage;
			settings.sunBrightness = source.sunBrightness;
			settings.phaseG = source.phaseG;
			settings.mainTileMeters = source.mainTileMeters;
			settings.detailTileMeters = source.detailTileMeters;
			settings.mainHeightScale = source.mainHeightScale;
			settings.detailHeightScale = source.detailHeightScale;
			settings.thresholdLowCoverage = source.thresholdLowCoverage;
			settings.thresholdHighCoverage = source.thresholdHighCoverage;
			settings.densityRemapLow = source.densityRemapLow;
			settings.densityRemapHigh = source.densityRemapHigh;
			settings.mainErosionStrength = source.mainErosionStrength;
			settings.detailErosionStrength = source.detailErosionStrength;
			settings.edgeErosionStrength = source.edgeErosionStrength;
			settings.verticalShapePower = source.verticalShapePower;
			settings.detailErosionLow = source.detailErosionLow;
			settings.detailErosionHigh = source.detailErosionHigh;
			settings.detailEdgeStrength = source.detailEdgeStrength;
			settings.shadowDensityScale = source.shadowDensityScale;
			return settings;
		}

		void ApplyCloudSettingsToGPUData(
			const CloudSettings& settings,
			VansGraphics::VansCloudParamsGPU& destination)
		{
			destination.planetRadius = settings.planetRadius;
			destination.seaLevel = settings.seaLevel;
			destination.cloudMinHeight = settings.cloudMinHeight;
			destination.cloudMaxHeight = settings.cloudMaxHeight;
			destination.density = settings.density;
			destination.coverage = settings.coverage;
			destination.sunBrightness = settings.sunBrightness;
			destination.phaseG = settings.phaseG;
			destination.mainTileMeters = settings.mainTileMeters;
			destination.detailTileMeters = settings.detailTileMeters;
			destination.mainHeightScale = settings.mainHeightScale;
			destination.detailHeightScale = settings.detailHeightScale;
			destination.thresholdLowCoverage = settings.thresholdLowCoverage;
			destination.thresholdHighCoverage = settings.thresholdHighCoverage;
			destination.densityRemapLow = settings.densityRemapLow;
			destination.densityRemapHigh = settings.densityRemapHigh;
			destination.mainErosionStrength = settings.mainErosionStrength;
			destination.detailErosionStrength = settings.detailErosionStrength;
			destination.edgeErosionStrength = settings.edgeErosionStrength;
			destination.verticalShapePower = settings.verticalShapePower;
			destination.detailErosionLow = settings.detailErosionLow;
			destination.detailErosionHigh = settings.detailErosionHigh;
			destination.detailEdgeStrength = settings.detailEdgeStrength;
			destination.shadowDensityScale = settings.shadowDensityScale;
		}

		PostProcessSettingsSnapshot ToAPIPostProcessSettings(
			const VansGraphics::VansPostProcessProfile& source)
		{
			PostProcessSettingsSnapshot settings;
			settings.available = true;
			settings.enableAutoExposure = source.m_EnableAutoExposure;
			settings.exposureCompensation = source.m_ExposureCompensation;
			settings.minEV100 = source.m_MinEV100;
			settings.maxEV100 = source.m_MaxEV100;
			settings.adaptationSpeedUp = source.m_AdaptationSpeedUp;
			settings.adaptationSpeedDown = source.m_AdaptationSpeedDown;
			settings.enableBloom = source.m_EnableBloom;
			settings.bloomThreshold = source.m_BloomThreshold;
			settings.bloomKnee = source.m_BloomKnee;
			settings.bloomIntensity = source.m_BloomIntensity;
			settings.bloomScatter = source.m_BloomScatter;
			settings.toneMapperType = source.m_ToneMapperType;
			settings.whitePoint = source.m_WhitePoint;
			settings.enableColorGrading = source.m_EnableColorGrading;
			settings.contrast = source.m_Contrast;
			settings.saturation = source.m_Saturation;
			settings.hueShift = source.m_HueShift;
			settings.temperature = source.m_Temperature;
			settings.tint = source.m_Tint;
			return settings;
		}

		void ApplyPostProcessSettingsToProfile(
			const PostProcessSettingsSnapshot& settings,
			VansGraphics::VansPostProcessProfile& destination)
		{
			destination.m_EnableAutoExposure = settings.enableAutoExposure;
			destination.m_ExposureCompensation = std::clamp(settings.exposureCompensation, -16.0f, 16.0f);
			destination.m_MinEV100 = std::clamp(settings.minEV100, -24.0f, 24.0f);
			destination.m_MaxEV100 = std::clamp(settings.maxEV100, destination.m_MinEV100, 24.0f);
			destination.m_AdaptationSpeedUp = std::clamp(settings.adaptationSpeedUp, 0.0f, 20.0f);
			destination.m_AdaptationSpeedDown = std::clamp(settings.adaptationSpeedDown, 0.0f, 20.0f);
			destination.m_EnableBloom = settings.enableBloom;
			destination.m_BloomThreshold = std::clamp(settings.bloomThreshold, 0.0f, 64.0f);
			destination.m_BloomKnee = std::clamp(settings.bloomKnee, 0.0f, 1.0f);
			destination.m_BloomIntensity = std::clamp(settings.bloomIntensity, 0.0f, 10.0f);
			destination.m_BloomScatter = std::clamp(settings.bloomScatter, 0.0f, 1.0f);
			destination.m_ToneMapperType = std::clamp(settings.toneMapperType, 0, 2);
			destination.m_WhitePoint = std::clamp(settings.whitePoint, 0.1f, 64.0f);
			destination.m_EnableColorGrading = settings.enableColorGrading;
			destination.m_Contrast = std::clamp(settings.contrast, 0.0f, 4.0f);
			destination.m_Saturation = std::clamp(settings.saturation, 0.0f, 4.0f);
			destination.m_HueShift = std::clamp(settings.hueShift, -1.0f, 1.0f);
			destination.m_Temperature = std::clamp(settings.temperature, -1.0f, 1.0f);
			destination.m_Tint = std::clamp(settings.tint, -1.0f, 1.0f);
			destination.m_IsDirty = true;
		}

		class SetPostProcessSettingsCommand final : public IEngineCommand
		{
		public:
			explicit SetPostProcessSettingsCommand(PostProcessSettingsSnapshot settings)
				: m_Settings(settings)
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				if (!m_HasBefore)
				{
					m_Before = ToAPIPostProcessSettings(materialManager->m_PostProcessProfile);
					m_HasBefore = true;
				}
				ApplyPostProcessSettingsToProfile(m_Settings, materialManager->m_PostProcessProfile);
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (materialManager)
					ApplyPostProcessSettingsToProfile(m_Before, materialManager->m_PostProcessProfile);
			}

			std::string GetDescription() const override
			{
				return "Set post-process settings";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				return dynamic_cast<const SetPostProcessSettingsCommand*>(&other) != nullptr;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetPostProcessSettingsCommand*>(&other);
				if (!next)
					return false;

				m_Settings = next->m_Settings;
				Execute(context);
				return true;
			}

		private:
			PostProcessSettingsSnapshot m_Settings;
			PostProcessSettingsSnapshot m_Before;
			bool m_HasBefore = false;
		};

		FogSettings ToAPIFogSettings(const VansGraphics::VansFogSettings& source)
		{
			FogSettings settings;
			settings.fogDensity = source.fogDensity;
			settings.heightFalloff = source.heightFalloff;
			settings.sunScatterScale = source.sunScatterScale;
			settings.ambientScale = source.ambientScale;
			settings.fogMinHeight = source.fogMinHeight;
			settings.skyFogDistance = source.skyFogDistance;
			return settings;
		}

		VansGraphics::VansFogSettings ToRuntimeFogSettings(const FogSettings& source)
		{
			VansGraphics::VansFogSettings settings;
			settings.fogDensity = source.fogDensity;
			settings.heightFalloff = source.heightFalloff;
			settings.sunScatterScale = source.sunScatterScale;
			settings.ambientScale = source.ambientScale;
			settings.fogMinHeight = source.fogMinHeight;
			settings.skyFogDistance = source.skyFogDistance;
			return settings;
		}

		FogVolumeSettings ToAPIFogVolumeSettings(const VansGraphics::VansFogVolumeSettings& source)
		{
			FogVolumeSettings settings;
			settings.density = source.density;
			settings.anisotropy = source.anisotropy;
			settings.scatterScale = source.scatterScale;
			settings.ambientScale = source.ambientScale;
			settings.volumeNear = source.volumeNear;
			settings.volumeFar = source.volumeFar;
			settings.slicePower = source.slicePower;
			settings.padding = source.padding;
			std::copy(std::begin(source.fogBoxMin), std::end(source.fogBoxMin), std::begin(settings.fogBoxMin));
			std::copy(std::begin(source.fogBoxMax), std::end(source.fogBoxMax), std::begin(settings.fogBoxMax));
			return settings;
		}

		VansGraphics::VansFogVolumeSettings ToRuntimeFogVolumeSettings(const FogVolumeSettings& source)
		{
			VansGraphics::VansFogVolumeSettings settings;
			settings.density = source.density;
			settings.anisotropy = source.anisotropy;
			settings.scatterScale = source.scatterScale;
			settings.ambientScale = source.ambientScale;
			settings.volumeNear = source.volumeNear;
			settings.volumeFar = source.volumeFar;
			settings.slicePower = source.slicePower;
			settings.padding = source.padding;
			std::copy(std::begin(source.fogBoxMin), std::end(source.fogBoxMin), std::begin(settings.fogBoxMin));
			std::copy(std::begin(source.fogBoxMax), std::end(source.fogBoxMax), std::begin(settings.fogBoxMax));
			return settings;
		}

		class SetFogSettingsCommand final : public IEngineCommand
		{
		public:
			explicit SetFogSettingsCommand(FogSettings settings)
				: m_Settings(settings)
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				if (!m_HasBefore)
				{
					m_Before = ToAPIFogSettings(materialManager->GetFogSettings());
					m_HasBefore = true;
				}

				materialManager->ApplyFogSettings(ToRuntimeFogSettings(m_Settings));
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				materialManager->ApplyFogSettings(ToRuntimeFogSettings(m_Before));
			}

			std::string GetDescription() const override
			{
				return "Set fog settings";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				return dynamic_cast<const SetFogSettingsCommand*>(&other) != nullptr;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetFogSettingsCommand*>(&other);
				if (!next)
					return false;

				m_Settings = next->m_Settings;
				Execute(context);
				return true;
			}

		private:
			FogSettings m_Settings;
			FogSettings m_Before;
			bool m_HasBefore = false;
		};

		class SetFogVolumeSettingsCommand final : public IEngineCommand
		{
		public:
			explicit SetFogVolumeSettingsCommand(FogVolumeSettings settings)
				: m_Settings(settings)
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				if (!m_HasBefore)
				{
					m_Before = ToAPIFogVolumeSettings(materialManager->GetFogVolumeSettings());
					m_HasBefore = true;
				}

				materialManager->ApplyFogVolumeSettings(ToRuntimeFogVolumeSettings(m_Settings));
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				materialManager->ApplyFogVolumeSettings(ToRuntimeFogVolumeSettings(m_Before));
			}

			std::string GetDescription() const override
			{
				return "Set fog volume settings";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				return dynamic_cast<const SetFogVolumeSettingsCommand*>(&other) != nullptr;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetFogVolumeSettingsCommand*>(&other);
				if (!next)
					return false;

				m_Settings = next->m_Settings;
				Execute(context);
				return true;
			}

		private:
			FogVolumeSettings m_Settings;
			FogVolumeSettings m_Before;
			bool m_HasBefore = false;
		};

		class SetCloudSettingsCommand final : public IEngineCommand
		{
		public:
			explicit SetCloudSettingsCommand(CloudSettings settings)
				: m_Settings(settings)
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				if (!m_HasBefore)
				{
					m_Before = ToCloudSettings(materialManager->m_CloudParams);
					m_HasBefore = true;
				}

				ApplyCloudSettingsToGPUData(m_Settings, materialManager->m_CloudParams);
				materialManager->UploadCloudParamsToGPU();
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
				if (!materialManager)
					return;

				ApplyCloudSettingsToGPUData(m_Before, materialManager->m_CloudParams);
				materialManager->UploadCloudParamsToGPU();
			}

			std::string GetDescription() const override
			{
				return "Set cloud settings";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				return dynamic_cast<const SetCloudSettingsCommand*>(&other) != nullptr;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetCloudSettingsCommand*>(&other);
				if (!next)
					return false;

				m_Settings = next->m_Settings;
				Execute(context);
				return true;
			}

		private:
			CloudSettings m_Settings;
			CloudSettings m_Before;
			bool m_HasBefore = false;
		};
	}

	EngineAPIImpl::EngineAPIImpl(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device)
		: m_Scene(scene)
		, m_Device(device)
		, m_ScriptContext(&GetDefaultScriptContext())
	{
		m_ScriptContext->SetScene(static_cast<VansGraphics::VansScene*>(m_Scene));
	}

	void EngineAPIImpl::BindRuntime(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device)
	{
		const bool runtimeChanged = m_Scene != scene || m_Device != device;
		m_Scene = scene;
		m_Device = device;
		if (runtimeChanged)
		{
			m_UndoStack.clear();
			m_RedoStack.clear();
			m_AllowNextCommandMerge = true;
		}
		if (!m_ScriptContext)
			m_ScriptContext = &GetDefaultScriptContext();
		m_ScriptContext->SetScene(static_cast<VansGraphics::VansScene*>(m_Scene));
	}

	void EngineAPIImpl::BindGlobalRuntime(RuntimeRenderDeviceHandle device)
	{
		BindRuntime(::m_Scene, device);
	}

	void EngineAPIImpl::BindScriptContext(VansScriptContext* scriptContext)
	{
		m_ScriptContext = scriptContext ? scriptContext : &GetDefaultScriptContext();
		if (m_ScriptContext)
			m_ScriptContext->SetScene(static_cast<VansGraphics::VansScene*>(m_Scene));
	}

	SceneDataSnapshot EngineAPIImpl::GetSceneSnapshot() const
	{
		return {};
	}

	EntityDataSnapshot EngineAPIImpl::GetEntitySnapshot(EntityId) const
	{
		return {};
	}

	ComponentDataSnapshot EngineAPIImpl::GetComponentSnapshot(EntityId, ComponentId) const
	{
		return {};
	}

	void EngineAPIImpl::SubmitCommand(std::unique_ptr<IEngineCommand> command)
	{
		if (!command)
			return;

		EngineCommandContext context(m_Scene, m_Device);
		if (m_AllowNextCommandMerge && !m_UndoStack.empty() && m_UndoStack.back()->CanMergeWith(*command))
		{
			if (m_UndoStack.back()->MergeWith(*command, context))
			{
				m_RedoStack.clear();
				m_AllowNextCommandMerge = true;
				return;
			}
		}

		command->Execute(context);
		m_UndoStack.push_back(std::move(command));
		m_RedoStack.clear();
		m_AllowNextCommandMerge = true;
	}

	void EngineAPIImpl::BreakCommandMergeGroup()
	{
		m_AllowNextCommandMerge = false;
	}

	std::vector<AssetEntry> EngineAPIImpl::QueryAssets(AssetTypeFilter filter) const
	{
		std::vector<AssetEntry> entries;
		const Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
		if (!database)
			return entries;

		for (const Vans::VansAssetRecord& record : database->All())
		{
			if (record.state == Vans::VansAssetState::Missing)
				continue;

			const AssetType type = ToEditorAssetType(record.type);
			if (filter.type != AssetType::Unknown && type != filter.type)
				continue;
			if (!filter.includeUnknown && type == AssetType::Unknown)
				continue;

			AssetEntry entry;
			entry.id = 0;
			entry.guid = record.guid.ToString();
			entry.name = record.sourcePath.filename().string();
			entry.relativePath = record.sourcePath.string();
			entry.type = type;
			entries.push_back(std::move(entry));
		}

		return entries;
	}

	AssetMetaSnapshot EngineAPIImpl::GetAssetMeta(AssetId) const
	{
		return {};
	}

	ProjectBrowserRootSnapshot EngineAPIImpl::GetProjectBrowserRoot() const
	{
		ProjectBrowserRootSnapshot snapshot;
		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
		{
			snapshot.projectLoaded = true;
			snapshot.rootPath = projectManager.GetProjectRootPath();
			snapshot.rootLabel = projectManager.GetProjectName();
			return snapshot;
		}

		if (auto* configuration = VansConfigration::GetInstance())
			snapshot.rootPath = configuration->GetProjectRootPath() + "EngineAssets";
		snapshot.rootLabel = "EngineAssets";
		return snapshot;
	}

	AssetDragPayload EngineAPIImpl::CreateAssetDragPayload(const std::string& assetPath)
	{
		AssetDragPayload payload;
		auto& projectManager = Vans::VansProjectManager::Get();
		if (!projectManager.IsProjectLoaded())
			return payload;

		Vans::VansAssetDatabase* database = projectManager.GetAssetDatabase();
		if (!database)
			return payload;

		const std::filesystem::path sourcePath(assetPath);
		std::string registrationError;
		if (!database->Find(sourcePath))
			database->RegisterOrRefresh(
				sourcePath,
				VansAssetOperationPolicy::Authoring(),
				registrationError);

		if (const auto record = database->Find(sourcePath))
		{
			payload.available = true;
			payload.guid = record->guid.ToString();
			payload.sourcePath = record->sourcePath.string();
			payload.displayName = record->sourcePath.filename().string();
			payload.assetType = ToEditorAssetType(record->type);
		}
		else
		{
			payload.error = registrationError;
		}

		return payload;
	}

	AssetGuidResolution EngineAPIImpl::ResolveAssetGuid(const std::string& assetGuid) const
	{
		AssetGuidResolution resolution;
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(assetGuid, guid))
			return resolution;

		const Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
		if (!database)
			return resolution;

		const auto record = database->Find(guid);
		if (!record || record->state == Vans::VansAssetState::Missing)
			return resolution;

		resolution.found = true;
		resolution.asset.id = 0;
		resolution.asset.guid = record->guid.ToString();
		resolution.asset.name = record->sourcePath.filename().string();
		resolution.asset.relativePath = record->sourcePath.string();
		resolution.asset.type = ToEditorAssetType(record->type);
		resolution.sourcePath = record->sourcePath.string();
		return resolution;
	}

	AssetRefreshResult EngineAPIImpl::RefreshProjectAsset(const std::string& assetPath, bool importIfMissing)
	{
		AssetRefreshResult result;
		Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
		if (!database)
		{
			result.message = "Asset database is not available";
			return result;
		}

		std::string refreshError;
		result.success = database->RegisterOrRefresh(
			std::filesystem::path(assetPath),
			importIfMissing
				? VansAssetOperationPolicy::Authoring()
				: VansAssetOperationPolicy::ReadOnly(),
			refreshError);
		result.message = refreshError;
		return result;
	}

	std::vector<RecentProjectEntry> EngineAPIImpl::GetRecentProjects() const
	{
		std::vector<RecentProjectEntry> entries;
		for (const auto& project : Vans::VansProjectManager::Get().GetRecentProjects())
		{
			RecentProjectEntry entry;
			entry.name = project.name;
			entry.path = project.path;
			entry.lastOpened = project.lastOpened;
			entries.push_back(std::move(entry));
		}
		return entries;
	}

	namespace
	{
		ProjectConfigDiagnosticSeverity ToEditorProjectConfigSeverity(
			VansProjectConfigDiagnosticSeverity severity)
		{
			switch (severity)
			{
			case VansProjectConfigDiagnosticSeverity::Warning:
				return ProjectConfigDiagnosticSeverity::Warning;
			case VansProjectConfigDiagnosticSeverity::Error:
				return ProjectConfigDiagnosticSeverity::Error;
			case VansProjectConfigDiagnosticSeverity::Info:
			default:
				return ProjectConfigDiagnosticSeverity::Info;
			}
		}

		std::vector<ProjectConfigDiagnostic> ToEditorProjectConfigDiagnostics(
			const VansProjectConfigDiagnostics& diagnostics)
		{
			std::vector<ProjectConfigDiagnostic> result;
			result.reserve(diagnostics.size());
			for (const VansProjectConfigDiagnostic& diagnostic : diagnostics)
			{
				ProjectConfigDiagnostic converted;
				converted.severity = ToEditorProjectConfigSeverity(diagnostic.severity);
				converted.propertyPointer = diagnostic.propertyPointer;
				converted.message = diagnostic.message;
				result.push_back(std::move(converted));
			}
			return result;
		}

		VansProjectConfigPathField ToProjectConfigPathField(ProjectPathField field)
		{
			switch (field)
			{
			case ProjectPathField::AssetsRoot:
				return VansProjectConfigPathField::AssetsRoot;
			case ProjectPathField::ImportedArtifactRoot:
				return VansProjectConfigPathField::ImportedArtifactRoot;
			case ProjectPathField::RenderSettings:
				return VansProjectConfigPathField::RenderSettings;
			case ProjectPathField::PhysicsSettings:
				return VansProjectConfigPathField::PhysicsSettings;
			case ProjectPathField::CollisionLayerSettings:
				return VansProjectConfigPathField::CollisionLayerSettings;
			case ProjectPathField::DefaultScene:
			default:
				return VansProjectConfigPathField::DefaultScene;
			}
		}

		ProjectConfigEditResult BuildProjectConfigEditResult(bool success, std::string message)
		{
			ProjectConfigEditResult result;
			result.success = success;
			result.message = std::move(message);
			result.diagnostics = ToEditorProjectConfigDiagnostics(
				VansProjectManager::Get().GetProjectConfigDiagnostics());
			return result;
		}
	}

	ProjectOpenResult EngineAPIImpl::OpenProject(const ProjectOpenRequest& request)
	{
		ProjectOpenResult result;
		CloseAllUIDocuments();

		auto& projectManager = Vans::VansProjectManager::Get();
		result.success = request.createNew
			? projectManager.CreateProject(request.projectPath, request.projectName)
			: projectManager.OpenProject(request.projectPath);

		if (!result.success)
		{
			result.message = request.createNew ? "CreateProject failed" : "OpenProject failed";
			return result;
		}

		result.projectRootPath = projectManager.GetProjectRootPath();
		result.defaultSceneRelativePath = projectManager.GetConfig().defaultScene;
		if (!result.defaultSceneRelativePath.empty())
			result.defaultScenePath =
				(std::filesystem::path(result.projectRootPath) / result.defaultSceneRelativePath).string();

		if (auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device))
		{
			device->GetPipelineCacheService().RefreshPersistencePath();

			const Vans::VansProjectFSRSettings& fsrSettings =
				projectManager.GetProjectSettings().GetFSRSettings();
			const VkExtent2D viewportExtent = device->GetRequestedSceneViewportExtent();
			device->RequestFSRConfig(
				static_cast<VansGraphics::VansFSRMode>(fsrSettings.mode),
				viewportExtent.width,
				viewportExtent.height,
				fsrSettings.sharpness);

			const Vans::VansProjectCommandRecordingSettings& commandRecordingSettings =
				projectManager.GetProjectSettings().GetCommandRecordingSettings();
			device->SetParallelCommandRecordingEnabled(commandRecordingSettings.parallelEnabled);
			device->SetFrameContextRingEnabled(
				commandRecordingSettings.frameContextRingEnabled,
				commandRecordingSettings.framesInFlight);
		}
		return result;
	}

	void EngineAPIImpl::CloseProject()
	{
		CloseAllUIDocuments();

		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
		{
			projectManager.CloseProject();
			if (auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device))
				device->GetPipelineCacheService().RefreshPersistencePath();
		}
	}

	ProjectConfigSnapshot EngineAPIImpl::GetProjectConfigSnapshot() const
	{
		ProjectConfigSnapshot snapshot;
		const auto& projectManager = Vans::VansProjectManager::Get();
		snapshot.projectLoaded = projectManager.IsProjectLoaded();
		if (!snapshot.projectLoaded)
			return snapshot;

		const VansProjectConfig& config = projectManager.GetConfig();
		snapshot.projectRootPath = projectManager.GetProjectRootPath();
		snapshot.projectName = config.projectName;
		snapshot.engineVersion = config.engineVersion;
		snapshot.createdAt = config.createdAt;
		snapshot.defaultScene = config.defaultScene;
		snapshot.assetsRoot = config.assetsRoot;
		snapshot.importedArtifactRoot = config.importedArtifactRoot;
		snapshot.metaExtension = config.metaExtension;
		snapshot.renderSettingsPath = config.renderSettings;
		snapshot.physicsSettingsPath = config.physicsSettings;
		snapshot.collisionLayerSettingsPath = config.collisionLayerSettings;

		snapshot.runtimeAssetBindings.reserve(config.runtimeAssetBindings.size());
		for (const auto& item : config.runtimeAssetBindings)
			snapshot.runtimeAssetBindings.push_back({ item.first, item.second });

		snapshot.assetDirectories.reserve(config.assetDirectories.size());
		for (const auto& item : config.assetDirectories)
			snapshot.assetDirectories.push_back({ item.first, item.second });

		snapshot.scriptSearchPaths = config.scriptSearchPaths;
		snapshot.diagnostics = ToEditorProjectConfigDiagnostics(
			projectManager.GetProjectConfigDiagnostics());
		return snapshot;
	}

	ProjectConfigEditResult EngineAPIImpl::SetProjectDefaultScene(const std::string& sceneRelativePath)
	{
		std::string error;
		const bool success =
			Vans::VansProjectManager::Get().SetProjectDefaultScene(sceneRelativePath, error);
		return BuildProjectConfigEditResult(success, success ? "Default scene updated" : error);
	}

	ProjectConfigEditResult EngineAPIImpl::SetProjectPathField(
		ProjectPathField field,
		const std::string& relativePath)
	{
		std::string error;
		const bool success = Vans::VansProjectManager::Get().SetProjectPathField(
			ToProjectConfigPathField(field),
			relativePath,
			error);
		return BuildProjectConfigEditResult(success, success ? "Project path updated" : error);
	}

	ProjectConfigEditResult EngineAPIImpl::SetProjectScriptSearchPaths(
		const std::vector<std::string>& paths)
	{
		std::string error;
		const bool success =
			Vans::VansProjectManager::Get().SetProjectScriptSearchPaths(paths, error);
		return BuildProjectConfigEditResult(success, success ? "Script search paths updated" : error);
	}

	ProjectConfigEditResult EngineAPIImpl::SetProjectAssetDirectory(
		const std::string& key,
		const std::string& relativePath)
	{
		std::string error;
		const bool success = Vans::VansProjectManager::Get().SetProjectAssetDirectory(
			key,
			relativePath,
			error);
		return BuildProjectConfigEditResult(success, success ? "Asset directory updated" : error);
	}

	ProjectConfigEditResult EngineAPIImpl::SaveProjectConfig()
	{
		std::string error;
		const bool success = Vans::VansProjectManager::Get().SaveProjectConfig(error);
		return BuildProjectConfigEditResult(success, success ? "Project config saved" : error);
	}

	float EngineAPIImpl::GetProjectPhysicsFixedTimeStep() const
	{
		auto& projectManager = Vans::VansProjectManager::Get();
		if (!projectManager.IsProjectLoaded())
			return 0.0f;
		return projectManager.GetProjectSettings().GetFixedTimeStep();
	}

	ProjectConfigEditResult EngineAPIImpl::SetProjectPhysicsFixedTimeStep(float fixedTimeStep)
	{
		if (fixedTimeStep <= 0.0f)
			return BuildProjectConfigEditResult(false, "Physics fixed timestep must be greater than zero");

		auto& projectManager = Vans::VansProjectManager::Get();
		if (!projectManager.IsProjectLoaded())
			return BuildProjectConfigEditResult(false, "No project loaded");

		projectManager.GetProjectSettings().SetFixedTimeStep(fixedTimeStep);
		if (!projectManager.SaveProjectSettings())
			return BuildProjectConfigEditResult(false, "Failed to save project physics settings");

		SetRuntimePhysicsFixedTimeStep(fixedTimeStep);
		return BuildProjectConfigEditResult(true, "Project physics settings saved");
	}

	bool EngineAPIImpl::SetCurrentProjectScenePath(const std::string& scenePath)
	{
		auto& projectManager = Vans::VansProjectManager::Get();
		if (!projectManager.IsProjectLoaded())
			return false;

		const std::string relativePath = projectManager.MakeRelativePath(scenePath);
		if (relativePath.empty())
			return false;

		projectManager.GetSceneManager().SetCurrentScene(relativePath);
		return true;
	}

	void EngineAPIImpl::ScanProjectAssets()
	{
		if (Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase())
			database->Scan(VansAssetOperationPolicy::Authoring());
	}

	EditorTextureHandle EngineAPIImpl::GetViewportTexture(ViewportId) const
	{
		return GetViewportPreview(0).texture;
	}

	RenderTexturePreview EngineAPIImpl::GetViewportPreview(ViewportId) const
	{
		RenderTexturePreview preview;
		preview.id = 0;
		preview.name = "Scene Viewport";

		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return preview;

		VansGraphics::VansVKImage& image = device->GetFSROutputImage();
		const VkImageView imageView = image.GetImageView();
		const VkSampler sampler = image.GetSampler();
		if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
			return preview;

		auto& cache = GetViewportPreviewCache();

		if (!cache.texture || cache.imageView != imageView || cache.sampler != sampler)
		{
			RetireEditorTexture(device, cache.texture);
			cache.imageView = imageView;
			cache.sampler = sampler;
			cache.texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
				sampler,
				imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		VkExtent3D extent = image.GetImageDimension();
		preview.texture = cache.texture;
		preview.width = extent.width;
		preview.height = extent.height;
		return preview;
	}

	FSRSettingsSnapshot EngineAPIImpl::GetFSRSettings() const
	{
		FSRSettingsSnapshot settings;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return settings;

		settings.mode = static_cast<FSRUpscaleMode>(device->GetFSRMode());
		settings.sharpness = device->GetFSRSharpness();
		settings.mipBias = device->GetUpscaleMipBias();
		settings.renderWidth = device->GetRenderWidth();
		settings.renderHeight = device->GetRenderHeight();
		const VkExtent3D outputExtent = device->GetFSROutputImage().GetImageDimension();
		settings.outputWidth = outputExtent.width;
		settings.outputHeight = outputExtent.height;
		return settings;
	}

	void EngineAPIImpl::SetFSRSettings(FSRUpscaleMode mode, float sharpness)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return;
		const VkExtent2D viewportExtent = device->GetRequestedSceneViewportExtent();
		device->RequestFSRConfig(
			static_cast<VansGraphics::VansFSRMode>(mode),
			viewportExtent.width,
			viewportExtent.height,
			sharpness);

		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
		{
			projectManager.GetProjectSettings().SetFSRSettings(
				static_cast<Vans::VansProjectFSRMode>(mode), sharpness);
			if (!projectManager.SaveProjectSettings())
			{
				VANS_LOG_WARN("[EngineAPI] Failed to persist FSR project settings");
			}
		}
	}

	CommandRecordingSettingsSnapshot EngineAPIImpl::GetCommandRecordingSettings() const
	{
		CommandRecordingSettingsSnapshot settings;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return settings;

		settings.parallelEnabled = device->IsParallelCommandRecordingEnabled();
		settings.frameContextRingEnabled = device->IsFrameContextRingEnabled();
		settings.framesInFlight = device->GetConfiguredFramesInFlight();
		return settings;
	}

	void EngineAPIImpl::SetCommandRecordingSettings(const CommandRecordingSettingsSnapshot& settings)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (device)
		{
			device->SetParallelCommandRecordingEnabled(settings.parallelEnabled);
			device->SetFrameContextRingEnabled(
				settings.frameContextRingEnabled,
				settings.framesInFlight);
		}

		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
		{
			projectManager.GetProjectSettings().SetCommandRecordingSettings(
				settings.parallelEnabled,
				settings.frameContextRingEnabled,
				settings.framesInFlight);
			if (!projectManager.SaveProjectSettings())
			{
				VANS_LOG_WARN("[EngineAPI] Failed to persist command recording project settings");
			}
		}
	}

	void EngineAPIImpl::SetSceneViewportExtent(std::uint32_t width, std::uint32_t height)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device || width == 0 || height == 0)
			return;
		device->RequestFSRConfig(
			device->GetFSRMode(),
			width,
			height,
			device->GetFSRSharpness());
	}

	std::vector<RenderTexturePreview> EngineAPIImpl::QueryRenderTexturePreviews(RenderTextureFilter filter) const
	{
		std::vector<RenderTexturePreview> previews;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (filter.category == "water")
		{
			RenderTexturePreview preview = BuildWaterTexturePreview(filter);
			if (preview.texture)
				previews.push_back(preview);
			return previews;
		}

		if (filter.category == "reflection_probe")
		{
			RenderTexturePreview preview = BuildReflectionProbePreview(filter);
			if (preview.texture)
				previews.push_back(preview);
			return previews;
		}

		auto* renderPassManager = VansGraphics::VansRenderPassManager::GetInstance();
		if (!renderPassManager)
			return previews;

		if (filter.category == "gbuffer")
		{
			previews.push_back(BuildImagePreview(device, 100, "GBuffer 0 (Albedo + Roughness)", renderPassManager->GetGbuffer0(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(device, 101, "GBuffer 1 (Metallic + AO + MatID)", renderPassManager->GetGbuffer1(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(device, 102, "GBuffer 2 (WorldPos + LinearDepth)", renderPassManager->GetGbuffer2(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(device, 103, "Normal", renderPassManager->GetNormal(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			return previews;
		}

		if (filter.category == "water_gbuffer")
		{
			previews.push_back(BuildImagePreview(device, 120, "WaterGBuf Normal", renderPassManager->GetWaterGBufNormal(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(device, 121, "WaterGBuf WorldPos+Depth (RGBA16F)", renderPassManager->GetWaterGBufLinearDepth(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			return previews;
		}

		if (filter.category == "render_debug")
		{
			previews.push_back(BuildImagePreview(device, 140, "Motion Vector", renderPassManager->GetMotionVector(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
			if (materialManager)
			{
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSR_RESULT))
					previews.push_back(BuildImagePreview(device, 141, "SSR Resolve Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSGI_RESULT))
					previews.push_back(BuildImagePreview(device, 142, "SSGI Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT))
					previews.push_back(BuildImagePreview(device, 143, "Fog Blend Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT))
					previews.push_back(BuildImagePreview(device, 144, "Screen Space Shadow", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
			}
			return previews;
		}

		if (filter.category == "hair_debug")
		{
			previews.push_back(BuildImagePreview(device, 160, "Hair Color", renderPassManager->GetHairColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(device, 161, "Hair Deep Opacity", renderPassManager->GetHairDeepOpacity(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			return previews;
		}

		return previews;
	}

	void EngineAPIImpl::RequestPunctualShadowDebugPreview()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* lightManager = scene ? scene->GetLightManager() : nullptr;
		if (lightManager != nullptr)
			lightManager->GetPunctualShadowManager().RequestDebugPreview();
	}

	PunctualShadowDebugSnapshot EngineAPIImpl::GetPunctualShadowDebugSnapshot() const
	{
		PunctualShadowDebugSnapshot snapshot;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		auto* lightManager = scene->GetLightManager();
		auto* materialManager = scene->GetMaterialManager();
		if (!lightManager || !materialManager)
			return snapshot;

		const VansGraphics::VansPunctualShadowDebugSnapshot runtime =
			lightManager->GetPunctualShadowManager().CaptureDebugSnapshot();
		snapshot.available = true;
		snapshot.frameIndex = runtime.frameIndex;
		snapshot.atlasSize = runtime.atlasSize;
		snapshot.atlasCount = runtime.atlasCount;
		snapshot.basePageSize = runtime.basePageSize;
		snapshot.gutter = runtime.gutter;
		snapshot.totalPages = lightManager->GetPunctualShadowManager().GetTotalAtlasPages();
		snapshot.usedPages = runtime.statistics.usedAtlasPages;
		snapshot.residentLights = runtime.statistics.residentLights;
		snapshot.residentViews = runtime.statistics.residentViews;
		snapshot.renderedViewsThisFrame = runtime.statistics.renderedViews;
		snapshot.fallbackLights = runtime.statistics.fallbackLights;
		snapshot.allocationFailures = runtime.statistics.allocationFailures;
		snapshot.dirtyTexelsThisFrame = runtime.statistics.dirtyTexels;

		const VansGraphics::VansScreenSpacePunctualShadowSettings screenSettings =
			materialManager->GetScreenSpacePunctualShadowSettings();
		snapshot.screenSpaceSettings.maxTraceDistance = screenSettings.maxTraceDistance;
		snapshot.screenSpaceSettings.thickness = screenSettings.thickness;
		snapshot.screenSpaceSettings.normalBias = screenSettings.normalBias;
		snapshot.screenSpaceSettings.maxSteps = screenSettings.maxSteps;
		snapshot.screenSpaceSettings.strength = screenSettings.strength;

		auto runtimeStateName = [](VansGraphics::VansShadowRuntimeState state) -> const char*
		{
			switch (state)
			{
			case VansGraphics::VansShadowRuntimeState::Disabled: return "Disabled";
			case VansGraphics::VansShadowRuntimeState::Candidate: return "Candidate";
			case VansGraphics::VansShadowRuntimeState::PendingAllocation: return "PendingAllocation";
			case VansGraphics::VansShadowRuntimeState::PendingRender: return "PendingRender";
			case VansGraphics::VansShadowRuntimeState::ResidentDirty: return "ResidentDirty";
			case VansGraphics::VansShadowRuntimeState::ResidentClean: return "ResidentClean";
			case VansGraphics::VansShadowRuntimeState::FallbackScreenSpace: return "FallbackScreenSpace";
			case VansGraphics::VansShadowRuntimeState::FallbackNone: return "FallbackNone";
			case VansGraphics::VansShadowRuntimeState::Evicting: return "Evicting";
			default: return "Unknown";
			}
		};

		auto policyName = [](VansGraphics::VansShadowPolicy policy) -> const char*
		{
			switch (policy)
			{
			case VansGraphics::VansShadowPolicy::Disabled: return "Disabled";
			case VansGraphics::VansShadowPolicy::Auto: return "Auto";
			case VansGraphics::VansShadowPolicy::Hero: return "Hero";
			case VansGraphics::VansShadowPolicy::DistanceDynamic: return "DistanceDynamic";
			default: return "Unknown";
			}
		};

		snapshot.lights.reserve(runtime.lights.size());
		for (const VansGraphics::VansPunctualShadowRuntimeDebug& source : runtime.lights)
		{
			PunctualShadowLightDebugSnapshot destination;
			destination.stableLightId = source.stableLightId;
			destination.gpuLightIndex = source.gpuLightIndex;
			destination.lightKind = source.lightType == VansGraphics::VansPunctualShadowLightType::Point
				? PunctualShadowLightKind::Point
				: (source.lightType == VansGraphics::VansPunctualShadowLightType::Spot
					? PunctualShadowLightKind::Spot
					: PunctualShadowLightKind::Rect);
			destination.runtimeState = runtimeStateName(source.runtimeState);
			destination.policy = policyName(source.policy);
			destination.priority = source.priority;
			destination.activeResolution = source.activeResolution;
			destination.targetResolution = source.targetResolution;
			destination.dirtyFaceMask = source.dirtyFaceMask;
			destination.validFaceMask = source.validFaceMask;
			destination.importance = source.importance;
			destination.coverage = source.coverage;
			destination.cameraDistance = source.cameraDistance;
			destination.distancePriority = source.distancePriority;
			destination.atlasWeight = source.atlasWeight;
			destination.residencyFrames = source.residencyFrames;
			destination.staleFrames = source.staleFrames;
			destination.lastRenderedFrame = source.lastRenderedFrame;
			destination.affectsFog = source.affectsVolumetricFog;
			destination.affectsGI = source.affectsGI;

			const bool hasValidAtlas = source.activeResolution != 0 &&
				source.validFaceMask != 0 && source.atlasWeight > 0.0f;
			if (!source.castShadows || source.policy == VansGraphics::VansShadowPolicy::Disabled)
				destination.displayMode = PunctualShadowDisplayMode::Disabled;
			else if (hasValidAtlas && source.atlasWeight < 0.999f)
				destination.displayMode = PunctualShadowDisplayMode::AtlasTransition;
			else if (hasValidAtlas && source.policy == VansGraphics::VansShadowPolicy::Hero)
				destination.displayMode = PunctualShadowDisplayMode::HeroAtlas;
			else if (hasValidAtlas)
				destination.displayMode = PunctualShadowDisplayMode::CachedAtlas;
			else if (source.runtimeState == VansGraphics::VansShadowRuntimeState::FallbackScreenSpace ||
				source.fallback == VansGraphics::VansShadowFallback::ScreenSpace)
				destination.displayMode = PunctualShadowDisplayMode::ScreenSpaceFallback;
			else
				destination.displayMode = PunctualShadowDisplayMode::Unshadowed;

			for (uint32_t face = 0; face < source.activeBlocks.size(); ++face)
			{
				const VansGraphics::VansShadowAtlasBlock& block = source.activeBlocks[face];
				if (!block.IsValid())
					continue;
				PunctualShadowAtlasViewSnapshot view;
				view.faceIndex = face;
				view.atlasIndex = block.atlasIndex;
				view.x = block.x;
				view.y = block.y;
				view.resolution = block.resolution;
				view.gutter = block.gutter;
				view.generation = block.generation;
				destination.atlasViews.push_back(view);
			}
			snapshot.lights.push_back(std::move(destination));
		}

		if (auto* texture = materialManager->GetRuntimeRenderTexture(
			VansGraphics::VansMaterialManager::RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW))
		{
			snapshot.atlasPreview = BuildImagePreview(
				device,
				190,
				"Punctual Shadow Atlas",
				texture->GetImage(),
				VK_IMAGE_LAYOUT_GENERAL);
		}

		if (auto* texture = materialManager->GetRuntimeRenderTexture(
			VansGraphics::VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT))
		{
			snapshot.screenSpacePreview = BuildImagePreview(
				device,
				191,
				"Screen-space Shadow Result",
				texture->GetImage(),
				VK_IMAGE_LAYOUT_GENERAL);
		}
		return snapshot;
	}

	void EngineAPIImpl::ApplyPunctualScreenSpaceShadowSettings(
		const PunctualScreenSpaceShadowSettingsSnapshot& settings)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
		if (!materialManager)
			return;

		VansGraphics::VansScreenSpacePunctualShadowSettings runtime;
		runtime.maxTraceDistance = settings.maxTraceDistance;
		runtime.thickness = settings.thickness;
		runtime.normalBias = settings.normalBias;
		runtime.maxSteps = settings.maxSteps;
		runtime.strength = settings.strength;
		materialManager->ApplyScreenSpacePunctualShadowSettings(runtime);
	}

	std::vector<ShaderProgramSourceSnapshot> EngineAPIImpl::QueryShaderProgramSources() const
	{
		std::vector<ShaderProgramSourceSnapshot> snapshots;
		VansGraphics::VansShaderManager::Get().ForEachShader(
			[&snapshots](const VansGraphics::VansShaderRecord& record)
			{
				if (!record.shader)
					return;

				ShaderProgramSourceSnapshot snapshot;
				snapshot.programId = record.entry.name;
				snapshot.sourceFolder = record.shader->GetShaderFolder();
				snapshot.rayTracing = record.entry.kind == VansGraphics::VansManagedShaderKind::RayTracing;
				for (const auto& [stage, moduleData] : record.shader->m_ShaderModuleDataMap)
				{
					(void)stage;
					if (moduleData.m_ShaderTextResourceFileName.empty() || moduleData.m_ShaderType.empty())
						continue;

					ShaderStageSourceSnapshot stageSnapshot;
					stageSnapshot.stage = moduleData.m_ShaderType;
					stageSnapshot.sourcePath = moduleData.m_ShaderTextResourceFileName;
					snapshot.stages.emplace_back(std::move(stageSnapshot));
				}

				if (!snapshot.programId.empty() && !snapshot.stages.empty())
					snapshots.emplace_back(std::move(snapshot));
			});

		std::sort(snapshots.begin(), snapshots.end(), [](const auto& lhs, const auto& rhs)
		{
			return lhs.programId < rhs.programId;
		});
		return snapshots;
	}

	ShaderCandidateApplyResult EngineAPIImpl::ApplyShaderCandidateAtRenderSafePoint(
		const ShaderCandidatePackage& package)
	{
		ShaderCandidateApplyResult result;
		result.programId = package.programId;
		result.sourceRevision = package.sourceRevision;
		if (package.programId.empty() || package.stages.empty())
		{
			result.error = "shader candidate is missing program id or stages";
			return result;
		}

		std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>> stageSpirv;
		for (const ShaderCompiledStagePackage& compiledStage : package.stages)
		{
			VkShaderStageFlagBits stage = static_cast<VkShaderStageFlagBits>(0);
			const auto graphicsIt = VansGraphics::m_ShaderTypeMap.find(compiledStage.stage);
			if (graphicsIt != VansGraphics::m_ShaderTypeMap.end())
			{
				stage = graphicsIt->second;
			}
			else
			{
				const auto rayTracingIt = VansGraphics::m_RayTracingShaderTypeMap.find(compiledStage.stage);
				if (rayTracingIt != VansGraphics::m_RayTracingShaderTypeMap.end())
					stage = rayTracingIt->second;
			}

			if (stage == 0 || compiledStage.spirv.empty())
			{
				result.error = "shader candidate contains an invalid stage: " + compiledStage.stage;
				return result;
			}
			if (!stageSpirv.emplace(stage, compiledStage.spirv).second)
			{
				result.error = "shader candidate contains duplicate stage: " + compiledStage.stage;
				return result;
			}
		}

		// Transitional Phase-1 safety barrier. The final implementation replaces
		// this stall with versioned pipelines and fence-bound retirement.
		auto* device = static_cast<VansGraphics::VansGraphicsDevice*>(m_Device);
		if (!device || !device->WaitForIdle())
		{
			result.error = "render device is unavailable or failed to become idle";
			return result;
		}

		result.applied = VansGraphics::VansShaderManager::Get().ApplyCompiledShaderCandidate(
			package.programId,
			stageSpirv,
			result.error);
		return result;
	}

	RenderBackendDiagnostics EngineAPIImpl::GetRenderBackendDiagnostics(bool includeRenderGraphSummary) const
	{
		RenderBackendDiagnostics diagnostics{};

		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return diagnostics;

		const VansGraphics::VansFrameContext& frameContext = device->GetCurrentFrameContext();
		diagnostics.frameSubmitSucceeded = frameContext.frameSubmitSucceeded;
		diagnostics.shadowSubmitted = frameContext.shadowSubmitted;
		diagnostics.gbufferSubmitted = frameContext.gbufferSubmitted;
		diagnostics.asyncComputeSubmitted = frameContext.asyncComputeSubmitted;
		diagnostics.frameNumber = frameContext.frameNumber;
		diagnostics.swapchainImageIndex = frameContext.swapchainImageIndex;
		diagnostics.deferredDeleteLastFlushCount = frameContext.lastDeferredDeleteFlushCount;
		diagnostics.deferredDeletePendingCount = frameContext.pendingDeferredDeleteCount;

		const VansGraphics::VansDescriptorPoolDiagnostics descriptorDiagnostics =
			VansGraphics::VansVKDescriptorManager::GetInstance()->GetDiagnostics();
		diagnostics.descriptorStandardPoolCount = descriptorDiagnostics.standardPoolCount;
		diagnostics.descriptorUpdateAfterBindPoolCount = descriptorDiagnostics.updateAfterBindPoolCount;
		diagnostics.descriptorTrackedSetCount = descriptorDiagnostics.trackedDescriptorSetCount;
		diagnostics.descriptorUpdateAfterBindLayoutCount = descriptorDiagnostics.updateAfterBindLayoutCount;
		diagnostics.descriptorGlobalPersistentSetCount = descriptorDiagnostics.globalPersistentSetCount;
		diagnostics.descriptorScenePersistentSetCount = descriptorDiagnostics.scenePersistentSetCount;
		diagnostics.descriptorFrameTransientSetCount = descriptorDiagnostics.frameTransientSetCount;
		diagnostics.descriptorPassPersistentSetCount = descriptorDiagnostics.passPersistentSetCount;
		diagnostics.descriptorUploadScratchSetCount = descriptorDiagnostics.uploadScratchSetCount;
		diagnostics.descriptorRayTracingPersistentSetCount = descriptorDiagnostics.rayTracingPersistentSetCount;
		diagnostics.renderNodeDescriptorValidationFailureCount =
			VansGraphics::VansRenderNode::GetDescriptorValidationFailureCount();
		diagnostics.textureUploadFailureCount =
			VansGraphics::VansTexture::GetUploadFailureCount();

		const VansGraphics::VansRenderGraphDiagnosticsSnapshot& graphDiagnostics =
			device->GetCurrentRenderGraphDiagnostics();
		diagnostics.available = graphDiagnostics.available;
		diagnostics.compiledGraphValid = graphDiagnostics.compiledGraphValid;
		diagnostics.featureAuditPassed = graphDiagnostics.featureAuditPassed;
		diagnostics.framePlanPassCount = graphDiagnostics.framePlanPassCount;
		diagnostics.compiledResourceCount = graphDiagnostics.compiledResourceCount;
		diagnostics.barrierDependencyCount = graphDiagnostics.barrierDependencyCount;
		diagnostics.renderGraphTopologyRevision = graphDiagnostics.topologyRevision;
		diagnostics.renderGraphTopologyHash = graphDiagnostics.topologyHash;
		diagnostics.renderGraphCompiledFrameNumber = graphDiagnostics.compiledFrameNumber;
		if (includeRenderGraphSummary && diagnostics.available)
		{
			diagnostics.renderGraphSummary = device->GetCurrentRenderGraphDebugSummary();
		}
		return diagnostics;
	}

	PipelineRegistryStatsSnapshot EngineAPIImpl::GetPipelineRegistryStats() const
	{
		const VansGraphics::VansPipelineRegistryStats runtimeStats =
			VansGraphics::VansPipelineRegistry::Get().GetStats();

		auto convertMap = [](const VansGraphics::VansPipelineRegistryMapStats& stats)
		{
			PipelineRegistryMapStatsSnapshot snapshot{};
			snapshot.bucketCount = static_cast<std::uint64_t>(stats.bucketCount);
			snapshot.activeCount = static_cast<std::uint64_t>(stats.activeCount);
			snapshot.expiredCount = static_cast<std::uint64_t>(stats.expiredCount);
			return snapshot;
		};

		PipelineRegistryStatsSnapshot snapshot{};
		snapshot.graphics = convertMap(runtimeStats.graphics);
		snapshot.compute = convertMap(runtimeStats.compute);
		snapshot.rayTracing = convertMap(runtimeStats.rayTracing);
		snapshot.totalActiveCount = static_cast<std::uint64_t>(runtimeStats.GetTotalActiveCount());
		snapshot.totalExpiredCount = static_cast<std::uint64_t>(runtimeStats.GetTotalExpiredCount());
		return snapshot;
	}

	RenderDocStatusSnapshot EngineAPIImpl::GetRenderDocStatus() const
	{
		const VansGraphics::VansRenderDocStatus runtimeStatus =
			VansGraphics::VansRenderDocCapture::Get().QueryStatus();

		RenderDocStatusSnapshot snapshot{};
		snapshot.available = runtimeStatus.available;
		snapshot.targetControlConnected = runtimeStatus.targetControlConnected;
		snapshot.frameCapturing = runtimeStatus.frameCapturing;
		snapshot.apiValidationEnabled = runtimeStatus.apiValidationEnabled;
		snapshot.referenceAllResources = runtimeStatus.referenceAllResources;
		snapshot.apiMajor = runtimeStatus.apiMajor;
		snapshot.apiMinor = runtimeStatus.apiMinor;
		snapshot.apiPatch = runtimeStatus.apiPatch;
		snapshot.captureCount = runtimeStatus.captureCount;
		snapshot.capturePathTemplate = runtimeStatus.capturePathTemplate;
		snapshot.lastCapturePath = runtimeStatus.lastCapturePath;
		snapshot.message = runtimeStatus.message;
		return snapshot;
	}

	void EngineAPIImpl::SetRenderDocAPIValidationEnabled(bool enabled)
	{
		VansGraphics::VansRenderDocCapture::Get().SetAPIValidationEnabled(enabled);
	}

	void EngineAPIImpl::SetRenderDocReferenceAllResources(bool enabled)
	{
		VansGraphics::VansRenderDocCapture::Get().SetReferenceAllResources(enabled);
	}

	void EngineAPIImpl::CaptureNextRenderDocFrame()
	{
		VansGraphics::VansRenderDocCapture::Get().CaptureNextFrame();
	}

	void EngineAPIImpl::OpenRenderDocUI()
	{
		VansGraphics::VansRenderDocCapture::Get().OpenReplayUI();
	}

	UIDocumentOpenResult EngineAPIImpl::OpenUIDocument(const std::string& path)
	{
		UIDocumentOpenResult result{};
		result.sourcePath = path;
		if (path.empty())
		{
			result.error = "UI document path is empty.";
			return result;
		}
		if (!VansRuntime::VansUISystem::Get().IsInitialized())
		{
			result.error = "Runtime UI system is not initialized.";
			return result;
		}

		std::shared_ptr<VansRuntime::VansUIDocument> document;
		VansRuntime::VansUIHandleId screenHandle = VansRuntime::kInvalidUIHandle;
		const std::filesystem::path sourcePath(path);
		if (sourcePath.extension() == ".json" && path.find(".vui.") != std::string::npos)
		{
			auto screen = VansRuntime::VansUISystem::Get().LoadScreen(path);
			if (screen)
			{
				screenHandle = screen->GetHandleId();
				document = screen->GetDocument();
			}
		}
		else
		{
			document = VansRuntime::VansUISystem::Get().LoadDocument(path);
		}
		if (!document)
		{
			result.error = "Failed to load UI document: " + path;
			return result;
		}

		document->Show();
		result.documentId = m_NextUIDocumentId++;
		result.sourcePath = path;
		result.success = true;
		m_UIDocuments[result.documentId] = std::move(document);
		m_UIDocumentSourcePaths[result.documentId] = path;
		if (screenHandle != VansRuntime::kInvalidUIHandle)
			m_UIScreenPreviewHandles[result.documentId] = screenHandle;
		return result;
	}

	void EngineAPIImpl::CloseUIDocument(UIDocumentId documentId)
	{
		DestroyUIPreviewsForDocument(documentId);

		const auto it = m_UIDocuments.find(documentId);
		if (it == m_UIDocuments.end())
			return;
		const auto screenIt = m_UIScreenPreviewHandles.find(documentId);
		if (screenIt != m_UIScreenPreviewHandles.end())
		{
			VansRuntime::VansUISystem::Get().CloseScreen(screenIt->second);
			m_UIScreenPreviewHandles.erase(screenIt);
		}
		else if (it->second)
		{
			VansRuntime::VansUISystem::Get().UnloadDocument(it->second);
		}
		m_UIDocuments.erase(it);
		m_UIDocumentSourcePaths.erase(documentId);
	}

	void EngineAPIImpl::SetUIDocumentVisible(UIDocumentId documentId, bool visible)
	{
		const auto it = m_UIDocuments.find(documentId);
		if (it == m_UIDocuments.end() || !it->second)
			return;
		if (visible)
			it->second->Show();
		else
			it->second->Hide();
	}

	UIDocumentSnapshot EngineAPIImpl::GetUIDocumentSnapshot(UIDocumentId documentId) const
	{
		UIDocumentSnapshot snapshot{};
		const auto it = m_UIDocuments.find(documentId);
		if (it == m_UIDocuments.end() || !it->second)
			return snapshot;

		snapshot.valid = true;
		snapshot.documentId = documentId;
		const auto sourceIt = m_UIDocumentSourcePaths.find(documentId);
		snapshot.sourcePath = sourceIt != m_UIDocumentSourcePaths.end()
			? sourceIt->second
			: it->second->GetSourcePath();
		snapshot.visible = it->second->IsVisible();
		if (snapshot.sourcePath.find(".vui.") != std::string::npos)
		{
			VansRuntime::VansUIScreenConfig config;
			std::vector<std::string> diagnostics;
			if (ReadEditorUIScreenConfig(snapshot.sourcePath, config, diagnostics))
			{
				snapshot.assetKind = "Screen";
				snapshot.name = config.name;
				snapshot.xamlPath = config.xamlPath;
				snapshot.layer = UIScreenLayerToString(config.layer);
				snapshot.zOrder = config.zOrder;
				snapshot.themes = config.themes;
				snapshot.tokens = config.tokens;
				snapshot.localization = config.localization;
				snapshot.dependencies = config.dependencies;
				snapshot.performanceBudget.maxDrawCalls = config.performanceBudget.maxDrawCalls;
				snapshot.performanceBudget.maxTextureMemoryMB = config.performanceBudget.maxTextureMemoryMB;
				snapshot.performanceBudget.maxLayoutMs = config.performanceBudget.maxLayoutMs;
				snapshot.performanceBudget.maxBindingUpdatesPerFrame =
					config.performanceBudget.maxBindingUpdatesPerFrame;
				snapshot.performanceBudget.maxAnimations = config.performanceBudget.maxAnimations;
				for (const VansRuntime::VansUIScreenEventBindingConfig& event : config.events)
				{
					snapshot.events.push_back(UIScreenEventSummary{
						event.source,
						event.eventName,
						event.action
					});
				}
			}
		}
		return snapshot;
	}

	UIDiagnosticsSnapshot EngineAPIImpl::GetUIDiagnostics(UIDocumentId documentId) const
	{
		UIDiagnosticsSnapshot diagnostics{};
		diagnostics.available = true;

		const UIDocumentSnapshot snapshot = GetUIDocumentSnapshot(documentId);
		if (!snapshot.valid)
		{
			diagnostics.messages.push_back("No UI document is loaded.");
			return diagnostics;
		}

		diagnostics.messages.push_back("Document: " + snapshot.sourcePath);
		diagnostics.messages.push_back(snapshot.visible ? "Visibility: visible" : "Visibility: hidden");
		diagnostics.messages.push_back("Preview texture: offscreen render target available.");
		AppendScreenConfigDiagnostics(snapshot.sourcePath, diagnostics);
		return diagnostics;
	}

	void EngineAPIImpl::DestroyUIPreviewResource(UIPreviewGpuResource& resource)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		VkDevice logicalDevice = device ? device->GetLogicDevice() : VK_NULL_HANDLE;

		RetireEditorTexture(device, resource.texture);
		resource.texture = nullptr;

		if (logicalDevice != VK_NULL_HANDLE)
		{
			if (resource.framebuffer != VK_NULL_HANDLE)
			{
				VansGraphics::vkDestroyFramebuffer(logicalDevice, resource.framebuffer, nullptr);
				resource.framebuffer = VK_NULL_HANDLE;
			}
			if (resource.renderPass != VK_NULL_HANDLE)
			{
				VansGraphics::vkDestroyRenderPass(logicalDevice, resource.renderPass, nullptr);
				resource.renderPass = VK_NULL_HANDLE;
			}
			resource.colorImage.DestroyVulkanImage(logicalDevice);
		}
	}

	void EngineAPIImpl::DestroyUIPreviewsForDocument(UIDocumentId documentId)
	{
		for (auto it = m_UIPreviewResources.begin(); it != m_UIPreviewResources.end();)
		{
			if (it->second.documentId == documentId)
			{
				DestroyUIPreviewResource(it->second);
				it = m_UIPreviewResources.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void EngineAPIImpl::DestroyAllUIPreviewResources()
	{
		for (auto& preview : m_UIPreviewResources)
			DestroyUIPreviewResource(preview.second);
		m_UIPreviewResources.clear();
	}

	void EngineAPIImpl::CloseAllUIDocuments()
	{
		std::vector<UIDocumentId> documentIds;
		documentIds.reserve(m_UIDocuments.size());
		for (const auto& document : m_UIDocuments)
			documentIds.push_back(document.first);

		for (UIDocumentId documentId : documentIds)
			CloseUIDocument(documentId);

		DestroyAllUIPreviewResources();
		m_UIDocuments.clear();
		m_UIDocumentSourcePaths.clear();
		m_UIScreenPreviewHandles.clear();
	}

	UIPreviewResult EngineAPIImpl::RequestUIPreview(const UIPreviewRequest& request)
	{
		UIPreviewResult result{};
		const UIDocumentSnapshot snapshot = GetUIDocumentSnapshot(request.documentId);
		if (!snapshot.valid)
		{
			result.message = "No valid UI document for preview request.";
			return result;
		}

		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
		{
			result.message = "Runtime render device is not available.";
			return result;
		}

		const auto docIt = m_UIDocuments.find(request.documentId);
		if (docIt == m_UIDocuments.end() || !docIt->second)
		{
			result.message = "UI document handle is no longer valid.";
			return result;
		}

		const std::uint32_t width = std::max<std::uint32_t>(1, request.width);
		const std::uint32_t height = std::max<std::uint32_t>(1, request.height);
		const VkFormat previewFormat = VK_FORMAT_R8G8B8A8_UNORM;

		UIPreviewGpuResource resource{};
		resource.documentId = request.documentId;
		resource.width = width;
		resource.height = height;

		VkDevice logicalDevice = device->GetLogicDevice();
		VkExtent3D extent{ width, height, 1 };
		if (!resource.colorImage.CreateVulkanImage(
			logicalDevice,
			extent,
			previewFormat,
			1,
			1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			false,
			false,
			true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		{
			result.message = "Failed to create UI preview color target.";
			return result;
		}

		std::string error;
		if (!CreateUIPreviewRenderPass(logicalDevice, previewFormat, resource.renderPass, error))
		{
			DestroyUIPreviewResource(resource);
			result.message = error;
			return result;
		}

		VkImageView attachment = resource.colorImage.GetImageView();
		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = resource.renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &attachment;
		framebufferInfo.width = width;
		framebufferInfo.height = height;
		framebufferInfo.layers = 1;
		if (VansGraphics::vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &resource.framebuffer) != VK_SUCCESS)
		{
			DestroyUIPreviewResource(resource);
			result.message = "Failed to create UI preview framebuffer.";
			return result;
		}

		auto& commandBuffer = device->GetImmediateGraphicsCommandBuffer();
		commandBuffer.ResetCommandBuffer(false);
		if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			DestroyUIPreviewResource(resource);
			result.message = "Failed to begin UI preview command buffer.";
			return result;
		}

		VkCommandBuffer rawCmd = commandBuffer.GetVKCommandBuffer();
		RecordPreviewImageBarrier(
			rawCmd,
			resource.colorImage.GetImage(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		resource.colorImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		docIt->second->SetSize(width, height);
		VansRuntime::VansUISystem::Get().PrepareDocumentPreview(
			docIt->second,
			rawCmd,
			0.0);

		VkClearValue clearValue{};
		clearValue.color = { { 0.02f, 0.02f, 0.035f, 1.0f } };
		VkRenderPassBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		beginInfo.renderPass = resource.renderPass;
		beginInfo.framebuffer = resource.framebuffer;
		beginInfo.renderArea.offset = { 0, 0 };
		beginInfo.renderArea.extent = { width, height };
		beginInfo.clearValueCount = 1;
		beginInfo.pClearValues = &clearValue;
		commandBuffer.BeginRenderPass(beginInfo);
		VansRuntime::VansUISystem::Get().RenderDocumentPreviewPass(
			docIt->second,
			resource.renderPass,
			1);
		commandBuffer.EndRenderPass();
		resource.colorImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		if (!commandBuffer.EndCommandBufferRecord())
		{
			DestroyUIPreviewResource(resource);
			result.message = "Failed to record UI preview command buffer.";
			return result;
		}

		VkQueue queue = device->GetGraphicsQueue();
		const bool submitted = VansGraphics::VansVKCommandBuffer::SubmitCommands(
			queue,
			logicalDevice,
			{ rawCmd },
			{},
			{},
			commandBuffer.m_CommandBufferFinishSubmitFence);
		if (!submitted)
		{
			DestroyUIPreviewResource(resource);
			result.message = "Failed to submit UI preview command buffer.";
			return result;
		}

		resource.texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
			resource.colorImage.GetSampler(),
			resource.colorImage.GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (!resource.texture)
		{
			DestroyUIPreviewResource(resource);
			result.message = "Failed to register UI preview texture for editor.";
			return result;
		}

		result.previewId = m_NextUIPreviewId++;
		result.texture = resource.texture;
		result.success = true;
		result.message = "Offscreen UI preview rendered.";
		m_UIPreviewResources.emplace(result.previewId, std::move(resource));
		return result;
	}

	EditorTextureHandle EngineAPIImpl::GetUIPreviewTexture(UIPreviewId id) const
	{
		const auto it = m_UIPreviewResources.find(id);
		return it != m_UIPreviewResources.end() ? it->second.texture : nullptr;
	}

	void EngineAPIImpl::RebuildReflectionProbeResources()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device)
			return;

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes)
			return;

		probes->CreateGPUResources(*device, device->GetImmediateGraphicsCommandBuffer());
		probes->UpdateGlobalDescriptors(scene->GetGlobalDescriptorSet());
	}

	void EngineAPIImpl::BakeQueuedReflectionProbesNow()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device)
			return;

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes)
			return;

		probes->BakeQueuedProbesNow(*scene, *device, device->GetImmediateGraphicsCommandBuffer());
	}

	RenderTexturePreview EngineAPIImpl::BuildReflectionProbePreview(RenderTextureFilter filter) const
	{
		RenderTexturePreview preview;
		preview.name = "Reflection Probe";

		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene)
			return preview;

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes || !probes->GetSpecularArray())
			return preview;

		const uint32_t mipCount = std::max(1u, probes->GetMipCount());
		const uint32_t face = std::min(filter.face, 5u);
		const uint32_t mip = static_cast<uint32_t>(
			std::round(std::clamp(filter.roughness, 0.0f, 1.0f) * float(mipCount - 1)));

		VkImageView imageView = probes->GetPreviewFaceView(
			static_cast<size_t>(filter.probeIndex),
			face,
			mip);
		if (imageView == VK_NULL_HANDLE)
			return preview;

		auto& cache = GetReflectionProbePreviewCache();

		VansGraphics::VansTexture* texture = probes->GetSpecularArray();
		VkSampler sampler = texture->GetImage().GetSampler();
		if (!cache.texture || cache.sourceTexture != texture || cache.imageView != imageView || cache.sampler != sampler)
		{
			RetireEditorTexture(device, cache.texture);
			cache.sourceTexture = texture;
			cache.imageView = imageView;
			cache.sampler = sampler;
			cache.texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
				sampler,
				imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		const float mipScale = 1.0f / float(1u << mip);
		const uint32_t textureSize = static_cast<uint32_t>(
			std::max(1.0f, float(probes->GetArrayResolution()) * mipScale));
		preview.id = static_cast<RenderTextureId>(filter.probeIndex + 1u);
		preview.texture = cache.texture;
		preview.width = textureSize;
		preview.height = textureSize;
		return preview;
	}

	ReflectionProbeSettingsSnapshot EngineAPIImpl::GetReflectionProbeSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return {};

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes)
			return {};

		return ToReflectionProbeSettings(*probes);
	}

	void EngineAPIImpl::ApplyReflectionProbeSettings(const ReflectionProbeSettingsSnapshot& settings)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !settings.available)
			return;

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes)
			return;

		ApplyReflectionProbeSettingsToSystem(settings, *probes);
	}

	GIInspectorSettingsSnapshot EngineAPIImpl::GetGISettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return {};

		const VansGraphics::VansGISettings& gi = scene->GetGISettings();
		const glm::vec3 volumeSize = glm::vec3(gi.gridDimensions) * gi.probeSpacingAxes;
		const glm::vec3 volumeMin = gi.regionCenter - volumeSize * 0.5f;
		const glm::vec3 volumeMax = volumeMin + volumeSize;

		GIInspectorSettingsSnapshot settings;
		settings.available = true;
		settings.regionCenter = ToEditorVec3(gi.regionCenter);
		settings.volumeMin = ToEditorVec3(volumeMin);
		settings.volumeMax = ToEditorVec3(volumeMax);
		settings.gridDimensions = {
			static_cast<float>(gi.gridDimensions.x),
			static_cast<float>(gi.gridDimensions.y),
			static_cast<float>(gi.gridDimensions.z) };
		settings.probeSpacingAxes = ToEditorVec3(gi.probeSpacingAxes);
		settings.normalBias = gi.normalBias;
		settings.maxRayDistance = gi.maxRayDistance;
		settings.volumeFadeDistance = gi.volumeFadeDistance;
		settings.raysPerProbe = gi.raysPerProbe;
		settings.spatialUpdateDivisor = gi.spatialUpdateDivisor;
		settings.directionUpdateSlices = gi.directionUpdateSlices;
		settings.environmentIntensity = gi.environmentIntensity;
		settings.maxIndirectRadiance = gi.maxIndirectRadiance;
		settings.maxSHL0 = gi.maxSHL0;
		settings.showProbeGizmos = gi.showProbeGizmos;
		settings.showProbeVolume = gi.showProbeVolume;
		settings.debugView = static_cast<int>(gi.debugView);
		settings.debugExposure = gi.debugExposure;
		settings.gizmoStride = gi.gizmoStride;
		settings.totalProbeCount = gi.gridDimensions.x * gi.gridDimensions.y * gi.gridDimensions.z;
		return settings;
	}

	void EngineAPIImpl::ApplyGISettings(const GIInspectorSettingsSnapshot& settings)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !settings.available)
			return;

		VansGraphics::VansGISettings gi = scene->GetGISettings();
		auto clampDimension = [](float value, std::uint32_t fallback) {
			if (!std::isfinite(value) || value < 1.0f)
				return fallback;
			return std::clamp(static_cast<std::uint32_t>(std::lround(value)), 1u, 256u);
		};
		auto clampSpacing = [](float value, float fallback) {
			return std::isfinite(value) && value > 0.0f
				? std::max(value, 0.001f)
				: std::max(fallback, 0.001f);
		};
		gi.gridDimensions = glm::uvec3(
			clampDimension(settings.gridDimensions.x, gi.gridDimensions.x),
			clampDimension(settings.gridDimensions.y, gi.gridDimensions.y),
			clampDimension(settings.gridDimensions.z, gi.gridDimensions.z));
		gi.probeSpacingAxes = glm::vec3(
			clampSpacing(settings.probeSpacingAxes.x, gi.probeSpacingAxes.x),
			clampSpacing(settings.probeSpacingAxes.y, gi.probeSpacingAxes.y),
			clampSpacing(settings.probeSpacingAxes.z, gi.probeSpacingAxes.z));
		gi.regionCenter = glm::vec3(settings.regionCenter.x, settings.regionCenter.y, settings.regionCenter.z);
		gi.raysPerProbe = std::clamp(settings.raysPerProbe, 1u, 4096u);
		gi.spatialUpdateDivisor = std::clamp(
			settings.spatialUpdateDivisor, 1u,
			std::min({ gi.gridDimensions.x, gi.gridDimensions.y, gi.gridDimensions.z }));
		gi.directionUpdateSlices = std::clamp(settings.directionUpdateSlices, 1u, gi.raysPerProbe);
		gi.maxRayDistance = std::max(settings.maxRayDistance, 0.001f);
		gi.normalBias = std::max(settings.normalBias, 0.0f);
		gi.environmentIntensity = std::max(settings.environmentIntensity, 0.0f);
		gi.maxIndirectRadiance = std::max(settings.maxIndirectRadiance, 0.0f);
		gi.maxSHL0 = std::max(settings.maxSHL0, 0.0f);
		gi.volumeFadeDistance = std::max(settings.volumeFadeDistance, 0.0f);
		gi.showProbeGizmos = settings.showProbeGizmos;
		gi.showProbeVolume = settings.showProbeVolume;
		gi.debugView = static_cast<std::uint32_t>(std::max(settings.debugView, 0));
		gi.debugExposure = std::max(settings.debugExposure, 0.001f);
		gi.gizmoStride = std::clamp(settings.gizmoStride, 1u,
			std::max({ 1u, gi.gridDimensions.x, gi.gridDimensions.y, gi.gridDimensions.z }));
		scene->SetGISettings(gi);

		auto* materialManager = scene->GetMaterialManager();
		if (!device || !materialManager || materialManager->m_SSGICBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return;

		const glm::vec3 volumeSize = glm::vec3(gi.gridDimensions) * gi.probeSpacingAxes;
		const glm::vec3 volumeMin = gi.regionCenter - volumeSize * 0.5f;
		VansGraphics::SSGIParamsGPU data{};
		data.screenSize = glm::vec4(
			static_cast<float>(device->GetRenderWidth()),
			static_cast<float>(device->GetRenderHeight()),
			1.0f / std::max(1u, device->GetRenderWidth()),
			1.0f / std::max(1u, device->GetRenderHeight()));
		data.giVolumeMin = glm::vec4(volumeMin, 0.0f);
		data.giVolumeSizeAndBias = glm::vec4(volumeSize, gi.normalBias);
		data.traceParams = glm::vec4(gi.maxRayDistance, 0.75f, gi.volumeFadeDistance, 0.0f);
		materialManager->m_SSGICBBuffer.SetBufferData(&data, 0, sizeof(data));
	}

	GIProbeDebugSnapshot EngineAPIImpl::CaptureGIProbeDebugSnapshot(std::uint32_t stride, float exposure)
	{
		m_GIProbeDebugSnapshot = {};
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device)
		{
			m_GIProbeDebugSnapshot.status = "Runtime scene or device is not available.";
			return m_GIProbeDebugSnapshot;
		}

		auto* materialManager = scene->GetMaterialManager();
		if (!materialManager)
		{
			m_GIProbeDebugSnapshot.status = "Material manager is not available.";
			return m_GIProbeDebugSnapshot;
		}

		VansGraphics::VansTexture* shR = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SH_R_RESULT);
		VansGraphics::VansTexture* shG = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SH_G_RESULT);
		VansGraphics::VansTexture* shB = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SH_B_RESULT);
		if (!shR || !shG || !shB)
		{
			m_GIProbeDebugSnapshot.status = "GI SH textures are not created yet.";
			return m_GIProbeDebugSnapshot;
		}

		const VansGraphics::VansGISettings& gi = scene->GetGISettings();
		const glm::uvec3 gridDimensions = glm::max(gi.gridDimensions, glm::uvec3(1u));
		const std::uint32_t maxGridDimension = std::max({ gridDimensions.x, gridDimensions.y, gridDimensions.z });
		stride = std::clamp(stride, 1u, maxGridDimension);
		exposure = std::max(exposure, 0.001f);

		struct GISampleCoord
		{
			std::uint32_t x = 0;
			std::uint32_t y = 0;
			std::uint32_t z = 0;
		};
		std::vector<GISampleCoord> sampleCoords;
		for (std::uint32_t z = 0; z < gridDimensions.z; z += stride)
		for (std::uint32_t y = 0; y < gridDimensions.y; y += stride)
		for (std::uint32_t x = 0; x < gridDimensions.x; x += stride)
			sampleCoords.push_back({ x, y, z });

		const VkDeviceSize sampleBytes = 4u * sizeof(float);
		const VkDeviceSize readbackBytes = std::max<VkDeviceSize>(
			1,
			static_cast<VkDeviceSize>(sampleCoords.size()) * sampleBytes);
		VansGraphics::VansVKBuffer readR;
		VansGraphics::VansVKBuffer readG;
		VansGraphics::VansVKBuffer readB;
		auto destroyReadbacks = [&]()
		{
			if (readR.IsMapped()) readR.Unmap();
			if (readG.IsMapped()) readG.Unmap();
			if (readB.IsMapped()) readB.Unmap();
			readR.DestroyVulkanBuffer(device->GetLogicDevice());
			readG.DestroyVulkanBuffer(device->GetLogicDevice());
			readB.DestroyVulkanBuffer(device->GetLogicDevice());
		};

		const VkBufferUsageFlags readUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		const VkMemoryPropertyFlags readMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		if (!readR.CreatVulkanBuffer(device->GetLogicDevice(), readbackBytes, VK_FORMAT_R32_SFLOAT, readUsage, readMemory) ||
			!readG.CreatVulkanBuffer(device->GetLogicDevice(), readbackBytes, VK_FORMAT_R32_SFLOAT, readUsage, readMemory) ||
			!readB.CreatVulkanBuffer(device->GetLogicDevice(), readbackBytes, VK_FORMAT_R32_SFLOAT, readUsage, readMemory) ||
			!readR.PersistentMap() || !readG.PersistentMap() || !readB.PersistentMap())
		{
			m_GIProbeDebugSnapshot.status = "Failed to allocate GI SH readback buffers.";
			destroyReadbacks();
			return m_GIProbeDebugSnapshot;
		}

		std::vector<VkBufferImageCopy> regions;
		regions.reserve(sampleCoords.size());
		for (std::size_t i = 0; i < sampleCoords.size(); ++i)
		{
			const GISampleCoord& sample = sampleCoords[i];
			VkBufferImageCopy region{};
			region.bufferOffset = static_cast<VkDeviceSize>(i) * sampleBytes;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageOffset = {
				static_cast<int32_t>(sample.x),
				static_cast<int32_t>(sample.y),
				static_cast<int32_t>(sample.z) };
			region.imageExtent = { 1, 1, 1 };
			regions.push_back(region);
		}

		VansGraphics::VansVKCommandBuffer& commandBuffer = device->GetImmediateGraphicsCommandBuffer();
		if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			m_GIProbeDebugSnapshot.status = "Failed to begin GI SH readback command buffer.";
			destroyReadbacks();
			return m_GIProbeDebugSnapshot;
		}

		auto transition = [&](VansGraphics::VansTexture* texture, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
			VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
		{
			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = texture->GetImage().GetImage();
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			commandBuffer.PipelineBarrier(srcStage, dstStage, {}, {}, { barrier });
			texture->GetImage().SetTrackedImageLayout(newLayout);
		};

		const VkAccessFlags readbackSourceAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		const VkPipelineStageFlags readbackSourceStage =
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		transition(shR, readbackSourceAccess, VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			readbackSourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT);
		transition(shG, readbackSourceAccess, VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			readbackSourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT);
		transition(shB, readbackSourceAccess, VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			readbackSourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

		VansGraphics::VansVKMemoryManager::CopyImageToBuffer(commandBuffer, shR->GetImage(), readR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, regions);
		VansGraphics::VansVKMemoryManager::CopyImageToBuffer(commandBuffer, shG->GetImage(), readG, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, regions);
		VansGraphics::VansVKMemoryManager::CopyImageToBuffer(commandBuffer, shB->GetImage(), readB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, regions);

		transition(shR, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		transition(shG, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		transition(shB, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		commandBuffer.EndCommandBufferRecord();
		const bool submitted = VansGraphics::VansVKCommandBuffer::SubmitCommands(
			device->GetGraphicsQueue(),
			device->GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() },
			{}, {},
			commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false);
		if (!submitted)
		{
			m_GIProbeDebugSnapshot.status = "Failed to submit GI SH readback command buffer.";
			destroyReadbacks();
			return m_GIProbeDebugSnapshot;
		}

		readR.InvalidateMappedRange(0, readbackBytes);
		readG.InvalidateMappedRange(0, readbackBytes);
		readB.InvalidateMappedRange(0, readbackBytes);

		const float* rData = static_cast<const float*>(readR.GetMappedPtr());
		const float* gData = static_cast<const float*>(readG.GetMappedPtr());
		const float* bData = static_cast<const float*>(readB.GetMappedPtr());
		if (!rData || !gData || !bData)
		{
			m_GIProbeDebugSnapshot.status = "GI SH readback buffers are not mapped.";
			destroyReadbacks();
			return m_GIProbeDebugSnapshot;
		}

		const glm::vec3 volumeSize = glm::vec3(gridDimensions) * gi.probeSpacingAxes;
		const glm::vec3 volumeMin = gi.regionCenter - volumeSize * 0.5f;
		m_GIProbeDebugSnapshot.available = true;
		m_GIProbeDebugSnapshot.gridDimensions = {
			static_cast<float>(gridDimensions.x),
			static_cast<float>(gridDimensions.y),
			static_cast<float>(gridDimensions.z) };
		m_GIProbeDebugSnapshot.stride = stride;
		m_GIProbeDebugSnapshot.exposure = exposure;
		m_GIProbeDebugSnapshot.status = "Captured GI probe SH.";
		m_GIProbeDebugSnapshot.probes.clear();

		m_GIProbeDebugSnapshot.probes.reserve(sampleCoords.size());
		for (std::size_t i = 0; i < sampleCoords.size(); ++i)
		{
			const GISampleCoord& sample = sampleCoords[i];
			const std::size_t texel = i * 4u;
			const glm::vec3 l0(std::max(rData[texel + 0], 0.0f), std::max(gData[texel + 0], 0.0f), std::max(bData[texel + 0], 0.0f));
			const glm::vec3 l1(
				std::abs(rData[texel + 1]) + std::abs(rData[texel + 2]) + std::abs(rData[texel + 3]),
				std::abs(gData[texel + 1]) + std::abs(gData[texel + 2]) + std::abs(gData[texel + 3]),
				std::abs(bData[texel + 1]) + std::abs(bData[texel + 2]) + std::abs(bData[texel + 3]));
			const float l0Energy = std::max({ l0.r, l0.g, l0.b, 1e-4f });
			const float l1Energy = std::max({ l1.r, l1.g, l1.b });
			GIProbeDebugEntrySnapshot entry;
			entry.position = ToEditorVec3(
				volumeMin + (glm::vec3(sample.x, sample.y, sample.z) + glm::vec3(0.5f)) * gi.probeSpacingAxes);
			entry.l0Diffuse = ToEditorVec3(l0 * 0.28209479f * exposure);
			entry.l1Ratio = std::clamp(l1Energy / l0Energy, 0.0f, 1.0f);
			m_GIProbeDebugSnapshot.probes.push_back(entry);
		}

		destroyReadbacks();
		return m_GIProbeDebugSnapshot;
	}

	GIProbeDebugSnapshot EngineAPIImpl::GetGIProbeDebugSnapshot() const
	{
		return m_GIProbeDebugSnapshot;
	}

	MainCameraHiZCullDebugSnapshot EngineAPIImpl::GetMainCameraHiZCullDebugSnapshot() const
	{
		MainCameraHiZCullDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		const VansGraphics::VansMainCameraVisibilityStats stats = scene->GetMainCameraVisibilityStats();
		snapshot.available = true;
		snapshot.enabled = stats.enabled;
		snapshot.historyValid = stats.historyValid;
		snapshot.candidateCount = stats.candidateCount;
		snapshot.frustumVisibleCount = stats.frustumVisibleCount;
		snapshot.hizCulledCount = stats.hizCulledCount;
		snapshot.forcedVisibleCount = stats.forcedVisibleCount;
		snapshot.preCullDrawCallCount = stats.preCullDrawCallCount;
		snapshot.culledDrawCallCount = stats.culledDrawCallCount;
		snapshot.drawnDrawCallCount = stats.drawnDrawCallCount;

		const auto& culledNodes = scene->GetMainCameraHiZCulledDebugNodes();
		snapshot.culledNodes.reserve(culledNodes.size());
		for (const VansGraphics::VansMainCameraHiZCulledNodeDebug& source : culledNodes)
		{
			if (!source.bounds.IsValid())
				continue;

			MainCameraHiZCulledNodeSnapshot node;
			node.name = source.nodeName.empty() ? std::string("<unnamed>") : source.nodeName;
			node.cullClass = ToMainCameraCullClassLabel(source.cullClass);
			node.center = ToEditorVec3(source.bounds.obb.center);
			node.axisXHalf = ToEditorVec3(source.bounds.obb.axisX * source.bounds.obb.halfExtent.x);
			node.axisYHalf = ToEditorVec3(source.bounds.obb.axisY * source.bounds.obb.halfExtent.y);
			node.axisZHalf = ToEditorVec3(source.bounds.obb.axisZ * source.bounds.obb.halfExtent.z);
			snapshot.culledNodes.push_back(std::move(node));
		}

		return snapshot;
	}

	RenderTexturePreview EngineAPIImpl::RequestGIRTPreview(
		std::uint32_t mode,
		std::uint32_t zSlice,
		std::uint32_t rayIndex,
		float exposure,
		float positionScale)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (device == nullptr)
			return {};

		auto& rayTracing = device->GetRayTracingContext();
		rayTracing.RequestGIRTPreview(mode, zSlice, rayIndex, exposure, positionScale);
		auto* texture = rayTracing.GetGIRTPreviewTexture();
		if (texture == nullptr)
			return {};

		static constexpr const char* kPreviewNames[] = {
			"RT Miss Ratio",
			"RT Hit Mask",
			"RT Hit Position",
			"RT Hit Normal",
			"RT Hit Albedo",
			"RT Hit Roughness",
			"GI Direct Radiance",
			"GI SH L0",
			"GI SH L1 Magnitude",
			"GI SH R L1 (Signed)",
			"GI SH G L1 (Signed)",
			"GI SH B L1 (Signed)"
		};
		const std::uint32_t safeMode = std::min<std::uint32_t>(mode, 11u);
		return BuildImagePreview(
			device,
			180,
			kPreviewNames[safeMode],
			texture->GetImage(),
			VK_IMAGE_LAYOUT_GENERAL);
	}

	void EngineAPIImpl::GenerateAutoReflectionProbes()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device)
			return;

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes)
			return;

		probes->GenerateAutoProbes(*scene, true);
		RebuildReflectionProbeResources();
	}

	void EngineAPIImpl::ClearAutoReflectionProbes()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return;

		auto* probes = scene->GetReflectionProbeSystem();
		if (!probes)
			return;

		probes->ClearAutoProbes();
		RebuildReflectionProbeResources();
	}

	void EngineAPIImpl::RequestReflectionProbeBakeAll()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* probes = scene ? scene->GetReflectionProbeSystem() : nullptr;
		if (probes)
			probes->RequestBakeAll();
	}

	void EngineAPIImpl::RequestReflectionProbeBake(std::uint32_t probeIndex)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* probes = scene ? scene->GetReflectionProbeSystem() : nullptr;
		if (probes)
			probes->RequestBake(static_cast<std::size_t>(probeIndex));
	}

	void EngineAPIImpl::SaveReflectionProbeConfiguration()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* probes = scene ? scene->GetReflectionProbeSystem() : nullptr;
		if (!probes)
			return;

		m_PendingScenePropertyEdits.push_back({
			"/reflectionProbes",
			ReflectionProbeScenePropertyValue(*probes)
		});
	}

	void EngineAPIImpl::ConvertReflectionProbeToManual(std::uint32_t probeIndex)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* probes = scene ? scene->GetReflectionProbeSystem() : nullptr;
		if (probes)
			probes->ConvertToManual(static_cast<std::size_t>(probeIndex));
	}

	RenderTexturePreview EngineAPIImpl::BuildWaterTexturePreview(RenderTextureFilter filter) const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device)
			return {};

		auto* waterSystem = scene->GetWaterSystem();
		if (!waterSystem)
			return {};

		auto build = [&](RenderTextureId id, const char* name, VansGraphics::VansVKImage& image, std::uint32_t layer)
		{
			return BuildLayerImagePreview(
				device,
				id,
				name,
				image,
				layer,
				VK_IMAGE_LAYOUT_GENERAL);
		};

		const std::string& textureName = filter.name;
		if (textureName == "displacement")
			return build(200, "Water Displacement", waterSystem->GetDisplacementImage(), filter.layer);
		if (textureName == "derivative")
			return build(201, "Water Derivative / FFT Normal Source", waterSystem->GetDerivativeImage(), filter.layer);
		if (textureName == "flow_map")
			return build(202, "Water Flow Map", waterSystem->GetFlowMapImage(), 0u);
		if (textureName == "reflection")
			return build(203, "Reflection", waterSystem->GetReflectionImage(), 0u);
		if (textureName == "refraction")
			return build(204, "Refraction", waterSystem->GetRefractionImage(), 0u);
		if (textureName == "caustics")
			return build(205, "Caustics", waterSystem->GetCausticsImage(), 0u);
		if (textureName == "thickness")
			return build(206, "Thickness", waterSystem->GetThicknessImage(), 0u);

		auto* fft = waterSystem->GetFFT();
		if (!fft)
			return {};

		if (textureName == "fft_h0")
			return build(220, "FFT H0 Spectrum", fft->GetH0SpectrumImage(), filter.layer);
		if (textureName == "fft_ping0")
			return build(221, "FFT Ping-Pong 0", fft->GetPingPongImage(0), filter.layer);
		if (textureName == "fft_ping1")
			return build(222, "FFT Ping-Pong 1", fft->GetPingPongImage(1), filter.layer);

		return {};
	}

	WaterSettingsSnapshot EngineAPIImpl::GetWaterSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->HasWaterNodes())
			return {};

		return ToWaterSettings(scene->GetWaterConfig());
	}

	void EngineAPIImpl::ApplyWaterSettings(const WaterSettingsSnapshot& settings)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->HasWaterNodes() || !settings.available)
			return;

		VansGraphics::VansWaterConfig& config = scene->EditWaterConfig();
		const VansGraphics::VansWaterConfig previousConfig = config;
		ApplyWaterSettingsToConfig(settings, config);

		auto* waterSystem = scene->GetWaterSystem();

		if (waterSystem)
		{
			waterSystem->SetWaterLevel(config.m_WaterLevel);
			if (ShouldRegenerateGerstnerSpectrum(previousConfig, config))
				waterSystem->UpdateWaveSSBO();
			if (ShouldRegenerateWaveParticleSpectrum(previousConfig, config))
				waterSystem->UpdateWaveParticleSSBO();
			if (ShouldReinitializeWaterFFT(previousConfig, config))
			{
				if (auto* fft = waterSystem->GetFFT())
					fft->MarkReinit();
			}
		}

		if (VansGraphics::VansRenderNode* waterNode = scene->GetWaterRenderNode())
		{
			glm::vec3 position = waterNode->GetTransformPosition();
			position.y = config.m_WaterLevel;
			waterNode->SetTransformData(
				position,
				waterNode->GetTransformRotation(),
				waterNode->GetTransformScale());
		}
	}

	WaterRuntimeStats EngineAPIImpl::GetWaterRuntimeStats() const
	{
		WaterRuntimeStats stats;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->HasWaterNodes())
			return stats;

		stats.available = true;
		stats.maxSpectrumCascade = std::max(scene->GetWaterConfig().m_Spectrum.m_CascadeCount - 1, 0);
		stats.fftFieldCount = int(VansGraphics::VansWaterFFT::FIELD_COUNT);

		auto* waterSystem = scene->GetWaterSystem();
		if (!waterSystem)
			return stats;

		stats.systemInitialized = true;
		stats.fftAvailable = waterSystem->GetFFT() != nullptr;
		auto* geometry = waterSystem->GetGeometryClipmap();
		if (geometry)
		{
			stats.patchCount = static_cast<std::uint32_t>(geometry->GetPatchCount());
			stats.meshDim = geometry->GetMeshDim();
			stats.basePatchSize = geometry->GetBasePatchSize();
			stats.indexCount = geometry->GetIndexCount();
			stats.lodLevels = geometry->GetLodLevels();
			stats.geometryLodRatio = VansGraphics::VansWaterConfig::GEOMETRY_LOD_RATIO;
		}
		return stats;
	}

	MeshLoadResult EngineAPIImpl::EnsureProjectMeshLoaded(const MeshLoadRequest& request)
	{
		MeshLoadResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device || request.meshName.empty() || request.sourcePath.empty())
			return result;

		if (scene->HasProjectMeshAlias(request.meshName))
		{
			result.available = scene->FindMeshAsset(request.meshName) != nullptr;
			return result;
		}

		auto* mesh = new VansGraphics::VansMesh(false, false);
		mesh->LoadMesh(
			device->GetLogicDevice(),
			device->GetGraphicsQueue(),
			&device->GetCommandBuffer(),
			request.sourcePath,
			false);
		mesh->SetName(request.meshName);
		scene->AddMeshAsset(mesh);
		scene->SetProjectMeshAlias(request.meshName, mesh);

		result.loaded = true;
		result.available = true;
		return result;
	}

	ProjectMeshInfoSnapshot EngineAPIImpl::GetProjectMeshInfo(const std::string& meshName) const
	{
		ProjectMeshInfoSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || meshName.empty())
			return snapshot;

		auto* mesh = static_cast<VansGraphics::VansMesh*>(scene->FindMeshAsset(meshName));
		if (!mesh)
			return snapshot;

		snapshot.available = true;
		snapshot.isMultiMesh = mesh->m_IsMultiMesh;

		const auto& materialInfos = mesh->m_SubmeshMaterialInfos;
		snapshot.submeshes.reserve(mesh->m_SubMeshes.size());
		for (std::size_t i = 0; i < mesh->m_SubMeshes.size(); ++i)
		{
			VansGraphics::VansMesh* submesh = mesh->m_SubMeshes[i];
			ProjectSubmeshInfo info;
			if (submesh)
			{
				info.sourceNodeName = submesh->m_SourceNodeName;
				info.vertexCount = static_cast<std::uint32_t>(submesh->GetMeshVertexCount());
				info.indexCount = submesh->GetIndexCount();
			}

			if (!materialInfos.empty())
			{
				const auto& materialInfo = i < materialInfos.size() ? materialInfos[i] : materialInfos[0];
				info.materialName = materialInfo.materialName;
				info.diffuseTexturePath = materialInfo.diffuseTexPath;
			}
			snapshot.submeshes.push_back(info);
		}

		return snapshot;
	}

	void EngineAPIImpl::RegisterProjectMeshAlias(const ProjectMeshAliasRequest& request)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || request.aliasName.empty() || request.meshName.empty())
			return;

		scene->SetProjectMeshAlias(request.aliasName, scene->FindMeshAsset(request.meshName));
	}

	std::string EngineAPIImpl::GetDefaultMaterialAssetName() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return {};

		const auto& materials = scene->GetMaterialAssets();
		if (materials.empty() || !materials[0])
			return {};

		return materials[0]->m_AssetName;
	}

	RuntimeSceneEntitiesCreateResult EngineAPIImpl::CreateRuntimeSceneEntities(
		const RuntimeSceneEntitiesCreateRequest& request)
	{
		RuntimeSceneEntitiesCreateResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device)
		{
			result.message = "Runtime scene or render device is not available";
			return result;
		}
		if (request.sceneEntities.empty())
		{
			result.message = "No runtime scene entities requested";
			return result;
		}

		std::vector<Vans::VansSerializedValue> entities;
		entities.reserve(request.sceneEntities.size());
		for (const ScenePropertyValue& value : request.sceneEntities)
		{
			Vans::VansSerializedValue entity = ScenePropertyValues::ToSerializedValue(value);
			if (entity.kind != Vans::VansSerializedValue::Kind::Object)
			{
				result.message = "Runtime scene entity payload must contain objects";
				return result;
			}
			const std::string entityGuid = Vans::ReadSerializedStringField(entity, "id");
			if (entityGuid.empty())
			{
				result.message = "Runtime scene entity payload is missing an id";
				return result;
			}
			result.entityGuids.push_back(entityGuid);
			entities.push_back(std::move(entity));
		}

		Vans::VansSerializedValue sceneRoot = Vans::VansSerializedValue::Object({
			{ "schemaVersion", Vans::VansSerializedValue::Int(Vans::VansSceneSchemaVersion) },
			{ "settings", Vans::VansSerializedValue::Object({}) },
			{ "entities", Vans::VansSerializedValue::Array(std::move(entities)) }
		});

		Vans::VansSceneContentBuildPlan buildPlan;
		std::string planError;
		const std::string projectRoot = GetProjectRootPath();
		if (!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
			sceneRoot,
			projectRoot,
			buildPlan,
			planError))
		{
			result.message = planError.empty()
				? "Could not build runtime scene entity plan"
				: planError;
			return result;
		}

		VkDevice logicalDevice = device->GetLogicDevice();
		scene->LoadSceneObjects(logicalDevice, buildPlan.objects, projectRoot);
		scene->GetLightManager()->CreateLightUniformData(logicalDevice);
		for (const std::string& entityGuid : result.entityGuids)
		{
			if (!scene->FindObjectByGuid(entityGuid))
			{
				result.message = "Runtime scene entity was not created: " + entityGuid;
				return result;
			}
		}
		result.created = true;
		return result;
	}

	ModelAssetPlacementPayload EngineAPIImpl::PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request)
	{
		return ModelAssetPlacementPreparationService::Prepare(request, m_Scene, m_Device);
	}

	RuntimeEntityDestroyResult EngineAPIImpl::DestroyRuntimeEntity(const RuntimeEntityDestroyRequest& request)
	{
		RuntimeEntityDestroyResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return result;

		if (!request.entityGuid.empty())
		{
			if (VansScriptObject* obj = scene->FindObjectByGuid(request.entityGuid))
			{
				result.destroyed = scene->DestroyEntity(obj);
			}
		}
		return result;
	}

	RuntimeEntityReparentResult EngineAPIImpl::ReparentRuntimeEntity(const RuntimeEntityReparentRequest& request)
	{
		RuntimeEntityReparentResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
		{
			result.message = "Runtime scene is not available";
			return result;
		}
		if (request.childEntityGuid.empty())
		{
			result.message = "Child entity id is empty";
			return result;
		}
		if (request.childEntityGuid == request.newParentEntityGuid)
		{
			result.message = "An entity cannot be parented to itself";
			return result;
		}

		result.applied = scene->SetEntityParentByGuid(
			request.childEntityGuid,
			request.newParentEntityGuid);
		result.message = result.applied
			? (request.newParentEntityGuid.empty() ? "Runtime entity parent cleared" : "Runtime entity reparented")
			: "Runtime entity parent edit failed";
		return result;
	}

	std::string EngineAPIImpl::MakeUniqueRuntimeEntityName(const std::string& baseName) const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || baseName.empty())
			return baseName;

		std::string uniqueName = baseName;
		int suffix = 1;
		while (scene->FindObjectByName(uniqueName))
			uniqueName = baseName + "_" + std::to_string(suffix++);
		return uniqueName;
	}

	std::string EngineAPIImpl::GetProjectRootPath() const
	{
		return Vans::VansProjectManager::Get().GetProjectRootPath();
	}

	bool EngineAPIImpl::IsRuntimeSceneReady() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		return scene && scene->IsSceneReady();
	}

	bool EngineAPIImpl::IsRuntimeSceneSwitching() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		return scene && scene->IsSceneSwitching();
	}

	RuntimeSceneLoadResult EngineAPIImpl::LoadRuntimeScene(const std::string& scenePath, RuntimeSceneLoadMode mode)
	{
		RuntimeSceneLoadResult result;
		result.requestedMode = mode;
		result.contentRevision = m_SceneContentRevision;

		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene)
		{
			result.diagnostics.push_back({ "runtime_scene_unbound", "Runtime scene is not bound" });
			return result;
		}
		if (!device)
		{
			result.diagnostics.push_back({ "runtime_device_unbound", "Runtime render device is not bound" });
			return result;
		}
		if (scenePath.empty())
		{
			result.diagnostics.push_back({ "scene_path_empty", "Scene path is empty" });
			return result;
		}
		if (!scene->AreResourcesLoaded())
		{
			result.finalState = scene->IsSceneReady()
				? RuntimeSceneLoadFinalState::Ready
				: RuntimeSceneLoadFinalState::Empty;
			result.diagnostics.push_back({ "project_resources_not_loaded", "Project resources must be loaded before the scene" });
			return result;
		}

		if (scene->IsSceneReady() || scene->IsSceneSwitching())
		{
			device->WaitForDevice();
			ClearEditorRenderTexturePreviewCaches(device);
		}

		const VansGraphics::VansSceneLoadMode runtimeMode =
			mode == RuntimeSceneLoadMode::Runtime
				? VansGraphics::VansSceneLoadMode::Runtime
				: VansGraphics::VansSceneLoadMode::Editor;
		if (!scene->LoadSceneForRendering(scenePath.c_str(), device, runtimeMode) || !scene->IsSceneReady())
		{
			result.finalState = scene->IsSceneReady()
				? RuntimeSceneLoadFinalState::Ready
				: RuntimeSceneLoadFinalState::Empty;
			result.diagnostics.push_back({ "scene_build_failed", "Scene content could not be built and prepared for rendering" });
			return result;
		}

		result.success = true;
		result.finalState = RuntimeSceneLoadFinalState::Ready;
		result.contentRevision = ++m_SceneContentRevision;
		return result;
	}

	void EngineAPIImpl::UnloadRuntimeScene()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return;

		if (auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device))
		{
			device->WaitForDevice();
			ClearEditorRenderTexturePreviewCaches(device);
		}

		if (scene->IsSceneReady() || scene->IsSceneSwitching())
			scene->UnLoadScene();
	}

	bool EngineAPIImpl::AreRuntimeProjectResourcesLoaded() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		return scene && scene->AreResourcesLoaded();
	}

	void EngineAPIImpl::UnloadRuntimeProjectResources()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene)
			return;

		if (scene->AreResourcesLoaded())
			scene->UnloadProjectResources(device);
	}

	bool EngineAPIImpl::LoadRuntimeProjectAssetsForScene(const std::string& scenePath)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
		if (!scene || !database || scenePath.empty())
			return false;

		return scene->LoadProjectAssets(*database, scenePath, device);
	}

	VehicleDebugSnapshot EngineAPIImpl::GetVehicleDebugSnapshot() const
	{
		VehicleDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* vehicle = scene ? scene->GetVehicle() : nullptr;
		if (!vehicle)
			return snapshot;

		snapshot.available = true;
		const physx::PxTransform bodyPose = vehicle->GetTransform();
		const physx::PxTransform chassisPose = bodyPose * vehicle->GetBodyBoxLocalPose();
		snapshot.chassis.center = ToEditorVec3(chassisPose.p);
		snapshot.chassis.axisX = ToEditorVec3(chassisPose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f)));
		snapshot.chassis.axisY = ToEditorVec3(chassisPose.q.rotate(physx::PxVec3(0.0f, 1.0f, 0.0f)));
		snapshot.chassis.axisZ = ToEditorVec3(chassisPose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f)));
		snapshot.chassis.halfExtents = ToEditorVec3(vehicle->GetBodyBoxHalfExtents());

		const VansEngine::VansVehicleTuning& tuning = vehicle->GetTuning();
		const physx::PxVec3 localLateral = VehicleAxisToPx(tuning.lateralAxis);
		const physx::PxVec3 localVertical = VehicleAxisToPx(tuning.verticalAxis);
		const physx::PxVec3 localLongitudinal = VehicleAxisToPx(tuning.longitudinalAxis);

		const std::uint32_t wheelCount = std::min<std::uint32_t>(vehicle->GetNumWheels(), 4u);
		snapshot.wheels.reserve(wheelCount);
		for (std::uint32_t wheelIndex = 0; wheelIndex < wheelCount; ++wheelIndex)
		{
			const physx::PxTransform wheelPose = vehicle->GetWheelWorldPose(wheelIndex);
			const float radius = vehicle->GetWheelRadius(wheelIndex);
			const physx::PxVec3 attachLocal = vehicle->GetSuspensionAttachmentLocal(wheelIndex);
			const physx::PxVec3 travelDir = vehicle->GetSuspensionTravelDir(wheelIndex);
			const float travel = vehicle->GetSuspensionTravelDist(wheelIndex);

			VehicleWheelDebugSnapshot wheel;
			wheel.center = ToEditorVec3(wheelPose.p);
			wheel.lateralAxis = ToEditorVec3(wheelPose.q.rotate(localLateral));
			wheel.verticalAxis = ToEditorVec3(wheelPose.q.rotate(localVertical));
			wheel.longitudinalAxis = ToEditorVec3(wheelPose.q.rotate(localLongitudinal));
			wheel.suspensionAttach = ToEditorVec3(bodyPose.transform(attachLocal));
			wheel.suspensionRayEnd = ToEditorVec3(bodyPose.transform(attachLocal + travelDir * (travel + radius)));
			wheel.radius = radius;
			wheel.halfWidth = vehicle->GetWheelHalfWidth(wheelIndex);
			snapshot.wheels.push_back(wheel);
		}

		return snapshot;
	}

	bool EngineAPIImpl::HasAnimationDebugNodes() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		return scene && !scene->GetAnimationNodes().empty();
	}

	VansGraphics::VansAnimationNode* EngineAPIImpl::FindRuntimeAnimationNodeByEntityGuid(
		const std::string& entityGuid) const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || entityGuid.empty())
			return nullptr;

		for (auto* animNode : scene->GetAnimationNodes())
		{
			if (!animNode)
				continue;

			for (auto* renderNode : animNode->GetRenderNodes())
			{
				if (!renderNode)
					continue;
				if (renderNode->m_EntityGuid == entityGuid ||
				    renderNode->m_ParentEntityGuid == entityGuid)
				{
					return animNode;
				}
			}
		}

		return nullptr;
	}

	MotionMatchingDebugSnapshot EngineAPIImpl::GetMotionMatchingDebugSnapshot() const
	{
		MotionMatchingDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		for (auto* animNode : scene->GetAnimationNodes())
		{
			auto* controller = animNode ? animNode->GetController() : nullptr;
			if (!controller || !controller->IsMotionMatchingConfigured())
				continue;

			const auto* motionMatching = controller->GetMotionMatchingDebugData();
			if (!motionMatching || !motionMatching->enabled)
				continue;

			const uint32_t transformId = animNode->GetTransformID();
			glm::mat4 worldMatrix = VansTransformStore::GetTransform(transformId).GetModelMatrix();
			const auto& globals = controller->GetCachedGlobalTransforms();
			glm::vec3 rootWorld = glm::vec3(worldMatrix[3]);

			const VansGraphics::Skeleton& skeleton = animNode->GetSkeleton();
			int rootBoneIndex = -1;
			auto rootIt = skeleton.boneNameToIndex.find("root");
			if (rootIt != skeleton.boneNameToIndex.end())
				rootBoneIndex = rootIt->second;
			else
			{
				rootIt = skeleton.boneNameToIndex.find("Root");
				if (rootIt != skeleton.boneNameToIndex.end())
					rootBoneIndex = rootIt->second;
			}

			if (rootBoneIndex >= 0 && rootBoneIndex < static_cast<int>(globals.size()))
			{
				const glm::mat4 boneWorld = worldMatrix * globals[rootBoneIndex];
				rootWorld = glm::vec3(boneWorld[3]);
			}

			const float direction = motionMatching->queryDirection;
			const float speed = motionMatching->querySpeed * 0.01f;
			glm::vec3 localVelocity(std::sin(direction) * speed, -std::cos(direction) * speed, 0.0f);
			glm::vec3 worldVelocity = glm::vec3(worldMatrix * glm::vec4(localVelocity, 0.0f));
			worldVelocity.y = 0.0f;
			if (glm::length(worldVelocity) > 0.0001f)
				worldVelocity = glm::normalize(worldVelocity) * speed;

			MotionMatchingDebugVisual visual;
			visual.rootPosition = ToEditorVec3(rootWorld);
			visual.velocity = ToEditorVec3(worldVelocity);
			visual.activeClip = motionMatching->activeClip;
			visual.playbackRate = motionMatching->playbackRate;
			snapshot.visuals.push_back(visual);
		}

		snapshot.available = !snapshot.visuals.empty();
		return snapshot;
	}

	SkeletonDebugSnapshot EngineAPIImpl::GetSkeletonDebugSnapshot(const std::string& entityGuidFilter) const
	{
		SkeletonDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		auto matchesFilter = [&](VansGraphics::VansAnimationNode* animNode) -> bool
		{
			if (entityGuidFilter.empty())
				return true;
			if (!animNode)
				return false;
			for (auto* renderNode : animNode->GetRenderNodes())
			{
				if (!renderNode)
					continue;
				if (renderNode->m_EntityGuid == entityGuidFilter ||
				    renderNode->m_ParentEntityGuid == entityGuidFilter)
				{
					return true;
				}
			}
			return false;
		};

		auto buildFallbackGlobals = [](const VansGraphics::Skeleton& skeleton)
		{
			std::vector<glm::mat4> globals(skeleton.bones.size(), glm::mat4(1.0f));
			auto applyBone = [&](int index)
			{
				if (index < 0 || index >= static_cast<int>(skeleton.bones.size()))
					return;
				const auto& bone = skeleton.bones[index];
				if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(globals.size()))
					globals[index] = globals[bone.parentIndex] * bone.localTransform;
				else
					globals[index] = bone.localTransform;
			};

			if (!skeleton.topologicalOrder.empty())
			{
				for (int index : skeleton.topologicalOrder)
					applyBone(index);
			}
			else
			{
				for (int index = 0; index < static_cast<int>(skeleton.bones.size()); ++index)
					applyBone(index);
			}
			return globals;
		};

		auto appendRig = [&](
			VansGraphics::VansAnimationNode* animNode,
			const VansGraphics::Skeleton& skeleton,
			const VansGraphics::VansAnimationController* controller,
			const glm::mat4& ownerWorld,
			const std::string& role,
			bool retargetSource)
		{
			if (!animNode || skeleton.bones.empty())
				return;

			std::vector<glm::mat4> fallbackGlobals;
			const std::vector<glm::mat4>* globals = nullptr;
			if (controller && controller->GetCachedGlobalTransforms().size() == skeleton.bones.size())
				globals = &controller->GetCachedGlobalTransforms();
			else
			{
				fallbackGlobals = buildFallbackGlobals(skeleton);
				globals = &fallbackGlobals;
			}

			SkeletonDebugRigSnapshot rig;
			rig.nodeName = retargetSource ? animNode->GetName() + " Source" : animNode->GetName();
			rig.role = role;
			rig.retargetSource = retargetSource;
			if (controller)
			{
				rig.currentState = controller->GetCurrentStateName();
				rig.currentTime = controller->GetCurrentPlayTime();
				rig.normalizedTime = controller->GetNormalizedTime();
				rig.playing = controller->GetPlaybackState() == VansGraphics::AnimationState::Playing;
				if (const auto* motionMatching = controller->GetMotionMatchingDebugData())
				{
					rig.activeClip = motionMatching->activeClip;
					rig.selectedClip = motionMatching->selectedClip;
				}
			}
			if (auto* renderNode = animNode->GetRenderNode())
				rig.entityGuid = renderNode->m_ParentEntityGuid.empty() ? renderNode->m_EntityGuid : renderNode->m_ParentEntityGuid;
			rig.bones.reserve(skeleton.bones.size());

			for (size_t i = 0; i < skeleton.bones.size(); ++i)
			{
				const glm::mat4 boneWorld = ownerWorld * (*globals)[i];
				SkeletonDebugBoneSnapshot bone;
				bone.name = skeleton.bones[i].name;
				bone.parentIndex = skeleton.bones[i].parentIndex;
				bone.worldPosition = ToEditorVec3(glm::vec3(boneWorld[3]));
				rig.bones.push_back(std::move(bone));
			}

			snapshot.rigs.push_back(std::move(rig));
		};

		for (auto* animNode : scene->GetAnimationNodes())
		{
			if (!animNode || !matchesFilter(animNode))
				continue;

			const VansGraphics::Skeleton& skeleton = animNode->GetSkeleton();
			if (skeleton.bones.empty())
				continue;

			glm::mat4 ownerWorld(1.0f);
			const uint32_t transformId = animNode->GetTransformID();
			if (transformId < VansGraphics::VansTransformStore::GlobalTransforms.size())
				ownerWorld = VansGraphics::VansTransformStore::GetTransform(transformId).GetModelMatrix();

			appendRig(animNode, skeleton, animNode->GetController(), ownerWorld, "Target", false);
			if (animNode->IsRetargetEnabled() && animNode->GetRetargetSourceController())
			{
				appendRig(animNode,
					animNode->GetRetargetSourceSkeleton(),
					animNode->GetRetargetSourceController(),
					ownerWorld,
					"Retarget Source",
					true);
			}
		}

		snapshot.available = !snapshot.rigs.empty();
		return snapshot;
	}

	void EngineAPIImpl::SetFootIKDebugVisualization(bool enabled)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return;

		for (auto* animNode : scene->GetAnimationNodes())
		{
			auto* controller = animNode ? animNode->GetController() : nullptr;
			if (controller && controller->IsFootPlacementConfigured())
				controller->SetFootPlacementDebugVisualization(enabled);
		}
	}

	FootIKDebugSnapshot EngineAPIImpl::GetFootIKDebugSnapshot() const
	{
		FootIKDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		for (auto* animNode : scene->GetAnimationNodes())
		{
			auto* controller = animNode ? animNode->GetController() : nullptr;
			if (!controller || !controller->IsFootPlacementConfigured())
				continue;

			const auto* debug = controller->GetFootPlacementDebugData();
			if (!debug || !debug->enabled)
				continue;

			snapshot.leftLegs.push_back(ToFootIKDebugLeg(debug->left));
			snapshot.rightLegs.push_back(ToFootIKDebugLeg(debug->right));
		}

		snapshot.available = !snapshot.leftLegs.empty() || !snapshot.rightLegs.empty();
		return snapshot;
	}

	TerrainSettingsSnapshot EngineAPIImpl::GetTerrainSettings() const
	{
		TerrainSettingsSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* terrain = GetRuntimeTerrain(scene);
		if (!terrain)
			return snapshot;

		snapshot.available = true;
		snapshot.tessellationEnabled = terrain->IsTessellationEnabled();
		snapshot.tessellationDistance = terrain->GetTessellationDistance();
		snapshot.maxTessellationLevel = terrain->GetMaxTessellationLevel();
		snapshot.tessellationPower = terrain->GetTessellationPower();
		snapshot.tessLodBias = terrain->GetTessLodBias();
		snapshot.noiseDetailEnabled = terrain->IsNoiseDetailEnabled();
		snapshot.noiseStrength = terrain->GetNoiseStrength();
		snapshot.noiseFrequency = terrain->GetNoiseFrequency();
		snapshot.noiseOctaves = terrain->GetNoiseOctaves();
		snapshot.noiseGain = terrain->GetNoiseGain();
		snapshot.noiseLacunarity = terrain->GetNoiseLacunarity();
		snapshot.noiseWarpStrength = terrain->GetNoiseWarpStrength();
		snapshot.noiseFadeStart = terrain->GetNoiseFadeStart();
		snapshot.terrainSize = terrain->GetTerrainSize();
		snapshot.splitDistMult = terrain->GetSplitDistMult();
		snapshot.lodDistanceRatio = terrain->GetLodDistanceRatio();
		return snapshot;
	}

	void EngineAPIImpl::ApplyTerrainSettings(const TerrainSettingsSnapshot& settings)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* terrain = GetRuntimeTerrain(scene);
		if (!terrain)
			return;

		terrain->SetTessellationEnabled(settings.tessellationEnabled);
		terrain->SetTessellationDistance(settings.tessellationDistance);
		terrain->SetMaxTessellationLevel(settings.maxTessellationLevel);
		terrain->SetTessellationPower(settings.tessellationPower);
		terrain->SetTessLodBias(settings.tessLodBias);
		terrain->SetNoiseDetailEnabled(settings.noiseDetailEnabled);
		terrain->SetNoiseStrength(settings.noiseStrength);
		terrain->SetNoiseFrequency(settings.noiseFrequency);
		terrain->SetNoiseOctaves(settings.noiseOctaves);
		terrain->SetNoiseGain(settings.noiseGain);
		terrain->SetNoiseLacunarity(settings.noiseLacunarity);
		terrain->SetNoiseWarpStrength(settings.noiseWarpStrength);
		terrain->SetNoiseFadeStart(settings.noiseFadeStart);
		terrain->SetSplitDistMult(settings.splitDistMult);
		terrain->SetLodDistanceRatio(settings.lodDistanceRatio);
	}

	bool EngineAPIImpl::ApplyRuntimeEntityPreviewChange(const RuntimeEntityPreviewChange& change)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || change.Empty())
			return false;

		bool applied = false;
		bool failed = false;
		for (const RuntimeEntityNameEdit& nameEdit : change.nameEdits)
		{
			const bool nameApplied = scene->SetEntityNameByGuid(nameEdit.entityGuid, nameEdit.name);
			applied = nameApplied || applied;
			failed = !nameApplied || failed;
		}

		for (const RuntimeEntityActiveEdit& activeEdit : change.activeEdits)
		{
			const bool activeApplied = scene->SetEntityActiveByGuid(activeEdit.entityGuid, activeEdit.active);
			applied = activeApplied || applied;
			failed = !activeApplied || failed;
		}

		for (const RuntimeComponentEnabledEdit& componentEdit : change.componentEnabled)
		{
			const bool componentApplied = SetRuntimeComponentEnabled(
				componentEdit.entityGuid,
				componentEdit.componentGuid,
				componentEdit.componentType,
				componentEdit.enabled);
			applied = componentApplied || applied;
			failed = !componentApplied || failed;
		}

		for (const RuntimeEntityParentEdit& parentEdit : change.parentEdits)
		{
			const bool parentApplied = scene->SetEntityParentByGuid(parentEdit.entityGuid, parentEdit.parentEntityGuid);
			applied = parentApplied || applied;
			failed = !parentApplied || failed;
		}

		if (change.hasTransform)
		{
			const bool transformApplied = !change.transform.entityGuid.empty();
			if (transformApplied)
				ApplyRuntimeTransform(change.transform);
			applied = transformApplied || applied;
			failed = !transformApplied || failed;
		}

		for (RuntimeLightEdit lightEdit : change.lights)
		{
			SubmitCommand(std::make_unique<SetRuntimeLightPropertiesCommand>(std::move(lightEdit)));
			applied = true;
		}

		if (!change.materialOverrides.empty())
		{
			VansGraphics::VansMaterialLiveEditService liveEdit;
			for (const RuntimeRendererMaterialOverrideEdit& edit : change.materialOverrides)
			{
				const bool materialApplied = liveEdit.ApplyRendererMaterialOverride(scene, edit);
				applied = materialApplied || applied;
				failed = !materialApplied || failed;
			}
		}

		return applied && !failed;
	}

	bool EngineAPIImpl::SetRuntimeComponentEnabled(
		const std::string& entityGuid,
		const std::string& componentGuid,
		const std::string& componentType,
		bool enabled)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || componentGuid.empty())
			return false;

		const std::string runtimeKey = Vans::CanonicalRuntimeComponentKeyForName(componentType);
		Vans::VansComponentHandle runtimeComponent;
		bool projectedByRuntime = false;
		bool runtimeEffectiveEnabled = enabled;
		if (auto* runtimeWorld = scene->GetRuntimeWorld())
		{
			const std::uint16_t typeId = Vans::VansRuntimeComponentTypeIdForKey(runtimeKey);
			runtimeComponent = runtimeWorld->FindComponentByGuid(componentGuid, typeId);
			if (runtimeComponent.IsValid())
			{
				runtimeWorld->Commands().SetComponentEnabled(runtimeComponent, enabled);
				runtimeWorld->FlushCommands();
				runtimeEffectiveEnabled = runtimeWorld->IsComponentEffectivelyEnabled(runtimeComponent);
				projectedByRuntime = scene->ApplyRuntimeComponentEnabled(
					runtimeComponent,
					runtimeEffectiveEnabled);
			}
		}

		VansScriptObject* obj = entityGuid.empty() ? nullptr : scene->FindObjectByGuid(entityGuid);
		if (!obj)
			return projectedByRuntime;

		for (auto* comp : obj->m_Components)
		{
			if (!comp)
				continue;
			if (comp->m_ComponentGuid == componentGuid)
			{
				const std::string componentRuntimeKey =
					Vans::CanonicalRuntimeComponentKeyForName(comp->m_ComponentName);
				if (!runtimeKey.empty() && componentRuntimeKey != runtimeKey)
				{
					VANS_LOG_WARN("[RuntimePreview] Component enabled edit type mismatch: entity='"
						<< entityGuid << "' component='" << componentGuid
						<< "' expected='" << runtimeKey << "' actual='" << componentRuntimeKey << "'");
				}
				if (projectedByRuntime)
				{
					comp->MirrorRuntimeEnabledState(enabled, runtimeEffectiveEnabled);
					if (runtimeComponent.typeId == Vans::VansRuntimeComponentType_UI)
					{
						std::vector<std::uint64_t> openScreens;
						if (scene->CopyRuntimeUIOpenScreens(runtimeComponent, openScreens))
							comp->MirrorRuntimeOpenScreens(openScreens);
					}
				}
				else
				{
					comp->SetEnabled(enabled);
					if (runtimeComponent.IsValid() &&
						runtimeComponent.typeId == Vans::VansRuntimeComponentType_Script)
					{
						if (auto* luaComponent = dynamic_cast<VansLuaScriptComponent*>(comp))
							scene->SyncRuntimeScriptComponentFromFacade(componentGuid, *luaComponent);
					}
				}
				return true;
			}
		}
		return projectedByRuntime;
	}

	bool EngineAPIImpl::ApplyRuntimeMaterialPreviewChange(const RuntimeMaterialPreviewChange& change)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || change.Empty())
			return false;

		VansGraphics::VansMaterialLiveEditService liveEdit;
		return liveEdit.ApplyMaterialPreviewChange(
			scene,
			std::filesystem::path(change.assetPath),
			change.parameters,
			change.textures);
	}

	void EngineAPIImpl::CommitLightingChanges()
	{
		CommitRuntimeLighting(static_cast<VansGraphics::VansScene*>(m_Scene));
	}

	LightingSettingsSnapshot EngineAPIImpl::GetLightingSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* lightManager = scene ? scene->GetLightManager() : nullptr;
		return CaptureLightingSettings(lightManager);
	}

	void EngineAPIImpl::ApplyLightingSettings(const LightingSettingsSnapshot& settings)
	{
		SubmitCommand(std::make_unique<SetLightingSettingsCommand>(settings));
	}

	PostProcessSettingsSnapshot EngineAPIImpl::GetPostProcessSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
		if (!materialManager)
			return {};

		return ToAPIPostProcessSettings(materialManager->m_PostProcessProfile);
	}

	void EngineAPIImpl::ApplyPostProcessSettings(const PostProcessSettingsSnapshot& settings)
	{
		if (!settings.available)
			return;
		SubmitCommand(std::make_unique<SetPostProcessSettingsCommand>(settings));
	}

	void EngineAPIImpl::CommitPostProcessSettings()
	{
		const PostProcessSettingsSnapshot settings = GetPostProcessSettings();
		if (!settings.available)
			return;

		m_PendingScenePropertyEdits.push_back({ "/settings/postProcess", ScenePropertyValues::Object({
			{ "exposure", ScenePropertyValues::Object({
				{ "enableAutoExposure", ScenePropertyValues::Bool(settings.enableAutoExposure) },
				{ "exposureCompensation", ScenePropertyValues::Float(settings.exposureCompensation) },
				{ "minEV100", ScenePropertyValues::Float(settings.minEV100) },
				{ "maxEV100", ScenePropertyValues::Float(settings.maxEV100) },
				{ "adaptationSpeedUp", ScenePropertyValues::Float(settings.adaptationSpeedUp) },
				{ "adaptationSpeedDown", ScenePropertyValues::Float(settings.adaptationSpeedDown) }
			}) },
			{ "bloom", ScenePropertyValues::Object({
				{ "enable", ScenePropertyValues::Bool(settings.enableBloom) },
				{ "threshold", ScenePropertyValues::Float(settings.bloomThreshold) },
				{ "knee", ScenePropertyValues::Float(settings.bloomKnee) },
				{ "intensity", ScenePropertyValues::Float(settings.bloomIntensity) },
				{ "scatter", ScenePropertyValues::Float(settings.bloomScatter) }
			}) },
			{ "toneMapping", ScenePropertyValues::Object({
				{ "type", ScenePropertyValues::Int(settings.toneMapperType) },
				{ "whitePoint", ScenePropertyValues::Float(settings.whitePoint) }
			}) },
			{ "colorGrading", ScenePropertyValues::Object({
				{ "enable", ScenePropertyValues::Bool(settings.enableColorGrading) },
				{ "contrast", ScenePropertyValues::Float(settings.contrast) },
				{ "saturation", ScenePropertyValues::Float(settings.saturation) },
				{ "hueShift", ScenePropertyValues::Float(settings.hueShift) },
				{ "temperature", ScenePropertyValues::Float(settings.temperature) },
				{ "tint", ScenePropertyValues::Float(settings.tint) }
			}) }
		}) });
	}

	void EngineAPIImpl::ApplyFogSettings(const FogSettings& settings)
	{
		SubmitCommand(std::make_unique<SetFogSettingsCommand>(settings));
	}

	void EngineAPIImpl::CommitHeightFogSettings()
	{
		const FogSettings settings = GetFogSettings();
		m_PendingScenePropertyEdits.push_back({ "/settings/heightFog", ScenePropertyValues::Object({
			{ "fogDensity", ScenePropertyValues::Float(settings.fogDensity) },
			{ "heightFalloff", ScenePropertyValues::Float(settings.heightFalloff) },
			{ "sunScatterScale", ScenePropertyValues::Float(settings.sunScatterScale) },
			{ "ambientScale", ScenePropertyValues::Float(settings.ambientScale) },
			{ "fogMinHeight", ScenePropertyValues::Float(settings.fogMinHeight) },
			{ "skyFogDistance", ScenePropertyValues::Float(settings.skyFogDistance) }
		}) });
	}

	FogSettings EngineAPIImpl::GetFogSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
		if (!materialManager)
			return {};

		return ToAPIFogSettings(materialManager->GetFogSettings());
	}

	void EngineAPIImpl::ApplyFogVolumeSettings(const FogVolumeSettings& settings)
	{
		SubmitCommand(std::make_unique<SetFogVolumeSettingsCommand>(settings));
	}

	void EngineAPIImpl::CommitVolumetricFogSettings()
	{
		const FogVolumeSettings settings = GetFogVolumeSettings();
		m_PendingScenePropertyEdits.push_back({ "/settings/volumetricFog", ScenePropertyValues::Object({
			{ "density", ScenePropertyValues::Float(settings.density) },
			{ "anisotropy", ScenePropertyValues::Float(settings.anisotropy) },
			{ "scatterScale", ScenePropertyValues::Float(settings.scatterScale) },
			{ "ambientScale", ScenePropertyValues::Float(settings.ambientScale) },
			{ "volumeNear", ScenePropertyValues::Float(settings.volumeNear) },
			{ "volumeFar", ScenePropertyValues::Float(settings.volumeFar) },
			{ "slicePower", ScenePropertyValues::Float(settings.slicePower) },
			{ "fogBoxMin", ScenePropertyValues::Array({
				ScenePropertyValues::Float(settings.fogBoxMin[0]),
				ScenePropertyValues::Float(settings.fogBoxMin[1]),
				ScenePropertyValues::Float(settings.fogBoxMin[2])
			}) },
			{ "fogBoxMax", ScenePropertyValues::Array({
				ScenePropertyValues::Float(settings.fogBoxMax[0]),
				ScenePropertyValues::Float(settings.fogBoxMax[1]),
				ScenePropertyValues::Float(settings.fogBoxMax[2])
			}) }
		}) });
	}

	FogVolumeSettings EngineAPIImpl::GetFogVolumeSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
		if (!materialManager)
			return {};

		return ToAPIFogVolumeSettings(materialManager->GetFogVolumeSettings());
	}

	CloudSettings EngineAPIImpl::GetCloudSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
		if (!materialManager)
			return {};

		return ToCloudSettings(materialManager->m_CloudParams);
	}

	void EngineAPIImpl::ApplyCloudSettings(const CloudSettings& settings)
	{
		SubmitCommand(std::make_unique<SetCloudSettingsCommand>(settings));
	}

	void EngineAPIImpl::ResetCloudSettings()
	{
		SubmitCommand(std::make_unique<SetCloudSettingsCommand>(
			ToCloudSettings(VansGraphics::VansCloudParamsGPU())));
	}

	void EngineAPIImpl::CommitCloudSettings()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->GetMaterialManager())
			return;

		auto* materialManager = scene->GetMaterialManager();
		materialManager->UploadCloudParamsToGPU();
		const CloudSettings settings = ToCloudSettings(materialManager->m_CloudParams);
		m_PendingScenePropertyEdits.push_back({ "/settings/volumetricClouds", ScenePropertyValues::Object({
			{ "planetRadius", ScenePropertyValues::Float(settings.planetRadius) },
			{ "seaLevel", ScenePropertyValues::Float(settings.seaLevel) },
			{ "cloudBaseHeight", ScenePropertyValues::Float(settings.cloudMinHeight) },
			{ "cloudThickness", ScenePropertyValues::Float(std::max(settings.cloudMaxHeight - settings.cloudMinHeight, 100.0f)) },
			{ "density", ScenePropertyValues::Float(settings.density) },
			{ "coverage", ScenePropertyValues::Float(settings.coverage) },
			{ "sunBrightness", ScenePropertyValues::Float(settings.sunBrightness) },
			{ "phaseG", ScenePropertyValues::Float(settings.phaseG) },
			{ "mainTileMeters", ScenePropertyValues::Float(settings.mainTileMeters) },
			{ "detailTileMeters", ScenePropertyValues::Float(settings.detailTileMeters) },
			{ "mainHeightScale", ScenePropertyValues::Float(settings.mainHeightScale) },
			{ "detailHeightScale", ScenePropertyValues::Float(settings.detailHeightScale) },
			{ "thresholdLowCoverage", ScenePropertyValues::Float(settings.thresholdLowCoverage) },
			{ "thresholdHighCoverage", ScenePropertyValues::Float(settings.thresholdHighCoverage) },
			{ "densityRemapLow", ScenePropertyValues::Float(settings.densityRemapLow) },
			{ "densityRemapHigh", ScenePropertyValues::Float(settings.densityRemapHigh) },
			{ "mainErosionStrength", ScenePropertyValues::Float(settings.mainErosionStrength) },
			{ "detailErosionStrength", ScenePropertyValues::Float(settings.detailErosionStrength) },
			{ "edgeErosionStrength", ScenePropertyValues::Float(settings.edgeErosionStrength) },
			{ "verticalShapePower", ScenePropertyValues::Float(settings.verticalShapePower) },
			{ "detailErosionLow", ScenePropertyValues::Float(settings.detailErosionLow) },
			{ "detailErosionHigh", ScenePropertyValues::Float(settings.detailErosionHigh) },
			{ "detailEdgeStrength", ScenePropertyValues::Float(settings.detailEdgeStrength) },
			{ "shadowDensityScale", ScenePropertyValues::Float(settings.shadowDensityScale) }
		}) });
	}

	std::vector<ScenePropertyEdit> EngineAPIImpl::ConsumeScenePropertyEdits()
	{
		std::vector<ScenePropertyEdit> edits = std::move(m_PendingScenePropertyEdits);
		m_PendingScenePropertyEdits.clear();
		return edits;
	}

	EnginePlayState EngineAPIImpl::GetPlayState() const
	{
		return m_PlayState;
	}

	void EngineAPIImpl::SetPlayState(EnginePlayState state)
	{
		if (m_PlayState == state)
			return;

		const EnginePlayState previousState = m_PlayState;
		m_PlayState = state;
		Vans::VansInputManager::Get().SetCursorCaptureAllowed(state == EnginePlayState::Play);
		Vans::VansEventBus::Get().PublishNow(
			VansEditorPlayStateChangedEvent{ previousState, state });
	}

	EntityId EngineAPIImpl::RaycastScene(const Ray&) const
	{
		return InvalidEntityId;
	}

	std::string EngineAPIImpl::PickRuntimeEntity(const Ray& ray) const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return {};

		const glm::vec3 rayOrigin = ToRuntimeVec3(ray.origin);
		const glm::vec3 rayDirection = glm::normalize(ToRuntimeVec3(ray.direction));
		float bestT = FLT_MAX;
		VansGraphics::VansRenderNode* bestNode = nullptr;

		auto testNode = [&](VansGraphics::VansRenderNode* node)
		{
			if (!node || !node->m_Mesh)
				return;

			VansGraphics::VansTransform& transform =
				VansGraphics::VansTransformStore::GetTransform(node->m_TransformID);
			glm::vec3 center = transform.m_Position;
			float radius = 1.0f;

			const std::vector<float>& rawPositions = node->m_Mesh->GetMeshRawPositionData();
			if (rawPositions.size() >= 3)
			{
				glm::vec3 localMin(FLT_MAX);
				glm::vec3 localMax(-FLT_MAX);
				for (std::size_t i = 0; i + 2 < rawPositions.size(); i += 3)
				{
					const glm::vec3 vertex(rawPositions[i], rawPositions[i + 1], rawPositions[i + 2]);
					localMin = glm::min(localMin, vertex);
					localMax = glm::max(localMax, vertex);
				}

				const glm::vec3 halfExtents = (localMax - localMin) * 0.5f * transform.m_Scale;
				const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
				center += localCenter * transform.m_Scale;
				radius = glm::length(halfExtents);
			}
			else
			{
				radius = glm::max(glm::length(transform.m_Scale) * 0.5f, 0.25f);
			}

			const float t = RaySphereIntersect(rayOrigin, rayDirection, center, radius);
			if (t > 0.0f && t < bestT)
			{
				bestT = t;
				bestNode = node;
			}
		};

		for (auto* node : scene->CollectSSBOManagedRenderNodes())
			testNode(node);

		if (!bestNode)
			return {};

		if (!bestNode->m_EntityGuid.empty())
			return bestNode->m_EntityGuid;
		if (!bestNode->m_ParentEntityGuid.empty())
			return bestNode->m_ParentEntityGuid;

		const Vans::VansRuntimeWorld* runtimeWorld = scene->GetRuntimeWorld();
		if (!runtimeWorld)
			return {};

		for (Vans::VansEntityHandle entity : runtimeWorld->Entities().CollectAliveEntities())
		{
			const Vans::VansEntityRecord* entityRecord = runtimeWorld->Entities().Get(entity);
			if (!entityRecord)
				continue;
			const std::vector<Vans::VansComponentHandle> components =
				runtimeWorld->CollectComponentsOwnedBy(entity);
			for (Vans::VansComponentHandle component : components)
			{
				const auto* render = GetRuntimeComponentPayload<Vans::VansRuntimeRenderComponent>(
					*runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Render);
				if (!render)
					continue;
				if (render->renderNode == bestNode ||
					std::find(render->renderNodes.begin(), render->renderNodes.end(), bestNode) !=
						render->renderNodes.end())
				{
					return entityRecord->stableGuid;
				}
			}
		}
		return {};
	}

	RuntimeTransformSnapshot EngineAPIImpl::GetRuntimeTransform(const std::string& entityGuid) const
	{
		RuntimeTransformSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || entityGuid.empty())
			return snapshot;

		ReadRuntimeTransformById(
			ResolveRuntimeTransformId(scene, entityGuid),
			entityGuid,
			snapshot);
		return snapshot;
	}

	void EngineAPIImpl::ApplyRuntimeTransform(const RuntimeTransformEdit& edit)
	{
		if (edit.entityGuid.empty())
			return;

		SubmitCommand(std::make_unique<SetRuntimeTransformCommand>(edit));
	}

	std::vector<RuntimeMultiMeshGroupSnapshot> EngineAPIImpl::BuildRuntimeMultiMeshExpansionSnapshot()
	{
		std::vector<RuntimeMultiMeshGroupSnapshot> snapshots;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady())
			return snapshots;

		auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
		if (database == nullptr)
			return snapshots;

		const auto& groups = scene->GetMultiMeshGroups();
		snapshots.reserve(groups.size());
		for (const auto& [parentName, group] : groups)
		{
			if (group.sourceMesh == nullptr || group.childNodes.empty())
				continue;

			RuntimeMultiMeshGroupSnapshot snapshot;
			snapshot.parentName = parentName;
			snapshot.children.reserve(group.childNodes.size());

			for (VansGraphics::VansRenderNode* childNode : group.childNodes)
			{
				if (childNode == nullptr || childNode->m_SubmeshIndex == UINT32_MAX)
					continue;

				RuntimeMultiMeshChildSnapshot child;
				child.submeshIndex = childNode->m_SubmeshIndex;
				child.sourceNode = SanitizeRuntimeGeneratedMaterialText(childNode->m_Mesh ? childNode->m_Mesh->m_SourceNodeName : std::string{});
				child.sourceMaterial = SanitizeRuntimeGeneratedMaterialText(childNode->m_Material ? childNode->m_Material->m_AssetName : std::string{});
				child.materialGuid = EnsureRuntimeGeneratedMaterialAsset(parentName, childNode, database->AssetsRoot());
				if (!child.materialGuid.empty())
					snapshot.children.push_back(std::move(child));
			}

			if (!snapshot.children.empty())
				snapshots.push_back(std::move(snapshot));
		}

		return snapshots;
	}

	AudioBusDebugSnapshot EngineAPIImpl::GetAudioBusDebugSnapshot() const
	{
		AudioBusDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		const VansEngine::VansAudioManager* audioManager = scene->GetAudioManager();
		if (!audioManager)
			return snapshot;

		snapshot.available = true;
		const VansEngine::VansAudioSystem& audioSystem = VansEngine::VansAudioSystem::GetInstance();
		snapshot.audioSystemInitialized = audioSystem.IsInitialized();
		snapshot.efxSupported = audioSystem.IsEfxSupported();
		snapshot.maxActiveVoices =
			static_cast<int>(scene->GetAudioVoiceBudgetSettings().maxActiveVoices);
		snapshot.defaultReverbPreset = audioSystem.GetDefaultReverbPresetName();
		snapshot.defaultReverbWetGain = audioSystem.GetDefaultReverbWetGain();
		snapshot.activeSourceLeaseCount = static_cast<int>(audioSystem.GetActiveSourceLeaseCount());
		snapshot.pooledSourceCount = static_cast<int>(audioSystem.GetPooledSourceCount());
		const VansEngine::AudioVoiceLeaseFrameStats voiceLeaseStats =
			audioManager->GetVoiceLeaseFrameStats();
		snapshot.hardwareVoiceSuspendedThisFrame = voiceLeaseStats.suspendedThisFrame;
		snapshot.hardwareVoiceResumedThisFrame = voiceLeaseStats.resumedThisFrame;
		const std::vector<VansEngine::AudioBusDebugEntry> buses =
			audioManager->GetBusDebugSnapshot();
		snapshot.buses.reserve(buses.size());
		for (const VansEngine::AudioBusDebugEntry& bus : buses)
		{
			snapshot.buses.push_back(AudioBusDebugState{
				bus.name,
				bus.state.gain,
				bus.state.duckingGain,
				bus.effectiveGain,
				bus.activeVoiceCount,
				bus.state.muted,
				bus.state.soloed });
		}
		const std::vector<VansEngine::AudioDuckingRuleDebugEntry> duckingRules =
			audioManager->GetDuckingRuleDebugSnapshot();
		snapshot.duckingRules.reserve(duckingRules.size());
		for (const VansEngine::AudioDuckingRuleDebugEntry& entry : duckingRules)
		{
			snapshot.duckingRules.push_back(AudioDuckingRuleDebugState{
				entry.rule.triggerBusName,
				entry.rule.targetBusName,
				entry.rule.targetGain,
				entry.rule.attackSeconds,
				entry.rule.releaseSeconds,
				entry.rule.enabled,
				entry.active });
		}

		glm::vec3 listenerPosition(0.0f);
		if (VansGraphics::VansCamera* camera = scene->GetCamera())
		{
			const glm::vec4 cameraPosition = camera->GetPosition();
			listenerPosition = glm::vec3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
			snapshot.listenerAvailable = true;
			snapshot.listenerPosition = Vec3{ listenerPosition.x, listenerPosition.y, listenerPosition.z };
		}

		int selectedReverbZoneIndex = -1;
		VansEngine::AudioReverbZoneEvaluation selectedReverbEvaluation;
		const Vans::VansRuntimeWorld* runtimeWorld = scene->GetRuntimeWorld();
		if (!runtimeWorld)
			return snapshot;

		const std::vector<Vans::VansEntityHandle> entities =
			runtimeWorld->Entities().CollectAliveEntities();
		snapshot.sources.reserve(entities.size());
		snapshot.reverbZones.reserve(entities.size());
		for (Vans::VansEntityHandle entity : entities)
		{
			const Vans::VansEntityRecord* entityRecord = runtimeWorld->Entities().Get(entity);
			if (!entityRecord)
				continue;

			const std::uint32_t transformId = ResolveRuntimeEntityTransformId(*runtimeWorld, entity);
			const bool hasTransform = transformId < VansGraphics::VansTransformStore::GlobalTransforms.size();
			glm::vec3 objectPosition(0.0f);
			if (hasTransform)
			{
				const VansGraphics::VansTransform& transform =
					VansGraphics::VansTransformStore::GetTransform(transformId);
				objectPosition = glm::vec3(
					transform.m_Position.x,
					transform.m_Position.y,
					transform.m_Position.z);
			}

			const std::vector<Vans::VansComponentHandle> components =
				runtimeWorld->CollectComponentsOwnedBy(entity);
			for (Vans::VansComponentHandle component : components)
			{
				if (const auto* audio = GetRuntimeComponentPayload<Vans::VansRuntimeAudioComponent>(
					*runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Audio))
				{
					AudioSourceDebugState source;
					source.objectName = entityRecord->name;
					source.sourceName = audio->sourceName;
					source.position = Vec3{ objectPosition.x, objectPosition.y, objectPosition.z };
					if (snapshot.listenerAvailable)
					{
						const glm::vec3 delta = objectPosition - listenerPosition;
						source.listenerDistance = std::sqrt(
							delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
					}
					if (audio->sourceBinding)
					{
						source.sourceName = audio->sourceBinding->GetSourceName();
						source.busName = audio->sourceBinding->GetBusName();
						source.volume = audio->sourceBinding->GetVolume();
						source.reverbSend = audio->sourceBinding->GetReverbSend();
						source.bound = audio->sourceBinding->IsBound();
						source.playing = audio->sourceBinding->IsPlaying();
						source.paused = audio->sourceBinding->IsPaused();
						source.spatial = audio->sourceBinding->GetSpatial();
						source.usesInstance = audio->sourceBinding->UsesInstance();
						source.usesPrivateNode = audio->sourceBinding->UsesPrivateNode();
						source.hardwareVoiceActive = audio->sourceBinding->IsHardwareVoiceActive();
						source.virtualized =
							source.playing && audio->sourceBinding->GetVirtualizationGain() <= 0.0005f;
					}
					source.effectiveBusGain = audioManager->GetEffectiveBusGain(source.busName);
					source.objectActive = entityRecord->selfActive;
					source.componentEnabled = runtimeWorld->IsComponentSelfEnabled(component);
					source.dopplerEnabled = audio->dopplerEnabled;
					source.occlusionGain = audio->occlusionState.gain;
					source.occlusionHighFrequencyGain = audio->occlusionState.highFrequencyGain;
					source.occlusionMaterial = audio->occlusionSettings.material;
					source.occlusionMaterialThickness = audio->occlusionSettings.materialThickness;
					source.occlusionQueryTimer = audio->occlusionState.queryTimer;
					source.occlusionEnabled = audio->occlusionSettings.enabled;
					source.occlusionBlocked = audio->occlusionState.lastBlocked;

					snapshot.sourceCount += 1;
					if (source.bound) snapshot.boundSourceCount += 1;
					if (source.playing) snapshot.playingSourceCount += 1;
					if (source.spatial) snapshot.spatialSourceCount += 1;
					if (source.virtualized) snapshot.virtualizedSourceCount += 1;
					if (source.hardwareVoiceActive) snapshot.hardwareVoiceActiveCount += 1;
					snapshot.sources.push_back(std::move(source));
				}

				if (component.typeId == Vans::VansRuntimeComponentType_AudioReverbZone ||
					component.typeId == Vans::VansRuntimeComponentType_AudioVolume)
				{
					const auto* zone = GetRuntimeComponentPayload<Vans::VansRuntimeAudioReverbZoneComponent>(
						*runtimeWorld,
						component,
						component.typeId);
					if (!zone)
						continue;

					VansEngine::AudioReverbZoneState zoneState;
					zoneState.shape = VansEngine::AudioReverbZoneShapeFromString(zone->shape);
					zoneState.centerX = objectPosition.x;
					zoneState.centerY = objectPosition.y;
					zoneState.centerZ = objectPosition.z;
					zoneState.radius = zone->radius;
					zoneState.halfExtentX = zone->halfExtentX;
					zoneState.halfExtentY = zone->halfExtentY;
					zoneState.halfExtentZ = zone->halfExtentZ;
					zoneState.fadeDistance = zone->fadeDistance;
					zoneState.wetGain = zone->wetGain;
					zoneState.priority = zone->priority;
					zoneState.preset = VansEngine::AudioReverbPresetFromString(zone->preset);
					zoneState.presetParameters = zone->presetParameters;
					zoneState.overridePresetParameters = zone->overridePresetParameters;

					const VansEngine::AudioReverbZoneEvaluation evaluation =
						snapshot.listenerAvailable
						? VansEngine::EvaluateReverbZone(
							listenerPosition.x,
							listenerPosition.y,
							listenerPosition.z,
							zoneState)
						: VansEngine::AudioReverbZoneEvaluation{};
					if (VansEngine::ShouldSelectReverbZoneCandidate(
						selectedReverbEvaluation,
						evaluation))
					{
						selectedReverbEvaluation = evaluation;
						selectedReverbZoneIndex = static_cast<int>(snapshot.reverbZones.size());
					}

					AudioReverbZoneDebugState zoneDebug;
					zoneDebug.objectName = entityRecord->name;
					zoneDebug.componentType =
						component.typeId == Vans::VansRuntimeComponentType_AudioVolume
						? "AudioVolume"
						: "AudioReverbZone";
					zoneDebug.shape = VansEngine::AudioReverbZoneShapeToString(zoneState.shape);
					zoneDebug.preset = zone->overridePresetParameters
						? (VansEngine::AudioReverbPresetToString(zoneState.preset) + std::string(" custom"))
						: VansEngine::AudioReverbPresetToString(zoneState.preset);
					zoneDebug.position = Vec3{ objectPosition.x, objectPosition.y, objectPosition.z };
					zoneDebug.blend = evaluation.blend;
					zoneDebug.wetGain = zoneState.wetGain;
					zoneDebug.effectiveWetGain = evaluation.wetGain;
					zoneDebug.priority = zoneState.priority;
					zoneDebug.affectsListener = evaluation.affectsListener;
					snapshot.reverbZoneCount += 1;
					if (zoneDebug.affectsListener)
						snapshot.affectingReverbZoneCount += 1;
					snapshot.reverbZones.push_back(std::move(zoneDebug));
				}
			}
		}
		if (selectedReverbZoneIndex >= 0 &&
			selectedReverbZoneIndex < static_cast<int>(snapshot.reverbZones.size()))
		{
			snapshot.reverbZones[static_cast<std::size_t>(selectedReverbZoneIndex)].selected = true;
		}
		return snapshot;
	}

	void EngineAPIImpl::SetAudioBusGain(const std::string& busName, float gain)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->GetAudioManager())
			scene->GetAudioManager()->SetBusGain(busName, gain);
	}

	void EngineAPIImpl::SetAudioBusMuted(const std::string& busName, bool muted)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->GetAudioManager())
			scene->GetAudioManager()->SetBusMuted(busName, muted);
	}

	void EngineAPIImpl::SetAudioBusSoloed(const std::string& busName, bool soloed)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->GetAudioManager())
			scene->GetAudioManager()->SetBusSoloed(busName, soloed);
	}

	void EngineAPIImpl::SetAudioMaxActiveVoices(int maxActiveVoices)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene)
			scene->SetAudioMaxActiveVoices(static_cast<std::size_t>(std::max(1, maxActiveVoices)));
	}

	void EngineAPIImpl::SetRuntimePhysicsFixedTimeStep(float deltaTimeSeconds)
	{
		VansEngine::VansPhysicsSystem::GetInstance().SetFixedTimeStep(deltaTimeSeconds);
	}

	std::vector<std::string> EngineAPIImpl::GetRuntimeCollisionLayerNames() const
	{
		std::vector<std::string> names;
		auto& layers = VansEngine::VansCollisionLayerManager::Get();
		names.reserve(static_cast<std::size_t>(layers.GetLayerCount()));
		for (int index = 0; index < layers.GetLayerCount(); ++index)
			names.push_back(layers.GetLayerName(index));
		return names;
	}

	void EngineAPIImpl::InstallRuntimeVehiclePhysicsStepCallback()
	{
		VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback([this](float dt)
		{
			StepRuntimeVehicle(dt);
		});
	}

	void EngineAPIImpl::ClearRuntimePhysicsStepCallback()
	{
		VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);
	}

	bool EngineAPIImpl::IsRuntimePhysicsRunning() const
	{
		return VansEngine::VansPhysicsSystem::GetInstance().IsSimulationRunning();
	}

	void EngineAPIImpl::StartRuntimePhysicsIfNeeded()
	{
		auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
		if (!physics.IsSimulationRunning())
			physics.StartSimulation();
		else
			physics.ResumeSimulation();
	}

	void EngineAPIImpl::PauseRuntimePhysics()
	{
		VansEngine::VansPhysicsSystem::GetInstance().PauseSimulation();
	}

	void EngineAPIImpl::ResumeRuntimePhysics()
	{
		VansEngine::VansPhysicsSystem::GetInstance().ResumeSimulation();
	}

	void EngineAPIImpl::StepRuntimeVehicle(float deltaTimeSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->GetVehicle())
			return;

		scene->GetVehicle()->Step(deltaTimeSeconds);
	}

	void EngineAPIImpl::SetRuntimeVehicleInput(float throttle, float brake, float steer, float handbrake)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady() || !scene->GetVehicle())
			return;

		auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());
		scene->GetVehicle()->SetInputs(throttle, brake, steer, handbrake);
	}

	void EngineAPIImpl::SyncRuntimePhysicsTransforms()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady())
			return;

		scene->UpdatePhysicsTransforms();
	}

	void EngineAPIImpl::FlushRuntimeCharacterControllerTransforms()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady())
			return;

		scene->UpdateCharControllerTransforms();
	}

	void EngineAPIImpl::UpdateRuntimeNonCameraScripts()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady() || !m_ScriptContext)
			return;

		m_ScriptContext->SetScene(scene);
		m_ScriptContext->VansScriptUpdateNonCameraScripts();
	}

	void EngineAPIImpl::UpdateRuntimeCameraScripts()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady() || !m_ScriptContext)
			return;

		m_ScriptContext->SetScene(scene);
		m_ScriptContext->VansScriptUpdateCameraScripts();
	}

	void EngineAPIImpl::InitializeRuntimeScripts()
	{
		if (!m_ScriptContext)
			m_ScriptContext = &GetDefaultScriptContext();
		m_ScriptContext->VansScriptSetup();
	}

	void EngineAPIImpl::SetupRuntimeScriptProjectVenv(const std::string& projectRootPath)
	{
		if (!m_ScriptContext)
			m_ScriptContext = &GetDefaultScriptContext();
		m_ScriptContext->SetActiveProjectRoot(projectRootPath);
	}

	void EngineAPIImpl::ReloadRuntimeScripts()
	{
		if (m_ScriptContext)
			m_ScriptContext->ReloadAllLuaScripts();
	}

	void EngineAPIImpl::ReloadRuntimeScriptModule()
	{
		if (m_ScriptContext)
			m_ScriptContext->ReloadAllLuaScripts();
	}

	bool EngineAPIImpl::CanUndo() const
	{
		return !m_UndoStack.empty();
	}

	bool EngineAPIImpl::CanRedo() const
	{
		return !m_RedoStack.empty();
	}

	void EngineAPIImpl::Undo()
	{
		if (m_UndoStack.empty())
			return;

		std::unique_ptr<IEngineCommand> command = std::move(m_UndoStack.back());
		m_UndoStack.pop_back();
		EngineCommandContext context(m_Scene, m_Device);
		command->Undo(context);
		m_RedoStack.push_back(std::move(command));
		m_AllowNextCommandMerge = false;
	}

	void EngineAPIImpl::Redo()
	{
		if (m_RedoStack.empty())
			return;

		std::unique_ptr<IEngineCommand> command = std::move(m_RedoStack.back());
		m_RedoStack.pop_back();
		EngineCommandContext context(m_Scene, m_Device);
		command->Execute(context);
		m_UndoStack.push_back(std::move(command));
		m_AllowNextCommandMerge = false;
	}

}

