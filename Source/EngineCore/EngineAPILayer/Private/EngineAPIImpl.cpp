#include "EngineAPIImpl.h"

#include "EngineCommandContext.h"
#include "ModelAssetPlacementPreparationService.h"
#include "VansMaterialLiveEditService.h"
#include "VansEditorTextureBridge.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansCamera.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/ReflectionProbeCore/VansReflectionProbeSystem.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../RenderCore/TerrainCore/VansTerrain.h"
#include "../../RenderCore/WaterCore/VansWaterFFT.h"
#include "../../RenderCore/WaterCore/VansWaterSystem.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansVKImage.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansPhysicsVehicle.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneEntityFactory.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../Util/VansLog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
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

		std::uint32_t ExtractSummaryCount(const std::string& summary, const std::string& key)
		{
			const size_t keyPos = summary.find(key);
			if (keyPos == std::string::npos)
				return 0;

			size_t valuePos = keyPos + key.size();
			while (valuePos < summary.size() && std::isspace(static_cast<unsigned char>(summary[valuePos])))
				++valuePos;

			std::uint32_t value = 0;
			while (valuePos < summary.size() && std::isdigit(static_cast<unsigned char>(summary[valuePos])))
			{
				value = value * 10u + static_cast<std::uint32_t>(summary[valuePos] - '0');
				++valuePos;
			}
			return value;
		}

		std::uint32_t ExtractSummaryCountAfter(
			const std::string& summary,
			const std::string& anchor,
			const std::string& key)
		{
			const size_t anchorPos = summary.find(anchor);
			if (anchorPos == std::string::npos)
				return 0;

			const size_t keyPos = summary.find(key, anchorPos);
			if (keyPos == std::string::npos)
				return 0;

			size_t valuePos = keyPos + key.size();
			while (valuePos < summary.size() && std::isspace(static_cast<unsigned char>(summary[valuePos])))
				++valuePos;

			std::uint32_t value = 0;
			while (valuePos < summary.size() && std::isdigit(static_cast<unsigned char>(summary[valuePos])))
			{
				value = value * 10u + static_cast<std::uint32_t>(summary[valuePos] - '0');
				++valuePos;
			}
			return value;
		}

		bool SummaryContainsFlag(const std::string& summary, const std::string& key, bool expected)
		{
			const std::string pattern = key + (expected ? "true" : "false");
			return summary.find(pattern) != std::string::npos;
		}

		void ClearEditorRenderTexturePreviewCaches(VkDevice device)
		{
			for (auto& cache : GetImagePreviewCaches())
			{
				Vans::Editor::VansEditorTextureBridge::RemoveTexture(cache.texture);
				cache = {};
			}
			GetImagePreviewCaches().clear();

			for (auto& cache : GetLayerPreviewCaches())
			{
				Vans::Editor::VansEditorTextureBridge::RemoveTexture(cache.texture);
				if (device != VK_NULL_HANDLE)
					VansGraphics::VansVKImage::DestroyImageView(device, cache.view);
				cache = {};
			}
			GetLayerPreviewCaches().clear();

			auto& viewportCache = GetViewportPreviewCache();
			Vans::Editor::VansEditorTextureBridge::RemoveTexture(viewportCache.texture);
			viewportCache = {};

			auto& reflectionCache = GetReflectionProbePreviewCache();
			Vans::Editor::VansEditorTextureBridge::RemoveTexture(reflectionCache.texture);
			reflectionCache = {};
		}

		RenderTexturePreview BuildImagePreview(
			RenderTextureId id,
			const char* name,
			VansGraphics::VansVKImage& image,
			VkImageLayout layout)
		{
			RenderTexturePreview preview;
			preview.id = id;
			preview.name = name ? name : "";

			VkImageView imageView = image.GetImageView();
			VkSampler sampler = image.GetSampler();
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
				Vans::Editor::VansEditorTextureBridge::RemoveTexture(it->texture);
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
			VkDevice device,
			RenderTextureId id,
			const char* name,
			VansGraphics::VansVKImage& image,
			std::uint32_t requestedLayer,
			VkImageLayout layout)
		{
			RenderTexturePreview preview;
			preview.id = id;
			preview.name = name ? name : "";

			if (device == VK_NULL_HANDLE || image.GetImage() == VK_NULL_HANDLE || image.GetSampler() == VK_NULL_HANDLE)
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
				Vans::Editor::VansEditorTextureBridge::RemoveTexture(it->texture);
				it->texture = nullptr;
				if (it->view != VK_NULL_HANDLE)
				{
					VansGraphics::VansVKImage::DestroyImageView(device, it->view);
					it->view = VK_NULL_HANDLE;
				}

				it->view = image.CreateLayerMipView(device, layer, 0);
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
			default: return AssetType::Unknown;
			}
		}

		const nlohmann::ordered_json* FindRuntimePatchComponent(
			const nlohmann::ordered_json& entity,
			const std::string& type)
		{
			if (!entity.contains("components") || !entity["components"].is_array())
				return nullptr;

			for (const nlohmann::ordered_json& component : entity["components"])
			{
				if (component.value("type", "") == type)
					return &component;
			}
			return nullptr;
		}

		bool ReadJsonVec3(const nlohmann::ordered_json& value, Vec3& out)
		{
			if (!value.is_array() || value.size() < 3)
				return false;

			out = {
				value[0].get<float>(),
				value[1].get<float>(),
				value[2].get<float>()
			};
			return true;
		}

		bool ReadJsonRotationEuler(const nlohmann::ordered_json& value, Vec3& out)
		{
			if (!value.is_array())
				return false;

			if (value.size() == 4)
			{
				const glm::quat q(
					value[3].get<float>(),
					value[0].get<float>(),
					value[1].get<float>(),
					value[2].get<float>());
				out = ToEditorVec3(glm::degrees(glm::eulerAngles(q)));
				return true;
			}

			return ReadJsonVec3(value, out);
		}

		bool BuildRuntimeTransformEditFromPatch(
			const nlohmann::ordered_json& entity,
			RuntimeTransformEdit& edit)
		{
			const std::string entityGuid = entity.value("id", "");
			if (entityGuid.empty())
				return false;

			const nlohmann::ordered_json* transformComponent =
				FindRuntimePatchComponent(entity, "Transform");
			if (!transformComponent || !transformComponent->value("enabled", true) ||
				!transformComponent->contains("data"))
			{
				return false;
			}

			const nlohmann::ordered_json& data = (*transformComponent)["data"];
			edit = {};
			edit.entityGuid = entityGuid;
			edit.writePosition = false;
			edit.writeRotation = false;
			edit.writeScale = false;

			if (data.contains("position") && ReadJsonVec3(data["position"], edit.position))
				edit.writePosition = true;
			if (data.contains("rotation") && ReadJsonRotationEuler(data["rotation"], edit.rotationDegrees))
				edit.writeRotation = true;
			if (data.contains("scale") && ReadJsonVec3(data["scale"], edit.scale))
				edit.writeScale = true;

			return edit.writePosition || edit.writeRotation || edit.writeScale;
		}

		enum class RuntimeLightPatchType
		{
			Directional,
			Point,
			Spot,
			Rect
		};

		struct RuntimeLightPatch
		{
			RuntimeLightPatchType type = RuntimeLightPatchType::Directional;
			std::string entityGuid;
			bool writeColor = false;
			bool writeIntensity = false;
			bool writeRadius = false;
			bool writeInnerCutoff = false;
			bool writeOuterCutoff = false;
			bool writeRectWidth = false;
			bool writeRectHeight = false;
			bool writeRectRange = false;
			bool writeRectTwoSided = false;
			bool writeRectShadow = false;
			Vec3 color;
			float intensity = 0.0f;
			float radius = 0.0f;
			float innerCutoffRadians = 0.0f;
			float outerCutoffRadians = 0.0f;
			float rectWidth = 0.0f;
			float rectHeight = 0.0f;
			float rectRange = 0.0f;
			float rectTwoSided = 0.0f;
			float rectShadowIndex = -1.0f;
		};

		struct RuntimeLightBinding
		{
			VansGraphics::VansLightManager* manager = nullptr;
			int index = -1;
		};

		bool ReadPatchColor(const nlohmann::ordered_json& data, Vec3& out)
		{
			const auto it = data.find("color");
			return it != data.end() && ReadJsonVec3(*it, out);
		}

		bool AppendLightPatchFromComponent(
			const nlohmann::ordered_json& entity,
			const char* componentType,
			RuntimeLightPatchType patchType,
			std::vector<RuntimeLightPatch>& patches)
		{
			const nlohmann::ordered_json* component =
				FindRuntimePatchComponent(entity, componentType);
			if (!component || !component->value("enabled", true) || !component->contains("data"))
				return false;

			const nlohmann::ordered_json& data = (*component)["data"];
			RuntimeLightPatch patch;
			patch.type = patchType;
			patch.entityGuid = entity.value("id", "");
			if (patch.entityGuid.empty())
				return false;

			if (ReadPatchColor(data, patch.color))
				patch.writeColor = true;
			if (data.contains("intensity"))
			{
				patch.intensity = data.value("intensity", 0.0f);
				patch.writeIntensity = true;
			}

			if (patchType == RuntimeLightPatchType::Point || patchType == RuntimeLightPatchType::Spot)
			{
				if (data.contains("radius"))
				{
					patch.radius = data.value("radius", 0.0f);
					patch.writeRadius = true;
				}
			}

			if (patchType == RuntimeLightPatchType::Spot)
			{
				if (data.contains("innercutoff"))
				{
					patch.innerCutoffRadians = glm::radians(data.value("innercutoff", 0.0f));
					patch.writeInnerCutoff = true;
				}
				if (data.contains("outerCutoff"))
				{
					patch.outerCutoffRadians = glm::radians(data.value("outerCutoff", 0.0f));
					patch.writeOuterCutoff = true;
				}
			}

			if (patchType == RuntimeLightPatchType::Rect)
			{
				if (data.contains("width"))
				{
					patch.rectWidth = data.value("width", 0.0f);
					patch.writeRectWidth = true;
				}
				if (data.contains("height"))
				{
					patch.rectHeight = data.value("height", 0.0f);
					patch.writeRectHeight = true;
				}
				if (data.contains("range"))
				{
					patch.rectRange = data.value("range", 0.0f);
					patch.writeRectRange = true;
				}
				if (data.contains("two_sided"))
				{
					patch.rectTwoSided = data.value("two_sided", false) ? 1.0f : 0.0f;
					patch.writeRectTwoSided = true;
				}
				if (data.contains("shadow"))
				{
					patch.rectShadowIndex = data.value("shadow", false) ? 0.0f : -1.0f;
					patch.writeRectShadow = true;
				}
			}

			if (patch.writeColor || patch.writeIntensity || patch.writeRadius ||
				patch.writeInnerCutoff || patch.writeOuterCutoff ||
				patch.writeRectWidth || patch.writeRectHeight || patch.writeRectRange ||
				patch.writeRectTwoSided || patch.writeRectShadow)
			{
				patches.push_back(patch);
				return true;
			}
			return false;
		}

		std::vector<RuntimeLightPatch> BuildRuntimeLightPatchesFromPatch(
			const nlohmann::ordered_json& entity)
		{
			std::vector<RuntimeLightPatch> patches;
			AppendLightPatchFromComponent(entity, "DirectionalLight", RuntimeLightPatchType::Directional, patches);
			AppendLightPatchFromComponent(entity, "PointLight", RuntimeLightPatchType::Point, patches);
			AppendLightPatchFromComponent(entity, "SpotLight", RuntimeLightPatchType::Spot, patches);
			AppendLightPatchFromComponent(entity, "RectLight", RuntimeLightPatchType::Rect, patches);
			return patches;
		}

		std::uint32_t ResolveRuntimeTransformId(
			VansGraphics::VansScene* scene,
			const std::string& entityGuid)
		{
			if (!scene || entityGuid.empty())
				return UINT32_MAX;

			if (VansGraphics::VansRenderNode* node = scene->FindPrimaryRenderNodeByEntityGuid(entityGuid))
				return node->m_TransformID;
			if (VansScriptObject* obj = scene->FindObjectByGuid(entityGuid))
				return obj->m_TransformID;
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

			VansScriptObject* obj = scene->FindObjectByGuid(patch.entityGuid);
			if (!obj)
				return binding;

			switch (patch.type)
			{
			case RuntimeLightPatchType::Directional:
				if (auto* component = obj->GetComponent<VansScriptDirectionalLightComponent>())
					binding = { component->m_LightManager, component->m_LightIndex };
				break;
			case RuntimeLightPatchType::Point:
				if (auto* component = obj->GetComponent<VansScriptPointLightComponent>())
					binding = { component->m_LightManager, component->m_LightIndex };
				break;
			case RuntimeLightPatchType::Spot:
				if (auto* component = obj->GetComponent<VansScriptSpotLightComponent>())
					binding = { component->m_LightManager, component->m_LightIndex };
				break;
			case RuntimeLightPatchType::Rect:
				if (auto* component = obj->GetComponent<VansScriptRectLightComponent>())
					binding = { component->m_LightManager, component->m_LightIndex };
				break;
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
					light.m_ShadowIndex = patch.rectShadowIndex;
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

			const auto& pointLights = lightManager->GetPointLights();
			snapshot.pointLights.reserve(pointLights.size());
			for (const auto& source : pointLights)
			{
				PointLightSettings light;
				light.position = ToEditorVec3(source.m_Position);
				light.color = ToEditorVec3(source.m_Color);
				light.intensity = source.m_Intensity;
				light.radius = source.m_Radius;
				snapshot.pointLights.push_back(light);
			}

			const auto& spotLights = lightManager->GetSpotLight();
			snapshot.spotLights.reserve(spotLights.size());
			for (const auto& source : spotLights)
			{
				SpotLightSettings light;
				light.position = ToEditorVec3(source.m_Position);
				light.direction = ToEditorVec3(source.m_Direction);
				light.color = ToEditorVec3(source.m_Color);
				light.intensity = source.m_Intensity;
				light.radius = source.m_Radius;
				light.innerCutoffRadians = source.m_InnerCutOff;
				light.outerCutoffRadians = source.m_OuterCutOff;
				snapshot.spotLights.push_back(light);
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

			auto& pointLights = lightManager->GetPointLights();
			const std::size_t pointCount = std::min(pointLights.size(), settings.pointLights.size());
			for (std::size_t i = 0; i < pointCount; ++i)
			{
				const PointLightSettings& source = settings.pointLights[i];
				pointLights[i].m_Position = ToRuntimeVec3(source.position);
				pointLights[i].m_Color = ToRuntimeVec3(source.color);
				pointLights[i].m_Intensity = source.intensity;
				pointLights[i].m_Radius = source.radius;
			}

			auto& spotLights = lightManager->GetSpotLight();
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

		std::string SafeRuntimeAssetName(std::string value)
		{
			if (value.empty())
				value = "Unnamed";
			for (char& c : value)
			{
				const unsigned char uc = static_cast<unsigned char>(c);
				if (!std::isalnum(uc) && c != '_' && c != '-')
					c = '_';
			}
			while (!value.empty() && value.front() == '_') value.erase(value.begin());
			while (!value.empty() && value.back() == '_') value.pop_back();
			if (value.empty())
				value = "Unnamed";
			if (value.size() > 96)
				value.resize(96);
			return value;
		}

		std::string SanitizeRuntimeJsonText(std::string value)
		{
			for (char& c : value)
			{
				const unsigned char uc = static_cast<unsigned char>(c);
				if (uc < 0x20 || uc >= 0x7f)
					c = '_';
			}
			return value;
		}

		Vans::SceneJson Vec3Json(const glm::vec3& value)
		{
			return Vans::SceneJson::array({ value.x, value.y, value.z });
		}

		Vans::VansAssetGuid ReadOrCreateMetaGuid(const std::filesystem::path& metaPath)
		{
			std::ifstream input(metaPath);
			if (input)
			{
				const auto meta = Vans::SceneJson::parse(input, nullptr, false);
				if (!meta.is_discarded() && meta.is_object() && meta.contains("guid") && meta["guid"].is_string())
				{
					Vans::VansAssetGuid parsed;
					if (Vans::VansAssetGuid::TryParse(meta["guid"].get<std::string>(), parsed))
						return parsed;
				}
			}
			return Vans::VansAssetGuid::New();
		}

		bool WriteJsonFile(const std::filesystem::path& path, const Vans::SceneJson& json)
		{
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output)
				return false;
			output << json.dump(4);
			return static_cast<bool>(output);
		}

		bool IsGuidString(const std::string& value)
		{
			Vans::VansAssetGuid parsed;
			return Vans::VansAssetGuid::TryParse(value, parsed);
		}

		std::string LowerAscii(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		std::string ResolveRuntimeTextureGuid(
			const std::string& textureName,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			if (textureName.empty())
				return {};
			if (IsGuidString(textureName))
				return textureName;
			if (database == nullptr)
				return {};

			const std::string wanted = LowerAscii(std::filesystem::path(textureName).stem().string());
			const std::string rootToken = LowerAscii(SafeRuntimeAssetName(rootName));
			std::string fallbackGuid;
			for (const Vans::VansAssetRecord& record : database->All())
			{
				if (record.type != Vans::VansAssetType::Texture || record.state == Vans::VansAssetState::Missing)
					continue;
				const std::string recordStem = LowerAscii(record.sourcePath.stem().string());
				const std::string recordFile = LowerAscii(record.sourcePath.filename().string());
				if (recordStem != wanted && recordFile != LowerAscii(textureName))
					continue;

				if (fallbackGuid.empty())
					fallbackGuid = record.guid.ToString();
				const std::string recordPath = LowerAscii(record.sourcePath.generic_string());
				if (!rootToken.empty() && recordPath.find(rootToken) != std::string::npos)
					return record.guid.ToString();
			}
			return fallbackGuid;
		}

		std::string ResolveRuntimeTextureGuid(
			VansGraphics::VansTexture* texture,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			return texture ? ResolveRuntimeTextureGuid(texture->m_AssetName, database, rootName) : std::string{};
		}

		bool IsDefaultRuntimeTextureName(const std::string& textureName)
		{
			const std::string lowered = LowerAscii(std::filesystem::path(textureName).stem().string());
			return lowered == "defaultalbedo" ||
				   lowered == "defaultnormal" ||
				   lowered == "defaultmetal" ||
				   lowered == "defaultroughness" ||
				   lowered == "defaultao";
		}

		void AddTextureRefIfResolvable(
			Vans::SceneJson& textures,
			const char* slot,
			VansGraphics::VansTexture* texture,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			const std::string textureGuid = ResolveRuntimeTextureGuid(texture, database, rootName);
			if (!textureGuid.empty())
				textures[slot] = { { "guid", textureGuid } };
		}

		void AddTextureRefFromPathIfResolvable(
			Vans::SceneJson& textures,
			const char* slot,
			const std::string& texturePath,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			const std::string textureGuid = ResolveRuntimeTextureGuid(texturePath, database, rootName);
			if (!textureGuid.empty())
				textures[slot] = { { "guid", textureGuid } };
		}

		Vans::SceneJson SerializeFbxMaterialInfo(
			const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			Vans::SceneJson json;

			if (fbxInfo.IsTransparent())
			{
				json["materialType"] = "transparent";
				json["parameters"] = Vans::SceneJson::object();
				json["textures"] = Vans::SceneJson::array();

				auto addTransparentTexture = [&](const char* slot, const std::string& texturePath)
				{
					const std::string textureGuid = ResolveRuntimeTextureGuid(texturePath, database, rootName);
					if (textureGuid.empty())
						return;
					json["textures"].push_back({
						{ "slot", slot },
						{ "texture", { { "guid", textureGuid } } }
					});
				};

				addTransparentTexture("diffuse", fbxInfo.diffuseTexPath);
				addTransparentTexture("opacity", fbxInfo.opacityTexPath);
				return json;
			}

			json["materialType"] = "pbr";
			json["parameters"] = {
				{ "albedo", Vans::SceneJson::array({
					fbxInfo.diffuseColor[0],
					fbxInfo.diffuseColor[1],
					fbxInfo.diffuseColor[2] }) },
				{ "metallic", fbxInfo.metallic },
				{ "roughness", fbxInfo.roughness },
				{ "ao", 1.0f }
			};

			Vans::SceneJson textures = Vans::SceneJson::object();
			AddTextureRefFromPathIfResolvable(textures, "basecolor", fbxInfo.diffuseTexPath, database, rootName);
			AddTextureRefFromPathIfResolvable(textures, "normal", fbxInfo.normalTexPath, database, rootName);
			AddTextureRefFromPathIfResolvable(textures, "metal", fbxInfo.metallicTexPath, database, rootName);
			AddTextureRefFromPathIfResolvable(textures, "roughness", fbxInfo.roughnessTexPath, database, rootName);
			AddTextureRefFromPathIfResolvable(textures, "ao", fbxInfo.aoTexPath, database, rootName);
			json["textures"] = std::move(textures);
			return json;
		}

		Vans::SceneJson SerializeRuntimeMaterial(
			VansGraphics::VansMaterial* material,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			Vans::SceneJson json;

			if (auto* pbr = dynamic_cast<VansGraphics::VansPBRMaterial*>(material))
			{
				json["materialType"] = "pbr";
				json["parameters"] = {
					{ "albedo", Vec3Json(pbr->m_BasePBRParam.m_albedo) },
					{ "metallic", pbr->m_BasePBRParam.m_metallic },
					{ "roughness", pbr->m_BasePBRParam.m_roughness },
					{ "ao", pbr->m_BasePBRParam.m_ao }
				};
				Vans::SceneJson textures = Vans::SceneJson::object();
				AddTextureRefIfResolvable(textures, "basecolor", pbr->m_BaseColorTexture, database, rootName);
				AddTextureRefIfResolvable(textures, "normal", pbr->m_NormalTexture, database, rootName);
				AddTextureRefIfResolvable(textures, "metal", pbr->m_MetalTexture, database, rootName);
				AddTextureRefIfResolvable(textures, "roughness", pbr->m_RoughnessTexture, database, rootName);
				AddTextureRefIfResolvable(textures, "ao", pbr->m_AoTexture, database, rootName);
				json["textures"] = std::move(textures);
				return json;
			}

			if (auto* transparent = dynamic_cast<VansGraphics::VansTransparentMaterial*>(material))
			{
				json["materialType"] = "transparent";
				json["parameters"] = Vans::SceneJson::object();
				json["textures"] = Vans::SceneJson::array();
				const size_t textureCount = std::max(
					transparent->m_TransparentTextures.size(),
					transparent->m_TransparentTextureMap.size());
				for (size_t index = 0; index < textureCount; ++index)
				{
					const std::string slot = index < transparent->m_TransparentTextureMap.size()
						? transparent->m_TransparentTextureMap[index].first
						: "texture_" + std::to_string(index);
					std::string textureName = index < transparent->m_TransparentTextureMap.size()
						? transparent->m_TransparentTextureMap[index].second
						: std::string{};
					if (textureName.empty() && index < transparent->m_TransparentTextures.size()
						&& transparent->m_TransparentTextures[index] != nullptr)
					{
						textureName = transparent->m_TransparentTextures[index]->m_AssetName;
					}
					if (textureName.empty())
						continue;

					Vans::SceneJson textureEntry = { { "slot", slot } };
					const std::string textureGuid = ResolveRuntimeTextureGuid(textureName, database, rootName);
					if (!textureGuid.empty())
					{
						textureEntry["texture"] = { { "guid", textureGuid } };
						json["textures"].push_back(std::move(textureEntry));
					}
				}
				return json;
			}

			json["materialType"] = "pbr";
			json["parameters"] = {
				{ "albedo", Vans::SceneJson::array({ 1.0f, 1.0f, 1.0f }) },
				{ "metallic", 0.0f },
				{ "roughness", 0.5f },
				{ "ao", 1.0f }
			};
			json["textures"] = Vans::SceneJson::object();
			return json;
		}

		bool HasTextureSlot(const Vans::SceneJson& materialJson, const std::string& slot)
		{
			const auto texturesIt = materialJson.find("textures");
			if (texturesIt == materialJson.end())
				return false;

			if (texturesIt->is_object())
				return texturesIt->contains(slot);

			if (texturesIt->is_array())
			{
				for (const auto& entry : *texturesIt)
				{
					if (entry.is_object() && entry.value("slot", "") == slot)
						return true;
				}
			}

			return false;
		}

		void AddRuntimeBaseColorFallback(
			Vans::SceneJson& materialJson,
			VansGraphics::VansMaterial* material,
			Vans::VansAssetDatabase* database,
			const std::string& rootName)
		{
			if (material == nullptr || database == nullptr)
				return;

			const std::string materialType = materialJson.value("materialType", "pbr");
			if (materialType == "transparent")
			{
				if (HasTextureSlot(materialJson, "diffuse"))
					return;

				auto* transparent = dynamic_cast<VansGraphics::VansTransparentMaterial*>(material);
				if (transparent == nullptr)
					return;

				std::string textureName;
				for (const auto& [slot, name] : transparent->m_TransparentTextureMap)
				{
					if (slot == "diffuse" || slot == "basecolor" || slot == "baseColor")
					{
						textureName = name;
						break;
					}
				}
				if (textureName.empty() && !transparent->m_TransparentTextures.empty()
					&& transparent->m_TransparentTextures[0] != nullptr)
				{
					textureName = transparent->m_TransparentTextures[0]->m_AssetName;
				}
				if (IsDefaultRuntimeTextureName(textureName))
				{
					return;
				}

				const std::string textureGuid = ResolveRuntimeTextureGuid(textureName, database, rootName);
				if (!textureGuid.empty())
				{
					if (!materialJson["textures"].is_array())
						materialJson["textures"] = Vans::SceneJson::array();
					materialJson["textures"].push_back({
						{ "slot", "diffuse" },
						{ "texture", { { "guid", textureGuid } } }
					});
				}
				return;
			}

			if (HasTextureSlot(materialJson, "basecolor"))
				return;

			auto* pbr = dynamic_cast<VansGraphics::VansPBRMaterial*>(material);
			if (pbr == nullptr || pbr->m_BaseColorTexture == nullptr)
				return;

			const std::string runtimeTextureName = pbr->m_BaseColorTexture->m_AssetName;
			if (IsDefaultRuntimeTextureName(runtimeTextureName))
			{
				return;
			}

			const std::string textureGuid = ResolveRuntimeTextureGuid(pbr->m_BaseColorTexture, database, rootName);
			if (!textureGuid.empty())
			{
				if (!materialJson["textures"].is_object())
					materialJson["textures"] = Vans::SceneJson::object();
				materialJson["textures"]["basecolor"] = { { "guid", textureGuid } };
			}
			else
			{
				VANS_LOG_WARN("[MultiMeshMaterialGen] Runtime basecolor fallback unresolved for "
					<< rootName << " material=" << material->m_AssetName
					<< " texture=" << runtimeTextureName);
			}
		}

		std::string EnsureRuntimeGeneratedMaterialAsset(
			const std::string& rootName,
			VansGraphics::VansRenderNode* node,
			const std::filesystem::path& assetsRoot)
		{
			if (node == nullptr || node->m_Material == nullptr)
				return {};

			const std::string materialName = SafeRuntimeAssetName(
				rootName + "_" + node->m_Material->m_AssetName + "_" + std::to_string(node->m_SubmeshIndex));
			const std::filesystem::path materialDir = assetsRoot / "Generated" / "MultiMeshMaterials" / SafeRuntimeAssetName(rootName);
			const std::filesystem::path materialPath = materialDir / (materialName + ".mat");
			const std::filesystem::path metaPath = materialPath.string() + ".meta";
			const Vans::VansAssetGuid guid = ReadOrCreateMetaGuid(metaPath);

			Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
			Vans::SceneJson materialJson;
			if (node->m_SourceMesh != nullptr &&
				node->m_SubmeshIndex != UINT32_MAX &&
				!node->m_SourceMesh->m_SubmeshMaterialInfos.empty())
			{
				const auto& materialInfos = node->m_SourceMesh->m_SubmeshMaterialInfos;
				const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo =
					node->m_SubmeshIndex < materialInfos.size() ? materialInfos[node->m_SubmeshIndex] : materialInfos[0];
				materialJson = SerializeFbxMaterialInfo(fbxInfo, database, rootName);
				AddRuntimeBaseColorFallback(materialJson, node->m_Material, database, rootName);
			}
			else
			{
				materialJson = SerializeRuntimeMaterial(node->m_Material, database, rootName);
			}
			materialJson["guid"] = guid.ToString();
			materialJson["importSource"] = {
				{ "model", rootName },
				{ "sourceNode", SanitizeRuntimeJsonText(node->m_Mesh ? node->m_Mesh->m_SourceNodeName : std::string{}) },
				{ "sourceMaterial", SanitizeRuntimeJsonText(node->m_Material->m_AssetName) },
				{ "submeshIndex", node->m_SubmeshIndex },
				{ "generatedFor", "runtimeMultiMeshExpansion" }
			};
			if (!WriteJsonFile(materialPath, materialJson))
				return {};

			Vans::SceneJson metaJson = {
				{ "guid", guid.ToString() },
				{ "importer", "MaterialImporter" },
				{ "version", 1u },
				{ "settings", {
					{ "generatedFrom", rootName },
					{ "generatedFor", "runtimeMultiMeshExpansion" }
				} },
				{ "subAssets", Vans::SceneJson::object() }
			};
			if (!WriteJsonFile(metaPath, metaJson))
				return {};

			return guid.ToString();
		}

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
			sample.rawHitPosition = ToEditorVec3(source.rawHitPosition);
			sample.hasHit = source.hasHit;
			sample.hasRawHit = source.hasRawHit;
			sample.accepted = source.accepted;
			sample.quality = source.quality;
			sample.hitLayer = source.hitLayer;
			sample.rawHitLayer = source.rawHitLayer;
			sample.hitActorName = source.hitActorName;
			sample.rawHitActorName = source.rawHitActorName;
			sample.status = source.status;
			return sample;
		}

		FootIKDebugLegSnapshot ToFootIKDebugLeg(const VansGraphics::FootPlacementDebugLeg& source)
		{
			FootIKDebugLegSnapshot leg;
			leg.hip = ToEditorVec3(source.hip);
			leg.knee = ToEditorVec3(source.knee);
			leg.foot = ToEditorVec3(source.foot);
			leg.target = ToEditorVec3(source.target);
			leg.contact = ToEditorVec3(source.contact);
			leg.normal = ToEditorVec3(source.normal);
			leg.overlapCenter = ToEditorVec3(source.overlapCenter);
			leg.overlapHalfExtents = ToEditorVec3(source.overlapHalfExtents);
			leg.hasContact = source.hasContact;
			leg.hasTarget = source.hasTarget;
			leg.hasOverlap = source.hasOverlap;
			leg.targetWeight = source.targetWeight;
			leg.overlapLayer = source.overlapLayer;
			leg.overlapActorName = source.overlapActorName;
			leg.samples.reserve(source.samples.size());
			for (const auto& sample : source.samples)
				leg.samples.push_back(ToFootIKDebugSample(sample));
			return leg;
		}

		WaterSettingsSnapshot ToWaterSettings(const VansGraphics::VansWaterConfig& source)
		{
			WaterSettingsSnapshot settings;
			settings.available = true;
			settings.type = static_cast<int>(source.m_Type);
			settings.waterLevel = source.m_WaterLevel;
			settings.specularIntensity = source.m_SpecularIntensity;

			settings.medium.absorptionCoeff = ToEditorVec3(source.m_Medium.m_AbsorptionCoeff);
			settings.medium.scatteringCoeff = ToEditorVec3(source.m_Medium.m_ScatteringCoeff);
			settings.medium.ior = source.m_Medium.m_IOR;
			settings.medium.fresnelPower = source.m_Medium.m_FresnelPower;
			settings.medium.anisotropy = source.m_Medium.m_Anisotropy;
			settings.medium.waterRoughness = source.m_Medium.m_WaterRoughness;
			settings.medium.deepColor = ToEditorVec4(source.m_Medium.m_DeepColor);
			settings.medium.shallowColor = ToEditorVec4(source.m_Medium.m_ShallowColor);

			settings.lod.maxLod = source.m_LOD.m_MaxLOD;
			settings.lod.basePatchSize = source.m_LOD.m_BasePatchSize;
			settings.lod.meshDim = source.m_LOD.m_MeshDim;
			settings.lod.detailBalance = source.m_LOD.m_DetailBalance;
			settings.lod.morphWidthRatio = source.m_LOD.m_MorphWidthRatio;

			settings.waves.mode = static_cast<int>(source.m_Waves.m_Mode);
			settings.waves.baseScale = source.m_Waves.m_BaseScale;
			settings.waves.maxLod = source.m_Waves.m_MaxLOD;
			settings.waves.windDirection = ToEditorVec2(source.m_Waves.m_WindDirection);
			settings.waves.windSpeed = source.m_Waves.m_WindSpeed;
			settings.waves.swellAmplitude = source.m_Waves.m_SwellAmplitude;
			settings.waves.chopScale = source.m_Waves.m_ChopScale;
			settings.waves.gerstnerWaveCount = source.m_Waves.m_GerstnerWaveCount;
			settings.waves.fftLodCount = source.m_Waves.m_FftLODCount;
			settings.waves.fftResolution = source.m_Waves.m_FftResolution;
			settings.waves.fft.useDerivativeNormal = source.m_Waves.m_FFT.m_UseDerivativeNormal;
			settings.waves.fft.resolution = source.m_Waves.m_FFT.m_Resolution;
			settings.waves.fft.lodCount = source.m_Waves.m_FFT.m_LODCount;
			settings.waves.fft.spectrumAmplitude = source.m_Waves.m_FFT.m_SpectrumAmplitude;
			settings.waves.fft.choppiness = source.m_Waves.m_FFT.m_Choppiness;
			settings.waves.fft.smallWaveDamping = source.m_Waves.m_FFT.m_SmallWaveDamping;
			settings.waves.fft.windDependency = source.m_Waves.m_FFT.m_WindDependency;
			settings.waves.fft.depth = source.m_Waves.m_FFT.m_Depth;
			settings.waves.fft.repeatPeriod = source.m_Waves.m_FFT.m_RepeatPeriod;
			settings.waves.fft.foamSlopeScale = source.m_Waves.m_FFT.m_FoamSlopeScale;
			settings.waves.fft.foamFoldScale = source.m_Waves.m_FFT.m_FoamFoldScale;
			settings.waves.detailNormal.enabled = source.m_Waves.m_DetailNormal.m_Enabled;
			settings.waves.detailNormal.intensity = source.m_Waves.m_DetailNormal.m_Intensity;
			settings.waves.detailNormal.scale = source.m_Waves.m_DetailNormal.m_Scale;
			settings.waves.detailNormal.octaveCount = source.m_Waves.m_DetailNormal.m_OctaveCount;
			settings.waves.detailNormal.timeOffset = source.m_Waves.m_DetailNormal.m_TimeOffset;
			settings.waves.detailNormal.detailBaseScale = source.m_Waves.m_DetailNormal.m_DetailBaseScale;

			settings.sssEnabled = source.m_SSS.m_Enabled;
			settings.maxThicknessDistance = source.m_SSS.m_MaxThicknessDistance;
			settings.deepWaterThicknessFallback = source.m_SSS.m_DeepWaterThicknessFallback;
			settings.causticsEnabled = source.m_Caustics.m_Enabled;
			settings.causticsIntensity = source.m_Caustics.m_Intensity;
			settings.causticsScale = source.m_Caustics.m_Scale;
			settings.refractionEnabled = source.m_Refraction.m_Enabled;
			settings.refractionMaxDistance = source.m_Refraction.m_MaxDistance;
			settings.refractionScale = source.m_Refraction.m_Scale;
			settings.ssrEnabled = source.m_SSR.m_Enabled;
			settings.ssrMaxDistance = source.m_SSR.m_MaxDistance;
			settings.ssrMaxRoughness = source.m_SSR.m_MaxRoughness;
			settings.foamEnabled = source.m_Foam.m_Enabled;
			settings.foamIntensity = source.m_Foam.m_Intensity;
			return settings;
		}

		void ApplyWaterSettingsToConfig(const WaterSettingsSnapshot& settings, VansGraphics::VansWaterConfig& destination)
		{
			destination.m_Type = static_cast<VansGraphics::VansWaterType>(std::clamp(settings.type, 0, 3));
			destination.m_WaterLevel = settings.waterLevel;
			destination.m_SpecularIntensity = settings.specularIntensity;

			destination.m_Medium.m_AbsorptionCoeff = ToRuntimeVec3(settings.medium.absorptionCoeff);
			destination.m_Medium.m_ScatteringCoeff = ToRuntimeVec3(settings.medium.scatteringCoeff);
			destination.m_Medium.m_IOR = settings.medium.ior;
			destination.m_Medium.m_FresnelPower = settings.medium.fresnelPower;
			destination.m_Medium.m_Anisotropy = settings.medium.anisotropy;
			destination.m_Medium.m_WaterRoughness = settings.medium.waterRoughness;
			destination.m_Medium.m_DeepColor = ToRuntimeVec4(settings.medium.deepColor);
			destination.m_Medium.m_ShallowColor = ToRuntimeVec4(settings.medium.shallowColor);

			destination.m_LOD.m_MaxLOD = settings.lod.maxLod;
			destination.m_LOD.m_BasePatchSize = settings.lod.basePatchSize;
			destination.m_LOD.m_MeshDim = settings.lod.meshDim;
			destination.m_LOD.m_DetailBalance = settings.lod.detailBalance;
			destination.m_LOD.m_MorphWidthRatio = settings.lod.morphWidthRatio;

			destination.m_Waves.m_Mode = static_cast<VansGraphics::VansWaveMode>(std::clamp(settings.waves.mode, 0, 2));
			destination.m_Waves.m_BaseScale = settings.waves.baseScale;
			destination.m_Waves.m_MaxLOD = settings.waves.maxLod;
			destination.m_Waves.m_WindDirection = ToRuntimeVec2(settings.waves.windDirection);
			destination.m_Waves.m_WindSpeed = settings.waves.windSpeed;
			destination.m_Waves.m_SwellAmplitude = settings.waves.swellAmplitude;
			destination.m_Waves.m_ChopScale = settings.waves.chopScale;
			destination.m_Waves.m_GerstnerWaveCount = settings.waves.gerstnerWaveCount;
			destination.m_Waves.m_FftLODCount = settings.waves.fftLodCount;
			destination.m_Waves.m_FftResolution = settings.waves.fftResolution;
			destination.m_Waves.m_FFT.m_UseDerivativeNormal = settings.waves.fft.useDerivativeNormal;
			destination.m_Waves.m_FFT.m_Resolution = settings.waves.fft.resolution;
			destination.m_Waves.m_FFT.m_LODCount = settings.waves.fft.lodCount;
			destination.m_Waves.m_FFT.m_SpectrumAmplitude = settings.waves.fft.spectrumAmplitude;
			destination.m_Waves.m_FFT.m_Choppiness = settings.waves.fft.choppiness;
			destination.m_Waves.m_FFT.m_SmallWaveDamping = settings.waves.fft.smallWaveDamping;
			destination.m_Waves.m_FFT.m_WindDependency = settings.waves.fft.windDependency;
			destination.m_Waves.m_FFT.m_Depth = settings.waves.fft.depth;
			destination.m_Waves.m_FFT.m_RepeatPeriod = settings.waves.fft.repeatPeriod;
			destination.m_Waves.m_FFT.m_FoamSlopeScale = settings.waves.fft.foamSlopeScale;
			destination.m_Waves.m_FFT.m_FoamFoldScale = settings.waves.fft.foamFoldScale;
			destination.m_Waves.m_DetailNormal.m_Enabled = settings.waves.detailNormal.enabled;
			destination.m_Waves.m_DetailNormal.m_Intensity = settings.waves.detailNormal.intensity;
			destination.m_Waves.m_DetailNormal.m_Scale = settings.waves.detailNormal.scale;
			destination.m_Waves.m_DetailNormal.m_OctaveCount = settings.waves.detailNormal.octaveCount;
			destination.m_Waves.m_DetailNormal.m_TimeOffset = settings.waves.detailNormal.timeOffset;
			destination.m_Waves.m_DetailNormal.m_DetailBaseScale = settings.waves.detailNormal.detailBaseScale;

			destination.m_SSS.m_Enabled = settings.sssEnabled;
			destination.m_SSS.m_MaxThicknessDistance = settings.maxThicknessDistance;
			destination.m_SSS.m_DeepWaterThicknessFallback = settings.deepWaterThicknessFallback;
			destination.m_Caustics.m_Enabled = settings.causticsEnabled;
			destination.m_Caustics.m_Intensity = settings.causticsIntensity;
			destination.m_Caustics.m_Scale = settings.causticsScale;
			destination.m_Refraction.m_Enabled = settings.refractionEnabled;
			destination.m_Refraction.m_MaxDistance = settings.refractionMaxDistance;
			destination.m_Refraction.m_Scale = settings.refractionScale;
			destination.m_SSR.m_Enabled = settings.ssrEnabled;
			destination.m_SSR.m_MaxDistance = settings.ssrMaxDistance;
			destination.m_SSR.m_MaxRoughness = settings.ssrMaxRoughness;
			destination.m_Foam.m_Enabled = settings.foamEnabled;
			destination.m_Foam.m_Intensity = settings.foamIntensity;
		}

		void SyncWaterConfigToMaterial(const VansGraphics::VansWaterConfig& config, VansGraphics::VansWaterMaterial* material)
		{
			if (!material)
				return;

			material->m_Config = config;
			material->m_AbsorptionCoeffs = config.m_Medium.m_AbsorptionCoeff;
			material->m_ScatteringCoeffs = config.m_Medium.m_ScatteringCoeff;
			material->m_WaterIOR = config.m_Medium.m_IOR;
			material->m_FresnelPower = config.m_Medium.m_FresnelPower;
			material->m_Anisotropy = config.m_Medium.m_Anisotropy;
			material->m_WaterRoughness = config.m_Medium.m_WaterRoughness;
			material->m_SpecularIntensity = config.m_SpecularIntensity;
			material->m_DeepWaterColor = config.m_Medium.m_DeepColor;
			material->m_ShallowWaterColor = config.m_Medium.m_ShallowColor;
			material->m_OceanBaseScale = config.m_Waves.m_BaseScale;
			material->m_GerstnerWaveCount = config.m_Waves.m_GerstnerWaveCount;
			material->m_FftLODCount = config.m_Waves.m_FftLODCount;
			material->m_FftResolution = config.m_Waves.m_FftResolution;
			material->m_FFTUseDerivativeNormal = config.m_Waves.m_FFT.m_UseDerivativeNormal;
			material->m_FFTSpectrumAmplitude = config.m_Waves.m_FFT.m_SpectrumAmplitude;
			material->m_FFTChoppiness = config.m_Waves.m_FFT.m_Choppiness;
			material->m_FFTSmallWaveDamping = config.m_Waves.m_FFT.m_SmallWaveDamping;
			material->m_FFTWindDependency = config.m_Waves.m_FFT.m_WindDependency;
			material->m_FFTDepth = config.m_Waves.m_FFT.m_Depth;
			material->m_FFTRepeatPeriod = config.m_Waves.m_FFT.m_RepeatPeriod;
			material->m_FFTFoamSlopeScale = config.m_Waves.m_FFT.m_FoamSlopeScale;
			material->m_FFTFoamFoldScale = config.m_Waves.m_FFT.m_FoamFoldScale;
			material->m_WindSpeed = config.m_Waves.m_WindSpeed;
			material->m_SwellAmplitude = config.m_Waves.m_SwellAmplitude;
			material->m_ChopScale = config.m_Waves.m_ChopScale;
			material->m_WindDirection = config.m_Waves.m_WindDirection;
			material->m_MaxLODCount = config.m_LOD.m_MaxLOD;
			material->m_LODBasePatchSize = config.m_LOD.m_BasePatchSize;
			material->m_LODMeshDim = config.m_LOD.m_MeshDim;
			material->m_LODDetailBalance = config.m_LOD.m_DetailBalance;
			material->m_LODMorphWidthRatio = config.m_LOD.m_MorphWidthRatio;
			material->m_EnableFoam = config.m_Foam.m_Enabled;
			material->m_FoamIntensity = config.m_Foam.m_Intensity;
			material->m_SSSEnabled = config.m_SSS.m_Enabled;
			material->m_MaxThicknessDistance = config.m_SSS.m_MaxThicknessDistance;
			material->m_DeepWaterThicknessFallback = config.m_SSS.m_DeepWaterThicknessFallback;
			material->m_EnableCaustics = config.m_Caustics.m_Enabled;
			material->m_CausticsIntensity = config.m_Caustics.m_Intensity;
			material->m_CausticsScale = config.m_Caustics.m_Scale;
			material->m_EnableRefraction = config.m_Refraction.m_Enabled;
			material->m_RefractionMaxDist = config.m_Refraction.m_MaxDistance;
			material->m_RefractionScale = config.m_Refraction.m_Scale;
			material->m_EnableSSR = config.m_SSR.m_Enabled;
			material->m_SSRMaxDistance = config.m_SSR.m_MaxDistance;
			material->m_SSRMaxRoughness = config.m_SSR.m_MaxRoughness;
			material->m_DetailNormalEnabled = config.m_Waves.m_DetailNormal.m_Enabled;
			material->m_DetailNormalIntensity = config.m_Waves.m_DetailNormal.m_Intensity;
			material->m_DetailNormalScale = config.m_Waves.m_DetailNormal.m_Scale;
			material->m_DetailNormalOctaves = config.m_Waves.m_DetailNormal.m_OctaveCount;
			material->m_DetailNormalTimeOffset = config.m_Waves.m_DetailNormal.m_TimeOffset;
			material->m_DetailNormalBaseScale = config.m_Waves.m_DetailNormal.m_DetailBaseScale;
		}

		bool ShouldReinitializeWaterFFT(
			const VansGraphics::VansWaterConfig& previous,
			const VansGraphics::VansWaterConfig& current)
		{
			return previous.m_Waves.m_Mode != current.m_Waves.m_Mode
				|| previous.m_Waves.m_FftLODCount != current.m_Waves.m_FftLODCount
				|| previous.m_Waves.m_FftResolution != current.m_Waves.m_FftResolution
				|| previous.m_Waves.m_FFT.m_UseDerivativeNormal != current.m_Waves.m_FFT.m_UseDerivativeNormal
				|| previous.m_Waves.m_FFT.m_LODCount != current.m_Waves.m_FFT.m_LODCount
				|| previous.m_Waves.m_FFT.m_Resolution != current.m_Waves.m_FFT.m_Resolution
				|| previous.m_Waves.m_FFT.m_SpectrumAmplitude != current.m_Waves.m_FFT.m_SpectrumAmplitude
				|| previous.m_Waves.m_FFT.m_Choppiness != current.m_Waves.m_FFT.m_Choppiness
				|| previous.m_Waves.m_FFT.m_SmallWaveDamping != current.m_Waves.m_FFT.m_SmallWaveDamping
				|| previous.m_Waves.m_FFT.m_WindDependency != current.m_Waves.m_FFT.m_WindDependency
				|| previous.m_Waves.m_FFT.m_Depth != current.m_Waves.m_FFT.m_Depth
				|| previous.m_Waves.m_FFT.m_RepeatPeriod != current.m_Waves.m_FFT.m_RepeatPeriod
				|| previous.m_Waves.m_FFT.m_FoamSlopeScale != current.m_Waves.m_FFT.m_FoamSlopeScale
				|| previous.m_Waves.m_FFT.m_FoamFoldScale != current.m_Waves.m_FFT.m_FoamFoldScale;
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
			database->RegisterOrRefresh(sourcePath, true, registrationError);

		if (const auto record = database->Find(sourcePath))
		{
			payload.available = true;
			payload.guid = record->guid.ToString();
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
			importIfMissing,
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

	ProjectOpenResult EngineAPIImpl::OpenProject(const ProjectOpenRequest& request)
	{
		ProjectOpenResult result;
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
		return result;
	}

	void EngineAPIImpl::CloseProject()
	{
		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
			projectManager.CloseProject();
	}

	float EngineAPIImpl::GetProjectPhysicsFixedTimeStep() const
	{
		auto& projectManager = Vans::VansProjectManager::Get();
		if (!projectManager.IsProjectLoaded())
			return 0.0f;
		return projectManager.GetProjectSettings().GetFixedTimeStep();
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
			database->Scan();
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
			Vans::Editor::VansEditorTextureBridge::RemoveTexture(cache.texture);
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

	std::vector<RenderTexturePreview> EngineAPIImpl::QueryRenderTexturePreviews(RenderTextureFilter filter) const
	{
		std::vector<RenderTexturePreview> previews;
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
			previews.push_back(BuildImagePreview(100, "GBuffer 0 (Albedo + Roughness)", renderPassManager->GetGbuffer0(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(101, "GBuffer 1 (Metallic + AO + MatID)", renderPassManager->GetGbuffer1(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(102, "GBuffer 2 (WorldPos + LinearDepth)", renderPassManager->GetGbuffer2(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(103, "Normal", renderPassManager->GetNormal(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			return previews;
		}

		if (filter.category == "water_gbuffer")
		{
			previews.push_back(BuildImagePreview(120, "WaterGBuf Normal", renderPassManager->GetWaterGBufNormal(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(121, "WaterGBuf WorldPos+Depth (RGBA16F)", renderPassManager->GetWaterGBufLinearDepth(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			return previews;
		}

		if (filter.category == "render_debug")
		{
			previews.push_back(BuildImagePreview(140, "Motion Vector", renderPassManager->GetMotionVector(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
			if (materialManager)
			{
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSR_RESULT))
					previews.push_back(BuildImagePreview(141, "SSR Resolve Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSGI_RESULT))
					previews.push_back(BuildImagePreview(142, "SSGI Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT))
					previews.push_back(BuildImagePreview(143, "Fog Blend Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT))
					previews.push_back(BuildImagePreview(144, "Screen Space Shadow", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
			}
			return previews;
		}

		if (filter.category == "hair_debug")
		{
			previews.push_back(BuildImagePreview(160, "Hair Color", renderPassManager->GetHairColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			previews.push_back(BuildImagePreview(161, "Hair Deep Opacity", renderPassManager->GetHairDeepOpacity(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			return previews;
		}

		return previews;
	}

	RenderBackendDiagnostics EngineAPIImpl::GetRenderBackendDiagnostics() const
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

		diagnostics.renderGraphSummary = device->GetCurrentRenderGraphDebugSummary();
		diagnostics.available = !diagnostics.renderGraphSummary.empty();
		if (!diagnostics.available)
			return diagnostics;

		diagnostics.compiledGraphValid =
			diagnostics.renderGraphSummary.find("CompiledRenderGraph") != std::string::npos
			&& ExtractSummaryCount(diagnostics.renderGraphSummary, "errors=") == 0;
		diagnostics.featureAuditPassed =
			SummaryContainsFlag(diagnostics.renderGraphSummary, "passed=", true);
		diagnostics.framePlanPassCount =
			ExtractSummaryCountAfter(diagnostics.renderGraphSummary, "RenderFramePlan", "passes=");
		diagnostics.compiledResourceCount =
			ExtractSummaryCountAfter(diagnostics.renderGraphSummary, "CompiledRenderGraph", "resources=");
		diagnostics.barrierDependencyCount =
			ExtractSummaryCountAfter(diagnostics.renderGraphSummary, "RenderGraphBarrierPlan", "dependencies=");
		return diagnostics;
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
			Vans::Editor::VansEditorTextureBridge::RemoveTexture(cache.texture);
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
		if (probes)
			probes->SaveConfiguration();
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
				device->GetLogicDevice(),
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
		if (textureName == "detail_normal")
			return build(202, "Detail Normal", waterSystem->GetDetailNormalImage(), 0u);
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

		VansGraphics::VansWaterConfig& config = const_cast<VansGraphics::VansWaterConfig&>(scene->GetWaterConfig());
		const VansGraphics::VansWaterConfig previousConfig = config;
		ApplyWaterSettingsToConfig(settings, config);

		auto* waterMaterial = scene->GetWaterMaterial();
		auto* waterSystem = scene->GetWaterSystem();
		SyncWaterConfigToMaterial(config, waterMaterial);

		if (waterSystem)
		{
			waterSystem->SetWaterLevel(config.m_WaterLevel);
			waterSystem->UpdateWaveSSBO();
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
		stats.maxWaterTextureLayer = std::max(int(VansGraphics::VansWaterLOD::MAX_LOD_COUNT) - 1, 0);
		stats.maxFftLod = std::max(int(VansGraphics::VansWaterFFT::MAX_LOD_COUNT) - 1, 0);
		stats.fftFieldCount = int(VansGraphics::VansWaterFFT::FIELD_COUNT);

		auto* waterSystem = scene->GetWaterSystem();
		if (!waterSystem)
			return stats;

		stats.systemInitialized = true;
		stats.fftAvailable = waterSystem->GetFFT() != nullptr;
		auto* lod = waterSystem->GetLOD();
		if (lod)
		{
			stats.patchCount = static_cast<std::uint32_t>(lod->GetPatchCount());
			stats.meshDim = lod->GetMeshDim();
			stats.basePatchSize = lod->GetBasePatchSize();
			stats.indexCount = lod->GetIndexCount();
			stats.lodLevels = lod->GetLodLevels();
			stats.detailBalance = lod->GetDetailBalance();
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

	RuntimeModelEntityCreateResult EngineAPIImpl::CreateRuntimeModelEntity(const RuntimeModelEntityCreateRequest& request)
	{
		RuntimeModelEntityCreateResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device || request.entityName.empty() || request.meshName.empty())
			return result;

		const glm::vec3 position(request.position.x, request.position.y, request.position.z);
		VansScriptObject* object = scene->CreateEntity(
			device->GetLogicDevice(),
			request.entityName,
			request.meshName,
			request.materialName,
			position);
		if (!object)
			return result;

		result.created = true;
		result.entityGuid = object->m_EntityGuid;
		return result;
	}

	ModelAssetPlacementPayload EngineAPIImpl::PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request)
	{
		return ModelAssetPlacementPreparationService::Prepare(request, m_Scene, m_Device);
	}

	RuntimeEntityDestroyResult EngineAPIImpl::DestroyRuntimeEntityByName(const RuntimeEntityDestroyRequest& request)
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
				return result;
			}
		}

		if (!request.entityName.empty())
			result.destroyed = scene->DestroyEntity(request.entityName);
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

	bool EngineAPIImpl::LoadRuntimeScene(const std::string& scenePath, RuntimeSceneLoadMode mode)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!scene || !device || scenePath.empty())
			return false;

		if (scene->IsSceneReady() || scene->IsSceneSwitching())
		{
			device->WaitForDevice();
			ClearEditorRenderTexturePreviewCaches(device->GetLogicDevice());
		}

		const VansGraphics::VansSceneLoadMode runtimeMode =
			mode == RuntimeSceneLoadMode::Runtime
				? VansGraphics::VansSceneLoadMode::Runtime
				: VansGraphics::VansSceneLoadMode::Editor;
		scene->LoadSceneForRendering(scenePath.c_str(), device, runtimeMode);
		return true;
	}

	void EngineAPIImpl::UnloadRuntimeScene()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return;

		if (auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device))
		{
			device->WaitForDevice();
			ClearEditorRenderTexturePreviewCaches(device->GetLogicDevice());
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
			snapshot.visuals.push_back(visual);
		}

		snapshot.available = !snapshot.visuals.empty();
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

	void EngineAPIImpl::ApplyRuntimeEntityPatchJson(const std::string& entityJson)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || entityJson.empty())
			return;

		auto entity = nlohmann::ordered_json::parse(entityJson, nullptr, false);
		if (!entity.is_object())
			return;

		RuntimeTransformEdit transformEdit;
		if (BuildRuntimeTransformEditFromPatch(entity, transformEdit))
			ApplyRuntimeTransform(transformEdit);

		for (RuntimeLightPatch& lightPatch : BuildRuntimeLightPatchesFromPatch(entity))
			SubmitCommand(std::make_unique<SetRuntimeLightPropertiesCommand>(std::move(lightPatch)));
	}

	void EngineAPIImpl::SetRuntimeComponentEnabled(
		const std::string& entityGuid,
		const std::string& componentType,
		bool enabled)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || entityGuid.empty() || componentType.empty())
			return;

		VansScriptObject* obj = scene->FindObjectByGuid(entityGuid);
		if (!obj)
			return;

		static const std::unordered_map<std::string, std::string> kTypeToRuntime = {
			{"ModelRenderer",       "render"},
			{"Physics",             "physics"},
			{"Camera",              "camera"},
			{"Cloth",               "cloth"},
			{"Vehicle",             "vehicle"},
			{"Animator",            "animation"},
			{"Animation",           "animation"},
			{"CharacterController", "CharacterController"},
		};

		std::string runtimeName = componentType;
		auto mapIt = kTypeToRuntime.find(componentType);
		if (mapIt != kTypeToRuntime.end())
			runtimeName = mapIt->second;

		for (auto* comp : obj->m_Components)
		{
			if (comp && comp->m_ComponentName == runtimeName)
			{
				comp->SetEnabled(enabled);
				return;
			}
		}
	}

	bool EngineAPIImpl::ApplyRuntimeMaterialAssetPatch(
		const std::string& assetPath,
		const std::string& assetRootJson,
		const std::string& changedPointer)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || assetPath.empty() || assetRootJson.empty())
			return false;

		auto assetRoot = nlohmann::ordered_json::parse(assetRootJson, nullptr, false);
		if (!assetRoot.is_object())
			return false;

		VansGraphics::VansMaterialLiveEditService liveEdit;
		return liveEdit.ApplyMaterialAssetPatch(
			scene,
			std::filesystem::path(assetPath),
			assetRoot,
			changedPointer);
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

	void EngineAPIImpl::ApplyFogSettings(const FogSettings& settings)
	{
		SubmitCommand(std::make_unique<SetFogSettingsCommand>(settings));
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

		scene->GetMaterialManager()->UploadCloudParamsToGPU();
	}

	EnginePlayState EngineAPIImpl::GetPlayState() const
	{
		return m_PlayState;
	}

	void EngineAPIImpl::SetPlayState(EnginePlayState state)
	{
		if (m_PlayState == state)
			return;

		m_PlayState = state;
		for (IEngineEventListener* listener : m_Listeners)
		{
			if (listener)
				listener->OnPlayStateChanged(state);
		}
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

		for (auto* node : scene->GetOpaqueRenderNodes())
			testNode(node);
		for (auto* node : scene->GetTransparentRenderNodes())
			testNode(node);

		if (!bestNode)
			return {};

		if (!bestNode->m_EntityGuid.empty())
			return bestNode->m_EntityGuid;

		for (VansScriptObject* obj : scene->GetSceneObjects())
		{
			if (!obj)
				continue;
			if (auto* render = obj->GetComponent<VansScriptRenderComponent>())
				if (render->m_RenderNode == bestNode)
					return obj->m_EntityGuid;
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
				child.sourceNode = SanitizeRuntimeJsonText(childNode->m_Mesh ? childNode->m_Mesh->m_SourceNodeName : std::string{});
				child.sourceMaterial = SanitizeRuntimeJsonText(childNode->m_Material ? childNode->m_Material->m_AssetName : std::string{});
				child.materialGuid = EnsureRuntimeGeneratedMaterialAsset(parentName, childNode, database->AssetsRoot());
				if (!child.materialGuid.empty())
					snapshot.children.push_back(std::move(child));
			}

			if (!snapshot.children.empty())
				snapshots.push_back(std::move(snapshot));
		}

		return snapshots;
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
		m_ScriptContext->SetupProjectVenv(projectRootPath);
	}

	void EngineAPIImpl::ReloadRuntimeScripts()
	{
		if (m_ScriptContext)
			m_ScriptContext->ReloadAllPyScripts();
	}

	void EngineAPIImpl::ReloadRuntimeScriptModule()
	{
		if (m_ScriptContext)
			m_ScriptContext->ReloadPydModule();
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

	void EngineAPIImpl::Subscribe(IEngineEventListener* listener)
	{
		if (!listener)
			return;

		if (std::find(m_Listeners.begin(), m_Listeners.end(), listener) == m_Listeners.end())
			m_Listeners.push_back(listener);
	}

	void EngineAPIImpl::Unsubscribe(IEngineEventListener* listener)
	{
		m_Listeners.erase(
			std::remove(m_Listeners.begin(), m_Listeners.end(), listener),
			m_Listeners.end());
	}
}

