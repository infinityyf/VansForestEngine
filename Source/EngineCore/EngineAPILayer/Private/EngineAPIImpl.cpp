#include "EngineAPIImpl.h"

#include "AnimationAuthoringBridge.h"
#include "AnimationPreviewAttachmentAuthoringService.h"
#include "AnimationPreviewRigAuthoringService.h"
#include "GameplayActionAuthoringBridge.h"
#include "GameplayActionSimulationBridge.h"
#include "../../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../../GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../../GameplayActionDebug/VansGameplayActionDebug.h"
#include "../../GameplayActionAdapters/Combat/VansCombatActionService.h"
#include "EngineCommandContext.h"
#include "../Public/EngineEvents.h"
#include "ModelAssetPlacementPreparationService.h"
#include "RuntimeGeneratedMaterialAssetService.h"
#include "ScenePropertyValueBuilders.h"
#include "VansMaterialLiveEditService.h"
#include "VansEditorTextureBridge.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/VansAssetMeta.h"
#include "../../AssetCore/VansSkinProfile.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../../AudioCore/Storage/VansAudioBusSnapshotAssetStorage.h"
#include "../../AudioCore/Storage/VansAudioDuckingRulesAssetStorage.h"
#include "../../AudioCore/Storage/VansAudioReverbPresetAssetStorage.h"
#include "../../AssetCore/Storage/VansSkinProfileStorage.h"
#include "../../AudioCore/VansAudioBusSnapshotAsset.h"
#include "../../AudioCore/VansAudioDuckingRulesAsset.h"
#include "../../AudioCore/VansAudioReverbPresetAsset.h"
#include "../../AudioCore/VansAudioReverbPreset.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansCamera.h"
#include "../../RenderCore/VansRenderSystem.h"
#include "../../RenderCore/VansAnimationPreviewRenderer.h"
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
#include "../../SceneCore/VansSceneParentReference.h"
#include "../../SceneCore/VansSceneContentBuildPlan.h"
#include "../../TimelineCore/VansTimelineTypes.h"
#include "../../TimelineCore/VansTimelineSerialization.h"
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
#include "../../AnimationCore/VansSkinnedMeshLoader.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/VansAnimatorRuntimeCompiler.h"
#include "../../EventCore/VansEventBus.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../AudioCore/VansAudioReverbEnvironment.h"
#include "../../AudioCore/VansAudioSystem.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../../Graphics/Vulkan/VansVKFunctions.h"

#include "../../Util/VansInputManager.h"
#include "../../Util/VansLog.h"
#include "../../RuntimeCore/VansThreadContract.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>

namespace Vans::EditorAPI
{
	namespace
	{
		struct RenderSettingsTransactionState final
		{
			enum class Operation
			{
				ApplyUpscaler,
				ApplyCommandRecording,
				ApplyProjectRuntimeConfig,
				RefreshPipelineCachePath
			};

			Operation operation = Operation::ApplyUpscaler;
			VansGraphics::VansUpscalerConfig upscalerConfig;
			VansGraphics::VansUpscalerSelectionChange upscalerSelection;
			VansGraphics::VansRenderRuntimeConfig runtimeConfig;
			CommandRecordingSettingsSnapshot commandRecording;
			std::uint32_t outputWidth = 0;
			std::uint32_t outputHeight = 0;
		};

		// EngineAPILayer 只负责 DTO/设置翻译；所有会改变 Vulkan backend 的操作
		// 都作为有序事务进入 RenderThread，不能从 Editor/Main 直接写 device。
		class RenderSettingsTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit RenderSettingsTransaction(
				std::shared_ptr<RenderSettingsTransactionState> state)
				: m_State(std::move(state)) {}

			bool Execute(VansGraphics::VansGraphicsDevice& backend) override
			{
				VANS_ASSERT_RENDER_THREAD();
				auto* device = dynamic_cast<VansGraphics::VansVKDevice*>(&backend);
				if (!m_State || !device)
					return false;

				switch (m_State->operation)
				{
				case RenderSettingsTransactionState::Operation::ApplyUpscaler:
					m_State->upscalerSelection = device->RequestUpscalerConfig(
						m_State->upscalerConfig,
						m_State->outputWidth,
						m_State->outputHeight);
					if (!m_State->upscalerSelection.accepted)
						return false;
					// 在事务返回前完成 resolution-owned 资源切换，使下一次
					// Main BuildRenderViewSnapshot 读取到匹配的新 render extent。
					device->CommitRenderRuntimeConfigAtSafePoint();
					{
						const VkExtent2D outputExtent = device->GetUpscalerOutputExtent();
						m_State->outputWidth = outputExtent.width;
						m_State->outputHeight = outputExtent.height;
					}
					return true;
				case RenderSettingsTransactionState::Operation::ApplyCommandRecording:
					return device->ApplyCommandRecordingSettings(
						m_State->commandRecording.parallelEnabled,
						m_State->commandRecording.frameContextRingEnabled,
						m_State->commandRecording.framesInFlight,
						m_State->commandRecording.asyncComputeRequested);
				case RenderSettingsTransactionState::Operation::ApplyProjectRuntimeConfig:
				{
					device->GetPipelineCacheService().RefreshPersistencePath();
					const VkExtent2D outputExtent = device->GetUpscalerOutputExtent();
					device->ApplyRenderRuntimeConfig(
						m_State->runtimeConfig,
						outputExtent.width,
						outputExtent.height);
					device->CommitRenderRuntimeConfigAtSafePoint();
					const VkExtent2D appliedOutputExtent = device->GetUpscalerOutputExtent();
					m_State->outputWidth = appliedOutputExtent.width;
					m_State->outputHeight = appliedOutputExtent.height;
					return true;
				}
				case RenderSettingsTransactionState::Operation::RefreshPipelineCachePath:
					device->GetPipelineCacheService().RefreshPersistencePath();
					return true;
				}
				return false;
			}

		private:
			std::shared_ptr<RenderSettingsTransactionState> m_State;
		};

		void ApplyRuntimeUIOutputExtent(std::uint32_t width, std::uint32_t height)
		{
			VANS_ASSERT_MAIN_THREAD();
			auto& uiSystem = VansRuntime::VansUISystem::Get();
			if (width > 0 && height > 0 && uiSystem.IsInitialized())
				uiSystem.SetScreenSize(width, height);
		}

		bool TryToSceneParentReference(
			const RuntimeParentReference& source,
			std::optional<Vans::VansSceneParentReference>& output,
			std::string& error)
		{
			output.reset();
			if (source.kind == RuntimeParentKind::None)
				return source.entityGuid.empty()
					&& source.animationComponentGuid.empty() && source.anchorGuid.empty();

			Vans::VansSceneParentReference parent;
			if (!Vans::VansAssetGuid::TryParse(source.entityGuid, parent.entityGuid))
			{
				error = "Parent entity GUID is invalid";
				return false;
			}
			switch (source.kind)
			{
			case RuntimeParentKind::Entity:
				parent.kind = Vans::VansSceneParentKind::Entity;
				if (!source.animationComponentGuid.empty() || !source.anchorGuid.empty())
				{
					error = "Entity parent must not contain animation or anchor GUIDs";
					return false;
				}
				break;
			case RuntimeParentKind::Bone:
			case RuntimeParentKind::Socket:
				parent.kind = source.kind == RuntimeParentKind::Bone
					? Vans::VansSceneParentKind::Bone : Vans::VansSceneParentKind::Socket;
				if (!Vans::VansAssetGuid::TryParse(
					source.animationComponentGuid, parent.animationComponentGuid)
					|| !Vans::VansAssetGuid::TryParse(source.anchorGuid, parent.anchorGuid))
				{
					error = "Bone or socket parent contains an invalid component or anchor GUID";
					return false;
				}
				break;
			default:
				error = "Parent kind is invalid";
				return false;
			}
			output = std::move(parent);
			return true;
		}

		Vans::VansTransformReparentMode ToRuntimeReparentMode(
			RuntimeReparentTransformPolicy policy)
		{
			switch (policy)
			{
			case RuntimeReparentTransformPolicy::KeepLocal:
				return Vans::VansTransformReparentMode::KeepLocal;
			case RuntimeReparentTransformPolicy::Snap:
				return Vans::VansTransformReparentMode::Snap;
			case RuntimeReparentTransformPolicy::KeepWorld:
			default:
				return Vans::VansTransformReparentMode::KeepWorld;
			}
		}

		bool TryToRenderBackend(
			UpscalerBackend backend,
			VansGraphics::VansUpscalerBackend& output)
		{
			switch (backend)
			{
			case UpscalerBackend::Off:
				output = VansGraphics::VansUpscalerBackend::Off;
				return true;
			case UpscalerBackend::FSR:
				output = VansGraphics::VansUpscalerBackend::FSR;
				return true;
			case UpscalerBackend::DLSS:
				output = VansGraphics::VansUpscalerBackend::DLSS;
				return true;
			default:
				return false;
			}
		}

		UpscalerBackend ToEditorBackend(VansGraphics::VansUpscalerBackend backend)
		{
			switch (backend)
			{
			case VansGraphics::VansUpscalerBackend::Off: return UpscalerBackend::Off;
			case VansGraphics::VansUpscalerBackend::FSR: return UpscalerBackend::FSR;
			case VansGraphics::VansUpscalerBackend::DLSS: return UpscalerBackend::DLSS;
			default: return UpscalerBackend::Off;
			}
		}

		bool TryToRenderQuality(
			UpscaleQualityMode quality,
			VansGraphics::VansUpscaleQualityMode& output)
		{
			switch (quality)
			{
			case UpscaleQualityMode::NativeAA:
				output = VansGraphics::VansUpscaleQualityMode::NativeAA;
				return true;
			case UpscaleQualityMode::Quality:
				output = VansGraphics::VansUpscaleQualityMode::Quality;
				return true;
			case UpscaleQualityMode::Balanced:
				output = VansGraphics::VansUpscaleQualityMode::Balanced;
				return true;
			case UpscaleQualityMode::Performance:
				output = VansGraphics::VansUpscaleQualityMode::Performance;
				return true;
			case UpscaleQualityMode::UltraPerformance:
				output = VansGraphics::VansUpscaleQualityMode::UltraPerformance;
				return true;
			default:
				return false;
			}
		}

		UpscaleQualityMode ToEditorQuality(VansGraphics::VansUpscaleQualityMode quality)
		{
			switch (quality)
			{
			case VansGraphics::VansUpscaleQualityMode::NativeAA:
				return UpscaleQualityMode::NativeAA;
			case VansGraphics::VansUpscaleQualityMode::Quality:
				return UpscaleQualityMode::Quality;
			case VansGraphics::VansUpscaleQualityMode::Balanced:
				return UpscaleQualityMode::Balanced;
			case VansGraphics::VansUpscaleQualityMode::Performance:
				return UpscaleQualityMode::Performance;
			case VansGraphics::VansUpscaleQualityMode::UltraPerformance:
				return UpscaleQualityMode::UltraPerformance;
			default:
				return UpscaleQualityMode::NativeAA;
			}
		}

		UpscalerCapabilitiesSnapshot ToEditorCapabilities(
			const VansGraphics::VansUpscalerCapabilities& capabilities)
		{
			UpscalerCapabilitiesSnapshot output;
			output.backend = ToEditorBackend(capabilities.backend);
			output.compiledIn = capabilities.compiledIn;
			output.runtimeAvailable = capabilities.runtimeAvailable;
			output.deviceSupported = capabilities.deviceSupported;
			output.supportedQualityMask = capabilities.supportedQualityMask;
			output.featureVersion = capabilities.featureVersion;
			output.unavailableReason = capabilities.unavailableReason;
			return output;
		}

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
			std::uint32_t mipLevel = UINT32_MAX;
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
			std::uint32_t requestedMip,
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
			const uint32_t mipCount = std::max(image.GetImageCreateInfo().mipLevels, 1u);
			const uint32_t mipLevel = std::min(requestedMip, mipCount - 1u);

			auto& caches = GetLayerPreviewCaches();

			auto it = std::find_if(caches.begin(), caches.end(),
				[id](const LayerPreviewCache& cache)
				{
					return cache.id == id;
				});
			if (it == caches.end())
				it = caches.insert(caches.end(), LayerPreviewCache{ id });

