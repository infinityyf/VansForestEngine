#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationCollection.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationSystem.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using namespace VansGraphics;
void Require(bool value, const std::string& message)
{ if (!value) throw std::runtime_error(message); }
uint32_t validationErrors = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    { ++validationErrors; std::cerr << data->pMessage << std::endl; }
    return VK_FALSE;
}
struct Window final : INativeWindowProvider
{
    GLFWwindow* handle = nullptr;
    void* GetNativeWindowHandle() const override { return handle; }
    ~Window() { if (handle) glfwDestroyWindow(handle); glfwTerminate(); }
};
}

bool RunGrassPoolGpuContractTests()
{
    try
    {
        validationErrors = 0;
        Window window;
        Require(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window.handle = glfwCreateWindow(64, 64, "Grass pool GPU contract", nullptr, nullptr);
        Require(window.handle != nullptr, "Hidden window creation failed");
        auto device = std::make_unique<VansVKDevice>(VkExtent2D{64, 64}, &window);
        try
        {
        Require(device->IsInitialized(), "Engine Vulkan initialization failed");
        m_GraphicsDevice = device.get();
        VkDevice native = device->GetLogicDevice();
        const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(device->GetInstance(), "vkCreateDebugUtilsMessengerEXT"));
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(device->GetInstance(), "vkDestroyDebugUtilsMessengerEXT"));
        struct Messenger
        {
            VkInstance instance;
            PFN_vkDestroyDebugUtilsMessengerEXT destroy;
            VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE;
            ~Messenger() { if (handle) destroy(instance, handle, nullptr); }
        } messenger{device->GetInstance(), destroy};
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        info.pfnUserCallback = Validation;
        // Release does not request VK_EXT_debug_utils; the independent workset
        // fixture supplies validation coverage without changing engine settings.
        if (create && destroy)
            Require(create(messenger.instance, &info, nullptr, &messenger.handle) == VK_SUCCESS,
                "Validation messenger creation failed");
        auto& shaders = VansShaderManager::Get();
        shaders.RegisterComputeShader("GrassBoneSim", "EngineAssets/Shaders/GrassBoneSim", sizeof(GrassSimPushConstants));
        shaders.RegisterComputeShader("GrassCull", "EngineAssets/Shaders/GrassCull", sizeof(GrassCullPushConstants));
        VansShaderEntry grassShader;
        grassShader.relativePath = "EngineAssets/Shaders/Grass/Deferred";
        grassShader.pushConstantSize = sizeof(GrassDrawPushConstants);
        shaders.RegisterGraphicsShader("GrassPoolContractDraw", grassShader);
        Require(shaders.LoadAll(std::filesystem::current_path().generic_string() + "/",
            std::filesystem::current_path()/"Library/Artifacts/Shaders", native), "Grass shaders failed to load");
        {
            VansGrassMaterial material;
            VansScene scene;
            material.m_MaterialType = VansMaterialType::VAN_GRASS;
            material.SetName("789f5988-d5f0-4a02-b9d6-215a60d92b31");
            material.m_PassShaders[VansPass::GBUFFER] = shaders.FindGraphicsShader("GrassPoolContractDraw");
            scene.AddMaterialAsset(&material);
            auto plant = std::make_shared<Vans::VansPlantTypeAsset>();
            plant->category = Vans::VansPlantCategory::Grass;
            plant->grass.boneCount = 3; plant->grass.subBladeCount = 1;
            plant->grass.bladeHeight = 1; plant->grass.windDirection = {1,0};
            plant->grass.stiffness = 15; plant->grass.damping = .92f;
            plant->grass.simulationFullDistance = 100; plant->grass.simulationFadeDistance = 200;
            plant->render.cullingEnabled = true; plant->render.cullDistance = 100;
            Vans::VansPlantVariant variant;
            variant.id = "main"; variant.geometry = Vans::VansPlantGeometry::ProceduralBlade;
            variant.bladeWidth = .02f; variant.cullingRadius = 1;
            Vans::VansPlantPart part;
            part.id = "blade";
            Require(Vans::VansAssetGuid::TryParse(material.m_AssetName, part.material), "Material GUID failed");
            variant.parts.push_back(part); plant->variants.push_back(variant);
            variant.id = "zz-invalid-material";
            Require(Vans::VansAssetGuid::TryParse("47ed7ce0-07b5-4cf0-8c54-fad077394ab6", variant.parts[0].material),
                "Failure fixture GUID failed");
            plant->variants.push_back(variant);
            const auto source = [&](int64_t cell, uint32_t count) {
                auto result = std::make_shared<Vans::VansPcgBatchSource>();
                result->key = {"contract", "grass", "main", cell, 0}; result->plant = plant;
                for (uint32_t index=0; index<count; ++index)
                {
                    Vans::VansPcgPoint point;
                    point.id = uint64_t(cell)*1000u+index;
                    point.position = {float(cell)*16.f+float(index)*.001f,0,-5};
                    point.rotation = {0,0,0,1}; point.scale = {1,1,1};
                    result->points.push_back(point);
                }
                return result;
            };
            VansVegetationCollection collection;
            std::string error;
            const auto residency = [&](float x) {
                Require(collection.UpdateResidency(scene,*device,x,-5,glm::mat4(1),0,error),error);
            };
            const auto onlyPool = [&] {
                VansVegetationSystem* result = nullptr;
                size_t count = 0;
                collection.ForEach([&](VansVegetationSystem& value) { result=&value; ++count; });
                Require(count == 1, "Expected one shared grass owner");
                return result;
            };
            residency(0);
            Vans::VansPcgBatchUpdate update;
            update.region = "contract"; update.layer = "grass"; update.cellSize = 16;
            auto first=source(0,65), second=source(1,63);
            update.batches[first->key]=first; update.batches[second->key]=second;
            Require(collection.Apply(scene,*device,update,error),error);
            auto* original = onlyPool();
            const auto handle = original->GetInstanceBuffer().GetNativeBuffer();
            const auto drawSet = original->GetRenderConfigsGPU()[0].drawDescSet;
            Require(original->GetInstanceBuffer().PersistentMap(), "Instance mapping failed");
            const auto byteCount = original->GetInstanceBuffer().GetBufferSize();
            const auto* mapped = static_cast<const uint8_t*>(original->GetInstanceBuffer().GetMappedPtr());
            const std::vector<uint8_t> before(mapped,mapped+byteCount);
            auto edited=std::make_shared<Vans::VansPcgBatchSource>(*first);
            edited->points[0].position[1]=2; update.batches[first->key]=edited;
            Require(collection.Apply(scene,*device,update,error),error);
            Require(onlyPool()==original && original->GetInstanceBuffer().GetNativeBuffer()==handle &&
                original->GetRenderConfigsGPU()[0].drawDescSet==drawSet, "Local edit recreated pool resources");
            Require(std::memcmp(mapped,before.data(),before.size())==0, "Preparation wrote an in-flight buffer");
            bool trackedRead=false,trackedWrite=false;
            original->ForEachMutableFrameBuffer(0,[&](VkBuffer buffer,VansVegetationSystem::FrameBufferConsumer consumer) {
                if(buffer==handle) { trackedRead|=consumer==VansVegetationSystem::FrameBufferConsumer::ReadOnlyDraw;
                    trackedWrite|=consumer==VansVegetationSystem::FrameBufferConsumer::TransferWrite; }
            });
            Require(trackedRead && trackedWrite, "Instance upload missing its physical read/write dependencies");
            auto& command = device->GetImmediateGraphicsCommandBuffer();
            Require(command.ResetCommandBuffer(false) && command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),
                "Command recording failed");
            original->RecordGrassStateTransfer(command);
            Require(command.EndCommandBufferRecord(), "Aborted command end failed");
            original->ResolveGrassStateTransfer(false);
            Require(original->HasGrassStateTransfer(), "Cancelled recording discarded its upload");
            edited=std::make_shared<Vans::VansPcgBatchSource>(*edited);
            edited->points[0].position[1]=3; update.batches[first->key]=edited;
            Require(collection.Apply(scene,*device,update,error),error);
            const auto submitTransfers = [&] {
                Require(command.ResetCommandBuffer(false) && command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),
                    "Transfer begin failed");
                collection.ForEach([&](VansVegetationSystem& system) { system.RecordGrassStateTransfer(command); });
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{host});
                Require(command.EndCommandBufferRecord(), "Transfer end failed");
                const VkCommandBuffer nativeCommand=command.GetVKCommandBuffer();
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&nativeCommand;
                Require(vkQueueSubmit(device->GetGraphicsQueue(),1,&submit,VK_NULL_HANDLE)==VK_SUCCESS &&
                    vkQueueWaitIdle(device->GetGraphicsQueue())==VK_SUCCESS, "Transfer submit failed");
                collection.ForEach([&](VansVegetationSystem& system) {
                    system.ResolveGrassStateTransfer(true); system.RetireGrassStateTransfer(*device); });
            };
            submitTransfers();
            Require(static_cast<const GrassInstance*>(original->GetInstanceBuffer().GetMappedPtr())[0].modelMatrix[3].y==3,
                "Ordered local updates did not publish the latest page");
            Require(std::memcmp(mapped+128u*sizeof(GrassInstance),before.data()+128u*sizeof(GrassInstance),63u*sizeof(GrassInstance))==0,
                "Local edit changed its neighbour");
            Require(!original->HasGrassStateTransfer(), "Accepted uploads were not retired");
            auto failed=update;
            auto pending=std::make_shared<Vans::VansPcgBatchSource>(*edited);
            pending->points[0].position[1]=4; failed.batches[first->key]=pending;
            auto invalid=source(2,1); invalid->key.variant="zz-invalid-material";
            failed.batches[invalid->key]=invalid;
            Require(!collection.Apply(scene,*device,failed,error) && !error.empty(), "Late PCG failure was accepted");
            Require(onlyPool()==original && !original->HasGrassStateTransfer() &&
                static_cast<const GrassInstance*>(original->GetInstanceBuffer().GetMappedPtr())[0].modelMatrix[3].y==3,
                "Failed transaction committed a prepared page or changed its owner");
            update.batches.erase(first->key);
            Require(collection.Apply(scene,*device,update,error) && onlyPool()==original, "Page removal rebuilt the arena");
            update.batches[first->key]=edited;
            Require(collection.Apply(scene,*device,update,error) && onlyPool()==original, "Hole reentry rebuilt the arena");
            submitTransfers();
            auto grown=source(0,129); update.batches[first->key]=grown;
            Require(collection.Apply(scene,*device,update,error),error);
            Require(onlyPool()!=original, "Capacity growth failed to replace its physical owner");
            submitTransfers();
            residency(1000);
            Require(collection.BatchCount()==0, "Distant cells retained an active GPU pool");
            residency(0);
            Require(collection.BatchCount()==1, "Residency reentry lost the grass pool");
            Require(validationErrors==0, "Grass pool Vulkan validation errors");
            std::cout << "GRASS_POOL_GPU_CONTRACT_PASS localReuse=1 deferredUpload=1 orderedEdits=1 cancelledRecording=1"
                " lateFailureAtomic=1 removeReentry=1 growth=1 residencyReentry=1 validationMessenger="
                << (messenger.handle != VK_NULL_HANDLE) << " validationErrors=" << validationErrors << std::endl;
        }
        shaders.Clear();
        m_GraphicsDevice = nullptr;
        return true;
        }
        catch (const std::exception& error)
        {
            // Report before native-device teardown, which can stall in the
            // optional Streamline runtime on this machine.
            std::cerr << "GRASS_POOL_GPU_CONTRACT_FAIL: " << error.what() << std::endl;
            throw;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "GRASS_POOL_GPU_CONTRACT_FAIL: " << error.what() << std::endl;
        return false;
    }
}
