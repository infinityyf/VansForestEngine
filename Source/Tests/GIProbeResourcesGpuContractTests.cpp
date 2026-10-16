#include <fstream>
#include <filesystem>
#include <thread>
#include <fstream>
#include "../EngineCore/AssetCore/Importers/VansTextureCooker.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/SceneCore/VansSceneResourcePlan.h"
#define VK_NO_PROTOTYPES
#include "../EngineCore/RenderCore/GICore/VansGIProbeIrradianceHistory.h"
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/RayTracingCore/VansRayTracing.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansRenderSceneSnapshot.h"
#include "../EngineCore/RenderCore/BRDFData/VansIESProfile.h"
#include "../EngineCore/RenderCore/BRDFData/VansLight.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMesh.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationCollection.h"
#include "../EngineCore/RenderCore/GeometryCore/VansSceneGeometrySnapshot.h"
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
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKMemoryAllocator.h"
#include "../EngineCore/TerrainCore/VansTerrainAsset.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include "../EngineCore/Util/VansProfiler.h"
#include "../EngineCore/RenderCore/GICore/VansGIFrameCost.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/EditorCore/VansEditorRuntimePreviewProjector.h"
#include "../EngineCore/EngineAPILayer/Private/VansMaterialLiveEditService.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include <glm/gtc/packing.hpp>
#include <cstring>
#include <array>
#include <limits>
#include "TreeShadowGpuContractTests.h"

