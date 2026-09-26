#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/RayTracingCore/VansRayTracing.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/BRDFData/VansIESProfile.h"
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
#include "../EngineCore/TerrainCore/VansTerrainAsset.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include <glm/gtc/packing.hpp>
#include <cstring>
#include <array>

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
    uint32_t instanceEditIdleCalls = 0;
    VKAPI_ATTR VkResult VKAPI_CALL CountInstanceEditWaitIdle(VkDevice device)
    { ++instanceEditIdleCalls; return originalWaitIdle(device); }
    void Check(bool valid, const std::string& message)
    { if (!valid) { std::cerr << "[GIResourcesGPU] CHECK FAILED: " << message << std::endl; throw std::runtime_error(message); } }
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
    void VerifyArrayLayerUpload(VansVKDevice& device)
    {
        auto& command = device.GetImmediateGraphicsCommandBuffer();
        for (bool generateMips : {false, true})
        {
            VansTexture texture;
            texture.InitTextureArray(command, 4, 4, 3, 4, generateMips);
            auto& image = texture.GetImage();
            const uint32_t mipCount = generateMips ? 3 : 1;
            std::vector<std::array<uint8_t, 4>> colors{{20,40,60,255},{80,100,120,255},{140,160,180,255}};
            const auto upload = [&](int layer)
            {
                std::vector<uint8_t> pixels(4 * 4 * 4);
                for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = colors[layer][i % 4];
                Check(texture.UpdateArrayLayerFromPixels(command, pixels.data(), 4, 4, layer), "Array layer upload failed");
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
    void VerifyScrollingPublication(VansVKDevice& device, VansScene& scene, VansGISettings settings)
    {
        settings.regions.resize(2);
        settings.regions[1]=settings.regions[0];settings.regions[1].stableId=2;
        settings.regions[1].worldOnly=true;settings.regions[1].followView=true;
        settings.regions[1].gridDimensions={9,10,11};settings.regions[1].probeSpacing=1;settings.regions[1].center={0,0,0};
        auto& gi=device.GetRayTracingContext();auto& command=device.GetImmediateGraphicsCommandBuffer();
        Check(gi.CreateRayTracingResource(&device,&command,&scene,settings),gi.GetResourceError());
        const auto fixed=*gi.GetGIRegionResolved(0);
        const auto* current=gi.GetGIRegionProbeStateBuffer(1);const auto* previous=gi.GetGIRegionPreviousProbeStateBuffer(1);
        const auto& layout=gi.GetGIProbeLayoutBuffer();
        const auto atlas=gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage();
        const auto bytes=gi.GetWorldAllocatedBytes();
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
            auto parameters=settings;parameters.regions[1].normalBias=.17f;gi.UpdateGISettings(parameters);
            Check(gi.GetGIRegionResolved(1)->volumeMin==oracle.Minimum() && gi.GetGIRegionResolved(1)->scrollEpoch==epoch,
                "Lighting parameter edit restored the authored center");
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Scrolling publication begin failed");
            // 工作表为空，仅执行真实帧入口的布局发布与回收槽清理，不伪造几何追踪结果。
            gi.DispatchRayTracing(&device,&command,&scene);
            transfer.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{transfer});
            for(size_t i=0;i<3;++i)command.CopyBuffer(source[i]->GetNativeBuffer(),readback.buffers[i].GetNativeBuffer(),0,0,source[i]->GetBufferSize());
            transfer.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{transfer});Submit(device,command);
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
            Check(gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage()==atlas && gi.GetWorldAllocatedBytes()==bytes,
                "Scrolling reallocated GI resources");
        }
        const auto before=*gi.GetGIRegionResolved(1);bool rejected=false;
        try{gi.SetWorldViewCenter({NAN,0,0});}catch(const std::exception&){rejected=true;}
        Check(rejected && gi.GetGIRegionResolved(1)->volumeMin==before.volumeMin && gi.GetGIRegionResolved(1)->scrollEpoch==before.scrollEpoch,
            "Invalid camera partially published a scrolling grid");
        std::cout<<"[GIResourcesGPU] scrolling runtime sparse="<<settings.placement.enabled<<": overlap retained, current/previous state cleared, layout+epoch published, parameter edits stable, fixed resources PASS"<<std::endl;
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
        VansGISettings settings;settings.placement.enabled=false;settings.world.enabled=true;
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
        variant.parts.push_back({"trunk",Vans::VansPlantPartKind::Trunk,meshId,-1,materialId});plant->variants.push_back(variant);
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
            std::string error;Check(scene.GetVegetationCollection()->Apply(scene,device.GetLogicDevice(),update,&device,error),error);
        };
        apply({0,40});
        Check(gi.CreateRayTracingResource(&device,&command,&scene,settings),gi.GetResourceError());
        VansGIWorld world;world.Initialize(device,scene,settings.world,false,settings.placement.maxRaysPerFrame);
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
        const auto worldBytes=world.AllocatedBytes(),giBytes=gi.GetWorldAllocatedBytes();
        const auto worldDescriptor=world.Descriptor();const auto indoor=gi.GetGIRegionIrradianceAtlas(0)->GetImage().GetImage();
        const auto outdoor=gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage();
        const auto pump=[&]()
        {
            world.PrepareUpdates();gi.PrepareWorldUpdates();
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG publication begin failed");
            world.RecordUpdates(command);gi.DispatchRayTracing(&device,&command,&scene);Submit(device,command);
        };
        const auto drain=[&]()
        {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(world.HasPendingUpdates() || gi.HasPendingWorldUpdates())
            {pump();Check(std::chrono::steady_clock::now()<deadline,"PCG voxel transaction did not finish");}
            Check(world.AllocatedBytes()==worldBytes && gi.GetWorldAllocatedBytes()==giBytes && world.Descriptor()==worldDescriptor &&
                gi.GetGIRegionIrradianceAtlas(0)->GetImage().GetImage()==indoor && gi.GetGIRegionIrradianceAtlas(1)->GetImage().GetImage()==outdoor,
                "PCG instance edit reallocated GI resources");
        };
        drain();
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
            ~NoDeviceIdle(){vkDeviceWaitIdle=originalWaitIdle;}
        } noDeviceIdle;
        VansComputeShader query;query.SetName("GIWorldPcgQuery");
        query.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(query.InitShader(device.GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldLodBoundary.comp"}}),"PCG query shader failed");
        VansPipelineProgramDesc description{};description.kind=VansPipelineProgramKind::Compute;description.pushConstantSize=32;
        query.SetPipelineProgramDesc(description);query.SetPushConstant(32);
        struct Readback { VkDevice device;VansVKBuffer buffer;~Readback(){buffer.DestroyVulkanBuffer(device);} } readback{device.GetLogicDevice()};
        Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(),gi.GetGIRegionProbeStateBuffer(1)->GetBufferSize(),VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && readback.buffer.PersistentMap(),"PCG readback failed");
        const auto sample=[&](float x,float distance,float yz=.125f)
        {
            const std::array<glm::vec4,2> ray{glm::vec4(x,yz,yz,distance),glm::vec4(1,0,0,0)};
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"PCG query begin failed");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
            command.EnsureComputeShader(query,{world.Layout()});command.DispatchCompute(query,1,1,1,{world.Descriptor()},ray.data(),sizeof(ray));
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
            command.CopyBuffer(world.ScatterBuffer().GetNativeBuffer(),readback.buffer.GetNativeBuffer(),0,0,16);
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
        refresh({2,40});drain();hit=sample(-2,12);
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
        plant=std::make_shared<Vans::VansPlantTypeAsset>(*plant);apply({0,40});
        Check(gi.NeedsWorldSourceRebuild(),"New model identity silently reused an old template");
        std::cout<<"[GIPcgPublicationGPU] PASS: unavailable-source retention, chunk deltas, shared templates, cached move/delete/reload, superseded and cross-chunk transactions, ray readback, local state clear, indoor preservation, new model identity rejection and fixed GI allocations; submittedInstances="<<editStats.submittedInstances<<" preparedInstances="<<editStats.preparedInstances<<" clearedWords="<<cleared<<" preservedWords="<<preserved<<" deviceIdleCalls="<<instanceEditIdleCalls<<std::endl;
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

    void VerifyWorldQueries(VansVKDevice& device)
    {
        bool lodBoundariesPassed=true;
        Vans::VansTerrainAsset terrain;terrain.width=terrain.height=2;terrain.heights={0,65535,0,65535};
        terrain.settings.terrainSize=16;terrain.settings.maxHeight=2;terrain.settings.heightOffset=-1;
        GIWorldHeightData heights;std::string error;Check(heights.Build(terrain,error),error);
        GIWorldParameters parameters;parameters.terrain=heights.parameters;parameters.terrain.w=1;
        parameters.voxelMin=glm::vec4(-16);parameters.voxelMax=glm::vec4(16);
        parameters.height={2,2,uint32_t(heights.levels.size()),1};parameters.table={64,2,4,0};parameters.center={0,0,0,1};parameters.grid={1,1,1,1};
        std::copy(heights.levels.begin(),heights.levels.end(),parameters.heightLevels.begin());
        std::array<GIWorldPageGPU,64> pages;
        pages[GIWorldHash({0,0,0,0})&63].key={0,0,0,0};pages[GIWorldHash({0,0,0,0})&63].data={0,1,0,0};
        auto slot=GIWorldHash({1,0,0,0})&63;while(pages[slot].data.x!=~0u)slot=(slot+1)&63;
        pages[slot].key={1,0,0,0};pages[slot].data={512,0,0,0};
        std::array<GIWorldVoxel,1024> voxels{};voxels[0].optical=glm::packHalf1x16(1.f);
        voxels[1].optical=0x10000;voxels[1].surface=GIWorldEncodeNormal({-1,0,0})<<16;
        glm::vec4 material(.5,.5,.5,1);std::array<glm::vec4,78> results{};
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
                std::array<glm::vec4,78> colored;buffers[7].InvalidateMappedRange(0,sizeof(colored));std::memcpy(colored.data(),buffers[7].GetMappedPtr(),sizeof(colored));
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
                std::array<glm::vec4,78> after;buffers[7].InvalidateMappedRange(0,sizeof(after));std::memcpy(after.data(),buffers[7].GetMappedPtr(),sizeof(after));
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
            coarsePages[fineSlot].key={0,0,0,0};coarsePages[fineSlot].data={0,1,0,0};
            auto coarseSlot=GIWorldHash({0,0,0,1})&63;while(coarsePages[coarseSlot].data.x!=~0u)coarseSlot=(coarseSlot+1)&63;
            coarsePages[coarseSlot].key={0,0,0,1};coarsePages[coarseSlot].data={512,1,0,0};
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
                std::array<glm::vec4,78> sampled;buffers[7].InvalidateMappedRange(0,sizeof(sampled));std::memcpy(sampled.data(),buffers[7].GetMappedPtr(),sizeof(sampled));
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
            pages[rootSlot].key={0,0,0,15};pages[rootSlot].data={0,1,0,0};
            auto adaptiveFineSlot=GIWorldHash({0,0,0,0})&63;while(pages[adaptiveFineSlot].data.x!=~0u)adaptiveFineSlot=(adaptiveFineSlot+1)&63;
            pages[adaptiveFineSlot].key={0,0,0,0};pages[adaptiveFineSlot].data={512,0,0,0};
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
                std::array<glm::vec4,78> sampled;buffers[7].InvalidateMappedRange(0,sizeof(sampled));std::memcpy(sampled.data(),buffers[7].GetMappedPtr(),sizeof(sampled));
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
                        pages[slot].key={key.x,key.y,key.z,key.level};pages[slot].data={address,ready?1u:0u,0,0};
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
        Check(lodBoundariesPassed,"LOD crossing skipped parent geometry, fine entry, optical depth or unknown pages");
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
        shaders.RegisterRayTracingShader("RayTracingTest", "EngineAssets/Shaders/RayTracingTest", sizeof(RayTracingPushConstant));
        for (const char* name : {"GIPointLight", "GIVisibilityUpdate", "GIProbeState"})
            shaders.RegisterComputeShader(name, std::string("EngineAssets/Shaders/") + name, sizeof(RayTracingPushConstant));
        shaders.RegisterComputeShader("GIRTPreview", "EngineAssets/Shaders/GIRTPreview", sizeof(GIRTPreviewPushConstant));
        Check(shaders.LoadAll(std::filesystem::current_path().generic_string() + "/",
            std::filesystem::current_path()/"Library/Artifacts/Shaders", device->GetLogicDevice()), "GI shader load failed");
        VerifyArrayLayerUpload(*device);
        VerifyIESArrayResources(*device);
        VerifyTerrainFootprint(*device);
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
            Check(gi.GetWorldAllocatedBytes()==0,"Disabled field allocated world resources");
            scene->GetBLASVertexBuffers().clear();scene->GetTLASInstanceData().clear();scene->GetTLASInstanceMaterials().clear();scene->GetTLASInstanceGIEmission().clear();
            auto worldSettings=settings;worldSettings.world.enabled=true;worldSettings.world.maxBricks=64;
            {
                struct WorkerScope
                {
                    WorkerScope(){Vans::VansJobSystem::Get().Initialize(2);}
                    ~WorkerScope(){Vans::VansJobSystem::Get().Shutdown();}
                } workers;
                VansGIWorld world;
                world.Initialize(*device,*scene,worldSettings.world,false,worldSettings.placement.maxRaysPerFrame);
                Check(world.CoarseReady(),"Known empty world was incorrectly blocked from prewarm");
                for(auto* shader:{&world.Trace(),&world.Lighting(),&world.Atlas(),&world.State()})
                    Check(shader->GetPushConstantSize()==96 &&
                        shader->GetPipelineProgramDesc().kind==VansPipelineProgramKind::Compute,
                        "GI world shader initialization lost its compute push constant interface");
                const auto bytes=world.AllocatedBytes();
                {
                    const std::array<uint32_t,8> counts={3,0,7,4,0,1,0,2};
                    const glm::uvec4 total(17,0,0,0);constexpr size_t readBytes=32+17*16;
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
                    command.EnsureComputeShader(shader,{world.Layout()});command.DispatchCompute(shader,1,1,1,{world.Descriptor()},&total,sizeof(total));
                    ready.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{ready});
                    command.CopyBuffer(world.ScatterBuffer().GetNativeBuffer(),readback.GetNativeBuffer(),0,0,readBytes);
                    ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                    command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{ready});Submit(*device,command);
                    readback.InvalidateMappedRange(0,readBytes);
                    const auto* offsets=static_cast<const uint32_t*>(readback.GetMappedPtr());
                    const auto* events=reinterpret_cast<const glm::vec4*>(offsets+8);uint32_t cursor=0;
                    for(uint32_t region=0;region<8;++region)
                    {
                        Check(offsets[region]==cursor,"Scatter region prefix does not match current work");
                        for(uint32_t ray=0;ray<counts[region];++ray,++cursor)
                            Check(events[cursor]==glm::vec4(float(region),float(ray),float(cursor),100.125f+cursor),"Scatter events aliased across regions or lost float32 distance");
                    }
                    std::cout<<"[GIScatteringGPU] shared scratch: empty/mixed regions, disjoint GPU addresses and capacity rejection PASS"<<std::endl;
                }
                for(const auto center:{glm::vec3(128,0,-128),glm::vec3(-512,64,512),glm::vec3(0)})
                {
                    const auto revision=world.Revision();world.SetViewCenter(center);
                    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    do
                    {
                        world.PrepareUpdates();
                        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Async world update begin failed");
                        world.RecordUpdates(command);Submit(*device,command);
                        Check(std::chrono::steady_clock::now()<deadline,"Async world page publication did not complete");
                    }while(world.Revision()==revision);
                    Check(world.AllocatedBytes()==bytes && world.PendingBricks()==0,"Camera scroll reallocated world GPU buffers or left empty pages pending");
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
                auto hybridSettings=worldSettings;hybridSettings.placement.enabled=true;
                VerifyScrollingPublication(*device,*scene,hybridSettings);
            }
            catch(const std::exception& error)
            {
                std::cerr<<"[GIResourcesGPU] scrolling publication exception: "<<error.what()<<std::endl;
                gi.CleanupSceneResources(device->GetLogicDevice());throw;
            }
            for(uint32_t cycle=0;cycle<3;++cycle)
            {
                Check(gi.CreateRayTracingResource(device.get(),&command,scene.get(),worldSettings),gi.GetResourceError());
                Check(gi.IsReady()&&gi.GetWorldAllocatedBytes()>0,"Terrain/PCG-only GI did not allocate without ordinary BLAS");
                Check(gi.CreateRayTracingResource(device.get(),&command,scene.get(),settings),gi.GetResourceError());
                Check(gi.GetWorldAllocatedBytes()==0,"Disabling retained world resources");
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
