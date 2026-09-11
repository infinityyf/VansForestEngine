#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/RayTracingCore/VansRayTracing.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <crtdbg.h>

namespace
{
    using namespace VansGraphics;
    uint32_t errors = 0, createdViews = 0, injectedFailures = 0, layoutCalls = 0, failAtLayout = 1;
    std::set<VkImageView> candidateViews;
    PFN_vkCreateImageView originalCreateView = nullptr;
    PFN_vkDestroyImageView originalDestroyView = nullptr;
    PFN_vkCreateDescriptorSetLayout originalCreateLayout = nullptr;
    void Check(bool valid, const std::string& message)
    { if (!valid) throw std::runtime_error(message); }
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        { ++errors; std::cerr << "[GIResourcesGPU validation] " << message->pMessage << std::endl; }
        return VK_FALSE;
    }
    VKAPI_ATTR VkResult VKAPI_CALL CountCreateView(VkDevice device, const VkImageViewCreateInfo* info,
        const VkAllocationCallbacks* allocator, VkImageView* view)
    {
        const auto result = originalCreateView(device, info, allocator, view);
        if (result == VK_SUCCESS) { ++createdViews; candidateViews.insert(*view); }
        return result;
    }
    VKAPI_ATTR void VKAPI_CALL CountDestroyView(VkDevice device, VkImageView view,
        const VkAllocationCallbacks* allocator)
    { candidateViews.erase(view); originalDestroyView(device, view, allocator); }
    VKAPI_ATTR VkResult VKAPI_CALL FailCreateLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* info,
        const VkAllocationCallbacks* allocator, VkDescriptorSetLayout* layout)
    {
        if (++layoutCalls != failAtLayout) return originalCreateLayout(device, info, allocator, layout);
        ++injectedFailures; *layout = VK_NULL_HANDLE; return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    struct LateAllocationFailure
    {
        explicit LateAllocationFailure(uint32_t target)
        {
            createdViews = injectedFailures = 0; candidateViews.clear();
            layoutCalls = 0; failAtLayout = target;
            originalCreateView = vkCreateImageView;
            originalDestroyView = vkDestroyImageView;
            originalCreateLayout = vkCreateDescriptorSetLayout;
            vkCreateImageView = CountCreateView;
            vkDestroyImageView = CountDestroyView;
            vkCreateDescriptorSetLayout = FailCreateLayout;
        }
        ~LateAllocationFailure()
        {
            vkCreateImageView = originalCreateView;
            vkDestroyImageView = originalDestroyView;
            vkCreateDescriptorSetLayout = originalCreateLayout;
        }
    };
    struct Window final : INativeWindowProvider
    {
        GLFWwindow* handle = nullptr;
        ~Window() { if (handle) glfwDestroyWindow(handle); glfwTerminate(); }
        void* GetNativeWindowHandle() const override { return handle; }
    };
    void Submit(VansVKDevice& device, VansVKCommandBuffer& command)
    {
        Check(command.EndCommandBufferRecord() && VansVKCommandBuffer::SubmitCommands(
            device.GetGraphicsQueue(), device.GetLogicDevice(), {command.GetVKCommandBuffer()}, {}, {},
            command.m_CommandBufferFinishSubmitFence) && command.ResetCommandBuffer(false), "Fixture submit failed");
    }
    void VerifyAtlas(VansVKDevice& device, VansVKCommandBuffer& command, VkImage image)
    {
        VansVKBuffer readback;
        Check(readback.CreatVulkanBuffer(device.GetLogicDevice(), 8u, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            && readback.PersistentMap(), "Atlas readback allocation failed");
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Readback begin failed");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command.GetVKCommandBuffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {1, 1, 1};
        vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(), image, VK_IMAGE_LAYOUT_GENERAL,
            readback.GetNativeBuffer(), 1, &copy);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, {host});
        Submit(device, command);
        readback.InvalidateMappedRange(0, 8);
        const auto* values = static_cast<const uint16_t*>(readback.GetMappedPtr());
        const bool preserved = values[0] == 0x3400 && values[1] == 0x3800 && values[2] == 0x3a00 && values[3] == 0x3c00;
        readback.DestroyVulkanBuffer(device.GetLogicDevice());
        Check(preserved, "Previous GI irradiance contents were lost");
    }
}