			if (!it->texture || it->image != image.GetImage() ||
				it->layer != layer || it->mipLevel != mipLevel)
			{
				RetireEditorTexture(renderDevice, it->texture);
				it->texture = nullptr;
				if (it->view != VK_NULL_HANDLE)
				{
					RetireImageView(renderDevice, logicalDevice, it->view);
					it->view = VK_NULL_HANDLE;
				}

				it->view = image.CreateLayerMipView(logicalDevice, layer, mipLevel);
				it->image = image.GetImage();
				it->layer = layer;
				it->mipLevel = mipLevel;
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
			preview.width = (std::max)(extent.width >> mipLevel, 1u);
			preview.height = (std::max)(extent.height >> mipLevel, 1u);
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
			case VansGraphics::VansMainCameraCullClass::ForwardOpaquePreAtmosphere:
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
			case Vans::VansAssetType::AnimationRig: return AssetType::AnimationRig;
			case Vans::VansAssetType::BoneMask: return AssetType::BoneMask;
			case Vans::VansAssetType::Timeline: return AssetType::Timeline;
			case Vans::VansAssetType::ActionDefinition: return AssetType::ActionDefinition;
			case Vans::VansAssetType::ActionSet: return AssetType::ActionSet;
			case Vans::VansAssetType::GameplayEffect: return AssetType::GameplayEffect;
			case Vans::VansAssetType::GameplayCue: return AssetType::GameplayCue;
			case Vans::VansAssetType::AttributeSet: return AssetType::AttributeSet;
			case Vans::VansAssetType::TargetingPolicy: return AssetType::TargetingPolicy;
			case Vans::VansAssetType::GameplayTagTree: return AssetType::GameplayTagTree;
			case Vans::VansAssetType::PayloadSchema: return AssetType::PayloadSchema;
			case Vans::VansAssetType::ActionGraph: return AssetType::ActionGraph;
			case Vans::VansAssetType::CameraRigProfile: return AssetType::CameraRigProfile;
			case Vans::VansAssetType::CameraShakeProfile: return AssetType::CameraShakeProfile;
			case Vans::VansAssetType::GAFEditorLayout: return AssetType::GAFEditorLayout;
			case Vans::VansAssetType::ClothProfile: return AssetType::ClothProfile;
			case Vans::VansAssetType::SkinProfile: return AssetType::SkinProfile;
			case Vans::VansAssetType::PostProcessProfile: return AssetType::PostProcessProfile;
			case Vans::VansAssetType::RagdollProfile: return AssetType::RagdollProfile;
			case Vans::VansAssetType::AudioReverbPreset: return AssetType::AudioReverbPreset;
			case Vans::VansAssetType::AudioBusSnapshot: return AssetType::AudioBusSnapshot;
			case Vans::VansAssetType::AudioDuckingRules: return AssetType::AudioDuckingRules;
			default: return AssetType::Unknown;
			}
		}

		bool ResolveGAFCreation(
			ProjectAssetCreationKind kind,
			Vans::VansAssetType& assetType,
			const char*& baseName,
			const char*& extension)
		{
			switch (kind)
			{
			case ProjectAssetCreationKind::ActionDefinition:
				assetType = Vans::VansAssetType::ActionDefinition; baseName = "New Action"; extension = ".vaction"; return true;
			case ProjectAssetCreationKind::ActionSet:
				assetType = Vans::VansAssetType::ActionSet; baseName = "New Action Set"; extension = ".vactionset"; return true;
			case ProjectAssetCreationKind::GameplayEffect:
				assetType = Vans::VansAssetType::GameplayEffect; baseName = "New Effect"; extension = ".veffect"; return true;
			case ProjectAssetCreationKind::GameplayCue:
				assetType = Vans::VansAssetType::GameplayCue; baseName = "New Cue"; extension = ".vcue"; return true;
			case ProjectAssetCreationKind::AttributeSet:
				assetType = Vans::VansAssetType::AttributeSet; baseName = "New Attribute Set"; extension = ".vattributeset"; return true;
			case ProjectAssetCreationKind::TargetingPolicy:
				assetType = Vans::VansAssetType::TargetingPolicy; baseName = "New Targeting"; extension = ".vtargeting"; return true;
			case ProjectAssetCreationKind::GameplayTagTree:
				assetType = Vans::VansAssetType::GameplayTagTree; baseName = "Gameplay Tags"; extension = ".vtagtree"; return true;
			case ProjectAssetCreationKind::PayloadSchema:
				assetType = Vans::VansAssetType::PayloadSchema; baseName = "New Payload"; extension = ".vpayloadschema"; return true;
			case ProjectAssetCreationKind::ActionGraph:
				assetType = Vans::VansAssetType::ActionGraph; baseName = "New Action Graph"; extension = ".vactiongraph"; return true;
			case ProjectAssetCreationKind::CameraRigProfile:
				assetType = Vans::VansAssetType::CameraRigProfile; baseName = "New Camera Rig"; extension = ".vcamerarig"; return true;
			case ProjectAssetCreationKind::CameraShakeProfile:
				assetType = Vans::VansAssetType::CameraShakeProfile; baseName = "New Camera Shake"; extension = ".vcamerashake"; return true;
			default:
				return false;
			}
		}

		bool ResolveProjectAnimationAsset(
			const std::string& guidText,
			Vans::VansAssetType expectedType,
			const std::string& pathHint,
			std::filesystem::path& path,
			std::string& error)
		{
			path.clear();
			Vans::VansAssetGuid guid;
			if (!Vans::VansAssetGuid::TryParse(guidText, guid))
			{
				error = "Invalid animation dependency GUID '" + guidText + "'";
				return false;
			}
			const auto record = Vans::VansProjectManager::Get().FindAssetRecord(guid);
			if (!record || record->state == Vans::VansAssetState::Missing)
			{
				error = "Animation dependency cannot resolve GUID '" + guidText
					+ "' in the current project (hint: '" + pathHint + "')";
				return false;
			}
			if (record->type != expectedType)
			{
				error = "Animation dependency GUID '" + guidText + "' has the wrong asset type";
				return false;
			}
			path = expectedType == Vans::VansAssetType::Model
				? record->sourcePath
				: (!record->artifactPath.empty() ? record->artifactPath : record->sourcePath);
			if (path.is_relative() && Vans::VansProjectManager::Get().IsProjectLoaded())
				path = std::filesystem::path(Vans::VansProjectManager::Get().GetProjectRootPath()) / path;
			path = path.lexically_normal();
			if (path.empty() || !std::filesystem::exists(path))
			{
				error = "Animation dependency has no readable project/engine asset";
				return false;
			}
			return true;
		}

		bool LoadAnimationPreviewSkeleton(
			const std::string& modelGuid,
			VansGraphics::Skeleton& skeleton,
			std::string& sourcePath,
			std::string& error)
		{
			std::filesystem::path path;
			if (!ResolveProjectAnimationAsset(modelGuid, Vans::VansAssetType::Model, {}, path, error))
				return false;
			sourcePath = path.string();
			return VansGraphics::VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
				sourcePath, skeleton, error);
		}

		bool ResolveAnimationPreviewModel(
			const std::string& modelGuid,
			std::filesystem::path& path,
			float& scaleFactor,
			Vans::VansSkeletalMeshImportSettings& importSettings,
			std::string& error)
		{
			if (!ResolveProjectAnimationAsset(
				modelGuid, Vans::VansAssetType::Model, {}, path, error))
				return false;
			Vans::VansAssetGuid guid;
			if (!Vans::VansAssetGuid::TryParse(modelGuid, guid))
			{
				error = "Invalid animation preview model GUID";
				return false;
			}
			const auto record = Vans::VansProjectManager::Get().FindAssetRecord(guid);
			if (!record)
			{
				error = "Animation preview model metadata record does not exist";
				return false;
			}
			Vans::VansAssetMeta meta;
			if (!Vans::VansAssetMetaStorage::Load(record->metaPath, meta, error))
			{
				error = "Cannot load animation preview model metadata: " + error;
				return false;
			}
			scaleFactor = meta.ReadFloatSetting("scaleFactor", 1.0f);
			if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
			{
				error = "Animation preview model has an invalid scaleFactor";
				return false;
			}
			importSettings = Vans::ReadSkeletalMeshImportSettings(meta);
			return true;
		}

		std::unique_ptr<VansGraphics::VansAnimationController> CompileProjectAnimator(
			const std::filesystem::path& animatorPath,
			const VansGraphics::Skeleton& skeleton,
			bool enableTargetPostProcess,
			bool enableRootMotion,
			bool externalPoseTarget,
			const std::string& animationRigGuidOverride,
			std::string& error)
		{
			VansGraphics::AnimatorAssetData asset;
			if (!VansGraphics::VansAnimatorIO::Load(animatorPath.string(), asset))
			{
				error = "Failed to load Animator '" + animatorPath.string() + "'";
				return nullptr;
			}
			VansGraphics::VansAnimatorRuntimeCompileOptions options;
			options.mode = externalPoseTarget
				? VansGraphics::VansAnimatorRuntimeCompileMode::ExternalPoseTarget
				: VansGraphics::VansAnimatorRuntimeCompileMode::FullGraph;
			options.enableTargetPostProcess = enableTargetPostProcess;
			options.enableRootMotion = enableRootMotion && !externalPoseTarget;
			options.animationRigGuidOverride = animationRigGuidOverride;
			options.rigResolver = [](const std::string& guid, std::filesystem::path& path, std::string& resolveError)
			{
				return ResolveProjectAnimationAsset(
					guid, Vans::VansAssetType::AnimationRig, {}, path, resolveError);
			};
			options.queryProfileResolver = [](const std::string& profile, std::uint32_t& mask, std::string& resolveError)
			{
				return Vans::VansProjectManager::Get().GetProjectSettings()
					.ResolvePhysicsQueryProfile(profile, mask, resolveError);
			};
			return VansGraphics::VansAnimatorRuntimeCompiler::Compile(
				asset,
				skeleton,
				[](const VansGraphics::AnimatorClipRef& reference,
				   std::filesystem::path& path,
				   std::string& resolveError)
				{
					return ResolveProjectAnimationAsset(
						reference.assetGuid, Vans::VansAssetType::AnimationClip,
						reference.pathHint, path, resolveError);
				},
				[](const VansGraphics::VansAnimationLayerDefinition& layer,
				   std::filesystem::path& path,
				   std::string& resolveError)
				{
					return ResolveProjectAnimationAsset(
						layer.maskGuid, Vans::VansAssetType::BoneMask,
						layer.maskPathHint, path, resolveError);
				},
				options,
				error);
		}

		using RuntimeLightPatch = RuntimeLightEdit;
		using RuntimeLightPatchType = RuntimePreviewLightType;

		struct RuntimeLightBinding
		{
			VansGraphics::VansLightManager* manager = nullptr;
			int index = -1;
		};

		struct AnimationPreviewSessionState
		{
			AnimationPreviewSessionId id = 0;
			AnimationPreviewTargetKind targetKind =
				AnimationPreviewTargetKind::IsolatedModel;
			std::string modelGuid;
			std::string modelPath;
			std::string animatorAssetGuid;
			std::string animatorAssetPath;
			std::string entityGuid;
			std::string animationComponentGuid;
			std::uint64_t sceneContentRevision = 0;
			VansGraphics::AnimationState originalPlaybackState =
				VansGraphics::AnimationState::Stopped;
			float originalSpeed = 1.0f;
			bool originalSceneStateCaptured = false;
			bool replacedRetargetSourceController = false;
			std::unique_ptr<VansGraphics::VansAnimationController>
				originalSceneController;
			std::vector<std::pair<Vans::VansComponentHandle, bool>>
				previewRenderComponents;
			VansGraphics::Skeleton skeleton;
			std::unique_ptr<VansGraphics::VansAnimationPreviewRenderer> renderer;
			EditorTextureHandle texture = nullptr;
			std::unique_ptr<VansGraphics::VansAnimationController> controller;
			std::uint64_t requestedRevision = 0;
			std::uint64_t displayedRevision = 0;
			bool playing = true;
			float speed = 1.0f;
			AnimationPreviewPlaybackRequest::RootMotionMode rootMotionMode =
				AnimationPreviewPlaybackRequest::RootMotionMode::InPlace;
			glm::vec3 rootMotionPosition = glm::vec3(0.0f);
			std::vector<glm::vec3> rootMotionTrail = { glm::vec3(0.0f) };
			std::vector<VansGraphics::VansSlotPlaybackHandle> slotHandles;
			VansGraphics::VansAnimationPreviewView view;
			int visualizedLayerIndex = -1;
			std::vector<glm::vec4> visualizationColors;
			float renderAccumulator = 0.0f;
			bool renderDirty = true;
			bool renderLogged = false;
			std::uint64_t lastScenePoseRevision = 0;
			float lastUpdateMilliseconds = 0.0f;
			std::string diagnostic;
		};

		struct AnimationPreviewGpuTransactionState
		{
			enum class Operation
			{
				Initialize,
				Upload,
				Destroy
			};

			Operation operation = Operation::Initialize;
			VansGraphics::VansAnimationPreviewRenderer* renderer = nullptr;
			std::unique_ptr<VansGraphics::VansAnimationPreviewRenderer> ownedRenderer;
			EditorTextureHandle texture = nullptr;
			std::string error;
		};

		class AnimationPreviewGpuTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit AnimationPreviewGpuTransaction(
				std::shared_ptr<AnimationPreviewGpuTransactionState> state)
				: m_State(std::move(state)) {}

			bool Execute(VansGraphics::VansGraphicsDevice& backend) override
			{
				VANS_ASSERT_RENDER_THREAD();
				auto* device = dynamic_cast<VansGraphics::VansVKDevice*>(&backend);
				if (!m_State || !device)
					return false;

				if (m_State->operation ==
					AnimationPreviewGpuTransactionState::Operation::Destroy)
				{
					const bool idle = backend.WaitForIdle();
					if (m_State->texture)
					{
						Vans::Editor::VansEditorTextureBridge::RemoveTexture(
							m_State->texture);
						m_State->texture = nullptr;
					}
					if (m_State->ownedRenderer)
					{
						m_State->ownedRenderer->Shutdown();
						m_State->ownedRenderer.reset();
					}
					return idle;
				}

				if (!m_State->renderer)
					return false;
				if (m_State->operation ==
					AnimationPreviewGpuTransactionState::Operation::Upload)
				{
					return m_State->renderer->UploadRenderThread(m_State->error);
				}

				if (!m_State->renderer->InitializeGpuRenderThread(
					*device, m_State->error))
				{
					return false;
				}
				m_State->texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
					m_State->renderer->GetColorImage().GetSampler(),
					m_State->renderer->GetColorImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				if (!m_State->texture)
				{
					m_State->error =
						"Failed to register isolated animation preview texture";
					m_State->renderer->Shutdown();
					return false;
				}
				return true;
			}

		private:
			std::shared_ptr<AnimationPreviewGpuTransactionState> m_State;
		};

		const char* AnimationPreviewSlotStateName(VansGraphics::VansSlotPlaybackState state)
		{
			switch (state)
			{
			case VansGraphics::VansSlotPlaybackState::Queued: return "Queued";
			case VansGraphics::VansSlotPlaybackState::BlendingIn: return "Blending In";
			case VansGraphics::VansSlotPlaybackState::Playing: return "Playing";
			case VansGraphics::VansSlotPlaybackState::BlendingOut: return "Blending Out";
			case VansGraphics::VansSlotPlaybackState::Completed: return "Completed";
			case VansGraphics::VansSlotPlaybackState::Interrupted: return "Interrupted";
			case VansGraphics::VansSlotPlaybackState::Rejected: return "Rejected";
			case VansGraphics::VansSlotPlaybackState::Invalid: break;
			}
			return "Invalid";
		}

		const char* AnimationPreviewSlotEventName(VansGraphics::VansSlotLifecycleEventType type)
		{
			switch (type)
			{
			case VansGraphics::VansSlotLifecycleEventType::Started: return "Started";
			case VansGraphics::VansSlotLifecycleEventType::BlendingOut: return "Blending Out";
			case VansGraphics::VansSlotLifecycleEventType::Completed: return "Completed";
			case VansGraphics::VansSlotLifecycleEventType::Interrupted: return "Interrupted";
			case VansGraphics::VansSlotLifecycleEventType::InterruptedByReload: return "Interrupted By Reload";
			case VansGraphics::VansSlotLifecycleEventType::Rejected: return "Rejected";
			}
			return "Unknown";
		}

		std::unordered_map<AnimationPreviewSessionId, std::unique_ptr<AnimationPreviewSessionState>>&
		GetAnimationPreviewSessions()
		{
			static std::unordered_map<AnimationPreviewSessionId,
				std::unique_ptr<AnimationPreviewSessionState>> sessions;
			return sessions;
		}

		AnimationPreviewSessionId NextAnimationPreviewSessionId()
		{
			static AnimationPreviewSessionId nextId = 1;
			return nextId++;
		}

		VansGraphics::VansAnimationNode* ResolveSceneAnimationPreviewNode(
			VansGraphics::VansScene* scene,
			const std::string& entityGuid,
			const std::string& animationComponentGuid)
		{
			if (!scene || entityGuid.empty() || animationComponentGuid.empty())
				return nullptr;
			Vans::VansRuntimeWorld* world = scene->GetRuntimeWorld();
			if (!world)
				return nullptr;
			auto* storage = static_cast<Vans::VansComponentStorage<
				Vans::VansRuntimeAnimationComponent>*>(world->FindStorage(
					Vans::VansRuntimeComponentType_Animation));
			if (!storage)
				return nullptr;
			const auto& components = storage->DenseData();
			const auto& headers = storage->Headers();
			for (std::size_t index = 0;
				index < components.size() && index < headers.size(); ++index)
			{
				const Vans::VansEntityRecord* owner =
					world->Entities().Get(headers[index].owner);
				if (owner && owner->stableGuid == entityGuid &&
					headers[index].stableGuid == animationComponentGuid)
				{
					return components[index].animationNode;
				}
			}
			return nullptr;
		}

		VansGraphics::VansAnimationController* ResolveAnimationPreviewController(
			AnimationPreviewSessionState& session,
			VansGraphics::VansScene* scene)
		{
			if (session.targetKind == AnimationPreviewTargetKind::IsolatedModel)
				return session.controller.get();
			VansGraphics::VansAnimationNode* node = ResolveSceneAnimationPreviewNode(
				scene, session.entityGuid, session.animationComponentGuid);
			return node ? node->GetCharacterMotionController() : nullptr;
		}

		VansGraphics::VansAnimationController* ResolveAnimationPreviewPoseController(
			AnimationPreviewSessionState& session,
			VansGraphics::VansScene* scene)
		{
			if (session.targetKind == AnimationPreviewTargetKind::IsolatedModel)
				return session.controller.get();
			VansGraphics::VansAnimationNode* node = ResolveSceneAnimationPreviewNode(
				scene, session.entityGuid, session.animationComponentGuid);
			return node ? node->GetController() : nullptr;
		}

		AnimationPreviewRigContext ResolveAnimationPreviewRigContext(
			AnimationPreviewSessionState& session,
			VansGraphics::VansScene* scene)
		{
			AnimationPreviewRigContext context;
			context.sessionId = session.id;
			context.sceneContentRevision = session.sceneContentRevision;
			context.entityGuid = session.entityGuid;
			context.animationComponentGuid = session.animationComponentGuid;
			context.controller = ResolveAnimationPreviewPoseController(session, scene);
			if (session.targetKind == AnimationPreviewTargetKind::IsolatedModel)
			{
				context.skeleton = &session.skeleton;
				return context;
			}
			auto* node = ResolveSceneAnimationPreviewNode(
				scene, session.entityGuid, session.animationComponentGuid);
			if (!node)
				return context;
			context.skeleton = &node->GetSkeleton();
			if (VansGraphics::VansTransformStore::IsAllocated(node->GetTransformID()))
				context.ownerWorld = VansGraphics::VansTransformStore::GetTransform(
					node->GetTransformID()).GetModelMatrix();
			context.retargetEnabled = node->IsRetargetEnabled();
			if (context.retargetEnabled)
			{
				const auto& desc = node->GetRetargetRuntimeDesc();
				context.retargetProfilePath = desc.profilePath;
				context.retargetSourceModelPath = desc.sourceModelPath;
				context.retargetSourceAnimatorPath = desc.sourceAnimatorPath;
			}
			return context;
		}

		void SetSceneAnimationPreviewSpeed(
			VansGraphics::VansAnimationNode& node, float speed)
		{
			if (VansGraphics::VansAnimationController* target = node.GetController())
				target->SetSpeed(speed);
			if (node.IsRetargetEnabled())
				if (VansGraphics::VansAnimationController* source =
					node.GetRetargetSourceController())
					source->SetSpeed(speed);
		}

		void AccumulateAnimationPreviewRootMotion(
			const glm::vec3& delta,
			glm::vec3& position,
			std::vector<glm::vec3>& trail)
		{
			position += delta;
			const glm::vec3 trailDelta = trail.empty()
				? position : position - trail.back();
			if (trail.empty() || glm::dot(trailDelta, trailDelta) > 1.0e-8f)
			{
				trail.push_back(position);
				if (trail.size() > 512)
					trail.erase(trail.begin(), trail.begin() + 128);
			}
		}

		bool SeekSceneAnimationPreview(
			VansGraphics::VansScene& scene,
			VansGraphics::VansAnimationNode& node,
			float targetSeconds,
			float previewSpeed,
			AnimationPreviewPlaybackRequest::RootMotionMode rootMotionMode,
			glm::vec3& rootMotionPosition,
			std::vector<glm::vec3>& rootMotionTrail,
			std::string& diagnostic)
		{
			VansGraphics::VansAnimationController* controller =
				node.GetCharacterMotionController();
			if (!controller)
			{
				diagnostic = "Scene preview controller is no longer available";
				return false;
			}
			if (node.IsGraphSetTransitioning())
			{
				diagnostic =
					"Finish the active Graph Set transition before seeking the preview timeline";
				return false;
			}
			if (const VansGraphics::MotionMatchingDebugData* motionMatching =
				controller->GetMotionMatchingDebugData();
				motionMatching && motionMatching->usedThisFrame)
			{
				diagnostic =
					"Motion Matching and Turn-in-Place Warping require forward playback with a trajectory; arbitrary seek is disabled for the active graph";
				return false;
			}

			const float duration = controller->GetCurrentDuration();
			if (!(duration > 0.0f))
			{
				diagnostic = "The active Graph Set has no finite preview range";
				return false;
			}
			const float clampedTarget = std::clamp(targetSeconds, 0.0f, duration);
			SetSceneAnimationPreviewSpeed(node, 1.0f);
			node.Play(VansGraphics::VansAnimationEvaluationPurpose::EditorPreview);
			constexpr float fixedStep = 1.0f / 60.0f;
			float elapsed = 0.0f;
			while (elapsed + 0.00001f < clampedTarget)
			{
				const float step = (std::min)(fixedStep, clampedTarget - elapsed);
				if (!scene.EvaluateEditorAnimationPreviewStep(&node, step))
				{
					diagnostic = "Scene rejected the Editor animation preview step";
					return false;
				}
				if (rootMotionMode !=
					AnimationPreviewPlaybackRequest::RootMotionMode::InPlace)
				{
					AccumulateAnimationPreviewRootMotion(
						node.GetRootMotionDelta(), rootMotionPosition,
						rootMotionTrail);
				}
				elapsed += step;
			}
			node.Pause();
			SetSceneAnimationPreviewSpeed(node, previewSpeed);
			diagnostic.clear();
			return true;
		}

		bool SeekIsolatedAnimationPreview(
			AnimationPreviewSessionState& session,
			float targetSeconds)
		{
			if (!session.controller)
				return false;
			if (session.controller->IsGraphSetTransitioning())
			{
				session.diagnostic =
					"Finish the active Graph Set transition before seeking the preview timeline";
				return false;
			}
			if (const VansGraphics::MotionMatchingDebugData* motionMatching =
				session.controller->GetMotionMatchingDebugData();
				motionMatching && motionMatching->usedThisFrame)
			{
				session.diagnostic =
					"Motion Matching requires forward playback with trajectory history; arbitrary seek is disabled for the active graph";
				return false;
			}
			const float duration = session.controller->GetCurrentDuration();
			if (!(duration > 0.0f))
			{
				session.diagnostic = "The active Graph Set has no finite preview range";
				return false;
			}
			const float clampedTarget = std::clamp(targetSeconds, 0.0f, duration);
			session.controller->SetSpeed(1.0f);
			session.controller->Play();
			constexpr float fixedStep = 1.0f / 60.0f;
			float elapsed = 0.0f;
			while (elapsed + 0.00001f < clampedTarget)
			{
				const float step = (std::min)(fixedStep, clampedTarget - elapsed);
				session.controller->Update(step, session.skeleton);
				if (session.rootMotionMode !=
					AnimationPreviewPlaybackRequest::RootMotionMode::InPlace)
				{
					AccumulateAnimationPreviewRootMotion(
						session.controller->GetRootMotionDelta(),
						session.rootMotionPosition, session.rootMotionTrail);
				}
				elapsed += step;
			}
			session.controller->Pause();
			session.controller->SetSpeed(session.speed);
			session.diagnostic.clear();
			session.renderDirty = true;
			return true;
		}

		std::vector<AnimationPreviewSessionId> CollectSceneAnimationPreviewSessions()
		{
			std::vector<AnimationPreviewSessionId> ids;
			for (const auto& [id, session] : GetAnimationPreviewSessions())
				if (session && session->targetKind ==
					AnimationPreviewTargetKind::SceneAnimationComponent)
					ids.push_back(id);
			return ids;
		}

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
			VansGraphics::VansScene* scene,
			std::uint32_t transformId,
			const std::string& entityGuid,
			RuntimeTransformSpace space,
			RuntimeTransformSnapshot& snapshot)
		{
			if (transformId >= VansGraphics::VansTransformStore::GlobalTransforms.size())
				return false;
			if (space == RuntimeTransformSpace::Model)
				return false;
			Vans::VansLocalTransform local;
			if (space == RuntimeTransformSpace::Local)
			{
				if (!scene || !scene->TryGetEntityLocalTransformByGuid(entityGuid, local))
					return false;
			}
			else
			{
				const auto& transform = VansGraphics::VansTransformStore::GetTransform(transformId);
				local.position = transform.m_Position;
				local.rotation = glm::quat(glm::radians(transform.m_Rotation));
				local.scale = transform.m_Scale;
			}
			snapshot.available = true;
			snapshot.entityGuid = entityGuid;
			snapshot.space = space;
			snapshot.position = ToEditorVec3(local.position);
			snapshot.rotationDegrees = ToEditorVec3(glm::degrees(glm::eulerAngles(local.rotation)));
			snapshot.scale = ToEditorVec3(local.scale);
			return true;
		}

		bool ApplyRuntimeTransformById(
			VansGraphics::VansScene* scene,
			std::uint32_t transformId,
			const RuntimeTransformEdit& edit)
		{
			if (transformId >= VansGraphics::VansTransformStore::GlobalTransforms.size())
				return false;
			if (edit.space == RuntimeTransformSpace::Model)
				return false;
			if (edit.space == RuntimeTransformSpace::Local)
			{
				Vans::VansLocalTransform transform;
				if (!scene || !scene->TryGetEntityLocalTransformByGuid(edit.entityGuid, transform))
					return false;
				if (edit.writePosition)
					transform.position = ToRuntimeVec3(edit.position);
				if (edit.writeRotation)
					transform.rotation = glm::quat(glm::radians(ToRuntimeVec3(edit.rotationDegrees)));
				if (edit.writeScale)
					transform.scale = ToRuntimeVec3(edit.scale);
				return scene->SetEntityLocalTransformByGuid(edit.entityGuid, transform);
			}

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

			VansGraphics::VansLightManager* lightManager = scene->GetLightManager();
			if (!lightManager)
				return;

			lightManager->RefreshDerivedLightingState();
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
			edit.space = snapshot.space;
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
					m_HasBefore = ReadRuntimeTransformById(
						scene, transformId, m_Edit.entityGuid, m_Edit.space, m_Before);
					if (!m_HasBefore)
						return;
				}

				ApplyRuntimeTransformById(scene, transformId, m_Edit);
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;

				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				const std::uint32_t transformId = ResolveRuntimeTransformId(scene, m_Before.entityGuid);
				ApplyRuntimeTransformById(scene, transformId, MakeFullTransformEdit(m_Before));
			}

			std::string GetDescription() const override
			{
				return "Set runtime transform";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				const auto* next = dynamic_cast<const SetRuntimeTransformCommand*>(&other);
				return next && next->m_Edit.entityGuid == m_Edit.entityGuid
					&& next->m_Edit.space == m_Edit.space;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetRuntimeTransformCommand*>(&other);
				if (!next || next->m_Edit.entityGuid != m_Edit.entityGuid
					|| next->m_Edit.space != m_Edit.space)
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

			settings.waveParticle.particlesPerCascade = source.m_WaveParticle.m_ParticlesPerCascade;
			settings.waveParticle.rmsAmplitude = source.m_WaveParticle.m_RmsAmplitude;
			settings.waveParticle.packetWidth = source.m_WaveParticle.m_PacketWidth;
			settings.waveParticle.dispersionScale = source.m_WaveParticle.m_DispersionScale;
			settings.waveParticle.directionSpread = source.m_WaveParticle.m_DirectionSpread;
			settings.waveParticle.cascadeAmplitudeFalloff = source.m_WaveParticle.m_CascadeAmplitudeFalloff;
			settings.waveParticle.foamThreshold = source.m_WaveParticle.m_FoamThreshold;
			settings.waveParticle.foamSoftness = source.m_WaveParticle.m_FoamSoftness;
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

			settings.detailNormal.enabled = source.m_DetailNormal.m_Enabled;
			settings.detailNormal.decodeMode = static_cast<int>(source.m_DetailNormal.m_DecodeMode);
			settings.detailNormal.flipGreen = source.m_DetailNormal.m_FlipGreen;
			settings.detailNormal.globalStrength = source.m_DetailNormal.m_GlobalStrength;
			settings.detailNormal.maxSlope = source.m_DetailNormal.m_MaxSlope;
			settings.detailNormal.mipBias = source.m_DetailNormal.m_MipBias;
			settings.detailNormal.anisotropy = source.m_DetailNormal.m_Anisotropy;
			settings.detailNormal.layerCount = VansGraphics::VansWaterDetailNormalConfig::MAX_LAYER_COUNT;
			for (std::size_t layerIndex = 0; layerIndex < settings.detailNormal.layers.size(); ++layerIndex)
			{
				const auto& sourceLayer = source.m_DetailNormal.m_Layers[layerIndex];
				auto& destinationLayer = settings.detailNormal.layers[layerIndex];
				destinationLayer.enabled = sourceLayer.m_Enabled;
				destinationLayer.tileSizeMeters = sourceLayer.m_TileSizeMeters;
				destinationLayer.direction = ToEditorVec2(sourceLayer.m_Direction);
				destinationLayer.speedMetersPerSecond = sourceLayer.m_SpeedMetersPerSecond;
				destinationLayer.phase = sourceLayer.m_Phase;
				destinationLayer.strength = sourceLayer.m_Strength;
				destinationLayer.fadeStartMeters = sourceLayer.m_FadeStartMeters;
				destinationLayer.fadeEndMeters = sourceLayer.m_FadeEndMeters;
			}
			settings.effectiveRoughness.mode = static_cast<int>(source.m_EffectiveRoughness.m_Mode);
			settings.effectiveRoughness.distanceStartMeters = source.m_EffectiveRoughness.m_DistanceStartMeters;
			settings.effectiveRoughness.distanceEndMeters = source.m_EffectiveRoughness.m_DistanceEndMeters;
			settings.effectiveRoughness.distanceStrength = source.m_EffectiveRoughness.m_DistanceStrength;
			settings.colorMip.refractionScatterScale = source.m_ColorMip.m_RefractionScatterScale;
			settings.colorMip.refractionRoughnessScale = source.m_ColorMip.m_RefractionRoughnessScale;
			settings.colorMip.forwardScatterMipScale = source.m_ColorMip.m_ForwardScatterMipScale;
			settings.colorMip.backgroundScatterScale = source.m_ColorMip.m_BackgroundScatterScale;
			settings.colorMip.lodBias = source.m_ColorMip.m_LodBias;
			settings.shadow.enabled = source.m_Shadow.m_Enabled;
			settings.shadow.quality = source.m_Shadow.m_Quality;
			settings.shadow.depthBias = source.m_Shadow.m_DepthBias;
			settings.shadow.normalBias = source.m_Shadow.m_NormalBias;
			settings.shadow.volumeStepStride = source.m_Shadow.m_VolumeStepStride;

			settings.thinSSSEnabled = source.m_SSS.m_Enabled;
			settings.maxThicknessDistance = source.m_SSS.m_MaxThicknessDistance;
			settings.deepWaterThicknessFallback = source.m_SSS.m_DeepWaterThicknessFallback;
			settings.causticsEnabled = source.m_Caustics.m_Enabled;
			settings.causticsIntensity = source.m_Caustics.m_Intensity;
			settings.causticsMaxDistance = source.m_Caustics.m_MaxDistance;
			settings.causticsMaxGain = source.m_Caustics.m_MaxGain;
			settings.causticsFilterRadius = source.m_Caustics.m_FilterRadius;
			settings.refractionEnabled = source.m_Refraction.m_Enabled;
			settings.refractionDistortionStrength = source.m_Refraction.m_DistortionStrength;
			settings.ssrEnabled = source.m_SSR.m_Enabled;
			settings.ssrMaxDistance = source.m_SSR.m_MaxDistance;
			settings.ssrMaxRoughness = source.m_SSR.m_MaxRoughness;
			settings.ssrRoughnessFadeStart = source.m_SSR.m_RoughnessFadeStart;
			settings.ssrColorMipConeScale = source.m_SSR.m_ColorMipConeScale;
			settings.ssrColorMipBias = source.m_SSR.m_ColorMipBias;
			settings.ssrEdgeFadePixels = source.m_SSR.m_EdgeFadePixels;
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

			destination.m_WaveParticle.m_ParticlesPerCascade = settings.waveParticle.particlesPerCascade;
			destination.m_WaveParticle.m_RmsAmplitude = settings.waveParticle.rmsAmplitude;
			destination.m_WaveParticle.m_PacketWidth = settings.waveParticle.packetWidth;
			destination.m_WaveParticle.m_DispersionScale = settings.waveParticle.dispersionScale;
			destination.m_WaveParticle.m_DirectionSpread = settings.waveParticle.directionSpread;
			destination.m_WaveParticle.m_CascadeAmplitudeFalloff = settings.waveParticle.cascadeAmplitudeFalloff;
			destination.m_WaveParticle.m_FoamThreshold = settings.waveParticle.foamThreshold;
			destination.m_WaveParticle.m_FoamSoftness = settings.waveParticle.foamSoftness;
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

			destination.m_DetailNormal.m_Enabled = settings.detailNormal.enabled;
			destination.m_DetailNormal.m_DecodeMode = VansGraphics::VansWaterNormalDecodeMode::RGReconstructZ;
			destination.m_DetailNormal.m_FlipGreen = settings.detailNormal.flipGreen;
			destination.m_DetailNormal.m_GlobalStrength = settings.detailNormal.globalStrength;
			destination.m_DetailNormal.m_MaxSlope = settings.detailNormal.maxSlope;
			destination.m_DetailNormal.m_MipBias = settings.detailNormal.mipBias;
			destination.m_DetailNormal.m_Anisotropy = settings.detailNormal.anisotropy;
			const std::size_t detailLayerCount = (std::min)(
				std::size_t(settings.detailNormal.layerCount), settings.detailNormal.layers.size());
			for (std::size_t layerIndex = 0; layerIndex < destination.m_DetailNormal.m_Layers.size(); ++layerIndex)
			{
				auto& destinationLayer = destination.m_DetailNormal.m_Layers[layerIndex];
				if (layerIndex >= detailLayerCount)
				{
					destinationLayer.m_Enabled = false;
					continue;
				}
				const auto& sourceLayer = settings.detailNormal.layers[layerIndex];
				destinationLayer.m_Enabled = sourceLayer.enabled;
				destinationLayer.m_TileSizeMeters = sourceLayer.tileSizeMeters;
				destinationLayer.m_Direction = ToRuntimeVec2(sourceLayer.direction);
				destinationLayer.m_SpeedMetersPerSecond = sourceLayer.speedMetersPerSecond;
				destinationLayer.m_Phase = sourceLayer.phase;
				destinationLayer.m_Strength = sourceLayer.strength;
				destinationLayer.m_FadeStartMeters = sourceLayer.fadeStartMeters;
				destinationLayer.m_FadeEndMeters = sourceLayer.fadeEndMeters;
			}
			destination.m_EffectiveRoughness.m_Mode = settings.effectiveRoughness.mode == 1
				? VansGraphics::VansWaterEffectiveRoughnessMode::DistanceHeuristic
				: VansGraphics::VansWaterEffectiveRoughnessMode::BaseOnly;
			destination.m_EffectiveRoughness.m_DistanceStartMeters = settings.effectiveRoughness.distanceStartMeters;
			destination.m_EffectiveRoughness.m_DistanceEndMeters = settings.effectiveRoughness.distanceEndMeters;
			destination.m_EffectiveRoughness.m_DistanceStrength = settings.effectiveRoughness.distanceStrength;
			destination.m_ColorMip.m_RefractionScatterScale = settings.colorMip.refractionScatterScale;
			destination.m_ColorMip.m_RefractionRoughnessScale = settings.colorMip.refractionRoughnessScale;
			destination.m_ColorMip.m_ForwardScatterMipScale = settings.colorMip.forwardScatterMipScale;
			destination.m_ColorMip.m_BackgroundScatterScale = settings.colorMip.backgroundScatterScale;
			destination.m_ColorMip.m_LodBias = settings.colorMip.lodBias;
			destination.m_Shadow.m_Enabled = settings.shadow.enabled;
			destination.m_Shadow.m_Quality = settings.shadow.quality;
			destination.m_Shadow.m_DepthBias = settings.shadow.depthBias;
			destination.m_Shadow.m_NormalBias = settings.shadow.normalBias;
			destination.m_Shadow.m_VolumeStepStride = settings.shadow.volumeStepStride;

			destination.m_SSS.m_Enabled = settings.thinSSSEnabled;
			destination.m_SSS.m_MaxThicknessDistance = settings.maxThicknessDistance;
			destination.m_SSS.m_DeepWaterThicknessFallback = settings.deepWaterThicknessFallback;
			destination.m_Caustics.m_Enabled = settings.causticsEnabled;
			destination.m_Caustics.m_Intensity = settings.causticsIntensity;
			destination.m_Caustics.m_MaxDistance = settings.causticsMaxDistance;
			destination.m_Caustics.m_MaxGain = settings.causticsMaxGain;
			destination.m_Caustics.m_FilterRadius = settings.causticsFilterRadius;
			destination.m_Refraction.m_Enabled = settings.refractionEnabled;
			destination.m_Refraction.m_DistortionStrength = settings.refractionDistortionStrength;
			destination.m_SSR.m_Enabled = settings.ssrEnabled;
			destination.m_SSR.m_MaxDistance = settings.ssrMaxDistance;
			destination.m_SSR.m_MaxRoughness = settings.ssrMaxRoughness;
			destination.m_SSR.m_RoughnessFadeStart = settings.ssrRoughnessFadeStart;
			destination.m_SSR.m_ColorMipConeScale = settings.ssrColorMipConeScale;
			destination.m_SSR.m_ColorMipBias = settings.ssrColorMipBias;
			destination.m_SSR.m_EdgeFadePixels = settings.ssrEdgeFadePixels;
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

		bool ShouldReinitializeWaterResources(
			const VansGraphics::VansWaterConfig& previous,
			const VansGraphics::VansWaterConfig& current)
		{
			return previous.m_Volume.m_ResolutionScale != current.m_Volume.m_ResolutionScale
				|| previous.m_Geometry.m_MeshDim != current.m_Geometry.m_MeshDim
				|| previous.m_DetailNormal.m_Anisotropy != current.m_DetailNormal.m_Anisotropy;
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
				|| previous.m_Spectrum.m_CascadeCount != current.m_Spectrum.m_CascadeCount
				|| previous.m_Spectrum.m_BaseCoverage != current.m_Spectrum.m_BaseCoverage
				|| previous.m_Spectrum.m_CascadeScale != current.m_Spectrum.m_CascadeScale
				|| previous.m_Spectrum.m_MinWavelength != current.m_Spectrum.m_MinWavelength
				|| previous.m_WaveParticle.m_ParticlesPerCascade != current.m_WaveParticle.m_ParticlesPerCascade
				|| previous.m_WaveParticle.m_PacketWidth != current.m_WaveParticle.m_PacketWidth
				|| previous.m_WaveParticle.m_DirectionSpread != current.m_WaveParticle.m_DirectionSpread
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

		ScenePropertyValue Vec2ScenePropertyValue(const glm::vec2& value)
		{
			return ScenePropertyValues::Array({
				ScenePropertyValues::Float(value.x),
				ScenePropertyValues::Float(value.y)
			});
		}

		ScenePropertyValue WaterScenePropertyValue(const VansGraphics::VansWaterConfig& config)
		{
			std::vector<ScenePropertyValue> detailLayers;
			detailLayers.reserve(config.m_DetailNormal.m_Layers.size());
			for (const VansGraphics::VansWaterDetailNormalLayerConfig& layer : config.m_DetailNormal.m_Layers)
			{
				detailLayers.push_back(ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(layer.m_Enabled) },
					{ "tileSizeMeters", ScenePropertyValues::Float(layer.m_TileSizeMeters) },
					{ "direction", Vec2ScenePropertyValue(layer.m_Direction) },
					{ "speedMetersPerSecond", ScenePropertyValues::Float(layer.m_SpeedMetersPerSecond) },
					{ "phase", ScenePropertyValues::Float(layer.m_Phase) },
					{ "strength", ScenePropertyValues::Float(layer.m_Strength) },
					{ "fadeStartMeters", ScenePropertyValues::Float(layer.m_FadeStartMeters) },
					{ "fadeEndMeters", ScenePropertyValues::Float(layer.m_FadeEndMeters) }
				}));
			}

			const char* waveMode = config.m_Spectrum.m_Mode == VansGraphics::VansWaveMode::Gerstner
				? "gerstner"
				: config.m_Spectrum.m_Mode == VansGraphics::VansWaveMode::FFT ? "fft" : "waveParticle";
			const char* roughnessMode =
				config.m_EffectiveRoughness.m_Mode == VansGraphics::VansWaterEffectiveRoughnessMode::DistanceHeuristic
				? "distanceHeuristic" : "baseOnly";

			return ScenePropertyValues::Object({
				{ "level", ScenePropertyValues::Float(config.m_WaterLevel) },
				{ "specularIntensity", ScenePropertyValues::Float(config.m_SpecularIntensity) },
				{ "medium", ScenePropertyValues::Object({
					{ "absorption", Vec3ScenePropertyValue(config.m_Medium.m_AbsorptionCoeff) },
					{ "scattering", Vec3ScenePropertyValue(config.m_Medium.m_ScatteringCoeff) },
					{ "ior", ScenePropertyValues::Float(config.m_Medium.m_IOR) },
					{ "anisotropy", ScenePropertyValues::Float(config.m_Medium.m_Anisotropy) },
					{ "roughness", ScenePropertyValues::Float(config.m_Medium.m_WaterRoughness) }
				}) },
				{ "geometry", ScenePropertyValues::Object({
					{ "lodCount", ScenePropertyValues::Int(config.m_Geometry.m_LodCount) },
					{ "basePatchSize", ScenePropertyValues::Float(config.m_Geometry.m_BasePatchSize) },
					{ "meshDim", ScenePropertyValues::Int(config.m_Geometry.m_MeshDim) },
					{ "morphStartRatio", ScenePropertyValues::Float(config.m_Geometry.m_MorphStartRatio) }
				}) },
				{ "spectrum", ScenePropertyValues::Object({
					{ "mode", ScenePropertyValues::String(waveMode) },
					{ "cascadeCount", ScenePropertyValues::Int(config.m_Spectrum.m_CascadeCount) },
					{ "baseCoverage", ScenePropertyValues::Float(config.m_Spectrum.m_BaseCoverage) },
					{ "cascadeScale", ScenePropertyValues::Float(config.m_Spectrum.m_CascadeScale) },
					{ "windDirection", Vec2ScenePropertyValue(config.m_Spectrum.m_WindDirection) },
					{ "windSpeed", ScenePropertyValues::Float(config.m_Spectrum.m_WindSpeed) },
					{ "swellAmplitude", ScenePropertyValues::Float(config.m_Spectrum.m_SwellAmplitude) },
					{ "choppiness", ScenePropertyValues::Float(config.m_Spectrum.m_Choppiness) },
					{ "gerstnerWaveCount", ScenePropertyValues::Int(config.m_Spectrum.m_GerstnerWaveCount) },
					{ "spectrumAmplitude", ScenePropertyValues::Float(config.m_Spectrum.m_SpectrumAmplitude) },
					{ "minWavelength", ScenePropertyValues::Float(config.m_Spectrum.m_MinWavelength) },
					{ "smallWaveDamping", ScenePropertyValues::Float(config.m_Spectrum.m_SmallWaveDamping) },
					{ "windDependency", ScenePropertyValues::Float(config.m_Spectrum.m_WindDependency) },
					{ "depth", ScenePropertyValues::Float(config.m_Spectrum.m_Depth) },
					{ "repeatPeriod", ScenePropertyValues::Float(config.m_Spectrum.m_RepeatPeriod) },
					{ "randomSeed", ScenePropertyValues::Int(config.m_Spectrum.m_RandomSeed) }
				}) },
				{ "waveParticle", ScenePropertyValues::Object({
					{ "particlesPerCascade", ScenePropertyValues::Int(config.m_WaveParticle.m_ParticlesPerCascade) },
					{ "rmsAmplitude", ScenePropertyValues::Float(config.m_WaveParticle.m_RmsAmplitude) },
					{ "packetWidth", ScenePropertyValues::Float(config.m_WaveParticle.m_PacketWidth) },
					{ "dispersionScale", ScenePropertyValues::Float(config.m_WaveParticle.m_DispersionScale) },
					{ "directionSpread", ScenePropertyValues::Float(config.m_WaveParticle.m_DirectionSpread) },
					{ "cascadeAmplitudeFalloff", ScenePropertyValues::Float(config.m_WaveParticle.m_CascadeAmplitudeFalloff) },
					{ "foamThreshold", ScenePropertyValues::Float(config.m_WaveParticle.m_FoamThreshold) },
					{ "foamSoftness", ScenePropertyValues::Float(config.m_WaveParticle.m_FoamSoftness) },
					{ "randomSeed", ScenePropertyValues::Int(config.m_WaveParticle.m_RandomSeed) }
				}) },
				{ "flowMap", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_FlowMap.m_Enabled) },
					{ "strength", ScenePropertyValues::Float(config.m_FlowMap.m_Strength) },
					{ "speed", ScenePropertyValues::Float(config.m_FlowMap.m_Speed) },
					{ "phaseLength", ScenePropertyValues::Float(config.m_FlowMap.m_PhaseLength) },
					{ "noiseAmount", ScenePropertyValues::Float(config.m_FlowMap.m_NoiseAmount) },
					{ "worldOrigin", Vec2ScenePropertyValue(config.m_FlowMap.m_WorldOrigin) },
					{ "worldSize", Vec2ScenePropertyValue(config.m_FlowMap.m_WorldSize) },
					{ "fallbackDirection", Vec2ScenePropertyValue(config.m_FlowMap.m_FallbackDirection) }
				}) },
				{ "detailNormal", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_DetailNormal.m_Enabled) },
					{ "decodeMode", ScenePropertyValues::String("rgReconstructZ") },
					{ "flipGreen", ScenePropertyValues::Bool(config.m_DetailNormal.m_FlipGreen) },
					{ "globalStrength", ScenePropertyValues::Float(config.m_DetailNormal.m_GlobalStrength) },
					{ "maxSlope", ScenePropertyValues::Float(config.m_DetailNormal.m_MaxSlope) },
					{ "mipBias", ScenePropertyValues::Float(config.m_DetailNormal.m_MipBias) },
					{ "anisotropy", ScenePropertyValues::Float(config.m_DetailNormal.m_Anisotropy) },
					{ "layers", ScenePropertyValues::Array(std::move(detailLayers)) }
				}) },
				{ "effectiveRoughness", ScenePropertyValues::Object({
					{ "mode", ScenePropertyValues::String(roughnessMode) },
					{ "distanceStartMeters", ScenePropertyValues::Float(config.m_EffectiveRoughness.m_DistanceStartMeters) },
					{ "distanceEndMeters", ScenePropertyValues::Float(config.m_EffectiveRoughness.m_DistanceEndMeters) },
					{ "distanceStrength", ScenePropertyValues::Float(config.m_EffectiveRoughness.m_DistanceStrength) }
				}) },
				{ "colorMip", ScenePropertyValues::Object({
					{ "refractionScatterScale", ScenePropertyValues::Float(config.m_ColorMip.m_RefractionScatterScale) },
					{ "refractionRoughnessScale", ScenePropertyValues::Float(config.m_ColorMip.m_RefractionRoughnessScale) },
					{ "forwardScatterMipScale", ScenePropertyValues::Float(config.m_ColorMip.m_ForwardScatterMipScale) },
					{ "backgroundScatterScale", ScenePropertyValues::Float(config.m_ColorMip.m_BackgroundScatterScale) },
					{ "lodBias", ScenePropertyValues::Float(config.m_ColorMip.m_LodBias) }
				}) },
				{ "shadow", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_Shadow.m_Enabled) },
					{ "quality", ScenePropertyValues::Int(config.m_Shadow.m_Quality) },
					{ "depthBias", ScenePropertyValues::Float(config.m_Shadow.m_DepthBias) },
					{ "normalBias", ScenePropertyValues::Float(config.m_Shadow.m_NormalBias) },
					{ "volumeStepStride", ScenePropertyValues::Int(config.m_Shadow.m_VolumeStepStride) }
				}) },
				{ "optics", ScenePropertyValues::Object({
					{ "maxCrossDistance", ScenePropertyValues::Float(config.m_Optics.m_MaxCrossDistance) },
					{ "maxRefractionCrossDistance", ScenePropertyValues::Float(config.m_Optics.m_MaxRefractionCrossDistance) },
					{ "multiScatterScale", ScenePropertyValues::Float(config.m_Optics.m_MultiScatterScale) },
					{ "waterDispersionStrength", ScenePropertyValues::Float(config.m_Optics.m_WaterDispersionStrength) },
					{ "sssPathScale", ScenePropertyValues::Float(config.m_Optics.m_SSSPathScale) },
					{ "sssNonlinearStrength", ScenePropertyValues::Float(config.m_Optics.m_SSSNonlinearStrength) },
					{ "sssScatterBoost", ScenePropertyValues::Float(config.m_Optics.m_SSSScatterBoost) },
					{ "backlitPathScale", ScenePropertyValues::Float(config.m_Optics.m_BacklitPathScale) },
					{ "backlitPhaseG", ScenePropertyValues::Float(config.m_Optics.m_BacklitPhaseG) }
				}) },
				{ "volume", ScenePropertyValues::Object({
					{ "resolutionScale", ScenePropertyValues::Float(config.m_Volume.m_ResolutionScale) },
					{ "sampleCount", ScenePropertyValues::Int(config.m_Volume.m_SampleCount) },
					{ "spatialFilterIterations", ScenePropertyValues::Int(config.m_Volume.m_SpatialFilterIterations) },
					{ "spatialDepthSensitivity", ScenePropertyValues::Float(config.m_Volume.m_SpatialDepthSensitivity) }
				}) },
				{ "sss", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_SSS.m_Enabled) },
					{ "maxThickness", ScenePropertyValues::Float(config.m_SSS.m_MaxThicknessDistance) },
					{ "deepFallback", ScenePropertyValues::Float(config.m_SSS.m_DeepWaterThicknessFallback) }
				}) },
				{ "caustics", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_Caustics.m_Enabled) },
					{ "intensity", ScenePropertyValues::Float(config.m_Caustics.m_Intensity) },
					{ "maxDistance", ScenePropertyValues::Float(config.m_Caustics.m_MaxDistance) },
					{ "maxGain", ScenePropertyValues::Float(config.m_Caustics.m_MaxGain) },
					{ "filterRadius", ScenePropertyValues::Float(config.m_Caustics.m_FilterRadius) }
				}) },
				{ "refraction", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_Refraction.m_Enabled) },
					{ "distortionStrength", ScenePropertyValues::Float(config.m_Refraction.m_DistortionStrength) }
				}) },
				{ "ssr", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(config.m_SSR.m_Enabled) },
					{ "maxDistance", ScenePropertyValues::Float(config.m_SSR.m_MaxDistance) },
					{ "maxRoughness", ScenePropertyValues::Float(config.m_SSR.m_MaxRoughness) },
					{ "roughnessFadeStart", ScenePropertyValues::Float(config.m_SSR.m_RoughnessFadeStart) },
					{ "colorMipConeScale", ScenePropertyValues::Float(config.m_SSR.m_ColorMipConeScale) },
					{ "colorMipBias", ScenePropertyValues::Float(config.m_SSR.m_ColorMipBias) },
					{ "edgeFadePixels", ScenePropertyValues::Float(config.m_SSR.m_EdgeFadePixels) }
				}) }
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

		CloudSettings ToCloudSettings(const Vans::VansSceneVolumetricCloudSettingsConfig& source)
		{
			CloudSettings settings;
			settings.enabled = source.enabled;
			settings.cloudMinHeight = source.cloudMinHeight;
			settings.cloudMaxHeight = source.cloudMaxHeight;
			settings.density = source.density;
			settings.coverage = source.coverage;
			settings.sunBrightness = source.sunBrightness;
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
			settings.sigmaTRef = source.sigmaTRef;
			settings.viewAbsorption = source.viewAbsorption;
			settings.lightAbsorption = source.lightAbsorption;
			settings.singleScatteringAlbedo = source.singleScatteringAlbedo;
			settings.forwardEccentricity = source.forwardEccentricity;
			settings.backwardEccentricity = source.backwardEccentricity;
			settings.msAttenuation = source.msAttenuation;
			settings.msContribution = source.msContribution;
			settings.msEccentricity = source.msEccentricity;
			settings.scatteringTintR = source.scatteringTintR;
			settings.scatteringTintG = source.scatteringTintG;
			settings.scatteringTintB = source.scatteringTintB;
			settings.scatterSourceODScale = source.scatterSourceODScale;
			settings.scatterSourceCurvePow = source.scatterSourceCurvePow;
			settings.aoUpwardScale = source.aoUpwardScale;
			settings.ambientBottomStrength = source.ambientBottomStrength;
			settings.ambientTopStrength = source.ambientTopStrength;
			settings.ambientDuskWarmth = source.ambientDuskWarmth;
			settings.boundaryConfidence = source.boundaryConfidence;
			settings.boundaryWrap = source.boundaryWrap;
			settings.phiFwdIntensity = source.phiFwdIntensity;
			settings.phiFwdDepthPow = source.phiFwdDepthPow;
			settings.phiFwdDepthBias = source.phiFwdDepthBias;
			settings.phiFwdMSBuildScale = source.phiFwdMSBuildScale;
			settings.phiFwdCompress = source.phiFwdCompress;
			settings.phiFwdMaxDistance = source.phiFwdMaxDistance;
			settings.phiFwdConeRatio = source.phiFwdConeRatio;
			settings.phiFwdMinStep = source.phiFwdMinStep;
			settings.lightStepCount = source.lightStepCount;
			settings.boundaryGradientStep = source.boundaryGradientStep;
			settings.boundaryGradientStrength = source.boundaryGradientStrength;
			settings.shadingDebugMode = source.shadingDebugMode;
			settings.shadow.enabled = source.shadow.enabled;
			settings.shadow.atmosphereStrength = source.shadow.atmosphereStrength;
			settings.shadow.ambientOcclusionStrength = source.shadow.ambientOcclusionStrength;
			return settings;
		}

		Vans::VansSceneVolumetricCloudSettingsConfig ToRuntimeCloudSettings(
			const CloudSettings& settings,
			Vans::VansSceneVolumetricCloudSettingsConfig destination = {})
		{
			destination.enabled = settings.enabled;
			destination.cloudMinHeight = settings.cloudMinHeight;
			destination.cloudMaxHeight = settings.cloudMaxHeight;
			destination.density = settings.density;
			destination.coverage = settings.coverage;
			destination.sunBrightness = settings.sunBrightness;
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
			destination.sigmaTRef = settings.sigmaTRef;
			destination.viewAbsorption = settings.viewAbsorption;
			destination.lightAbsorption = settings.lightAbsorption;
			destination.singleScatteringAlbedo = settings.singleScatteringAlbedo;
			destination.forwardEccentricity = settings.forwardEccentricity;
			destination.backwardEccentricity = settings.backwardEccentricity;
			destination.msAttenuation = settings.msAttenuation;
			destination.msContribution = settings.msContribution;
			destination.msEccentricity = settings.msEccentricity;
			destination.scatteringTintR = settings.scatteringTintR;
			destination.scatteringTintG = settings.scatteringTintG;
			destination.scatteringTintB = settings.scatteringTintB;
			destination.scatterSourceODScale = settings.scatterSourceODScale;
			destination.scatterSourceCurvePow = settings.scatterSourceCurvePow;
			destination.aoUpwardScale = settings.aoUpwardScale;
			destination.ambientBottomStrength = settings.ambientBottomStrength;
			destination.ambientTopStrength = settings.ambientTopStrength;
			destination.ambientDuskWarmth = settings.ambientDuskWarmth;
			destination.boundaryConfidence = settings.boundaryConfidence;
			destination.boundaryWrap = settings.boundaryWrap;
			destination.phiFwdIntensity = settings.phiFwdIntensity;
			destination.phiFwdDepthPow = settings.phiFwdDepthPow;
			destination.phiFwdDepthBias = settings.phiFwdDepthBias;
			destination.phiFwdMSBuildScale = settings.phiFwdMSBuildScale;
			destination.phiFwdCompress = settings.phiFwdCompress;
			destination.phiFwdMaxDistance = settings.phiFwdMaxDistance;
			destination.phiFwdConeRatio = settings.phiFwdConeRatio;
			destination.phiFwdMinStep = settings.phiFwdMinStep;
			destination.lightStepCount = settings.lightStepCount;
			destination.boundaryGradientStep = settings.boundaryGradientStep;
			destination.boundaryGradientStrength = settings.boundaryGradientStrength;
			destination.shadingDebugMode = settings.shadingDebugMode;
			destination.shadow.enabled = settings.shadow.enabled;
			destination.shadow.atmosphereStrength = settings.shadow.atmosphereStrength;
			destination.shadow.ambientOcclusionStrength = settings.shadow.ambientOcclusionStrength;
			return destination;
		}

		EnvironmentSettings ToAPIEnvironmentSettings(
			const Vans::VansSceneEnvironmentSettingsConfig& source)
		{
			EnvironmentSettings settings;
			settings.planet.centerWorldMeters = source.planet.centerWorldMeters;
			settings.planet.bottomRadiusMeters = source.planet.bottomRadiusMeters;
			settings.planet.atmosphereHeightMeters = source.planet.atmosphereHeightMeters;
			const auto& src = source.physicalAtmosphere;
			auto& dst = settings.physicalAtmosphere;
			dst.enabled = src.enabled;
			dst.groundAlbedo = src.groundAlbedo;
			dst.rayleigh.scatteringPerMeterAtGround =
				src.rayleigh.scatteringPerMeterAtGround;
			dst.rayleigh.densityScaleHeightMeters =
				src.rayleigh.densityScaleHeightMeters;
			dst.mie.scatteringPerMeterAtGround = src.mie.scatteringPerMeterAtGround;
			dst.mie.absorptionPerMeterAtGround = src.mie.absorptionPerMeterAtGround;
			dst.mie.densityScaleHeightMeters = src.mie.densityScaleHeightMeters;
			dst.mie.anisotropy = src.mie.anisotropy;
			dst.ozone.absorptionPerMeter = src.ozone.absorptionPerMeter;
			dst.ozone.centerAltitudeMeters = src.ozone.centerAltitudeMeters;
			dst.ozone.halfWidthMeters = src.ozone.halfWidthMeters;
			dst.aerialPerspective.distanceScale = src.aerialPerspective.distanceScale;
			dst.mainLightVolumetricScatteringScale =
				src.mainLightVolumetricScatteringScale;
			dst.celestialBodies.reserve(src.celestialBodies.size());
			for (const auto& sourceBody : src.celestialBodies)
			{
				CelestialBodySettings body;
				body.name = sourceBody.name;
				body.lightEntityId = sourceBody.lightEntityId;
				body.disk.enabled = sourceBody.disk.enabled;
				body.disk.angularRadiusRadians = sourceBody.disk.angularRadiusRadians;
				body.disk.featherRadians = sourceBody.disk.featherRadians;
				body.disk.radianceScale = sourceBody.disk.radianceScale;
				body.disk.occlusionStrength = sourceBody.disk.occlusionStrength;
				dst.celestialBodies.push_back(std::move(body));
			}
			settings.heightFog.enabled = source.heightFog.enabled;
			settings.heightFog.groundHeightWorldMeters =
				source.heightFog.groundHeightWorldMeters;
			settings.heightFog.visibilityAtGroundMeters =
				source.heightFog.visibilityAtGroundMeters;
			settings.heightFog.densityFalloffHeightMeters =
				source.heightFog.densityFalloffHeightMeters;
			settings.heightFog.startDistanceMeters = source.heightFog.startDistanceMeters;
			settings.heightFog.nearFadeDistanceMeters =
				source.heightFog.nearFadeDistanceMeters;
			settings.heightFog.maximumDistanceMeters =
				source.heightFog.maximumDistanceMeters;
			settings.heightFog.farFadeDistanceMeters =
				source.heightFog.farFadeDistanceMeters;
			settings.heightFog.singleScatteringAlbedo =
				source.heightFog.singleScatteringAlbedo;
			settings.heightFog.anisotropy = source.heightFog.anisotropy;
			settings.heightFog.emissivePerMeter = source.heightFog.emissivePerMeter;
			settings.heightFog.skyLightingScale = source.heightFog.skyLightingScale;
			settings.heightFog.mainLightVolumetricScale =
				source.heightFog.mainLightVolumetricScale;
			settings.heightFog.receiveCloudShadows =
				source.heightFog.receiveCloudShadows;
			settings.volumetricClouds = ToCloudSettings(source.volumetricClouds);
			return settings;
		}

		Vans::VansSceneEnvironmentSettingsConfig ToRuntimeEnvironmentSettings(
			const EnvironmentSettings& source)
		{
			Vans::VansSceneEnvironmentSettingsConfig settings;
			settings.planet.centerWorldMeters = source.planet.centerWorldMeters;
			settings.planet.bottomRadiusMeters = source.planet.bottomRadiusMeters;
			settings.planet.atmosphereHeightMeters = source.planet.atmosphereHeightMeters;
			const auto& src = source.physicalAtmosphere;
			auto& dst = settings.physicalAtmosphere;
			dst.enabled = src.enabled;
			dst.groundAlbedo = src.groundAlbedo;
			dst.rayleigh.scatteringPerMeterAtGround =
				src.rayleigh.scatteringPerMeterAtGround;
			dst.rayleigh.densityScaleHeightMeters =
				src.rayleigh.densityScaleHeightMeters;
			dst.mie.scatteringPerMeterAtGround = src.mie.scatteringPerMeterAtGround;
			dst.mie.absorptionPerMeterAtGround = src.mie.absorptionPerMeterAtGround;
			dst.mie.densityScaleHeightMeters = src.mie.densityScaleHeightMeters;
			dst.mie.anisotropy = src.mie.anisotropy;
			dst.ozone.absorptionPerMeter = src.ozone.absorptionPerMeter;
			dst.ozone.centerAltitudeMeters = src.ozone.centerAltitudeMeters;
			dst.ozone.halfWidthMeters = src.ozone.halfWidthMeters;
			dst.aerialPerspective.distanceScale = src.aerialPerspective.distanceScale;
			dst.mainLightVolumetricScatteringScale =
				src.mainLightVolumetricScatteringScale;
			dst.celestialBodies.reserve(src.celestialBodies.size());
			for (const auto& sourceBody : src.celestialBodies)
			{
				Vans::VansSceneCelestialBodySettingsConfig body;
				body.name = sourceBody.name;
				body.lightEntityId = sourceBody.lightEntityId;
				body.disk.enabled = sourceBody.disk.enabled;
				body.disk.angularRadiusRadians = sourceBody.disk.angularRadiusRadians;
				body.disk.featherRadians = sourceBody.disk.featherRadians;
				body.disk.radianceScale = sourceBody.disk.radianceScale;
				body.disk.occlusionStrength = sourceBody.disk.occlusionStrength;
				dst.celestialBodies.push_back(std::move(body));
			}
			settings.heightFog.enabled = source.heightFog.enabled;
			settings.heightFog.groundHeightWorldMeters =
				source.heightFog.groundHeightWorldMeters;
			settings.heightFog.visibilityAtGroundMeters =
				source.heightFog.visibilityAtGroundMeters;
			settings.heightFog.densityFalloffHeightMeters =
				source.heightFog.densityFalloffHeightMeters;
			settings.heightFog.startDistanceMeters = source.heightFog.startDistanceMeters;
			settings.heightFog.nearFadeDistanceMeters =
				source.heightFog.nearFadeDistanceMeters;
			settings.heightFog.maximumDistanceMeters =
				source.heightFog.maximumDistanceMeters;
			settings.heightFog.farFadeDistanceMeters =
				source.heightFog.farFadeDistanceMeters;
			settings.heightFog.singleScatteringAlbedo =
				source.heightFog.singleScatteringAlbedo;
			settings.heightFog.anisotropy = source.heightFog.anisotropy;
			settings.heightFog.emissivePerMeter = source.heightFog.emissivePerMeter;
			settings.heightFog.skyLightingScale = source.heightFog.skyLightingScale;
			settings.heightFog.mainLightVolumetricScale =
				source.heightFog.mainLightVolumetricScale;
			settings.heightFog.receiveCloudShadows =
				source.heightFog.receiveCloudShadows;
			settings.volumetricClouds = ToRuntimeCloudSettings(source.volumetricClouds);
			return settings;
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
			settings.bloomClamp = source.m_BloomClamp;
			settings.bloomTintR = source.m_BloomTintR;
			settings.bloomTintG = source.m_BloomTintG;
			settings.bloomTintB = source.m_BloomTintB;
			settings.bloomShapeMode = source.m_BloomShapeMode;
			settings.bloomShapeIntensity = source.m_BloomShapeIntensity;
			settings.bloomShapeBlend = source.m_BloomShapeBlend;
			settings.bloomShapeAngleDeg = source.m_BloomShapeAngleDeg;
			settings.bloomAnamorphicStretch = source.m_BloomAnamorphicStretch;
			settings.bloomStreakCount = source.m_BloomStreakCount;
			settings.bloomStreakLength = source.m_BloomStreakLength;
			settings.bloomStreakAttenuation = source.m_BloomStreakAttenuation;
			settings.enableDOF = source.m_EnableDOF;
			settings.focusDistance = source.m_FocusDistance;
			settings.focalLengthMm = source.m_FocalLengthMm;
			settings.fStop = source.m_FStop;
			settings.sensorHeightMm = source.m_SensorHeightMm;
			settings.maxCoC = source.m_MaxCoC;
			settings.dofBlurTransmissionBackground = source.m_DOFBlurTransmissionBackground;
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
			destination.m_BloomClamp = std::clamp(settings.bloomClamp, 0.0f, 1024.0f);
			destination.m_BloomTintR = std::clamp(settings.bloomTintR, 0.0f, 8.0f);
			destination.m_BloomTintG = std::clamp(settings.bloomTintG, 0.0f, 8.0f);
			destination.m_BloomTintB = std::clamp(settings.bloomTintB, 0.0f, 8.0f);
			destination.m_BloomShapeMode = std::clamp(settings.bloomShapeMode, 0, 2);
			destination.m_BloomShapeIntensity = std::clamp(settings.bloomShapeIntensity, 0.0f, 4.0f);
			destination.m_BloomShapeBlend = std::clamp(settings.bloomShapeBlend, 0.0f, 1.0f);
			destination.m_BloomShapeAngleDeg = std::clamp(settings.bloomShapeAngleDeg, -360.0f, 360.0f);
			destination.m_BloomAnamorphicStretch = std::clamp(settings.bloomAnamorphicStretch, 0.0f, 16.0f);
			destination.m_BloomStreakCount = std::clamp(settings.bloomStreakCount, 2, 8);
			destination.m_BloomStreakLength = std::clamp(settings.bloomStreakLength, 0.0f, 128.0f);
			destination.m_BloomStreakAttenuation = std::clamp(settings.bloomStreakAttenuation, 0.0f, 0.98f);
			destination.m_EnableDOF = settings.enableDOF;
			destination.m_FocusDistance = std::clamp(settings.focusDistance, 0.01f, 100000.0f);
			destination.m_FocalLengthMm = std::clamp(settings.focalLengthMm, 8.0f, 300.0f);
			destination.m_FStop = std::clamp(settings.fStop, 0.7f, 32.0f);
			destination.m_SensorHeightMm = std::clamp(settings.sensorHeightMm, 1.0f, 80.0f);
			destination.m_MaxCoC = std::clamp(settings.maxCoC, 0.0f, 64.0f);
			destination.m_DOFBlurTransmissionBackground = settings.dofBlurTransmissionBackground;
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

		class SetEnvironmentSettingsCommand final : public IEngineCommand
		{
		public:
			explicit SetEnvironmentSettingsCommand(
				Vans::VansSceneEnvironmentSettingsConfig settings)
				: m_Settings(std::move(settings))
			{
			}

			void Execute(EngineCommandContext& context) override
			{
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				if (!scene)
					return;
				if (!m_HasBefore)
				{
					m_Before = scene->GetEnvironmentSettings();
					m_HasBefore = true;
				}
				scene->SetEnvironmentSettings(m_Settings);
			}

			void Undo(EngineCommandContext& context) override
			{
				if (!m_HasBefore)
					return;
				auto* scene = static_cast<VansGraphics::VansScene*>(context.GetScene());
				if (scene)
					scene->SetEnvironmentSettings(m_Before);
			}

			std::string GetDescription() const override
			{
				return "Set environment settings";
			}

			bool CanMergeWith(const IEngineCommand& other) const override
			{
				return dynamic_cast<const SetEnvironmentSettingsCommand*>(&other) != nullptr;
			}

			bool MergeWith(const IEngineCommand& other, EngineCommandContext& context) override
			{
				const auto* next = dynamic_cast<const SetEnvironmentSettingsCommand*>(&other);
				if (!next)
					return false;
				m_Settings = next->m_Settings;
				Execute(context);
				return true;
			}

		private:
			Vans::VansSceneEnvironmentSettingsConfig m_Settings;
			Vans::VansSceneEnvironmentSettingsConfig m_Before;
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

	void EngineAPIImpl::BindRenderSystem(VansGraphics::VansRenderSystem* renderSystem)
	{
		m_RenderSystem = renderSystem;
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
			if (filter.requiredCapability == AssetQueryCapability::SkeletalModel &&
				!record.hasSkeletalMesh)
			{
				continue;
			}

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

	ProjectAssetCreateResult EngineAPIImpl::CreateProjectAsset(
		const ProjectAssetCreateRequest& request)
	{
		ProjectAssetCreateResult result;
		auto& projectManager = Vans::VansProjectManager::Get();
		if (!projectManager.IsProjectLoaded())
		{
			result.message = "Open a project before creating an asset";
			return result;
		}

		if (request.directoryPath.empty())
		{
			result.message = "Choose a valid project folder before creating an asset";
			return result;
		}

		std::error_code directoryError;
		std::error_code rootError;
		const std::filesystem::path directory =
			std::filesystem::weakly_canonical(request.directoryPath, directoryError);
		const std::filesystem::path projectRoot = std::filesystem::weakly_canonical(
			projectManager.GetProjectRootPath(), rootError);
		if (directoryError || rootError || !std::filesystem::is_directory(directory))
		{
			result.message = "Choose a valid project folder before creating an asset";
			return result;
		}

		const std::filesystem::path relative = directory.lexically_relative(projectRoot);
		bool insideProject = directory == projectRoot || (!relative.empty() && !relative.is_absolute());
		for (const std::filesystem::path& part : relative)
			if (part == "..")
				insideProject = false;
		if (!insideProject)
		{
			result.message = "Asset creation folder is outside the current project";
			return result;
		}

		if (request.kind == ProjectAssetCreationKind::AnimatorController ||
			request.kind == ProjectAssetCreationKind::BoneMask)
		{
			AnimationAuthoringAssetCreateRequest animationRequest;
			animationRequest.directoryPath = directory.string();
			animationRequest.kind = request.kind == ProjectAssetCreationKind::AnimatorController
				? AnimationAuthoringAssetKind::Animator
				: AnimationAuthoringAssetKind::BoneMask;
			const AnimationAuthoringAssetCreateResult animationResult =
				AnimationAuthoringBridge::CreateAsset(animationRequest);
			result.success = animationResult.success;
			result.message = animationResult.message;
			result.assetPath = animationResult.assetPath;
			return result;
		}

		auto makeUniquePath = [&](const std::string& baseName, const std::string& extension)
		{
			std::filesystem::path candidate = directory / (baseName + extension);
			if (!std::filesystem::exists(candidate))
				return candidate;
			for (int index = 1; index < 1000; ++index)
			{
				candidate = directory / (baseName + " " + std::to_string(index) + extension);
				if (!std::filesystem::exists(candidate))
					return candidate;
			}
			return std::filesystem::path{};
		};

		std::string error;
		std::filesystem::path createdPath;
		Vans::VansAssetType gafAssetType = Vans::VansAssetType::Unknown;
		const char* gafBaseName = nullptr;
		const char* gafExtension = nullptr;
		if (ResolveGAFCreation(request.kind, gafAssetType, gafBaseName, gafExtension))
		{
			createdPath = makeUniquePath(request.name.empty() ? gafBaseName : request.name, gafExtension);
			const auto* schema = Vans::VansGameplayAssetSchemaRegistry::BuiltIns().Resolve(gafAssetType);
			Vans::VansGAFProjectConfiguration configuration;
			const std::filesystem::path engineRoot = projectManager.GetPathResolver().GetEngineRoot();
			const std::filesystem::path settingsDirectory = projectRoot / "ProjectSettings";
			if (!schema || createdPath.empty() ||
				!Vans::VansGAFProjectConfiguration::EnsureProjectFiles(settingsDirectory,
					engineRoot / "EngineAssets/GAF/ProjectSettings", error) ||
				!Vans::VansGAFProjectConfiguration::LoadForProject(
					projectRoot, engineRoot, configuration, error))
			{
				result.message = error.empty() ? "GAF project configuration is unavailable" : error;
				return result;
			}
			const auto templateFound = configuration.templates.find(schema->assetKind);
			if (templateFound == configuration.templates.end())
			{
				result.message = "GAF template is missing for " + schema->assetKind;
				return result;
			}
			result.success = Vans::VansGameplayAssetStorage::SaveSourceAtomic(
				createdPath, templateFound->second, error);
			result.assetPath = result.success ? createdPath.string() : std::string{};
			result.message = result.success ? "Project GAF asset created" : error;
			return result;
		}
		switch (request.kind)
		{
		case ProjectAssetCreationKind::Timeline:
		{
			const std::filesystem::path namePath(request.name);
			if (request.name.empty() || namePath.is_absolute() || namePath.has_parent_path() ||
				namePath.filename() != namePath || request.name == "." || request.name == "..")
			{
				result.message = "Timeline name must be a non-empty file name";
				return result;
			}
			createdPath = directory / (request.name + ".vtimeline");
			if (std::filesystem::exists(createdPath))
			{
				result.message = "Timeline already exists";
				return result;
			}
			Vans::VansTimelineAsset asset;
			asset.metadata.displayName = request.name;
			result.success = Vans::VansTimelineSerialization::SaveAtomic(createdPath, asset, error);
			break;
		}
		case ProjectAssetCreationKind::SkinProfile:
		{
			createdPath = makeUniquePath("Neutral Skin", ".skinprofile");
			Vans::VansSkinProfile profile;
			profile.name = "Neutral Skin";
			profile.description = "Neutral skin material profile";
			profile.basePreset = "neutral";
			result.success = !createdPath.empty() &&
				Vans::VansSkinProfileStorage::SaveAtomic(createdPath, profile, error);
			break;
		}
		case ProjectAssetCreationKind::AudioReverbPreset:
		{
			createdPath = makeUniquePath("Room Reverb", ".vreverb");
			Vans::VansAudioReverbPresetAsset asset;
			asset.displayName = "Room Reverb";
			asset.parameters = VansEngine::GetAudioReverbPresetParameters(
				VansEngine::AudioReverbPreset::Room);
			result.success = !createdPath.empty() &&
				Vans::VansAudioReverbPresetAssetStorage::SaveAtomic(createdPath, asset, error);
			break;
		}
		case ProjectAssetCreationKind::AudioBusSnapshot:
		{
			createdPath = makeUniquePath("Gameplay Mix", ".vaudiosnapshot");
			Vans::VansAudioBusSnapshotAsset asset;
			asset.displayName = "Gameplay Mix";
			asset.snapshot.fadeSeconds = 0.25f;
			asset.snapshot.buses = {
				VansEngine::AudioBusSnapshotEntry{ "Music", 0.8f },
				VansEngine::AudioBusSnapshotEntry{ "SFX", 1.0f },
				VansEngine::AudioBusSnapshotEntry{ "Voice", 1.0f }
			};
			result.success = !createdPath.empty() &&
				Vans::VansAudioBusSnapshotAssetStorage::SaveAtomic(createdPath, asset, error);
			break;
		}
		case ProjectAssetCreationKind::AudioDuckingRules:
		{
			createdPath = makeUniquePath("Voice Ducking", ".vducking");
			Vans::VansAudioDuckingRulesAsset asset;
			asset.displayName = "Voice Ducking";
			asset.rules = { VansEngine::AudioDuckingRule{} };
			result.success = !createdPath.empty() &&
				Vans::VansAudioDuckingRulesAssetStorage::SaveAtomic(createdPath, asset, error);
			break;
		}
		default:
			result.message = "Unsupported project asset creation kind";
			return result;
		}

		result.assetPath = result.success ? createdPath.string() : std::string{};
		result.message = result.success
			? "Project asset created"
			: (error.empty() ? "Project asset creation failed" : error);
		return result;
	}

	GAFEditorDocumentSnapshot EngineAPIImpl::OpenGAFAsset(const std::string& sourcePath)
	{
		return GameplayActionAuthoringBridge::Open(sourcePath);
	}

	GAFEditorOperationResult EngineAPIImpl::SetGAFAssetField(const GAFEditorFieldEditRequest& request)
	{
		return GameplayActionAuthoringBridge::SetField(request);
	}

	GAFEditorOperationResult EngineAPIImpl::ResetGAFAssetField(
		const std::string& sourcePath,
		const std::string& fieldPath)
	{
		return GameplayActionAuthoringBridge::ResetField(sourcePath, fieldPath);
	}

	GAFEditorOperationResult EngineAPIImpl::EditGAFAssetArray(
		const GAFEditorArrayEditRequest& request)
	{
		return GameplayActionAuthoringBridge::EditArray(request);
	}

	std::vector<GAFGraphNodeTypeSnapshot> EngineAPIImpl::GetGAFGraphNodeCatalog() const
	{
		return GameplayActionAuthoringBridge::GetGraphNodeCatalog();
	}

	GAFEditorOperationResult EngineAPIImpl::EditGAFGraph(const GAFGraphEditRequest& request)
	{
		return GameplayActionAuthoringBridge::EditGraph(request);
	}

	GAFEditorOperationResult EngineAPIImpl::UndoGAFAsset(const std::string& sourcePath)
	{
		return GameplayActionAuthoringBridge::Undo(sourcePath);
	}

	GAFEditorOperationResult EngineAPIImpl::RedoGAFAsset(const std::string& sourcePath)
	{
		return GameplayActionAuthoringBridge::Redo(sourcePath);
	}

	GAFEditorOperationResult EngineAPIImpl::RevertGAFAsset(const std::string& sourcePath)
	{
		return GameplayActionAuthoringBridge::Revert(sourcePath);
	}

	GAFEditorOperationResult EngineAPIImpl::SaveGAFAsset(const std::string& sourcePath)
	{
		return GameplayActionAuthoringBridge::Save(*this, sourcePath);
	}

	GAFSemanticDiffResult EngineAPIImpl::DiffGAFAsset(
		const std::string& sourcePath,
		const std::string& baselineCanonicalJson)
	{
		return GameplayActionAuthoringBridge::Diff(sourcePath, baselineCanonicalJson);
	}

	GAFProjectConfigurationSnapshot EngineAPIImpl::GetGAFProjectConfiguration() const
	{
		return GameplayActionAuthoringBridge::GetProjectConfiguration();
	}

	std::vector<std::string> EngineAPIImpl::GetGAFTagCatalog() const
	{
		return GameplayActionAuthoringBridge::GetTagCatalog();
	}

	GAFProjectConfigurationResult EngineAPIImpl::ApplyGAFProjectConfiguration(
		const GAFProjectConfigurationSnapshot& configuration)
	{
		return GameplayActionAuthoringBridge::ApplyProjectConfiguration(configuration);
	}

	namespace
	{
		std::string GAFHandle(Vans::VansGenerationHandle handle)
		{
			if (!handle.IsValid()) return "None";
			return std::to_string(handle.index) + ":" + std::to_string(handle.generation);
		}

		const char* GAFActionState(Vans::VansActionInstanceState state)
		{
			switch (state)
			{
			case Vans::VansActionInstanceState::Created: return "Created";
			case Vans::VansActionInstanceState::Queued: return "Queued";
			case Vans::VansActionInstanceState::Resolving: return "Resolving";
			case Vans::VansActionInstanceState::BuildingContext: return "BuildingContext";
			case Vans::VansActionInstanceState::Validating: return "Validating";
			case Vans::VansActionInstanceState::Preparing: return "Preparing";
			case Vans::VansActionInstanceState::Committing: return "Committing";
			case Vans::VansActionInstanceState::Committed: return "Committed";
			case Vans::VansActionInstanceState::Running: return "Running";
			case Vans::VansActionInstanceState::Waiting: return "Waiting";
			case Vans::VansActionInstanceState::Transitioning: return "Transitioning";
			case Vans::VansActionInstanceState::Ending: return "Ending";
			case Vans::VansActionInstanceState::Ended: return "Ended";
			}
			return "Unknown";
		}

		const char* GAFTaskState(Vans::VansActionTaskState state)
		{
			switch (state)
			{
			case Vans::VansActionTaskState::Waiting: return "Waiting";
			case Vans::VansActionTaskState::Completed: return "Completed";
			case Vans::VansActionTaskState::Cancelled: return "Cancelled";
			case Vans::VansActionTaskState::Failed: return "Failed";
			case Vans::VansActionTaskState::TimedOut: return "TimedOut";
			}
			return "Unknown";
		}

		const char* GAFEndReason(Vans::VansActionEndReason reason)
		{
			switch (reason)
			{
			case Vans::VansActionEndReason::Completed: return "Completed";
			case Vans::VansActionEndReason::Failed: return "Failed";
			case Vans::VansActionEndReason::Cancelled: return "Cancelled";
			case Vans::VansActionEndReason::Interrupted: return "Interrupted";
			case Vans::VansActionEndReason::TimedOut: return "TimedOut";
			case Vans::VansActionEndReason::CommitFailed: return "CommitFailed";
			case Vans::VansActionEndReason::OwnerDestroyed: return "OwnerDestroyed";
			}
			return "Unknown";
		}

		const char* GAFActionError(Vans::VansActionError error)
		{
			switch (error)
			{
			case Vans::VansActionError::None: return "None";
			case Vans::VansActionError::InvalidHandle: return "InvalidHandle";
			case Vans::VansActionError::DefinitionMissing: return "DefinitionMissing";
			case Vans::VansActionError::DefinitionInvalid: return "DefinitionInvalid";
			case Vans::VansActionError::NotGranted: return "NotGranted";
			case Vans::VansActionError::RequirementsFailed: return "RequirementsFailed";
			case Vans::VansActionError::TargetInvalid: return "TargetInvalid";
			case Vans::VansActionError::CostUnavailable: return "CostUnavailable";
			case Vans::VansActionError::CooldownActive: return "CooldownActive";
			case Vans::VansActionError::ConcurrencyBlocked: return "ConcurrencyBlocked";
			case Vans::VansActionError::AuthorityDenied: return "AuthorityDenied";
			case Vans::VansActionError::ServiceMissing: return "ServiceMissing";
			case Vans::VansActionError::CommitFailed: return "CommitFailed";
			case Vans::VansActionError::ExecutionFailed: return "ExecutionFailed";
			case Vans::VansActionError::Cancelled: return "Cancelled";
			case Vans::VansActionError::TimedOut: return "TimedOut";
			case Vans::VansActionError::InternalInvariant: return "InternalInvariant";
			case Vans::VansActionError::InvalidState: return "InvalidState";
			case Vans::VansActionError::ConcurrencyRejected: return "ConcurrencyRejected";
			case Vans::VansActionError::ConcurrencyQueueExpired: return "ConcurrencyQueueExpired";
			case Vans::VansActionError::BudgetExceeded: return "BudgetExceeded";
			}
			return "Unknown";
		}

		bool ParseGAFActionState(std::string_view name, Vans::VansActionInstanceState& state)
		{
			static constexpr std::pair<std::string_view, Vans::VansActionInstanceState> values[]{
				{ "Created", Vans::VansActionInstanceState::Created },
				{ "Queued", Vans::VansActionInstanceState::Queued },
				{ "Resolving", Vans::VansActionInstanceState::Resolving },
				{ "BuildingContext", Vans::VansActionInstanceState::BuildingContext },
				{ "Validating", Vans::VansActionInstanceState::Validating },
				{ "Preparing", Vans::VansActionInstanceState::Preparing },
				{ "Committing", Vans::VansActionInstanceState::Committing },
				{ "Committed", Vans::VansActionInstanceState::Committed },
				{ "Running", Vans::VansActionInstanceState::Running },
				{ "Waiting", Vans::VansActionInstanceState::Waiting },
				{ "Transitioning", Vans::VansActionInstanceState::Transitioning },
				{ "Ending", Vans::VansActionInstanceState::Ending },
				{ "Ended", Vans::VansActionInstanceState::Ended }
			};
			for (const auto& [candidate, value] : values)
				if (candidate == name) { state = value; return true; }
			return false;
		}

		bool ParseGAFActionError(std::string_view name, Vans::VansActionError& error)
		{
			for (int value = static_cast<int>(Vans::VansActionError::None);
				value <= static_cast<int>(Vans::VansActionError::BudgetExceeded); ++value)
			{
				const auto candidate = static_cast<Vans::VansActionError>(value);
				if (name == GAFActionError(candidate)) { error = candidate; return true; }
			}
			return false;
		}

		GAFDebugBreakpointSnapshot BuildGAFBreakpointDTO(const Vans::VansActionBreakpoint& source)
		{
			GAFDebugBreakpointSnapshot result;
			result.id = source.id;
			result.kind = static_cast<GAFDebugBreakpointKind>(source.kind);
			result.expression = source.expression;
			result.comparison = static_cast<GAFDebugBreakpointComparison>(source.comparison);
			result.value = source.value;
			result.epsilon = source.epsilon;
			result.enabled = source.enabled;
			return result;
		}

		std::string GAFValue(const Vans::VansSerializedValue& value)
		{
			return Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(value).dump();
		}

		std::string GAFTargetValue(const Vans::VansTargetDataValue& value)
		{
			std::ostringstream stream;
			if (const auto* entity = std::get_if<Vans::VansEntityHandle>(&value))
				stream << "Entity " << entity->index << ':' << entity->generation;
			else if (const auto* location = std::get_if<Vans::VansTargetLocation>(&value))
				stream << "Location " << location->value[0] << ", " << location->value[1]
					<< ", " << location->value[2];
			else if (const auto* direction = std::get_if<Vans::VansTargetDirection>(&value))
				stream << "Direction " << direction->value[0] << ", " << direction->value[1]
					<< ", " << direction->value[2];
			else if (const auto* transform = std::get_if<Vans::VansTargetTransform>(&value))
				stream << "Transform " << transform->position[0] << ", " << transform->position[1]
					<< ", " << transform->position[2];
			else if (const auto* ray = std::get_if<Vans::VansTargetRay>(&value))
				stream << "Ray origin " << ray->origin[0] << ", " << ray->origin[1] << ", "
					<< ray->origin[2] << " direction " << ray->direction[0] << ", "
					<< ray->direction[1] << ", " << ray->direction[2] << " length " << ray->length;
			else if (const auto* hit = std::get_if<Vans::VansTargetHitResult>(&value))
				stream << "Hit Entity " << hit->entity.index << ':' << hit->entity.generation
					<< " distance " << hit->distance;
			else if (const auto* deferred = std::get_if<Vans::VansDeferredTargetQuery>(&value))
				stream << "Deferred Query Service " << deferred->service.value;
			return stream.str();
		}

		GAFRuntimeDebugSnapshot BuildGAFDebugDTO(
			const Vans::VansGameplayDebugSnapshot& source,
			const Vans::VansGameplayAssetLibrary* assets,
			bool recording,
			bool replay,
			std::size_t replayFrame,
			std::size_t replayFrameCount)
		{
			GAFRuntimeDebugSnapshot result;
			result.available = true;
			result.recording = recording;
			result.replay = replay;
			result.frame = source.frame;
			result.timeSeconds = source.timeSeconds;
			result.contentManifestHash = source.contentManifestHash;
			result.replayFrame = replayFrame;
			result.replayFrameCount = replayFrameCount;
			for (const auto& sourceHost : source.hosts)
			{
				GAFDebugHostSnapshot host;
				host.owner = GAFHandle({ sourceHost.owner.index, sourceHost.owner.generation });
				host.enabled = sourceHost.enabled;
				host.commitFrozen = sourceHost.commitFrozen;
				host.activeCueCount = sourceHost.activeCueCount;
				for (const auto& [tag, count] : sourceHost.tags)
				{
					const auto* definition = assets ? assets->Tags().Resolve(tag) : nullptr;
					host.tags.push_back({ definition ? definition->name : std::to_string(tag.value),
						std::to_string(count) });
				}
				for (const auto& attribute : sourceHost.attributes)
				{
					const auto* definition = assets ? assets->Attributes().Resolve(attribute.attribute) : nullptr;
					std::ostringstream value;
					value << attribute.currentValue << " (base " << attribute.baseValue << ")";
					host.attributes.push_back({ definition ? definition->name :
						std::to_string(attribute.attribute.value), value.str() });
				}
				for (const auto& effect : sourceHost.effects)
					host.effects.push_back({ std::to_string(effect.effect.value),
						"stacks=" + std::to_string(effect.stacks) +
						" remaining=" + std::to_string(effect.remainingSeconds) });
				for (const auto& grant : sourceHost.grants)
				{
					const auto definition = assets ? assets->Actions().ResolveLatest(grant.action) : nullptr;
					host.grants.push_back({ definition ? definition->name : std::to_string(grant.action.value),
						"level=" + std::to_string(grant.level) + " charges=" +
						std::to_string(grant.charges) + (grant.pendingRemoval ? " pending-removal" : "") });
				}
				for (const auto& sourceAction : sourceHost.actions)
				{
					GAFDebugActionSnapshot action;
					action.handle = GAFHandle(sourceAction.handle.value);
					const auto definition = assets ? assets->Actions().ResolveLatest(sourceAction.action) : nullptr;
					action.actionId = definition ? definition->name : std::to_string(sourceAction.action.value);
					action.state = GAFActionState(sourceAction.state);
					action.endReason = GAFEndReason(sourceAction.endReason);
					action.error = GAFActionError(sourceAction.error);
					action.elapsedSeconds = sourceAction.elapsedSeconds;
					action.predictionKey = std::to_string(sourceAction.prediction.connection) + ":" +
						std::to_string(sourceAction.prediction.sequence);
					action.executor = sourceAction.executor.executor;
					action.activeNodes = sourceAction.executor.activeNodes;
					action.waitingNodes = sourceAction.executor.waitingNodes;
					if (sourceAction.hasTargetData)
						for (const auto& target : sourceAction.targetData.values)
							action.targets.push_back(GAFTargetValue(target));
					for (const auto& variable : sourceAction.variables)
					{
						std::string name = std::to_string(variable.field.value);
						if (definition)
							for (const auto& field : definition->variables)
								if (field.id == variable.field) { name = field.name; break; }
						action.variables.push_back({ std::move(name), GAFValue(variable.value) });
					}
					for (const auto& sourceTask : sourceAction.tasks)
						action.tasks.push_back({ GAFHandle(sourceTask.handle.value),
							std::to_string(sourceTask.type.value), sourceTask.debugName,
							GAFTaskState(sourceTask.state), sourceTask.elapsedSeconds,
							sourceTask.timeoutSeconds });
					for (const auto& sourceResource : sourceAction.resources)
					{
						const char* prediction = sourceResource.prediction ==
							Vans::VansActionPredictionResourcePolicy::UndoRedo ? "UndoRedo" :
							sourceResource.prediction == Vans::VansActionPredictionResourcePolicy::UndoOnly
							? "UndoOnly" : "NotPredictable";
						action.resources.push_back({ GAFHandle(sourceResource.handle.value),
							sourceResource.type, sourceResource.debugName,
							GAFHandle(sourceResource.dependsOn.value), prediction, sourceResource.undone });
					}
					for (const auto& event : sourceAction.recentEvents)
						action.recentEvents.push_back(std::to_string(event.sequence) + " " + event.stableName);
					for (const auto& entry : sourceAction.trace)
						action.trace.push_back(std::to_string(entry.elapsedSeconds) + " " +
							GAFActionState(entry.state) + " " + entry.message);
					host.actions.push_back(std::move(action));
				}
				result.hosts.push_back(std::move(host));
			}
			return result;
		}
	}

	GAFRuntimeDebugSnapshot EngineAPIImpl::GetGAFRuntimeDebugSnapshot()
	{
		if (m_GAFReplaySession && m_GAFReplaySession->Current())
		{
			GAFRuntimeDebugSnapshot result = BuildGAFDebugDTO(*m_GAFReplaySession->Current(), nullptr,
				m_GAFTraceRecorder && m_GAFTraceRecorder->IsRecording(), true,
				m_GAFReplaySession->FrameIndex(), m_GAFReplaySession->FrameCount());
			return result;
		}
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* runtime = scene ? scene->GetGameplayRuntime() : nullptr;
		if (!runtime || !runtime->IsInitialized())
		{
			m_GAFPreviousDebugSnapshot.reset();
			m_GAFBreakpointHits.clear();
			GAFRuntimeDebugSnapshot result;
			result.message = "Gameplay Runtime is not available in the current scene";
			return result;
		}
		const double time = std::chrono::duration<double>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		Vans::VansGameplayDebugSnapshot snapshot = Vans::VansGameplayActionDebugService::Capture(
			*runtime, ++m_GAFDebugFrame, time, runtime->Assets().ContentManifestHash());
		const Vans::VansGameplayDebugSnapshot empty;
		m_GAFBreakpointHits = m_GAFBreakpoints.Evaluate(
			m_GAFPreviousDebugSnapshot ? *m_GAFPreviousDebugSnapshot : empty, snapshot);
		m_GAFPreviousDebugSnapshot = snapshot;
		if (!m_GAFBreakpointHits.empty() &&
			runtime->Settings().networkMode != Vans::VansGAFNetworkMode::ExternalTransport)
			SetPlayState(EnginePlayState::Pause);
		std::string recordError;
		if (m_GAFTraceRecorder && m_GAFTraceRecorder->IsRecording() &&
			!m_GAFTraceRecorder->Record(snapshot, recordError))
		{
			m_GAFTraceRecorder->End();
			m_GAFTraceRecorder.reset();
		}
		GAFRuntimeDebugSnapshot result = BuildGAFDebugDTO(snapshot, &runtime->Assets(),
			m_GAFTraceRecorder && m_GAFTraceRecorder->IsRecording(), false, 0, 0);
		for (const Vans::VansActionBreakpointHit& hit : m_GAFBreakpointHits)
			result.breakpointHits.push_back("#" + std::to_string(hit.breakpoint) + " owner " +
				GAFHandle({ hit.owner.index, hit.owner.generation }) + " action " +
				GAFHandle(hit.action.value) + ": " + hit.reason);
		if (!recordError.empty()) result.message = std::move(recordError);
		return result;
	}

	GAFCombatDebugSnapshot EngineAPIImpl::GetGAFCombatDebugSnapshot() const
	{
		GAFCombatDebugSnapshot result;
		const auto* scene = static_cast<const VansGraphics::VansScene*>(m_Scene);
		const auto* service = scene ? scene->GetCombatActionService() : nullptr;
		if (!service)
			return result;

		const Vans::VansCombatDebugSnapshot source = service->CaptureDebugSnapshot();
		result.available = source.available;
		const auto toDto = [](const glm::vec3& value)
		{
			return Vec3{ value.x, value.y, value.z };
		};
		result.windows.reserve(source.windows.size());
		for (const Vans::VansCombatDebugMeleeWindow& sourceWindow : source.windows)
		{
			GAFCombatWindowDebugSnapshot window;
			window.owner = sourceWindow.owner;
			window.window = sourceWindow.window;
			window.active = sourceWindow.active;
			window.origin = toDto(sourceWindow.origin);
			window.forward = toDto(sourceWindow.forward);
			window.previousBase = toDto(sourceWindow.previousBase);
			window.previousTip = toDto(sourceWindow.previousTip);
			window.currentBase = toDto(sourceWindow.currentBase);
			window.currentTip = toDto(sourceWindow.currentTip);
			window.range = sourceWindow.range;
			window.halfAngleDegrees = sourceWindow.halfAngleDegrees;
			window.sweepRadius = sourceWindow.sweepRadius;
			window.hitCount = sourceWindow.hitCount;
			result.windows.push_back(std::move(window));
		}
		result.hurtBodies.reserve(source.hurtBodies.size());
		for (const Vans::VansCombatDebugHurtBody& sourceBody : source.hurtBodies)
		{
			GAFHurtBodyDebugSnapshot body;
			body.target = sourceBody.target;
			body.center = toDto(sourceBody.center);
			body.radius = sourceBody.radius;
			body.halfHeight = sourceBody.halfHeight;
			body.hit = sourceBody.hit;
			result.hurtBodies.push_back(std::move(body));
		}
		return result;
	}

	GAFDebugCommandResult EngineAPIImpl::ControlGAFDebugger(const GAFDebugCommand& command)
	{
		GAFDebugCommandResult result;
		const auto finish = [&]()
		{
			for (const auto& breakpoint : m_GAFBreakpoints.All())
				result.breakpoints.push_back(BuildGAFBreakpointDTO(breakpoint));
			return result;
		};
		switch (command.kind)
		{
		case GAFDebugCommandKind::Query:
			result.success = true;
			break;
		case GAFDebugCommandKind::AddBreakpoint:
		{
			const auto& source = command.breakpoint;
			if (source.expression.empty() || !std::isfinite(source.value) ||
				!std::isfinite(source.epsilon) || source.epsilon < 0.0)
			{
				result.message = "Gameplay breakpoint expression or numeric condition is invalid";
				return finish();
			}
			Vans::VansActionBreakpoint breakpoint;
			breakpoint.kind = static_cast<Vans::VansActionBreakpointKind>(source.kind);
			breakpoint.expression = source.expression;
			breakpoint.comparison =
				static_cast<Vans::VansActionBreakpointComparison>(source.comparison);
			breakpoint.value = source.value;
			breakpoint.epsilon = source.epsilon;
			breakpoint.enabled = source.enabled;
			switch (source.kind)
			{
			case GAFDebugBreakpointKind::Action:
				breakpoint.action = Vans::VansMakeStableId<Vans::VansActionIdTag>(source.expression); break;
			case GAFDebugBreakpointKind::State:
				if (!ParseGAFActionState(source.expression, breakpoint.state))
					{ result.message = "Gameplay breakpoint state is invalid"; return finish(); }
				break;
			case GAFDebugBreakpointKind::Node: breakpoint.node = source.expression; break;
			case GAFDebugBreakpointKind::Event: breakpoint.event = source.expression; break;
			case GAFDebugBreakpointKind::Error:
				if (!ParseGAFActionError(source.expression, breakpoint.error))
					{ result.message = "Gameplay breakpoint ErrorCode is invalid"; return finish(); }
				break;
			case GAFDebugBreakpointKind::Prediction:
				if (std::sscanf(source.expression.c_str(), "%u:%u",
					&breakpoint.prediction.connection, &breakpoint.prediction.sequence) != 2 ||
					!breakpoint.prediction.IsValid())
					{ result.message = "Prediction breakpoint must use connection:sequence"; return finish(); }
				break;
			case GAFDebugBreakpointKind::Attribute:
				breakpoint.attribute =
					Vans::VansMakeStableId<Vans::VansAttributeIdTag>(source.expression); break;
			case GAFDebugBreakpointKind::Window: breakpoint.window = source.expression; break;
			}
			m_GAFBreakpoints.Add(std::move(breakpoint));
			result.success = true;
			break;
		}
		case GAFDebugCommandKind::RemoveBreakpoint:
			result.success = m_GAFBreakpoints.Remove(command.breakpointId);
			if (!result.success) result.message = "Gameplay breakpoint was not found";
			break;
		case GAFDebugCommandKind::SetBreakpointEnabled:
			result.success = m_GAFBreakpoints.SetEnabled(command.breakpointId, command.enabled);
			if (!result.success) result.message = "Gameplay breakpoint was not found";
			break;
		case GAFDebugCommandKind::ClearBreakpoints:
			m_GAFBreakpoints.Clear();
			m_GAFBreakpointHits.clear();
			result.success = true;
			break;
		case GAFDebugCommandKind::Pause:
			SetPlayState(EnginePlayState::Pause);
			result.success = true;
			break;
		case GAFDebugCommandKind::Resume:
			SetPlayState(EnginePlayState::Play);
			result.success = true;
			break;
		case GAFDebugCommandKind::Step:
		{
			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			auto* runtime = scene ? scene->GetGameplayRuntime() : nullptr;
			if (!runtime || !runtime->IsInitialized() || GetPlayState() != EnginePlayState::Pause ||
				runtime->Settings().networkMode == Vans::VansGAFNetworkMode::ExternalTransport ||
				!std::isfinite(command.stepSeconds) || command.stepSeconds < 0.0)
			{
				result.message = "GAF single-step requires a paused offline/Loopback runtime and valid delta";
				return finish();
			}
			runtime->TickEarly(command.stepSeconds);
			runtime->RunLateContinuation();
			result.success = true;
			break;
		}
		}
		return finish();
	}

	GAFTraceCommandResult EngineAPIImpl::ControlGAFTrace(const GAFTraceCommand& command)
	{
		GAFTraceCommandResult result;
		std::string error;
		switch (command.kind)
		{
		case GAFTraceCommandKind::StartRecording:
		{
			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			auto* runtime = scene ? scene->GetGameplayRuntime() : nullptr;
			if (!runtime || !runtime->IsInitialized() || command.path.empty())
			{
				result.message = "Trace recording requires a running Gameplay Runtime and output path";
				return result;
			}
			if (m_GAFTraceRecorder && m_GAFTraceRecorder->IsRecording())
			{
				result.message = "A Gameplay Trace recording is already active";
				return result;
			}
			m_GAFReplaySession.reset();
			m_GAFTraceRecorder = std::make_shared<Vans::VansGameplayTraceRecorder>();
			if (!m_GAFTraceRecorder->Begin(runtime->Assets().ContentManifestHash(),
				command.maximumFrames, command.maximumBytes, error))
			{
				m_GAFTraceRecorder.reset();
				result.message = std::move(error);
				return result;
			}
			m_GAFTracePath = command.path;
			result.success = true;
			break;
		}
		case GAFTraceCommandKind::StopAndSave:
			if (!m_GAFTraceRecorder || !m_GAFTraceRecorder->IsRecording())
			{
				result.message = "No Gameplay Trace recording is active";
				return result;
			}
			result.success = Vans::VansGameplayTraceRecorder::Save(
				command.path.empty() ? std::filesystem::path(m_GAFTracePath) :
					std::filesystem::path(command.path), m_GAFTraceRecorder->End(), error);
			m_GAFTraceRecorder.reset();
			result.message = std::move(error);
			break;
		case GAFTraceCommandKind::CancelRecording:
			if (m_GAFTraceRecorder) m_GAFTraceRecorder->End();
			m_GAFTraceRecorder.reset();
			result.success = true;
			break;
		case GAFTraceCommandKind::OpenReplay:
		{
			Vans::VansGameplayTraceArchive archive;
			if (command.path.empty() || !Vans::VansGameplayTraceRecorder::Load(
				command.path, archive, error))
			{
				result.message = command.path.empty() ? "Gameplay Trace path is empty" : std::move(error);
				return result;
			}
			auto replay = std::make_shared<Vans::VansGameplayReplaySession>();
			if (!replay->Load(std::move(archive), error))
			{
				result.message = std::move(error);
				return result;
			}
			m_GAFReplaySession = std::move(replay);
			result.success = true;
			break;
		}
		case GAFTraceCommandKind::CloseReplay:
			m_GAFReplaySession.reset();
			result.success = true;
			break;
		case GAFTraceCommandKind::SeekReplay:
			result.success = m_GAFReplaySession && m_GAFReplaySession->SeekFrame(command.frame);
			if (!result.success) result.message = "Gameplay replay frame is outside the archive";
			break;
		case GAFTraceCommandKind::StepReplay:
			result.success = m_GAFReplaySession && m_GAFReplaySession->Step(command.step);
			if (!result.success) result.message = "Gameplay replay cannot step beyond the archive";
			break;
		}
		result.snapshot = GetGAFRuntimeDebugSnapshot();
		return result;
	}

	GAFSimulationResult EngineAPIImpl::SimulateGAFAction(const GAFSimulationRequest& request)
	{
		return GameplayActionSimulationBridge::Simulate(request);
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
		if (result.success)
		{
			std::string extension = std::filesystem::path(assetPath).extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			if (extension == ".vanimator" || extension == ".vclip"
				|| extension == ".vbonemask" || extension == ".vanimrig")
			{
				std::string reloadError;
				if (!ReloadSceneAnimationDefinitions(reloadError))
				{
					result.success = false;
					result.message = reloadError;
				}
			}
		}
		return result;
	}

	bool EngineAPIImpl::ReloadSceneAnimationDefinitions(std::string& error)
	{
		error.clear();
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return true;

		struct PendingReload
		{
			VansGraphics::VansAnimationNode* node = nullptr;
			bool retargetSource = false;
			std::unique_ptr<VansGraphics::VansAnimationController> controller;
			std::string stateDiagnostic;
		};
		std::vector<PendingReload> pending;
		for (VansGraphics::VansAnimationNode* node : scene->GetAnimationNodes())
		{
			if (!node)
				continue;
			if (VansGraphics::VansAnimationController* previous = node->GetController())
			{
				const std::filesystem::path animatorPath =
					std::filesystem::path(node->GetAnimatorFilePath()).lexically_normal();
				if (!animatorPath.empty())
				{
					std::string compileError;
					const bool externalPoseTarget = node->IsRetargetEnabled();
					auto compiled = CompileProjectAnimator(animatorPath, node->GetSkeleton(), true,
						previous->IsRootMotionEnabled(), externalPoseTarget,
						previous->GetAnimationRigAssetGuid(), compileError);
					if (!compiled)
					{
						error = "Animator hot reload kept the last-good target definition for '"
							+ node->GetName() + "': " + compileError;
						return false;
					}
					PendingReload reload;
					reload.node = node;
					reload.controller = std::move(compiled);
					reload.controller->TransferRuntimeStateFrom(
						*previous, node->GetSkeleton(), reload.stateDiagnostic);
					reload.controller->Update(0.0f, node->GetSkeleton());
					pending.push_back(std::move(reload));
				}
			}

			if (node->IsRetargetEnabled() && node->GetRetargetSourceController())
			{
				const std::filesystem::path animatorPath = std::filesystem::path(
					node->GetRetargetRuntimeDesc().sourceAnimatorPath).lexically_normal();
				if (!animatorPath.empty())
				{
					VansGraphics::VansAnimationController* previous = node->GetRetargetSourceController();
					std::string compileError;
					auto compiled = CompileProjectAnimator(animatorPath,
						node->GetRetargetSourceSkeleton(), false,
						previous->IsRootMotionEnabled(), false,
						previous->GetAnimationRigAssetGuid(), compileError);
					if (!compiled)
					{
						error = "Animator hot reload kept the last-good retarget source definition for '"
							+ node->GetName() + "': " + compileError;
						return false;
					}
					PendingReload reload;
					reload.node = node;
					reload.retargetSource = true;
					reload.controller = std::move(compiled);
					reload.controller->TransferRuntimeStateFrom(
						*previous, node->GetRetargetSourceSkeleton(), reload.stateDiagnostic);
					reload.controller->Update(0.0f, node->GetRetargetSourceSkeleton());
					pending.push_back(std::move(reload));
				}
			}
		}

		for (PendingReload& reload : pending)
		{
			if (!reload.stateDiagnostic.empty())
				VANS_LOG_WARN("[AnimationHotReload] " << reload.node->GetName()
					<< ": " << reload.stateDiagnostic);
			const bool replaced = reload.retargetSource
				? reload.node->ReplaceRetargetSourceController(std::move(reload.controller))
				: scene->ReplaceAnimationRuntimeController(reload.node, std::move(reload.controller));
			if (!replaced)
			{
				error = "Animator hot reload could not replace the runtime controller for '"
					+ reload.node->GetName() + "'";
				return false;
			}
		}
		return true;
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
			// CommitRenderRuntimeConfigAtSafePoint may recreate FinalDisplayColor.
			// Invalidate editor-owned descriptors at the mutation boundary so the
			// next viewport frame always registers the replacement image view.
			ClearEditorRenderTexturePreviewCaches(device);

			auto state = std::make_shared<RenderSettingsTransactionState>();
			state->operation =
				RenderSettingsTransactionState::Operation::ApplyProjectRuntimeConfig;
			state->runtimeConfig =
				projectManager.GetProjectSettings().GetRenderRuntimeConfig();
			bool applied = false;
			if (m_RenderSystem)
			{
				applied = m_RenderSystem->ExecuteRenderThreadTransaction(
					std::make_unique<RenderSettingsTransaction>(state));
			}
			else
			{
				// RenderSystem 尚未绑定时只可能处于 backend 启动前，不存在并发 RT。
				device->GetPipelineCacheService().RefreshPersistencePath();
				const VkExtent2D outputExtent = device->GetUpscalerOutputExtent();
				device->ApplyRenderRuntimeConfig(
					state->runtimeConfig,
					outputExtent.width,
					outputExtent.height);
				device->CommitRenderRuntimeConfigAtSafePoint();
				const VkExtent2D appliedOutputExtent = device->GetUpscalerOutputExtent();
				state->outputWidth = appliedOutputExtent.width;
				state->outputHeight = appliedOutputExtent.height;
				applied = true;
			}
			if (!applied)
			{
				result.success = false;
				result.message = "Render-thread project settings application failed";
				return result;
			}
			ApplyRuntimeUIOutputExtent(state->outputWidth, state->outputHeight);
		}
		return result;
	}

	void EngineAPIImpl::CloseProject()
	{
		CloseAllUIDocuments();
		while (!GetAnimationPreviewSessions().empty())
			DestroyAnimationPreview(GetAnimationPreviewSessions().begin()->first);

		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
		{
			projectManager.CloseProject();
			if (auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device))
			{
				if (m_RenderSystem)
				{
					auto state = std::make_shared<RenderSettingsTransactionState>();
					state->operation = RenderSettingsTransactionState::Operation::RefreshPipelineCachePath;
					if (!m_RenderSystem->ExecuteRenderThreadTransaction(
						std::make_unique<RenderSettingsTransaction>(state)))
					{
						VANS_LOG_ERROR(
							"[EngineAPI] Render-thread pipeline-cache path refresh failed");
					}
				}
				else
				{
					device->GetPipelineCacheService().RefreshPersistencePath();
				}
			}
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

		VansGraphics::VansVKImage& image = device->GetFinalDisplayImage();
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

	UpscalerSettingsSnapshot EngineAPIImpl::GetUpscalerSettings() const
	{
		UpscalerSettingsSnapshot settings;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return settings;

		const VansGraphics::VansUpscalerRuntimeDiagnostics diagnostics =
			device->GetUpscalerDiagnostics();
		settings.desiredBackend = ToEditorBackend(diagnostics.desired.backend);
		settings.desiredQuality = ToEditorQuality(diagnostics.desired.quality);
		settings.effectiveBackend = ToEditorBackend(diagnostics.effective.backend);
		settings.effectiveQuality = ToEditorQuality(diagnostics.effective.quality);
		settings.fsrSharpness = diagnostics.desired.fsrSharpness;
		settings.fsrDebugView = diagnostics.desired.fsrDebugView;
		settings.fallbackReason = VansGraphics::ToString(diagnostics.fallbackReason);
		settings.fallbackMessage = diagnostics.fallbackMessage;
		settings.mipBias = diagnostics.mipBias;
		settings.renderWidth = diagnostics.renderExtent.width;
		settings.renderHeight = diagnostics.renderExtent.height;
		settings.outputWidth = diagnostics.outputExtent.width;
		settings.outputHeight = diagnostics.outputExtent.height;
		settings.contextReady = diagnostics.contextReady;
		settings.lastDispatchSucceeded = diagnostics.lastDispatchSucceeded;
		settings.lastDispatchReset = diagnostics.lastDispatchReset;
		settings.pendingResetReasons =
			static_cast<std::uint32_t>(diagnostics.pendingResetReasons);
		settings.backendCreateCode = diagnostics.backendCreateCode;
		settings.backendQueryCode = diagnostics.backendQueryCode;
		settings.backendDispatchCode = diagnostics.backendDispatchCode;
		settings.backendAuxiliaryCode = diagnostics.backendAuxiliaryCode;
		settings.successfulDispatchCount = diagnostics.successfulDispatchCount;
		settings.failedDispatchCount = diagnostics.failedDispatchCount;
		settings.auxiliaryDispatchCount = diagnostics.auxiliaryDispatchCount;
		settings.gpuMemoryUsageBytes = diagnostics.gpuMemoryUsageBytes;
		settings.gpuMemoryAliasableBytes = diagnostics.gpuMemoryAliasableBytes;
		settings.jitterPhaseCount = diagnostics.jitterPhaseCount;
		settings.lastError = diagnostics.lastError;
		return settings;
	}

	std::vector<UpscalerCapabilitiesSnapshot> EngineAPIImpl::GetUpscalerCapabilities() const
	{
		std::vector<UpscalerCapabilitiesSnapshot> capabilities;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
			return capabilities;

		capabilities.reserve(3);
		capabilities.push_back(ToEditorCapabilities(
			device->GetUpscalerCapabilities(VansGraphics::VansUpscalerBackend::Off)));
		capabilities.push_back(ToEditorCapabilities(
			device->GetUpscalerCapabilities(VansGraphics::VansUpscalerBackend::FSR)));
		capabilities.push_back(ToEditorCapabilities(
			device->GetUpscalerCapabilities(VansGraphics::VansUpscalerBackend::DLSS)));
		return capabilities;
	}

	ApplyUpscalerSettingsResult EngineAPIImpl::ApplyUpscalerSettings(
		const UpscalerSettingsSnapshot& settings)
	{
		ApplyUpscalerSettingsResult result;
		VansGraphics::VansUpscalerConfig config;
		if (!TryToRenderBackend(settings.desiredBackend, config.backend) ||
			!TryToRenderQuality(settings.desiredQuality, config.quality))
		{
			result.message = "Unknown upscaler backend or quality";
			return result;
		}
		if (!std::isfinite(settings.fsrSharpness) ||
			settings.fsrSharpness < 0.0f || settings.fsrSharpness > 1.0f)
		{
			result.message = "FSR sharpness must be in [0, 1]";
			return result;
		}
		Vans::VansProjectRenderOutputSettings outputSettings;
		outputSettings.width = settings.outputWidth;
		outputSettings.height = settings.outputHeight;
		constexpr std::uint32_t kMinimumOutputWidth = 320u;
		constexpr std::uint32_t kMinimumOutputHeight = 180u;
		constexpr std::uint32_t kMaximumOutputDimension = 16384u;
		if (!outputSettings.HasExplicitExtent() ||
			outputSettings.width < kMinimumOutputWidth ||
			outputSettings.height < kMinimumOutputHeight ||
			outputSettings.width > kMaximumOutputDimension ||
			outputSettings.height > kMaximumOutputDimension)
		{
			result.message = "Output resolution must be between 320x180 and 16384x16384";
			return result;
		}
		if (config.backend == VansGraphics::VansUpscalerBackend::Off &&
			config.quality != VansGraphics::VansUpscaleQualityMode::NativeAA)
		{
			result.message = "Off backend requires NativeAA quality";
			return result;
		}
		config.fsrSharpness = settings.fsrSharpness;
		config.fsrDebugView = settings.fsrDebugView;

		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
		{
			result.message = "Render device is unavailable";
			return result;
		}
		const VansGraphics::VansUpscalerCapabilities capabilities =
			device->GetUpscalerCapabilities(config.backend);
		if (config.backend != VansGraphics::VansUpscalerBackend::DLSS &&
			(!capabilities.compiledIn ||
			 !capabilities.runtimeAvailable ||
			 !capabilities.deviceSupported ||
			 !capabilities.Supports(config.quality)))
		{
			result.message = capabilities.unavailableReason.empty()
				? "Selected upscaler configuration is unavailable"
				: capabilities.unavailableReason;
			return result;
		}

		auto& projectManager = Vans::VansProjectManager::Get();
		Vans::VansProjectUpscalerSettings previousProjectSettings;
		Vans::VansProjectRenderOutputSettings previousOutputSettings;
		bool projectSettingsChanged = false;
		if (projectManager.IsProjectLoaded())
		{
			Vans::VansProjectSettings& projectSettings =
				projectManager.GetProjectSettings();
			previousProjectSettings = projectSettings.GetUpscalerSettings();
			previousOutputSettings = projectSettings.GetRenderOutputSettings();
			std::string validationError;
			if (!projectSettings.SetUpscalerSettings(config, &validationError))
			{
				result.message = validationError;
				return result;
			}
			if (!projectSettings.SetRenderOutputSettings(
				outputSettings,
				&validationError))
			{
				projectSettings.SetUpscalerSettings(previousProjectSettings);
				result.message = validationError;
				return result;
			}
			projectSettingsChanged = true;
			if (!projectManager.SaveProjectSettings())
			{
				projectSettings.SetUpscalerSettings(previousProjectSettings);
				projectSettings.SetRenderOutputSettings(previousOutputSettings);
				result.message = "Failed to persist render project settings";
				return result;
			}
		}

		VansGraphics::VansUpscalerSelectionChange selection;
		std::uint32_t appliedOutputWidth = 0;
		std::uint32_t appliedOutputHeight = 0;
		bool backendApplied = false;
		if (m_RenderSystem)
		{
			auto state = std::make_shared<RenderSettingsTransactionState>();
			state->operation = RenderSettingsTransactionState::Operation::ApplyUpscaler;
			state->upscalerConfig = config;
			state->outputWidth = outputSettings.width;
			state->outputHeight = outputSettings.height;
			backendApplied = m_RenderSystem->ExecuteRenderThreadTransaction(
				std::make_unique<RenderSettingsTransaction>(state));
			selection = state->upscalerSelection;
			appliedOutputWidth = state->outputWidth;
			appliedOutputHeight = state->outputHeight;
		}
		else
		{
			selection = device->RequestUpscalerConfig(
				config,
				outputSettings.width,
				outputSettings.height);
			if (selection.accepted)
			{
				device->CommitRenderRuntimeConfigAtSafePoint();
				const VkExtent2D outputExtent = device->GetUpscalerOutputExtent();
				appliedOutputWidth = outputExtent.width;
				appliedOutputHeight = outputExtent.height;
			}
			backendApplied = selection.accepted;
		}
		if (!backendApplied || !selection.accepted)
		{
			if (projectSettingsChanged)
			{
				projectManager.GetProjectSettings().SetUpscalerSettings(
					previousProjectSettings);
				projectManager.GetProjectSettings().SetRenderOutputSettings(
					previousOutputSettings);
				if (!projectManager.SaveProjectSettings())
				{
					VANS_LOG_ERROR(
						"[EngineAPI] Failed to roll back persisted upscaler settings");
				}
			}
			result.message = selection.error.empty()
				? "Render-thread upscaler settings application failed"
				: selection.error;
			return result;
		}
		ApplyRuntimeUIOutputExtent(appliedOutputWidth, appliedOutputHeight);

		result.accepted = true;
		result.runtimeFallbackExpected = selection.fallbackActive;
		result.message = result.runtimeFallbackExpected
			? "Settings saved; runtime will use a supported fallback"
			: "Upscaler settings accepted";
		return result;
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
		settings.asyncComputeRequested = device->IsAsyncComputeRequested();
		settings.asyncComputeEnabled = device->IsAsyncComputeEnabled();
		settings.hasDedicatedAsyncComputeQueue =
			device->GetQueueCapabilities().hasDedicatedAsyncComputeQueue;
		return settings;
	}

	void EngineAPIImpl::SetCommandRecordingSettings(const CommandRecordingSettingsSnapshot& settings)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		bool backendApplied = device == nullptr;
		if (device)
		{
			if (m_RenderSystem)
			{
				auto state = std::make_shared<RenderSettingsTransactionState>();
				state->operation =
					RenderSettingsTransactionState::Operation::ApplyCommandRecording;
				state->commandRecording = settings;
				backendApplied = m_RenderSystem->ExecuteRenderThreadTransaction(
					std::make_unique<RenderSettingsTransaction>(state));
			}
			else
			{
				backendApplied = device->ApplyCommandRecordingSettings(
					settings.parallelEnabled,
					settings.frameContextRingEnabled,
					settings.framesInFlight,
					settings.asyncComputeRequested);
			}
		}
		if (!backendApplied)
		{
			VANS_LOG_ERROR(
				"[EngineAPI] Render-thread command-recording settings application failed");
			return;
		}

		auto& projectManager = Vans::VansProjectManager::Get();
		if (projectManager.IsProjectLoaded())
		{
			projectManager.GetProjectSettings().SetCommandRecordingSettings(
				settings.parallelEnabled,
				settings.frameContextRingEnabled,
				settings.framesInFlight,
				settings.asyncComputeRequested);
			if (!projectManager.SaveProjectSettings())
			{
				VANS_LOG_WARN("[EngineAPI] Failed to persist command recording project settings");
			}
		}
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
			const VansGraphics::VansUpscalerRuntimeDiagnostics upscalerDiagnostics =
				device->GetUpscalerDiagnostics();
			if (upscalerDiagnostics.contextReady &&
				upscalerDiagnostics.effective.backend ==
					VansGraphics::VansUpscalerBackend::FSR)
			{
				previews.push_back(BuildImagePreview(device, 158, "FSR Reactive Mask", device->GetFSRReactiveMaskImage(), VK_IMAGE_LAYOUT_GENERAL));
				previews.push_back(BuildImagePreview(device, 159, "FSR Transparency + Composition", device->GetFSRTransparencyAndCompositionImage(), VK_IMAGE_LAYOUT_GENERAL));
			}

			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			auto* materialManager = scene ? scene->GetMaterialManager() : nullptr;
			if (materialManager)
			{
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSR_RESULT))
					previews.push_back(BuildImagePreview(device, 141, "SSR Resolve Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSGI_RESULT))
					previews.push_back(BuildImagePreview(device, 142, "SSGI Reconstruct (Raw)", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SSGI_FILTER_RESULT))
					previews.push_back(BuildImagePreview(device, 167, "SSGI Filtered (Deferred)", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT))
					previews.push_back(BuildImagePreview(device, 144, "Screen Space Shadow", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_EXPOSURE_LUMINANCE))
					previews.push_back(BuildImagePreview(device, 145, "Exposure Luminance", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_EXPOSURE_CURRENT))
					previews.push_back(BuildImagePreview(device, 146, "Exposure Current EV", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(
					VansGraphics::VansMaterialManager::RT_UPSCALER_EXPOSURE))
					previews.push_back(BuildImagePreview(device, 166, "FSR Exposure Multiplier", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_PREFILTER))
					previews.push_back(BuildImagePreview(device, 147, "Bloom Prefilter", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_MIP0))
					previews.push_back(BuildImagePreview(device, 148, "Bloom Mip 0", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_MIP1))
					previews.push_back(BuildImagePreview(device, 149, "Bloom Mip 1", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_MIP2))
					previews.push_back(BuildImagePreview(device, 150, "Bloom Mip 2", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_MIP3))
					previews.push_back(BuildImagePreview(device, 151, "Bloom Mip 3", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_UP_MIP2))
					previews.push_back(BuildImagePreview(device, 155, "Bloom Upsample Mip 2", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_UP_MIP1))
					previews.push_back(BuildImagePreview(device, 156, "Bloom Upsample Mip 1", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_UP_MIP0))
					previews.push_back(BuildImagePreview(device, 157, "Bloom Upsample Mip 0", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_BASE))
					previews.push_back(BuildImagePreview(device, 154, "Bloom Base", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_BLOOM_RESULT))
					previews.push_back(BuildImagePreview(device, 152, "Bloom Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
				if (auto* texture = materialManager->GetRuntimeRenderTexture(VansGraphics::VansMaterialManager::RT_DOF_RESULT))
					previews.push_back(BuildImagePreview(device, 153, "Depth of Field Result", texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL));
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
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (device != nullptr)
			device->RequestPunctualShadowDebugPreview();
	}

	PunctualShadowDebugSnapshot EngineAPIImpl::GetPunctualShadowDebugSnapshot() const
	{
		PunctualShadowDebugSnapshot snapshot;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!device || !scene)
			return snapshot;

		auto* materialManager = scene->GetMaterialManager();
		if (!materialManager)
			return snapshot;

		const VansGraphics::VansPunctualShadowDebugSnapshot runtime =
			device->CapturePunctualShadowDebugSnapshot();
		snapshot.available = true;
		snapshot.frameIndex = runtime.frameIndex;
		snapshot.atlasSize = runtime.atlasSize;
		snapshot.atlasCount = runtime.atlasCount;
		snapshot.basePageSize = runtime.basePageSize;
		snapshot.gutter = runtime.gutter;
		snapshot.totalPages = device->GetPunctualShadowTotalAtlasPages();
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
		if (m_RenderSystem && !m_RenderSystem->WaitForIdle())
			return;
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

		if (!m_RenderSystem)
		{
			result.error = "render system is unavailable";
			return result;
		}

		struct ShaderApplyState final
		{
			std::string programId;
			std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>> stages;
			bool applied = false;
			std::string error;
		};
		class ShaderApplyTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit ShaderApplyTransaction(std::shared_ptr<ShaderApplyState> state)
				: m_State(std::move(state)) {}

			bool Execute(VansGraphics::VansGraphicsDevice& backend) override
			{
				VANS_ASSERT_RENDER_THREAD();
				if (!m_State || !backend.WaitForIdle())
				{
					if (m_State)
						m_State->error = "render backend failed to become idle";
					return false;
				}
				m_State->applied = VansGraphics::VansShaderManager::Get()
					.ApplyCompiledShaderCandidate(
						m_State->programId,
						m_State->stages,
						m_State->error);
				return m_State->applied;
			}

		private:
			std::shared_ptr<ShaderApplyState> m_State;
		};

		auto state = std::make_shared<ShaderApplyState>();
		state->programId = package.programId;
		state->stages = std::move(stageSpirv);
		const bool transactionSucceeded = m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<ShaderApplyTransaction>(state));
		result.applied = transactionSucceeded && state->applied;
		result.error = std::move(state->error);
		if (!result.applied && result.error.empty())
			result.error = "render-thread shader apply transaction failed";
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
		diagnostics.shadowMapsSubmitted =
			frameContext.frameSubmitSucceeded && frameContext.shadowMapsRecorded;
		diagnostics.gbufferSubmitted =
			frameContext.frameSubmitSucceeded && frameContext.gbufferRecorded;
		diagnostics.vegetationSubmitted =
			frameContext.frameSubmitSucceeded && frameContext.vegetationRecorded;
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

	struct EngineAPIImpl::UIPreviewTransactionState
	{
		enum class Operation
		{
			Render,
			Destroy
		};

		Operation operation = Operation::Render;
		std::shared_ptr<VansRuntime::VansUIDocument> document;
		UIPreviewGpuResource resource;
		std::string error;
	};

	class EngineAPIImpl::UIPreviewRenderTransaction final
		: public VansGraphics::IVansRenderThreadTransaction
	{
	public:
		explicit UIPreviewRenderTransaction(
			std::shared_ptr<UIPreviewTransactionState> state)
			: m_State(std::move(state))
		{
		}

		bool Execute(VansGraphics::VansGraphicsDevice& backend) override
		{
			VANS_ASSERT_RENDER_THREAD();
			auto* device = dynamic_cast<VansGraphics::VansVKDevice*>(&backend);
			if (!device || !m_State)
				return false;

			if (m_State->operation == UIPreviewTransactionState::Operation::Destroy)
			{
				if (!backend.WaitForIdle())
				{
					m_State->error = "Failed to idle the render backend before destroying a UI preview.";
					return false;
				}
				DestroyResource(*device, m_State->resource);
				return true;
			}

			return RenderPreview(*device);
		}

	private:
		static void DestroyResource(
			VansGraphics::VansVKDevice& device,
			UIPreviewGpuResource& resource)
		{
			if (resource.texture)
			{
				Vans::Editor::VansEditorTextureBridge::RemoveTexture(resource.texture);
				resource.texture = nullptr;
			}

			VkDevice logicalDevice = device.GetLogicDevice();
			if (resource.framebuffer != VK_NULL_HANDLE)
			{
				VansGraphics::vkDestroyFramebuffer(
					logicalDevice, resource.framebuffer, nullptr);
				resource.framebuffer = VK_NULL_HANDLE;
			}
			if (resource.renderPass != VK_NULL_HANDLE)
			{
				VansGraphics::vkDestroyRenderPass(
					logicalDevice, resource.renderPass, nullptr);
				resource.renderPass = VK_NULL_HANDLE;
			}
			resource.colorImage.DestroyVulkanImage(logicalDevice);
		}

		bool Fail(
			VansGraphics::VansVKDevice& device,
			std::string message,
			bool resetImmediateCommandBuffer = false)
		{
			if (resetImmediateCommandBuffer)
				device.GetImmediateGraphicsCommandBuffer().ResetCommandBuffer(false);
			m_State->error = std::move(message);
			DestroyResource(device, m_State->resource);
			return false;
		}

		bool RenderPreview(VansGraphics::VansVKDevice& device)
		{
			if (!m_State->document)
			{
				m_State->error = "UI preview document is no longer valid.";
				return false;
			}

			UIPreviewGpuResource& resource = m_State->resource;
			VkDevice logicalDevice = device.GetLogicDevice();
			const VkFormat previewFormat = VK_FORMAT_R8G8B8A8_UNORM;
			VkExtent3D extent{ resource.width, resource.height, 1 };
			if (!resource.colorImage.CreateVulkanImage(
				logicalDevice,
				extent,
				previewFormat,
				1,
				1,
				VK_IMAGE_TYPE_2D,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
					VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_SAMPLE_COUNT_1_BIT,
				false,
				false,
				true,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
			{
				return Fail(device, "Failed to create UI preview color target.");
			}

			if (!CreateUIPreviewRenderPass(
				logicalDevice, previewFormat, resource.renderPass, m_State->error))
			{
				return Fail(device, std::move(m_State->error));
			}

			VkImageView attachment = resource.colorImage.GetImageView();
			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = resource.renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &attachment;
			framebufferInfo.width = resource.width;
			framebufferInfo.height = resource.height;
			framebufferInfo.layers = 1;
			if (VansGraphics::vkCreateFramebuffer(
				logicalDevice, &framebufferInfo, nullptr, &resource.framebuffer) != VK_SUCCESS)
			{
				return Fail(device, "Failed to create UI preview framebuffer.");
			}

			auto& commandBuffer = device.GetImmediateGraphicsCommandBuffer();
			if (!commandBuffer.ResetCommandBuffer(false) ||
				!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			{
				return Fail(device, "Failed to begin UI preview command buffer.");
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

			if (!VansRuntime::VansUISystem::Get().PrepareDocumentPreview(
				m_State->document,
				static_cast<void*>(rawCmd),
				0.0))
			{
				commandBuffer.EndCommandBufferRecord();
				return Fail(
					device,
					"Failed to prepare the UI document render tree for preview.",
					true);
			}

			VkClearValue clearValue{};
			clearValue.color = { { 0.02f, 0.02f, 0.035f, 1.0f } };
			VkRenderPassBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			beginInfo.renderPass = resource.renderPass;
			beginInfo.framebuffer = resource.framebuffer;
			beginInfo.renderArea.offset = { 0, 0 };
			beginInfo.renderArea.extent = { resource.width, resource.height };
			beginInfo.clearValueCount = 1;
			beginInfo.pClearValues = &clearValue;
			commandBuffer.BeginRenderPass(beginInfo);
			const bool rendered =
				VansRuntime::VansUISystem::Get().RenderDocumentPreviewPass(
					m_State->document,
					static_cast<void*>(resource.renderPass),
					1);
			commandBuffer.EndRenderPass();
			resource.colorImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			if (!rendered || !commandBuffer.EndCommandBufferRecord())
			{
				return Fail(device, "Failed to record UI preview command buffer.", true);
			}

			const bool submitted = VansGraphics::VansVKCommandBuffer::SubmitCommands(
				device.GetGraphicsQueue(),
				logicalDevice,
				{ rawCmd },
				{},
				{},
				commandBuffer.m_CommandBufferFinishSubmitFence);
			commandBuffer.ResetCommandBuffer(false);
			if (!submitted)
				return Fail(device, "Failed to submit UI preview command buffer.");

			resource.texture = Vans::Editor::VansEditorTextureBridge::RegisterTexture(
				resource.colorImage.GetSampler(),
				resource.colorImage.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			if (!resource.texture)
				return Fail(device, "Failed to register UI preview texture for editor.");
			return true;
		}

		std::shared_ptr<UIPreviewTransactionState> m_State;
	};

	void EngineAPIImpl::DestroyUIPreviewResource(UIPreviewGpuResource& resource)
	{
		if (resource.colorImage.GetImage() == VK_NULL_HANDLE &&
			resource.framebuffer == VK_NULL_HANDLE &&
			resource.renderPass == VK_NULL_HANDLE && !resource.texture)
		{
			return;
		}
		if (!m_RenderSystem)
		{
			VANS_LOG_ERROR("[UIEditor] Cannot destroy preview without the RenderSystem owner.");
			return;
		}

		auto state = std::make_shared<UIPreviewTransactionState>();
		state->operation = UIPreviewTransactionState::Operation::Destroy;
		state->resource = std::move(resource);
		if (!m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<UIPreviewRenderTransaction>(state)))
		{
			VANS_LOG_ERROR("[UIEditor] " << (state->error.empty()
				? "Render-thread UI preview destruction failed." : state->error));
		}

		resource = {};
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

		if (!m_RenderSystem)
		{
			result.message = "Render-system frame execution is not available.";
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
		docIt->second->SetSize(width, height);
		// IView layout/update is Main-affine.  The resulting render tree and every
		// Vulkan action below are consumed by the ordered RenderThread transaction.
		docIt->second->Update(0.0);

		auto state = std::make_shared<UIPreviewTransactionState>();
		state->operation = UIPreviewTransactionState::Operation::Render;
		state->document = docIt->second;
		state->resource.documentId = request.documentId;
		state->resource.width = width;
		state->resource.height = height;
		if (!m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<UIPreviewRenderTransaction>(state)))
		{
			result.message = state->error.empty()
				? "Render-thread UI preview transaction failed."
				: state->error;
			return result;
		}

		result.previewId = m_NextUIPreviewId++;
		result.texture = state->resource.texture;
		result.success = true;
		result.message = "Offscreen UI preview rendered.";
		m_UIPreviewResources.emplace(result.previewId, std::move(state->resource));
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
		if (!scene || !m_RenderSystem)
			return;

		class RebuildReflectionProbeTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit RebuildReflectionProbeTransaction(VansGraphics::VansScene& scene)
				: m_Scene(scene) {}

			bool Execute(VansGraphics::VansGraphicsDevice& backend) override
			{
				VANS_ASSERT_RENDER_THREAD();
				auto* device = dynamic_cast<VansGraphics::VansVKDevice*>(&backend);
				auto* probes = m_Scene.GetReflectionProbeSystem();
				if (!device || !probes || !backend.WaitForIdle())
					return false;
				probes->CreateGPUResources(
					*device, device->GetImmediateGraphicsCommandBuffer());
				probes->UpdateGlobalDescriptors(m_Scene.GetGlobalDescriptorSet());
				return true;
			}

		private:
			VansGraphics::VansScene& m_Scene;
		};

		if (!m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<RebuildReflectionProbeTransaction>(*scene)))
		{
			VANS_LOG_ERROR("[ReflectionProbe] Render-thread resource rebuild failed.");
		}
	}

	void EngineAPIImpl::BakeQueuedReflectionProbesNow()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !m_RenderSystem)
			return;

		class BakeReflectionProbeTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit BakeReflectionProbeTransaction(VansGraphics::VansScene& scene)
				: m_Scene(scene) {}

			bool Execute(VansGraphics::VansGraphicsDevice& backend) override
			{
				VANS_ASSERT_RENDER_THREAD();
				auto* device = dynamic_cast<VansGraphics::VansVKDevice*>(&backend);
				auto* probes = m_Scene.GetReflectionProbeSystem();
				if (!device || !probes || !backend.WaitForIdle())
					return false;
				probes->BakeQueuedProbesNow(
					m_Scene, *device, device->GetImmediateGraphicsCommandBuffer());
				return true;
			}

		private:
			VansGraphics::VansScene& m_Scene;
		};

		if (!m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<BakeReflectionProbeTransaction>(*scene)))
		{
			VANS_LOG_ERROR("[ReflectionProbe] Render-thread bake transaction failed.");
		}
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
		auto estimateMemoryMB = [](const VansGraphics::GIResolvedRegion& region) {
			const VansGraphics::GIProbeUpdateBatch batch = VansGraphics::BuildGIProbeUpdateBatch(region, 0u);
			const std::uint64_t atlasAndStateBytes = 512ull + 1024ull + 48ull;
			const std::uint64_t activeRayWorkingBytes = batch.activeRayCount * (8ull + 8ull + 8ull + 8ull + 4ull);
			return static_cast<float>(
				static_cast<double>(region.probeCount * atlasAndStateBytes + activeRayWorkingBytes) / (1024.0 * 1024.0));
		};

		GIInspectorSettingsSnapshot settings;
		settings.available = true;
		settings.environmentIntensity = gi.environmentIntensity;
		settings.maxIndirectRadiance = gi.maxIndirectRadiance;
		settings.maxProbeRadiance = gi.maxProbeRadiance;
		settings.irradianceHysteresis = gi.irradianceHysteresis;
		settings.distanceHysteresis = gi.distanceHysteresis;
		settings.distanceSharpness = gi.distanceSharpness;
		settings.brightnessChangeThreshold = gi.brightnessChangeThreshold;
		settings.showProbeGizmos = gi.showProbeGizmos;
		settings.showProbeVolume = gi.showProbeVolume;
		settings.debugView = static_cast<int>(gi.debugView);
		settings.debugExposure = gi.debugExposure;
		settings.probeOnlyDeferredOutput = gi.probeOnlyDeferredOutput;
		settings.probeOnlyDeferredExposure = gi.probeOnlyDeferredExposure;
		settings.gizmoStride = gi.gizmoStride;
		settings.selectedRegionIndex = gi.selectedRegionIndex;
		settings.totalProbeCount = 0;
		settings.totalRayCacheEntries = 0;
		settings.totalEstimatedMemoryMB = 0.0f;
		settings.regions.reserve(gi.regions.size());
		for (const VansGraphics::GIProbeRegionDesc& regionDesc : gi.regions)
		{
			const VansGraphics::GIResolvedRegion region = VansGraphics::ResolveGIRegion(regionDesc);
			GIRegionSettingsSnapshot regionSnapshot;
			regionSnapshot.stableId = region.stableId;
			regionSnapshot.name = region.name;
			regionSnapshot.enabled = region.enabled;
			regionSnapshot.regionCenter = ToEditorVec3(region.center);
			regionSnapshot.size = ToEditorVec3(region.volumeSize);
			regionSnapshot.volumeMin = ToEditorVec3(region.volumeMin);
			regionSnapshot.volumeMax = ToEditorVec3(region.volumeMin + region.volumeSize);
			regionSnapshot.gridDimensions = {
				static_cast<float>(region.gridDimensions.x),
				static_cast<float>(region.gridDimensions.y),
				static_cast<float>(region.gridDimensions.z) };
			regionSnapshot.probeSpacing = region.probeSpacing;
			regionSnapshot.normalBias = region.normalBias;
			regionSnapshot.maxRayDistance = region.maxRayDistance;
			regionSnapshot.volumeFadeDistance = region.volumeFadeDistance;
			regionSnapshot.priority = region.priority;
			regionSnapshot.raysPerProbe = region.raysPerProbe;
			regionSnapshot.spatialUpdateDivisor = region.spatialUpdateDivisor;
			regionSnapshot.directionUpdateSlices = region.directionUpdateSlices;
			regionSnapshot.totalProbeCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
				region.probeCount,
				std::numeric_limits<std::uint32_t>::max()));
			regionSnapshot.rayCacheEntries = VansGraphics::BuildGIProbeUpdateBatch(region, 0u).activeRayCount;
			regionSnapshot.estimatedMemoryMB = estimateMemoryMB(region);
			if (region.enabled)
			{
				settings.totalProbeCount += regionSnapshot.totalProbeCount;
				settings.totalRayCacheEntries += regionSnapshot.rayCacheEntries;
				settings.totalEstimatedMemoryMB += regionSnapshot.estimatedMemoryMB;
			}
			settings.regions.push_back(regionSnapshot);
		}
		return settings;
	}

	void EngineAPIImpl::ApplyGISettings(const GIInspectorSettingsSnapshot& settings)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
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
		auto buildRegion = [&](const GIRegionSettingsSnapshot& snapshot, const VansGraphics::GIProbeRegionDesc& fallback) {
			VansGraphics::GIProbeRegionDesc region = fallback;
			region.stableId = snapshot.stableId != 0 ? snapshot.stableId : fallback.stableId;
			region.name = snapshot.name.empty() ? fallback.name : snapshot.name;
			region.enabled = snapshot.enabled;
			region.center = glm::vec3(snapshot.regionCenter.x, snapshot.regionCenter.y, snapshot.regionCenter.z);
			region.size = glm::vec3(
				clampSpacing(snapshot.size.x, fallback.size.x),
				clampSpacing(snapshot.size.y, fallback.size.y),
				clampSpacing(snapshot.size.z, fallback.size.z));
			region.probeSpacing = clampSpacing(snapshot.probeSpacing, fallback.probeSpacing);
			region.gridDimensions = glm::uvec3(
				clampDimension(snapshot.gridDimensions.x, fallback.gridDimensions.x),
				clampDimension(snapshot.gridDimensions.y, fallback.gridDimensions.y),
				clampDimension(snapshot.gridDimensions.z, fallback.gridDimensions.z));
			region.overrideGridDimensions = true;
			region.raysPerProbe = std::clamp(snapshot.raysPerProbe, 1u, 4096u);
			region.spatialUpdateDivisor = std::max(snapshot.spatialUpdateDivisor, 1u);
			region.directionUpdateSlices = std::max(snapshot.directionUpdateSlices, 1u);
			region.maxRayDistance = std::max(snapshot.maxRayDistance, 0.001f);
			region.normalBias = std::max(snapshot.normalBias, 0.0f);
			region.volumeFadeDistance = std::max(snapshot.volumeFadeDistance, 0.0f);
			region.priority = snapshot.priority;
			return region;
		};

		if (!settings.regions.empty())
		{
			const std::vector<VansGraphics::GIProbeRegionDesc> oldRegions = gi.regions;
			gi.regions.clear();
			gi.regions.reserve(settings.regions.size());
			for (size_t index = 0; index < settings.regions.size(); ++index)
			{
				const VansGraphics::GIProbeRegionDesc fallback =
					index < oldRegions.size() ? oldRegions[index] : VansGraphics::GIProbeRegionDesc{};
				gi.regions.push_back(buildRegion(settings.regions[index], fallback));
			}
			gi.selectedRegionIndex = std::min<std::uint32_t>(
				settings.selectedRegionIndex,
				static_cast<std::uint32_t>(gi.regions.size() - 1u));
		}


		gi.environmentIntensity = std::max(settings.environmentIntensity, 0.0f);
		gi.maxIndirectRadiance = std::max(settings.maxIndirectRadiance, 0.0f);
		gi.maxProbeRadiance = std::max(settings.maxProbeRadiance, 0.0f);
		gi.irradianceHysteresis = settings.irradianceHysteresis;
		gi.distanceHysteresis = settings.distanceHysteresis;
		gi.distanceSharpness = settings.distanceSharpness;
		gi.brightnessChangeThreshold = settings.brightnessChangeThreshold;
		gi.showProbeGizmos = settings.showProbeGizmos;
		gi.showProbeVolume = settings.showProbeVolume;
		gi.debugView = static_cast<std::uint32_t>(std::max(settings.debugView, 0));
		gi.debugExposure = std::max(settings.debugExposure, 0.001f);
		gi.probeOnlyDeferredOutput = settings.probeOnlyDeferredOutput;
		gi.probeOnlyDeferredExposure = std::max(settings.probeOnlyDeferredExposure, 0.001f);
		VansGraphics::NormalizeGISettings(gi);
		const VansGraphics::GIResolvedRegion primaryRegion =
			VansGraphics::ResolveGIRegion(VansGraphics::GetPrimaryGIRegionDesc(gi));
		gi.gizmoStride = std::clamp(settings.gizmoStride, 1u,
			std::max({ 1u, primaryRegion.gridDimensions.x, primaryRegion.gridDimensions.y, primaryRegion.gridDimensions.z }));
		scene->SetGISettings(gi);
		// ProcessPendingGISettings consumes the scene dirty flags at the safe
		// pre-render point and uses the renderer's single SSGI UBO upload path.
		// Do not mirror that upload here: the old copy zeroed the immutable DDGI
		// atlas tile dimensions and silently re-enabled the Probe Cache shader's
		// textureSize/integer-division compatibility path after Inspector edits.
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

		auto& rayTracing = device->GetRayTracingContext();
		VansGraphics::VansTexture* irradianceAtlas = rayTracing.GetGIRegionIrradianceAtlas(0);
		VansGraphics::VansTexture* visibilityAtlas = rayTracing.GetGIRegionVisibilityAtlas(0);
		if (!irradianceAtlas || !visibilityAtlas)
		{
			m_GIProbeDebugSnapshot.status = "DDGI atlases are not created yet.";
			return m_GIProbeDebugSnapshot;
		}

		const VansGraphics::VansGISettings& gi = scene->GetGISettings();
		const VansGraphics::GIResolvedRegion primaryRegion =
			VansGraphics::ResolveGIRegion(VansGraphics::GetPrimaryGIRegionDesc(gi));
		const glm::uvec3 gridDimensions = glm::max(primaryRegion.gridDimensions, glm::uvec3(1u));
		const std::uint32_t maxGridDimension = std::max({ gridDimensions.x, gridDimensions.y, gridDimensions.z });
		stride = std::clamp(stride, 1u, maxGridDimension);
		exposure = std::max(exposure, 0.001f);
		m_GIProbeDebugSnapshot.available = true;
		m_GIProbeDebugSnapshot.gridDimensions = {
			static_cast<float>(gridDimensions.x),
			static_cast<float>(gridDimensions.y),
			static_cast<float>(gridDimensions.z) };
		m_GIProbeDebugSnapshot.stride = stride;
		m_GIProbeDebugSnapshot.exposure = exposure;
		m_GIProbeDebugSnapshot.status = "DDGI atlas and probe-state views are available in GI Ray Tracing Preview.";
		return m_GIProbeDebugSnapshot;

	}

	GIProbeDebugSnapshot EngineAPIImpl::GetGIProbeDebugSnapshot() const
	{
		return m_GIProbeDebugSnapshot;
	}

	MainCameraHiZCullDebugSnapshot EngineAPIImpl::GetMainCameraHiZCullDebugSnapshot() const
	{
		MainCameraHiZCullDebugSnapshot snapshot;
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (device == nullptr)
			return snapshot;

		const VansGraphics::VansMainCameraVisibilityDebugSnapshot backendSnapshot =
			device->CaptureMainCameraVisibilityDebugSnapshot();
		const VansGraphics::VansMainCameraVisibilityStats& stats = backendSnapshot.stats;
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

		const auto& culledNodes = backendSnapshot.culledNodes;
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

	std::vector<RenderTexturePreview> EngineAPIImpl::RequestGIRTPreviews(
		std::uint32_t zSlice,
		std::uint32_t rayIndex,
		float exposure,
		float positionScale)
	{
		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (device == nullptr)
			return {};

		auto& rayTracing = device->GetRayTracingContext();
		rayTracing.RequestGIRTPreviews(zSlice, rayIndex, exposure, positionScale);

		static constexpr const char* kPreviewNames[] = {
			"RT Miss Ratio",
			"RT Hit Mask",
			"RT Hit Position",
			"RT Hit Normal",
			"RT Hit Albedo",
			"RT Hit Roughness",
			"GI Ray Radiance",
			"DDGI Irradiance (Up)",
			"DDGI Distance Mean (Up)",
			"DDGI Distance StdDev (Up)",
			"DDGI Probe Confidence",
			"DDGI Probe Classification"
		};
		std::vector<RenderTexturePreview> previews;
		previews.reserve(VansGraphics::GIRTPreviewModeCount + 2u);
		for (std::uint32_t mode = 0u; mode < VansGraphics::GIRTPreviewModeCount; ++mode)
		{
			auto* texture = rayTracing.GetGIRTPreviewTexture(mode);
			if (texture == nullptr)
				continue;
			previews.push_back(BuildImagePreview(
				device,
				static_cast<RenderTextureId>(180u + mode),
				kPreviewNames[mode],
				texture->GetImage(),
				VK_IMAGE_LAYOUT_GENERAL));
		}

		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		VansGraphics::VansMaterialManager* materialManager =
			scene != nullptr ? scene->GetMaterialManager() : nullptr;
		if (materialManager != nullptr)
		{
			if (VansGraphics::VansTexture* cacheRadiance =
				materialManager->GetRuntimeRenderTexture(
					VansGraphics::VansMaterialManager::RT_SSGI_PROBE_CACHE_RADIANCE))
			{
				previews.push_back(BuildImagePreview(
					device,
					static_cast<RenderTextureId>(198u),
					"SSGI Screen Probe Cache Radiance (1/4)",
					cacheRadiance->GetImage(),
					VK_IMAGE_LAYOUT_GENERAL));
			}
			if (VansGraphics::VansTexture* cacheSurface =
				materialManager->GetRuntimeRenderTexture(
					VansGraphics::VansMaterialManager::RT_SSGI_PROBE_CACHE_SURFACE))
			{
				previews.push_back(BuildImagePreview(
					device,
					static_cast<RenderTextureId>(199u),
					"SSGI Probe Cache Surface (Normal.xyz + LinearDepth.a)",
					cacheSurface->GetImage(),
					VK_IMAGE_LAYOUT_GENERAL));
			}
		}
		return previews;
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
				filter.mipLevel,
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
		if (textureName == "background_pyramid")
		{
			auto* renderPassManager = VansGraphics::VansRenderPassManager::GetInstance();
			return renderPassManager
				? BuildLayerImagePreview(
					device, 207, "Water Background Pyramid",
					renderPassManager->GetWaterBackgroundPyramid(), 0u,
					filter.mipLevel, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				: RenderTexturePreview{};
		}
		if (textureName == "detail_normal")
		{
			VansGraphics::VansTexture* detailNormal = waterSystem->GetDetailNormalTexture();
			return detailNormal
				? BuildLayerImagePreview(
					device, 208, "Water Detail Wave Normal", detailNormal->GetImage(),
					0u, filter.mipLevel, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				: RenderTexturePreview{};
		}

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

		VansGraphics::VansWaterConfig& destinationConfig = scene->EditWaterConfig();
		const VansGraphics::VansWaterConfig previousConfig = destinationConfig;
		VansGraphics::VansWaterConfig nextConfig = previousConfig;
		ApplyWaterSettingsToConfig(settings, nextConfig);

		auto* waterSystem = scene->GetWaterSystem();

		class WaterGpuSettingsTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			WaterGpuSettingsTransaction(
				VansGraphics::VansWaterSystem& waterSystem,
				VansGraphics::VansWaterConfig& destinationConfig,
				const VansGraphics::VansWaterConfig& previousConfig,
				const VansGraphics::VansWaterConfig& config)
				: m_WaterSystem(waterSystem)
				, m_DestinationConfig(destinationConfig)
				, m_PreviousConfig(previousConfig)
				, m_Config(config)
			{
			}

			bool Execute(VansGraphics::VansGraphicsDevice&) override
			{
				VANS_ASSERT_RENDER_THREAD();
				m_DestinationConfig = m_Config;
				if (ShouldReinitializeWaterResources(m_PreviousConfig, m_Config))
				{
					m_WaterSystem.ReinitializeConfiguredResources();
					return true;
				}
				m_WaterSystem.SetWaterLevel(m_Config.m_WaterLevel);
				if (ShouldRegenerateGerstnerSpectrum(m_PreviousConfig, m_Config))
					m_WaterSystem.UpdateWaveSSBO();
				if (ShouldRegenerateWaveParticleSpectrum(m_PreviousConfig, m_Config))
					m_WaterSystem.UpdateWaveParticleSSBO();
				if (ShouldReinitializeWaterFFT(m_PreviousConfig, m_Config))
				{
					if (auto* fft = m_WaterSystem.GetFFT())
						fft->MarkReinit();
				}
				return true;
			}

		private:
			VansGraphics::VansWaterSystem& m_WaterSystem;
			VansGraphics::VansWaterConfig& m_DestinationConfig;
			VansGraphics::VansWaterConfig m_PreviousConfig;
			VansGraphics::VansWaterConfig m_Config;
		};

		if (waterSystem)
		{
			if (m_RenderSystem)
			{
				if (!m_RenderSystem->ExecuteRenderThreadTransaction(
					std::make_unique<WaterGpuSettingsTransaction>(
						*waterSystem, destinationConfig, previousConfig, nextConfig)))
				{
					VANS_LOG_ERROR("[EngineAPI] Render-thread water resource update failed.");
					return;
				}
			}
			else
			{
				destinationConfig = nextConfig;
				if (ShouldReinitializeWaterResources(previousConfig, nextConfig))
				{
					waterSystem->ReinitializeConfiguredResources();
				}
				else
				{
				waterSystem->SetWaterLevel(nextConfig.m_WaterLevel);
				if (ShouldRegenerateGerstnerSpectrum(previousConfig, nextConfig))
					waterSystem->UpdateWaveSSBO();
				if (ShouldRegenerateWaveParticleSpectrum(previousConfig, nextConfig))
					waterSystem->UpdateWaveParticleSSBO();
				if (ShouldReinitializeWaterFFT(previousConfig, nextConfig))
					if (auto* fft = waterSystem->GetFFT())
						fft->MarkReinit();
				}
			}
		}
		else
		{
			destinationConfig = nextConfig;
		}

		if (VansGraphics::VansRenderNode* waterNode = scene->GetWaterRenderNode())
		{
			glm::vec3 position = waterNode->GetTransformPosition();
			position.y = nextConfig.m_WaterLevel;
			waterNode->SetTransformData(
				position,
				waterNode->GetTransformRotation(),
				waterNode->GetTransformScale());
		}

		ScenePropertyValue waterSettings = WaterScenePropertyValue(nextConfig);
		for (auto& [fieldName, fieldValue] : waterSettings.objectFields)
		{
			ScenePropertyEdit edit{
				"/settings/water/" + fieldName,
				std::move(fieldValue)
			};
			auto pending = std::find_if(
				m_PendingScenePropertyEdits.begin(),
				m_PendingScenePropertyEdits.end(),
				[&edit](const ScenePropertyEdit& candidate)
				{
					return candidate.propertyPointer == edit.propertyPointer;
				});
			if (pending != m_PendingScenePropertyEdits.end())
				*pending = std::move(edit);
			else
				m_PendingScenePropertyEdits.push_back(std::move(edit));
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
		stats.detailNormalAssetAvailable = waterSystem->IsDetailNormalAssetAvailable();
		stats.detailNormalAnisotropyActive = waterSystem->GetDetailNormalAnisotropy();
		stats.effectiveRoughnessMode = static_cast<int>(
			scene->GetWaterConfig().m_EffectiveRoughness.m_Mode);
		if (auto* detailNormal = waterSystem->GetDetailNormalTexture())
		{
			stats.detailNormalMipCount =
				detailNormal->GetImage().GetImageCreateInfo().mipLevels;
		}
		if (auto* renderPassManager = VansGraphics::VansRenderPassManager::GetInstance())
		{
			VansGraphics::VansVKImage& background =
				renderPassManager->GetWaterBackgroundPyramid();
			stats.waterBackgroundPyramidMipCount = background.GetImageCreateInfo().mipLevels;
			const VkExtent3D extent = background.GetImageDimension();
			stats.waterBackgroundPyramidWidth = extent.width;
			stats.waterBackgroundPyramidHeight = extent.height;
			stats.shadowCascadeAvailable =
				renderPassManager->GetCascadeShadowArrayView() != VK_NULL_HANDLE &&
				renderPassManager->GetCascadeShadowCompareSampler() != VK_NULL_HANDLE;
		}
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

		if (scene->FindMeshAsset(request.meshName))
		{
			result.available = true;
			return result;
		}

		struct MeshUploadState
		{
			std::unique_ptr<VansGraphics::VansMesh> mesh =
				std::make_unique<VansGraphics::VansMesh>(false, false);
			std::string meshName;
			std::string sourcePath;
			bool uploaded = false;
		};
		class MeshUploadTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit MeshUploadTransaction(std::shared_ptr<MeshUploadState> state)
				: m_State(std::move(state)) {}

			bool Execute(VansGraphics::VansGraphicsDevice& backend) override
			{
				VANS_ASSERT_RENDER_THREAD();
				auto* vkDevice = dynamic_cast<VansGraphics::VansVKDevice*>(&backend);
				if (!m_State || !m_State->mesh || !vkDevice || !backend.WaitForIdle())
					return false;

				m_State->mesh->LoadMesh(
					vkDevice->GetLogicDevice(),
					vkDevice->GetGraphicsQueue(),
					&vkDevice->GetCommandBuffer(),
					m_State->sourcePath,
					false);
				m_State->mesh->SetName(m_State->meshName);
				m_State->uploaded = true;
				return true;
			}

		private:
			std::shared_ptr<MeshUploadState> m_State;
		};

		auto state = std::make_shared<MeshUploadState>();
		state->meshName = request.meshName;
		state->sourcePath = request.sourcePath;
		if (!m_RenderSystem ||
			!m_RenderSystem->ExecuteRenderThreadTransaction(
				std::make_unique<MeshUploadTransaction>(state)) ||
			!state->uploaded)
		{
			VANS_LOG_ERROR("[EngineAPI] Render-thread mesh upload failed for '"
				<< request.sourcePath << "'.");
			return result;
		}

		VansGraphics::VansMesh* mesh = state->mesh.release();
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
		if (m_RenderSystem && !m_RenderSystem->WaitForIdle())
		{
			result.message = "Could not synchronize runtime entity creation with RenderThread";
			return result;
		}
		const auto rollbackRuntimeEntities = [&]()
		{
			for (auto it = result.entityGuids.rbegin(); it != result.entityGuids.rend(); ++it)
				if (VansScriptObject* object = scene->FindObjectByGuid(*it))
					scene->DestroyEntity(object);
		};

		VkDevice logicalDevice = device->GetLogicDevice();
		if (!scene->LoadSceneObjects(logicalDevice, buildPlan.objects, projectRoot))
		{
			rollbackRuntimeEntities();
			result.message = "Runtime scene entity batch could not initialize its components";
			return result;
		}
		for (const std::string& entityGuid : result.entityGuids)
		{
			if (!scene->FindObjectByGuid(entityGuid))
			{
				rollbackRuntimeEntities();
				result.message = "Runtime scene entity was not created: " + entityGuid;
				return result;
			}
		}
		result.created = true;
		return result;
	}

	ModelAssetPlacementPayload EngineAPIImpl::PrepareModelAssetPlacement(const ModelAssetPlacementRequest& request)
	{
		return ModelAssetPlacementPreparationService::Prepare(request, *this, m_Scene);
	}

	RuntimeEntityDestroyResult EngineAPIImpl::DestroyRuntimeEntity(const RuntimeEntityDestroyRequest& request)
	{
		RuntimeEntityDestroyResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return result;
		if (m_RenderSystem && !m_RenderSystem->WaitForIdle())
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
		if (request.childEntityGuid == request.newParent.entityGuid)
		{
			result.message = "An entity cannot be parented to itself";
			return result;
		}
		std::optional<Vans::VansSceneParentReference> parent;
		if (!TryToSceneParentReference(request.newParent, parent, result.message))
		{
			if (result.message.empty())
				result.message = "Runtime parent reference is invalid";
			return result;
		}

		result.applied = scene->SetEntityParentReferenceByGuid(
			request.childEntityGuid,
			parent ? &*parent : nullptr,
			ToRuntimeReparentMode(request.transformPolicy));
		result.message = result.applied
			? (request.newParent.kind == RuntimeParentKind::None
				? "Runtime entity parent cleared" : "Runtime entity reparented")
			: "Runtime entity parent edit failed";
		if (result.applied)
		{
			const std::uint32_t transformId = ResolveRuntimeTransformId(scene, request.childEntityGuid);
			ReadRuntimeTransformById(
				scene,
				transformId,
				request.childEntityGuid,
				RuntimeTransformSpace::Local,
				result.localTransform);
		}
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

		// Scene preview sessions borrow Editor-scene animation nodes. End those
		// sessions before replacing the scene so a session cannot outlive its
		// target or restore state across the load boundary.
		if (scene->IsSceneReady() || scene->IsSceneSwitching())
		{
			if (!m_RenderSystem || !m_RenderSystem->WaitForIdle())
			{
				result.diagnostics.push_back({
					"render_thread_idle_failed",
					"Render thread could not reach idle before scene switch" });
				return result;
			}
			ClearEditorRenderTexturePreviewCaches(device);
		}
		for (AnimationPreviewSessionId previewId :
			CollectSceneAnimationPreviewSessions())
		{
			DestroyAnimationPreview(previewId);
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
			if (!m_RenderSystem || !m_RenderSystem->WaitForIdle())
			{
				VANS_LOG_ERROR("[RuntimeScene] Render thread failed to idle before unload.");
				return;
			}
			ClearEditorRenderTexturePreviewCaches(device);
		}
		for (AnimationPreviewSessionId previewId :
			CollectSceneAnimationPreviewSessions())
		{
			DestroyAnimationPreview(previewId);
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

		if (!scene->AreResourcesLoaded())
			return;
		if (m_RenderSystem)
		{
			if (!m_RenderSystem->WaitForIdle())
			{
				VANS_LOG_ERROR(
					"[RuntimeScene] Render thread failed to idle before project-resource unload.");
				return;
			}
		}
		else if (device && !device->WaitForDevice())
		{
			VANS_LOG_ERROR(
				"[RuntimeScene] Render device failed to idle before project-resource unload.");
			return;
		}
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

	AnimationAssetBindingSnapshot EngineAPIImpl::GetAnimationAssetBinding(
		const std::string& entityGuid) const
	{
		AnimationAssetBindingSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || entityGuid.empty())
			return snapshot;

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
					snapshot.available = true;
					snapshot.animatorAssetPath = animNode->GetAnimatorFilePath();
					snapshot.runtimeNodeName = animNode->GetName();
					return snapshot;
				}
			}
		}

		return snapshot;
	}

	MotionMatchingDebugSnapshot EngineAPIImpl::GetMotionMatchingDebugSnapshot() const
	{
		MotionMatchingDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		for (auto* animNode : scene->GetAnimationNodes())
		{
			// 停用角色不会继续更新 Motion Matching，但其 Controller 仍保留最后一帧
			// 调试缓存。调试快照必须遵循 AnimationNode 的有效启用状态，否则角色
			// 切换后会继续在旧角色最后的位置绘制冻结的轨迹。
			if (!animNode || !animNode->IsEnabled())
				continue;

			auto* controller = animNode->IsRetargetEnabled()
				? animNode->GetRetargetSourceController()
				: animNode->GetController();
			if (!controller || !controller->IsMotionMatchingConfigured())
				continue;

			const auto* motionMatching = controller->GetMotionMatchingDebugData();
			if (!motionMatching || !motionMatching->enabled)
				continue;

			MotionMatchingDebugVisual visual;
			visual.runtimeNodeName = animNode->GetName();
			visual.retargetSource = animNode->IsRetargetEnabled();
			if (const auto* renderNode = animNode->GetRenderNode())
			{
				visual.entityGuid = renderNode->m_ParentEntityGuid.empty()
					? renderNode->m_EntityGuid
					: renderNode->m_ParentEntityGuid;
			}
			visual.rootPosition = ToEditorVec3(motionMatching->trajectoryOriginWorld);
			visual.actualVelocity = ToEditorVec3(motionMatching->actualVelocityWorld);
			visual.plannedVelocity = ToEditorVec3(motionMatching->plannedVelocityWorld);
			visual.desiredVelocity = ToEditorVec3(motionMatching->desiredVelocityWorld);
			visual.activeClipVelocity = ToEditorVec3(motionMatching->activeClipVelocityWorld);
			visual.selectedCandidateVelocity = ToEditorVec3(
				motionMatching->selectedCandidateVelocityWorld);
			visual.appliedRootMotionVelocity = ToEditorVec3(
				motionMatching->appliedRootMotionVelocityWorld);
			visual.rootMotionTargetVelocity = ToEditorVec3(
				motionMatching->rootMotionTargetVelocityWorld);
			visual.rootMotionReconciledVelocity = ToEditorVec3(
				motionMatching->rootMotionReconciledVelocityWorld);
			visual.moveInputLocal = ToEditorVec3(glm::vec3(
				motionMatching->moveInputLocal.x, 0.0f, motionMatching->moveInputLocal.y));
			visual.predictedPivotPosition = ToEditorVec3(motionMatching->predictedPivotPositionWorld);
			for (const auto& sample : motionMatching->trajectoryHistory)
				visual.historyPositions.push_back(ToEditorVec3(sample.positionWorld));
			for (const auto& sample : motionMatching->trajectoryFuture)
			{
				visual.futurePositions.push_back(ToEditorVec3(sample.positionWorld));
				visual.futureVelocities.push_back(ToEditorVec3(sample.velocityWorld));
			}
			visual.activeClip = motionMatching->activeClip;
			visual.selectedClip = motionMatching->selectedClip;
			visual.playbackRate = motionMatching->playbackRate;
			visual.querySpeed = motionMatching->querySpeed * 0.01f;
			visual.queryDirectionDegrees = glm::degrees(motionMatching->queryDirection);
			visual.directionChangeDegrees = motionMatching->directionChangeDegrees;
			visual.inputDirectionChangeDegrees = motionMatching->inputDirectionChangeDegrees;
			visual.facingDeltaDegrees = motionMatching->queryFacingDeltaDegrees;
			visual.currentFacingYawDegrees = motionMatching->currentFacingYawDegrees;
			visual.desiredFacingYawDegrees = motionMatching->desiredFacingYawDegrees;
			visual.desiredFacingYawRateDegreesPerSecond =
				motionMatching->desiredFacingYawRateDegreesPerSecond;
			visual.facingTurnState = motionMatching->facingTurnState;
			visual.facingTurnGateReason = motionMatching->facingTurnGateReason;
			visual.movementReferenceYaw = motionMatching->movementReferenceYaw;
			visual.movementReferenceYawRate = motionMatching->movementReferenceYawRate;
			visual.plannedFacingYaw = motionMatching->plannedFacingYaw;
			visual.steeringTargetFacingDeltaDegrees =
				motionMatching->steeringTargetFacingDeltaDegrees;
			visual.steeringAuthoredFacingDeltaDegrees =
				motionMatching->steeringAuthoredFacingDeltaDegrees;
			visual.steeringRequestedCorrectionDegrees =
				motionMatching->steeringRequestedCorrectionDegrees;
			visual.steeringAppliedCorrectionDegrees =
				motionMatching->steeringAppliedCorrectionDegrees;
			visual.steeringAppliedYawRateDegreesPerSecond =
				motionMatching->steeringAppliedYawRateDegreesPerSecond;
			visual.rootMotionTargetYawRateDegreesPerSecond =
				motionMatching->rootMotionTargetYawRateDegreesPerSecond;
			visual.rootMotionReconciledYawRateDegreesPerSecond =
				motionMatching->rootMotionReconciledYawRateDegreesPerSecond;
			visual.authoredRootYawDeltaDegrees = motionMatching->authoredRootYawDeltaDegrees;
			visual.appliedRootYawDeltaDegrees = motionMatching->appliedRootYawDeltaDegrees;
			visual.steeringActive = motionMatching->steeringActive;
			visual.steeringLimited = motionMatching->steeringLimited;
			visual.turnWarpActive = motionMatching->turnWarpActive;
			visual.turnWarpLimited = motionMatching->turnWarpLimited;
			visual.turnWarpNeedsReplan = motionMatching->turnWarpNeedsReplan;
			visual.turnWarpDisableReason = motionMatching->turnWarpDisableReason;
			visual.turnWarpReplanReason = motionMatching->turnWarpReplanReason;
			visual.turnWarpTargetDeltaDegrees =
				motionMatching->turnWarpTargetDeltaDegrees;
			visual.turnWarpAuthoredRemainingYawDegrees =
				motionMatching->turnWarpAuthoredRemainingYawDegrees;
			visual.turnWarpScaleRatio = motionMatching->turnWarpScaleRatio;
			visual.turnWarpResidualDegrees = motionMatching->turnWarpResidualDegrees;
			visual.turnWarpAppliedFrameCorrectionDegrees =
				motionMatching->turnWarpAppliedFrameCorrectionDegrees;
			visual.turnWarpAccumulatedAdditiveDegrees =
				motionMatching->turnWarpAccumulatedAdditiveDegrees;
			visual.turnWarpEndpointCost = motionMatching->turnWarpEndpointCost;
			visual.turnWarpMotionEndTimeSeconds =
				motionMatching->turnWarpMotionEndTimeSeconds;
			visual.turnWarpProfileIndex = motionMatching->turnWarpProfileIndex;
			visual.rootMotionReconciliationActive =
				motionMatching->rootMotionReconciliationActive;
			visual.currentCost = motionMatching->currentCost;
			visual.trajectoryCost = motionMatching->trajectoryCost;
			visual.poseCost = motionMatching->poseCost;
			visual.contactCost = motionMatching->contactCost;
			visual.pivotRequested = motionMatching->pivotRequested;
			visual.pivotDatabaseAvailable = motionMatching->pivotDatabaseAvailable;
			visual.hasPredictedPivot = motionMatching->hasPredictedPivot;
			visual.predictedPivotTime = motionMatching->predictedPivotTime;
			visual.motionConsumptionRatio = motionMatching->motionConsumptionRatio;
			visual.movementBlocked = motionMatching->movementBlocked;
			visual.urgentDirectionChange = motionMatching->urgentDirectionChange;
			visual.requestedMoveState = motionMatching->requestedMoveState;
			visual.effectiveMoveState = motionMatching->effectiveMoveState;
			visual.directionalStateFallback = motionMatching->directionalStateFallback;
			visual.facingTurnRequested = motionMatching->facingTurnRequested;
			visual.switches = motionMatching->switches;
			visual.activeDatabases = motionMatching->activeDatabases;
			for (const auto& candidate : motionMatching->topCandidates)
			{
				MotionMatchingDebugVisual::Candidate candidateDto;
				candidateDto.clipName = candidate.clipName;
				candidateDto.time = candidate.time;
				candidateDto.totalCost = candidate.totalCost;
				candidateDto.trajectoryCost = candidate.trajectoryCost;
				candidateDto.poseCost = candidate.poseCost;
				candidateDto.contactCost = candidate.contactCost;
				candidateDto.biasCost = candidate.biasCost;
				candidateDto.turnEndpointCost = candidate.turnEndpointCost;
				visual.topCandidates.push_back(std::move(candidateDto));
			}
			snapshot.visuals.push_back(visual);
		}

		snapshot.available = !snapshot.visuals.empty();
		return snapshot;
	}

	SceneSkeletonHierarchySnapshot EngineAPIImpl::GetSceneSkeletonHierarchy(
		const std::string& entityGuidFilter) const
	{
		SceneSkeletonHierarchySnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;
		const Vans::VansRuntimeWorld* world = scene->GetRuntimeWorld();
		if (!world)
			return snapshot;
		const auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
			world->FindStorage(Vans::VansRuntimeComponentType_Animation));
		if (!storage)
			return snapshot;

		const auto& animations = storage->DenseData();
		const auto& headers = storage->Headers();
		for (std::size_t index = 0; index < animations.size() && index < headers.size(); ++index)
		{
			VansGraphics::VansAnimationNode* animationNode = animations[index].animationNode;
			const Vans::VansEntityRecord* owner = world->Entities().Get(headers[index].owner);
			if (!animationNode || !owner || (!entityGuidFilter.empty()
				&& owner->stableGuid != entityGuidFilter))
			{
				continue;
			}
			const VansGraphics::Skeleton& skeleton = animationNode->GetSkeleton();
			if (skeleton.bones.empty())
				continue;

			SceneSkeletonHierarchyRig rig;
			rig.entityGuid = owner->stableGuid;
			rig.animationComponentGuid = headers[index].stableGuid;
			rig.nodeName = animationNode->GetName();
			rig.skeletonGuid = skeleton.sourceSkeletonGuid;
			rig.skeletonSignature = skeleton.signature;
			rig.bones.reserve(skeleton.bones.size());
			for (const VansGraphics::BoneInfo& sourceBone : skeleton.bones)
			{
				SceneSkeletonHierarchyBone bone;
				bone.guid = sourceBone.guid;
				bone.name = sourceBone.name;
				bone.canonicalPath = sourceBone.canonicalPath;
				bone.parentIndex = sourceBone.parentIndex;
				rig.bones.push_back(std::move(bone));
			}
			if (const VansGraphics::VansAnimationController* controller =
				animationNode->GetController())
			{
				if (const VansGraphics::VansCompiledAnimationRig* animationRig =
					controller->GetAnimationRig())
				{
					rig.sockets.reserve(animationRig->sockets.size());
					for (const VansGraphics::VansCompiledRigSocket& sourceSocket :
						animationRig->sockets)
					{
						if (sourceSocket.boneIndex < 0 || sourceSocket.boneIndex >=
							static_cast<int>(skeleton.bones.size()))
						{
							continue;
						}
						SceneSkeletonHierarchySocket socket;
						socket.guid = sourceSocket.guid;
						socket.name = sourceSocket.name;
						socket.parentBoneIndex = sourceSocket.boneIndex;
						socket.boneGuid = skeleton.bones[
							static_cast<std::size_t>(sourceSocket.boneIndex)].guid;
						rig.sockets.push_back(std::move(socket));
					}
				}
			}
			snapshot.rigs.push_back(std::move(rig));
		}
		snapshot.available = !snapshot.rigs.empty();
		return snapshot;
	}

	SceneSkeletonNodePoseSnapshot EngineAPIImpl::GetSceneSkeletonNodePose(
		const SceneSkeletonNodePoseRequest& request) const
	{
		SceneSkeletonNodePoseSnapshot snapshot;
		snapshot.kind = request.kind;
		snapshot.entityGuid = request.entityGuid;
		snapshot.animationComponentGuid = request.animationComponentGuid;
		snapshot.anchorGuid = request.anchorGuid;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;
		const Vans::VansRuntimeWorld* world = scene->GetRuntimeWorld();
		if (!world)
			return snapshot;
		const auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
			world->FindStorage(Vans::VansRuntimeComponentType_Animation));
		if (!storage)
			return snapshot;

		VansGraphics::VansAnimationNode* animationNode = nullptr;
		const auto& animations = storage->DenseData();
		const auto& headers = storage->Headers();
		for (std::size_t index = 0; index < animations.size() && index < headers.size(); ++index)
		{
			const Vans::VansEntityRecord* owner = world->Entities().Get(headers[index].owner);
			if (owner && owner->stableGuid == request.entityGuid
				&& headers[index].stableGuid == request.animationComponentGuid)
			{
				animationNode = animations[index].animationNode;
				break;
			}
		}
		if (!animationNode)
			return snapshot;

		const VansGraphics::Skeleton& skeleton = animationNode->GetSkeleton();
		const VansGraphics::VansAnimationController* controller = animationNode->GetController();
		if (skeleton.bones.empty())
			return snapshot;
		snapshot.skeletonGuid = skeleton.sourceSkeletonGuid;
		snapshot.skeletonSignature = skeleton.signature;

		std::vector<glm::mat4> fallbackGlobals;
		const std::vector<glm::mat4>* globals = nullptr;
		if (controller && controller->GetCachedGlobalTransforms().size() == skeleton.bones.size())
			globals = &controller->GetCachedGlobalTransforms();
		else
		{
			fallbackGlobals.assign(skeleton.bones.size(), glm::mat4(1.0f));
			auto accumulate = [&](int boneIndex)
			{
				if (boneIndex < 0 || boneIndex >= static_cast<int>(skeleton.bones.size()))
					return;
				const auto& bone = skeleton.bones[static_cast<std::size_t>(boneIndex)];
				fallbackGlobals[static_cast<std::size_t>(boneIndex)] = bone.parentIndex >= 0
					? fallbackGlobals[static_cast<std::size_t>(bone.parentIndex)] * bone.localTransform
					: bone.localTransform;
			};
			if (!skeleton.topologicalOrder.empty())
				for (const int boneIndex : skeleton.topologicalOrder) accumulate(boneIndex);
			else
				for (int boneIndex = 0; boneIndex < static_cast<int>(skeleton.bones.size()); ++boneIndex)
					accumulate(boneIndex);
			globals = &fallbackGlobals;
		}

		glm::mat4 ownerWorld(1.0f);
		const std::uint32_t transformId = animationNode->GetTransformID();
		if (VansGraphics::VansTransformStore::IsAllocated(transformId))
			ownerWorld = VansGraphics::VansTransformStore::GetTransform(transformId).GetModelMatrix();
		const VansGraphics::VansSkeletonPoseView pose = controller
			? controller->GetFinalPoseView(skeleton) : VansGraphics::VansSkeletonPoseView{};
		if (pose.IsValid())
			snapshot.poseRevision = pose.revision;
		auto transformSnapshot = [](const glm::mat4& matrix, RuntimeTransformSpace space)
		{
			RuntimeTransformSnapshot result;
			Vans::VansLocalTransform transform;
			if (!Vans::VansLocalTransform::TryFromMatrix(matrix, transform))
				return result;
			result.available = true;
			result.space = space;
			result.position = ToEditorVec3(transform.position);
			result.rotationDegrees = ToEditorVec3(
				glm::degrees(glm::eulerAngles(transform.rotation)));
			result.scale = ToEditorVec3(transform.scale);
			return result;
		};

		if (request.kind == SceneSkeletonNodeKind::Bone)
		{
			const auto found = skeleton.boneGuidToIndex.find(request.anchorGuid);
			if (found == skeleton.boneGuidToIndex.end())
				return snapshot;
			const std::size_t boneIndex = static_cast<std::size_t>(found->second);
			const VansGraphics::BoneInfo& bone = skeleton.bones[boneIndex];
			const glm::mat4 local = pose.IsValid()
				? (*pose.localTransforms)[boneIndex] : bone.localTransform;
			const glm::mat4 model = (*globals)[boneIndex];
			snapshot.name = bone.name;
			snapshot.canonicalPath = bone.canonicalPath;
			snapshot.boneGuid = bone.guid;
			snapshot.localTransform = transformSnapshot(local, RuntimeTransformSpace::Local);
			snapshot.modelTransform = transformSnapshot(model, RuntimeTransformSpace::Model);
			snapshot.worldTransform = transformSnapshot(ownerWorld * model, RuntimeTransformSpace::World);
			snapshot.available = true;
		return snapshot;
		}

		if (!controller)
			return snapshot;
		const VansGraphics::VansCompiledAnimationRig* animationRig = controller->GetAnimationRig();
		if (!animationRig)
			return snapshot;
		for (const VansGraphics::VansCompiledRigSocket& socket : animationRig->sockets)
		{
			if (socket.guid != request.anchorGuid || socket.boneIndex < 0
				|| socket.boneIndex >= static_cast<int>(skeleton.bones.size()))
			{
				continue;
			}
			const std::size_t boneIndex = static_cast<std::size_t>(socket.boneIndex);
			const glm::mat4 model = (*globals)[boneIndex] * socket.localTransform;
			snapshot.name = socket.name;
			snapshot.boneGuid = skeleton.bones[boneIndex].guid;
			snapshot.localTransform = transformSnapshot(
				socket.localTransform, RuntimeTransformSpace::Local);
			snapshot.modelTransform = transformSnapshot(model, RuntimeTransformSpace::Model);
			snapshot.worldTransform = transformSnapshot(ownerWorld * model, RuntimeTransformSpace::World);
			snapshot.available = true;
			return snapshot;
		}
		return snapshot;
	}

	SkeletonDebugSnapshot EngineAPIImpl::GetSkeletonDebugSnapshot(const std::string& entityGuidFilter) const
	{
		SkeletonDebugSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene)
			return snapshot;

		struct RuntimeAnimationIdentity
		{
			std::string entityGuid;
			std::string componentGuid;
		};
		std::unordered_map<VansGraphics::VansAnimationNode*, RuntimeAnimationIdentity> runtimeIdentities;
		if (const Vans::VansRuntimeWorld* world = scene->GetRuntimeWorld())
		{
			const auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
				world->FindStorage(Vans::VansRuntimeComponentType_Animation));
			if (storage)
			{
				const auto& animations = storage->DenseData();
				const auto& headers = storage->Headers();
				for (std::size_t index = 0; index < animations.size() && index < headers.size(); ++index)
				{
					const Vans::VansEntityRecord* owner = world->Entities().Get(headers[index].owner);
					if (animations[index].animationNode && owner)
						runtimeIdentities[animations[index].animationNode] = {
							owner->stableGuid, headers[index].stableGuid };
				}
			}
		}

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

		auto transformSnapshot = [](const glm::mat4& matrix, RuntimeTransformSpace space)
		{
			RuntimeTransformSnapshot result;
			Vans::VansLocalTransform transform;
			if (!Vans::VansLocalTransform::TryFromMatrix(matrix, transform))
				return result;
			result.available = true;
			result.space = space;
			result.position = ToEditorVec3(transform.position);
			result.rotationDegrees = ToEditorVec3(
				glm::degrees(glm::eulerAngles(transform.rotation)));
			result.scale = ToEditorVec3(transform.scale);
			return result;
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
			rig.skeletonGuid = skeleton.sourceSkeletonGuid;
			rig.skeletonSignature = skeleton.signature;
			if (!retargetSource)
			{
				const auto identity = runtimeIdentities.find(animNode);
				if (identity != runtimeIdentities.end())
				{
					rig.entityGuid = identity->second.entityGuid;
					rig.animationComponentGuid = identity->second.componentGuid;
				}
			}
			const VansGraphics::VansSkeletonPoseView pose = controller
				? controller->GetFinalPoseView(skeleton) : VansGraphics::VansSkeletonPoseView{};
			if (pose.IsValid())
				rig.poseRevision = pose.revision;
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
			if (rig.entityGuid.empty())
			{
				if (auto* renderNode = animNode->GetRenderNode())
					rig.entityGuid = renderNode->m_ParentEntityGuid.empty()
						? renderNode->m_EntityGuid : renderNode->m_ParentEntityGuid;
			}
			rig.bones.reserve(skeleton.bones.size());

			for (size_t i = 0; i < skeleton.bones.size(); ++i)
			{
				const glm::mat4 boneWorld = ownerWorld * (*globals)[i];
				SkeletonDebugBoneSnapshot bone;
				bone.guid = skeleton.bones[i].guid;
				bone.name = skeleton.bones[i].name;
				bone.canonicalPath = skeleton.bones[i].canonicalPath;
				bone.parentIndex = skeleton.bones[i].parentIndex;
				const glm::mat4 localTransform = pose.IsValid()
					? (*pose.localTransforms)[i] : skeleton.bones[i].localTransform;
				bone.localTransform = transformSnapshot(localTransform, RuntimeTransformSpace::Local);
				bone.worldTransform = transformSnapshot(boneWorld, RuntimeTransformSpace::World);
				bone.worldPosition = ToEditorVec3(glm::vec3(boneWorld[3]));
				rig.bones.push_back(std::move(bone));
			}

			if (!retargetSource && controller)
			{
				if (const VansGraphics::VansCompiledAnimationRig* animationRig = controller->GetAnimationRig())
				{
					rig.sockets.reserve(animationRig->sockets.size());
					for (const VansGraphics::VansCompiledRigSocket& compiledSocket : animationRig->sockets)
					{
						if (compiledSocket.boneIndex < 0
							|| compiledSocket.boneIndex >= static_cast<int>(globals->size()))
							continue;
						SkeletonDebugSocketSnapshot socket;
						socket.guid = compiledSocket.guid;
						socket.name = compiledSocket.name;
						socket.parentBoneIndex = compiledSocket.boneIndex;
						socket.boneGuid = skeleton.bones[
							static_cast<std::size_t>(compiledSocket.boneIndex)].guid;
						socket.localTransform = transformSnapshot(
							compiledSocket.localTransform, RuntimeTransformSpace::Local);
						const glm::mat4 socketWorld = ownerWorld
							* (*globals)[static_cast<std::size_t>(compiledSocket.boneIndex)]
							* compiledSocket.localTransform;
						socket.worldTransform = transformSnapshot(
							socketWorld, RuntimeTransformSpace::World);
						rig.sockets.push_back(std::move(socket));
					}
				}
			}

			snapshot.rigs.push_back(std::move(rig));
		};

		for (auto* animNode : scene->GetAnimationNodes())
		{
			if (!animNode || !matchesFilter(animNode))
				continue;

			const VansGraphics::Skeleton& targetSkeleton = animNode->GetSkeleton();
			if (targetSkeleton.bones.empty())
				continue;

			glm::mat4 ownerWorld(1.0f);
			const uint32_t transformId = animNode->GetTransformID();
			if (transformId < VansGraphics::VansTransformStore::GlobalTransforms.size())
				ownerWorld = VansGraphics::VansTransformStore::GetTransform(transformId).GetModelMatrix();

			appendRig(animNode, targetSkeleton, animNode->GetController(), ownerWorld, "Target", false);
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

	AssetSkeletonSnapshot EngineAPIImpl::GetAssetSkeletonSnapshot(const std::string& assetGuid) const
	{
		AssetSkeletonSnapshot snapshot;
		snapshot.assetGuid = assetGuid;
		VansGraphics::Skeleton skeleton;
		if (!LoadAnimationPreviewSkeleton(
			assetGuid, skeleton, snapshot.sourcePath, snapshot.error))
			return snapshot;

		std::vector<glm::mat4> bindGlobals(skeleton.bones.size(), glm::mat4(1.0f));
		auto accumulate = [&](int index)
		{
			if (index < 0 || index >= static_cast<int>(skeleton.bones.size()))
				return;
			const auto& bone = skeleton.bones[index];
			if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(skeleton.bones.size()))
				bindGlobals[index] = bindGlobals[bone.parentIndex] * bone.localTransform;
			else
				bindGlobals[index] = bone.localTransform;
		};
		if (!skeleton.topologicalOrder.empty())
			for (int index : skeleton.topologicalOrder) accumulate(index);
		else
			for (int index = 0; index < static_cast<int>(skeleton.bones.size()); ++index) accumulate(index);

		snapshot.bones.reserve(skeleton.bones.size());
		for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
		{
			AssetSkeletonBoneSnapshot bone;
			bone.name = skeleton.bones[index].name;
			bone.parentIndex = skeleton.bones[index].parentIndex;
			bone.bindPosition = ToEditorVec3(glm::vec3(bindGlobals[index][3]));
			snapshot.bones.push_back(std::move(bone));
		}
		snapshot.available = true;
		return snapshot;
	}

	AnimatorDocumentDecodeResult EngineAPIImpl::DecodeAnimatorDocument(
		const std::string& canonicalJson) const
	{
		return AnimationAuthoringBridge::DecodeAnimator(canonicalJson);
	}

	AnimatorDocumentEncodeResult EngineAPIImpl::EncodeAnimatorDocument(
		const AnimatorDocumentDTO& document) const
	{
		return AnimationAuthoringBridge::EncodeAnimator(document);
	}

	BoneMaskDocumentDecodeResult EngineAPIImpl::DecodeBoneMaskDocument(
		const std::string& canonicalJson) const
	{
		return AnimationAuthoringBridge::DecodeBoneMask(canonicalJson);
	}

	BoneMaskDocumentEncodeResult EngineAPIImpl::EncodeBoneMaskDocument(
		const BoneMaskDocumentDTO& document) const
	{
		return AnimationAuthoringBridge::EncodeBoneMask(document);
	}

	AnimationRigDocumentDecodeResult EngineAPIImpl::DecodeAnimationRigDocument(
		const std::string& canonicalJson) const
	{
		return AnimationAuthoringBridge::DecodeAnimationRig(canonicalJson);
	}

	AnimationRigDocumentEncodeResult EngineAPIImpl::EncodeAnimationRigDocument(
		const AnimationRigDocumentDTO& document) const
	{
		return AnimationAuthoringBridge::EncodeAnimationRig(document);
	}

	BoneMaskCompileResult EngineAPIImpl::CompileBoneMaskDocument(
		const BoneMaskDocumentDTO& document, const AssetSkeletonSnapshot& skeleton) const
	{
		return AnimationAuthoringBridge::CompileBoneMask(document, skeleton);
	}

	AnimationPreviewCreateResult EngineAPIImpl::CreateAnimationPreview(
		const AnimationPreviewCreateRequest& request)
	{
		AnimationPreviewCreateResult result;
		auto session = std::make_unique<AnimationPreviewSessionState>();
		session->id = NextAnimationPreviewSessionId();
		session->targetKind = request.targetKind;
		if (request.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			if (!scene || !scene->IsSceneReady())
			{
				result.message = "Scene animation preview requires a loaded scene";
				return result;
			}
			if (scene->GetLoadMode() != VansGraphics::VansSceneLoadMode::Editor)
			{
				result.message = "Scene animation preview is only available outside Play mode";
				return result;
			}
			for (const auto& [id, existing] : GetAnimationPreviewSessions())
			{
				if (existing && existing->targetKind ==
						AnimationPreviewTargetKind::SceneAnimationComponent &&
					existing->entityGuid == request.entityGuid &&
					existing->animationComponentGuid == request.animationComponentGuid)
				{
					result.message =
						"This Animation Component already has an active preview session";
					return result;
				}
			}
			VansGraphics::VansAnimationNode* node = ResolveSceneAnimationPreviewNode(
				scene, request.entityGuid, request.animationComponentGuid);
			if (!node || !node->GetCharacterMotionController())
			{
				result.message = "Scene Animation Component could not be resolved";
				return result;
			}
			if (request.animatorAssetGuid.empty())
			{
				result.message = "Scene animation preview requires an Animator asset";
				return result;
			}
			std::filesystem::path animatorPath;
			if (!ResolveProjectAnimationAsset(
				request.animatorAssetGuid,
				Vans::VansAssetType::AnimatorController,
				{}, animatorPath, result.message))
			{
				return result;
			}
			// Retarget is a property of the selected Scene Animation Component. A
			// retargeted target compiles the chosen Animator against its Source
			// Skeleton; a direct target compiles against its own Skeleton.
			const bool useRetarget = node->IsRetargetEnabled();
			const VansGraphics::Skeleton& compileSkeleton = useRetarget
				? node->GetRetargetSourceSkeleton() : node->GetSkeleton();
			auto previewController = CompileProjectAnimator(
				animatorPath, compileSkeleton, true, true, false,
				std::string{}, result.message);
			if (!previewController)
			{
				result.message = (useRetarget
					? "Selected Animator is incompatible with the Scene target's Retarget Source Skeleton: "
					: "Selected Animator is incompatible with the Scene target Skeleton: ")
					+ result.message;
				return result;
			}
			if (!useRetarget)
			{
				// Direct preview still authors the selected Scene component's Rig,
				// not whichever compatible Rig happens to be referenced by the
				// selected Animator asset.
				VansGraphics::VansAnimationController* targetController =
					node->GetController();
				const VansGraphics::VansCompiledAnimationRig* targetRig =
					targetController ? targetController->GetAnimationRig() : nullptr;
				if (!targetController || !targetRig
					|| targetController->GetAnimationRigAssetGuid().empty()
					|| targetController->GetAnimationRigAssetPath().empty())
				{
					result.message =
						"Scene Animation Component has no authorable target Animation Rig";
					return result;
				}
				std::string rigError;
				if (!previewController->ReplaceAnimationRig(*targetRig, rigError))
				{
					result.message =
						"Selected Animator cannot use the Scene target Animation Rig: "
						+ rigError;
					return result;
				}
				previewController->SetAnimationRigAssetIdentity(
					targetController->GetAnimationRigAssetGuid(),
					targetController->GetAnimationRigAssetPath());
			}
			if (!scene->BeginEditorAnimationPreview(node))
			{
				result.message =
					"Scene Animation Component is already owned by another Editor preview session";
				return result;
			}
			session->entityGuid = request.entityGuid;
			session->animationComponentGuid = request.animationComponentGuid;
			session->animatorAssetGuid = request.animatorAssetGuid;
			session->animatorAssetPath = animatorPath.string();
			session->sceneContentRevision = m_SceneContentRevision;
			session->originalPlaybackState = node->GetState();
			session->originalSpeed = node->GetSpeed();
			session->originalSceneStateCaptured = true;
			bool controllerExchanged = false;
			if (useRetarget)
			{
				controllerExchanged = node->ExchangeRetargetSourceController(
					std::move(previewController), session->originalSceneController);
				session->replacedRetargetSourceController = controllerExchanged;
			}
			else
			{
				controllerExchanged = scene->ExchangeAnimationRuntimeController(
					node, std::move(previewController), session->originalSceneController);
			}
			if (!controllerExchanged || !session->originalSceneController)
			{
				scene->EndEditorAnimationPreview(node);
				result.message = "Scene Animation Component rejected the preview Animator";
				return result;
			}
			AnimationPreviewAttachmentAuthoringService::BeginSession(session->id);
			std::string rigError;
			VansGraphics::VansAnimationController* targetController =
				node->GetController();
			if (!targetController || !AnimationPreviewRigAuthoringService::BeginSession(
				session->id, *targetController, rigError))
			{
				if (rigError.empty())
					rigError = "target controller is unavailable";
				std::string ignoredAttachmentError;
				AnimationPreviewAttachmentAuthoringService::EndSession(
					session->id, nullptr, ignoredAttachmentError);
				std::unique_ptr<VansGraphics::VansAnimationController> rejectedPreview;
				const bool restored = session->replacedRetargetSourceController
					? node->ExchangeRetargetSourceController(
						std::move(session->originalSceneController), rejectedPreview)
					: scene->ExchangeAnimationRuntimeController(
						node, std::move(session->originalSceneController), rejectedPreview);
				scene->EndEditorAnimationPreview(node);
				result.message = restored
					? "Scene target Animation Rig is unavailable for Socket authoring: "
						+ rigError
					: "Scene preview rollback failed after Animation Rig initialization error";
				return result;
			}

			// A dedicated preview entity may keep its Renderer disabled so normal
			// Scene flow never sees it. Enable only the runtime render node here;
			// do not mutate entity/component flags or activate unrelated behavior.
			if (Vans::VansRuntimeWorld* world = scene->GetRuntimeWorld())
			{
				const Vans::VansEntityHandle targetEntity =
					world->Entities().FindByGuid(request.entityGuid);
				if (world->Entities().Get(targetEntity))
				{
					for (Vans::VansComponentHandle component :
						world->CollectComponentsInSubtree(targetEntity))
					{
						if (component.typeId != Vans::VansRuntimeComponentType_Render)
							continue;
						session->previewRenderComponents.emplace_back(
							component, world->IsComponentEffectivelyEnabled(component));
						scene->ApplyRuntimeComponentEnabled(component, true);
					}
				}
			}
			session->playing = true;
			session->speed = 1.0f;
			SetSceneAnimationPreviewSpeed(*node, session->speed);
			node->Play(VansGraphics::VansAnimationEvaluationPurpose::EditorPreview);
			result.success = true;
			result.sessionId = session->id;
			result.message = useRetarget
				? "Preview uses the Scene target's Retarget chain"
				: "Preview Animator matches the Scene target Skeleton directly";
			GetAnimationPreviewSessions().emplace(session->id, std::move(session));
			return result;
		}

		auto* device = static_cast<VansGraphics::VansVKDevice*>(m_Device);
		if (!device)
		{
			result.message = "Runtime render device is not available for animation preview";
			return result;
		}
		session->modelGuid = request.previewModelGuid;
		std::filesystem::path modelPath;
		float scaleFactor = 1.0f;
		Vans::VansSkeletalMeshImportSettings importSettings;
		if (!ResolveAnimationPreviewModel(
			request.previewModelGuid, modelPath, scaleFactor,
			importSettings, result.message))
			return result;
		session->modelPath = modelPath.string();
		session->renderer = std::make_unique<VansGraphics::VansAnimationPreviewRenderer>();
		if (!m_RenderSystem)
		{
			result.message = "Render thread is not available for animation preview";
			return result;
		}
		if (!session->renderer->PrepareCpu(
			modelPath, scaleFactor, importSettings, result.message))
			return result;
		auto gpuState = std::make_shared<AnimationPreviewGpuTransactionState>();
		gpuState->operation = AnimationPreviewGpuTransactionState::Operation::Initialize;
		gpuState->renderer = session->renderer.get();
		if (!m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<AnimationPreviewGpuTransaction>(gpuState)))
		{
			result.message = gpuState->error.empty()
				? "Render-thread animation preview initialization failed"
				: gpuState->error;
			return result;
		}
		session->skeleton = session->renderer->GetSkeleton();
		session->visualizationColors.resize(session->skeleton.bones.size(), glm::vec4(0.0f));
		session->texture = gpuState->texture;
		if (!session->texture)
		{
			result.message = "Failed to register isolated animation preview texture";
			session->renderer.reset();
			return result;
		}
		result.success = true;
		result.sessionId = session->id;
		GetAnimationPreviewSessions().emplace(session->id, std::move(session));
		return result;
	}

	AnimationPreviewUpdateResult EngineAPIImpl::UpdateAnimationPreviewDefinition(
		const AnimationPreviewDefinitionUpdate& update)
	{
		AnimationPreviewUpdateResult result;
		auto found = GetAnimationPreviewSessions().find(update.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
		{
			result.message = "Animation preview session does not exist";
			return result;
		}
		AnimationPreviewSessionState& session = *found->second;
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			result.message =
				"Scene animation preview owns the explicitly selected Animator asset; restart the session after saving Animator edits";
			return result;
		}
		if (update.revision < session.requestedRevision)
		{
			result.message = "Stale animation preview revision was ignored";
			result.acceptedRevision = session.requestedRevision;
			result.displayedRevision = session.displayedRevision;
			result.usingLastGoodDefinition = session.controller != nullptr;
			return result;
		}
		session.requestedRevision = update.revision;
		result.acceptedRevision = update.revision;

		VansGraphics::AnimatorAssetData asset;
		std::string compileError;
		try
		{
			const nlohmann::json root = nlohmann::json::parse(update.canonicalJson);
			if (!VansGraphics::VansAnimatorIO::DeserializeFromJsonObject(root, asset, compileError))
				throw std::runtime_error(compileError.empty() ? "Animator snapshot decode failed" : compileError);
		}
		catch (const std::exception& exception)
		{
			session.diagnostic = exception.what();
			result.message = session.diagnostic;
			result.displayedRevision = session.displayedRevision;
			result.usingLastGoodDefinition = session.controller != nullptr;
			return result;
		}

		VansGraphics::VansAnimatorRuntimeCompileOptions options;
		options.enableTargetPostProcess = true;
		options.enableRootMotion = true;
		options.enableDebugMetrics = true;
		options.rigResolver = [](const std::string& guid, std::filesystem::path& path, std::string& error)
		{
			return ResolveProjectAnimationAsset(
				guid, Vans::VansAssetType::AnimationRig, {}, path, error);
		};
		options.queryProfileResolver = [](const std::string& profile, std::uint32_t& mask, std::string& error)
		{
			return Vans::VansProjectManager::Get().GetProjectSettings()
				.ResolvePhysicsQueryProfile(profile, mask, error);
		};
		auto compiled = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
			asset,
			session.skeleton,
			[](const VansGraphics::AnimatorClipRef& reference,
			   std::filesystem::path& path,
			   std::string& error)
			{
				return ResolveProjectAnimationAsset(
					reference.assetGuid, Vans::VansAssetType::AnimationClip,
					reference.pathHint, path, error);
			},
			[](const VansGraphics::VansAnimationLayerDefinition& layer,
			   std::filesystem::path& path,
			   std::string& error)
			{
				return ResolveProjectAnimationAsset(
					layer.maskGuid, Vans::VansAssetType::BoneMask,
					layer.maskPathHint, path, error);
			},
			options,
			compileError);
		if (!compiled)
		{
			session.diagnostic = compileError;
			result.message = compileError;
			result.displayedRevision = session.displayedRevision;
			result.usingLastGoodDefinition = session.controller != nullptr;
			return result;
		}

		if (update.revision != session.requestedRevision)
		{
			result.message = "Animation preview compile result became stale";
			result.displayedRevision = session.displayedRevision;
			result.usingLastGoodDefinition = session.controller != nullptr;
			return result;
		}
		compiled->SetSpeed(session.speed);
		if (session.playing) compiled->Play();
		else compiled->Pause();
		session.controller = std::move(compiled);
		{
			std::string rigError;
			if (!AnimationPreviewRigAuthoringService::BeginSession(
				session.id, *session.controller, rigError))
				session.diagnostic = std::move(rigError);
			else
				session.diagnostic.clear();
		}
		session.displayedRevision = update.revision;
		session.rootMotionPosition = glm::vec3(0.0f);
		session.rootMotionTrail.assign(1, glm::vec3(0.0f));
		session.slotHandles.clear();
		session.renderDirty = true;
		result.success = true;
		result.displayedRevision = update.revision;
		return result;
	}

	bool EngineAPIImpl::SetAnimationPreviewPlayback(const AnimationPreviewPlaybackRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
			return false;
		AnimationPreviewSessionState& session = *found->second;
		session.playing = request.playing;
		session.speed = std::clamp(request.speed, 0.0f, 8.0f);
		if (session.rootMotionMode != request.rootMotionMode)
		{
			session.rootMotionMode = request.rootMotionMode;
			session.rootMotionPosition = glm::vec3(0.0f);
			session.rootMotionTrail.assign(1, glm::vec3(0.0f));
			session.renderDirty = true;
		}
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			if (session.sceneContentRevision != m_SceneContentRevision)
			{
				session.diagnostic = "Scene preview target expired after scene reload";
				return false;
			}
			VansGraphics::VansAnimationNode* node = ResolveSceneAnimationPreviewNode(
				scene, session.entityGuid, session.animationComponentGuid);
			if (!node)
			{
				session.diagnostic = "Scene Animation Component no longer exists";
				return false;
			}
			session.diagnostic.clear();
			SetSceneAnimationPreviewSpeed(*node, session.speed);
			if (request.seek)
			{
				session.rootMotionPosition = glm::vec3(0.0f);
				session.rootMotionTrail.assign(1, glm::vec3(0.0f));
				if (!SeekSceneAnimationPreview(
					*scene, *node, request.seekSeconds, session.speed,
					session.rootMotionMode, session.rootMotionPosition,
					session.rootMotionTrail, session.diagnostic))
					return false;
				session.lastScenePoseRevision = node->GetFinalPoseView().revision;
			}
			if (request.playing)
			{
				if (node->GetState() == VansGraphics::AnimationState::Paused)
					node->Resume();
				else if (node->GetState() == VansGraphics::AnimationState::Stopped)
					node->Play(
						VansGraphics::VansAnimationEvaluationPurpose::EditorPreview);
			}
			else
				node->Pause();
			return true;
		}

		if (!session.controller)
			return true;
		session.diagnostic.clear();
		session.controller->SetSpeed(session.speed);
		if (request.seek)
		{
			session.rootMotionPosition = glm::vec3(0.0f);
			session.rootMotionTrail.assign(1, glm::vec3(0.0f));
			if (!SeekIsolatedAnimationPreview(session, request.seekSeconds))
				return false;
		}
		if (request.playing)
		{
			if (session.controller->GetPlaybackState() == VansGraphics::AnimationState::Paused)
				session.controller->Resume();
			else if (session.controller->GetPlaybackState() == VansGraphics::AnimationState::Stopped)
				session.controller->Play();
		}
		else
			session.controller->Pause();
		return true;
	}

	bool EngineAPIImpl::SetAnimationPreviewParameter(const AnimationPreviewParameterValue& value)
	{
		auto found = GetAnimationPreviewSessions().find(value.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second
			|| value.name.empty())
			return false;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		VansGraphics::VansAnimationController* resolved =
			ResolveAnimationPreviewController(*found->second, scene);
		if (!resolved)
			return false;
		auto& controller = *resolved;
		if (!controller.HasParameter(value.name))
			return false;
		switch (value.type)
		{
		case AnimationPreviewParameterType::Float: controller.SetFloat(value.name, value.floatValue); break;
		case AnimationPreviewParameterType::Bool: controller.SetBool(value.name, value.boolValue); break;
		case AnimationPreviewParameterType::Int: controller.SetInt(value.name, value.intValue); break;
		case AnimationPreviewParameterType::Trigger: controller.SetTrigger(value.name); break;
		case AnimationPreviewParameterType::Vector3:
			controller.SetVector3(value.name, glm::vec3(value.vectorValue.x, value.vectorValue.y, value.vectorValue.z)); break;
		case AnimationPreviewParameterType::Quaternion:
			controller.SetQuaternion(value.name, glm::quat(
				value.quaternionValue.w, value.quaternionValue.x,
				value.quaternionValue.y, value.quaternionValue.z)); break;
		}
		if (found->second->targetKind == AnimationPreviewTargetKind::IsolatedModel)
		{
			const bool paused = controller.GetPlaybackState() ==
				VansGraphics::AnimationState::Paused;
			if (paused) controller.Resume();
			controller.Update(0.0f, found->second->skeleton);
			if (paused) controller.Pause();
		}
		found->second->renderDirty = true;
		return true;
	}

	bool EngineAPIImpl::SwitchAnimationPreviewGraphSet(
		const AnimationPreviewGraphSetRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second
			|| request.graphSetId.empty())
			return false;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		VansGraphics::VansGraphSetSwitchResult result =
			VansGraphics::VansGraphSetSwitchResult::UnknownGraphSet;
		if (found->second->targetKind ==
			AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			if (VansGraphics::VansAnimationNode* node = ResolveSceneAnimationPreviewNode(
				scene, found->second->entityGuid,
				found->second->animationComponentGuid))
				result = node->SwitchGraphSet(request.graphSetId);
		}
		else if (found->second->controller)
			result = found->second->controller->SwitchGraphSet(request.graphSetId);
		const bool accepted = result == VansGraphics::VansGraphSetSwitchResult::Started
			|| result == VansGraphics::VansGraphSetSwitchResult::Completed
			|| result == VansGraphics::VansGraphSetSwitchResult::AlreadyActive
			|| result == VansGraphics::VansGraphSetSwitchResult::Queued;
		if (accepted) found->second->renderDirty = true;
		return accepted;
	}

	bool EngineAPIImpl::TriggerAnimationPreviewSlot(const AnimationPreviewSlotRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
			return false;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		VansGraphics::VansAnimationController* controller =
			ResolveAnimationPreviewController(*found->second, scene);
		if (!controller)
			return false;
		VansGraphics::VansSlotPlayRequest play;
		play.clipName = request.clipName;
		play.playRate = request.playRate;
		play.loopCount = request.loopCount;
		play.priority = request.priority;
		const VansGraphics::VansSlotPlaybackHandle handle =
			controller->PlaySlot(request.slotId, play);
		if (!handle)
			return false;
		found->second->slotHandles.push_back(handle);
		if (found->second->slotHandles.size() > 64)
			found->second->slotHandles.erase(found->second->slotHandles.begin(),
				found->second->slotHandles.begin() + 16);
		found->second->renderDirty = true;
		return true;
	}

	bool EngineAPIImpl::SetAnimationPreviewViewport(
		const AnimationPreviewViewportRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
			return false;
		AnimationPreviewSessionState& session = *found->second;
		const float yaw = std::isfinite(request.yaw) ? request.yaw : 0.0f;
		const float pitch = std::clamp(
			std::isfinite(request.pitch) ? request.pitch : 0.0f, -1.45f, 1.45f);
		const float zoom = std::clamp(
			std::isfinite(request.zoom) ? request.zoom : 1.0f, 0.2f, 3.0f);
		if (session.view.yaw != yaw || session.view.pitch != pitch
			|| session.view.zoom != zoom
			|| session.visualizedLayerIndex != request.visualizedLayerIndex)
		{
			session.view.yaw = yaw;
			session.view.pitch = pitch;
			session.view.zoom = zoom;
			session.visualizedLayerIndex = request.visualizedLayerIndex;
			session.renderDirty = true;
		}
		return true;
	}

	void EngineAPIImpl::TickAnimationPreview(AnimationPreviewSessionId sessionId, float deltaTime)
	{
		auto found = GetAnimationPreviewSessions().find(sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
			return;
		AnimationPreviewSessionState& session = *found->second;
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			VansGraphics::VansAnimationNode* node = ResolveSceneAnimationPreviewNode(
				scene, session.entityGuid, session.animationComponentGuid);
			if (!node || session.sceneContentRevision != m_SceneContentRevision)
			{
				session.diagnostic = "Scene animation preview target is no longer valid";
				return;
			}
			const auto begin = std::chrono::steady_clock::now();
			if (!scene->EvaluateEditorAnimationPreviewStep(
				node, session.playing ? std::max(deltaTime, 0.0f) : 0.0f))
			{
				session.diagnostic = "Scene rejected the Editor animation preview frame";
				return;
			}
			const auto end = std::chrono::steady_clock::now();
			session.lastUpdateMilliseconds =
				std::chrono::duration<float, std::milli>(end - begin).count();
			const std::uint64_t poseRevision = node->GetFinalPoseView().revision;
			if (session.playing && poseRevision != 0 &&
				poseRevision != session.lastScenePoseRevision &&
				session.rootMotionMode !=
					AnimationPreviewPlaybackRequest::RootMotionMode::InPlace)
			{
				AccumulateAnimationPreviewRootMotion(
					node->GetRootMotionDelta(), session.rootMotionPosition,
					session.rootMotionTrail);
			}
			session.lastScenePoseRevision = poseRevision;
			return;
		}
		if (!session.controller)
			return;
		const auto begin = std::chrono::steady_clock::now();
		session.controller->Update(
			session.playing ? std::max(deltaTime, 0.0f) : 0.0f,
			session.skeleton);
		if (session.playing
			&& session.rootMotionMode != AnimationPreviewPlaybackRequest::RootMotionMode::InPlace)
		{
			AccumulateAnimationPreviewRootMotion(
				session.controller->GetRootMotionDelta(),
				session.rootMotionPosition, session.rootMotionTrail);
		}
		const auto end = std::chrono::steady_clock::now();
		session.lastUpdateMilliseconds =
			std::chrono::duration<float, std::milli>(end - begin).count();

		session.renderAccumulator += std::max(deltaTime, 0.0f);
		if (session.renderer && (session.renderDirty || session.renderAccumulator >= (1.0f / 30.0f)))
		{
			const auto& layerInfo = session.controller->GetLayerRuntimeDebugInfo();
			if (session.visualizationColors.size() != session.skeleton.bones.size())
				session.visualizationColors.resize(session.skeleton.bones.size());
			std::fill(session.visualizationColors.begin(), session.visualizationColors.end(), glm::vec4(0.0f));
			if (session.visualizedLayerIndex >= 0
				&& session.visualizedLayerIndex < static_cast<int>(layerInfo.size()))
			{
				const auto& weights = layerInfo[session.visualizedLayerIndex].boneWeights;
				for (std::size_t bone = 0; bone < session.visualizationColors.size(); ++bone)
				{
					const float weight = bone < weights.size()
						? std::clamp(weights[bone], 0.0f, 1.0f) : 0.0f;
					session.visualizationColors[bone] = glm::vec4(
						0.25f + 0.75f * weight,
						0.25f + 0.35f * (1.0f - weight),
						0.95f - 0.80f * weight,
						0.88f);
				}
			}
			else
			{
				static const glm::vec3 palette[] = {
					{ 0.35f, 0.80f, 0.96f }, { 0.96f, 0.58f, 0.24f },
					{ 0.70f, 0.40f, 0.94f }, { 0.30f, 0.86f, 0.52f },
					{ 0.96f, 0.34f, 0.48f }
				};
				for (std::size_t bone = 0; bone < session.visualizationColors.size(); ++bone)
				{
					std::size_t dominantLayer = 0;
					float dominantWeight = 0.0f;
					for (std::size_t layer = 1; layer < layerInfo.size(); ++layer)
					{
						const auto& weights = layerInfo[layer].boneWeights;
						const float weight = bone < weights.size() ? weights[bone] : 0.0f;
						if (weight >= dominantWeight && weight > 0.0001f)
						{
							dominantLayer = layer;
							dominantWeight = weight;
						}
					}
					if (dominantLayer > 0)
						session.visualizationColors[bone] = glm::vec4(
							palette[dominantLayer % (sizeof(palette) / sizeof(palette[0]))],
							std::clamp(0.35f + dominantWeight * 0.60f, 0.0f, 0.95f));
				}
			}

			const glm::vec3 modelOffset =
				session.rootMotionMode == AnimationPreviewPlaybackRequest::RootMotionMode::VisualOffset
					? session.rootMotionPosition : glm::vec3(0.0f);
			std::string renderError;
			if (!session.renderer->RasterizeFrame(
				session.controller->GetBoneMatricesSSBO(),
				session.visualizationColors,
				modelOffset,
				session.view,
				renderError))
			{
				session.diagnostic = renderError;
			}
			else if (!m_RenderSystem)
			{
				session.diagnostic =
					"Render thread is not available for animation preview upload";
			}
			else
			{
				auto gpuState = std::make_shared<AnimationPreviewGpuTransactionState>();
				gpuState->operation = AnimationPreviewGpuTransactionState::Operation::Upload;
				gpuState->renderer = session.renderer.get();
				if (!m_RenderSystem->ExecuteRenderThreadTransaction(
					std::make_unique<AnimationPreviewGpuTransaction>(gpuState)))
				{
					session.diagnostic = gpuState->error.empty()
						? "Render-thread animation preview upload failed"
						: gpuState->error;
				}
			}
			if (session.diagnostic.empty() && !session.renderLogged)
			{
				const auto& stats = session.renderer->GetStats();
				VANS_LOG("[AnimationPreview] Isolated skinned model rendered: vertices="
					<< stats.vertexCount << " sampledTriangles=" << stats.renderedTriangleCount
					<< " texture=" << stats.width << "x" << stats.height
					<< " renderMs=" << stats.renderMilliseconds);
				session.renderLogged = true;
			}
			session.renderAccumulator = 0.0f;
			session.renderDirty = false;
		}
	}

	AnimationPreviewSnapshot EngineAPIImpl::GetAnimationPreviewSnapshot(
		AnimationPreviewSessionId sessionId) const
	{
		AnimationPreviewSnapshot snapshot;
		auto found = GetAnimationPreviewSessions().find(sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
			return snapshot;
		AnimationPreviewSessionState& session = *found->second;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		VansGraphics::VansAnimationNode* sceneNode = nullptr;
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			sceneNode = ResolveSceneAnimationPreviewNode(
				scene, session.entityGuid, session.animationComponentGuid);
			if (!sceneNode || session.sceneContentRevision != m_SceneContentRevision)
			{
				snapshot.available = true;
				snapshot.sceneTarget = true;
				snapshot.entityGuid = session.entityGuid;
				snapshot.animationComponentGuid = session.animationComponentGuid;
				snapshot.diagnostic = "Scene animation preview target expired";
				return snapshot;
			}
		}
		VansGraphics::VansAnimationController* controller =
			ResolveAnimationPreviewController(session, scene);
		VansGraphics::VansAnimationController* poseController = sceneNode
			? sceneNode->GetController() : controller;
		const VansGraphics::Skeleton* skeleton = sceneNode
			? &sceneNode->GetSkeleton() : &session.skeleton;
		snapshot.available = true;
		snapshot.compiled = controller != nullptr && poseController != nullptr;
		snapshot.playing = session.playing;
		snapshot.sceneTarget = sceneNode != nullptr;
		snapshot.entityGuid = session.entityGuid;
		snapshot.animationComponentGuid = session.animationComponentGuid;
		snapshot.speed = session.speed;
		snapshot.requestedRevision = session.requestedRevision;
		snapshot.displayedRevision = session.displayedRevision;
		snapshot.usingLastGoodDefinition = !sceneNode && session.controller
			&& session.displayedRevision != session.requestedRevision;
		snapshot.diagnostic = session.diagnostic;
		snapshot.lastUpdateMilliseconds = session.lastUpdateMilliseconds;
		if (controller)
		{
			if (controller->IsGraphSetTransitioning())
			{
				snapshot.seekSupported = false;
				snapshot.seekUnavailableReason =
					"Timeline seek waits for the active Graph Set transition to finish";
			}
			if (const VansGraphics::MotionMatchingDebugData* motionMatching =
				controller->GetMotionMatchingDebugData())
			{
				if (motionMatching->usedThisFrame)
				{
					snapshot.seekSupported = false;
					if (snapshot.seekUnavailableReason.empty())
						snapshot.seekUnavailableReason =
							"Motion Matching timeline seek is disabled because trajectory and turn-warp history require forward playback";
				}
			}
		}
		if (!sceneNode)
		{
			snapshot.modelTexture = session.texture;
			if (session.renderer && session.renderer->IsReady())
			{
				const auto& stats = session.renderer->GetStats();
				snapshot.modelRendered = session.texture != nullptr;
				snapshot.modelTextureWidth = stats.width;
				snapshot.modelTextureHeight = stats.height;
				snapshot.modelCenter = ToEditorVec3(session.renderer->GetModelCenter());
				snapshot.modelRadius = session.renderer->GetModelRadius();
				snapshot.modelVertexCount =
					static_cast<std::uint64_t>(stats.vertexCount);
				snapshot.modelTriangleCount =
					static_cast<std::uint64_t>(stats.renderedTriangleCount);
				snapshot.modelRenderMilliseconds = stats.renderMilliseconds;
			}
		}
		if (controller)
		{
			snapshot.frameScratchAllocations = static_cast<std::uint64_t>(
				controller->GetLastFrameScratchAllocations());
			snapshot.frameScratchAllocatedBytes = static_cast<std::uint64_t>(
				controller->GetLastFrameScratchAllocatedBytes());
		}

		std::vector<glm::mat4> globals(skeleton->bones.size(), glm::mat4(1.0f));
		if (poseController &&
			poseController->GetCachedGlobalTransforms().size() == skeleton->bones.size())
			globals = poseController->GetCachedGlobalTransforms();
		else
		{
			auto accumulate = [&](int index)
			{
				if (index < 0 || index >= static_cast<int>(skeleton->bones.size())) return;
				const auto& bone = skeleton->bones[index];
				globals[index] = bone.parentIndex >= 0
					&& bone.parentIndex < static_cast<int>(globals.size())
					? globals[bone.parentIndex] * bone.localTransform : bone.localTransform;
			};
			if (!skeleton->topologicalOrder.empty())
				for (int index : skeleton->topologicalOrder) accumulate(index);
			else
				for (int index = 0; index < static_cast<int>(skeleton->bones.size()); ++index)
					accumulate(index);
		}
		snapshot.bones.reserve(skeleton->bones.size());
		for (std::size_t index = 0; index < skeleton->bones.size(); ++index)
		{
			AnimationPreviewBoneSnapshot bone;
			bone.guid = skeleton->bones[index].guid;
			bone.name = skeleton->bones[index].name;
			bone.parentIndex = skeleton->bones[index].parentIndex;
			glm::vec3 position = glm::vec3(globals[index][3]);
			if (!sceneNode && session.rootMotionMode ==
				AnimationPreviewPlaybackRequest::RootMotionMode::VisualOffset)
				position += session.rootMotionPosition;
			bone.position = ToEditorVec3(position);
			snapshot.bones.push_back(std::move(bone));
		}
		snapshot.sockets = GetAnimationPreviewRigSnapshot(sessionId).sockets;
		if (sceneNode && !globals.empty())
		{
			glm::vec3 minimum(std::numeric_limits<float>::max());
			glm::vec3 maximum(std::numeric_limits<float>::lowest());
			for (const glm::mat4& transform : globals)
			{
				const glm::vec3 position(transform[3]);
				minimum = glm::min(minimum, position);
				maximum = glm::max(maximum, position);
			}
			const glm::vec3 center = (minimum + maximum) * 0.5f;
			snapshot.modelCenter = ToEditorVec3(center);
			snapshot.modelRadius = (std::max)(glm::length(maximum - center), 0.01f);
		}
		if (!controller)
			return snapshot;

		snapshot.currentTime = controller->GetCurrentPlayTime();
		snapshot.duration = controller->GetCurrentDuration();
		snapshot.normalizedTime = controller->GetNormalizedTime();
		snapshot.activeGraphSetId = controller->GetActiveGraphSetId();
		snapshot.incomingGraphSetId = controller->GetIncomingGraphSetId();
		snapshot.graphSetTransitionProgress =
			controller->GetGraphSetTransitionProgress();
		snapshot.rootMotionDelta = ToEditorVec3(controller->GetRootMotionDelta());
		snapshot.rootMotionPosition = ToEditorVec3(session.rootMotionPosition);
		snapshot.rootMotionTrail.reserve(session.rootMotionTrail.size());
		for (const glm::vec3& point : session.rootMotionTrail)
			snapshot.rootMotionTrail.push_back(ToEditorVec3(point));
		for (const auto& source : controller->GetLayerRuntimeDebugInfo())
		{
			AnimationPreviewLayerSnapshot layer;
			layer.id = source.id;
			layer.name = source.name;
			layer.state = source.state;
			layer.clip = source.clip;
			layer.weight = source.weight;
			layer.normalizedTime = source.normalizedTime;
			layer.enabled = source.enabled;
			layer.overlay = source.kind == VansGraphics::VansAnimationLayerKind::Overlay;
			layer.additive = source.blendMode == VansGraphics::VansLayerBlendMode::Additive;
			layer.evaluationMilliseconds = source.evaluationMilliseconds;
			layer.boneWeights = source.boneWeights;
			snapshot.layers.push_back(std::move(layer));
		}
		for (std::size_t boneIndex = 0; boneIndex < snapshot.bones.size(); ++boneIndex)
		{
			for (std::size_t layerIndex = 1; layerIndex < snapshot.layers.size(); ++layerIndex)
			{
				const auto& weights = snapshot.layers[layerIndex].boneWeights;
				const float weight = boneIndex < weights.size() ? weights[boneIndex] : 0.0f;
				if (weight >= snapshot.bones[boneIndex].dominantLayerWeight && weight > 0.0001f)
				{
					snapshot.bones[boneIndex].dominantLayerIndex = static_cast<int>(layerIndex);
					snapshot.bones[boneIndex].dominantLayerWeight = weight;
				}
			}
		}
		for (const auto& source : controller->GetSampledEvents())
		{
			AnimationPreviewEventSnapshot event;
			event.name = std::string(source.name);
			event.time = source.sourceTime;
			std::visit([&](const auto& payload)
			{
				using T = std::decay_t<decltype(payload)>;
				if constexpr (std::is_same_v<T, bool>) event.payload = payload ? "true" : "false";
				else if constexpr (std::is_same_v<T, std::int64_t>) event.payload = std::to_string(payload);
				else if constexpr (std::is_same_v<T, double>) event.payload = std::to_string(payload);
				else if constexpr (std::is_same_v<T, std::string>) event.payload = payload;
				else if constexpr (std::is_same_v<T, glm::vec3>)
					event.payload = std::to_string(payload.x) + ", " + std::to_string(payload.y) + ", " + std::to_string(payload.z);
			}, source.payload);
			snapshot.events.push_back(std::move(event));
		}
		for (const auto& source : controller->GetSampledCurves())
			if (source.present)
				snapshot.curves.push_back({ std::string(source.name), source.value });
		const VansGraphics::VansAnimationSyncState& sync = controller->GetSyncState();
		snapshot.syncValid = sync.valid;
		snapshot.syncMarkerId = sync.markerId;
		snapshot.syncNextMarkerId = sync.nextMarkerId;
		snapshot.syncPhase = sync.phase;
		for (const VansGraphics::VansSlotPlaybackHandle handle : session.slotHandles)
		{
			const VansGraphics::VansSlotPlaybackStatus status =
				controller->GetSlotStatus(handle);
			if (status.state == VansGraphics::VansSlotPlaybackState::Invalid)
				continue;
			AnimationPreviewSlotSnapshot slot;
			slot.handle = handle.value;
			slot.slotId = status.slotId;
			slot.clipName = status.clipName;
			slot.tag = status.tag;
			slot.state = AnimationPreviewSlotStateName(status.state);
			slot.playbackTime = status.playbackTime;
			slot.weight = status.weight;
			snapshot.slots.push_back(std::move(slot));
		}
		for (const VansGraphics::VansSlotLifecycleEvent& event :
			controller->GetSlotLifecycleEvents())
		{
			AnimationPreviewSlotEventSnapshot output;
			output.handle = event.handle.value;
			output.slotId = event.slotId;
			output.clipName = event.clipName;
			output.type = AnimationPreviewSlotEventName(event.type);
			snapshot.slotEvents.push_back(std::move(output));
		}
		return snapshot;
	}

	AnimationPreviewRigSnapshot EngineAPIImpl::GetAnimationPreviewRigSnapshot(
		AnimationPreviewSessionId sessionId) const
	{
		auto found = GetAnimationPreviewSessions().find(sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
		{
			AnimationPreviewRigSnapshot snapshot;
			snapshot.diagnostic = "Animation preview session does not exist";
			return snapshot;
		}
		AnimationPreviewSessionState& session = *found->second;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent
			&& session.sceneContentRevision != m_SceneContentRevision)
		{
			AnimationPreviewRigSnapshot snapshot;
			snapshot.sessionId = sessionId;
			snapshot.entityGuid = session.entityGuid;
			snapshot.animationComponentGuid = session.animationComponentGuid;
			snapshot.diagnostic = "Scene animation preview target expired";
			return snapshot;
		}
		AnimationPreviewRigSnapshot snapshot =
			AnimationPreviewRigAuthoringService::GetSnapshot(
				ResolveAnimationPreviewRigContext(session, scene));
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent
			&& scene)
		{
			snapshot.attachments =
				AnimationPreviewAttachmentAuthoringService::GetSnapshots(
					sessionId, *scene, session.entityGuid,
					session.animationComponentGuid, snapshot.attachmentRevision);
			for (auto& socket : snapshot.sockets)
			{
				socket.attachmentCount = static_cast<std::size_t>(std::count_if(
					snapshot.attachments.begin(), snapshot.attachments.end(),
					[&](const auto& attachment)
					{
						return attachment.parent.kind == RuntimeParentKind::Socket
							&& attachment.parent.anchorGuid == socket.guid;
					}));
			}
		}
		return snapshot;
	}

	std::vector<AnimationPreviewSceneEntitySnapshot>
	EngineAPIImpl::QueryAnimationPreviewSceneEntities(
		AnimationPreviewSessionId sessionId) const
	{
		std::vector<AnimationPreviewSceneEntitySnapshot> entities;
		auto found = GetAnimationPreviewSessions().find(sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second
			|| found->second->targetKind !=
				AnimationPreviewTargetKind::SceneAnimationComponent
			|| found->second->sceneContentRevision != m_SceneContentRevision)
		{
			return entities;
		}
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		const Vans::VansRuntimeWorld* world = scene ? scene->GetRuntimeWorld() : nullptr;
		if (!world)
			return entities;

		for (const VansScriptObject* object : scene->GetSceneObjects())
		{
			if (!object || object->m_EntityGuid.empty()
				|| object->m_EntityGuid == found->second->entityGuid
				|| !VansGraphics::VansTransformStore::IsAllocated(object->m_TransformID))
			{
				continue;
			}
			const Vans::VansEntityHandle handle =
				world->Entities().FindByGuid(object->m_EntityGuid);
			const Vans::VansEntityRecord* entity = world->Entities().Get(handle);
			entities.push_back({
				object->m_EntityGuid,
				object->m_ObjectName.empty() ? object->m_EntityGuid : object->m_ObjectName,
				object->m_ModelAssetGuid,
				entity ? entity->hierarchyActive : false });
		}
		std::sort(entities.begin(), entities.end(),
			[](const auto& lhs, const auto& rhs)
			{
				if (lhs.name != rhs.name) return lhs.name < rhs.name;
				return lhs.entityGuid < rhs.entityGuid;
			});
		return entities;
	}

	AnimationPreviewRigEditResult EngineAPIImpl::SetAnimationPreviewRigSocketTransform(
		const AnimationPreviewRigSocketTransformRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
		{
			AnimationPreviewRigEditResult result;
			result.message = "Animation preview session does not exist";
			return result;
		}
		AnimationPreviewSessionState& session = *found->second;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		AnimationPreviewRigEditResult result =
			AnimationPreviewRigAuthoringService::SetSocketTransform(
				ResolveAnimationPreviewRigContext(session, scene), request);
		if (result.success)
		{
			session.renderDirty = true;
			session.diagnostic.clear();
		}
		else
			session.diagnostic = result.message;
		return result;
	}

	AnimationPreviewRigEditResult EngineAPIImpl::SetAnimationPreviewRigAttachmentProfile(
		const AnimationPreviewRigAttachmentProfileRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
		{
			AnimationPreviewRigEditResult result;
			result.message = "Animation preview session does not exist";
			return result;
		}
		AnimationPreviewSessionState& session = *found->second;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		AnimationPreviewRigEditResult result =
			AnimationPreviewRigAuthoringService::SetAttachmentProfile(
				ResolveAnimationPreviewRigContext(session, scene), request);
		if (result.success)
		{
			session.renderDirty = true;
			session.diagnostic.clear();
		}
		else
			session.diagnostic = result.message;
		return result;
	}

	AnimationRigDocumentDecodeResult EngineAPIImpl::GetAnimationPreviewWorkingRigDocument(
		AnimationPreviewSessionId sessionId) const
	{
		std::string canonicalJson;
		std::string error;
		if (!AnimationPreviewRigAuthoringService::GetWorkingCanonicalJson(
			sessionId, canonicalJson, error))
		{
			AnimationRigDocumentDecodeResult result;
			result.message = std::move(error);
			return result;
		}
		return AnimationAuthoringBridge::DecodeAnimationRig(canonicalJson);
	}

	AnimationPreviewAttachmentEditResult EngineAPIImpl::SetAnimationPreviewAttachmentTransform(
		const AnimationPreviewAttachmentTransformRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second
			|| found->second->targetKind != AnimationPreviewTargetKind::SceneAnimationComponent)
			return { false, 0, {}, "Attachment preview transform requires a Scene target" };
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || found->second->sceneContentRevision != m_SceneContentRevision)
			return { false, 0, {}, "Attachment preview Scene target expired" };
		auto result = AnimationPreviewAttachmentAuthoringService::SetTransform(
			request, *scene, found->second->entityGuid,
			found->second->animationComponentGuid);
		found->second->diagnostic = result.success ? std::string{} : result.message;
		return result;
	}

	AnimationPreviewAttachmentEditResult EngineAPIImpl::SetAnimationPreviewAttachmentBinding(
		const AnimationPreviewAttachmentBindingRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second
			|| found->second->targetKind != AnimationPreviewTargetKind::SceneAnimationComponent)
			return { false, 0, {}, "Attachment binding requires a Scene target" };
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || found->second->sceneContentRevision != m_SceneContentRevision)
			return { false, 0, {}, "Attachment preview Scene target expired" };
		auto result = AnimationPreviewAttachmentAuthoringService::SetBinding(
			request, *scene, found->second->entityGuid,
			found->second->animationComponentGuid);
		found->second->diagnostic = result.success ? std::string{} : result.message;
		return result;
	}

	AnimationPreviewRigEditResult EngineAPIImpl::AdoptAnimationPreviewRig(
		const AnimationPreviewRigAdoptRequest& request)
	{
		auto found = GetAnimationPreviewSessions().find(request.sessionId);
		if (found == GetAnimationPreviewSessions().end() || !found->second)
			return { false, 0, false, "Animation preview session does not exist" };
		AnimationPreviewSessionState& session = *found->second;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		auto* controller = ResolveAnimationPreviewPoseController(session, scene);
		if (!controller)
			return { false, 0, false, "Animation Rig adoption target is unavailable" };
		const AnimationPreviewRigSnapshot snapshot =
			AnimationPreviewRigAuthoringService::GetSnapshot(
				ResolveAnimationPreviewRigContext(session, scene));
		if (!snapshot.available || snapshot.rigRevision != request.expectedRigRevision)
		{
			return { false, snapshot.rigRevision, true,
				"Animation Rig adoption revision does not match the preview" };
		}
		if (session.targetKind == AnimationPreviewTargetKind::SceneAnimationComponent
			&& !session.replacedRetargetSourceController
			&& session.originalSceneController)
		{
			const VansGraphics::VansCompiledAnimationRig* savedRig =
				controller->GetAnimationRig();
			std::string error;
			if (!savedRig)
				error = "saved compiled Rig is unavailable";
			if (!savedRig || !session.originalSceneController->ReplaceAnimationRig(
				*savedRig, error))
			{
				return { false, request.expectedRigRevision, true,
					"Saved Animation Rig could not be applied to the suspended Scene controller: "
					+ error };
			}
			session.originalSceneController->SetAnimationRigAssetIdentity(
				controller->GetAnimationRigAssetGuid(),
				controller->GetAnimationRigAssetPath());
		}
		return AnimationPreviewRigAuthoringService::Adopt(request, *controller);
	}

	void EngineAPIImpl::DestroyAnimationPreview(AnimationPreviewSessionId sessionId)
	{
		auto found = GetAnimationPreviewSessions().find(sessionId);
		if (found == GetAnimationPreviewSessions().end()) return;
		if (found->second && found->second->targetKind ==
			AnimationPreviewTargetKind::SceneAnimationComponent)
		{
			AnimationPreviewSessionState& session = *found->second;
			auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
			VansGraphics::VansAnimationNode* node =
				session.originalSceneStateCaptured &&
				session.sceneContentRevision == m_SceneContentRevision
					? ResolveSceneAnimationPreviewNode(
							scene, session.entityGuid, session.animationComponentGuid)
					: nullptr;
			std::string attachmentRestoreError;
			if (!AnimationPreviewAttachmentAuthoringService::EndSession(
				sessionId, node ? scene : nullptr, attachmentRestoreError))
			{
				VANS_LOG_ERROR("[AnimationPreview] Failed to restore attachment preview state: "
					<< attachmentRestoreError);
			}
			std::string rigRestoreError;
			if (!AnimationPreviewRigAuthoringService::EndSession(
				sessionId, node ? node->GetController() : nullptr, rigRestoreError))
			{
				VANS_LOG_ERROR("[AnimationPreview] Failed to restore last-good Animation Rig: "
					<< rigRestoreError);
			}
			if (node)
			{
				std::unique_ptr<VansGraphics::VansAnimationController> previewController;
				const bool controllerRestored = session.replacedRetargetSourceController
					? node->ExchangeRetargetSourceController(
						std::move(session.originalSceneController), previewController)
					: scene->ExchangeAnimationRuntimeController(
						node, std::move(session.originalSceneController), previewController);
				if (!controllerRestored)
				{
					VANS_LOG_ERROR("[AnimationPreview] Failed to restore the Scene Animation controller");
				}
				SetSceneAnimationPreviewSpeed(*node, session.originalSpeed);
				switch (session.originalPlaybackState)
				{
				case VansGraphics::AnimationState::Playing:
				case VansGraphics::AnimationState::Blending:
					break;
				case VansGraphics::AnimationState::Paused:
					node->Pause();
					break;
				case VansGraphics::AnimationState::Stopped:
				default:
					node->Stop();
					break;
				}
				scene->EndEditorAnimationPreview(node);
				if (Vans::VansRuntimeWorld* world = scene->GetRuntimeWorld())
				{
					for (const auto& [component, enabled] :
						session.previewRenderComponents)
					{
						if (world->GetComponentHeader(component))
							scene->ApplyRuntimeComponentEnabled(component, enabled);
					}
				}
			}
		}
		else if (found->second)
		{
			std::string ignoredRigError;
			AnimationPreviewRigAuthoringService::EndSession(
				sessionId, nullptr, ignoredRigError);
			auto gpuState = std::make_shared<AnimationPreviewGpuTransactionState>();
			gpuState->operation = AnimationPreviewGpuTransactionState::Operation::Destroy;
			gpuState->texture = found->second->texture;
			gpuState->ownedRenderer = std::move(found->second->renderer);
			found->second->texture = nullptr;
			if (!m_RenderSystem || !m_RenderSystem->ExecuteRenderThreadTransaction(
				std::make_unique<AnimationPreviewGpuTransaction>(gpuState)))
			{
				VANS_LOG_ERROR("[AnimationPreview] Render-thread destruction failed.");
			}
		}
		GetAnimationPreviewSessions().erase(found);
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
			std::optional<Vans::VansSceneParentReference> parent;
			std::string parentError;
			const bool parentDecoded = TryToSceneParentReference(parentEdit.parent, parent, parentError);
			const bool parentApplied = parentDecoded && scene->SetEntityParentReferenceByGuid(
				parentEdit.entityGuid,
				parent ? &*parent : nullptr,
				ToRuntimeReparentMode(parentEdit.transformPolicy));
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
			struct MaterialOverrideState final
			{
				VansGraphics::VansScene* scene = nullptr;
				std::vector<RuntimeRendererMaterialOverrideEdit> edits;
				bool anyApplied = false;
				bool anyFailed = false;
			};
			class MaterialOverrideTransaction final
				: public VansGraphics::IVansRenderThreadTransaction
			{
			public:
				explicit MaterialOverrideTransaction(
					std::shared_ptr<MaterialOverrideState> state)
					: m_State(std::move(state)) {}

				bool Execute(VansGraphics::VansGraphicsDevice& backend) override
				{
					VANS_ASSERT_RENDER_THREAD();
					if (!m_State || !m_State->scene || !backend.WaitForIdle())
						return false;
					VansGraphics::VansMaterialLiveEditService liveEdit;
					for (const RuntimeRendererMaterialOverrideEdit& edit : m_State->edits)
					{
						const bool itemApplied = liveEdit.ApplyRendererMaterialOverride(
							m_State->scene, edit);
						m_State->anyApplied |= itemApplied;
						m_State->anyFailed |= !itemApplied;
					}
					return !m_State->anyFailed;
				}

			private:
				std::shared_ptr<MaterialOverrideState> m_State;
			};

			if (!m_RenderSystem)
			{
				failed = true;
			}
			else
			{
				auto state = std::make_shared<MaterialOverrideState>();
				state->scene = scene;
				state->edits = change.materialOverrides;
				const bool transactionSucceeded =
					m_RenderSystem->ExecuteRenderThreadTransaction(
						std::make_unique<MaterialOverrideTransaction>(state));
				applied |= state->anyApplied;
				failed |= !transactionSucceeded || state->anyFailed;
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
		if (!scene || !m_RenderSystem || change.Empty())
			return false;
		const std::filesystem::path assetPath(change.assetPath);
		bool parameterApplied = false;
		if (!change.parameters.empty())
		{
			VansGraphics::VansMaterialLiveEditService liveEdit;
			parameterApplied = liveEdit.ApplyMaterialPreviewChange(
				scene, assetPath, change.parameters, {});
		}
		if (change.textures.empty())
			return parameterApplied;

		struct MaterialPreviewState final
		{
			VansGraphics::VansScene* scene = nullptr;
			std::filesystem::path assetPath;
			std::vector<RuntimeMaterialTextureEdit> textures;
			bool applied = false;
		};
		class MaterialPreviewTransaction final
			: public VansGraphics::IVansRenderThreadTransaction
		{
		public:
			explicit MaterialPreviewTransaction(
				std::shared_ptr<MaterialPreviewState> state)
				: m_State(std::move(state)) {}

			bool Execute(VansGraphics::VansGraphicsDevice&) override
			{
				VANS_ASSERT_RENDER_THREAD();
				if (!m_State || !m_State->scene)
					return false;
				VansGraphics::VansMaterialLiveEditService liveEdit;
				m_State->applied = liveEdit.ApplyMaterialPreviewChange(
					m_State->scene,
					m_State->assetPath,
					{},
					m_State->textures);
				return m_State->applied;
			}

		private:
			std::shared_ptr<MaterialPreviewState> m_State;
		};

		auto state = std::make_shared<MaterialPreviewState>();
		state->scene = scene;
		state->assetPath = assetPath;
		state->textures = change.textures;
		const bool textureTransactionSucceeded =
			m_RenderSystem->ExecuteRenderThreadTransaction(
			std::make_unique<MaterialPreviewTransaction>(state)) &&
			state->applied;
		return parameterApplied || textureTransactionSucceeded;
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
				{ "scatter", ScenePropertyValues::Float(settings.bloomScatter) },
				{ "clamp", ScenePropertyValues::Float(settings.bloomClamp) },
				{ "tintR", ScenePropertyValues::Float(settings.bloomTintR) },
				{ "tintG", ScenePropertyValues::Float(settings.bloomTintG) },
				{ "tintB", ScenePropertyValues::Float(settings.bloomTintB) },
				{ "shapeMode", ScenePropertyValues::Int(settings.bloomShapeMode) },
				{ "shapeIntensity", ScenePropertyValues::Float(settings.bloomShapeIntensity) },
				{ "shapeBlend", ScenePropertyValues::Float(settings.bloomShapeBlend) },
				{ "shapeAngleDeg", ScenePropertyValues::Float(settings.bloomShapeAngleDeg) },
				{ "anamorphicStretch", ScenePropertyValues::Float(settings.bloomAnamorphicStretch) },
				{ "streakCount", ScenePropertyValues::Int(settings.bloomStreakCount) },
				{ "streakLength", ScenePropertyValues::Float(settings.bloomStreakLength) },
				{ "streakAttenuation", ScenePropertyValues::Float(settings.bloomStreakAttenuation) }
			}) },
			{ "dof", ScenePropertyValues::Object({
				{ "enable", ScenePropertyValues::Bool(settings.enableDOF) },
				{ "focusDistance", ScenePropertyValues::Float(settings.focusDistance) },
				{ "focalLengthMm", ScenePropertyValues::Float(settings.focalLengthMm) },
				{ "fStop", ScenePropertyValues::Float(settings.fStop) },
				{ "sensorHeightMm", ScenePropertyValues::Float(settings.sensorHeightMm) },
				{ "maxCoC", ScenePropertyValues::Float(settings.maxCoC) },
				{ "blurTransmissionBackground", ScenePropertyValues::Bool(settings.dofBlurTransmissionBackground) }
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

	EnvironmentSettings EngineAPIImpl::GetEnvironmentSettings() const
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		return scene ? ToAPIEnvironmentSettings(scene->GetEnvironmentSettings()) : EnvironmentSettings{};
	}

	void EngineAPIImpl::ApplyEnvironmentSettings(const EnvironmentSettings& settings)
	{
		if (m_RenderSystem && !m_RenderSystem->WaitForIdle())
			return;
		SubmitCommand(std::make_unique<SetEnvironmentSettingsCommand>(
			ToRuntimeEnvironmentSettings(settings)));
	}

	void EngineAPIImpl::CommitEnvironmentSettings()
	{
		const EnvironmentSettings settings = GetEnvironmentSettings();
		auto float3 = [](const std::array<float, 3>& value)
		{
			return ScenePropertyValues::Array({
				ScenePropertyValues::Float(value[0]),
				ScenePropertyValues::Float(value[1]),
				ScenePropertyValues::Float(value[2])
			});
		};
		auto double3 = [](const std::array<double, 3>& value)
		{
			return ScenePropertyValues::Array({
				ScenePropertyValues::Float(value[0]),
				ScenePropertyValues::Float(value[1]),
				ScenePropertyValues::Float(value[2])
			});
		};
		std::vector<ScenePropertyValue> celestialBodies;
		celestialBodies.reserve(settings.physicalAtmosphere.celestialBodies.size());
		for (const CelestialBodySettings& body :
			settings.physicalAtmosphere.celestialBodies)
		{
			celestialBodies.push_back(ScenePropertyValues::Object({
				{ "name", ScenePropertyValues::String(body.name) },
				{ "lightEntityId", ScenePropertyValues::String(body.lightEntityId) },
				{ "disk", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(body.disk.enabled) },
					{ "angularRadiusRadians", ScenePropertyValues::Float(body.disk.angularRadiusRadians) },
					{ "featherRadians", ScenePropertyValues::Float(body.disk.featherRadians) },
					{ "radianceScale", ScenePropertyValues::Float(body.disk.radianceScale) },
					{ "occlusionStrength", ScenePropertyValues::Float(body.disk.occlusionStrength) }
				}) }
			}));
		}
		const CloudSettings& cloud = settings.volumetricClouds;
		m_PendingScenePropertyEdits.push_back({
			"/settings/environment",
			ScenePropertyValues::Object({
				{ "planet", ScenePropertyValues::Object({
					{ "centerWorldMeters", double3(settings.planet.centerWorldMeters) },
					{ "bottomRadiusMeters", ScenePropertyValues::Float(settings.planet.bottomRadiusMeters) },
					{ "atmosphereHeightMeters", ScenePropertyValues::Float(settings.planet.atmosphereHeightMeters) }
				}) },
				{ "physicalAtmosphere", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(settings.physicalAtmosphere.enabled) },
					{ "groundAlbedo", float3(settings.physicalAtmosphere.groundAlbedo) },
					{ "rayleigh", ScenePropertyValues::Object({
						{ "scatteringPerMeterAtGround", float3(settings.physicalAtmosphere.rayleigh.scatteringPerMeterAtGround) },
						{ "densityScaleHeightMeters", ScenePropertyValues::Float(settings.physicalAtmosphere.rayleigh.densityScaleHeightMeters) }
					}) },
					{ "mie", ScenePropertyValues::Object({
						{ "scatteringPerMeterAtGround", float3(settings.physicalAtmosphere.mie.scatteringPerMeterAtGround) },
						{ "absorptionPerMeterAtGround", float3(settings.physicalAtmosphere.mie.absorptionPerMeterAtGround) },
						{ "densityScaleHeightMeters", ScenePropertyValues::Float(settings.physicalAtmosphere.mie.densityScaleHeightMeters) },
						{ "anisotropy", ScenePropertyValues::Float(settings.physicalAtmosphere.mie.anisotropy) }
					}) },
					{ "ozone", ScenePropertyValues::Object({
						{ "absorptionPerMeter", float3(settings.physicalAtmosphere.ozone.absorptionPerMeter) },
						{ "centerAltitudeMeters", ScenePropertyValues::Float(settings.physicalAtmosphere.ozone.centerAltitudeMeters) },
						{ "halfWidthMeters", ScenePropertyValues::Float(settings.physicalAtmosphere.ozone.halfWidthMeters) }
					}) },
					{ "aerialPerspective", ScenePropertyValues::Object({
						{ "distanceScale", ScenePropertyValues::Float(settings.physicalAtmosphere.aerialPerspective.distanceScale) }
					}) },
					{ "mainLightVolumetricScatteringScale", ScenePropertyValues::Float(
						settings.physicalAtmosphere.mainLightVolumetricScatteringScale) },
					{ "celestialBodies", ScenePropertyValues::Array(std::move(celestialBodies)) }
				}) },
				{ "heightFog", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(settings.heightFog.enabled) },
					{ "groundHeightWorldMeters", ScenePropertyValues::Float(settings.heightFog.groundHeightWorldMeters) },
					{ "visibilityAtGroundMeters", ScenePropertyValues::Float(settings.heightFog.visibilityAtGroundMeters) },
					{ "densityFalloffHeightMeters", ScenePropertyValues::Float(settings.heightFog.densityFalloffHeightMeters) },
					{ "startDistanceMeters", ScenePropertyValues::Float(settings.heightFog.startDistanceMeters) },
					{ "nearFadeDistanceMeters", ScenePropertyValues::Float(settings.heightFog.nearFadeDistanceMeters) },
					{ "maximumDistanceMeters", ScenePropertyValues::Float(settings.heightFog.maximumDistanceMeters) },
					{ "farFadeDistanceMeters", ScenePropertyValues::Float(settings.heightFog.farFadeDistanceMeters) },
					{ "singleScatteringAlbedo", float3(settings.heightFog.singleScatteringAlbedo) },
					{ "anisotropy", ScenePropertyValues::Float(settings.heightFog.anisotropy) },
					{ "emissivePerMeter", float3(settings.heightFog.emissivePerMeter) },
					{ "skyLightingScale", ScenePropertyValues::Float(settings.heightFog.skyLightingScale) },
					{ "mainLightVolumetricScale", ScenePropertyValues::Float(settings.heightFog.mainLightVolumetricScale) },
					{ "receiveCloudShadows", ScenePropertyValues::Bool(settings.heightFog.receiveCloudShadows) }
				}) },
				{ "volumetricClouds", ScenePropertyValues::Object({
					{ "enabled", ScenePropertyValues::Bool(cloud.enabled) },
					{ "cloudMinHeight", ScenePropertyValues::Float(cloud.cloudMinHeight) },
					{ "cloudMaxHeight", ScenePropertyValues::Float(cloud.cloudMaxHeight) },
					{ "density", ScenePropertyValues::Float(cloud.density) },
					{ "coverage", ScenePropertyValues::Float(cloud.coverage) },
					{ "sunBrightness", ScenePropertyValues::Float(cloud.sunBrightness) },
					{ "mainTileMeters", ScenePropertyValues::Float(cloud.mainTileMeters) },
					{ "detailTileMeters", ScenePropertyValues::Float(cloud.detailTileMeters) },
					{ "mainHeightScale", ScenePropertyValues::Float(cloud.mainHeightScale) },
					{ "detailHeightScale", ScenePropertyValues::Float(cloud.detailHeightScale) },
					{ "thresholdLowCoverage", ScenePropertyValues::Float(cloud.thresholdLowCoverage) },
					{ "thresholdHighCoverage", ScenePropertyValues::Float(cloud.thresholdHighCoverage) },
					{ "densityRemapLow", ScenePropertyValues::Float(cloud.densityRemapLow) },
					{ "densityRemapHigh", ScenePropertyValues::Float(cloud.densityRemapHigh) },
					{ "mainErosionStrength", ScenePropertyValues::Float(cloud.mainErosionStrength) },
					{ "detailErosionStrength", ScenePropertyValues::Float(cloud.detailErosionStrength) },
					{ "edgeErosionStrength", ScenePropertyValues::Float(cloud.edgeErosionStrength) },
					{ "verticalShapePower", ScenePropertyValues::Float(cloud.verticalShapePower) },
					{ "detailErosionLow", ScenePropertyValues::Float(cloud.detailErosionLow) },
					{ "detailErosionHigh", ScenePropertyValues::Float(cloud.detailErosionHigh) },
					{ "detailEdgeStrength", ScenePropertyValues::Float(cloud.detailEdgeStrength) },
					{ "sigmaTRef", ScenePropertyValues::Float(cloud.sigmaTRef) },
					{ "viewAbsorption", ScenePropertyValues::Float(cloud.viewAbsorption) },
					{ "lightAbsorption", ScenePropertyValues::Float(cloud.lightAbsorption) },
					{ "singleScatteringAlbedo", ScenePropertyValues::Float(cloud.singleScatteringAlbedo) },
					{ "forwardEccentricity", ScenePropertyValues::Float(cloud.forwardEccentricity) },
					{ "backwardEccentricity", ScenePropertyValues::Float(cloud.backwardEccentricity) },
					{ "msAttenuation", ScenePropertyValues::Float(cloud.msAttenuation) },
					{ "msContribution", ScenePropertyValues::Float(cloud.msContribution) },
					{ "msEccentricity", ScenePropertyValues::Float(cloud.msEccentricity) },
					{ "scatteringTintR", ScenePropertyValues::Float(cloud.scatteringTintR) },
					{ "scatteringTintG", ScenePropertyValues::Float(cloud.scatteringTintG) },
					{ "scatteringTintB", ScenePropertyValues::Float(cloud.scatteringTintB) },
					{ "scatterSourceODScale", ScenePropertyValues::Float(cloud.scatterSourceODScale) },
					{ "scatterSourceCurvePow", ScenePropertyValues::Float(cloud.scatterSourceCurvePow) },
					{ "aoUpwardScale", ScenePropertyValues::Float(cloud.aoUpwardScale) },
					{ "ambientBottomStrength", ScenePropertyValues::Float(cloud.ambientBottomStrength) },
					{ "ambientTopStrength", ScenePropertyValues::Float(cloud.ambientTopStrength) },
					{ "ambientDuskWarmth", ScenePropertyValues::Float(cloud.ambientDuskWarmth) },
					{ "boundaryConfidence", ScenePropertyValues::Float(cloud.boundaryConfidence) },
					{ "boundaryWrap", ScenePropertyValues::Float(cloud.boundaryWrap) },
					{ "phiFwdIntensity", ScenePropertyValues::Float(cloud.phiFwdIntensity) },
					{ "phiFwdDepthPow", ScenePropertyValues::Float(cloud.phiFwdDepthPow) },
					{ "phiFwdDepthBias", ScenePropertyValues::Float(cloud.phiFwdDepthBias) },
					{ "phiFwdMSBuildScale", ScenePropertyValues::Float(cloud.phiFwdMSBuildScale) },
					{ "phiFwdCompress", ScenePropertyValues::Float(cloud.phiFwdCompress) },
					{ "phiFwdMaxDistance", ScenePropertyValues::Float(cloud.phiFwdMaxDistance) },
					{ "phiFwdConeRatio", ScenePropertyValues::Float(cloud.phiFwdConeRatio) },
					{ "phiFwdMinStep", ScenePropertyValues::Float(cloud.phiFwdMinStep) },
					{ "lightStepCount", ScenePropertyValues::Float(cloud.lightStepCount) },
					{ "boundaryGradientStep", ScenePropertyValues::Float(cloud.boundaryGradientStep) },
					{ "boundaryGradientStrength", ScenePropertyValues::Float(cloud.boundaryGradientStrength) },
					{ "shadingDebugMode", ScenePropertyValues::Float(cloud.shadingDebugMode) },
					{ "shadow", ScenePropertyValues::Object({
						{ "enabled", ScenePropertyValues::Bool(cloud.shadow.enabled) },
						{ "atmosphereStrength", ScenePropertyValues::Float(cloud.shadow.atmosphereStrength) },
						{ "ambientOcclusionStrength", ScenePropertyValues::Float(cloud.shadow.ambientOcclusionStrength) }
					}) }
				}) }
			})
		});
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
		// 编辑器 Play 仍与完整编辑器 UI 共用一个原生窗口。GLFW 的捕获模式会
		// 同时锁定并隐藏系统光标，因此编辑器内始终禁止脚本开启捕获；独立运行时
		// 继续由 ForestRuntimeExports 显式开放该能力。
		Vans::VansInputManager::Get().SetCursorCaptureAllowed(false);
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

	RuntimeTransformSnapshot EngineAPIImpl::GetRuntimeTransform(
		const std::string& entityGuid, RuntimeTransformSpace space) const
	{
		RuntimeTransformSnapshot snapshot;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || entityGuid.empty())
			return snapshot;

		ReadRuntimeTransformById(
			scene,
			ResolveRuntimeTransformId(scene, entityGuid),
			entityGuid,
			space,
			snapshot);
		return snapshot;
	}

	RuntimeTransformEditResult EngineAPIImpl::ApplyRuntimeTransform(const RuntimeTransformEdit& edit)
	{
		RuntimeTransformEditResult result;
		if (edit.entityGuid.empty())
		{
			result.message = "Runtime transform edit requires an entity GUID";
			return result;
		}
		if (edit.space == RuntimeTransformSpace::Model)
		{
			result.message = "Model-space entity transform edits are not supported";
			return result;
		}
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		const std::uint32_t transformId = ResolveRuntimeTransformId(scene, edit.entityGuid);
		RuntimeTransformSnapshot current;
		if (!ReadRuntimeTransformById(scene, transformId, edit.entityGuid, edit.space, current))
		{
			result.message = "Runtime entity transform is unavailable";
			return result;
		}

		SubmitCommand(std::make_unique<SetRuntimeTransformCommand>(edit));
		ReadRuntimeTransformById(scene, transformId, edit.entityGuid,
			RuntimeTransformSpace::Local, result.localTransform);
		ReadRuntimeTransformById(scene, transformId, edit.entityGuid,
			RuntimeTransformSpace::World, result.worldTransform);
		result.applied = edit.space == RuntimeTransformSpace::Local
			? result.localTransform.available : result.worldTransform.available;
		result.message = result.applied ? "Runtime transform applied"
			: "Runtime transform could not be applied";
		return result;
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

	void EngineAPIImpl::PrepareRuntimeCharacterLocomotion(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady())
			return;
		scene->PrepareCharacterLocomotion(static_cast<float>(deltaSeconds));
	}

	void EngineAPIImpl::UpdateRuntimeNonCameraScripts()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady() || !m_ScriptContext)
			return;

		m_ScriptContext->SetScene(scene);
		m_ScriptContext->VansScriptUpdateNonCameraScripts();
	}

	void EngineAPIImpl::UpdateRuntimeActionsEarly(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->UpdateActionsEarly(deltaSeconds);
	}

	void EngineAPIImpl::UpdateRuntimeAI(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->UpdateAI(deltaSeconds);
	}

	void EngineAPIImpl::RunRuntimeActionLateContinuation()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->RunActionLateContinuation();
	}

	void EngineAPIImpl::UpdateRuntimeTimelinesPostScript(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->UpdateTimelinesPostScript(deltaSeconds);
	}

	void EngineAPIImpl::UpdateRuntimeCameraScripts()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady() || !m_ScriptContext)
			return;

		m_ScriptContext->SetScene(scene);
		m_ScriptContext->VansScriptUpdateCameraScripts();
	}

	void EngineAPIImpl::BeginRuntimeCameraControlFrame()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady()) scene->BeginCameraControlFrame();
	}

	void EngineAPIImpl::CaptureRuntimeCameraControlBase()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady()) scene->CaptureCameraControlBase();
	}

	void EngineAPIImpl::UpdateRuntimeTimelinesCamera(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->UpdateTimelinesCamera(deltaSeconds);
	}

	void EngineAPIImpl::UpdateTimelinePreviewsPostScript(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->UpdateTimelinePreviewsPostScript(deltaSeconds);
	}

	void EngineAPIImpl::UpdateTimelinePreviewsCamera(double deltaSeconds)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady())
			scene->UpdateTimelinePreviewsCamera(deltaSeconds);
	}

	void EngineAPIImpl::ResolveRuntimeCameraControlFrame()
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (scene && scene->IsSceneReady()) scene->ResolveCameraControlFrame();
	}

	TimelinePreviewResult EngineAPIImpl::GetTimelinePreview(const std::string& previewId) const
	{
		TimelinePreviewResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		int state = 0;
		std::int64_t tick = 0;
		if (!scene || !scene->GetTimelinePreviewState(previewId, state, tick))
		{
			result.message = "Timeline preview is detached";
			return result;
		}
		result.success = true;
		result.currentTick = tick;
		switch (static_cast<Vans::VansTimelinePlayerState>(state))
		{
		case Vans::VansTimelinePlayerState::Playing: result.state = TimelinePreviewState::Playing; break;
		case Vans::VansTimelinePlayerState::Paused: result.state = TimelinePreviewState::Paused; break;
		case Vans::VansTimelinePlayerState::Completed: result.state = TimelinePreviewState::Completed; break;
		case Vans::VansTimelinePlayerState::Error: result.state = TimelinePreviewState::Error; break;
		default: result.state = TimelinePreviewState::Stopped; break;
		}
		return result;
	}

	TimelinePreviewResult EngineAPIImpl::StartTimelinePreview(const TimelinePreviewStartRequest& request)
	{
		TimelinePreviewResult result;
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->IsSceneReady())
		{
			result.message = "Runtime scene is not ready";
			return result;
		}
		std::string ownerEntityGuid = request.ownerEntityGuid;
		if (ownerEntityGuid.empty() && !request.sourceAssetPath.empty())
		{
			const Vans::VansAssetDatabase* database =
				Vans::VansProjectManager::Get().GetAssetDatabase();
			const auto record = database
				? database->Find(std::filesystem::path(request.sourceAssetPath)) : std::nullopt;
			if (record && record->type == Vans::VansAssetType::Timeline)
				ownerEntityGuid = scene->FindTimelineInstanceOwnerGuid(record->guid.ToString());
		}
		if (!scene->StartTimelinePreview(request.previewId, request.canonicalJson,
			ownerEntityGuid, request.safeEvents, request.includeSubTimelines, result.message))
			return result;
		result = GetTimelinePreview(request.previewId);
		result.ownerEntityGuid = std::move(ownerEntityGuid);
		return result;
	}

	TimelinePreviewResult EngineAPIImpl::ConfigureTimelinePreviewPlayback(
		const TimelinePreviewPlaybackRequest& request)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->ConfigureTimelinePreviewPlayback(request.previewId,
			request.playRate, request.direction, request.loopPlaybackRange))
			return { false, TimelinePreviewState::Detached, 0, "Timeline preview is unavailable" };
		return GetTimelinePreview(request.previewId);
	}

	TimelinePreviewResult EngineAPIImpl::PlayTimelinePreview(const std::string& previewId)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->PlayTimelinePreview(previewId))
			return { false, TimelinePreviewState::Detached, 0, "Timeline preview is unavailable" };
		return GetTimelinePreview(previewId);
	}

	TimelinePreviewResult EngineAPIImpl::PauseTimelinePreview(const std::string& previewId)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->PauseTimelinePreview(previewId))
			return { false, TimelinePreviewState::Detached, 0, "Timeline preview is unavailable" };
		return GetTimelinePreview(previewId);
	}

	TimelinePreviewResult EngineAPIImpl::SeekTimelinePreview(
		const std::string& previewId,
		std::int64_t tick,
		bool safeEdges)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		if (!scene || !scene->SeekTimelinePreview(previewId, tick, safeEdges))
			return { false, TimelinePreviewState::Detached, 0, "Timeline preview is unavailable" };
		return GetTimelinePreview(previewId);
	}

	TimelinePreviewResult EngineAPIImpl::StopTimelinePreview(const std::string& previewId)
	{
		auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
		TimelinePreviewResult result;
		result.success = scene && scene->StopTimelinePreview(previewId);
		result.state = TimelinePreviewState::Detached;
		if (!result.success) result.message = "Timeline preview is unavailable";
		return result;
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
		if (m_RenderSystem && !m_RenderSystem->WaitForIdle())
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
		if (m_RenderSystem && !m_RenderSystem->WaitForIdle())
			return;

		std::unique_ptr<IEngineCommand> command = std::move(m_RedoStack.back());
		m_RedoStack.pop_back();
		EngineCommandContext context(m_Scene, m_Device);
		command->Execute(context);
		m_UndoStack.push_back(std::move(command));
		m_AllowNextCommandMerge = false;
	}

}

