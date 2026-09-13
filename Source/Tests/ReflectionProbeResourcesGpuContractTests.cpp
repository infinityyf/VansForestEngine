#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeSystem.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include "../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeCache.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <crtdbg.h>

namespace
{
    using namespace VansGraphics;
    uint32_t errors = 0, candidateViews = 0, injectedFailures = 0;
    PFN_vkCreateImageView originalCreateView = nullptr;
    PFN_vkAllocateDescriptorSets originalAllocateSets = nullptr;
    void Check(bool valid, const std::string& message)
    {
        if (!valid) throw std::runtime_error(message);
    }
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        { ++errors; std::cerr << "[ReflectionResourcesGPU validation] " << message->pMessage << std::endl; }
        return VK_FALSE;
    }
    VKAPI_ATTR VkResult VKAPI_CALL CountCreateView(VkDevice device, const VkImageViewCreateInfo* info,
        const VkAllocationCallbacks* allocator, VkImageView* view)
    {
        const auto result = originalCreateView(device, info, allocator, view);
        if (result == VK_SUCCESS) ++candidateViews;
        return result;
    }
    VKAPI_ATTR VkResult VKAPI_CALL FailAllocateSets(VkDevice, const VkDescriptorSetAllocateInfo* info,
        VkDescriptorSet* sets)
    {
        ++injectedFailures;
        for (uint32_t i = 0; i < info->descriptorSetCount; ++i) sets[i] = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    struct LateAllocationFailure
    {
        LateAllocationFailure()
        {
            candidateViews = injectedFailures = 0;
            originalCreateView = vkCreateImageView;
            originalAllocateSets = vkAllocateDescriptorSets;
            vkCreateImageView = CountCreateView;
            // 布局已共享，改在实例仍须执行的 set 分配阶段注入晚期失败。
            vkAllocateDescriptorSets = FailAllocateSets;
        }
        ~LateAllocationFailure()
        { vkCreateImageView = originalCreateView; vkAllocateDescriptorSets = originalAllocateSets; }
    };
    struct Window final : INativeWindowProvider
    {
        GLFWwindow* handle = nullptr;
        ~Window() { if (handle) glfwDestroyWindow(handle); glfwTerminate(); }
        void* GetNativeWindowHandle() const override { return handle; }
    };
}