bool TestGIProbeResourcesGpuContract()
{
    try
    {
#ifndef _DEBUG
        Check(false, "This native Vulkan validation contract requires a Debug build");
#endif
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        std::cout << "[GIResourcesGPU] PID=" << GetCurrentProcessId() << std::endl;
        errors = 0;
        Check(m_GraphicsDevice == nullptr, "Fixture requires a separate native process");
        const char* layers = std::getenv("VK_INSTANCE_LAYERS");
        Check(layers && std::string(layers).find("VK_LAYER_KHRONOS_validation") != std::string::npos,
            "VK_LAYER_KHRONOS_validation must be explicitly enabled");
        Window window;
        Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window.handle = glfwCreateWindow(64, 64, "GI resources contract", nullptr, nullptr);
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
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        info.pfnUserCallback = Validation;
        Check(createMessenger(device->GetInstance(), &info, nullptr, &messenger) == VK_SUCCESS,
            "Validation messenger creation failed");
        auto& shaders = VansShaderManager::Get();
        shaders.RegisterRayTracingShader("RayTracingTest", "EngineAssets/Shaders/RayTracingTest", sizeof(RayTracingPushConstant));
        for (const char* name : {"GIPointLight", "GIVisibilityUpdate", "GIProbeState"})
            shaders.RegisterComputeShader(name, std::string("EngineAssets/Shaders/") + name, sizeof(RayTracingPushConstant));
        shaders.RegisterComputeShader("GIRTPreview", "EngineAssets/Shaders/GIRTPreview", sizeof(GIRTPreviewPushConstant));
        Check(shaders.LoadAll(std::filesystem::current_path().generic_string() + "/", device->GetLogicDevice()), "GI shader load failed");
        {
            auto scene = std::make_unique<VansScene>();
            // 此测试只创建资源，不绑定或追踪几何；占位条目用于真实的布局容量。
            scene->GetBLASVertexBuffers().resize(1);
            scene->GetTLASInstanceData() = {0};
            scene->GetTLASInstanceMaterials() = {{0, 0.0f}};
            scene->GetTLASInstanceGIEmission() = {glm::vec4(0)};
            VansGISettings settings;
            settings.placement.enabled = false;
            settings.regions = {GIProbeRegionDesc{}};
            settings.regions[0].overrideGridDimensions = true;
            settings.regions[0].gridDimensions = glm::uvec3(2);
            settings.regions[0].raysPerProbe = 16;
            NormalizeGISettings(settings);
            auto& gi = device->GetRayTracingContext();
            auto& command = device->GetImmediateGraphicsCommandBuffer();
            Check(gi.CreateRayTracingResource(device.get(), &command, scene.get(), settings), gi.GetResourceError());
            auto* texture = gi.GetGIRegionIrradianceAtlas(0);
            Check(gi.IsReady() && texture, "Initial GI atlas missing");
            const VkImage originalImage = texture->GetImage().GetImage();
            auto* preview = gi.GetGIRTPreviewTexture(0);
            const VkBuffer stateBuffer = gi.GetGIRegionProbeStateBuffer(0)->GetNativeBuffer();
            const VkBuffer layoutBuffer = gi.GetGIProbeLayoutBuffer().GetNativeBuffer();
            auto* pipeline = shaders.FindRayTracingShader("RayTracingTest")->GetRayTracingPipeline();
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Marker begin failed");
            VkClearColorValue color{}; color.float32[0] = .25f; color.float32[1] = .5f; color.float32[2] = .75f; color.float32[3] = 1;
            command.ClearColorImage(texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL, color);
            Submit(*device, command);
            auto resized = settings; resized.regions[0].gridDimensions = glm::uvec3(4);
            for (uint32_t attempt = 0; attempt != 3; ++attempt)
            {
                { LateAllocationFailure failure(1u + attempt * 2u);
                  Check(!gi.CreateRayTracingResource(device.get(), &command, scene.get(), resized), "Allocation failure was not reported"); }
                Check(createdViews >= 4 && injectedFailures == 1 && layoutCalls == 1u + attempt * 2u && candidateViews.empty(),
                    "Late failure did not reach its target or release candidate image views");
                Check(gi.IsReady() && gi.GetGIRegionIrradianceAtlas(0) == texture && gi.GetGIRTPreviewTexture(0) == preview &&
                    gi.GetGIRegionProbeStateBuffer(0)->GetNativeBuffer() == stateBuffer && gi.GetGIProbeLayoutBuffer().GetNativeBuffer() == layoutBuffer &&
                    GISettingsResourceLayoutEquals(gi.GetAppliedSettings(), settings) && !gi.GetResourceError().empty() &&
                    shaders.FindRayTracingShader("RayTracingTest")->GetRayTracingPipeline() == pipeline,
                    "Failed candidate replaced GI resources, settings or shared pipeline");
                VerifyAtlas(*device, command, originalImage);
            }
            auto metadata = settings; metadata.maxIndirectRadiance = .75f; metadata.regions[0].normalBias = .12f;
            gi.UpdateGISettings(metadata);
            Check(gi.GetAppliedSettings().maxIndirectRadiance == .75f && gi.GetAppliedSettings().regions[0].normalBias == .12f &&
                gi.GetGIRegionIrradianceAtlas(0) == texture, "Parameter edit rebuilt the GI atlas or lost settings");
            VerifyAtlas(*device, command, originalImage);
            Check(gi.CreateRayTracingResource(device.get(), &command, scene.get(), resized), gi.GetResourceError());
            Check(gi.GetGIRegionIrradianceAtlas(0) != texture && gi.GetGIRegionPhysicalProbeCount(0) == 64 && gi.GetResourceError().empty(),
                "Successful rebuild did not publish resized GI resources");
            VerifyAtlas(*device, command, originalImage);
            Check(device->WaitForIdle(), "GPU idle failed before cleanup");
            gi.CleanupSceneResources(device->GetLogicDevice());
        }
        Check(errors == 0, "Vulkan validation errors during GI transaction checks");
        std::cout << "[GIResourcesGPU] CHECKS PASS: three late failures; candidate view cleanup; retained irradiance readback; metadata edit; resized publication; preview retirement; validationErrors=0" << std::endl;
        destroyMessenger(device->GetInstance(), messenger, nullptr);
        device.reset(); m_GraphicsDevice = nullptr;
        std::cout << "[GIResourcesGPU] PASS: device teardown completed" << std::endl;
        return true;
    }
    catch (const std::exception& error)
    { std::cerr << "[GIResourcesGPU] FAIL: " << error.what() << std::endl; return false; }
}