namespace
{
    using namespace VansGraphics;
    uint32_t errors = 0, createdViews = 0, injectedFailures = 0, layoutCalls = 0, failAtLayout = 1;
    std::set<VkImageView> candidateViews;
    std::set<VkImageView> arrayViews;
    PFN_vkCreateImageView originalCreateView = nullptr;
    PFN_vkDestroyImageView originalDestroyView = nullptr;
    PFN_vkAllocateDescriptorSets originalAllocateSets = nullptr;
    PFN_vkDeviceWaitIdle originalWaitIdle = nullptr;
    VkImageViewType observedRuntimeView = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    VKAPI_ATTR VkResult VKAPI_CALL ObserveRuntimeView(VkDevice device, const VkImageViewCreateInfo* info,
        const VkAllocationCallbacks* allocator, VkImageView* view)
    {
        observedRuntimeView = info->viewType;
        return originalCreateView(device, info, allocator, view);
    }
    uint32_t instanceEditIdleCalls = 0;
    VKAPI_ATTR VkResult VKAPI_CALL CountInstanceEditWaitIdle(VkDevice device)
    { ++instanceEditIdleCalls; return originalWaitIdle(device); }
    void Check(bool valid, const std::string& message)
    { if (!valid) { std::cerr << "[GIResourcesGPU] CHECK FAILED: " << message << std::endl; throw std::runtime_error(message); } }
    class BufferUpdateObserver
    {
    public:
        explicit BufferUpdateObserver(VkCommandBuffer command) : command(command), original(vkCmdUpdateBuffer)
        { Check(!active,"Nested buffer observer");active=this;vkCmdUpdateBuffer=Observe; }
        ~BufferUpdateObserver(){vkCmdUpdateBuffer=original;active=nullptr;}
        uint64_t bytes=0;
        uint32_t commands=0;
    private:
        VkCommandBuffer command;
        PFN_vkCmdUpdateBuffer original;
        inline static BufferUpdateObserver* active=nullptr;
        static VKAPI_ATTR void VKAPI_CALL Observe(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize size,const void* data)
        {
            if(command==active->command){active->bytes+=size;++active->commands;}
            active->original(command,buffer,offset,size,data);
        }
    };
    class CaptureSubmissionObserver
    {
    public:
        CaptureSubmissionObserver():submit(vkQueueSubmit),wait(vkWaitForFences),status(vkGetFenceStatus)
        {
            Check(!active,"Nested capture observer");active=this;
            vkQueueSubmit=Submit;vkWaitForFences=Wait;vkGetFenceStatus=Status;
        }
        ~CaptureSubmissionObserver(){vkQueueSubmit=submit;vkWaitForFences=wait;vkGetFenceStatus=status;active=nullptr;}
        uint32_t submissions=0,waits=0,polls=0;
        bool holdReady=false,rejectSubmit=false;
        VkFence ignoredFence=VK_NULL_HANDLE;
    private:
        PFN_vkQueueSubmit submit;PFN_vkWaitForFences wait;PFN_vkGetFenceStatus status;
        inline static CaptureSubmissionObserver* active=nullptr;
        static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue queue,uint32_t count,const VkSubmitInfo* infos,VkFence fence)
        {
            if(active->ignoredFence && fence==active->ignoredFence)return active->submit(queue,count,infos,fence);
            ++active->submissions;
            if(active->rejectSubmit)return VK_ERROR_OUT_OF_HOST_MEMORY;
            return active->submit(queue,count,infos,fence);
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice device,uint32_t count,const VkFence* fences,VkBool32 all,uint64_t timeout)
        {if(count!=1 || !active->ignoredFence || fences[0]!=active->ignoredFence)++active->waits;return active->wait(device,count,fences,all,timeout);}
        static VKAPI_ATTR VkResult VKAPI_CALL Status(VkDevice device,VkFence fence)
        {if(active->ignoredFence && fence==active->ignoredFence)return active->status(device,fence);++active->polls;return active->holdReady?VK_NOT_READY:active->status(device,fence);}
    };
    class GICostCapture
    {
    public:
        GICostCapture(VansVKDevice& device, VansVKCommandBuffer& command) : device(device)
        {
            Check(!Vans::VansGpuProfiler::Get().IsInitialized(), "Cost fixture must own its profiler pools");
            device.InitializeGpuProfiler();
            Check(Vans::VansGpuProfiler::Get().IsInitialized(), "Native GPU profiler initialization failed");
            profiler.SetPaused(false); profiler.SetCaptureEnabled(true); profiler.BeginFrame();
            frame = profiler.GetActiveFrameIndex(); profiler.MarkCurrentFrameHasRender(); profiler.BeginRenderFrame(frame);
            Vans::VansGpuProfiler::Get().BeginQueue(command.GetVKCommandBuffer(), Vans::VansGpuQueueLane::Graphics);
        }
        ~GICostCapture()
        {
            device.WaitForDevice();
            if (!finished) profiler.EndRenderFrame(frame, false);
            profiler.EndFrame();
            Vans::VansGpuProfiler::Get().Destroy();
            profiler.SetPaused(paused); profiler.SetCaptureEnabled(captured);
        }
        void CompleteAfterSubmission(uint32_t expectedSamples)
        {
            // Submit 已等待本 CB fence；只有此时才能退役时间戳池。
            Vans::VansGpuProfiler::Get().MarkSubmittedFrameRetired(frame);
            profiler.EndRenderFrame(frame, true); profiler.EndFrame(); finished = true;
            // GPU fence 完成后，同帧后台 CPU scope 仍可能在途。停止创建新帧，
            // 仅轮询同一退役帧的组装，避免以更新的空帧覆盖它或强制伪造完成。
            profiler.SetCaptureEnabled(false);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            GIFrameCostSnapshot cost;
            do
            {
                profiler.BeginFrame();
                cost=BuildGIFrameCostSnapshot(profiler.GetTimeline(),false);
                if(cost.frameIndex==frame && cost.gpuValid)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }while(std::chrono::steady_clock::now()<deadline);
            const auto& upload = cost.stages[size_t(GICostStage::WorldUpload)];
            if(cost.frameIndex!=frame || !cost.gpuValid || upload.samples!=expectedSamples)
                std::cerr << "[GIFrameCostGPU] expectedFrame=" << frame << " actualFrame=" << cost.frameIndex
                    << " gpuValid=" << cost.gpuValid << " samples=" << upload.samples << '\n';
            Check(cost.gpuValid && cost.frameIndex == frame && upload.samples == expectedSamples &&
                std::isfinite(upload.milliseconds) && upload.milliseconds >= 0.0 &&
                std::abs(cost.gpuScopedMilliseconds-upload.milliseconds) < 1e-8,
                "Production world upload timestamp did not reach the retired GI cost snapshot");
            std::cout << "[GIFrameCostGPU] PASS: production world upload, native timestamp retirement, frame=" << frame
                << " samples=" << upload.samples << " measuredMs=" << upload.milliseconds << '\n';
        }
    private:
        VansVKDevice& device;
        Vans::VansProfiler& profiler = Vans::VansProfiler::Get();
        bool captured = profiler.IsCaptureEnabled(), paused = profiler.IsPaused(), finished = false;
        uint64_t frame = Vans::VansProfiler::INVALID_FRAME_INDEX;
    };
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
        if (result == VK_SUCCESS)
        {
            ++createdViews; candidateViews.insert(*view);
            if (info->viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) arrayViews.insert(*view);
        }
        return result;
    }
    VKAPI_ATTR void VKAPI_CALL CountDestroyView(VkDevice device, VkImageView view,
        const VkAllocationCallbacks* allocator)
    { candidateViews.erase(view); arrayViews.erase(view); originalDestroyView(device, view, allocator); }
    VKAPI_ATTR VkResult VKAPI_CALL FailAllocateSets(VkDevice device,const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* sets)
    {
        if (++layoutCalls != failAtLayout) return originalAllocateSets(device,info,sets);
        ++injectedFailures;for(uint32_t i=0;i<info->descriptorSetCount;++i)sets[i]=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    struct LateAllocationFailure
    {
        explicit LateAllocationFailure(uint32_t target)
        {
            createdViews = injectedFailures = 0; candidateViews.clear(); arrayViews.clear();
            layoutCalls = 0; failAtLayout = target;
            originalCreateView = vkCreateImageView;
            originalDestroyView = vkDestroyImageView;
            originalAllocateSets = vkAllocateDescriptorSets;
            vkCreateImageView = CountCreateView;
            vkDestroyImageView = CountDestroyView;
            vkAllocateDescriptorSets = FailAllocateSets;
        }
        ~LateAllocationFailure()
        {
            vkCreateImageView = originalCreateView;
            vkDestroyImageView = originalDestroyView;
            vkAllocateDescriptorSets = originalAllocateSets;
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
    void VerifyRuntimeTextureDimensions(VansVKDevice& device)
    {
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        struct ViewObserver
        {
            ViewObserver() { originalCreateView=vkCreateImageView; vkCreateImageView=ObserveRuntimeView; }
            ~ViewObserver() { vkCreateImageView=originalCreateView; }
        } observer;
        for (VkImageViewType type : {VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_VIEW_TYPE_CUBE})
        {
            VansTexture texture;
            Check(texture.InitTextureWithoutData(command,1,1,1,VK_FORMAT_R16G16B16A16_SFLOAT,type,false,true),
                "Singleton runtime texture allocation failed");
            const auto& info=texture.GetImage().GetImageCreateInfo();
            Check(observedRuntimeView==type && info.imageType==(type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D) &&
                info.arrayLayers==(type==VK_IMAGE_VIEW_TYPE_CUBE?6u:1u) && info.extent.depth==1 &&
                texture.GetImage().GetImageLayout()==VK_IMAGE_LAYOUT_GENERAL,
                "Singleton runtime image/view type was inferred from its extent or layout was not initialized");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Singleton clear begin failed");
            const VkClearColorValue unknown{};
            command.ClearColorImage(texture.GetImage(),VK_IMAGE_LAYOUT_GENERAL,unknown);
            Submit(device,command);
        }
        VansTexture volume;
        Check(volume.InitTextureWithoutData(command,1,1,8,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_VIEW_TYPE_3D,true,true) &&
            volume.GetImage().GetImageCreateInfo().mipLevels==4 && observedRuntimeView==VK_IMAGE_VIEW_TYPE_3D,
            "Volume mip count ignored its depth or changed the view dimension");
        std::cout<<"[GIResourcesGPU] explicit runtime dimensions: singleton 2D/3D/cube, initialized layout and depth mip chain PASS\n";
    }
    void VerifyAmbientSkyResourceOwner(VansVKDevice& device)
    {
        const auto before = device.QueryGpuMemorySnapshot();
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        struct Resources
        {
            VkDevice device;
            VansRuntimeRenderTextureManager textures;
            VansVKAmbientSkyCache cache;
            VansVKBuffer readback;
            ~Resources() { cache.Destroy(device, textures); readback.DestroyVulkanBuffer(device); }
        } resources{device.GetLogicDevice()};
        auto& cache = resources.cache;
        const std::array<const char*, 3> names{
            VansVKAmbientSkyCache::TextureX, VansVKAmbientSkyCache::TextureY, VansVKAmbientSkyCache::TextureZ};
        // 覆盖启停、世界不可用、再次启用；每次都调用真实生产创建和释放入口。
        for (uint32_t pass = 0; pass < 4; ++pass)
        {
            GIAmbientSkyCacheSettings settings;
            settings.enabled = pass != 0;
            settings.gridSpacing = pass == 3 ? 3.0f : 2.0f;
            settings.cellUpdatesPerFrame = pass == 3 ? 43u : 22u;
            const bool hasWorld = pass != 2;
            const bool enabled = settings.enabled && hasWorld;
            const glm::vec3 cameraPosition(13.25f, -7.5f, 29.0f);
            cache.Initialize(device, command, resources.textures, settings, cameraPosition, hasWorld);
            const auto publishedMemory = device.GetGIMemorySnapshot();
            Check(publishedMemory.ambientSky.bufferCapacityBytes == (enabled ? 192u : 96u) &&
                publishedMemory.ambientSky.AllocationBytes() == device.QueryGpuMemorySnapshot().allocationBytes - before.allocationBytes,
                "Enabled/disabled sky memory differs from actual allocations");
            const glm::uvec3 grid = enabled ? glm::uvec3(16, 8, 16) : glm::uvec3(1);
            const glm::vec3 expectedOrigin = enabled
                ? glm::floor(cameraPosition / settings.gridSpacing) * settings.gridSpacing
                    - glm::vec3(grid) * settings.gridSpacing * 0.5f
                : cameraPosition;
            Check(cache.origin == expectedOrigin && cache.ringOffset == glm::ivec3(0) &&
                cache.initialized == enabled && cache.frameOffset == 0, "Sky cache initial state changed");
            Check(enabled ? cache.shader == VansShaderManager::Get().FindComputeShader("AmbientSkyTransmittance") &&
                    cache.shader && cache.layout && cache.sets.size() == VansCommandRecordingConfig::MaximumFramesInFlight
                : !cache.shader && !cache.layout && cache.sets.empty(), "Sky cache descriptor/shader ownership mismatch");
            for (uint32_t slot = 0; slot < VansCommandRecordingConfig::MaximumFramesInFlight; ++slot)
            {
                auto verifyBuffer = [&](VansVKBuffer& buffer, uint32_t query)
                {
                    Check(buffer.GetBufferSize() == sizeof(VansAmbientSkyCacheParameters) && buffer.PersistentMap(),
                        "Sky cache parameter buffer missing or ABI changed");
                    buffer.InvalidateMappedRange(0, buffer.GetBufferSize());
                    VansAmbientSkyCacheParameters value{};
                    std::memcpy(&value, buffer.GetMappedPtr(), sizeof(value));
                    Check(value.originAndSpacing == glm::vec4(expectedOrigin, settings.gridSpacing) &&
                        value.gridAndQuery == glm::uvec4(grid, query) && value.ringOffset == glm::ivec4(0),
                        "Sky cache initial parameter contents changed");
                };
                verifyBuffer(cache.info[slot], 0u);
                if (enabled) verifyBuffer(cache.params[slot], settings.cellUpdatesPerFrame);
                else Check(!cache.params[slot].GetNativeBuffer(), "Disabled sky cache allocated update parameters");
            }
            const VkDeviceSize imageBytes = VkDeviceSize(grid.x) * grid.y * grid.z * 8;
            Check(resources.readback.CreatVulkanBuffer(resources.device, imageBytes * names.size(), VK_FORMAT_R32_UINT,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                && resources.readback.PersistentMap(), "Sky cache readback allocation failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Sky cache readback begin failed");
            VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ready.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {ready});
            for (uint32_t axis = 0; axis < names.size(); ++axis)
            {
                auto* texture = resources.textures.Get(names[axis]);
                Check(texture != nullptr, "Sky cache texture not registered");
                auto& image = texture->GetImage();
                const auto& info = image.GetImageCreateInfo();
                Check(info.imageType == VK_IMAGE_TYPE_3D && info.format == VK_FORMAT_R16G16B16A16_SFLOAT &&
                    info.extent.width == grid.x && info.extent.height == grid.y && info.extent.depth == grid.z &&
                    info.mipLevels == 1 && info.arrayLayers == 1 && image.GetImageLayout() == VK_IMAGE_LAYOUT_GENERAL,
                    "Sky cache image format/dimensions/layout changed");
                VkBufferImageCopy copy{};
                copy.bufferOffset = imageBytes * axis;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = info.extent;
                vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(), image.GetImage(), VK_IMAGE_LAYOUT_GENERAL,
                    resources.readback.GetNativeBuffer(), 1, &copy);
            }
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, {host});
            Submit(device, command);
            resources.readback.InvalidateMappedRange(0, resources.readback.GetBufferSize());
            const auto* bytes = static_cast<const uint8_t*>(resources.readback.GetMappedPtr());
            Check(std::all_of(bytes, bytes + resources.readback.GetBufferSize(), [](uint8_t value) { return value == 0; }),
                "Sky cache did not initialize all texels to unknown");
            resources.readback.DestroyVulkanBuffer(resources.device);
            if (pass == 2)
            {
                resources.textures.Clear();
                cache.RefreshMemory(resources.textures);
                const auto memory = device.GetGIMemorySnapshot();
                Check(memory.ambientSky.imageAllocationBytes == 0 && memory.ambientSky.bufferCapacityBytes == 96,
                    "Runtime texture clearing left stale sky image memory or cleared live buffers");
            }
            cache.Destroy(resources.device, resources.textures);
            cache.Destroy(resources.device, resources.textures);
            for (const char* name : names) Check(!resources.textures.Has(name), "Sky cache texture retained after release");
            for (uint32_t slot = 0; slot < VansCommandRecordingConfig::MaximumFramesInFlight; ++slot)
                Check(!cache.params[slot].GetNativeBuffer() && !cache.info[slot].GetNativeBuffer(), "Sky cache buffer retained after release");
            Check(!cache.shader && !cache.layout && cache.sets.empty() && !cache.initialized &&
                cache.frameOffset == 0 && cache.origin == glm::vec3(0) && cache.ringOffset == glm::ivec3(0),
                "Sky cache state retained after release");
            const auto after = device.QueryGpuMemorySnapshot();
            Check(after.allocationCount == before.allocationCount && after.allocationBytes == before.allocationBytes,
                "Sky cache lifecycle retained native allocations");
        }
        std::cout << "[GIAmbientSkyOwnerGPU] PASS: enabled/disabled/world-unavailable/re-enabled, three cleared 3D images, both slots, repeated release and zero retained allocation\n";
    }
    void VerifySSGIResourceOwner(VansVKDevice& device)
    {
        // 使用正式 Deferred 布局验证区域状态是数组，不能越过 binding 22 写入贴花/天空槽。
        {
            struct DeferredBindings
            {
                VkDevice device;
                VansVKBuffer state;
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                std::vector<VkDescriptorSet> sets;
                ~DeferredBindings()
                {
                    auto* manager = VansVKDescriptorManager::GetInstance();
                    manager->DestroyDescriptorSet(sets);
                    manager->ReleaseDescriptorSetLayout(layout);
                    state.DestroyVulkanBuffer(device);
                }
            } bindings{device.GetLogicDevice()};
            Check(bindings.state.CreatVulkanBuffer(bindings.device, 48, VK_FORMAT_UNDEFINED,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Deferred GI state allocation failed");
            VansDescriptorSetLayoutFactory::CreateAndAllocate_DeferredLighting(bindings.layout, bindings.sets);
            Check(bindings.layout && bindings.sets.size() == 1, "Deferred GI descriptor allocation failed");
            const auto beforeErrors = errors;
            auto* manager = VansVKDescriptorManager::GetInstance();
            manager->BeginDescriptorUpdate();
            manager->WriteBufferDescriptor(bindings.sets[0], DEFERRED_BINDING_GI_PROBE_STATE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, std::vector<VkDescriptorBufferInfo>(kGIMaximumRegionCount,
                    {bindings.state.GetNativeBuffer(), 0, bindings.state.GetBufferSize()}));
            manager->UpdateDescriptorSets();
            Check(errors == beforeErrors, "Deferred region array overlaps another binding or retains the old capacity");
        }
        const auto before = device.QueryGpuMemorySnapshot();
        struct Resources
        {
            VkDevice device;
            VansRuntimeRenderTextureManager textures;
            VansVKSSGI ssgi;
            ~Resources() { ssgi.Destroy(device, textures); }
        } resources{device.GetLogicDevice()};
        auto& ssgi = resources.ssgi;
        const std::array<const char*, 11> names{VansVKSSGI::TextureResult,
            VansVKSSGI::TextureProbeCacheRadiance, VansVKSSGI::TextureProbeCacheSurface,
            VansVKSSGI::TextureFilterResult, VansVKSSGI::TextureTemporalA, VansVKSSGI::TextureTemporalB,
            VansVKSSGI::TextureMomentsA, VansVKSSGI::TextureMomentsB,
            VansVKSSGI::TextureSurfaceHistoryA, VansVKSSGI::TextureSurfaceHistoryB, VansVKSSGI::TextureAtrousA};
        VansGISettings settings;
        settings.regions.resize(2);
        for (uint32_t index = 0; index < 2; ++index)
        {
            auto& region = settings.regions[index];
            region.stableId = index + 1;
            region.overrideGridDimensions = true;
            region.gridDimensions = index == 0 ? glm::uvec3(3, 2, 2) : glm::uvec3(2, 3, 3);
            region.trace.maxDistance = index == 0 ? 64.0f : 128.0f;
        }
        settings.probeOnlyDeferredOutput = true;
        settings.probeOnlyDeferredExposure = 2.5f;
        for (glm::uvec2 extent : {glm::uvec2(17, 13), glm::uvec2(31, 9)})
        {
            ssgi.Initialize(device, device.GetImmediateGraphicsCommandBuffer(), resources.textures,
                settings, extent.x, extent.y);
            Check(!ssgi.history.HasHistory() && ssgi.history.WriteIndex() == 0, "SSGI resources inherited stale history");
            for (uint32_t index = 0; index < names.size(); ++index)
            {
                auto* texture = resources.textures.Get(names[index]);
                Check(texture != nullptr, "SSGI texture missing from runtime registry");
                const auto& image = texture->GetImage().GetImageCreateInfo();
                const glm::uvec2 expected = index == 1 || index == 2 ? (extent + glm::uvec2(3)) / 4u : extent;
                const VkFormat format = index == 6 || index == 7 ? VK_FORMAT_R16G16_SFLOAT :
                    index == 8 || index == 9 ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
                Check(image.imageType == VK_IMAGE_TYPE_2D && image.format == format && image.mipLevels == 1 &&
                    image.arrayLayers == 1 && image.extent.width == expected.x && image.extent.height == expected.y &&
                    image.extent.depth == 1 && texture->GetImage().GetImageLayout() == VK_IMAGE_LAYOUT_GENERAL,
                    "SSGI texture extent/format/layout changed");
            }
            Check(ssgi.shader == VansShaderManager::Get().FindComputeShader("SSGI") && ssgi.shader &&
                ssgi.probeCacheShader == VansShaderManager::Get().FindComputeShader("SSGIProbeCache") && ssgi.probeCacheShader &&
                ssgi.temporalShader == VansShaderManager::Get().FindComputeShader("SSGITemporal") && ssgi.temporalShader &&
                ssgi.atrousShader == VansShaderManager::Get().FindComputeShader("SSGIAtrous") && ssgi.atrousShader,
                "SSGI production shaders not associated with owner");
            std::set<VkDescriptorSet> uniqueSets;
            for (const auto* sets : {&ssgi.sets, &ssgi.probeCacheSets, &ssgi.temporalSets, &ssgi.atrousSets})
            {
                Check(sets->size() == (sets == &ssgi.sets ? 2u : 4u), "SSGI descriptor frame/history slot count changed");
                for (auto set : *sets) Check(set && uniqueSets.insert(set).second, "SSGI descriptor missing or reused across slots");
            }
            Check(ssgi.layout && ssgi.probeCacheLayout && ssgi.temporalLayout && ssgi.atrousLayout,
                "SSGI descriptor layout missing");
            Check(ssgi.params.GetBufferSize() == sizeof(VansSSGIParameters) && ssgi.params.PersistentMap(), "SSGI parameter ABI changed");
            ssgi.params.InvalidateMappedRange(0, ssgi.params.GetBufferSize());
            VansSSGIParameters params{};
            std::memcpy(&params, ssgi.params.GetMappedPtr(), sizeof(params));
            const glm::vec4 screen(float(extent.x), float(extent.y), 1.0f / extent.x, 1.0f / extent.y);
            Check(params.screenSize == screen && params.regionInfo == glm::vec4(2, 128, .75f, 0) &&
                params.deferredProbeDebug == glm::vec4(1, 2.5f, 0, 0) && params.regions[0].volumeMin.w == 4 &&
                params.regions[0].traceParams.w == 3 && params.regions[1].volumeMin.w == 5 &&
                params.regions[1].traceParams.w == 4, "SSGI initial settings/atlas parameters not uploaded");
            for (auto& buffer : ssgi.temporalParams)
            {
                Check(buffer.GetBufferSize() == 64 && buffer.PersistentMap(), "SSGI temporal parameter ABI changed");
                buffer.InvalidateMappedRange(0, buffer.GetBufferSize());
                VansSSGIHistoryParameters params{};
                std::memcpy(&params, buffer.GetMappedPtr(), sizeof(params));
                Check(params.screenSize == screen && params.frameParams == glm::vec4(0) &&
                    params.lightingMinimum == glm::vec4(0) && params.lightingMaximum == glm::vec4(0),
                    "SSGI temporal parameters did not start cold");
            }
            // 图像内容在首次写入前未定义；冷历史参数保证不会误用它们，不为测试新增清零。
            ssgi.history.PrepareFrame(extent.x, extent.y, glm::vec2(0));
            ssgi.history.ResolveRecordedFrame(true);
            Check(ssgi.history.HasHistory(), "SSGI fixture did not prime submission history");
            ssgi.Destroy(resources.device, resources.textures);
            ssgi.Destroy(resources.device, resources.textures);
            for (const char* name : names) Check(!resources.textures.Has(name), "SSGI texture retained after release");
            for (auto& buffer : ssgi.temporalParams) Check(!buffer.GetNativeBuffer(), "SSGI temporal buffer retained");
            Check(!ssgi.params.GetNativeBuffer() && !ssgi.layout && !ssgi.probeCacheLayout && !ssgi.temporalLayout &&
                !ssgi.atrousLayout && ssgi.sets.empty() && ssgi.probeCacheSets.empty() && ssgi.temporalSets.empty() &&
                ssgi.atrousSets.empty() && !ssgi.shader && !ssgi.probeCacheShader && !ssgi.temporalShader && !ssgi.atrousShader &&
                !ssgi.history.HasHistory() && ssgi.history.WriteIndex() == 0, "SSGI resource/state retained after release");
            const auto after = device.QueryGpuMemorySnapshot();
            Check(after.allocationCount == before.allocationCount && after.allocationBytes == before.allocationBytes,
                "SSGI owner retained native allocations after resize/release");
        }
        std::cout << "[SSGIOwnerGPU] PASS: 11 textures, odd extents/resize, 14 descriptor sets, initial settings, cold history, repeated release and zero retained allocation\n";
    }
    void VerifyArrayLayerUpload(VansVKDevice& device)
    {
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        for (bool generateMips : {false, true})
        {
            VansTexture texture;
            texture.InitTextureArray(command, 4, 4, 3, VK_FORMAT_R8G8B8A8_UNORM, generateMips);
            auto& image = texture.GetImage();
            const uint32_t mipCount = generateMips ? 3 : 1;
            std::vector<std::array<uint8_t, 4>> colors{{20,40,60,255},{80,100,120,255},{140,160,180,255}};
            const auto upload = [&](int layer)
            {
                std::vector<uint8_t> pixels(4 * 4 * 4);
                for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = colors[layer][i % 4];
                Check(texture.UpdateArrayLayerFromPixels(command, pixels.data(), 4, 4, layer, Vans::VansColorEncoding::Linear, VK_FILTER_NEAREST), "Array layer upload failed");
            };
            for (int layer = 0; layer < 3; ++layer) upload(layer);
            struct Readback
            {
                VkDevice device; VansVKBuffer buffer;
                ~Readback(){buffer.DestroyVulkanBuffer(device);}
            } readback{device.GetLogicDevice()};
            const size_t bytes = 3 * mipCount * 4;
            Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(), bytes, VK_FORMAT_R8G8B8A8_UNORM,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                && readback.buffer.PersistentMap(), "Array readback allocation failed");
            for (int round = 0; round < 3; ++round)
            {
                if (round != 0)
                {
                    const int layer = round == 1 ? 1 : 0;
                    colors[layer] = {uint8_t(33 * round), uint8_t(45 * round), uint8_t(57 * round), 255};
                    upload(layer);
                }
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Array readback begin failed");
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image.GetImage(); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 3};
                command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {}, {}, {barrier});
                std::vector<VkBufferImageCopy> regions;
                for (uint32_t layer = 0; layer < 3; ++layer)
                    for (uint32_t mip = 0; mip < mipCount; ++mip)
                    {
                        VkBufferImageCopy copy{}; copy.bufferOffset = regions.size() * 4;
                        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1}; copy.imageExtent = {1,1,1};
                        regions.push_back(copy);
                    }
                vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(), image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    readback.buffer.GetNativeBuffer(), uint32_t(regions.size()), regions.data());
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {host}, {}, {barrier});
                Submit(device, command);
                readback.buffer.InvalidateMappedRange(0, bytes);
                const auto* data = static_cast<const uint8_t*>(readback.buffer.GetMappedPtr());
                for (uint32_t layer = 0; layer < 3; ++layer)
                    for (uint32_t mip = 0; mip < mipCount; ++mip)
                        Check(std::memcmp(data + (layer * mipCount + mip) * 4, colors[layer].data(), 4) == 0,
                            "Array upload changed another layer or failed to regenerate its mip chain");
            }
            std::cout << "[GIResourcesGPU] array layers=3 mips=" << mipCount << " updates=5 allLayersPreserved=1" << std::endl;
        }
    }
    std::vector<std::vector<uint8_t>> ReadRgba8Layer(VansVKDevice& device, VansTexture& texture, uint32_t layer)
    {
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        auto& image=texture.GetImage();const auto& info=image.GetImageCreateInfo();
        std::vector<VkBufferImageCopy> copies;std::vector<std::vector<uint8_t>> result;
        size_t bytes=0;
        for(uint32_t mip=0;mip<info.mipLevels;++mip)
        {
            const uint32_t width=std::max(1u,info.extent.width>>mip),height=std::max(1u,info.extent.height>>mip);
            VkBufferImageCopy copy{};copy.bufferOffset=bytes;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,layer,1};copy.imageExtent={width,height,1};
            copies.push_back(copy);result.emplace_back(size_t(width)*height*4);bytes+=result.back().size();
        }
        struct Readback{VkDevice device;VansVKBuffer buffer;~Readback(){buffer.DestroyVulkanBuffer(device);}} readback{device.GetLogicDevice()};
        Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(),bytes,VK_FORMAT_R8G8B8A8_UNORM,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&
            readback.buffer.PersistentMap(),"RGBA8 layer readback allocation failed");
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"RGBA8 layer readback begin failed");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image.GetImage();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,info.mipLevels,layer,1};
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{},{},{barrier});
        vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(),image.GetImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readback.buffer.GetNativeBuffer(),uint32_t(copies.size()),copies.data());
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,{host},{},{barrier});
        Submit(device,command);readback.buffer.InvalidateMappedRange(0,bytes);
        for(size_t mip=0;mip<result.size();++mip)
            std::memcpy(result[mip].data(),static_cast<const uint8_t*>(readback.buffer.GetMappedPtr())+copies[mip].bufferOffset,result[mip].size());
        return result;
    }

    void VerifyRectLightMidtone(VansVKDevice& device)
    {
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        std::vector<uint8_t> pixels(4*4*4,128);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
        VansTexture source;source.LoadFromMemory(command,pixels.data(),pixels.size(),4,4,VK_FORMAT_R8G8B8A8_SRGB,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        std::array<int,2> measured{};
        for(size_t i=0;i<2;++i)
        {
            const int size=i==0?4:2;
            VansTexture target;target.InitTextureArray(command,size,size,2,VK_FORMAT_R8G8B8A8_UNORM,true);
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Color transfer begin failed");
            Check(target.RecordArrayLayerCopyFromTexture(command,&source,0),"Color transfer recording failed");
            Submit(device,command);measured[i]=ReadRgba8Layer(device,target,0)[0][0];
        }
        std::cout<<"[GIColorGPU] srgb128-to-linear sameSize="<<measured[0]<<" scaled="<<measured[1]<<" expected=55\n";
        Check(std::abs(measured[0]-55)<=1 && std::abs(measured[1]-55)<=1,
            "Emitter energy changes with source dimensions: sRGB bytes were copied into linear storage");
    }

    void VerifyColorUploadPaths(VansVKDevice& device)
    {
        using Vans::VansColorEncoding;
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        device.ResetFrameStageUploadAllocator();
        int maximumDifference=0, cases=0;
        for(bool sourceSrgb:{false,true}) for(bool targetSrgb:{false,true})
        for(int pattern=0;pattern<3;++pattern) for(const auto size:{glm::ivec2(4,4),glm::ivec2(2,2),glm::ivec2(7,5),glm::ivec2(3,2)})
        {
            const auto encoding=sourceSrgb?VansColorEncoding::SRGB:VansColorEncoding::Linear;
            const auto targetEncoding=targetSrgb?VansColorEncoding::SRGB:VansColorEncoding::Linear;
            std::vector<uint8_t> pixels(4*4*4);
            for(int y=0;y<4;++y)for(int x=0;x<4;++x)
            {
                auto* p=pixels.data()+(y*4+x)*4;
                if(pattern==0){p[0]=128;p[1]=64;p[2]=192;p[3]=96;}
                else if(pattern==1){p[0]=p[1]=p[2]=((x+y)%2)?255:0;p[3]=((x+y)%2)?192:64;}
                else {p[0]=uint8_t(x*73+y*9);p[1]=uint8_t(y*61+x*3);p[2]=uint8_t((x+y)*37);p[3]=uint8_t(17+x*29+y*41);}
            }
            VansTexture source,target;
            source.LoadFromMemory(command,pixels.data(),pixels.size(),4,4,
                sourceSrgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            target.InitTextureArray(command,size.x,size.y,4,
                targetSrgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM,true);
            const std::array<uint8_t,4> sentinel{31,73,149,213};
            Check(target.UpdateArrayLayerFromPixels(command,sentinel.data(),1,1,3,targetEncoding,VK_FILTER_NEAREST),"Color sentinel upload failed");
            Check(target.UpdateArrayLayerFromPixels(command,pixels.data(),4,4,0,encoding,VK_FILTER_LINEAR),"Immediate color upload failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Color paths begin failed");
            Check(target.RecordArrayLayerUploadFromPixels(command,pixels.data(),4,4,1,encoding,VK_FILTER_LINEAR),"Recorded color upload failed");
            Check(target.RecordArrayLayerCopyFromTexture(command,&source,2),"GPU color upload failed");
            Submit(device,command);
            const auto immediate=ReadRgba8Layer(device,target,0),recorded=ReadRgba8Layer(device,target,1),gpu=ReadRgba8Layer(device,target,2);
            Check(immediate==recorded,"Static and recorded CPU upload produced different pixels");
            for(size_t mip=0;mip<immediate.size();++mip)for(size_t i=0;i<immediate[mip].size();++i)
            {
                const int difference=std::abs(int(immediate[mip][i])-int(gpu[mip][i]));
                maximumDifference=std::max(maximumDifference,difference);
                Check(difference<=2,"CPU/GPU linear filtering diverged: pattern="+std::to_string(pattern)+
                    " size="+std::to_string(size.x)+"x"+std::to_string(size.y)+" mip="+std::to_string(mip)+
                    " channel="+std::to_string(i%4)+" CPU="+std::to_string(immediate[mip][i])+" GPU="+std::to_string(gpu[mip][i]));
                if(pattern==0)
                {
                    // 已知传递函数参考值，防止三条路径一致地丢失或重复执行 sRGB 转换。
                    const std::array<int,4> expected=sourceSrgb==targetSrgb?std::array<int,4>{128,64,192,96}:
                        sourceSrgb?std::array<int,4>{55,13,134,96}:std::array<int,4>{188,137,225,96};
                    Check(std::abs(int(gpu[mip][i])-expected[i%4])<=1,"Constant color/alpha energy changed through conversion or mip chain");
                }
                if(pattern==1 && size==glm::ivec2(2,2))
                {
                    const int expected=i%4==3?128:targetSrgb?188:128;
                    Check(std::abs(int(gpu[mip][i])-expected)<=1,"Checkerboard was filtered in encoded space or alpha was gamma corrected");
                }
            }
            for(const auto& mip:ReadRgba8Layer(device,target,3))for(size_t i=0;i<mip.size();++i)
                Check(std::abs(int(mip[i])-int(sentinel[i%4]))<=1,"Color conversion damaged an unrelated layer");
            ++cases;
        }

        // 皮肤 LUT 为线性数据，保留全部 256 个字节值及既有最近邻取样位置。
        std::vector<uint8_t> lut(16*16*4);for(size_t i=0;i<lut.size();++i)lut[i]=uint8_t(i/4);
        for(int size:{16,7})
        {
            VansTexture target;target.InitTextureArray(command,size,size,2,VK_FORMAT_R8G8B8A8_UNORM,false);
            Check(target.UpdateArrayLayerFromPixels(command,lut.data(),16,16,0,VansColorEncoding::Linear,VK_FILTER_NEAREST),"Linear LUT immediate upload failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Linear LUT begin failed");
            Check(target.RecordArrayLayerUploadFromPixels(command,lut.data(),16,16,1,VansColorEncoding::Linear,VK_FILTER_NEAREST),"Linear LUT recorded upload failed");
            Submit(device,command);
            const auto pixels=ReadRgba8Layer(device,target,0)[0];
            Check(pixels==ReadRgba8Layer(device,target,1)[0],"Linear LUT paths differ");
            for(int y=0;y<size;++y)for(int x=0;x<size;++x)for(int c=0;c<4;++c)
                Check(pixels[(y*size+x)*4+c]==uint8_t(int(y*(16.0f/size))*16+int(x*(16.0f/size))),"Linear LUT bytes or nearest samples changed");
        }
        std::cout<<"[GIColorGPU] colorCases="<<cases<<" everyPixelEveryMip=1 CPURecordedExact=1 maximumCpuGpuByteDifference="<<maximumDifference<<" linearLUTExact=1\n";
    }

    void VerifyRetainedColorEncoding(VansVKDevice& device)
    {
        using Vans::VansColorEncoding;
        namespace fs=std::filesystem;
        struct Fixture
        {
            fs::path path=fs::temp_directory_path()/("forest-gi-color-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ~Fixture(){std::error_code error;fs::remove_all(path,error);}
        } fixture;
        fs::create_directories(fixture.path);
        const auto sourcePath=fixture.path/"color.tga";
        {
            std::ofstream file(sourcePath,std::ios::binary);
            std::array<uint8_t,18> header{};header[2]=2;header[12]=2;header[14]=2;header[16]=32;header[17]=0x28;
            file.write(reinterpret_cast<const char*>(header.data()),header.size());
            const std::array<uint8_t,4> bgra{192,64,128,96};
            for(int i=0;i<4;++i)file.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());
            Check(bool(file),"Retained color fixture write failed");
        }
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        for(bool srgb:{false,true})
        {
            Vans::VansAssetMeta meta;
            Check(Vans::VansAssetGuid::TryParse(srgb?"04d8658b193648f58a723c9cae85a4e1":"105c79f35ce3475b99f218b4c1e5a7d2",meta.guid),"Color fixture GUID failed");
            meta.importer="TextureImporter";
            meta.SetSerializedSettings(Vans::VansSerializedValue::Object({
                {"colorSpace",Vans::VansSerializedValue::String(srgb?"srgb":"linear")},
                {"sRGB",Vans::VansSerializedValue::Bool(srgb)},
                {"useCompress",Vans::VansSerializedValue::Bool(false)},
                {"needMip",Vans::VansSerializedValue::Bool(true)},
                {"importChannel",Vans::VansSerializedValue::Int(4)}}));
            const auto metaPath=Vans::VansAssetMeta::MetaPathFor(sourcePath);std::string error;
            Check(Vans::VansAssetMetaStorage::SaveAtomic(metaPath,meta,error),"Color fixture metadata save failed");
            const auto cooked=Vans::VansTextureCooker::CookIfNeeded(sourcePath,metaPath,meta,fixture.path/"Artifacts");
            Check(cooked.status==Vans::VansTextureCookStatus::Cooked,"Color fixture cook failed: "+cooked.error);
            for(int path=0;path<3;++path)
            {
                VansTexture::TextureLoadDesc desc;desc.path=sourcePath.string();desc.isSRGB=srgb;
                desc.retainRgba8Pixels=true;desc.needMip=true;
                if(path!=0){desc.cookedPath=cooked.artifactPath.string();desc.path=(fixture.path/"source-must-not-be-read.tga").string();}
                VansTexture source;
                if(path!=2)source.LoadTexture(command,desc);
                else
                {
                    VansTextureMipChainUpload upload;std::vector<uint8_t> storage;
                    Check(source.TryPrepareCookedBatchUpload(device,desc,upload,storage),"Color batch preparation failed");
                    Check(device.SubmitTextureMipChainUploadBatch(command,{upload}),"Color batch submission failed");
                }
                const auto& retained=source.GetRetainedRgba8Image();
                Check(retained.IsValid()&&retained.encoding==(srgb?VansColorEncoding::SRGB:VansColorEncoding::Linear),"Retained pixels lost source color encoding");
                Check(source.GetImage().GetImageCreateInfo().format==(srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM),"Retained pixels disagree with source GPU format");
                VansTexture target;target.InitTextureArray(command,2,2,2,VK_FORMAT_R8G8B8A8_SRGB,true);
                Check(target.UpdateArrayLayerFromPixels(command,retained.pixels.data(),retained.width,retained.height,0,retained.encoding,VK_FILTER_LINEAR),"Retained emissive upload failed");
                const std::array<int,4> expected=srgb?std::array<int,4>{128,64,192,96}:std::array<int,4>{188,137,225,96};
                for(const auto& mip:ReadRgba8Layer(device,target,0))for(size_t i=0;i<mip.size();++i)
                    Check(std::abs(int(mip[i])-expected[i%4])<=1,"Raw/cooked/batched retained emission changed energy");
            }
            // Skin LUT 的文件后备路径也必须保留线性数据。
            VansTexture lut;lut.InitTextureArray(command,2,2,2,VK_FORMAT_R8G8B8A8_UNORM,false);
            Check(lut.LoadTextureLayer(command,sourcePath.string(),0,false),"Linear LUT file upload failed");
            const std::array<int,4> expected{128,64,192,96};
            const auto pixels=ReadRgba8Layer(device,lut,0)[0];
            for(size_t i=0;i<pixels.size();++i)Check(pixels[i]==expected[i%4],"Linear LUT file fallback changed bytes");
        }
        std::cout<<"[GIColorGPU] raw/cooked/batched source metadata and retained emission PASS; linear LUT file fallback exact\n";
    }


    void VerifyVideoLightingPublication(VansVKDevice& device)
    {
        // 极小 YUV4MPEG2 原始帧夹具通过真实 MediaCore/FFmpeg 解码，无外部编码工具依赖。
        struct Clip
        {
            std::filesystem::path path=std::filesystem::temp_directory_path()/
                ("ForestGIVideo_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".y4m");
            ~Clip(){std::error_code error;std::filesystem::remove(path,error);}
        } clip;
        {
            std::ofstream file(clip.path,std::ios::binary);
            file<<"YUV4MPEG2 W16 H16 F1:1 Ip A1:1 C444\n";
            for(uint8_t gray:{uint8_t(16),uint8_t(16),uint8_t(235),uint8_t(16),uint8_t(235),uint8_t(235)})
            {
                file<<"FRAME\n";
                std::array<uint8_t,256> plane;plane.fill(gray);
                file.write(reinterpret_cast<const char*>(plane.data()),plane.size());
                plane.fill(128);
                file.write(reinterpret_cast<const char*>(plane.data()),plane.size());
                file.write(reinterpret_cast<const char*>(plane.data()),plane.size());
            }
            Check(bool(file),"Video fixture write failed");
        }
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        VansVideoManager videos;
        Vans::VansSceneVideoResourceRequest request;
        request.name="gi-content";request.assetGuid="gi-content";request.path=clip.path.string();
        request.loop=false;request.autoplay=true;request.srgb=true;
        videos.Load({request},&device);
        auto* video=videos.Get("gi-content");Check(video && video->IsReady(),"Real video fixture decode failed");
        const auto runtimeIndex=videos.FindRuntimeIndex(video);
        VansTexture target;target.InitTextureArray(command,16,16,3,VK_FORMAT_R8G8B8A8_SRGB,true);
        std::vector<uint8_t> untouched(16*16*4,128);
        for(size_t i=3;i<untouched.size();i+=4)untouched[i]=255;
        for(int layer=0;layer<3;++layer)
            Check(target.UpdateArrayLayerFromPixels(command,untouched.data(),16,16,layer,Vans::VansColorEncoding::SRGB,VK_FILTER_LINEAR),"Video target initialization failed");
        const std::vector<VansRenderRectLightVideoFrameData> bindings{{runtimeIndex,0},{runtimeIndex,2}};
        const auto select=[&](double delta)
        {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            bool selected=video->Tick(delta);
            while(!selected && std::chrono::steady_clock::now()<deadline)
            {std::this_thread::sleep_for(std::chrono::milliseconds(1));selected=video->Tick(0);}
            Check(selected,"Video fixture did not publish the requested decoded frame");
        };
        const auto record=[&](float expected)
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Video command begin failed");
            const auto& uploads=videos.RecordPendingUploads(command,0,&target,bindings);
            Check(uploads.size()==1 && uploads[0].result.arrayLayers==5 &&
                std::abs(uploads[0].result.radianceChange-expected)<1e-6f,
                "Shared video upload lost a target layer or deterministic source change");
            VansFrameSubmitNode writer,reader;
            videos.AppendFrameResources(writer,&target,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,true);
            videos.AppendFrameResources(reader,&target,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,false);
            Check(writer.resources.size()==2 && reader.resources.size()==2 &&
                writer.resources[0].write && writer.resources[1].write &&
                !reader.resources[0].write && !reader.resources[1].write &&
                writer.resources[0].physical==reader.resources[0].physical,
                "Video frame graph omitted a published physical image or declared a reader as writer");
        };
        const auto cancel=[&]
        {Check(command.EndCommandBufferRecord() && command.ResetCommandBuffer(false),"Video cancelled command reset failed");videos.ResolveRecordedUploads(false);};
        const auto submit=[&]{Submit(device,command);videos.ResolveRecordedUploads(true);};
        const uint32_t mipCount=target.GetImage().GetImageCreateInfo().mipLevels;
        struct Readback{VkDevice device;VansVKBuffer buffer;~Readback(){buffer.DestroyVulkanBuffer(device);}} readback{device.GetLogicDevice()};
        const size_t bytes=3*mipCount*4;
        Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(),bytes,VK_FORMAT_R8G8B8A8_UNORM,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
            readback.buffer.PersistentMap(),"Video readback allocation failed");
        const auto verify=[&](bool white)
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Video readback begin failed");
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.image=target.GetImage().GetImage();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,mipCount,0,3};
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{},{},{barrier});
            std::vector<VkBufferImageCopy> copies;
            for(uint32_t layer=0;layer<3;++layer)for(uint32_t mip=0;mip<mipCount;++mip)
            {VkBufferImageCopy copy{};copy.bufferOffset=copies.size()*4;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,layer,1};copy.imageExtent={1,1,1};copies.push_back(copy);}
            vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(),barrier.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                readback.buffer.GetNativeBuffer(),uint32_t(copies.size()),copies.data());
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,{host},{},{barrier});
            Submit(device,command);readback.buffer.InvalidateMappedRange(0,bytes);
            const auto* data=static_cast<const uint8_t*>(readback.buffer.GetMappedPtr());
            for(uint32_t layer=0;layer<3;++layer)for(uint32_t mip=0;mip<mipCount;++mip)for(uint32_t c=0;c<4;++c)
            {
                const int expected=c==3?255:layer==1?128:white?255:0;
                Check(std::abs(int(data[(layer*mipCount+mip)*4+c])-expected)<=1,
                    "Video publication lost a shared layer, preserved the cancelled frame, or damaged another layer/mip");
            }
        };
        select(0);record(1);cancel();record(1);submit();verify(false);
        Check(videos.RecordPendingUploads(command,0,&target,bindings).empty(),"Accepted video frame was replayed");
        select(1);record(0);submit();verify(false);
        select(1);record(1);
        // Main 在 RT 解决旧录制结果前推进；取消后必须上传最新黑帧。
        select(1);cancel();record(1);submit();verify(false);
        select(1);
        auto invalid=bindings;invalid[1].rectLightLayer=0;
        bool rejected=false;try{videos.RecordPendingUploads(command,0,&target,invalid);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"Conflicting video layer bindings were accepted");
        record(1);submit();verify(true);
        select(1);record(0);submit();verify(true);
        videos.Clear();
        std::cout<<"[GIResourcesGPU] real video decode, shared layers, every mip, no-change response, cancelled retry and newer Main publication PASS\n";
    }

    void VerifyIESArrayResources(VansVKDevice& device)
    {
        const std::string profile = "IESNA:LM-63-2002\nTILT=NONE\n1 1000 1 2 1 1 2 0 0 0 1 1 10\n0 180\n0\n100 100\n";
        auto& logic = device.GetLogicDevice();
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        for (int count : {0, 1, 2})
        {
            // 调用真实 Vulkan 创建函数并记录 view 类型，覆盖占位、单层和多层路径。
            LateAllocationFailure observeOnly(UINT32_MAX);
            VansIESProfileManager ies;
            for (int i = 0; i < count; ++i)
            {
                int index = -1;
                Check(ies.LoadIESFromMemory("test-profile-" + std::to_string(i),
                    profile.data(), profile.size(), index) && index == i,
                    "IES profile fixture parse failed");
            }
            ies.CreateGPUResources(logic);
            const bool ready = ies.IsGPUResourcesCreated();
            const bool array = arrayViews.count(ies.GetIESProfileArrayView()) == 1;
            const bool layers = ies.GetIESProfileTexture().GetImageCreateInfo().arrayLayers == uint32_t(std::max(1, count));
            const auto firstView = ies.GetIESProfileArrayView();
            ies.CreateGPUResources(logic);
            const bool reused = firstView == ies.GetIESProfileArrayView();
            ies.UploadAllProfiles(&device, command);
            const bool readable = ies.GetIESProfileTexture().GetImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ies.DestroyGPUResources(logic);
            ies.DestroyGPUResources(logic);
            const bool released = !ies.IsGPUResourcesCreated() && ies.GetIESProfileArrayView() == VK_NULL_HANDLE
                && candidateViews.empty() && arrayViews.empty();
            Check(ready && array && layers && reused && readable && released,
                "IES array view/upload/resource lifecycle failed for profile count " + std::to_string(count));
            std::cout << "[GIResourcesGPU] IES profiles=" << count << " arrayView=1 readable=1 viewsReleased=1" << std::endl;
        }
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
    void VerifySelectedPreview(VansVKDevice& device, VansScene& scene, const glm::vec3& irradiance)
    {
        auto& gi = device.GetRayTracingContext();
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        auto& image = gi.GetGIRTPreviewTexture(7)->GetImage();
        const auto extent = image.GetImageCreateInfo().extent;
        const VkDeviceSize bytes = VkDeviceSize(extent.width) * extent.height * sizeof(glm::vec4);
        struct Readback
        {
            VkDevice device; VansVKBuffer buffer;
            ~Readback() { buffer.DestroyVulkanBuffer(device); }
        } readback{device.GetLogicDevice()};
        Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(), bytes, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
            readback.buffer.PersistentMap(), "Preview readback allocation failed");
        gi.RequestGIRTPreviews(0, 0, 1.0f, 1.0f);
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Preview begin failed");
        // 空工作表只发布真实区域寻址；执行实际预览 pass，不伪造命中或探针更新。
        gi.DispatchRayTracing(&device, &command, &scene);
        gi.UpdateGIProbe(&device, &command, scene.GetMaterialManager());
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {barrier});
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = extent;
        vkCmdCopyImageToBuffer(command.GetVKCommandBuffer(), image.GetImage(), VK_IMAGE_LAYOUT_GENERAL,
            readback.buffer.GetNativeBuffer(), 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, {barrier});
        Submit(device, command);
        readback.buffer.InvalidateMappedRange(0, bytes);
        const auto* pixels = static_cast<const glm::vec4*>(readback.buffer.GetMappedPtr());
        const glm::vec4 expected(irradiance / (glm::vec3(1.0f) + irradiance), 1.0f);
        for (size_t i = 0; i < size_t(extent.width) * extent.height; ++i)
            Check(glm::all(glm::lessThan(glm::abs(pixels[i] - expected), glm::vec4(.0001f))),
                "Actual GI preview sampled a different region or failed to consume its request");
    }

    void VerifyCompletedFrameWork(VansVKDevice& device,VansScene& scene,VansGISettings settings)
    {
        // 两个地址都位于真实 PCG 木质体素内：合法完整射线预算下，布局排除它们。
        // 零射线批次仍执行真实上传、提交、fence 退役和 API 发布。
        settings.placement.maxRaysPerFrame=16;
        settings.regions.resize(2);
        for(uint32_t index=0;index<2;++index)
        {
            auto& region=settings.regions[index];
            region.mode=GIProbeRegionMode::RegularGrid;region.worldOnly=true;region.followView=false;
            region.overrideGridDimensions=true;region.gridDimensions={1,1,1};region.probeSpacing=.25f;region.raysPerProbe=16;
            region.center={.125f+40.f*index,.125f,.125f};
        }
        NormalizeGISettings(settings);
        auto& gi=device.GetRayTracingContext();auto& command=device.GetImmediateGraphicsCommandBuffer();
        Check(gi.CreateRayTracingResource(&device,&command, &scene, settings, scene.GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
        for(const auto& region:settings.regions)
            Check(!gi.GetGIWorld()->IsPositionValid(region.center,0),"Zero-work fixture probe is outside its opaque source");
        Vans::EditorAPI::EngineAPIImpl api(&scene,&device),withoutDevice(&scene,nullptr);
        struct UnrelatedFence
        {
            VkDevice device;VkFence fence{};
            ~UnrelatedFence(){if(fence)vkDestroyFence(device,fence,nullptr);}
        } unrelated{device.GetLogicDevice()};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        Check(vkCreateFence(unrelated.device,&fenceInfo,nullptr,&unrelated.fence)==VK_SUCCESS,"Unrelated work fence creation failed");
        Check(!api.GetGIFrameWork().probes.available && !withoutDevice.GetGIFrameWork().probes.available,"New GI owner fabricated completed work");
        uint64_t uploaded=0;
        VansGIProbeFrameWork completed;
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Work publication begin failed");
            GICostCapture capture(device,command);
            gi.PrepareGIProbeUpdate(VansRenderLightFrameData{},scene.GetMaterialManager()->m_SSGI.history,command.m_CommandBufferFinishSubmitFence,{});
            {
                BufferUpdateObserver observer(command.GetVKCommandBuffer());
                gi.DispatchRayTracing(&device,&command,&scene);
                gi.UpdateGIProbe(&device,&command,scene.GetMaterialManager());
                uploaded=observer.bytes;
            }
            Check(!api.GetGIFrameWork().probes.available,"Recorded but unsubmitted work was published");
            gi.CompleteGIProbeUpdate(VK_NULL_HANDLE);
            gi.CompleteGIProbeUpdate(unrelated.fence);
            Check(!api.GetGIFrameWork().probes.available,"Unrelated fence published work");
            Submit(device,command);
            Check(!api.GetGIFrameWork().probes.available,"Submission bypassed explicit retirement");
            gi.CompleteGIProbeUpdate(command.m_CommandBufferFinishSubmitFence);
            completed=api.GetGIFrameWork().probes;
            Check(completed.available && completed.regionCount==2 && completed.worldRecorded &&
                completed.probeUploadBytes+completed.world.UploadBytes()==uploaded && uploaded>0 &&
                completed.feedbackReadbackBytes==0 && completed.diagnosticReadbackBytes==0 &&
                completed.world.bufferCapacityBytes==device.GetGIMemorySnapshot().probes.world.bufferCapacityBytes,"Retired upload accounting or API ownership differs");
            for(uint32_t index=0;index<completed.regionCount;++index)
            {
                const auto& region=completed.regions[index];
                Check(region.stableId==gi.GetGIRegionResolved(index)->stableId && region.physicalProbes==gi.GetGIRegionPhysicalProbeCount(index) &&
                    region.probeUpdates==0 && region.primaryRays==0 && region.hardwareRays==0 && region.worldRays==0 && region.rejectedFeedback==0,
                    "Zero-ray batch lost regional identity or fabricated work");
            }
            capture.CompleteAfterSubmission(1);
            Check(api.GetGIFrameCost().frameIndex==completed.profilerFrameIndex,"Work and native timing lost the shared profiler frame identity");
        }
        gi.CompleteGIProbeUpdate(command.m_CommandBufferFinishSubmitFence);
        Check(api.GetGIFrameWork().probes.probeUploadBytes==completed.probeUploadBytes,"Repeated retirement accumulated work twice");
        {LateAllocationFailure failure(1);Check(!gi.CreateRayTracingResource(&device,&command, &scene, settings, scene.GetMaterialManager()->GetGIVoxelMaterialInputs()),"Work fixture failure injection missed");}
        Check(api.GetGIFrameWork().probes.available && api.GetGIFrameWork().probes.profilerFrameIndex==completed.profilerFrameIndex,
            "Failed resource candidate erased completed work from the retained owner");
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Cancelled work begin failed");
            GICostCapture capture(device,command);
            gi.PrepareGIProbeUpdate(VansRenderLightFrameData{},scene.GetMaterialManager()->m_SSGI.history,command.m_CommandBufferFinishSubmitFence,{});
            gi.DispatchRayTracing(&device,&command,&scene);gi.UpdateGIProbe(&device,&command,scene.GetMaterialManager());
            Check(command.EndCommandBufferRecord() && command.ResetCommandBuffer(false),"Cancelled work reset failed");
            gi.DiscardGIProbeUpdate();gi.CompleteGIProbeUpdate(command.m_CommandBufferFinishSubmitFence);
            Check(api.GetGIFrameWork().probes.available && api.GetGIFrameWork().probes.profilerFrameIndex==completed.profilerFrameIndex,
                "Cancelled work replaced the last completed batch");
        }
        Check(gi.CreateRayTracingResource(&device,&command, &scene, settings, scene.GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
        Check(!api.GetGIFrameWork().probes.available,"Replacement resource owner inherited stale completed work");
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Unrecorded batch begin failed");
        gi.PrepareGIProbeUpdate(VansRenderLightFrameData{},scene.GetMaterialManager()->m_SSGI.history,command.m_CommandBufferFinishSubmitFence,{});
        Submit(device,command);gi.CompleteGIProbeUpdate(command.m_CommandBufferFinishSubmitFence);
        Check(!api.GetGIFrameWork().probes.available,"Prepared but unrecorded batch published work");
        gi.CleanupSceneResources(device.GetLogicDevice());
        Check(!api.GetGIFrameWork().probes.available,"Cleanup retained completed work");
        std::cout<<"[GIFrameWorkGPU] opaque-excluded zero-ray bytes="<<uploaded
            <<" snapshotBytes="<<sizeof(completed)<<" native upload / fence / API / timing identity / cancellation / failed candidate / replacement / omitted recording PASS\n";
    }

    struct ConsumerMemoryFixture
    {
        VansVKDevice& device;
        VansRuntimeRenderTextureManager textures;
        VansVKSSGI ssgi;
        VansVKAmbientSkyCache sky;
        VansVKGIReceiverVisibility receiver;
        uint64_t ssgiBytes = 0, skyBytes = 0, receiverBytes = 0;

        explicit ConsumerMemoryFixture(VansVKDevice& device) : device(device)
        {
            auto& command = device.GetImmediateGraphicsCommandBuffer();
            const auto initial = device.GetGIMemorySnapshot();
            Check(initial.ssgi.AllocationBytes() == 0 && initial.ambientSky.AllocationBytes() == 0 &&
                initial.receiver.AllocationBytes() == 0, "Memory fixture overlaps active consumer owners");
            try
            {
                auto before = device.QueryGpuMemorySnapshot().allocationBytes;
                ssgi.Initialize(device, command, textures, VansGISettings{}, 17, 13);
                auto after = device.QueryGpuMemorySnapshot().allocationBytes;
                ssgiBytes = after - before;
                auto memory = device.GetGIMemorySnapshot();
                Check(memory.ssgi.AllocationBytes() == ssgiBytes && memory.ssgi.bufferCapacityBytes ==
                    sizeof(VansSSGIParameters) + sizeof(VansSSGIHistoryParameters) * ssgi.temporalParams.size() &&
                    memory.ssgi.imageAllocationBytes > 0, "SSGI snapshot differs from actual VMA allocation growth");
                before = after;
                GIAmbientSkyCacheSettings settings;
                settings.enabled = true;
                sky.Initialize(device, command, textures, settings, glm::vec3(0), true);
                after = device.QueryGpuMemorySnapshot().allocationBytes;
                skyBytes = after - before;
                memory = device.GetGIMemorySnapshot();
                Check(memory.ambientSky.AllocationBytes() == skyBytes && memory.ambientSky.bufferCapacityBytes == 192 &&
                    memory.ambientSky.imageAllocationBytes > 0, "Sky snapshot differs from actual VMA allocation growth");
                before = after;
                receiver.Initialize(device, command, 5, 4);
                receiverBytes = device.QueryGpuMemorySnapshot().allocationBytes - before;
                memory = device.GetGIMemorySnapshot();
                Check(memory.receiver.AllocationBytes() == receiverBytes && memory.receiver.imageAllocationBytes == 0 &&
                    memory.receiver.bufferAllocationBytes >= memory.receiver.bufferCapacityBytes,
                    "Receiver snapshot differs from actual VMA allocation growth");
                Verify();
                std::cout << "[GIConsumerMemoryGPU] VMA receiver=" << receiverBytes << " ssgi=" << ssgiBytes
                    << " sky=" << skyBytes << " combined=" << memory.ActiveAllocationBytes() << " PASS\n";
            }
            catch (...) { Release(); throw; }
        }
        ~ConsumerMemoryFixture() { Release(); }
        void Verify()
        {
            const auto memory = device.GetGIMemorySnapshot();
            Vans::EditorAPI::EngineAPIImpl api(nullptr, &device);
            const auto exposed = api.GetGIMemorySnapshot();
            Check(memory.receiver.AllocationBytes() == receiverBytes && memory.ssgi.AllocationBytes() == ssgiBytes &&
                memory.ambientSky.AllocationBytes() == skyBytes &&
                memory.ActiveAllocationBytes() == memory.probes.ActiveAllocationBytes() + receiverBytes + ssgiBytes + skyBytes &&
                memory.publishedRetainedPeakBytes >= memory.ActiveAllocationBytes() + memory.retiredAllocationBytes &&
                exposed.ActiveAllocationBytes() == memory.ActiveAllocationBytes() && exposed.Available(),
                "Probe publication/retirement lost consumer memory, summed the wrong scope or broke API publication");
        }
        void Release()
        {
            receiver.Destroy(device.GetLogicDevice());
            sky.Destroy(device.GetLogicDevice(), textures);
            ssgi.Destroy(device.GetLogicDevice(), textures);
        }
    };

    void VerifyMemoryPublication(VansVKDevice& device, VansScene& scene)
    {
        auto& gi = device.GetRayTracingContext();
        const auto memory = device.GetGIMemorySnapshot();
        Vans::EditorAPI::EngineAPIImpl api(&scene, &device), missingDevice(&scene, nullptr);
        const auto exposed = api.GetGIMemorySnapshot();
        Check(memory.probes.available && memory.probes.regionCount == gi.GetGIRegionCount() &&
            exposed.ActiveAllocationBytes() == memory.ActiveAllocationBytes() &&
            exposed.retiredAllocationBytes == memory.retiredAllocationBytes &&
            !missingDevice.GetGIMemorySnapshot().Available(), "GI memory API lost native ownership");
        Check(memory.probes.shared.bufferCapacityBytes >= gi.GetGIProbeLayoutBuffer().GetBufferSize() &&
            memory.probes.shared.bufferAllocationBytes >= memory.probes.shared.bufferCapacityBytes,
            "GI shared memory omitted layout or allocation alignment");
        uint64_t previewBytes = 0;
        for (uint32_t i = 0; i < GIRTPreviewModeCount; ++i)
            if (auto* texture = gi.GetGIRTPreviewTexture(i)) previewBytes += texture->GetImage().GetAllocationSize();
        Check(memory.probes.previews.AllocationBytes() == previewBytes, "GI preview memory differs from actual images");
        for (uint32_t i = 0; i < memory.probes.regionCount; ++i)
        {
            const auto& region = memory.probes.regions[i];
            const auto* resolved = gi.GetGIRegionResolved(i);
            Check(exposed.probes.regions[i].storageDimensions == region.storageDimensions &&
                region.storageDimensions.x > 0 && region.storageDimensions.y > 0 && region.storageDimensions.z > 0 &&
                uint64_t(region.storageDimensions.x) * region.storageDimensions.y * region.storageDimensions.z >= region.physicalProbes,
                "Published probe storage dimensions are missing or cannot address all physical probes");
            if (!IsAdaptiveGIRegion(*resolved))
                Check(region.storageDimensions == resolved->gridDimensions, "Uniform probe storage differs from its authored grid");
            if (region.stableId == GetSelectedGIRegionDesc(gi.GetAppliedSettings()).stableId)
            {
                auto* preview = gi.GetGIRTPreviewTexture(0);
                Check(preview && preview->GetImage().GetImageCreateInfo().extent.width == region.storageDimensions.x &&
                    preview->GetImage().GetImageCreateInfo().extent.height == region.storageDimensions.y,
                    "Published preview dimensions differ from the actual selected region texture");
            }
            const auto* history = gi.GetGIRegionIrradianceHistoryBuffer(i);
            const uint64_t minimumBuffers = gi.GetGIRegionProbeStateBuffer(i)->GetBufferSize() +
                gi.GetGIRegionPreviousProbeStateBuffer(i)->GetBufferSize() + history->GetBufferSize();
            Check(region.stableId == resolved->stableId && region.physicalProbes == gi.GetGIRegionPhysicalProbeCount(i) &&
                region.rayCapacity > 0 && region.storage.bufferCapacityBytes >= minimumBuffers &&
                region.storage.bufferAllocationBytes >= region.storage.bufferCapacityBytes &&
                region.storage.imageAllocationBytes >= gi.GetGIRegionIrradianceAtlas(i)->GetImage().GetAllocationSize() +
                    gi.GetGIRegionVisibilityAtlas(i)->GetImage().GetAllocationSize(),
                "GI region memory omitted physical slots, state, history or atlases");
        }
        if (auto* world = gi.GetGIWorld())
            Check(memory.probes.world.bufferCapacityBytes == world->BufferCapacityBytes() &&
                memory.probes.world.bufferAllocationBytes == world->GetMemoryUsage().bufferAllocationBytes,
                "GI memory did not include the current world owner");
        std::cout << "[GIMemoryGPU] native/API activeBytes=" << memory.ActiveAllocationBytes()
            << " retiredBytes=" << memory.retiredAllocationBytes << " regions=" << memory.probes.regionCount
            << " adaptive=" << gi.UsesSparseGI() << " PASS\n";
    }

    void VerifyScrollingPublication(VansVKDevice& device, VansScene& scene, VansGISettings settings)
    {
        settings.regions.resize(2);
        settings.regions[1]=settings.regions[0];settings.regions[1].stableId=2;
        settings.regions[1].mode=GIProbeRegionMode::RegularGrid;
        settings.regions[1].worldOnly=true;settings.regions[1].followView=true;
        settings.regions[1].gridDimensions={9,10,11};settings.regions[1].probeSpacing=1;settings.regions[1].center={0,0,0};
        auto& gi=device.GetRayTracingContext();auto& command=device.GetImmediateGraphicsCommandBuffer();
        Check(gi.CreateRayTracingResource(&device,&command, &scene, settings, scene.GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
        VerifyMemoryPublication(device, scene);
        const auto* history=gi.GetGIRegionIrradianceHistoryBuffer(1);
        Check(history&&history->GetNativeBuffer()!=VK_NULL_HANDLE&&
            history->GetBufferSize()==uint64_t(9*10*11)*sizeof(VansGIProbeIrradianceHistory),"World component history has the wrong capacity");
        Check(gi.GetGIRegionResolved(0)->worldOnly&&gi.GetGIRegionIrradianceHistoryBuffer(0)->GetNativeBuffer()!=VK_NULL_HANDLE,
            "Geometry-free scene lost its existing world-query routing");
        const auto historyHandle=history->GetNativeBuffer();
        {
            struct HistoryReadback
            {
                VkDevice device;VansVKBuffer buffer;
                ~HistoryReadback(){buffer.DestroyVulkanBuffer(device);}
            } readback{device.GetLogicDevice()};
            Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(),history->GetBufferSize(),VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.buffer.PersistentMap(),"Component history readback allocation failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Component history readback begin failed");
            VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{ready});
            command.CopyBuffer(historyHandle,readback.buffer.GetNativeBuffer(),0,0,history->GetBufferSize());
            Submit(device,command);readback.buffer.InvalidateMappedRange(0,history->GetBufferSize());
            const auto* words=static_cast<const uint32_t*>(readback.buffer.GetMappedPtr());
            for(size_t i=0;i<history->GetBufferSize()/4;++i)Check(words[i]==0,"New component history was not initialized");
        }
        auto candidate=settings;candidate.regions[1].gridDimensions.x+=1;
        {
            LateAllocationFailure failure(2u);
            Check(!gi.CreateRayTracingResource(&device,&command, &scene, candidate, scene.GetMaterialManager()->GetGIVoxelMaterialInputs()),"World candidate failure was not reported");
        }
        Check(createdViews>=6&&injectedFailures==1&&layoutCalls==2&&
            gi.GetGIRegionIrradianceHistoryBuffer(1)->GetNativeBuffer()==historyHandle&&candidateViews.empty(),
            "Failed world candidate replaced component history or leaked candidate views");
        std::cout<<"[GIResourcesGPU] world-only component capacity, zero initialization and failed-candidate retention PASS\n";
        const auto fixed=*gi.GetGIRegionResolved(0);
        const auto* current=gi.GetGIRegionProbeStateBuffer(1);const auto* previous=gi.GetGIRegionPreviousProbeStateBuffer(1);
        const auto& layout=gi.GetGIProbeLayoutBuffer();
        const auto atlas=gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage();
        const auto bytes=device.GetGIMemorySnapshot().probes.world.bufferCapacityBytes;
        auto selection=settings; selection.selectedRegionIndex=1;
        gi.UpdateGISettings(&device,&command,selection);
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Mixed preview marker begin failed");
        VkClearColorValue previewColor{};
        previewColor.float32[0]=.25f;previewColor.float32[1]=.5f;previewColor.float32[2]=.75f;previewColor.float32[3]=1.0f;
        command.ClearColorImage(gi.GetGIRegionIrradianceAtlas(1)->GetImage(),VK_IMAGE_LAYOUT_GENERAL,previewColor);
        Submit(device,command);
        VerifySelectedPreview(device,scene,glm::vec3(.25f,.5f,.75f));
        gi.UpdateGISettings(&device,&command,settings);
        Check(gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage()==atlas,
            "Mixed-mode preview selection replaced the scrolling atlas");
        struct Readbacks
        {
            VkDevice device;std::array<VansVKBuffer,3> buffers;
            ~Readbacks(){for(auto& buffer:buffers)buffer.DestroyVulkanBuffer(device);}
        } readback{device.GetLogicDevice()};
        const std::array<const VansVKBuffer*,3> source={current,previous,&layout};
        for(size_t i=0;i<3;++i)
            Check(readback.buffers[i].CreatVulkanBuffer(device.GetLogicDevice(),source[i]->GetBufferSize(),VK_FORMAT_R32_UINT,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
                readback.buffers[i].PersistentMap(),"Scrolling readback allocation failed");
        VansGIScrollingGrid oracle;std::string error;
        Check(oracle.Initialize({9,10,11},1,{0,0,0},error),error);
        uint32_t epoch=0;
        for(auto center:{glm::vec3(1.1f,0,0),glm::vec3(-2.1f,-1,3),glm::vec3(500,-100,200)})
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Scrolling seed begin failed");
            VkMemoryBarrier transfer{VK_STRUCTURE_TYPE_MEMORY_BARRIER};transfer.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            transfer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{transfer});
            for(auto* buffer:{current,previous})command.FillBuffer(buffer->GetNativeBuffer(),0,buffer->GetBufferSize(),0x12345678u);
            Submit(device,command);
            std::vector<uint32_t> entering;Check(oracle.Move(center,entering,error),error);++epoch;
            gi.SetWorldViewCenter(center);
            const auto resolved=*gi.GetGIRegionResolved(1);
            Check(resolved.volumeMin==oracle.Minimum() && resolved.scrollOffset==oracle.RingOffset() && resolved.scrollEpoch==epoch,
                "Runtime failed to publish the moved world grid");
            Check(gi.GetGIRegionResolved(0)->volumeMin==fixed.volumeMin && gi.GetGIRegionResolved(0)->scrollEpoch==0,
                "Scrolling changed the fixed region");
            auto parameters=settings;parameters.regions[1].normalBias=.17f;gi.UpdateGISettings(&device,&command,parameters);
            Check(gi.GetGIRegionResolved(1)->volumeMin==oracle.Minimum() && gi.GetGIRegionResolved(1)->scrollEpoch==epoch,
                "Lighting parameter edit restored the authored center");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Scrolling publication begin failed");
            std::unique_ptr<GICostCapture> costCapture;
            if(epoch==1)costCapture=std::make_unique<GICostCapture>(device,command);
            // 工作表为空，仅执行真实帧入口的布局发布与回收槽清理，不伪造几何追踪结果。
            gi.DispatchRayTracing(&device,&command,&scene);
            transfer.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{transfer});
            for(size_t i=0;i<3;++i)command.CopyBuffer(source[i]->GetNativeBuffer(),readback.buffers[i].GetNativeBuffer(),0,0,source[i]->GetBufferSize());
            transfer.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{transfer});Submit(device,command);
            if(costCapture){costCapture->CompleteAfterSubmission(1);costCapture.reset();}
            for(size_t i=0;i<3;++i)readback.buffers[i].InvalidateMappedRange(0,source[i]->GetBufferSize());
            for(size_t buffer=0;buffer<2;++buffer)
            {
                const auto* words=static_cast<const uint32_t*>(readback.buffers[buffer].GetMappedPtr());
                for(uint32_t id=0;id<oracle.ProbeCount();++id)
                {
                    const auto expected=std::binary_search(entering.begin(),entering.end(),id)?0u:0x12345678u;
                    for(uint32_t word=0;word<12;++word)Check(words[id*12+word]==expected,"Scrolling cleared overlapping state or retained a recycled origin");
                }
            }
            const auto* words=static_cast<const glm::uvec4*>(readback.buffers[2].GetMappedPtr());
            Check(glm::vec3(glm::uintBitsToFloat(words[9]))==oracle.Minimum() && words[words[3].w+2u]==glm::uvec4(oracle.RingOffset(),1u) &&
                words[words[3].w+3u].w==epoch,"GPU layout addresses and cache epoch were not published together");
            Check(gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage()==atlas && device.GetGIMemorySnapshot().probes.world.bufferCapacityBytes==bytes,
                "Scrolling reallocated GI resources");
        }
        const auto before=*gi.GetGIRegionResolved(1);bool rejected=false;
        try{gi.SetWorldViewCenter({NAN,0,0});}catch(const std::exception&){rejected=true;}
        Check(rejected && gi.GetGIRegionResolved(1)->volumeMin==before.volumeMin && gi.GetGIRegionResolved(1)->scrollEpoch==before.scrollEpoch,
            "Invalid camera partially published a scrolling grid");
        std::cout<<"[GIResourcesGPU] scrolling runtime sparse="<<HasAdaptiveGIRegions(settings)<<": overlap retained, current/previous state cleared, layout+epoch published, parameter edits stable, fixed resources PASS"<<std::endl;
    }

    void VerifyPcgInstancePublication(VansVKDevice& device)
    {
        struct Workers { Workers(){Vans::VansJobSystem::Get().Initialize(2);} ~Workers(){Vans::VansJobSystem::Get().Shutdown();} } workers;
        const auto meshId=Vans::VansAssetGuid::FromStableName("gi-test","quad");
        const auto materialId=Vans::VansAssetGuid::FromStableName("gi-test","wood");
        struct Vertex { glm::vec3 position; glm::vec2 uv; glm::vec3 normal; };
        const std::array<Vertex,4> vertices{{{{0,-1,-1},{0,0},{1,0,0}},{{0,1,-1},{1,0},{1,0,0}},
            {{0,1,1},{1,1},{1,0,0}},{{0,-1,1},{0,1},{1,0,0}}}};
        const std::array<uint32_t,6> indices{0,1,2,0,2,3};
        VansMesh mesh;mesh.SetName(meshId.ToString());
        mesh.InitFromRawData(device.GetLogicDevice(),vertices.data(),4,sizeof(Vertex),indices.data(),6,
            {{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},
            {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,uv)},{2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)}});
        VansPBRMaterial material{};material.m_MaterialType=VansMaterialType::VAN_PBR;material.SetName(materialId.ToString());
        material.m_BasePBRParam.m_albedo=glm::vec3(.5f);material.m_BasePBRParam.m_metallic=0;material.m_BasePBRParam.m_roughness=1;
        VansScene scene;scene.AddMeshAsset(&mesh);scene.AddMaterialAsset(&material);
        scene.SetVegetationCollection(std::make_unique<VansVegetationCollection>());
        VansGISettings settings;settings.world.enabled=true;
        settings.world.levelCount=1;settings.world.maxBricks=64;settings.world.bricksPerFrame=1;
        settings.regions.resize(2);
        settings.regions[0].overrideGridDimensions=true;settings.regions[0].gridDimensions={2,2,2};
        settings.regions[0].raysPerProbe=16;settings.regions[0].center={100,0,0};
        settings.regions[1]=settings.regions[0];settings.regions[1].stableId=2;
        settings.regions[1].worldOnly=true;settings.regions[1].followView=true;
        settings.regions[1].gridDimensions={9,9,9};settings.regions[1].probeSpacing=1;settings.regions[1].center={0,0,0};
        NormalizeGISettings(settings);scene.SetGISettings(settings);
        auto plant=std::make_shared<Vans::VansPlantTypeAsset>();plant->category=Vans::VansPlantCategory::Tree;
        Vans::VansPlantVariant variant;variant.id="tree";variant.geometry=Vans::VansPlantGeometry::Mesh;variant.cullingRadius=2;
        variant.parts.push_back({"trunk",Vans::VansPlantPartKind::Trunk,meshId,-1,materialId});
        // The minimal quad is already irreducible; its test LOD uses the same
        // geometry with explicit runtime bounds, as required by the PCG owner.
        variant.lod.centerRadius={0,0,0,std::sqrt(2.0f)};
        variant.lod.levels.push_back({{{meshId,materialId,-1,0,2,0}}});
        variant.lodSettings.ratios={.5f};plant->render.lodDistances={60.0f};
        plant->variants.push_back(variant);
        auto& gi=device.GetRayTracingContext();auto& command=device.GetImmediateGraphicsCommandBuffer();
        struct GiLifetime { VansVKDevice& device; ~GiLifetime(){device.GetRayTracingContext().CleanupSceneResources(device.GetLogicDevice());} } lifetime{device};
        const auto apply=[&](std::vector<float> positions)
        {
            Vans::VansPcgBatchUpdate update;update.region="test";update.layer="trees";update.replaceAll=true;update.cellSize=16;
            for(const auto x:positions)
            {
                auto batch=std::make_shared<Vans::VansPcgBatchSource>();batch->key={"test","trees","tree",int64_t(std::floor(x/16)),0};batch->plant=plant;
                Vans::VansPcgPoint point;point.id=x<16?1:2;point.position={x,0,0};point.scale={1,1,1};point.rotation={0,0,0,1};batch->points.push_back(point);
                update.batches.emplace(batch->key,batch);
            }
            std::string error;Check(scene.GetVegetationCollection()->Apply(scene,device,update,error),error);
        };
        apply({0,40});
        Check(gi.CreateRayTracingResource(&device,&command, &scene, settings, scene.GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
        VansGIWorld world;world.Initialize(device,scene,settings.world,false,settings.placement.maxRaysPerFrame,scene.GetMaterialManager()->GetGIVoxelMaterialInputs());
        // 测试通过中间来源管理器绑定实际 GI 接收端，保持 PCG 不依赖设备/RT 类型。
        scene.GetGIVoxelSourceManager().Bind([&gi](GIVoxelSourceChanges changes)
        {
            const bool accepted=gi.QueueWorldSourceChanges(std::move(changes));
            if(!accepted)gi.InvalidateWorldSources();
            return accepted;
        });
        Check(world.SourceTemplateCount()==1,"PCG chunks duplicated a shared model template");
        std::map<std::string,std::vector<glm::mat4>> lastInputs;
        {std::vector<GIVoxelSource> initial;scene.CollectGIVoxelSources(initial);Check(initial.size()==2,"PCG source chunks were flattened before GI");
            for(auto& source:initial)lastInputs.emplace(source.key,std::move(source.instances));}
        const auto worldBytes=world.BufferCapacityBytes(),giBytes=device.GetGIMemorySnapshot().probes.world.bufferCapacityBytes;
        const auto worldDescriptor=world.Descriptor();const auto indoor=gi.GetGIRegionIrradianceAtlas(0)->GetImage().GetImage();
        const auto outdoor=gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage();
        VansGIWorldQueryChanges observedChanges;
        VansGIWorldUploadWork lastWorldWork;
        uint64_t observedUploadedBricks=0,observedMaterialBytes=0;
        bool capturedPagePublication = false;
        const auto pump=[&]()
        {
            world.PrepareUpdates();gi.PrepareWorldUpdates();
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG publication begin failed");
            std::unique_ptr<GICostCapture> costCapture;
            if(!capturedPagePublication)costCapture=std::make_unique<GICostCapture>(device,command);
            const auto before=world.QueryPublication().sequence;
            {
                BufferUpdateObserver observer(command.GetVKCommandBuffer());
                lastWorldWork=world.RecordUpdates(command);
                Check(lastWorldWork.UploadBytes()==observer.bytes && lastWorldWork.updateCommands==observer.commands,
                    "World upload accounting differs from actual vkCmdUpdateBuffer calls");
                Check(lastWorldWork.voxelBytes==uint64_t(lastWorldWork.uploadedBricks)*sizeof(GIWorldBrick::voxels) &&
                    lastWorldWork.readyBricks<=lastWorldWork.mappedBricks && lastWorldWork.mappedBricks<=lastWorldWork.brickCapacity &&
                    lastWorldWork.bufferCapacityBytes==worldBytes,"World brick or buffer capacity accounting is inconsistent");
                observedUploadedBricks+=lastWorldWork.uploadedBricks;
                observedMaterialBytes+=lastWorldWork.materialBytes;
            }
            gi.DispatchRayTracing(&device,&command,&scene);Submit(device,command);
            if(costCapture)
            {
                costCapture->CompleteAfterSubmission(2);costCapture.reset();
                capturedPagePublication=world.QueryPublication().sequence!=before && world.QueryPublication().changes.residency.has_value();
                if(capturedPagePublication)std::cout << "[GIFrameCostGPU] verified real page upload / readiness publication\n";
            }
            if(world.QueryPublication().sequence!=before)
            {
                const auto& changes=world.QueryPublication().changes;
                if(changes.geometry)VansGIWorldQueryChanges::Include(observedChanges.geometry,*changes.geometry);
                if(changes.residency)VansGIWorldQueryChanges::Include(observedChanges.residency,*changes.residency);
            }
        };
        const auto drain=[&]()
        {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(world.HasPendingUpdates() || gi.HasPendingWorldUpdates())
            {pump();Check(std::chrono::steady_clock::now()<deadline,"PCG voxel transaction did not finish");}
            Check(lastWorldWork.readyBricks==lastWorldWork.mappedBricks && lastWorldWork.pendingBuildBricks==0,
                "Drained world accounting retains unready or unbuilt bricks");
            Check(world.BufferCapacityBytes()==worldBytes && device.GetGIMemorySnapshot().probes.world.bufferCapacityBytes==giBytes && world.Descriptor()==worldDescriptor &&
                gi.GetGIRegionIrradianceAtlas(0)->GetImage().GetImage()==indoor && gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage()==outdoor,
                "PCG instance edit reallocated GI resources");
        };
        drain();
        Check(observedUploadedBricks>0,"World work accounting never observed a real brick upload");
        std::cout<<"[GIFrameWorkGPU] actual world upload byte/command counts and drained ready pages PASS; bricks="<<observedUploadedBricks<<'\n';
        Check(capturedPagePublication,"Native cost fixture did not exercise a real world page publication");
        Check(!observedChanges.geometry && observedChanges.residency,
            "Initial brick readiness must publish residency without a geometry edit");
        {
            GIVoxelSource unavailable;unavailable.key="pcg/4:test/5:trees/4:tree/0/0";
            unavailable.modelIdentity=plant;unavailable.availability=GIVoxelSourceAvailability::TemporarilyUnavailable;
            GIVoxelSourceChanges pending;pending.updated.push_back(std::move(unavailable));
            Check(world.QueueSourceChanges(std::move(pending)),"Temporarily unavailable source was rejected");
            Check(!world.HasPendingUpdates(),"Temporarily unavailable source dirtied published GI");
        }
        struct NoDeviceIdle
        {
            NoDeviceIdle(){instanceEditIdleCalls=0;originalWaitIdle=vkDeviceWaitIdle;vkDeviceWaitIdle=CountInstanceEditWaitIdle;}
            void Stop(){vkDeviceWaitIdle=originalWaitIdle;}
            ~NoDeviceIdle(){Stop();}
        } noDeviceIdle;
        VansComputeShader query;query.SetName("GIWorldPcgQuery");
        query.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(query.InitShader(device.GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldLodBoundary.comp"}}),"PCG query shader failed");
        VansPipelineProgramDesc description{};description.kind=VansPipelineProgramKind::Compute;description.pushConstantSize=32;
        query.SetPipelineProgramDesc(description);query.SetPushConstant(32);
        struct Readback { VkDevice device;VansVKBuffer buffer;~Readback(){buffer.DestroyVulkanBuffer(device);} } readback{device.GetLogicDevice()};
        Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(),gi.GetGIRegionProbeStateBuffer(1)->GetBufferSize(),VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && readback.buffer.PersistentMap(),"PCG readback failed");
        const auto sample=[&](float x,float distance,float yz=.125f,bool readMaterial=false,const VansGIWorld* target=nullptr)
        {
            const auto& queried=target?*target:world;
            const std::array<glm::vec4,2> ray{glm::vec4(x,yz,yz,distance),glm::vec4(1,0,0,0)};
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG query begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            command.EnsureComputeShader(query,{queried.Layout()});command.DispatchCompute(query,readMaterial?3:1,1,1,{queried.Descriptor()},ray.data(),sizeof(ray));
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            command.CopyBuffer(queried.ScatterBuffer().GetNativeBuffer(),readback.buffer.GetNativeBuffer(),readMaterial?32:0,0,16);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            readback.buffer.InvalidateMappedRange(0,16);return *static_cast<const glm::vec4*>(readback.buffer.GetMappedPtr());
        };
        auto hit=sample(-2,12);Check(hit.z==1 && hit.x>1.5f && hit.x<2.5f,"Initial PCG whole-model voxel hit missing");
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG seed begin failed");
        VkClearColorValue marker{};marker.float32[0]=.25f;marker.float32[1]=.5f;marker.float32[2]=.75f;marker.float32[3]=1;
        VkMemoryBarrier seed{VK_STRUCTURE_TYPE_MEMORY_BARRIER};seed.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;seed.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{seed});
        command.ClearColorImage(gi.GetGIRegionIrradianceAtlas(0)->GetImage(),VK_IMAGE_LAYOUT_GENERAL,marker);
        for(uint32_t region=0;region<2;++region)for(const auto* buffer:{gi.GetGIRegionProbeStateBuffer(region),gi.GetGIRegionPreviousProbeStateBuffer(region)})
            command.FillBuffer(buffer->GetNativeBuffer(),0,buffer->GetBufferSize(),0x12345678u);
        Submit(device,command);
        const auto refresh=[&](std::vector<float> positions)
        {
            apply(std::move(positions));Check(!gi.NeedsWorldSourceRebuild(),"PCG known-model chunk edit required GI reconstruction");
            std::vector<GIVoxelSource> sources;scene.CollectGIVoxelSources(sources);
            GIVoxelSourceChanges changes;std::map<std::string,std::vector<glm::mat4>> nextInputs;
            for(auto& source:sources)
            {
                nextInputs.emplace(source.key,source.instances);
                const auto old=lastInputs.find(source.key);
                if(old==lastInputs.end() || old->second!=source.instances)changes.updated.push_back(std::move(source));
            }
            for(const auto& old:lastInputs)if(!nextInputs.count(old.first))changes.removed.push_back(old.first);
            Check(world.QueueSourceChanges(std::move(changes)),"Known PCG model template was not reused");lastInputs=std::move(nextInputs);
        };
        observedChanges={};refresh({2,40});drain();hit=sample(-2,12);
        Check(observedChanges.geometry && observedChanges.residency &&
            observedChanges.geometry->minimum.x<=0 && observedChanges.geometry->maximum.x>=2 && observedChanges.geometry->maximum.x<40,
            "Local PCG publication must separate old/new content bounds from page residency without including unchanged source geometry");
        const auto editStats=gi.GetWorldSourceUpdateStats();
        Check(editStats.updatedChunks==1 && editStats.removedChunks==0 && editStats.submittedInstances==1 && editStats.preparedInstances==1,
            "Local PCG edit resubmitted or rebuilt an unchanged chunk");
        Check(hit.z==1 && hit.x>3.5f && hit.x<4.5f,"Moving a PCG tree retained old voxel contents");
        VerifyAtlas(device,command,indoor);
        size_t cleared=0,preserved=0;
        for(uint32_t region=0;region<2;++region)for(const auto* buffer:{gi.GetGIRegionProbeStateBuffer(region),gi.GetGIRegionPreviousProbeStateBuffer(region)})
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG state readback begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            command.CopyBuffer(buffer->GetNativeBuffer(),readback.buffer.GetNativeBuffer(),0,0,buffer->GetBufferSize());
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            readback.buffer.InvalidateMappedRange(0,buffer->GetBufferSize());const auto* words=static_cast<const uint32_t*>(readback.buffer.GetMappedPtr());
            for(size_t i=0;i<buffer->GetBufferSize()/4;++i)
            {
                if(region==0)Check(words[i]==0x12345678u,"PCG edit cleared indoor probe state");
                else if(words[i]==0)++cleared;else{Check(words[i]==0x12345678u,"PCG edit corrupted outdoor state");++preserved;}
            }
        }
        Check(cleared>0 && preserved>0,"PCG edit failed to restrict probe-state invalidation to local geometry");
        // 启动 A 的候选规划后，在轮询完成之前提交 B；任何一帧都不得发布已经过时的 A。
        refresh({6,40});
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG stale candidate begin failed");
        world.RecordUpdates(command);gi.DispatchRayTracing(&device,&command,&scene);Submit(device,command);
        refresh({4,40});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(world.HasPendingUpdates() || gi.HasPendingWorldUpdates())
        {
            pump();hit=sample(-2,12,-.875f);
            Check(world.IsPositionValid({6.125f,.125f,.125f},0),"Superseded PCG source snapshot reached probe placement");
            Check(hit.z==0 || hit.x<6.5f,"Superseded PCG source snapshot reached the GPU");
            Check(std::chrono::steady_clock::now()<deadline,"Coalesced PCG edit stalled");
        }
        hit=sample(-2,12);Check(hit.z==1 && hit.x>5.5f && hit.x<6.5f,"Latest PCG edit was not published");
        // 两个不同区块的连续编辑必须合并，不能因丢弃 A 的后台批次而丢掉 A 的改动。
        refresh({8,40});
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG cross-chunk candidate begin failed");
        world.RecordUpdates(command);gi.DispatchRayTracing(&device,&command,&scene);Submit(device,command);
        refresh({8,42});drain();hit=sample(-2,12);
        Check(hit.z==1 && hit.x>9.5f && hit.x<10.5f,"Coalescing B lost the unpublished change to chunk A");
        hit=sample(38,6);Check(hit.z==1 && hit.x>3.5f && hit.x<4.5f,"Coalescing A lost the latest change to chunk B");
        refresh({4,40});drain();
        refresh({40});drain();hit=sample(-2,12);Check(hit.z==1 && hit.x==12,"Removed PCG tree left stale occlusion");
        hit=sample(38,4);Check(hit.z==1 && hit.x>1.5f && hit.x<2.5f,"Local deletion damaged distant retained voxel pages");
        refresh({});drain();hit=sample(38,4);Check(hit.z==1 && hit.x==4,"Empty PCG source left stale global pages");
        refresh({0,40});drain();hit=sample(-2,12);
        Check(hit.z==1 && hit.x>1.5f && hit.x<2.5f && world.SourceTemplateCount()==1,"Known model chunk reload failed or rebaked its template");
        VerifyAtlas(device,command,indoor);
        Check(instanceEditIdleCalls==0,"PCG instance edit called vkDeviceWaitIdle");
        // 两端同时编辑，位于二者之间的整个探针区都应保留 current/previous。
        refresh({-40,40});drain();
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Separated edit seed begin failed");
        command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{seed});
        for(const auto* buffer:{gi.GetGIRegionProbeStateBuffer(1),gi.GetGIRegionPreviousProbeStateBuffer(1)})
            command.FillBuffer(buffer->GetNativeBuffer(),0,buffer->GetBufferSize(),0x12345678u);
        Submit(device,command);
        refresh({-42,42});drain();size_t middleWords=0;
        for(const auto* buffer:{gi.GetGIRegionProbeStateBuffer(1),gi.GetGIRegionPreviousProbeStateBuffer(1)})
        {
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Separated edit readback begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            command.CopyBuffer(buffer->GetNativeBuffer(),readback.buffer.GetNativeBuffer(),0,0,buffer->GetBufferSize());
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            readback.buffer.InvalidateMappedRange(0,buffer->GetBufferSize());const auto* words=static_cast<const uint32_t*>(readback.buffer.GetMappedPtr());
            for(size_t i=0;i<buffer->GetBufferSize()/4;++i){Check(words[i]==0x12345678u,"Separated edits cleared unchanged middle probe state");++middleWords;}
        }
        hit=sample(-44,6);Check(hit.z==1 && hit.x>1.5f && hit.x<2.5f,"Separated left edit failed to publish");
        hit=sample(38,6);Check(hit.z==1 && hit.x>3.5f && hit.x<4.5f,"Separated right edit failed to publish");
        VerifyAtlas(device,command,indoor);
        Check(instanceEditIdleCalls==0,"Separated PCG edits called vkDeviceWaitIdle");
        std::cout<<"[GIDisjointPublicationGPU] PASS: both changed chunks published; middle current/previous words preserved="<<middleWords<<std::endl;
        const auto cpu=world.CpuWorkStats();
        Check(cpu.pagePlanJobs>0 && cpu.pagesPlanned>0 && cpu.retirementJobs>=cpu.pagePlanJobs,
            "PCG page planning or transaction retirement was not exercised");
        Check(cpu.submissionThreadPagePlans==0 && cpu.submissionThreadRetirements==0,
            "PCG page planning or retired transaction release ran on the submission thread");
        std::cout<<"[GIWorldCpuPublication] PASS: background page plans="<<cpu.pagePlanJobs<<" pages="<<cpu.pagesPlanned
            <<" retirements="<<cpu.retirementJobs<<" submissionThreadPlans="<<cpu.submissionThreadPagePlans
            <<" submissionThreadRetirements="<<cpu.submissionThreadRetirements<<std::endl;
        const auto worldGeneration=world.QueryPublication().generation;
        const auto nativeGeneration=gi.GetGIWorld()->QueryPublication().generation;
        const auto submitNewPlant=[&]()
        {
            scene.GetMaterialManager()->FlushMaterialPayload(material);
            const auto frame=scene.GetMaterialManager()->CaptureRenderMaterialFrameData({&material});
            for(auto* field:{&world,gi.GetGIWorld()})field->ApplyMaterials(frame.voxelMaterials);
            plant=std::make_shared<Vans::VansPlantTypeAsset>(*plant);apply({0,40});
            Check(!gi.NeedsWorldSourceRebuild(),"New PCG template requested a whole-world reconstruction");
            GIVoxelSourceChanges changes;scene.CollectGIVoxelSources(changes.updated);
            std::map<std::string,std::vector<glm::mat4>> nextInputs;
            for(const auto& source:changes.updated)nextInputs.emplace(source.key,source.instances);
            for(const auto& old:lastInputs)if(!nextInputs.count(old.first))changes.removed.push_back(old.first);
            Check(world.QueueSourceChanges(std::move(changes)),"Standalone world rejected a new PCG template");
            lastInputs=std::move(nextInputs);
        };
        material.m_BasePBRParam.m_albedo={.15f,.65f,.25f};submitNewPlant();
        Check(world.SourceTemplateCount()==2 && gi.GetGIWorld()->SourceTemplateCount()==2,
            "New shared model was captured more than once for two PCG chunks");
        // Captured material values must remain independent of later owner edits.
        material.m_BasePBRParam.m_albedo={.8f,.1f,.05f};
        pump();refresh({4,44});drain();
        Check(world.CpuWorkStats().templateBakes==1 && gi.GetGIWorld()->CpuWorkStats().templateBakes==1 &&
            world.CpuWorkStats().submissionThreadTemplateBakes==0 && gi.GetGIWorld()->CpuWorkStats().submissionThreadTemplateBakes==0,
            "Superseded new-template transaction duplicated baking or baked on the submission thread");
        hit=sample(-2,12);Check(hit.z==1 && hit.x>5.5f && hit.x<6.5f,"Superseded new template published old transforms");
        const glm::vec4 capturedColor(.15f,.65f,.25f,1);
        for(const auto* queried:{&world,gi.GetGIWorld()})
            Check(glm::length(sample(-2,12,.125f,true,queried)-capturedColor)<.0001f,
                "New voxel referenced an unuploaded material or mutable material input");
        Check(observedMaterialBytes==2*sizeof(glm::vec4),"Published author edit and new template did not upload exactly their two material slots");
        // Removing an uncooked template retires its owned CPU capture without baking it.
        std::vector<GIVoxelSource> liveSources;scene.CollectGIVoxelSources(liveSources);
        auto cancelled=liveSources.front();cancelled.key="contract/cancelled-template";
        cancelled.modelIdentity=std::make_shared<int>(1);
        GIVoxelSourceChanges addition;addition.updated.push_back(cancelled);
        Check(world.QueueSourceChanges(addition) && gi.QueueWorldSourceChanges(std::move(addition)),"Pending template capture failed");
        GIVoxelSourceChanges removal;removal.removed.push_back(cancelled.key);
        Check(world.QueueSourceChanges(removal) && gi.QueueWorldSourceChanges(std::move(removal)),"Pending template cancellation failed");
        drain();
        Check(world.SourceTemplateCount()==2 && gi.GetGIWorld()->SourceTemplateCount()==2 &&
            world.CpuWorkStats().templateBakes==1 && gi.GetGIWorld()->CpuWorkStats().templateBakes==1,
            "Cancelled template retained its capture or was unnecessarily baked");
        // Reject a whole batch before readback or publication when its material IDs cannot fit.
        auto oversized=liveSources.front();oversized.key="contract/oversized-template";
        oversized.modelIdentity=std::make_shared<int>(2);
        const auto repeatedPart=oversized.parts.front();oversized.parts.assign(65536,repeatedPart);
        const auto sequence=world.QueryPublication().sequence;
        GIVoxelSourceChanges rejected;rejected.updated.push_back(std::move(oversized));
        Check(!world.QueueSourceChanges(std::move(rejected)) && world.QueryPublication().sequence==sequence &&
            world.SourceTemplateCount()==2 && !world.HasPendingUpdates(),"Rejected capture partially mutated the world");
        // Alpha pixels and cutoff are owned snapshots too; later material edits cannot affect baking.
        VansTexture transparent;const uint32_t transparentPixel=0x0000ff00u;
        transparent.LoadFromMemory(command,&transparentPixel,4,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        material.m_BaseColorTexture=&transparent;material.m_AlphaTestEnabled=true;material.m_AlphaCutoff=.5f;
        submitNewPlant();
        material.m_BaseColorTexture=nullptr;material.m_AlphaTestEnabled=false;material.m_AlphaCutoff=0;
        drain();hit=sample(-2,12);Check(hit.z==1 && hit.x==12,"Captured transparent alpha changed after material owner mutation");
        submitNewPlant();drain();hit=sample(-2,12);
        Check(hit.z==1 && hit.x>1.5f && hit.x<2.5f,"Opaque new model did not restore the voxel field");
        for(const auto* queried:{&world,gi.GetGIWorld()})
        {
            Check(glm::length(sample(-2,12,.125f,true,queried)-glm::vec4(.8f,.1f,.05f,1))<.0001f,
                "Later template material slot did not publish correctly");
            Check(queried->CpuWorkStats().templateBakes==3 && queried->CpuWorkStats().submissionThreadTemplateBakes==0,
                "Incremental template bake count or thread ownership is incorrect");
        }
        Check(world.QueryPublication().generation==worldGeneration && gi.GetGIWorld()->QueryPublication().generation==nativeGeneration &&
            !gi.NeedsWorldSourceRebuild() && instanceEditIdleCalls==0,"Incremental template publication rebuilt the world or waited for device idle");
        VerifyAtlas(device,command,indoor);
        Check(observedMaterialBytes==6*sizeof(glm::vec4),"Material publication uploaded unexpected or cancelled capture slots");
        std::cout<<"[GIIncrementalTemplateGPU] PASS: shared new identity, background bake, superseded job reuse, owned material/alpha snapshot, GPU material readback, cancelled capture, bounded rejection, fixed world/atlas resources; materialBytes="<<observedMaterialBytes<<std::endl;
        Check(world.CpuWorkStats().captureDecodes==3 && gi.GetGIWorld()->CpuWorkStats().captureDecodes==3 &&
            world.CpuWorkStats().submissionThreadCaptureDecodes==0 && gi.GetGIWorld()->CpuWorkStats().submissionThreadCaptureDecodes==0,
            "New source GPU snapshots were not decoded once per batch on workers");
        {
            // Retain native allocations even when every borrowed source wrapper is gone before submission.
            auto capturedMesh=std::make_unique<VansMesh>();
            capturedMesh->InitFromRawData(device.GetLogicDevice(),vertices.data(),4,sizeof(Vertex),indices.data(),6,
                {{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},
                {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,uv)},{2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)}});
            auto capturedTexture=std::make_unique<VansTexture>();const uint32_t pixel=0xff804020u;
            capturedTexture->LoadFromMemory(command,&pixel,4,1,1,VK_FORMAT_R8G8B8A8_UNORM);
            auto capturedMaterial=std::make_unique<VansPBRMaterial>();
            capturedMaterial->m_BaseColorTexture=capturedTexture.get();capturedMaterial->m_BasePBRParam.m_albedo=glm::vec3(1);
            capturedMaterial->m_BasePBRParam.m_metallic=0;capturedMaterial->m_BasePBRParam.m_roughness=.75f;
            const auto sourceBytes=capturedMesh->GetBLASVertexBuffer().GetAllocationSize()+capturedMesh->GetIndexBuffer().GetAllocationSize()+
                capturedTexture->GetImage().GetAllocationSize();
            const GIVoxelSourcePart part{capturedMesh.get(),capturedMaterial.get(),-1,false};
            auto captureShader=std::make_shared<VansComputeShader>();
            captureShader->SetName("GIWorld/GIWorldTexture");
            captureShader->SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
            Check(captureShader->InitShader(device.GetLogicDevice(),"EngineAssets/Shaders/GIWorld",
                {{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldTexture.comp"}}),"Capture fixture shader failed");
            captureShader->SetPushConstant(16);
            const std::weak_ptr<VansComputeShader> shaderLease=captureShader;
            VansGITemplateCapture capture;CaptureSubmissionObserver observer;
            const auto beforeSnapshot=VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            const VansGIVoxelMaterial materialInput{glm::vec3(1),0,.75f,false,.5f,capturedTexture.get()};
            Check(capture.Prepare(device,{{{part},100,{materialInput}},{{part},101,{materialInput}}},0,64u*1024u*1024u,captureShader)==2,
                "Small capture batch was unnecessarily split");
            captureShader.reset();Check(!shaderLease.expired(),"Prepared capture lost its shader owner");
            const auto held=VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            Check(held==beforeSnapshot && observer.submissions==0 && observer.waits==0 && !capture.Ready(),
                "Prepared capture allocated GPU storage, submitted work, or published an unsubmitted snapshot");
            capturedMaterial->m_BasePBRParam.m_albedo=glm::vec3(0);
            capturedMaterial.reset();capturedTexture.reset();capturedMesh.reset();
            Check(VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes==held,
                "Source wrapper destruction released a capture's native allocation");
            capture.Submit();observer.holdReady=true;
            const auto submittedBytes=VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            Check(submittedBytes>held,"Capture did not allocate readback storage at submission");
            Check(!capture.Poll() && observer.submissions==1 && observer.waits==0,"Batched GPU capture blocked instead of polling");
            observer.holdReady=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!capture.Poll()){Check(std::chrono::steady_clock::now()<deadline,"Native capture fence did not complete");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            Check(observer.waits==0 && VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes+sourceBytes==submittedBytes,
                "Completed capture waited or retained destroyed source allocations");
            Check(shaderLease.expired(),"Completed capture retained its shader lease");
            const auto decoded=capture.Decode();Check(decoded.size()==2,"Batched capture lost a model definition");
            Check(VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes+sourceBytes==held,
                "Decoded capture retained its GPU readback allocation");
            for(size_t i=0;i<decoded.size();++i)
            {
                Check(decoded[i].meshes->size()==1 && decoded[i].meshes->front().indices.size()==6 && decoded[i].meshes->front().material==100+i &&
                    decoded[i].meshes->front().positions[0]==vertices[0].position,"Retired mesh buffer readback changed geometry or material identity");
                Check(glm::length(decoded[i].materials.front()-glm::vec4(32.f/255,64.f/255,128.f/255,.75f))<.0001f,
                    "Retired texture or material metadata changed the captured value");
            }
        }
        {
            // A rejected native submission must leave cache, material table and source transaction untouched.
            std::vector<GIVoxelSource> sources;scene.CollectGIVoxelSources(sources);
            auto rejectedSource=sources.front();rejectedSource.key="contract/rejected-submit";
            rejectedSource.modelIdentity=std::make_shared<int>(3);
            GIVoxelSourceChanges changes;changes.updated.push_back(std::move(rejectedSource));
            const auto templates=world.SourceTemplateCount(),sequenceBefore=world.QueryPublication().sequence;
            const auto allocations=VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            CaptureSubmissionObserver observer;observer.rejectSubmit=true;
            Check(!world.QueueSourceChanges(std::move(changes)) && observer.submissions==1 && observer.waits==0,
                "Rejected capture submission was accepted or waited for a nonexistent fence signal");
            Check(world.SourceTemplateCount()==templates && world.QueryPublication().sequence==sequenceBefore && !world.HasPendingUpdates() &&
                VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes==allocations,
                "Rejected capture submission changed the old world or leaked GPU storage");
        }
        {
            std::vector<GIVoxelSource> sources;scene.CollectGIVoxelSources(sources);
            GIVoxelSourceChanges changes;
            for(int i=0;i<2;++i)
            {
                auto source=sources.front();source.key="contract/async-batch/"+std::to_string(i);
                source.modelIdentity=std::make_shared<int>(10+i);
                source.instances={glm::translate(glm::mat4(1),glm::vec3(16+40*i,0,0))};changes.updated.push_back(std::move(source));
            }
            const auto sequenceBefore=world.QueryPublication().sequence;
            {
                CaptureSubmissionObserver observer;observer.holdReady=true;
                Check(world.QueueSourceChanges(changes) && gi.QueueWorldSourceChanges(std::move(changes)),"Async world source batch was rejected");
                Check(observer.submissions==2 && observer.waits==0,"World capture did not use one nonblocking submission per owner");
                world.PrepareUpdates();gi.PrepareWorldUpdates();
                Check(observer.polls==2 && observer.waits==0 && world.QueryPublication().sequence==sequenceBefore,
                    "Incomplete capture waited or published an unready world");
            }
            drain();hit=sample(14,4);Check(hit.z==1 && hit.x>1.5f && hit.x<2.5f,"Async capture did not publish its new geometry");
            for(const auto* queried:{&world,gi.GetGIWorld()})
                Check(queried->CpuWorkStats().captureDecodes==4 && queried->CpuWorkStats().templateBakes==5 &&
                    queried->CpuWorkStats().submissionThreadCaptureDecodes==0,"Shared capture batch was redecoded or parsed on the submission thread");
            GIVoxelSourceChanges remove;remove.removed={"contract/async-batch/0","contract/async-batch/1"};
            Check(world.QueueSourceChanges(remove) && gi.QueueWorldSourceChanges(std::move(remove)),"Async capture cleanup failed");drain();
        }
        Check(instanceEditIdleCalls==0,"Async capture used device idle");
        std::cout<<"[GIAsyncCaptureGPU] PASS: one submit per source batch, zero fence waits during enqueue/poll, destroyed source wrappers retained until completion, exact geometry/texture decode, failed-submit rollback, unready old-field retention, worker decode and bake"<<std::endl;
        std::cout<<"[GIPcgPublicationGPU] PASS: unavailable-source retention, chunk deltas, shared templates, cached move/delete/reload, superseded and cross-chunk transactions, ray readback, local state clear, indoor preservation, incremental new model identity and fixed GI allocations; submittedInstances="<<editStats.submittedInstances<<" preparedInstances="<<editStats.preparedInstances<<" clearedWords="<<cleared<<" preservedWords="<<preserved<<" deviceIdleCalls="<<instanceEditIdleCalls<<std::endl;
        noDeviceIdle.Stop();
        {
            auto* manager=scene.GetMaterialManager();
            const int savedIndex=material.m_MaterialIndex;
            const auto savedParams=material.m_BasePBRParam;
            material.m_MaterialIndex=0;manager->m_GlobalPBRParamData.resize(1);
            VansTexture colorTexture;const uint32_t pixel=0xff4080c0u;
            colorTexture.LoadFromMemory(command,&pixel,sizeof(pixel),1,1,VK_FORMAT_R8G8B8A8_UNORM);
            material.m_BaseColorTexture=&colorTexture;
            const glm::vec3 textureColor(192.f/255.f,128.f/255.f,64.f/255.f);
            // 捕获已提交但尚未发布时改参数，完成值必须使用最新帧，不能回写捕获时的旧值。
            {
                CaptureSubmissionObserver observer;observer.holdReady=true;
                submitNewPlant();
                Check(manager->ApplyMaterialParameter(material,"albedo",glm::vec3(.4f,.2f,.6f)) &&
                    manager->ApplyMaterialParameter(material,"metallic",.25f) &&
                    manager->ApplyMaterialParameter(material,"roughness",.65f),"In-flight capture material edit failed");
                const auto frame=manager->CaptureRenderMaterialFrameData({&material});
                for(auto* field:{&world,gi.GetGIWorld()})field->ApplyMaterials(frame.voxelMaterials);
                const auto decodes=world.CpuWorkStats().captureDecodes;
                world.PrepareUpdates();gi.PrepareWorldUpdates();
                Check(observer.submissions==2 && observer.waits==0 && world.CpuWorkStats().captureDecodes==decodes,
                    "In-flight material update waited for or decoded an unfinished capture");
            }
            drain();
            for(auto* field:{&world,gi.GetGIWorld()})
            {
                Check(glm::length(sample(-2,12,.125f,true,field)-glm::vec4(textureColor*glm::vec3(.4f,.2f,.6f)*.75f,.65f))<.0001f,
                    "Capture completion overwrote a newer immutable material frame");
                field->TakeLightingChanges();
            }
            const auto beforeWork=world.CpuWorkStats(),beforeNativeWork=gi.GetGIWorld()->CpuWorkStats();
            const auto templates=world.SourceTemplateCount(),nativeTemplates=gi.GetGIWorld()->SourceTemplateCount();
            const auto publication=world.QueryPublication().sequence;
            std::shared_ptr<const VansGIVoxelMaterialInputs> previousSnapshot;
            glm::vec3 previousAlbedo(0);
            for(uint32_t edit=0;edit<64;++edit)
            {
                const glm::vec3 color=edit==0?glm::vec3(.02f,.9f,.3f):glm::vec3(float(edit%7)/6.f,float(edit%5)/4.f,float(edit%3)/2.f);
                const float metallic=float(edit%4)/3.f,roughness=.1f+float(edit%9)*.1f;
                Check(manager->ApplyMaterialParameter(material,"albedo",color) &&
                    manager->ApplyMaterialParameter(material,"metallic",metallic) &&
                    manager->ApplyMaterialParameter(material,"roughness",roughness),"Voxel radiance material edit failed");
                const auto frame=manager->CaptureRenderMaterialFrameData({&material});
                Check(frame.voxelMaterials && frame.voxelMaterials->at(&material).albedo==color,
                    "Voxel radiance snapshot omitted the author material");
                Check(manager->CaptureRenderMaterialFrameData({&material}).voxelMaterials==frame.voxelMaterials,
                    "Unchanged author material allocated a new voxel snapshot");
                if(previousSnapshot)Check(previousSnapshot->at(&material).albedo==previousAlbedo,
                    "A later Main edit mutated the submitted voxel material snapshot");
                const auto before=sample(-2,12,.125f,true);
                for(auto* field:{&world,gi.GetGIWorld()})
                {
                    field->ApplyMaterials(frame.voxelMaterials);
                    const auto changed=field->TakeLightingChanges();
                    Check(changed && changed->minimum.x<=0 && changed->maximum.x>=40,
                        "Shared material radiance did not notify both source chunks: edit="+std::to_string(edit)+
                        " field="+(field==&world?std::string("standalone"):std::string("native"))+
                        " bounds="+(changed?std::to_string(changed->minimum.x)+","+std::to_string(changed->maximum.x):std::string("none"))+
                        " oldRoughness="+std::to_string(before.w)+" snapshotRoughness="+std::to_string(frame.voxelMaterials->at(&material).roughness)+
                        " pending="+std::to_string(field->HasPendingUpdates()));
                    field->ApplyMaterials(frame.voxelMaterials);
                    Check(!field->TakeLightingChanges(),"Unchanged material snapshot repeated lighting invalidation");
                }
                Check(sample(-2,12,.125f,true)==before,"CPU material preparation changed GPU state before upload");
                drain();
                const glm::vec4 expected(textureColor*color*(1-metallic),roughness);
                for(const auto* field:{&world,gi.GetGIWorld()})for(float x:{0.f,40.f})
                    Check(glm::length(sample(x-2,12,.125f,true,field)-expected)<.0001f,
                        "Voxel GPU material did not follow the immutable radiance snapshot");
                previousSnapshot=frame.voxelMaterials;previousAlbedo=color;
            }
            Check(world.QueryPublication().sequence==publication,"A radiance-only edit invalidated geometry or residency");
            for(const auto* field:{&world,gi.GetGIWorld()})
            {
                const auto cpu=field->CpuWorkStats();const auto& before=field==&world?beforeWork:beforeNativeWork;
                Check(cpu.captureSubmissions==before.captureSubmissions && cpu.captureDecodes==before.captureDecodes &&
                    cpu.templateBakes==before.templateBakes && cpu.pagePlanJobs==before.pagePlanJobs,
                    "Radiance edits recaptured or rebuilt voxel geometry");
            }
            Check(world.SourceTemplateCount()==templates && gi.GetGIWorld()->SourceTemplateCount()==nativeTemplates,
                "Radiance edits retained an ever-growing template history");
            VansPBRMaterial unrelated;manager->RegisterGIVoxelMaterial(unrelated);
            const auto unrelatedFrame=manager->CaptureRenderMaterialFrameData({});
            world.ApplyMaterials(unrelatedFrame.voxelMaterials);
            Check(!world.TakeLightingChanges() && !world.HasPendingUpdates(),"Unrelated material changed the voxel field");
            manager->UnregisterGIVoxelMaterial(unrelated);
            const auto removedFrame=manager->CaptureRenderMaterialFrameData({});
            Check(!removedFrame.voxelMaterials->count(&unrelated) && unrelatedFrame.voxelMaterials->count(&unrelated),
                "Material removal mutated an older snapshot or retained the current entry");
            std::cout<<"[GIMaterialRadianceGPU] PASS: in-flight capture edits; 64 color/metal/roughness edits, immutable stable frames, linear texture mean, shared chunks, no geometry invalidation, capture, bake, page planning or template growth"<<std::endl;
            VansTexture mask;const uint32_t maskPixel=0x8040c080u;
            mask.LoadFromMemory(command,&maskPixel,sizeof(maskPixel),1,1,VK_FORMAT_R8G8B8A8_UNORM);
            const auto publishMaterials=[&]()
            {
                manager->FlushMaterialPayload(material);
                const auto frame=manager->CaptureRenderMaterialFrameData({&material});
                for(auto* field:{&world,gi.GetGIWorld()})field->ApplyMaterials(frame.voxelMaterials);
            };
            const auto compareFresh=[&](const char* label)
            {
                drain();
                VansGIWorld fresh;fresh.Initialize(device,scene,settings.world,false,settings.placement.maxRaysPerFrame,scene.GetMaterialManager()->GetGIVoxelMaterialInputs());
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(fresh.HasPendingUpdates())
                {
                    fresh.PrepareUpdates();
                    Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Material refresh oracle begin failed");
                    fresh.RecordUpdates(command);Submit(device,command);
                    Check(std::chrono::steady_clock::now()<deadline,"Material refresh oracle timed out");
                }
                float difference=0;
                for(const auto* field:{&world,gi.GetGIWorld()})for(float x:{0.f,40.f})for(bool color:{false,true})
                    difference=std::max(difference,glm::length(sample(x-2,12,.125f,color,field)-sample(x-2,12,.125f,color,&fresh)));
                Check(difference<.001f,std::string("Refreshed coverage differs from a fresh field: ")+label+" difference="+std::to_string(difference));
                Check(world.SourceTemplateCount()==templates && gi.GetGIWorld()->SourceTemplateCount()==nativeTemplates,
                    "Material coverage edits accumulated template variants");
                std::cout<<"[GIMaterialCoverageGPU] "<<label<<" freshDifference="<<difference<<std::endl;
            };
            Check(manager->ApplyMaterialParameter(material,"albedo",glm::vec3(.2f,.7f,.4f)) &&
                manager->ApplyMaterialParameter(material,"metallic",0.f),"Coverage fixture radiance reset failed");
            publishMaterials();drain();
            const auto opaque=sample(-2,12);
            material.m_BaseColorTexture=&mask;
            Check(manager->ApplyMaterialParameter(material,"alphaTest",true) &&
                manager->ApplyMaterialParameter(material,"alphaCutoff",.75f),"Alpha coverage edit failed");
            publishMaterials();
            {
                CaptureSubmissionObserver observer;observer.rejectSubmit=true;observer.ignoredFence=command.m_CommandBufferFinishSubmitFence;pump();
                Check(observer.submissions==2 && observer.waits==0 && sample(-2,12)==opaque &&
                    !world.SourcesDirty() && !gi.NeedsWorldSourceRebuild(),"Rejected material capture damaged the published field or waited");
            }
            Check(world.SourceTemplateCount()==templates && gi.GetGIWorld()->SourceTemplateCount()==nativeTemplates,
                "Rejected material capture changed the template cache");
            // 改变输入后才重试被拒绝的批次；后台持有旧输入时连续拖动只合并最新值。
            Check(manager->ApplyMaterialParameter(material,"alphaCutoff",.8f),"Retry coverage edit failed");publishMaterials();
            const auto capturesBefore=world.CpuWorkStats().captureSubmissions;
            {
                CaptureSubmissionObserver observer;observer.holdReady=true;observer.ignoredFence=command.m_CommandBufferFinishSubmitFence;pump();
                for(uint32_t edit=0;edit<16;++edit)
                {
                    Check(manager->ApplyMaterialParameter(material,"alphaCutoff",edit%2?.25f:.75f),"Continuous cutoff edit failed");
                    if(edit==7)Check(manager->ApplyMaterialParameter(material,"albedo",glm::vec3(.9f,.1f,.4f)),
                        "In-flight coverage color edit failed");
                    publishMaterials();pump();
                    Check(sample(-2,12)==opaque,"Unfinished coverage capture replaced the old GPU field");
                    if(edit==7)for(const auto* field:{&world,gi.GetGIWorld()})
                        Check(glm::length(sample(-2,12,.125f,true,field)-glm::vec4(textureColor*glm::vec3(.9f,.1f,.4f),material.m_BasePBRParam.m_roughness))<.0001f,
                            "Unfinished coverage capture delayed an independent color update to the old field");
                }
                Check(observer.submissions==2 && observer.waits==0,"Continuous edits accumulated captures or waited on a fence");
            }
            compareFresh("coalesced-alpha-accepted");
            Check(world.CpuWorkStats().captureSubmissions==capturesBefore+2,
                "Continuous edits did not reduce to the active and latest capture");
            for(uint32_t edit=0;edit<12;++edit)
            {
                material.m_BaseColorTexture=edit%3==0?&colorTexture:&mask;
                Check(manager->ApplyMaterialParameter(material,"alphaTest",edit%4!=0) &&
                    manager->ApplyMaterialParameter(material,"alphaCutoff",edit%2?.75f:.25f),"Repeated coverage edit failed");
                publishMaterials();compareFresh("alpha-texture-cycle");
            }
            Check(manager->ApplyMaterialParameter(material,"alphaCutoff",.3f),"Cancelled coverage edit failed");publishMaterials();
            {
                CaptureSubmissionObserver observer;observer.holdReady=true;observer.ignoredFence=command.m_CommandBufferFinishSubmitFence;pump();refresh({});
            }
            drain();refresh({0,40});compareFresh("cancel-delete-reload");
            std::cout<<"[GIMaterialCoverageGPU] PASS: alpha enable/cutoff, texture replacement, shared chunks, failed capture retention, latest-value coalescing, cancellation/delete/reload, fresh-field equivalence and bounded template count"<<std::endl;
            material.m_BaseColorTexture=nullptr;material.m_BasePBRParam=savedParams;material.m_MaterialIndex=savedIndex;
            material.m_AlphaTestEnabled=false;material.m_AlphaCutoff=.5f;
            manager->FlushMaterialPayload(material);
        }
        // Native PCG Leaves use their own alpha clip, even when ordinary PBR alphaTest is off.
        {
            using Value=Vans::VansSerializedValue;
            auto leafRoot=Value::Object({{"materialType",Value::String("tree")},
                {"parameters",Value::Object({{"leaf",Value::Object({{"alphaClip",Value::Float(.75f)}})}})}});
            const auto leafEdit=Vans::BuildRuntimeMaterialPreviewChange("Leaf.mat",leafRoot,"/asset/parameters/leaf/alphaClip");
            const bool projected=leafEdit.parameters.size()==1 && leafEdit.parameters[0].parameterPath=="leaf/alphaClip";
            auto* previewManager=scene.GetMaterialManager();
            const auto previousIndex=material.m_MaterialIndex;
            material.m_MaterialIndex=0;
            previewManager->m_GlobalPBRParamData.resize(1);
            previewManager->m_GlobalTreeLeafParamData.resize(1);
            VansMaterialLiveEditService liveEdit;
            const bool applied=liveEdit.ApplyMaterialParameter(&scene,materialId.ToString(),"leaf/alphaClip",.75f);
            const auto nativeLeafFrame=previewManager->CaptureRenderMaterialFrameData({&material});
            VansTreeLeafParamsGPU nativeLeafPayload;
            Check(nativeLeafFrame.treeLeaf.bytes.size()==sizeof(nativeLeafPayload),"Leaf frame payload size");
            std::memcpy(&nativeLeafPayload,nativeLeafFrame.treeLeaf.bytes.data(),sizeof(nativeLeafPayload));
            std::cout<<"[GILeafEditDiagnostic] projected="<<projected<<" applied="<<applied
                <<" nativeCutoff="<<nativeLeafPayload.scattering.w<<std::endl;
            Check(projected && applied && nativeLeafPayload.scattering.w==.75f,
                "Authored leaf cutoff edit did not reach both the native leaf frame and GI input");
            auto& authoredLeaf=*Vans::FindObjectField(*Vans::FindObjectField(leafRoot,"parameters"),"leaf");
            Vans::SetSerializedObjectField(authoredLeaf,"subsurfaceColor",Value::Array({Value::Float(.2f),Value::Float(.4f),Value::Float(.6f)}));
            Vans::SetSerializedObjectField(authoredLeaf,"transmission",Value::Float(.3f));
            Vans::SetSerializedObjectField(authoredLeaf,"wrap",Value::Float(.2f));
            Vans::SetSerializedObjectField(authoredLeaf,"scatterRoughness",Value::Float(.4f));
            Vans::SetSerializedObjectField(authoredLeaf,"specularScale",Value::Float(.8f));
            const auto fullLeafEdit=Vans::BuildRuntimeMaterialPreviewChange("Leaf.mat",leafRoot);
            Check(fullLeafEdit.parameters.size()==6,"Full tree preview did not emit its six leaf fields");
            for(const auto& edit:fullLeafEdit.parameters)
                Check(liveEdit.ApplyMaterialParameter(&scene,materialId.ToString(),edit.parameterPath,edit.value),"Full tree live preview failed");
            Check(material.m_TreeLeafParams.subsurfaceColorAndStrength==glm::vec4(.2f,.4f,.6f,.3f) &&
                material.m_TreeLeafParams.scattering==glm::vec4(.2f,.4f,.8f,.75f),"Native tree preview lost a leaf field");
            const auto previewLeaf=[&](float cutoff)
            {
                auto* parameters=Vans::FindObjectField(leafRoot,"parameters");
                auto* leaf=Vans::FindObjectField(*parameters,"leaf");
                Vans::SetSerializedObjectField(*leaf,"alphaClip",Value::Float(cutoff));
                const auto change=Vans::BuildRuntimeMaterialPreviewChange("Leaf.mat",leafRoot,"/asset/parameters/leaf/alphaClip");
                Check(change.parameters.size()==1,"Single leaf edit expanded to a full material refresh");
                for(const auto& edit:change.parameters)
                    Check(liveEdit.ApplyMaterialParameter(&scene,materialId.ToString(),edit.parameterPath,edit.value),
                        "Leaf live-edit service rejected the authoring projection");
                const auto frame=previewManager->CaptureRenderMaterialFrameData({&material});
                VansTreeLeafParamsGPU payload;
                std::memcpy(&payload,frame.treeLeaf.bytes.data(),sizeof(payload));
                Check(payload.scattering.w==cutoff && frame.voxelMaterials->at(&material).leafAlphaCutoff==cutoff,
                    "Native and GI snapshots disagree after a leaf edit");
            };
            VansTexture leafMask;const uint32_t pixel=0x0000ff00u;
            leafMask.LoadFromMemory(command,&pixel,sizeof(pixel),1,1,VK_FORMAT_R8G8B8A8_UNORM);
            material.m_BaseColorTexture=&leafMask;
            material.m_AlphaTestEnabled=false;material.m_AlphaCutoff=.5f;
            previewLeaf(.5f);
            plant=std::make_shared<Vans::VansPlantTypeAsset>(*plant);
            plant->variants.front().parts.front().kind=Vans::VansPlantPartKind::Leaves;
            submitNewPlant();drain();
            for(const auto* field:{&world,gi.GetGIWorld()})
            {
                const auto leafHit=sample(-2,12,.125f,false,field);
                std::cout<<"[GILeafCoverageDiagnostic] alpha=0 nativeLeafClip=0.5 pbrAlphaTest=0 distance="
                    <<leafHit.x<<" transmission="<<leafHit.y<<" valid="<<leafHit.z<<std::endl;
                Check(leafHit.z==1 && leafHit.x==12 && leafHit.y==1,
                    "PCG tree leaves discarded by the native alpha test still attenuate GIWorld");
            }
            auto* leafManager=scene.GetMaterialManager();
            const auto publishLeafInputs=[&]()
            {
                leafManager->FlushMaterialPayload(material);
                const auto frame=leafManager->CaptureRenderMaterialFrameData({&material});
                for(auto* field:{&world,gi.GetGIWorld()})field->ApplyMaterials(frame.voxelMaterials);
                drain();
                return frame.voxelMaterials;
            };
            const auto templatesBefore=world.SourceTemplateCount();
            VansTexture halfLeaf;const uint32_t halfPixel=0x8000ff00u;
            halfLeaf.LoadFromMemory(command,&halfPixel,sizeof(halfPixel),1,1,VK_FORMAT_R8G8B8A8_UNORM);
            material.m_BaseColorTexture=&halfLeaf;previewLeaf(.75f);
            const auto priorLeafInputs=publishLeafInputs();
            for(const auto* field:{&world,gi.GetGIWorld()})
            {
                const auto value=sample(-2,12,.125f,false,field);
                Check(value.z==1 && value.x==12 && value.y==1,"Leaf cutoff ignored its own authored threshold");
            }
            previewLeaf(.25f);
            const auto acceptedLeafInputs=publishLeafInputs();
            Check(priorLeafInputs!=acceptedLeafInputs && priorLeafInputs->at(&material).leafAlphaCutoff==.75f,
                "Leaf coverage input changed in an older submitted material snapshot");
            for(const auto* field:{&world,gi.GetGIWorld()})
            {
                const auto value=sample(-2,12,.125f,false,field);
                Check(value.z==1 && value.x==12 && value.y>0 && value.y<1,
                    "Accepted native leaf coverage did not publish porous extinction");
            }
            Check(world.SourceTemplateCount()==templatesBefore,"Leaf coverage edit appended a permanent template");
            // The same material's trunk ignores leaf alpha, matching the native per-part draw flag.
            material.m_BaseColorTexture=&leafMask;
            plant=std::make_shared<Vans::VansPlantTypeAsset>(*plant);
            plant->variants.front().parts.front().kind=Vans::VansPlantPartKind::Trunk;
            submitNewPlant();drain();
            for(const auto* field:{&world,gi.GetGIWorld()})
            {
                const auto value=sample(-2,12,.125f,false,field);
                Check(value.z==1 && value.x>1.5f && value.x<2.5f,
                    "Leaf alpha was incorrectly applied to an opaque trunk sharing its material");
            }
            material.m_BaseColorTexture=nullptr;submitNewPlant();drain();
            Vans::SetSerializedObjectField(leafRoot,"parameters",Value::Object({}));
            const auto defaults=Vans::BuildRuntimeMaterialPreviewChange("Leaf.mat",leafRoot);
            Check(defaults.parameters.size()==6 && defaults.textures.empty(),"Leaf removal did not project canonical defaults");
            for(const auto& edit:defaults.parameters)
                Check(liveEdit.ApplyMaterialParameter(&scene,materialId.ToString(),edit.parameterPath,edit.value),"Leaf default restoration failed");
            std::memcpy(&nativeLeafPayload,nativeLeafFrame.treeLeaf.bytes.data(),sizeof(nativeLeafPayload));
            Check(material.m_TreeLeafParams.scattering.w==.5f && nativeLeafPayload.scattering.w==.75f,
                "Leaf default restoration changed a published frame");
            Check(!liveEdit.ApplyMaterialParameter(&scene,materialId.ToString(),"leaf/alphaClip",std::numeric_limits<float>::quiet_NaN()) &&
                material.m_TreeLeafParams.scattering.w==.5f,"Invalid leaf edit partially changed the material");
            material.m_MaterialIndex=previousIndex;
            std::cout<<"[GILeafEditGPU] PASS: authoring projection, live-edit service, native/GI frame agreement, cutoff queries, defaults and immutable frames"<<std::endl;
            std::cout<<"[GILeafCoverageGPU] PASS: native Leaves alpha, independent cutoff, immutable input, same-slot refresh, shared-material opaque trunk"<<std::endl;
        }
        VerifyCompletedFrameWork(device,scene,settings);
    }

    void VerifyTerrainFootprint(VansVKDevice& device)
    {
        auto* manager=VansVKDescriptorManager::GetInstance();auto& command=device.GetImmediateGraphicsCommandBuffer();
        VansComputeShader shader;shader.SetName("GIWorldTextureFootprint");
        shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(shader.InitShader(device.GetLogicDevice(),"EngineAssets/Shaders/GIWorld",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldTexture.comp"}}),"Terrain footprint shader load failed");
        VansPipelineProgramDesc description{};description.name="GIWorldTextureFootprint";description.kind=VansPipelineProgramKind::Compute;description.pushConstantSize=16;
        shader.SetPipelineProgramDesc(description);shader.SetPushConstant(16);
        for(const glm::uvec2 size:{glm::uvec2(1,1),glm::uvec2(37,23),glm::uvec2(1024,513)})
        {
            std::vector<uint32_t> source(size_t(size.x)*size.y);
            for(size_t i=0;i<source.size();++i)source[i]=uint32_t(i*2654435761u+12345u);
            VansTexture texture;texture.LoadFromMemory(command,source.data(),source.size()*4,int(size.x),int(size.y),VK_FORMAT_R8G8B8A8_UNORM);
            VansVKBuffer output;const size_t bytes=128*128*4*sizeof(glm::vec4);
            VkDescriptorSetLayout layout=VK_NULL_HANDLE;std::vector<VkDescriptorSet> sets;
            auto release=[&](){manager->DestroyDescriptorSet(sets);manager->ReleaseDescriptorSetLayout(layout);output.DestroyVulkanBuffer(device.GetLogicDevice());};
            bool passed=true;
            try
            {
                Check(output.CreatVulkanBuffer(device.GetLogicDevice(),bytes,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&output.PersistentMap(),"Terrain footprint readback failed");
                Check(manager->CreateDesciptorSetLayout({{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
                    {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},layout)&&manager->AllocateDescriptorSet({layout},sets),"Terrain footprint descriptors failed");
                manager->BeginDescriptorUpdate();manager->WriteImageDescriptor(sets[0],0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    {{texture.GetImage().GetSampler(),texture.GetImage().GetImageView(),texture.GetImage().GetImageLayout()}});
                manager->WriteBufferDescriptor(sets[0],1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{{output.GetNativeBuffer(),0,bytes}});manager->UpdateDescriptorSets();
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Terrain footprint begin failed");
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                command.EnsureComputeShader(shader,{layout});const glm::uvec4 extent(128,128,4,0);command.DispatchCompute(shader,16,16,1,sets,&extent,sizeof(extent));
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                output.InvalidateMappedRange(0,bytes);const auto* pixels=static_cast<const glm::vec4*>(output.GetMappedPtr());
                for(int y=0;y<128;++y)for(int x=0;x<128;++x)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                {
                    const int sx=std::clamp(int(std::floor((x+.5)*size.x/128.0-.5))+dx,0,int(size.x)-1),
                        sy=std::clamp(int(std::floor((y+.5)*size.y/128.0-.5))+dy,0,int(size.y)-1);
                    passed=passed && glm::packUnorm4x8(pixels[(y*128+x)*4+dy*2+dx])==source[sy*size.x+sx];
                }
                release();
            }
            catch(...){release();throw;}
            Check(passed,"GPU terrain corner capture differs from original RGBA8 source");
            std::cout<<"[GITerrainColorGPU] live footprint "<<size.x<<"x"<<size.y<<" 65536 corners exact PASS"<<std::endl;
        }
    }

    void VerifyProbeTracePolicy(VansVKDevice& device)
    {
        constexpr uint32_t rayCount = 64;
        auto* manager = VansVKDescriptorManager::GetInstance();
        struct Resources
        {
            VansVKDevice& device;
            VansVKDescriptorManager* manager;
            std::array<VansVKBuffer,16> buffers;
            std::vector<VkDescriptorSetLayout> layouts;
            std::vector<VkDescriptorSet> sets;
            ~Resources()
            {
                device.WaitForDevice();
                manager->DestroyDescriptorSet(sets);
                for (auto layout : layouts) manager->ReleaseDescriptorSetLayout(layout);
                for (auto& buffer : buffers) buffer.DestroyVulkanBuffer(device.GetLogicDevice());
            }
        } resources{device,manager};
        auto& buffers = resources.buffers;
        GIWorldParameters world{};
        world.height = glm::uvec4(0);
        world.table = {64,2,256,0}; world.grid = {1,16,2,3}; world.center = {0,0,0,1};
        world.voxelMin = glm::vec4(-4); world.voxelMax = glm::vec4(4);
        std::array<GIWorldPageGPU,64> pages;
        for (int level = 0; level < 2; ++level)
        {
            uint32_t slot = GIWorldHash({0,0,0,level}) & 63u;
            while (pages[slot].data.x != ~0u) slot = (slot+1u) & 63u;
            pages[slot].key = {0,0,0,level}; pages[slot].data = {uint32_t(level)*512u,1,~0u,~0u};
        }
        // 细层为空，粗层为蓝色不透明表面，查询精度必须改变真实命中。
        std::array<GIWorldVoxel,1024> voxels{};
        for (size_t i = 512; i < voxels.size(); ++i)
            voxels[i] = {0x10000u,GIWorldEncodeNormal({0,1,0}) << 16u};
        const glm::vec4 blue(0,0,1,1), zero(0);
        std::array<glm::uvec4,rayCount+kGIMaximumRegionCount/4> scatter{};
        std::array<float,rayCount> distances{};
        using Half4 = std::array<uint16_t,4>;
        std::array<Half4,rayCount> half{};
        std::array<glm::uvec4,3> state{}, work{};
        work[0] = {1,rayCount,0,0};
        std::array<glm::uvec4,11> layout{};
        layout[0] = {0,1,4,0}; layout[1].z = 9; layout[6] = {1,1,1,0}; layout[7].w = 1;
        const void* input[] = {&world,&zero,&zero,pages.data(),voxels.data(),&blue,&blue,scatter.data(),
            distances.data(),half.data(),half.data(),half.data(),half.data(),state.data(),work.data(),layout.data()};
        const size_t sizes[] = {sizeof(world),16,16,sizeof(pages),sizeof(voxels),16,16,sizeof(scatter),
            sizeof(distances),sizeof(half),sizeof(half),sizeof(half),sizeof(half),sizeof(state),sizeof(work),sizeof(layout)};
        for (size_t i = 0; i < buffers.size(); ++i)
            Check(buffers[i].CreatVulkanBuffer(device.GetLogicDevice(),sizes[i],VK_FORMAT_UNDEFINED,
                (i == 0 ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
                buffers[i].SetBufferData(input[i],0,sizes[i]),"Trace policy fixture allocation failed");
        resources.layouts.resize(3);
        Check(manager->CreateDesciptorSetLayout({},resources.layouts[0]),"Empty trace set failed");
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (uint32_t binding : {0u,1u,2u,3u,10u,13u,14u,15u})
            bindings.push_back({binding,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,binding == 13 ? kGIMaximumRegionCount : 1u,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        Check(manager->CreateDesciptorSetLayout(bindings,resources.layouts[1]),"Trace output set failed");
        bindings.clear();
        for (uint32_t binding = 0; binding < 8; ++binding)
            bindings.push_back({binding,binding == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        Check(manager->CreateDesciptorSetLayout(bindings,resources.layouts[2]) &&
            manager->AllocateDescriptorSet(resources.layouts,resources.sets),"Trace world descriptors failed");
        manager->BeginDescriptorUpdate();
        const std::array<uint32_t,8> outputBindings{0,1,2,3,10,13,14,15};
        for (size_t i = 0; i < outputBindings.size(); ++i)
        {
            const VkDescriptorBufferInfo info{buffers[i+8].GetNativeBuffer(),0,sizes[i+8]};
            manager->WriteBufferDescriptor(resources.sets[1],outputBindings[i],VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                std::vector<VkDescriptorBufferInfo>(outputBindings[i] == 13 ? kGIMaximumRegionCount : 1,info));
        }
        for (uint32_t i = 0; i < 8; ++i)
            manager->WriteBufferDescriptor(resources.sets[2],i,i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                {{buffers[i].GetNativeBuffer(),0,sizes[i]}});
        manager->UpdateDescriptorSets();
        for (size_t i : {size_t(8),size_t(11),size_t(12)}) Check(buffers[i].PersistentMap(),"Trace output mapping failed");
        VansComputeShader shader; shader.SetName("GIProbeTracePolicyContract");
        shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(shader.InitShader(device.GetLogicDevice(),"EngineAssets/Shaders/GIWorld",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldTrace.comp"}}),
            "Production trace shader failed");
        shader.SetPushConstant(sizeof(VansGIProbeDispatchParameters));
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        uint32_t cases = 0;
        for (float spacing : {.125f,1.0f,16.0f,64.0f}) for (bool sparse : {false,true})
            for (bool coarse : {false,true}) for (bool hardware : {false,true})
        {
            world.table.w = hardware ? 1u : 0u;
            layout[0].x = sparse ? 1u : 0u;
            layout[9] = glm::floatBitsToUint(glm::vec4(.5f,.5f,.5f,spacing));
            work[0].w = sparse ? 1u : 0u;
            distances.fill(hardware ? 4.0f : -1.0f);
            half.fill(Half4{0,0x3c00,0,0x3c00});
            Check(buffers[0].SetBufferData(&world,0,sizeof(world)) && buffers[8].SetBufferData(distances.data(),0,sizeof(distances)) &&
                buffers[11].SetBufferData(half.data(),0,sizeof(half)) && buffers[12].SetBufferData(half.data(),0,sizeof(half)) &&
                buffers[14].SetBufferData(work.data(),0,sizeof(work)) && buffers[15].SetBufferData(layout.data(),0,sizeof(layout)),
                "Trace policy fixture update failed");
            VansGIProbeDispatchParameters parameters{};
            parameters.gridParams = glm::vec4(glm::vec3(spacing),0);
            parameters.dispatchParams = {1,1,1,rayCount}; parameters.regionParams = {.5f,.5f,.5f,0};
            parameters.traceParams = {8,coarse ? 2.0f : 0.0f,0,0};
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Trace policy begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            command.EnsureComputeShader(shader,resources.layouts);
            command.DispatchCompute(shader,1,1,1,resources.sets,&parameters,sizeof(parameters));
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});
            Submit(device,command);
            for (size_t i : {size_t(8),size_t(11),size_t(12)}) buffers[i].InvalidateMappedRange(0,sizes[i]);
            const auto* hits = static_cast<const float*>(buffers[8].GetMappedPtr());
            const auto* emission = static_cast<const Half4*>(buffers[11].GetMappedPtr());
            const auto* material = static_cast<const Half4*>(buffers[12].GetMappedPtr());
            for (size_t ray = 0; ray < rayCount; ++ray)
            {
                const float expected = coarse ? .0001f : (hardware ? 4.0f : -1.0f);
                Check(std::abs(hits[ray]-expected) < 1e-6f,"Probe density changed explicit query precision or nearest hit");
                const Half4 expectedMaterial = coarse ? Half4{0,0,0x3c00,0x3c00} : (hardware ? Half4{0,0x3c00,0,0x3c00} : Half4{});
                Check(material[ray] == expectedMaterial && emission[ray] == (!coarse && hardware ? Half4{0,0x3c00,0,0x3c00} : Half4{}),
                    "Nearest-hit material/emission disagreed: spacing=" + std::to_string(spacing) + " sparse=" + std::to_string(sparse) +
                    " coarse=" + std::to_string(coarse) + " hardware=" + std::to_string(hardware) + " ray=" + std::to_string(ray) +
                    " material=" + std::to_string(material[ray][0]) + "," + std::to_string(material[ray][1]) + "," +
                    std::to_string(material[ray][2]) + "," + std::to_string(material[ray][3]) +
                    " emission=" + std::to_string(emission[ray][0]) + "," + std::to_string(emission[ray][1]) + "," +
                    std::to_string(emission[ray][2]) + "," + std::to_string(emission[ray][3]));
            }
            ++cases;
        }
        std::cout << "[GITracePolicyGPU] PASS production shader cases=" << cases
            << " raysPerCase=" << rayCount << " uniform/adaptive density, explicit voxel precision and hardware-hit preservation\n";
    }

    void VerifyProbeLightingPublication(VansVKDevice& device)
    {
        constexpr uint32_t rays=64;
        auto* manager=VansVKDescriptorManager::GetInstance();
        struct Resources
        {
            VansVKDevice& device; VansVKDescriptorManager* manager;
            std::array<VansVKBuffer,19> buffers;
            VansVKImage depth;
            std::vector<VkDescriptorSetLayout> layouts;
            std::vector<VkDescriptorSet> sets;
            ~Resources()
            {
                device.WaitForDevice();manager->DestroyDescriptorSet(sets);
                for(auto layout:layouts)manager->ReleaseDescriptorSetLayout(layout);
                for(auto& buffer:buffers)buffer.DestroyVulkanBuffer(device.GetLogicDevice());
                depth.DestroyVulkanImage(device.GetLogicDevice());
            }
        } resources{device,manager};
        auto& buffers=resources.buffers;
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        const auto half=[](glm::vec4 v){return glm::uvec2(glm::packHalf2x16(glm::vec2(v)),glm::packHalf2x16({v.z,v.w}));};
        GIWorldParameters world{};
        world.table={64,2,256,0};world.grid={1,16,1,1};world.center={0,0,0,1};
        world.voxelMin=glm::vec4(0);world.voxelMax={16,8,8,0};
        std::array<GIWorldPageGPU,64> pages;
        std::array<GIWorldVoxel,1024> voxels{};
        const glm::vec4 zero(0), material(.5f,.5f,.5f,1);
        std::array<glm::vec4,rays+kGIMaximumRegionCount/4> scatter{};
        std::array<float,rays> distances{};
        std::array<glm::uvec2,rays> normals{},radiance{},emission{},albedo{};
        std::array<glm::uvec4,3> state{},work{};
        work[0]={1,rays,0,0};
        std::array<glm::uvec4,11> layout{};
        layout[0]={0,1,4,0};layout[1].z=9;layout[6]={1,1,1,0};layout[7].w=1;
        VansRenderLightFrameData lights;lights.prepared=true;lights.punctualShadowMapWidth=32;
        lights.directionalLights.resize(1);
        auto& sun=lights.directionalLights[0];sun={};sun.m_Direction={1,0,0};sun.m_Color={2,3,4};sun.m_Intensity=1;
        std::vector<uint8_t> lightData;
        Check(VansLightManager::BuildRenderLightBufferPayload(lights,{},{},lightData),"Sun fixture light payload failed");
        std::array<VansLightCookieGPU,VANS_LIGHT_COOKIE_COUNT> cookies{};
        const void* initial[]={&world,&zero,&zero,pages.data(),voxels.data(),&material,&material,scatter.data(),
            distances.data(),normals.data(),radiance.data(),emission.data(),albedo.data(),state.data(),work.data(),layout.data(),lightData.data(),&zero,cookies.data()};
        const size_t sizes[]={sizeof(world),16,16,sizeof(pages),sizeof(voxels),16,16,sizeof(scatter),sizeof(distances),
            sizeof(normals),sizeof(radiance),sizeof(emission),sizeof(albedo),sizeof(state),sizeof(work),sizeof(layout),lightData.size(),16,sizeof(cookies)};
        for(size_t i=0;i<buffers.size();++i)
            Check(buffers[i].CreatVulkanBuffer(device.GetLogicDevice(),sizes[i],VK_FORMAT_UNDEFINED,
                i==0||i==17?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&
                buffers[i].SetBufferData(initial[i],0,sizes[i]),"Sun fixture buffer failed");
        VansTexture image,cube,rectEmission,blackCookie,whiteCookie;
        rectEmission.InitTextureArray(command,8,8,2,VK_FORMAT_R8G8B8A8_SRGB,true);
        const std::array<uint8_t,4> cookieBlack{0,0,0,255},cookieWhite{255,255,255,0};
        blackCookie.LoadFromMemory(command,cookieBlack.data(),4,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        whiteCookie.LoadFromMemory(command,cookieWhite.data(),4,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        const auto uploadEmission=[&](std::array<uint8_t,4> color)
        {
            std::array<uint8_t,8*8*4> pixels{};
            for(size_t i=0;i<pixels.size();++i)pixels[i]=color[i%4];
            Check(rectEmission.UpdateArrayLayerFromPixels(command,pixels.data(),8,8,1,Vans::VansColorEncoding::SRGB,VK_FILTER_NEAREST),
                "Probe emission upload failed");
        };
        uploadEmission({255,255,255,255});
        const std::array<float,32*32> black{};
        image.LoadFromMemory(command,black.data(),sizeof(black),32,32,VK_FORMAT_R32_SFLOAT);
        Check(cube.InitTextureWithoutData(command,1,1,1,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_VIEW_TYPE_CUBE,false,false),
            "Sun fixture cube failed");
        Check(resources.depth.CreateVulkanImage(device.GetLogicDevice(),{1,1,1},VK_FORMAT_D32_SFLOAT,1,1,VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT,VK_SAMPLE_COUNT_1_BIT,false,false,true,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,true),
            "Sun fixture comparison depth failed");
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Sun depth transition begin failed");
        resources.depth.SetImageMemoryBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {resources.depth.GetImage(),0,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
             VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,VK_IMAGE_ASPECT_DEPTH_BIT});
        Submit(device,command);
        resources.layouts.resize(3);
        Check(manager->CreateDesciptorSetLayout({{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {37,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {38,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VANS_LIGHT_COOKIE_COUNT,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {39,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},resources.layouts[0]),"Sun global layout failed");
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(uint32_t binding:{0u,1u,2u,3u,10u,13u,14u,15u})
            bindings.push_back({binding,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,binding==13?kGIMaximumRegionCount:1u,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        for(uint32_t binding:{4u,8u,9u,11u,12u,16u})
            bindings.push_back({binding,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,binding==9?2u:(binding==11||binding==12)?kGIMaximumRegionCount:1u,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        Check(manager->CreateDesciptorSetLayout(bindings,resources.layouts[1]),"Sun shading layout failed");
        bindings.clear();
        for(uint32_t i=0;i<8;++i)bindings.push_back({i,i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        Check(manager->CreateDesciptorSetLayout(bindings,resources.layouts[2])&&manager->AllocateDescriptorSet(resources.layouts,resources.sets),
            "Sun world descriptors failed");
        manager->BeginDescriptorUpdate();
        manager->WriteBufferDescriptor(resources.sets[0],1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{{buffers[16].GetNativeBuffer(),0,sizes[16]}});
        manager->WriteBufferDescriptor(resources.sets[0],37,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,{{buffers[17].GetNativeBuffer(),0,sizes[17]}});
        manager->WriteBufferDescriptor(resources.sets[0],39,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{{buffers[18].GetNativeBuffer(),0,sizes[18]}});
        auto& initialCookie=blackCookie.GetImage();
        manager->WriteImageDescriptor(resources.sets[0],38,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            std::vector<VkDescriptorImageInfo>(VANS_LIGHT_COOKIE_COUNT,{initialCookie.GetSampler(),initialCookie.GetImageView(),initialCookie.GetImageLayout()}));
        const std::array<uint32_t,8> shadingBindings{0,1,2,3,10,13,14,15};
        for(size_t i=0;i<shadingBindings.size();++i)
            manager->WriteBufferDescriptor(resources.sets[1],shadingBindings[i],VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                std::vector<VkDescriptorBufferInfo>(i==5?kGIMaximumRegionCount:1,{buffers[8+i].GetNativeBuffer(),0,sizes[8+i]}));
        for(uint32_t binding:{4u,8u,9u,11u,12u,16u})
        {
            auto& img=binding==4?cube.GetImage():binding==9?resources.depth:binding==16?rectEmission.GetImage():image.GetImage();
            manager->WriteImageDescriptor(resources.sets[1],binding,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                std::vector<VkDescriptorImageInfo>(binding==9?2:(binding==11||binding==12)?kGIMaximumRegionCount:1,{img.GetSampler(),img.GetImageView(),img.GetImageLayout()}));
        }
        for(uint32_t i=0;i<8;++i)manager->WriteBufferDescriptor(resources.sets[2],i,i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{buffers[i].GetNativeBuffer(),0,sizes[i]}});
        manager->UpdateDescriptorSets();
        for(size_t i:{size_t(8),size_t(10)})Check(buffers[i].PersistentMap(),"Sun readback mapping failed");
        VansComputeShader shader;shader.SetName("GIWorldSunPublicationContract");
        shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(shader.InitShader(device.GetLogicDevice(),"EngineAssets/Shaders/GIWorld",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldLighting.comp"}}),
            "Production world lighting shader failed");
        shader.SetPushConstant(sizeof(VansGIProbeDispatchParameters));
        VansGIProbeDispatchParameters params{};
        params.gridParams={1,1,1,0};params.dispatchParams={1,1,1,rays};params.regionParams={.5f,.5f,.5f,0};
        params.lightingParams={10,100,0,0};params.traceParams={64,0,0,0};
        for(uint32_t test=0;test<9;++test)
        {
            pages.fill(GIWorldPageGPU{});voxels.fill(GIWorldVoxel{});
            const auto addPage=[&](int x,uint32_t offset,bool ready){uint32_t slot=GIWorldHash({x,0,0,0})&63u;
                while(pages[slot].data.x!=~0u)slot=(slot+1)&63u;pages[slot].key={x,0,0,0};pages[slot].data={offset,ready?1u:0u,~0u,~0u};};
            addPage(0,0,true);addPage(test==7?5:1,512,test==1||test==7||test==8);
            world.table.z=test==3?1:256;world.grid.y=test==7?2.0f:16.0f;world.voxelMax.x=test==7?48.0f:16.0f;
            if(test==2)voxels[3]={0x10000u,GIWorldEncodeNormal({-1,0,0})<<16u};
            if(test==7)voxels[512]={0x10000u,GIWorldEncodeNormal({-1,0,0})<<16u};
            if(test==8)voxels[1].optical=voxels[2].optical=glm::packHalf1x16(1.0f);
            normals.fill(half({test==4?-1.0f:1.0f,0,0,1}));albedo.fill(half(material));radiance.fill(half({0,0,0,1}));
            for(size_t i=kGIMaximumRegionCount/4;i<scatter.size();++i)scatter[i]={0,0,0,-1};
            sun.m_Intensity=test==5?0.0f:1.0f;
            for(auto& matrix:sun.m_ShadowMatrix){matrix=glm::mat4(0);matrix[3]=test==6?glm::vec4(0,0,.5f,1):glm::vec4(4,0,0,1);}
            Check(VansLightManager::BuildRenderLightBufferPayload(lights,{},{},lightData),"Sun payload update failed");
            for(size_t i:{size_t(0),size_t(3),size_t(4),size_t(7),size_t(9),size_t(10),size_t(12),size_t(16)})
                Check(buffers[i].SetBufferData(i==16?lightData.data():initial[i],0,sizes[i]),"Sun case upload failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Sun lighting begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            command.EnsureComputeShader(shader,resources.layouts);command.DispatchCompute(shader,1,1,1,resources.sets,&params,sizeof(params));
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            buffers[8].InvalidateMappedRange(0,sizes[8]);buffers[10].InvalidateMappedRange(0,sizes[10]);
            const auto* result=static_cast<const glm::uvec2*>(buffers[10].GetMappedPtr());
            const float validity=test==0?-float(GIProbeVoxelPage):test==3?-float(GIProbeVoxelBudget):1.0f;
            const glm::vec3 expected=test==1?glm::vec3(1,1.5f,2)*.31830988618f:
                test==8?glm::vec3(1,1.5f,2)*(.31830988618f*std::exp(-1.749f)):glm::vec3(0);
            std::cout<<"[GIWorldSunGPU] case="<<test<<" radiance="<<glm::unpackHalf2x16(result[32].x).x
                <<" validity="<<glm::unpackHalf2x16(result[32].y).y<<" expectedValidity="<<validity<<std::endl;
            Check(std::memcmp(buffers[8].GetMappedPtr(),distances.data(),sizeof(distances))==0,"Sun failure overwrote primary geometry");
            for(size_t ray=0;ray<rays;++ray)
            {
                const auto xy=glm::unpackHalf2x16(result[ray].x),zw=glm::unpackHalf2x16(result[ray].y);
                Check(zw.y==(ray<32?0.0f:validity),"World sun unknown was published as valid lighting, or a zero-contribution query blocked publication");
                if(ray>=32&&validity>0)Check(glm::all(glm::lessThan(glm::abs(glm::vec3(xy,zw.x)-expected),glm::vec3(.001f))),
                    "World sun radiance, opaque clipping, environment extent or transmittance differs");
            }
        }
        std::cout<<"[GIWorldSunGPU] PASS: production lighting, missing/step failures, empty/opaque paths, zero-contribution skip, far occluder, optical depth and immutable hit distances\n";

        // 直接调度硬件/GIWorld 两条生产入口；相同位置、材质和灯光下只改变实际纹理内容与 Cookie 参数。
        VansComputeShader hardware;hardware.SetName("GIHardwareLightConsumptionContract");
        hardware.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(hardware.InitShader(device.GetLogicDevice(),"EngineAssets/Shaders/GIPointLight",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIPointLight.comp"}}),
            "Production hardware lighting shader failed");
        hardware.SetPushConstant(sizeof(params));
        params.regionParams=glm::vec4(0); // 单探针位于原点；解析期望值使用同一固定命中位置。
        layout[5]=glm::floatBitsToUint(glm::vec4(1,1,1,0));
        Check(buffers[15].SetBufferData(layout.data(),0,sizeof(layout)),"Probe lighting layout upload failed");
        world.table.z=256;world.grid.y=16;world.voxelMax.x=16;voxels.fill(GIWorldVoxel{});
        pages.fill(GIWorldPageGPU{});
        for(int x=0;x<2;++x){uint32_t slot=GIWorldHash({x,0,0,0})&63u;while(pages[slot].data.x!=~0u)slot=(slot+1)&63u;
            pages[slot].key={x,0,0,0};pages[slot].data={uint32_t(x*512),1,~0u,~0u};}
        normals.fill(half({0,0,1,1}));
        sun.m_Direction={0,0,1};sun.m_Color={1,1,1};
        for(auto& matrix:sun.m_ShadowMatrix){matrix=glm::mat4(0);matrix[3]={4,0,0,1};}
        lights.pointLights.resize(1);auto& point=lights.pointLights[0];point={};
        point.m_Position={0,0,2};point.m_Color={1,1,1};point.m_Radius=10;point.m_ShadowMetaIndex=~0u;
        // 最后一盏聚光灯验证扩容后的读取，以及紧邻的 Rect/Cookie 槽位没有串位。
        lights.spotLights.resize(VANS_MAX_SPOT_LIGHTS);auto& spot=lights.spotLights.back();spot={};
        spot.m_Position={0,0,2};spot.m_Direction={0,0,1};spot.m_Color={1,1,1};spot.m_Radius=10;
        spot.m_InnerCutOff=.2f;spot.m_OuterCutOff=.4f;spot.m_ShadowMetaIndex=~0u;
        lights.rectLights.resize(1);auto& rect=lights.rectLights[0];rect={};
        rect.m_Position={0,0,2};rect.m_Normal={0,0,-1};rect.m_Right={1,0,0};rect.m_Up={0,1,0};
        rect.m_HalfWidth=rect.m_HalfHeight=.5f;rect.m_Range=10;rect.m_Color={1,1,1};
        rect.m_ShadowMetaIndex=~0u;rect.m_TextureSlot=-1;
        uint32_t cases=0;
        for(bool useWorld:{false,true})for(uint32_t kind=0;kind<4;++kind)for(uint32_t test=0;test<(kind==3?11u:8u);++test)
        {
            sun.m_Intensity=kind==0?1.0f:0.0f;point.m_Intensity=kind==1?1.0f:0.0f;
            spot.m_Intensity=kind==2?1.0f:0.0f;rect.m_Intensity=kind==3?1.0f:0.0f;
            rect.m_TextureSlot=test>=8?1.0f:-1.0f;
            cookies.fill(VansLightCookieGPU{});auto& cookie=cookies[VansLightCookieOffset(kind)+(kind==2?VANS_MAX_SPOT_LIGHTS-1:0)];
            cookie.worldToLight[3].z=-2;
            cookie.options={test==0||test>=8?0.0f:test==5?.25f:1.0f,float(kind),test==7?1.0f:0.0f,test==3||test==4?1.0f:0.0f};
            if(test==6||test==7)cookie.scaleOffset.w=2;
            auto& cookieImage=(test==2||test==4||test==6||test==7?whiteCookie:blackCookie).GetImage();
            manager->BeginDescriptorUpdate();
            manager->WriteImageDescriptor(resources.sets[0],38,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                std::vector<VkDescriptorImageInfo>(VANS_LIGHT_COOKIE_COUNT,{cookieImage.GetSampler(),cookieImage.GetImageView(),cookieImage.GetImageLayout()}));
            manager->UpdateDescriptorSets();
            glm::vec3 tint(1);
            if(test==8){uploadEmission({255,0,0,255});tint={1,0,0};}
            if(test==9){uploadEmission({0,0,255,255});tint={0,0,1};}
            if(test==10){uploadEmission({128,128,128,255});tint=glm::vec3(.2158605f);}
            const float cookieFactor=test==1||test==4||test==6?0.0f:test==5?.75f:1.0f;
            const float base=kind==0?.5f:kind==3?.08f:.32f;
            const glm::vec3 expected=tint*(base*.31830988618f*cookieFactor);
            radiance.fill(half({0,0,0,1}));
            Check(VansLightManager::BuildRenderLightBufferPayload(lights,{},{},lightData),"Probe light payload update failed");
            for(size_t i:{size_t(0),size_t(3),size_t(4),size_t(9),size_t(10),size_t(18)})
                Check(buffers[i].SetBufferData(initial[i],0,sizes[i]),"Probe light input upload failed");
            Check(buffers[16].SetBufferData(lightData.data(),0,sizes[16]),"Probe light payload upload failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Probe light begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            auto& consumer=useWorld?shader:hardware;
            command.EnsureComputeShader(consumer,resources.layouts);command.DispatchCompute(consumer,1,1,1,resources.sets,&params,sizeof(params));
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            buffers[10].InvalidateMappedRange(0,sizes[10]);
            const auto* result=static_cast<const glm::uvec2*>(buffers[10].GetMappedPtr());
            for(size_t ray=0;ray<rays;++ray)
            {
                const auto xy=glm::unpackHalf2x16(result[ray].x),zw=glm::unpackHalf2x16(result[ray].y);
                Check(zw.y==(ray<32?0.0f:1.0f),"Light texture changed ray validity");
                if(ray>=32&&glm::any(glm::greaterThan(glm::abs(glm::vec3(xy,zw.x)-expected),glm::vec3(.001f))))
                {
                    std::cout<<"[GILightConsumptionGPU] failed world="<<useWorld<<" kind="<<kind<<" test="<<test
                        <<" actual="<<xy.x<<","<<xy.y<<","<<zw.x<<" expected="<<expected.x<<","<<expected.y<<","<<expected.z<<std::endl;
                    Check(false,"Production GI ignored or misapplied light emission/Cookie");
                }
            }
            ++cases;
        }
        std::cout<<"[GILightConsumptionGPU] PASS cases="<<cases<<" hardware/world, four light kinds, disabled/black/white/alpha/strength/outside/repeat Cookies, array slot and red/blue/sRGB frame updates\n";
    }

    void VerifyNearestHitQueries(VansVKDevice& device)
    {
        auto* manager=VansVKDescriptorManager::GetInstance();
        struct Resources
        {
            VansVKDevice& device; VansVKDescriptorManager* manager;
            std::array<VansVKBuffer,8> buffers;
            VkDescriptorSetLayout layout=VK_NULL_HANDLE;
            std::vector<VkDescriptorSet> sets;
            ~Resources()
            {
                device.WaitForDevice();manager->DestroyDescriptorSet(sets);
                if(layout)manager->ReleaseDescriptorSetLayout(layout);
                for(auto& buffer:buffers)buffer.DestroyVulkanBuffer(device.GetLogicDevice());
            }
        } resources{device,manager};
        auto& buffers=resources.buffers;
        Vans::VansTerrainAsset terrain;terrain.width=terrain.height=2;terrain.heights={0,65535,0,65535};
        terrain.settings.terrainSize=16;terrain.settings.maxHeight=2;terrain.settings.heightOffset=-1;
        GIWorldHeightData heights;std::string error;Check(heights.Build(terrain,error),error);
        GIWorldParameters world{};world.terrain=heights.parameters;world.terrain.w=1;
        world.height={2,2,uint32_t(heights.levels.size()),1};world.table={64,2,1,0};
        world.center={0,0,0,1};world.grid={1,32,1,1};
        world.voxelMin={-8,-8,-8,0};world.voxelMax={8,0,0,0};
        std::copy(heights.levels.begin(),heights.levels.end(),world.heightLevels.begin());
        std::array<GIWorldPageGPU,64> pages;
        auto insertPage=[&](const glm::ivec4& key,uint32_t ready)
        {
            uint32_t slot=GIWorldHash({key.x,key.y,key.z,key.w})&63u;
            while(pages[slot].data.x!=~0u)slot=(slot+1u)&63u;
            pages[slot].key=key;pages[slot].data={0,ready,~0u,~0u};return slot;
        };
        const uint32_t nearPage=insertPage({-1,-1,-1,0},1);
        insertPage({0,-1,-1,0},0); // 地形后方未驻留，已确认的近处遮挡不能受它影响。
        std::array<GIWorldVoxel,512> voxels{};
        auto voxel=[&](uint32_t x,uint32_t z)->GIWorldVoxel& { return voxels[x+8u*(7u+8u*z)]; };
        for(auto cell:{glm::uvec2(2,0),glm::uvec2(6,1),glm::uvec2(2,3),glm::uvec2(2,4),
            glm::uvec2(2,5),glm::uvec2(7,6),glm::uvec2(4,7)})
            voxel(cell.x,cell.y)={0x10000u,GIWorldEncodeNormal({-1,0,0})<<16u};
        for(uint32_t z:{3u,4u,5u})voxel(1,z).optical=glm::packHalf2x16({1,0});
        for(uint32_t x=1;x<7;++x)voxel(x,6).optical=glm::packHalf2x16({1,0});
        const glm::vec4 plantColor(.1f,.2f,.9f,1),terrainColor(.9f,.2f,.1f,1);
        std::array<glm::vec4,40> results{};
        const void* input[]={&world,heights.heights.data(),heights.ranges.data(),pages.data(),voxels.data(),&plantColor,&terrainColor,results.data()};
        const size_t sizes[]={sizeof(world),heights.heights.size()*4,heights.ranges.size()*8,sizeof(pages),sizeof(voxels),16,16,sizeof(results)};
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(uint32_t i=0;i<8;++i)
        {
            Check(buffers[i].CreatVulkanBuffer(device.GetLogicDevice(),sizes[i],VK_FORMAT_UNDEFINED,
                i==0?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&
                buffers[i].SetBufferData(input[i],0,sizes[i]),"Nearest-hit fixture allocation failed");
            bindings.push_back({i,i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        }
        Check(manager->CreateDesciptorSetLayout(bindings,resources.layout)&&manager->AllocateDescriptorSet({resources.layout},resources.sets),
            "Nearest-hit fixture descriptors failed");
        manager->BeginDescriptorUpdate();
        for(uint32_t i=0;i<8;++i)manager->WriteBufferDescriptor(resources.sets[0],i,
            i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{{buffers[i].GetNativeBuffer(),0,sizes[i]}});
        manager->UpdateDescriptorSets();Check(buffers[7].PersistentMap(),"Nearest-hit fixture mapping failed");
        VansComputeShader shader;shader.SetName("GINearestHitContract");
        shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(shader.InitShader(device.GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GINearestHitContract.comp"}}),
            "Nearest-hit fixture shader failed");
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        for(uint32_t scenario=0;scenario<3;++scenario)
        {
            world.table.z=scenario==0?1u:256u;pages[nearPage].data.y=scenario==2?0u:1u;
            Check(buffers[0].SetBufferData(&world,0,sizeof(world))&&buffers[3].SetBufferData(pages.data(),0,sizeof(pages)),
                "Nearest-hit fixture update failed");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Nearest-hit fixture begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            command.EnsureComputeShader(shader,{resources.layout});command.DispatchCompute(shader,1,1,1,resources.sets);
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            buffers[7].InvalidateMappedRange(0,sizeof(results));std::memcpy(results.data(),buffers[7].GetMappedPtr(),sizeof(results));
            for(uint32_t i=0;i<8;++i)
                std::cout<<"[GINearestHitGPU] scenario="<<scenario<<" ray="<<i<<" terrainT="<<results[i*5].x<<" terrainValid="<<results[i*5].y
                    <<" voxelT="<<results[i*5+1].x<<" voxelValid="<<results[i*5+1].z<<" mergedT="<<results[i*5+2].x
                    <<" mergedValid="<<results[i*5+2].z<<" T="<<results[i*5+2].y<<" event="<<results[i*5+3].w<<std::endl;
            for(uint32_t i=0;i<8;++i)
            {
                const bool expectedValid=scenario==1 || (scenario==0 && (i==0 || i==3 || i==4 || i==5));
                const auto hit=results[i*5+2];
                Check((hit.z==1)==expectedValid,"Nearest-hit merge discarded a proved near hit or accepted an unchecked prefix");
                if(!expectedValid){Check(hit.w!=0,"Unknown prefix lost its failure reason");continue;}
                const float distance=i==4?.5f:(i==5?1.f:(i==7?3.f:((i==0||i==3)?1.f:5.f)));
                const float transmission=i==6?std::exp(-5.f):((i>=3&&i<=5)?std::exp(-distance):1.f);
                Check(std::abs(hit.x-distance)<3e-5f && std::abs(hit.y-transmission)<3e-5f && hit.w==0,
                    "Nearest-hit distance, bounded transmission or completed failure mask is wrong");
                if(i!=4 && i!=5)Check(results[i*5+4]==((i==0||i==3||i==7)?plantColor:terrainColor),
                    "Nearest-hit material does not belong to the winning surface");
                const float event=results[i*5+3].w;
                if(i==3||i==5||i==6)Check(std::abs(event-.5f)<1e-5f && event<hit.x,"Scattering event crossed its nearest surface");
                else Check(event<0,"A scattering event was created beyond the clipped segment");
            }
        }
        std::cout<<"[GINearestHitGPU] PASS: checked prefixes, near/far/tied blockers, clipped transmission, scatter cutoff and unavailable pages\n";
    }

    void VerifyWorldQueries(VansVKDevice& device)
    {
        bool lodBoundariesPassed=true,emptySkipPassed=true;
        Vans::VansTerrainAsset terrain;terrain.width=terrain.height=2;terrain.heights={0,65535,0,65535};
        terrain.settings.terrainSize=16;terrain.settings.maxHeight=2;terrain.settings.heightOffset=-1;
        GIWorldHeightData heights;std::string error;Check(heights.Build(terrain,error),error);
        GIWorldParameters parameters;parameters.terrain=heights.parameters;parameters.terrain.w=1;
        parameters.voxelMin=glm::vec4(-16);parameters.voxelMax=glm::vec4(16);
        parameters.height={2,2,uint32_t(heights.levels.size()),1};parameters.table={64,2,4,0};parameters.center={0,0,0,1};parameters.grid={1,1,1,1};
        std::copy(heights.levels.begin(),heights.levels.end(),parameters.heightLevels.begin());
        std::array<GIWorldPageGPU,64> pages;
        pages[GIWorldHash({0,0,0,0})&63].key={0,0,0,0};pages[GIWorldHash({0,0,0,0})&63].data={0,1,~0u,~0u};
        auto slot=GIWorldHash({1,0,0,0})&63;while(pages[slot].data.x!=~0u)slot=(slot+1)&63;
        pages[slot].key={1,0,0,0};pages[slot].data={512,0,~0u,~0u};
        std::array<GIWorldVoxel,1024> voxels{};voxels[0].optical=glm::packHalf1x16(1.f);
        voxels[1].optical=0x10000;voxels[1].surface=GIWorldEncodeNormal({-1,0,0})<<16;
        glm::vec4 material(.5,.5,.5,1);std::array<glm::vec4,80> results{};
        std::vector<glm::vec4> terrainColors(128*128,material);
        const void* data[]={&parameters,heights.heights.data(),heights.ranges.data(),pages.data(),voxels.data(),&material,terrainColors.data(),results.data()};
        size_t sizes[]={sizeof(parameters),heights.heights.size()*4,heights.ranges.size()*8,sizeof(pages),sizeof(voxels),16,terrainColors.size()*sizeof(glm::vec4),sizeof(results)};
        std::array<VansVKBuffer,8> buffers;std::vector<VkDescriptorSetLayoutBinding> bindings;
        VkDescriptorSetLayout layout=VK_NULL_HANDLE;std::vector<VkDescriptorSet> sets;
        auto* manager=VansVKDescriptorManager::GetInstance();
        for(uint32_t i=0;i<8;++i)
        {
            Check(buffers[i].CreatVulkanBuffer(device.GetLogicDevice(),sizes[i],VK_FORMAT_UNDEFINED,
                (i?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&
                buffers[i].SetBufferData(data[i],0,sizes[i]),"Query fixture upload failed");
            bindings.push_back({i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
        }
        Check(manager->CreateDesciptorSetLayout(bindings,layout)&&manager->AllocateDescriptorSet({layout},sets),"Query fixture descriptors failed");
        manager->BeginDescriptorUpdate();for(uint32_t i=0;i<8;++i)manager->WriteBufferDescriptor(sets[0],i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,{{buffers[i].GetNativeBuffer(),0,sizes[i]}});manager->UpdateDescriptorSets();
        {
            VansComputeShader shader;shader.SetName("GIWorldContract");
            shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
            Check(shader.InitShader(device.GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldContract.comp"}}),"Query fixture shader failed");
            auto& command=device.GetImmediateGraphicsCommandBuffer();Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Query fixture begin failed");
            command.EnsureComputeShader(shader,{layout});command.DispatchCompute(shader,2,1,1,sets);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
            Check(buffers[7].PersistentMap(),"Query map failed");buffers[7].InvalidateMappedRange(0,sizeof(results));std::memcpy(results.data(),buffers[7].GetMappedPtr(),sizeof(results));
            for(auto color:{glm::vec3(0),glm::vec3(.1f,.7f,.2f),glm::vec3(1)})
            {
                const glm::vec4 coloredMaterial(color,1);
                Check(buffers[5].SetBufferData(&coloredMaterial,0,sizeof(coloredMaterial)),"Scatter material upload failed");
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Scatter material begin failed");
                barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                command.EnsureComputeShader(shader,{layout});command.DispatchCompute(shader,2,1,1,sets);
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                std::array<glm::vec4,80> colored;buffers[7].InvalidateMappedRange(0,sizeof(colored));std::memcpy(colored.data(),buffers[7].GetMappedPtr(),sizeof(colored));
                const float transmission=std::exp(-.8f);
                Check(glm::all(glm::lessThan(glm::abs(glm::vec3(colored[64])-color*(1-transmission)),glm::vec3(.001f))) &&
                    glm::all(glm::lessThan(glm::abs(glm::vec3(colored[65])-(glm::vec3(transmission)+color*(1-transmission))),glm::vec3(.001f))),
                    "Free-flight scattering lost leaf color, violated white-furnace energy, or attenuated outgoing transmission twice");
                std::cout<<"[GIScatteringGPU] albedo="<<color.x<<","<<color.y<<","<<color.z<<" scattered="<<colored[64].x<<","<<colored[64].y<<","<<colored[64].z<<" T="<<colored[64].w<<std::endl;
            }
            Check(buffers[5].SetBufferData(&material,0,sizeof(material)),"Restore scatter material failed");
            for(bool raised:{true,false})
            {
                GIWorldHeightData::Patch changed;
                Check(heights.ApplyPatch(0,0,1,1,std::vector<uint8_t>(2,raised?255:0),changed,error),"GPU height patch failed");
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Patch begin failed");
                barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
                for(auto span:changed.heights)command.UpdateBuffer(buffers[1].GetNativeBuffer(),span.x*4,span.y*4,heights.heights.data()+span.x);
                for(auto span:changed.ranges)command.UpdateBuffer(buffers[2].GetNativeBuffer(),span.x*8,span.y*8,heights.ranges.data()+span.x);
                barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                command.EnsureComputeShader(shader,{layout});command.DispatchCompute(shader,2,1,1,sets);
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                std::array<glm::vec4,80> after;buffers[7].InvalidateMappedRange(0,sizeof(after));std::memcpy(after.data(),buffers[7].GetMappedPtr(),sizeof(after));
                std::cout<<"[GIHeightPatchGPU] raised="<<raised<<" cornerT="<<after[0].x<<" valid="<<after[0].z<<" centerT="<<after[2].x<<" valid="<<after[2].z<<std::endl;
                Check(after[0].z==1 && std::abs(after[0].x-(raised?1.f:3.f))<.001f && after[2].z==1 && std::abs(after[2].x-(raised?1.5f:2.f))<.001f,
                    "Incremental GPU min/max did not follow terrain raise/undo");
                Check(after[36].z==1 && std::abs(after[36].x-(raised?1.65685425f:9.89949494f))<.003f &&
                    after[38].z==1 && std::abs(after[38].x-(raised?5.65685425f:8.48528137f))<.003f,
                    "Height quadratic lost its first root or tangent intersection");
            }
            std::cout<<"[GIWorldGPU] partial height/min-max upload raise and undo passed"<<std::endl;
            auto colorParameters=parameters;colorParameters.terrain.w=128;
            Check(buffers[0].SetBufferData(&colorParameters,0,sizeof(colorParameters)),"Terrain color parameters failed");
            GIWorldTerrainColorData colorData;std::array<std::vector<uint32_t>,2> corners;
            corners[0].assign(128*128*4,255u);corners[1].assign(128*128*4,0u);
            Check(colorData.Build(2,2,std::move(corners),{material,glm::vec4(.1f,.8f,.2f,1)},error),"Terrain color fixture failed");
            for(bool painted:{true,false})
            {
                GIWorldTerrainColorData::Patch changed;
                Check(colorData.ApplyPatch(0,0,0,1,1,painted?std::vector<uint8_t>{0,255,0,0}:std::vector<uint8_t>{255,0,0,0},changed,error),"Terrain color patch rejected");
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Terrain color patch begin failed");
                barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                size_t uploaded=0;
                for(auto span:changed.spans){command.UpdateBuffer(buffers[6].GetNativeBuffer(),span.x*16,span.y*16,colorData.colors.data()+span.x);uploaded+=span.y*16;}
                barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                command.EnsureComputeShader(shader,{layout});command.DispatchCompute(shader,2,1,1,sets);
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                buffers[7].InvalidateMappedRange(0,sizeof(results));const auto* sampled=static_cast<const glm::vec4*>(buffers[7].GetMappedPtr());
                Check(glm::all(glm::lessThan(glm::abs(sampled[77]-colorData.colors[32*128+32]),glm::vec4(1e-6f))),"Height hit ignored same-submission partial material upload");
                std::cout<<"[GITerrainColorGPU] paint="<<painted<<" uploadBytes="<<uploaded<<" surface color="<<sampled[77].x<<","<<sampled[77].y<<","<<sampled[77].z<<" PASS"<<std::endl;
            }
            Check(buffers[0].SetBufferData(&parameters,0,sizeof(parameters)),"Restore terrain color parameters failed");

            auto coarseParameters=parameters;coarseParameters.grid={1,128,2,3};
            std::array<GIWorldPageGPU,64> coarsePages;
            const auto fineSlot=GIWorldHash({0,0,0,0})&63;
            coarsePages[fineSlot].key={0,0,0,0};coarsePages[fineSlot].data={0,1,~0u,~0u};
            auto coarseSlot=GIWorldHash({0,0,0,1})&63;while(coarsePages[coarseSlot].data.x!=~0u)coarseSlot=(coarseSlot+1)&63;
            coarsePages[coarseSlot].key={0,0,0,1};coarsePages[coarseSlot].data={512,1,~0u,~0u};
            std::array<GIWorldVoxel,1024> homogeneous;
            for(auto& voxel:homogeneous){voxel.optical=glm::packHalf1x16(.5f);voxel.surface=0;}
            Check(buffers[0].SetBufferData(&coarseParameters,0,sizeof(coarseParameters)) &&
                buffers[4].SetBufferData(homogeneous.data(),0,sizeof(homogeneous)),"Footprint fixture upload failed");
            for(bool missing:{false,true})
            {
                coarsePages[coarseSlot].data.y=missing?0u:1u;
                Check(buffers[3].SetBufferData(coarsePages.data(),0,sizeof(coarsePages)),"Footprint page upload failed");
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Footprint begin failed");
                barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                command.EnsureComputeShader(shader,{layout});command.DispatchCompute(shader,2,1,1,sets);
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                std::array<glm::vec4,80> sampled;buffers[7].InvalidateMappedRange(0,sizeof(sampled));std::memcpy(sampled.data(),buffers[7].GetMappedPtr(),sizeof(sampled));
                if(missing)Check(sampled[71].w<0,"Unpublished scattering path became known lighting");
                else Check(sampled[71].w==6 && std::abs(sampled[70].x-.5f*(1-std::exp(-3.f)))<.001f,
                    "Scattering LOD changed homogeneous optical energy");
                Check(sampled[52].z==0 && sampled[53].x==float(GIProbeVoxelBudget) && sampled[58].z==0,
                    "Fine queries unexpectedly changed precision or ignored the four-step budget");
                Check(sampled[76].z==1 && sampled[76].x==6 && std::abs(sampled[76].y-std::exp(-3.f))<.0001f,
                    "Additional scatter steps did not complete the same fine optical path while retaining the primary budget");
                for(uint32_t ray:{27u,28u,30u})
                    if(missing)Check(sampled[ray*2].z==0 && sampled[ray*2+1].x==float(GIProbeVoxelPage),"Missing coarse page fabricated visible sky");
                    else Check(sampled[ray*2].z==1 && std::abs(sampled[ray*2].y-std::exp(-3.f))<.0001f,
                        "Coarse footprint failed within the same step budget or changed homogeneous optical depth");
            }
            std::cout<<"[GIWorldGPU] footprint LOD: fine budget exhaustion, coarse optical conservation, last-level clamp and missing-page rejection PASS"<<std::endl;
            // 扩展后的最高级与中间缺级由真实查询 shader 处理，不用 CPU 模拟结果。
            parameters.height.w=0;parameters.grid={.25f,256,16,32769};parameters.table.y=2;
            parameters.voxelMin=glm::vec4(-65536);parameters.voxelMax=glm::vec4(65536);
            pages.fill(GIWorldPageGPU{});
            const auto rootSlot=GIWorldHash({0,0,0,15})&63;
            pages[rootSlot].key={0,0,0,15};pages[rootSlot].data={0,1,~0u,~0u};
            auto adaptiveFineSlot=GIWorldHash({0,0,0,0})&63;while(pages[adaptiveFineSlot].data.x!=~0u)adaptiveFineSlot=(adaptiveFineSlot+1)&63;
            pages[adaptiveFineSlot].key={0,0,0,0};pages[adaptiveFineSlot].data={512,0,~0u,~0u};
            voxels.fill(GIWorldVoxel{glm::packHalf1x16(.5f),0});
            Check(buffers[4].SetBufferData(voxels.data(),0,sizeof(voxels)),"Adaptive root voxel upload failed");
            for(uint32_t state=0;state<3;++state)
            {
                pages[rootSlot].data.y=state==1?0:1;parameters.grid.w=state==2?1.f:32769.f;
                Check(buffers[0].SetBufferData(&parameters,0,sizeof(parameters)) && buffers[3].SetBufferData(pages.data(),0,sizeof(pages)),
                    "Adaptive root metadata upload failed");
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Adaptive root query begin failed");
                barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                command.EnsureComputeShader(shader,{layout});command.DispatchCompute(shader,2,1,1,sets);
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                std::array<glm::vec4,80> sampled;buffers[7].InvalidateMappedRange(0,sizeof(sampled));std::memcpy(sampled.data(),buffers[7].GetMappedPtr(),sizeof(sampled));
                for(uint32_t ray:{26u,27u,28u,30u})
                    if(state==0)Check(sampled[ray*2].z==1 && std::abs(sampled[ray*2].y-std::exp(-3.f))<.0001f,
                        "Adaptive highest level lost optical energy or failed pending fine fallback");
                    else Check(sampled[ray*2].z==0 && sampled[ray*2+1].x==float(state==1?GIProbeVoxelPage:GIProbeVoxelCoverage),
                        "Missing adaptive root was interpreted as known sky");
            }
            std::cout<<"[GIWorldGPU] adaptive root: level 15, sparse level mask, pending fine fallback, root missing/uncovered rejection PASS"<<std::endl;
            {
                VansComputeShader boundary;boundary.SetName("GIWorldLodBoundary");
                boundary.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
                Check(boundary.InitShader(device.GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldLodBoundary.comp"}}),"LOD boundary shader load failed");
                VansPipelineProgramDesc description{};description.name="GIWorldLodBoundary";description.kind=VansPipelineProgramKind::Compute;description.pushConstantSize=32;
                boundary.SetPipelineProgramDesc(description);boundary.SetPushConstant(32);
                for(int axis=0;axis<3;++axis)for(int sign:{-1,1})for(int mode=0;mode<6;++mode)
                {
                    parameters.grid={.25f,mode==2?62.f:256.f,5,24};parameters.center={0,0,0,1};parameters.center[axis]=float(sign*8);
                    parameters.voxelMin=glm::vec4(-128);parameters.voxelMax=glm::vec4(128);parameters.table={64,2,64,0};
                    pages.fill(GIWorldPageGPU{});voxels.fill(GIWorldVoxel{});
                    auto insertPage=[&](int coordinate,int level,uint32_t address,bool ready)
                    {
                        GIWorldCell key{0,0,0,level};if(axis==0)key.x=coordinate;else if(axis==1)key.y=coordinate;else key.z=coordinate;
                        auto slot=GIWorldHash(key)&63;while(pages[slot].data.x!=~0u)slot=(slot+1)&63;
                        pages[slot].key={key.x,key.y,key.z,key.level};pages[slot].data={address,ready?1u:0u,~0u,~0u};
                    };
                    insertPage(sign>0?2:-3,4,0,mode!=3);
                    const uint32_t stride=axis==0?1u:(axis==1?8u:64u);
                    if(mode==2)
                    {
                        for(uint32_t cell=0;cell<512;++cell)voxels[cell].optical=glm::packHalf1x16(.25f);
                        insertPage(sign>0?4:-5,3,512,true);
                        voxels[512+(sign>0?2:5)*stride].optical=0x10000u;
                    }
                    else voxels[(sign>0?2:5)*stride].optical=mode==1?glm::packHalf1x16(.5f):0x10000u;
                    const float distance=mode==4?2.f:8.f;
                    std::array<glm::vec4,2> query{glm::vec4(.5f,.5f,.5f,distance),glm::vec4(0)};
                    query[0][axis]=float(sign*(mode==2?74:70));query[1][axis]=float(sign*(mode==2?-1:1));query[1].w=mode==5?4.f:0.f;
                    Check(buffers[0].SetBufferData(&parameters,0,sizeof(parameters)) && buffers[3].SetBufferData(pages.data(),0,sizeof(pages)) &&
                        buffers[4].SetBufferData(voxels.data(),0,sizeof(voxels)),"LOD boundary upload failed");
                    Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"LOD boundary begin failed");
                    barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                    command.EnsureComputeShader(boundary,{layout});command.DispatchCompute(boundary,2,1,1,sets,query.data(),sizeof(query));
                    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                    std::array<glm::vec4,2> sampled;buffers[7].InvalidateMappedRange(0,sizeof(sampled));std::memcpy(sampled.data(),buffers[7].GetMappedPtr(),sizeof(sampled));
                    bool passed=true;
                    for(uint32_t ray=0;ray<2;++ray)
                    {
                        const float expectedT=ray==1?1.f:(mode==1?std::exp(-2.f):(mode==2?std::exp(-1.f):1.f));
                        const float expectedDistance=mode==0||mode==5?2.f:(mode==2?4.f:distance);
                        if(mode==3)passed=passed&&sampled[ray].z==0 && sampled[ray].w==float(GIProbeVoxelPage);
                        else passed=passed&&sampled[ray].z==1&&std::abs(sampled[ray].x-expectedDistance)<.0001f&&std::abs(sampled[ray].y-expectedT)<.0001f;
                    }
                    lodBoundariesPassed=lodBoundariesPassed&&passed;
                    std::cout<<"[GIWorldLODBoundary] axis="<<axis<<" sign="<<sign<<" mode="<<mode<<" t="<<sampled[0].x<<" T="<<sampled[0].y
                        <<" valid="<<sampled[0].z<<" failure="<<sampled[0].w<<" pass="<<passed<<std::endl;
                }
                // 已驻留的稀疏砖不应让空走廊按每个体素耗尽预算；同时保留表面和消光积分。
                for(int sign:{-1,1})for(int mode=0;mode<4;++mode)
                {
                    parameters.grid={1,256,1,1};parameters.center={0,0,0,1};parameters.height.w=0;
                    parameters.voxelMin=glm::vec4(sign>0?0.f:-512.f,0,0,0);
                    parameters.voxelMax=glm::vec4(sign>0?512.f:0.f,8,8,0);parameters.table={64,64,256,0};
                    pages.fill(GIWorldPageGPU{});voxels.fill(GIWorldVoxel{});
                    const uint32_t occupied=mode==1?511u:uint32_t(sign>0?7:0);
                    const uint32_t group=mode==1?63u:uint32_t(sign>0?3:0);
                    glm::uvec2 occupancy(0);
                    if(mode!=0){voxels[occupied].optical=mode==3?glm::packHalf1x16(.5f):0x10000u;occupancy[group/32]|=1u<<(group%32);}
                    for(int brick=0;brick<64;++brick)
                    {
                        const int x=sign>0?brick:-brick-1;const GIWorldCell key{x,0,0,0};
                        auto slot=GIWorldHash(key)&63;while(pages[slot].data.x!=~0u)slot=(slot+1)&63;
                        pages[slot].key={x,0,0,0};pages[slot].data={0,1,occupancy.x,occupancy.y};
                    }
                    const float distance=mode<2?500.f:12.f;
                    const std::array<glm::vec4,2> query{glm::vec4(sign*.1f,.5f,.5f,distance),glm::vec4(float(sign),0,0,0)};
                    Check(buffers[0].SetBufferData(&parameters,0,sizeof(parameters)) && buffers[3].SetBufferData(pages.data(),0,sizeof(pages)) &&
                        buffers[4].SetBufferData(voxels.data(),0,sizeof(voxels)),"Sparse corridor upload failed");
                    Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Sparse corridor begin failed");
                    barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
                    command.EnsureComputeShader(boundary,{layout});command.DispatchCompute(boundary,2,1,1,sets,query.data(),sizeof(query));
                    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Submit(device,command);
                    std::array<glm::vec4,2> sampled;buffers[7].InvalidateMappedRange(0,sizeof(sampled));std::memcpy(sampled.data(),buffers[7].GetMappedPtr(),sizeof(sampled));
                    bool passed=true;
                    for(uint32_t ray=0;ray<2;++ray)
                        passed=passed && sampled[ray].z==1 && sampled[ray].w==0 &&
                            std::abs(sampled[ray].x-(mode==2?6.9f:distance))<.0001f &&
                            std::abs(sampled[ray].y-(mode==3 && ray==0?std::exp(-.5f):1.f))<.0001f;
                    emptySkipPassed=emptySkipPassed&&passed;
                    std::cout<<"[GIWorldEmptySkip] sign="<<sign<<" mode="<<mode<<" t="<<sampled[0].x<<" T="<<sampled[0].y
                        <<" valid="<<sampled[0].z<<" failure="<<sampled[0].w<<" pass="<<passed<<std::endl;
                }
            }
        }
        manager->DestroyDescriptorSet(sets);manager->ReleaseDescriptorSetLayout(layout);for(auto& buffer:buffers)buffer.DestroyVulkanBuffer(device.GetLogicDevice());
        for(uint32_t i=0;i<13;++i)std::cout<<"[GIWorldGPU] ray="<<i<<" t="<<results[i*2].x<<" T="<<results[i*2].y<<" valid="<<results[i*2].z<<std::endl;
        for(uint32_t i=20;i<26;++i)std::cout<<"[GIWorldGPU] farRay="<<i<<" t="<<results[i*2].x<<" T="<<results[i*2].y<<" valid="<<results[i*2].z<<std::endl;
        Check(results[40].z==1 && std::abs(results[40].x-510)<.002f && results[42].z==1 && std::abs(results[42].x-510)<.002f,
            "Far height rays lost the first intersection while crossing cells");
        Check(results[44].z==1 && std::abs(results[44].x-513)<.002f && std::abs(results[44].y-std::exp(-1.f))<.001f &&
            results[46].z==1 && std::abs(results[46].x-510)<.002f,"Far voxel rays stalled or lost optical depth/nearest hit");
        Check(results[48].z==1 && results[48].x==514 && results[50].z==1 && results[50].x==511,
            "Far grazing voxel rays exhausted the traversal budget");
        Check(results[0].z==1&&std::abs(results[0].x-3)<.001f,"Height clamped edge intersection incorrect");
        Check(results[2].z==1&&std::abs(results[2].x-2)<.001f,"Height bilinear intersection incorrect");
        Check(results[4].z==1&&std::abs(results[4].y-std::exp(-.8f))<.001f,"Vegetation optical depth incorrect");
        Check(results[6].z==1&&std::abs(results[6].x-2)<.001f&&std::abs(results[6].y-std::exp(-1.f))<.001f,"Whole-model opaque voxel nearest hit incorrect");
        Check(results[8].z==1&&results[8].y==1,"Known empty region was not visible");
        Check(results[10].z==0&&results[12].z==0,"Pending global pages outside detail radius or budget exhaustion were treated as sky");
        Check(results[11].w==float(GIProbeVoxelPage) && results[13].w==float(GIProbeVoxelBudget),
            "World query did not distinguish missing pages from traversal exhaustion");
        Check(results[14].z==1&&results[14].w==1,"Terrain backface flag missing");
        Check(results[16].z==1&&results[16].x==.25f&&results[16].y==1,
            "Nearly horizontal negative boundary ray exhausted its budget without crossing a cell");
        Check(results[18].z==1&&results[18].x==1&&results[19].y> -1,
            "Raster terrain below bilinear heightfield self-occludes the receiver");
        Check(results[20].z==1&&std::abs(results[20].x-.02f)<.001f&&results[21].y==-1.02f,
            "Non-terrain receiver was moved through world geometry");
        Check(results[23].y==-.8f&&results[25].y==-1.02f,
            "Terrain correction changed an above-surface or outside-terrain origin");
        Check(glm::all(glm::lessThan(glm::abs(results[26]-glm::vec4(1,1,.6f,.2f)),glm::vec4(.0001f))) &&
            std::abs(results[27].x-1)<.0001f,"GI boundary fades to black instead of sky");
        Check(glm::all(glm::lessThan(glm::abs(results[28]-glm::vec4(.6f,.4f,.2f,0)),glm::vec4(.0001f))) &&
            std::abs(results[29].x-.5f)<.0001f,"GI overlap blend loses energy or replaces valid black lighting");
        Check(glm::all(glm::lessThan(glm::abs(results[30]-glm::vec4(1,.25f,.2f,.6f)),glm::vec4(.0001f))),
            "GI back hemisphere, zero fade or region priority differs between consumers");
        Check(glm::all(glm::lessThan(glm::abs(results[32]-glm::vec4(.6f,.4f,0,0)),glm::vec4(.0001f))),
            "Unpublished cascade failed to fall back to a valid coarse volume or replaced valid black");
        Check(glm::all(glm::lessThan(glm::abs(results[34]-glm::vec4(.5f,0,1,0)),glm::vec4(.0001f))),
            "Unknown probe data fabricated unoccluded sky inside the volume");
        std::cout<<"[GIRegionBlendGPU] PASS: boundary continuity, overlapping volumes, black interior, back hemisphere and priority"<<std::endl;
        Check(glm::all(glm::lessThan(glm::abs(results[62]-glm::vec4(.4f,.6f,0,0)),glm::vec4(.0001f))) &&
            glm::all(glm::lessThan(glm::abs(results[63]-glm::vec4(.6f,.4f,.2f,0)),glm::vec4(.0001f))),
            "World bounce blend lost coarse support, injected sky/foreign domains or repeatedly attenuated boundary lighting");
        std::cout<<"[GIWorldBounceGPU] PASS: cross-level blend, unpublished/partial fallback, valid black, domain isolation, normalized boundary and unknown exterior"<<std::endl;
        std::cout<<"[GIScatteringGPU] opaque-path scattering="<<results[66].x<<" expected="<<.5f*(1-std::exp(-1.f))
            <<" hitDelta="<<results[67].w-2.f<<std::endl;
        Check(std::abs(results[66].x-.5f*(1-std::exp(-1.f)))<.001f && std::abs(results[67].w-2.f)<.0001f,
            "Scattering moved the opaque hit used by probe geometry/distance moments");
        Check(results[68]==glm::vec4(-1,.8f,1,1) && results[69]==glm::vec4(-1,-1,-1,0),
            "Fixed rays, empty space, long free flights or opaque starts generated leaf scattering");
        Check(std::abs(results[72].x-.5f)<.001f && results[72].y==1 && results[72].w==1 &&
            std::abs(results[72].z-std::exp(-.5f))<.001f && results[73].w==0 &&
            glm::all(glm::lessThan(glm::abs(glm::vec3(results[73])-glm::vec3(.1f,.7f,.2f)*std::exp(-.5f)*16.f),glm::vec3(.001f))),
            "Single scattering ignored incoming blocker, transmittance or sparse sampling probability");
        std::cout<<"[GIScatteringGPU] PASS: material response, white-furnace energy, opaque/fixed identity, incoming occlusion/transmittance, sparse estimator and LOD"<<std::endl;
        Check(results[74]==glm::vec4(17,17,17,.125f) && results[75]==glm::vec4(3,3,3,8),
            "Sparse scattering was clamped after probability compensation or changed its bounded work allocation");
        std::cout<<"[GIScatteringGPU] sparse clamp: selected radiance 17, expected mean 3, probability 1/8 and double voxel budget PASS"<<std::endl;
        Check(results[78] == glm::vec4(4,4,8,1),"Scattering query minimum was reinterpreted as probe density");
        Check(lodBoundariesPassed,"LOD crossing skipped parent geometry, fine entry, optical depth or unknown pages");
        Check(emptySkipPassed,"Resident empty blocks exhausted traversal budget or changed opaque/porous transport");
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
        struct MessengerLifetime
        {
            VkInstance instance;VkDebugUtilsMessengerEXT& handle;PFN_vkDestroyDebugUtilsMessengerEXT destroy;
            ~MessengerLifetime(){if(handle)destroy(instance,handle,nullptr);}
        } messengerLifetime{device->GetInstance(),messenger,destroyMessenger};
        auto& shaders = VansShaderManager::Get();
        shaders.RegisterRayTracingShader("RayTracingTest", "EngineAssets/Shaders/RayTracingTest", sizeof(VansGIProbeDispatchParameters));
        for (const char* name : {"GIPointLight", "GIVisibilityUpdate", "GIProbeState"})
            shaders.RegisterComputeShader(name, std::string("EngineAssets/Shaders/") + name, sizeof(VansGIProbeDispatchParameters));
        shaders.RegisterComputeShader("GIRTPreview", "EngineAssets/Shaders/GIRTPreview", sizeof(GIRTPreviewPushConstant));
        shaders.RegisterComputeShaderFile("GIWorldPreview", "EngineAssets/Shaders/GIWorld", "GIWorldPreview.comp", sizeof(GIRTPreviewPushConstant));
        shaders.RegisterComputeShader("AmbientSkyTransmittance", "EngineAssets/Shaders/AmbientSkyTransmittance", sizeof(VansAmbientSkyCachePushConstants));
        for (const char* name : {"SSGI", "SSGIProbeCache", "SSGITemporal"})
            shaders.RegisterComputeShader(name, std::string("EngineAssets/Shaders/") + name);
        shaders.RegisterComputeShader("SSGIAtrous", "EngineAssets/Shaders/SSGIAtrous", sizeof(VansSSGIAtrousPushConstants));
        shaders.RegisterComputeShader("GIReceiverVisibilityPrepare", "EngineAssets/Shaders/GIReceiverVisibilityPrepare", 32);
        shaders.RegisterComputeShader("GIReceiverVisibilityReproject", "EngineAssets/Shaders/GIReceiverVisibilityReproject", 80);
        shaders.RegisterComputeShader("GIReceiverVisibilityWorldCache", "EngineAssets/Shaders/GIReceiverVisibilityWorldCache", 112);
        Check(shaders.LoadAll(std::filesystem::current_path().generic_string() + "/",
            std::filesystem::current_path()/"Library/Artifacts/Shaders", device->GetLogicDevice()), "GI shader load failed");
        VerifyRuntimeTextureDimensions(*device);
        VerifyTreeShadowGpu(*device);
        VerifyAmbientSkyResourceOwner(*device);
        VerifySSGIResourceOwner(*device);
        VerifyArrayLayerUpload(*device);
        VerifyVideoLightingPublication(*device);
        VerifyRectLightMidtone(*device);
        VerifyColorUploadPaths(*device);
        VerifyRetainedColorEncoding(*device);
        VerifyIESArrayResources(*device);
        VerifyTerrainFootprint(*device);
        VerifyProbeTracePolicy(*device);
        VerifyProbeLightingPublication(*device);
        VerifyNearestHitQueries(*device);
        VerifyWorldQueries(*device);
        VerifyPcgInstancePublication(*device);
        {
            auto scene = std::make_unique<VansScene>();
            // 此测试只创建资源，不绑定或追踪几何；占位条目用于真实的布局容量。
            scene->GetBLASVertexBuffers().resize(1);
            scene->GetTLASInstanceData() = {0};
            scene->GetTLASInstanceMaterials() = {{0, 0.0f}};
            scene->GetTLASInstanceGIEmission() = {glm::vec4(0)};
            VansGISettings settings;

            settings.regions = {GIProbeRegionDesc{}};
            settings.regions[0].overrideGridDimensions = true;
            settings.regions[0].gridDimensions = glm::uvec3(2);
            settings.regions[0].raysPerProbe = 16;
            settings.regions.push_back(settings.regions[0]);
            settings.regions[1].stableId = 2;
            settings.regions[1].gridDimensions = glm::uvec3(3,2,2);
            settings.regions.push_back(settings.regions[0]);
            settings.regions[2].stableId = 3;
            settings.regions[2].enabled = false;
            NormalizeGISettings(settings);
            auto& gi = device->GetRayTracingContext();
            auto& command = device->GetImmediateGraphicsCommandBuffer();
            Check(device->DrainDeferredDeletesAfterDeviceIdle(), "Memory fixture retirement drain failed");
            ConsumerMemoryFixture consumers(*device);
            Check(gi.CreateRayTracingResource(device.get(), &command, scene.get(), settings, scene->GetMaterialManager()->GetGIVoxelMaterialInputs()), gi.GetResourceError());
            VerifyMemoryPublication(*device, *scene);
            const auto initialMemory = device->GetGIMemorySnapshot();
            consumers.Verify();
            auto* texture = gi.GetGIRegionIrradianceAtlas(0);
            Check(gi.IsReady() && texture, "Initial GI atlas missing");
            Check(!gi.GetGIRegionResolved(0)->worldOnly&&gi.GetGIRegionIrradianceHistoryBuffer(0)->GetNativeBuffer()==VK_NULL_HANDLE&&
                gi.GetGIRegionIrradianceHistoryBuffer(1)->GetNativeBuffer()==VK_NULL_HANDLE,"Hardware regions allocated unused component history");
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
                  Check(!gi.CreateRayTracingResource(device.get(), &command, scene.get(), resized, scene->GetMaterialManager()->GetGIVoxelMaterialInputs()), "Allocation failure was not reported"); }
                Check(createdViews >= 4 && injectedFailures == 1 && layoutCalls == 1u + attempt * 2u && candidateViews.empty(),
                    "Late failure did not reach its target or release candidate image views");
                Check(gi.IsReady() && gi.GetGIRegionIrradianceAtlas(0) == texture && gi.GetGIRTPreviewTexture(0) == preview &&
                    gi.GetGIRegionProbeStateBuffer(0)->GetNativeBuffer() == stateBuffer && gi.GetGIProbeLayoutBuffer().GetNativeBuffer() == layoutBuffer &&
                    GISettingsResourceLayoutEquals(gi.GetAppliedSettings(), settings) && !gi.GetResourceError().empty() &&
                    shaders.FindRayTracingShader("RayTracingTest")->GetRayTracingPipeline() == pipeline,
                    "Failed candidate replaced GI resources, settings or shared pipeline");
                VerifyAtlas(*device, command, originalImage);
                const auto afterFailure = device->GetGIMemorySnapshot();
                consumers.Verify();
                Check(afterFailure.ActiveAllocationBytes() == initialMemory.ActiveAllocationBytes() &&
                    afterFailure.retiredAllocationBytes == initialMemory.retiredAllocationBytes &&
                    afterFailure.publishedRetainedPeakBytes == initialMemory.publishedRetainedPeakBytes,
                    "Failed candidate changed published resource memory");
            }
            auto metadata = settings; metadata.maxIndirectRadiance = .75f; metadata.regions[0].normalBias = .12f;
            gi.UpdateGISettings(device.get(), &command, metadata);
            Check(gi.GetAppliedSettings().maxIndirectRadiance == .75f && gi.GetAppliedSettings().regions[0].normalBias == .12f &&
                gi.GetGIRegionIrradianceAtlas(0) == texture, "Parameter edit rebuilt the GI atlas or lost settings");
            VerifyAtlas(*device, command, originalImage);
            const auto otherAtlas = gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage();
            const auto previousStateBuffer = gi.GetGIRegionPreviousProbeStateBuffer(0)->GetNativeBuffer();
            const auto unchangedTrace = gi.GetGIRegionResolved(1)->trace;
            metadata.regions[0].trace = {79.0f, .25f, 2.0f};
            gi.UpdateGISettings(device.get(), &command, metadata);
            Check(GIProbeTraceSettingsEqual(gi.GetGIRegionResolved(0)->trace, metadata.regions[0].trace) &&
                GIProbeTraceSettingsEqual(gi.GetGIRegionResolved(1)->trace, unchangedTrace) &&
                gi.GetGIRegionIrradianceAtlas(0) == texture && gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage() == otherAtlas &&
                gi.GetGIRegionProbeStateBuffer(0)->GetNativeBuffer() == stateBuffer &&
                gi.GetGIRegionPreviousProbeStateBuffer(0)->GetNativeBuffer() == previousStateBuffer &&
                gi.GetGIProbeLayoutBuffer().GetNativeBuffer() == layoutBuffer &&
                shaders.FindRayTracingShader("RayTracingTest")->GetRayTracingPipeline() == pipeline,
                "Regional trace edit replaced resources, shared pipeline or another region's query policy");
            VerifyAtlas(*device, command, originalImage);
            std::cout << "[GITracePolicyGPU] live regional edit preserves atlas pixels, current/previous state, layout and pipeline identity PASS\n";
            metadata.selectedRegionIndex = 1;
            const auto beforePreviewMemory = device->GetGIMemorySnapshot();
            gi.UpdateGISettings(device.get(), &command, metadata);
            VerifyMemoryPublication(*device, *scene);
            Check(device->GetGIMemorySnapshot().retiredAllocationBytes == beforePreviewMemory.retiredAllocationBytes +
                beforePreviewMemory.probes.previews.AllocationBytes(), "Old preview allocation was not retained in memory accounting");
            Check(gi.GetGIRegionResolved(0)->stableId == 1 && gi.GetGIRegionResolved(1)->stableId == 2 &&
                gi.GetGIRegionIrradianceAtlas(0) == texture && gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage() == otherAtlas &&
                gi.GetGIRegionProbeStateBuffer(0)->GetNativeBuffer() == stateBuffer && gi.GetGIProbeLayoutBuffer().GetNativeBuffer() == layoutBuffer &&
                gi.GetGIRTPreviewTexture(0)->GetImage().GetImageCreateInfo().extent.width == 3,
                "Diagnostic selection rebuilt, reordered or cleared GI, or used the previous preview size");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT), "Second region marker begin failed");
            VkClearColorValue otherColor{};
            otherColor.float32[0]=.75f; otherColor.float32[1]=.25f; otherColor.float32[2]=.5f; otherColor.float32[3]=1.0f;
            command.ClearColorImage(gi.GetGIRegionIrradianceAtlas(1)->GetImage(), VK_IMAGE_LAYOUT_GENERAL, otherColor);
            Submit(*device, command);
            VerifySelectedPreview(*device, *scene, glm::vec3(.75f,.25f,.5f));
            VerifyAtlas(*device, command, originalImage);
            metadata.selectedRegionIndex = 2;
            gi.UpdateGISettings(device.get(), &command, metadata);
            Check(gi.GetGIRTPreviewTexture(0) == nullptr && gi.GetGIRegionCount() == 2 && gi.GetGIRegionIrradianceAtlas(0) == texture,
                "Disabled selection displayed another region or disabled active GI");
            metadata.selectedRegionIndex = 0;
            gi.UpdateGISettings(device.get(), &command, metadata);
            Check(gi.GetGIRTPreviewTexture(0) && gi.GetGIRTPreviewTexture(0)->GetImage().GetImageCreateInfo().extent.width == 2,
                "Restoring the selected region did not restore its preview target");
            VerifySelectedPreview(*device, *scene, glm::vec3(.25f,.5f,.75f));
            VerifyAtlas(*device, command, originalImage);
            std::cout << "[GIResourcesGPU] diagnostic region selection preserves atlas pixels, layout and state identity; resizes only preview targets PASS\n";
            const auto beforeRebuildMemory = device->GetGIMemorySnapshot();
            const auto allocationBeforeRebuild = VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            Check(gi.CreateRayTracingResource(device.get(), &command, scene.get(), resized, scene->GetMaterialManager()->GetGIVoxelMaterialInputs()), gi.GetResourceError());
            const auto rebuiltMemory = device->GetGIMemorySnapshot();
            consumers.Verify();
            const auto allocationAfterRebuild = VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            Check(rebuiltMemory.probes.regions[0].physicalProbes == 64 &&
                rebuiltMemory.retiredAllocationBytes == beforeRebuildMemory.retiredAllocationBytes + beforeRebuildMemory.probes.ActiveAllocationBytes() &&
                rebuiltMemory.publishedRetainedPeakBytes >= rebuiltMemory.ActiveAllocationBytes() + rebuiltMemory.retiredAllocationBytes &&
                allocationAfterRebuild - allocationBeforeRebuild == rebuiltMemory.probes.ActiveAllocationBytes(),
                "GI replacement memory differs from real allocator growth or omits retired resources");
            VerifyMemoryPublication(*device, *scene);
            Check(gi.GetGIRegionIrradianceAtlas(0) != texture && gi.GetGIRegionPhysicalProbeCount(0) == 64 && gi.GetResourceError().empty(),
                "Successful rebuild did not publish resized GI resources");
            VerifyAtlas(*device, command, originalImage);
            const auto allocationBeforeDrain = VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            Check(device->DrainDeferredDeletesAfterDeviceIdle(), "GI retired memory drain failed");
            const auto allocationAfterDrain = VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;
            Check(device->GetGIMemorySnapshot().retiredAllocationBytes == 0 &&
                allocationBeforeDrain - allocationAfterDrain == rebuiltMemory.retiredAllocationBytes &&
                device->GetGIMemorySnapshot().ActiveAllocationBytes() == rebuiltMemory.ActiveAllocationBytes(),
                "GI retired memory does not match actual allocator release");
            std::cout << "[GIMemoryGPU] allocator growth=" << allocationAfterRebuild - allocationBeforeRebuild
                << " retired release=" << allocationBeforeDrain - allocationAfterDrain
                << " failed candidates / preview replacement / resource replacement / retirement PASS\n";
            Check(device->WaitForIdle(), "GPU idle failed before cleanup");
            gi.CleanupSceneResources(device->GetLogicDevice());
            Check(!device->GetGIMemorySnapshot().probes.available && device->GetGIMemorySnapshot().probes.ActiveAllocationBytes() == 0 &&
                device->GetGIMemorySnapshot().retiredAllocationBytes == 0, "Cleanup retained active memory diagnostics");
            consumers.Verify();
            consumers.Release();
            Check(!device->GetGIMemorySnapshot().Available() && device->GetGIMemorySnapshot().ActiveAllocationBytes() == 0,
                "Consumer cleanup retained memory diagnostics");
            Check(device->GetGIMemorySnapshot().probes.world.bufferCapacityBytes==0,"Disabled field allocated world resources");
            scene->GetBLASVertexBuffers().clear();scene->GetTLASInstanceData().clear();scene->GetTLASInstanceMaterials().clear();scene->GetTLASInstanceGIEmission().clear();
            auto worldSettings=settings;worldSettings.world.enabled=true;worldSettings.world.maxBricks=64;
            {
                struct WorkerScope
                {
                    WorkerScope(){Vans::VansJobSystem::Get().Initialize(2);}
                    ~WorkerScope(){Vans::VansJobSystem::Get().Shutdown();}
                } workers;
                VansGIWorld world;
                world.Initialize(*device,*scene,worldSettings.world,false,worldSettings.placement.maxRaysPerFrame,scene->GetMaterialManager()->GetGIVoxelMaterialInputs());
                Check(world.CoarseReady(),"Known empty world was incorrectly blocked from prewarm");
                for(auto* shader:{&world.Trace(),&world.Lighting(),&world.Atlas(),&world.State()})
                    Check(shader->GetPushConstantSize()==sizeof(VansGIProbeDispatchParameters) &&
                        shader->GetPipelineProgramDesc().kind==VansPipelineProgramKind::Compute,
                        "GI world shader initialization lost its compute push constant interface");
                const auto bytes=world.BufferCapacityBytes();
                {
                    const std::array<uint32_t,kGIMaximumRegionCount> counts={3,0,7,4,0,1,0,2,0,0,0,0,0,0,0,5};
                    const glm::uvec4 total(22,0,0,0);constexpr size_t readBytes=sizeof(uint32_t)*kGIMaximumRegionCount+22*16;
                    struct ScopedReadback
                    {
                        VkDevice device;VansVKBuffer buffer;
                        ~ScopedReadback(){buffer.DestroyVulkanBuffer(device);}
                    } scopedReadback{device->GetLogicDevice()};
                    auto& readback=scopedReadback.buffer;
                    Check(readback.CreatVulkanBuffer(device->GetLogicDevice(),readBytes,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.PersistentMap(),"Scatter range readback allocation failed");
                    VansComputeShader shader;shader.SetName("GIWorldScatterRanges");
                    shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
                    Check(shader.InitShader(device->GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldScatterRanges.comp"}}),"Scatter range shader failed");
                    VansPipelineProgramDesc description{};description.kind=VansPipelineProgramKind::Compute;description.pushConstantSize=16;
                    shader.SetPipelineProgramDesc(description);shader.SetPushConstant(16);
                    Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Scatter range begin failed");
                    bool rejected=false;auto invalid=counts;invalid[0]=worldSettings.placement.maxRaysPerFrame;
                    try{world.RecordScatterRanges(command,invalid);}catch(const std::exception&){rejected=true;}
                    Check(rejected,"Scatter scratch accepted work beyond global ray capacity");
                    world.RecordScatterRanges(command,counts);
                    VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{ready});
                    command.EnsureComputeShader(shader,{world.Layout()});command.DispatchCompute(shader,(kGIMaximumRegionCount*8u+63u)/64u,1,1,{world.Descriptor()},&total,sizeof(total));
                    ready.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{ready});
                    command.CopyBuffer(world.ScatterBuffer().GetNativeBuffer(),readback.GetNativeBuffer(),0,0,readBytes);
                    ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{ready});Submit(*device,command);
                    readback.InvalidateMappedRange(0,readBytes);
                    const auto* offsets=static_cast<const uint32_t*>(readback.GetMappedPtr());
                    const auto* events=reinterpret_cast<const glm::vec4*>(offsets+kGIMaximumRegionCount);uint32_t cursor=0;
                    for(uint32_t region=0;region<kGIMaximumRegionCount;++region)
                    {
                        Check(offsets[region]==cursor,"Scatter region prefix does not match current work");
                        for(uint32_t ray=0;ray<counts[region];++ray,++cursor)
                            Check(events[cursor]==glm::vec4(float(region),float(ray),float(cursor),100.125f+cursor),"Scatter events aliased across regions or lost float32 distance");
                    }
                    std::cout<<"[GIScatteringGPU] shared scratch: empty/mixed regions, disjoint GPU addresses and capacity rejection PASS"<<std::endl;
                }
                for(const auto center:{glm::vec3(128,0,-128),glm::vec3(-512,64,512),glm::vec3(0)})
                {
                    const auto publication=world.QueryPublication();world.SetViewCenter(center);
                    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    do
                    {
                        world.PrepareUpdates();
                        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Async world update begin failed");
                        world.RecordUpdates(command);Submit(*device,command);
                        Check(std::chrono::steady_clock::now()<deadline,"Async world page publication did not complete");
                    }while(world.QueryPublication().sequence==publication.sequence);
                    Check(world.QueryPublication().generation==publication.generation && world.QueryPublication().changes.Empty(),
                        "Empty-world scroll changed query owner or invalidated unrelated receiver segments");
                    Check(world.BufferCapacityBytes()==bytes && world.PendingBricks()==0,"Camera scroll reallocated world GPU buffers or left empty pages pending");
                    Check(world.CoarseReady(),"Empty world scroll lost coarse publication readiness");
                }
                // 已提交后台规划时退役场对象；生命周期不能依赖下一帧轮询或 GPU 工作。
                world.SetViewCenter(glm::vec3(2048));
                world.PrepareUpdates();
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Async retire begin failed");
                world.RecordUpdates(command);Submit(*device,command);
                std::cout<<"[GIWorldGPU] background empty-field scroll, fixed allocations and pending retirement passed"<<std::endl;
            }
            try
            {
                VerifyScrollingPublication(*device,*scene,worldSettings);
                auto hybridSettings=worldSettings;hybridSettings.regions[0].mode=GIProbeRegionMode::Adaptive;
                VerifyScrollingPublication(*device,*scene,hybridSettings);
            }
            catch(const std::exception& error)
            {
                std::cerr<<"[GIResourcesGPU] scrolling publication exception: "<<error.what()<<std::endl;
                gi.CleanupSceneResources(device->GetLogicDevice());throw;
            }
            for(uint32_t cycle=0;cycle<3;++cycle)
            {
                Check(gi.CreateRayTracingResource(device.get(),&command, scene.get(), worldSettings, scene->GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
                Check(gi.IsReady()&&device->GetGIMemorySnapshot().probes.world.bufferCapacityBytes>0,"Terrain/PCG-only GI did not allocate without ordinary BLAS");
                VerifyMemoryPublication(*device, *scene);
                if(cycle==0)
                try
                {
                    const auto generation=gi.GetWorldQueryPublication().generation;
                    struct FeedbackFence
                    {
                        VkDevice device;VkFence fence{};
                        ~FeedbackFence(){if(fence)vkDestroyFence(device,fence,nullptr);}
                    } feedback{device->GetLogicDevice()};
                    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                    Check(vkCreateFence(feedback.device,&fenceInfo,nullptr,&feedback.fence)==VK_SUCCESS,"Cancelled GI fence creation failed");
                    gi.DiscardGIProbeUpdate();
                    Check(!gi.NeedsWorldSourceRebuild(),"Discard without a prepared frame dirtied valid world resources");
                    gi.PrepareGIProbeUpdate(VansRenderLightFrameData{},scene->GetMaterialManager()->m_SSGI.history,feedback.fence,{});
                    gi.DiscardGIProbeUpdate();
                    Check(gi.NeedsWorldSourceRebuild(),"Cancelled publication retained trusted CPU page readiness");
                    Check(gi.CreateRayTracingResource(device.get(),&command, scene.get(), worldSettings, scene->GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
                    Check(!gi.NeedsWorldSourceRebuild() && gi.GetWorldQueryPublication().generation!=generation,
                        "Recovery must replace the untrusted query owner and clear failed publication state");
                    std::cout<<"[GIWorldGPU] cancelled publication invalidates readiness; safe rebuild changes owner PASS"<<std::endl;
                }
                catch(const std::exception& error)
                {
                    std::cerr<<"[GIResourcesGPU] cancelled publication exception: "<<error.what()<<std::endl;
                    gi.CleanupSceneResources(device->GetLogicDevice());throw;
                }
                Check(gi.CreateRayTracingResource(device.get(),&command, scene.get(), settings, scene->GetMaterialManager()->GetGIVoxelMaterialInputs()),gi.GetResourceError());
                Check(device->GetGIMemorySnapshot().probes.world.bufferCapacityBytes==0,"Disabling retained world resources");
            }
            gi.CleanupSceneResources(device->GetLogicDevice());
        }
        Check(errors == 0, "Vulkan validation errors during GI transaction checks");
        std::cout << "[GIResourcesGPU] CHECKS PASS: three late failures; candidate view cleanup; retained irradiance readback; metadata edit; resized publication; preview retirement; validationErrors=0" << std::endl;
        destroyMessenger(device->GetInstance(), messenger, nullptr);
        messenger=VK_NULL_HANDLE;
        device.reset(); m_GraphicsDevice = nullptr;
        std::cout << "[GIResourcesGPU] PASS: device teardown completed" << std::endl;
        return true;
    }
    catch (const std::exception& error)
    { std::cerr << "[GIResourcesGPU] FAIL: " << error.what() << std::endl; return false; }
}