bool TestReflectionProbeResourcesGpuContract()
{
    try
    {
#ifndef _DEBUG
        Check(false, "This native Vulkan validation contract requires a Debug build");
#endif
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        std::cout << "[ReflectionResourcesGPU] PID=" << GetCurrentProcessId() << std::endl;
        errors = 0;
        Check(m_GraphicsDevice == nullptr, "Fixture requires a separate native process");
        { VansTexture empty; Check(!empty.GetImage().HasResources(), "Default texture unexpectedly owns resources"); }
        const char* layers = std::getenv("VK_INSTANCE_LAYERS");
        Check(layers && std::string(layers).find("VK_LAYER_KHRONOS_validation") != std::string::npos,
            "VK_LAYER_KHRONOS_validation must be explicitly enabled");
        Window window;
        Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window.handle = glfwCreateWindow(64, 64, "Reflection resources contract", nullptr, nullptr);
        Check(window.handle != nullptr, "Hidden window creation failed");
        auto device = std::make_unique<VansVKDevice>(VkExtent2D{64, 64}, &window);
        Check(device->IsInitialized(), "Engine Vulkan device initialization failed");
        m_GraphicsDevice = device.get();
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(device->GetInstance(), "vkCreateDebugUtilsMessengerEXT"));
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(device->GetInstance(), "vkDestroyDebugUtilsMessengerEXT"));
        Check(createMessenger && destroyMessenger, "Vulkan debug-utils extension is unavailable");
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        messengerInfo.pfnUserCallback = Validation;
        Check(createMessenger(device->GetInstance(), &messengerInfo, nullptr, &messenger) == VK_SUCCESS,
            "Validation messenger creation failed");
        auto& shaders = VansShaderManager::Get();
        shaders.RegisterComputeShader("ReflectionProbePrefilter", "EngineAssets/Shaders/ReflectionProbePrefilter",
            sizeof(VansReflectionProbeSystem::PrefilterPushConstants));
        Check(shaders.LoadAll(std::filesystem::current_path().generic_string() + "/", device->GetLogicDevice()),
            "Production prefilter shader load failed");
        {
            const auto cacheRoot = std::filesystem::temp_directory_path() /
                ("ForestSkySourceCache-" + std::to_string(GetCurrentProcessId()));
            Check(!std::filesystem::exists(cacheRoot), "Sky cache fixture path already exists");
            VansReflectionProbeCacheData data; data.resolution = 32;
            std::vector<VansReflectionProbeCacheSurface> surfaces;
            uint64_t bytes = 0; std::string cacheError;
            Check(VansReflectionProbeCache::Describe(32, surfaces, bytes, data.mipCount, cacheError), cacheError);
            data.texels.resize(size_t(bytes), 0);
            Check(VansReflectionProbeCache::Save(cacheRoot / VansReflectionProbeCache::FileName, data, cacheError), cacheError);
            { std::ofstream key(cacheRoot / "sky-lighting.key"); key << "sky-a"; }
            Vans::VansSceneReflectionProbeConfig config; config.hasBlock = true; config.placement.enabled = false;
            Vans::VansSceneReflectionProbeDescConfig authored; authored.name = "SkyCacheProbe";
            authored.type = "baked"; authored.resolution = 32; authored.cachePath = cacheRoot.string();
            config.probes.push_back(authored);
            for (const char* key : {"sky-a", "sky-b"})
            {
                VansReflectionProbeSystem probes;
                probes.LoadFromSceneConfig(config, "SkyCacheContract.json");
                probes.SetSkyLightingSource(key);
                Check(!probes.HasCaptureWork(), "Initial sky source queued work before checking cache validity");
                Check(probes.CreateGPUResources(*device, device->GetImmediateGraphicsCommandBuffer()), probes.GetPlacementError());
                const bool match = std::string(key) == "sky-a";
                Check(probes.GetBakeResults()[0].valid == match && probes.HasCaptureWork() != match,
                    "Sky source fingerprint failed to accept current cache or reject stale cache");
                if (match)
                {
                    probes.SetSkyLightingSource("sky-b");
                    Check(probes.GetBakeResults()[0].valid && probes.GetBakeResults()[0].dirty && probes.HasCaptureWork(),
                        "Changing sky intensity failed to preserve publication and queue recapture");
                }
                probes.Clear(device->GetLogicDevice());
            }
            std::filesystem::remove(cacheRoot / VansReflectionProbeCache::FileName);
            std::filesystem::remove(cacheRoot / "sky-lighting.key");
            std::filesystem::remove(cacheRoot);
            std::cout << "[SkyCacheGPU] PASS: current cache loaded; stale cache rejected; source changes preserve publication and recapture" << std::endl;
        }
        {
            auto scene = std::make_unique<VansScene>();
            VansReflectionProbeSystem probes;
            Vans::VansSceneReflectionProbeConfig config;
            config.hasBlock = true;
            config.placement.enabled = false;
            Vans::VansSceneReflectionProbeDescConfig authored;
            authored.name = "ResourceContractProbe"; authored.type = "realtime";
            authored.refreshMode = "on_demand"; authored.resolution = 32;
            authored.position = std::array<float, 3>{0, 2, 0};
            authored.boxMin = std::array<float, 3>{-4, 0, -4};
            authored.boxMax = std::array<float, 3>{4, 4, 4};
            config.probes.push_back(authored);
            probes.LoadFromSceneConfig(config, "ReflectionResourcesContract.json");
            auto& command = device->GetImmediateGraphicsCommandBuffer();
            Check(probes.CreateGPUResources(*device, command), probes.GetPlacementError());
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            std::vector<VkDescriptorSet> sets;
            Check(VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom({
                {GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ReflectionProbeMaxTexturePages, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {GLOBAL_BINDING_REFLECTION_PROBE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {GLOBAL_BINDING_REFLECTION_PROBE_INDEX, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}, layout, sets, 1),
                "Reflection descriptor allocation failed");
            probes.UpdateGlobalDescriptors(sets[0]);
            auto* originalTexture = probes.GetProbeTexture(0);
            const VkImage originalImage = originalTexture->GetImage().GetImage();
            const VkImageView originalPreview = probes.GetPreviewFaceView(0, 0, 0);
            Check(originalPreview != VK_NULL_HANDLE, "Initial face preview missing");
            const auto originalCount = probes.GetProbes().size();
            const auto originalBytes = probes.GetResidentTextureBytes();
            auto resized = probes.GetProbes();
            resized[0].resolution = 64;
            for (uint32_t attempt = 0; attempt != 3; ++attempt)
            {
                {
                    LateAllocationFailure failure;
                    const bool applied = attempt == 0 ? probes.CreateGPUResources(*device, command) :
                        probes.ApplySettings(*scene, *device, probes.GetPlacementSettings(), probes.GetLightingSettings(),
                            probes.GetEditorState(), resized, false);
                    Check(!applied, "Late allocation failure was not reported");
                }
                Check(candidateViews >= 2 && injectedFailures == 1, "Failure did not occur after candidate texture allocation");
                Check(probes.GetProbes().size() == originalCount && probes.GetResidentTextureBytes() == originalBytes &&
                    probes.GetProbes()[0].resolution == 32 && probes.GetAuthoredProbes()[0].resolution == 32 &&
                    probes.GetProbeTexture(0) == originalTexture && probes.GetPreviewFaceView(0, 0, 0) == originalPreview,
                    "Failed rebuild replaced the authored layout, page or preview");
                Check(!probes.GetPlacementError().empty(), "Failure reason was lost");
                VkMemoryRequirements memory{};
                vkGetImageMemoryRequirements(device->GetLogicDevice(), originalImage, &memory);
                Check(memory.size > 0, "Original image is unavailable after failed rebuild");
            }
            // 非结构编辑发布新的元数据和索引，同时保留纹理及 mip 地址。
            auto edits = probes.GetProbes();
            edits[0].position.x = 1.0f; edits[0].intensity = 0.75f;
            Check(probes.ApplySettings(*scene, *device, probes.GetPlacementSettings(), probes.GetLightingSettings(),
                probes.GetEditorState(), edits, false), probes.GetPlacementError());
            Check(probes.GetProbeTexture(0) == originalTexture && probes.GetGPUProbes()[0].positionAndRadius.x == 1.0f &&
                probes.GetGPUProbes()[0].fadeAndIntensity.y == 0.75f && probes.GetGPUProbes()[0].specularAndMip.z == 5.0f,
                "Metadata-only edit changed page ownership or lost mip/lighting data");
            edits[0].resolution = 64;
            Check(probes.ApplySettings(*scene, *device, probes.GetPlacementSettings(), probes.GetLightingSettings(),
                probes.GetEditorState(), edits, false), probes.GetPlacementError());
            Check(probes.GetProbeTexture(0) != originalTexture && probes.GetProbeResolution(0) == 64 &&
                probes.GetAuthoredProbes()[0].resolution == 64 && probes.GetPlacementError().empty(),
                "Successful rebuild did not publish new resources and clear the error");
            // 旧页面必须保留到现有帧退役队列执行，当前 GUI 帧仍可能引用其预览。
            VkMemoryRequirements retiredMemory{};
            vkGetImageMemoryRequirements(device->GetLogicDevice(), originalImage, &retiredMemory);
            Check(retiredMemory.size > 0, "Successful rebuild destroyed the current-frame preview early");
            Check(device->WaitForIdle(), "GPU idle failed before fixture cleanup");
            VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(sets);
            VansVKDescriptorManager::GetInstance()->ReleaseDescriptorSetLayout(layout);
            probes.Clear(device->GetLogicDevice());
        }
        Check(errors == 0, "Vulkan validation errors during resource transaction checks");
        std::cout << "[ReflectionResourcesGPU] CHECKS PASS: three late allocation failures preserve resources; metadata edits; full rebuild; preview retirement; validationErrors=0" << std::endl;
        destroyMessenger(device->GetInstance(), messenger, nullptr);
        device.reset();
        m_GraphicsDevice = nullptr;
        { VansTexture empty; }
        std::cout << "[ReflectionResourcesGPU] PASS: device teardown completed" << std::endl;
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ReflectionResourcesGPU] FAIL: " << error.what() << std::endl;
        return false;
    }
}
