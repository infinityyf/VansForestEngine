#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/GICore/VansGIInstanceMaterial.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace
{
using namespace VansGraphics;
using Record = VansGIReceiverVisibility::Record;
uint32_t errors=0;
void Check(bool value,const char* message)
{
    if (!value)
    {
        std::cerr << "[GIReceiverVisibilityGPU] FAILED: " << message << std::endl;
        throw std::runtime_error(message);
    }
}
VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*)
{
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { ++errors; std::cerr<<data->pMessage<<std::endl; }
    return VK_FALSE;
}
struct Window : INativeWindowProvider
{
    GLFWwindow* handle=nullptr;
    void* GetNativeWindowHandle() const override { return handle; }
    ~Window() { if(handle) glfwDestroyWindow(handle);glfwTerminate(); }
};
struct Fixture
{
    Window window;
    std::unique_ptr<VansVKDevice> device;
    std::vector<std::unique_ptr<VansVKBuffer>> buffers;
    std::vector<std::unique_ptr<VansTexture>> textures;
    std::vector<VkAccelerationStructureKHR> acceleration;
    VkDebugUtilsMessengerEXT messenger{};
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
    VkDescriptorSetLayout rootLayout{},passLayout{};
    void TestBias();
    void TestZeroSupportFallback();
    std::vector<VkDescriptorSet> rootSets,passSets;
    VansRayTracingShader* trace{};
    VansComputeShader *prepare{}, *reproject{}, *worldShader{};
    VansVKBuffer *bias{}, *current{},*history{},*work{},*states{},*previous{},*info{},*layout{},*anchors{},*world{},*claims{};
    static constexpr uint32_t Width=512,Height=512,CacheWidth=128,CacheHeight=128,Count=CacheWidth*CacheHeight;
    static constexpr VkDeviceSize CacheBytes=16+Count*sizeof(Record);
    struct State { glm::vec4 next{},published{};glm::uvec4 metadata{1,0,256,0}; };
    std::array<State,32> probeStates{};
    SSGIParamsGPU params{};
    glm::mat4 viewProjection{0.0f};
    VkQueryPool timing{};
    double lastGpuMilliseconds = 0.0;
    ~Fixture()
    {
        if(!device) return;
        vkDeviceWaitIdle(device->GetLogicDevice());
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        descriptors->DestroyDescriptorSet(rootSets);descriptors->DestroyDescriptorSet(passSets);
        descriptors->ReleaseDescriptorSetLayout(rootLayout);descriptors->ReleaseDescriptorSetLayout(passLayout);
        for(auto as:acceleration) device->DestroyAccelerationStructure(as);
        textures.clear();
        for(auto& buffer:buffers) buffer->DestroyVulkanBuffer(device->GetLogicDevice());
        buffers.clear();
        std::cout << "[GIReceiverVisibilityGPU] fixture resources released" << std::endl;
        if (timing) vkDestroyQueryPool(device->GetLogicDevice(), timing, nullptr);
        if(messenger) destroyMessenger(device->GetInstance(),messenger,nullptr);
        device.reset(); // shader/pipeline cache 按正式设备关闭顺序释放。
        m_GraphicsDevice=nullptr;
        std::cout << "[GIReceiverVisibilityGPU] device teardown completed" << std::endl;
    }
    VansVKCommandBuffer& Command() { return device->GetImmediateGraphicsCommandBuffer(); }
    void Begin() { Check(Command().BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"begin fixture command"); }
    void End()
    {
        Check(Command().EndCommandBufferRecord() && VansVKCommandBuffer::SubmitCommands(device->GetGraphicsQueue(),device->GetLogicDevice(),
            {Command().GetVKCommandBuffer()},{},{},Command().m_CommandBufferFinishSubmitFence) && Command().ResetCommandBuffer(false),"submit fixture command");
    }
    VansVKBuffer* Buffer(VkDeviceSize size,const void* data=nullptr,VkBufferUsageFlags extra=0)
    {
        auto buffer=std::make_unique<VansVKBuffer>();
        Check(buffer->CreatVulkanBuffer(device->GetLogicDevice(),size,VK_FORMAT_R32_UINT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
            VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|extra,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && buffer->PersistentMap(),"fixture buffer allocation");
        std::memset(buffer->GetMappedPtr(),0,size);
        if(data) std::memcpy(buffer->GetMappedPtr(),data,size);
        auto* result=buffer.get();buffers.push_back(std::move(buffer));return result;
    }
    VkDeviceAddress Address(VansVKBuffer* buffer)
    {
        VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; info.buffer=buffer->GetNativeBuffer();
        auto function=reinterpret_cast<PFN_vkGetBufferDeviceAddress>(vkGetDeviceProcAddr(device->GetLogicDevice(),"vkGetBufferDeviceAddress"));
        return function(device->GetLogicDevice(),&info);
    }
    VkAccelerationStructureKHR Build(VkAccelerationStructureTypeKHR type,VkAccelerationStructureGeometryKHR geometry,uint32_t count)
    {
        VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type=type;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(device->GetLogicDevice(),VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);
        auto* storage=Buffer(sizes.accelerationStructureSize,nullptr,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
        VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create.type=type;create.size=sizes.accelerationStructureSize;create.buffer=storage->GetNativeBuffer();
        VkAccelerationStructureKHR as{};
        Check(vkCreateAccelerationStructureKHR(device->GetLogicDevice(),&create,nullptr,&as)==VK_SUCCESS,"create fixture AS");
        acceleration.push_back(as);
        auto* scratch=Buffer(sizes.buildScratchSize+65536);
        build.scratchData.deviceAddress=(Address(scratch)+65535)&~VkDeviceAddress(65535);
        build.dstAccelerationStructure=as;
        VkAccelerationStructureBuildRangeInfoKHR range{};range.primitiveCount=count;auto* rangePtr=&range;
        Command().BuildAccelerationStructures(&build,&rangePtr);
        Barrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        return as;
    }
    void Barrier(VkPipelineStageFlags a,VkPipelineStageFlags b,VkAccessFlags c,VkAccessFlags d)
    { VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=c;barrier.dstAccessMask=d;Command().PipelineBarrier(a,b,{barrier}); }
    VansTexture* Texture(uint32_t width,uint32_t height,const std::vector<glm::vec4>& data)
    {
        auto texture=std::make_unique<VansTexture>();
        texture->LoadFromMemory(Command(),data.data(),data.size()*sizeof(glm::vec4),width,height,
            VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        auto* result=texture.get();textures.push_back(std::move(texture));return result;
    }
    void BindBuffer(VkDescriptorSet set,uint32_t binding,VansVKBuffer* buffer,uint32_t count=1,VkDescriptorType type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
    {
        std::vector<VkDescriptorBufferInfo> data(count,{buffer->GetNativeBuffer(),0,buffer->GetBufferSize()});
        VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(set,binding,type,data);
    }
    void Initialize();
    void BindInput(uint32_t binding, const std::vector<glm::vec4>& pixels)
    {
        auto* texture = Texture(Width, Height, pixels);
        auto* descriptors = VansVKDescriptorManager::GetInstance();
        descriptors->BeginDescriptorUpdate();
        descriptors->WriteImageDescriptor(passSets[0], binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{texture->GetImage().GetSampler(), texture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        descriptors->CommitDescriptorUpdates();
    }
    void PlaneView(glm::vec2 shift, glm::vec2 oldShift)
    {
        std::vector<glm::vec4> pixels(Width * Height);
        for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x)
            pixels[y * Width + x] = glm::vec4((float(x) - 256 + shift.x) * .001f, .2f,
                (float(y) - 256 + shift.y) * .001f, 1);
        BindInput(0, pixels);
        std::fill(pixels.begin(), pixels.end(), glm::vec4((oldShift - shift) / glm::vec2(Width, Height), 0, 0));
        BindInput(3, pixels);
        viewProjection = glm::mat4(0.0f);
        viewProjection[0][0] = 2.0f / (.001f * Width);
        viewProjection[2][1] = -2.0f / (.001f * Height);
        viewProjection[3] = glm::vec4((.5f - shift.x) * 2.0f / Width,
            (shift.y - .5f) * 2.0f / Height, .5f, 1.0f);
    }
    void WorldPhase(uint32_t frame, bool enabled, uint32_t operation)
    {
        struct Push { glm::uvec4 frame, operation; } push{
            glm::uvec4(CacheWidth, CacheHeight, frame, enabled ? 1u : 0u), glm::uvec4(operation, 0, 0, 0)};
        Command().EnsureComputeShader(*worldShader, {rootLayout, passLayout});
        uint32_t count = operation == 1u ? Count : VansGIReceiverVisibility::WorldCapacity;
        Command().DispatchCompute(*worldShader, (count + 63) / 64, 1, 1,
            {rootSets[0], passSets[0]}, &push, sizeof(push));
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    void Trace()
    {
        auto* pipeline=trace->GetRayTracingPipeline(device.get(),{rootLayout,passLayout});Check(pipeline!=nullptr,"production occlusion pipeline");
        Command().BindRayTracingPipeline(*pipeline);Command().BindRayTracingDescriptorSets(*pipeline,0,{rootSets[0],passSets[0]});
        Command().TraceRays(*pipeline,VansGIReceiverVisibility::RayBudget,1,1);
    }
    void Snapshot()
    {
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_READ_BIT);
        Command().CopyBuffer(current->GetNativeBuffer(),history->GetNativeBuffer(),0,0,CacheBytes);
        Command().CopyBuffer(states->GetNativeBuffer(),previous->GetNativeBuffer(),0,0,sizeof(probeStates));
        Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_READ_BIT);
    }
    // 阶段控制仅供契约测试隔离贡献；正式管线通过独立调用接入，不提供运行期开关。
    uint32_t Frame(uint32_t frame,bool enabled=true,bool stable=true,bool persistent=true,
        bool traceRays=true,glm::vec2 jitter=glm::vec2(0))
    {
        Begin();
        vkCmdResetQueryPool(Command().GetVKCommandBuffer(), timing, 0, 2);
        vkCmdWriteTimestamp(Command().GetVKCommandBuffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timing, 0);
        Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        Command().FillBuffer(work->GetNativeBuffer(),0,16,0);
        Command().FillBuffer(work->GetNativeBuffer(),16,VansGIReceiverVisibility::WorkBytes-16,0xffffffffu);
        Command().FillBuffer(anchors->GetNativeBuffer(),0,Count*4,0xffffffffu);
        Command().FillBuffer(claims->GetNativeBuffer(),0,VansGIReceiverVisibility::WorldCapacity*4,0xffffffffu);
        if (!persistent) Command().FillBuffer(world->GetNativeBuffer(),0,16,0);
        Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        struct Push { glm::uvec4 frame;glm::vec4 jitter; } push{glm::uvec4(CacheWidth,CacheHeight,frame,enabled?1:0),glm::vec4(jitter,0,0)};
        if (stable)
        {
            struct ReprojectPush { glm::uvec4 frame; glm::mat4 matrix; } project{push.frame, viewProjection};
            Command().EnsureComputeShader(*reproject, {rootLayout, passLayout});
            Command().DispatchCompute(*reproject, (Count + 63) / 64, 1, 1,
                {rootSets[0], passSets[0]}, &project, sizeof(project));
            Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        }
        if (persistent) WorldPhase(frame, enabled, 0);
        Command().EnsureComputeShader(*prepare,{rootLayout,passLayout});
        Command().DispatchCompute(*prepare,CacheWidth/8,CacheHeight/8,1,{rootSets[0],passSets[0]},&push,sizeof(push));
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        if(enabled && traceRays) Trace();
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (persistent)
        {
            WorldPhase(frame, enabled, 1);
            WorldPhase(frame, enabled, 2);
        }
        Snapshot();
        vkCmdWriteTimestamp(Command().GetVKCommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing, 1);
        End();
        uint64_t ticks[2]{};
        Check(vkGetQueryPoolResults(device->GetLogicDevice(), timing, 0, 2, sizeof(ticks), ticks,
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS, "receiver GPU timestamp readback");
        lastGpuMilliseconds = double(ticks[1] - ticks[0]) * VansVKDevice::GetTimestampPeriodMs(device->GetPhysicalDevice());
        const auto rays=static_cast<uint32_t*>(work->GetMappedPtr())[1];
        Check(rays<=VansGIReceiverVisibility::RayBudget,"ray budget overflow");
        return rays;
    }
};
glm::vec4 Surface(glm::vec3 P,glm::vec3 N)
{
    N/=std::abs(N.x)+std::abs(N.y)+std::abs(N.z);glm::vec2 e(N.x,N.z);
    if(N.y<0) e=glm::vec2(1-std::abs(e.y),1-std::abs(e.x))*glm::vec2(e.x>=0?1.f:-1.f,e.y>=0?1.f:-1.f);
    auto oct=glm::uvec2(glm::round((e*.5f+.5f)*255.f));
    return glm::vec4(P,float(oct.x|(oct.y<<8u)|(1u<<16u)));
}
void Fixture::Initialize()
{
    Check(glfwInit()!=0,"GLFW initialize");glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    window.handle=glfwCreateWindow(64,64,"GI receiver visibility GPU contract",nullptr,nullptr);Check(window.handle!=nullptr,"hidden fixture window");
    device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);Check(device->IsInitialized(),"Vulkan initialize");m_GraphicsDevice=device.get();
    VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query.queryType = VK_QUERY_TYPE_TIMESTAMP; query.queryCount = 2;
    Check(vkCreateQueryPool(device->GetLogicDevice(), &query, nullptr, &timing) == VK_SUCCESS, "receiver GPU timestamps");
    auto createMessenger=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkCreateDebugUtilsMessengerEXT"));
    destroyMessenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkDestroyDebugUtilsMessengerEXT"));
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;debug.pfnUserCallback=Validation;
    if(createMessenger && destroyMessenger)
        Check(createMessenger(device->GetInstance(),&debug,nullptr,&messenger)==VK_SUCCESS,"validation messenger");
    auto& shaders=VansShaderManager::Get();
    shaders.RegisterRayTracingShader("GIReceiverVisibilityTrace","EngineAssets/Shaders/GIReceiverVisibilityTrace");
    shaders.RegisterComputeShader("GIReceiverVisibilityPrepare","EngineAssets/Shaders/GIReceiverVisibilityPrepare",32);
    shaders.RegisterComputeShader("GIReceiverVisibilityReproject","EngineAssets/Shaders/GIReceiverVisibilityReproject",80);
    shaders.RegisterComputeShader("GIReceiverVisibilityWorldCache","EngineAssets/Shaders/GIReceiverVisibilityWorldCache",32);
    shaders.RegisterRayTracingShader("GIReceiverBias","EngineAssets/Shaders/GIReceiverBias");
    Check(shaders.LoadAll(std::filesystem::current_path().generic_string()+"/",
        std::filesystem::current_path()/"Library/Artifacts/Shaders",device->GetLogicDevice()),"production visibility shaders");
    trace=shaders.FindRayTracingShader("GIReceiverVisibilityTrace");prepare=shaders.FindComputeShader("GIReceiverVisibilityPrepare");
    reproject=shaders.FindComputeShader("GIReceiverVisibilityReproject");
    worldShader=shaders.FindComputeShader("GIReceiverVisibilityWorldCache");
    Begin();
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    auto quad=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d) { uint32_t n=uint32_t(vertices.size());vertices.insert(vertices.end(),{a,b,c,d});indices.insert(indices.end(),{n,n+1,n+2,n,n+2,n+3}); };
    quad({0,-1,-1},{0,1,-1},{0,1,1},{0,-1,1}); // 有限墙面
    quad({-1,0,3},{1,0,3},{1,0,5},{-1,0,5}); // 单面地板
    quad({-1,0,7},{1,0,7},{1,1,7},{-1,1,7}); // 台阶立面
    quad({-1,1,7},{1,1,7},{1,1,8},{-1,1,8}); // 台阶顶面
    auto* vertex=Buffer(vertices.size()*sizeof(glm::vec3),vertices.data(),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    auto* index=Buffer(indices.size()*4,indices.data(),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    // 与 VansMesh::BuildBLAS 一致：必须在不透明 BLAS 上验证实例级裁剪覆盖。
    geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& triangle=geometry.geometry.triangles;triangle.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangle.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;triangle.vertexStride=sizeof(glm::vec3);triangle.maxVertex=uint32_t(vertices.size()-1);
    triangle.vertexData.deviceAddress=Address(vertex);triangle.indexType=VK_INDEX_TYPE_UINT32;triangle.indexData.deviceAddress=Address(index);
    auto blas=Build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,geometry,uint32_t(indices.size()/3));
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};addressInfo.accelerationStructure=blas;
    const std::array<GIInstanceMaterialGPU,6> materials={
        GIInstanceMaterialGPU::Resolve(0xffffffffu,false,.5f),
        GIInstanceMaterialGPU::Resolve(0,true,.5f),
        GIInstanceMaterialGPU::Resolve(1,true,.25f),
        GIInstanceMaterialGPU::Resolve(1,true,.75f),
        GIInstanceMaterialGPU::Resolve(0,false,.75f),
        GIInstanceMaterialGPU::Resolve(1,true,.5f)};
    Check(materials[0].InstanceFlags()==VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR &&
        materials[1].InstanceFlags()==VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR,
        "opaque and alpha-tested instances must select different traversal paths");
    Check(GIInstanceMaterialGPU::Resolve(0,true,0).InstanceFlags()==VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR,
        "zero alpha cutoff must retain opaque fast path");
    VkAccelerationStructureInstanceKHR instances[6]{};
    for(uint32_t i=0;i<6;++i)
    {
        instances[i].transform.matrix[0][0]=instances[i].transform.matrix[1][1]=instances[i].transform.matrix[2][2]=1;
        instances[i].transform.matrix[0][3]=float(i)*5;instances[i].mask=255;
        instances[i].flags=materials[i].InstanceFlags();
        instances[i].accelerationStructureReference=vkGetAccelerationStructureDeviceAddressKHR(device->GetLogicDevice(),&addressInfo);
    }
    auto* instanceBuffer=Buffer(sizeof(instances),instances,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    geometry={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress=Address(instanceBuffer);
    auto tlas=Build(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,geometry,6);
    End(); // 纹理上传拥有自己的 immediate command 提交，先完成 AS 构建。
    std::cout << "[GIReceiverVisibilityGPU] AS ready" << std::endl;
    auto* transparent=Texture(1,1,{glm::vec4(0)});
    auto* halfCoverage=Texture(1,1,{glm::vec4(.5f)});
    std::vector<uint16_t> hitVertices(vertices.size()*14,0); // any-hit 只使用 UV，所有 UV 均为零。
    auto* hitVertex=Buffer(hitVertices.size()*2,hitVertices.data());
    uint32_t modelIds[6]={0,0,0,0,0,0};
    auto* model=Buffer(sizeof(modelIds),modelIds);auto* instanceMaterials=Buffer(sizeof(materials),materials.data());
    bias=Buffer(16+Count*32);
    current=Buffer(CacheBytes);history=Buffer(CacheBytes);work=Buffer(VansGIReceiverVisibility::WorkBytes);
    anchors=Buffer(Count*4);world=Buffer(VansGIReceiverVisibility::WorldBytes);
    claims=Buffer(VansGIReceiverVisibility::WorldCapacity*4);
    states=Buffer(sizeof(probeStates),probeStates.data());previous=Buffer(sizeof(probeStates),probeStates.data());
    info=Buffer(sizeof(params));layout=Buffer(256);
    const auto ah=VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    Check(VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom({
        {0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_RAYGEN_BIT_KHR,nullptr},
        {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,ah,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,ah,nullptr},
        {5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,ah,nullptr},{7,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,ah,nullptr},
        {14,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,ah,nullptr},
        {50,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2,ah,nullptr}},rootLayout,rootSets),"root descriptors");
    const VkShaderStageFlags stages=VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for(uint32_t i=0;i<4;++i) bindings.push_back({i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,stages,nullptr});
    bindings.push_back({4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stages,nullptr});
    for(uint32_t i:{6u,7u,8u,9u,10u,11u,12u,13u,14u,15u}) bindings.push_back({i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,(i==7||i==8)?8u:1u,stages,nullptr});
    Check(VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(bindings,passLayout,passSets),"pass descriptors");
    auto* descriptors=VansVKDescriptorManager::GetInstance();descriptors->BeginDescriptorUpdate();
    VkWriteDescriptorSetAccelerationStructureKHR asInfo{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};asInfo.accelerationStructureCount=1;asInfo.pAccelerationStructures=&tlas;
    VkWriteDescriptorSet asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};asWrite.pNext=&asInfo;asWrite.dstSet=rootSets[0];asWrite.dstBinding=0;
    asWrite.descriptorCount=1;asWrite.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;vkUpdateDescriptorSets(device->GetLogicDevice(),1,&asWrite,0,nullptr);
    BindBuffer(rootSets[0],3,hitVertex);BindBuffer(rootSets[0],4,index);BindBuffer(rootSets[0],5,model);BindBuffer(rootSets[0],7,instanceMaterials);
    descriptors->WriteImageDescriptor(rootSets[0],50,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{transparent->GetImage().GetSampler(),transparent->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {halfCoverage->GetImage().GetSampler(),halfCoverage->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
    BindBuffer(passSets[0],4,info,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);BindBuffer(passSets[0],6,layout);BindBuffer(passSets[0],7,states,8);
    BindBuffer(passSets[0],8,previous,8);BindBuffer(passSets[0],9,history);BindBuffer(passSets[0],10,current);BindBuffer(passSets[0],11,work);
    glm::uvec4 biasMeshLayout[2]={glm::uvec4(12,0,~0u,~0u),glm::uvec4(1,0,0,0)};
    BindBuffer(rootSets[0],14,Buffer(sizeof(biasMeshLayout),biasMeshLayout));
    BindBuffer(passSets[0],15,bias);
    BindBuffer(passSets[0],12,anchors);BindBuffer(passSets[0],13,world);BindBuffer(passSets[0],14,claims);
    std::vector<glm::vec4> image(Width*Height);
    for(uint32_t y=0;y<Height;++y) for(uint32_t x=0;x<Width;++x) image[y*Width+x]=glm::vec4((float(x)-256)*.001f,.2f,(float(y)-256)*.001f,1);
    auto* position=Texture(Width,Height,image);
    std::fill(image.begin(),image.end(),glm::vec4(0,1,0,0));auto* normal=Texture(Width,Height,image);
    std::fill(image.begin(),image.end(),glm::vec4(0));auto* material=Texture(Width,Height,image);auto* motion=Texture(Width,Height,image);
    VansTexture* images[]={position,normal,material,motion};
    for(uint32_t i=0;i<4;++i) descriptors->WriteImageDescriptor(passSets[0],i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{images[i]->GetImage().GetSampler(),images[i]->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
    descriptors->CommitDescriptorUpdates();
    viewProjection[0][0]=2.0f/(.001f*Width);viewProjection[2][1]=-2.0f/(.001f*Height);
    viewProjection[3]=glm::vec4(1.0f/Width,-1.0f/Height,.5f,1.0f);
    std::cout << "[GIReceiverVisibilityGPU] resources ready" << std::endl;
}
void Fixture::TestZeroSupportFallback()
{
    std::ifstream file("Source/Tests/Shaders/GIReceiverZeroSupportFallback.comp.spv",std::ios::binary|std::ios::ate);
    Check(bool(file),"fallback shader artifact");
    const auto bytes=static_cast<size_t>(file.tellg());std::vector<uint32_t> code(bytes/4);
    file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);
    struct PipelineScope
    {
        VkDevice device{};VkShaderModule module{};VkPipelineLayout layout{};VkPipeline pipeline{};VkImageView opaqueView{};
        ~PipelineScope()
        {
            if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
            if(module)vkDestroyShaderModule(device,module,nullptr);
            VansVKImage::DestroyImageView(device,opaqueView);
        }
    } pipeline;
    pipeline.device=device->GetLogicDevice();
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=bytes;module.pCode=code.data();
    Check(vkCreateShaderModule(pipeline.device,&module,nullptr,&pipeline.module)==VK_SUCCESS,"fallback module");
    const VkDescriptorSetLayout layouts[]={rootLayout,passLayout};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layoutInfo.setLayoutCount=2;layoutInfo.pSetLayouts=layouts;
    Check(vkCreatePipelineLayout(pipeline.device,&layoutInfo,nullptr,&pipeline.layout)==VK_SUCCESS,"fallback layout");
    VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=pipeline.layout;
    compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,pipeline.module,"main",nullptr};
    Check(vkCreateComputePipelines(pipeline.device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline.pipeline)==VK_SUCCESS,"fallback pipeline");

    const std::vector<glm::vec4> previewPixels={glm::vec4(.2f,.4f,.6f,0),glm::vec4(.2f,.4f,.6f,-1),
        glm::vec4(.2f,.4f,.6f,1.0f/255.0f),glm::vec4(.2f,.4f,.6f,2)};
    auto* preview=Texture(4,1,previewPixels);
    pipeline.opaqueView=preview->GetImage().CreateLayerMipView(pipeline.device,0,0,
        {VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_ONE});
    Check(pipeline.opaqueView!=VK_NULL_HANDLE,"opaque preview view");
    auto* descriptors=VansVKDescriptorManager::GetInstance();
    descriptors->BeginDescriptorUpdate();
    descriptors->WriteImageDescriptor(passSets[0],2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{preview->GetImage().GetSampler(),pipeline.opaqueView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
    descriptors->WriteImageDescriptor(passSets[0],3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{preview->GetImage().GetSampler(),preview->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
    descriptors->CommitDescriptorUpdates();

    std::array<State,32> published{};
    std::memcpy(states->GetMappedPtr(),published.data(),sizeof(published));
    auto* records=reinterpret_cast<Record*>(static_cast<char*>(current->GetMappedPtr())+16);
    for(uint32_t i=0;i<12;++i)
    {
        records[i]={};records[i].metadata=glm::uvec4(0,9,255,0);
        for(uint32_t p=0;p<16;++p)records[i].probes[p/4][p%4]=p<8?p:~0u;
    }
    records[1].metadata.w=4; // 保留一个有光的可见 Probe。
    records[2].metadata.w=255;
    records[3].metadata.z=0; // 未知遮挡仍用距离矩。
    records[6].metadata.w=1; // 可见 Probe 的 RGB 为黑，但支持权重非零。
    records[7].metadata.x=1; // 不允许跨区域使用遮挡记录。
    records[8].metadata.z=1;
    for(auto& ids:records[11].probes)ids=glm::uvec4(24,25,26,27); // 候选身份不匹配。

    auto close=[](glm::vec4 a,glm::vec4 b){return glm::all(glm::lessThanEqual(glm::abs(a-b),glm::vec4(2e-6f)));};
    for(uint32_t variant=0;variant<3;++variant)
    {
        std::vector<glm::vec4> radiance(32*16),moments(64*32);
        for(uint32_t y=0;y<16;++y)for(uint32_t x=0;x<32;++x)
        {
            uint32_t p=x/8+(y/8)*4;
            radiance[y*32+x]=variant==2||p==0?glm::vec4(0):glm::vec4(float(p),.25f*float(p),.1f,1);
        }
        for(uint32_t y=0;y<32;++y)for(uint32_t x=0;x<64;++x)
        {
            uint32_t p=x/16+(y/16)*4;
            moments[y*64+x]=variant==1?glm::vec4(0):p%2==0?glm::vec4(10,100,0,0):glm::vec4(.1f,.02f,0,0);
        }
        auto* atlas=Texture(32,16,radiance);auto* visibility=Texture(64,32,moments);
        descriptors->BeginDescriptorUpdate();
        descriptors->WriteImageDescriptor(passSets[0],0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{atlas->GetImage().GetSampler(),atlas->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        descriptors->WriteImageDescriptor(passSets[0],1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{visibility->GetImage().GetSampler(),visibility->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        descriptors->CommitDescriptorUpdates();
        Begin();Barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        auto cmd=Command().GetVKCommandBuffer();const VkDescriptorSet sets[]={rootSets[0],passSets[0]};
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,2,sets,0,nullptr);vkCmdDispatch(cmd,1,1,1);
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);End();
        const auto* values=static_cast<const glm::vec4*>(work->GetMappedPtr());
        for(uint32_t i=0;i<12;++i)
        {
            auto actual=values[i*4],strict=values[i*4+1],baseline=values[i*4+2];
            Check(close(actual,strict.w>0?strict:baseline),"zero-support fallback or supported-path preservation");
            Check(values[i*4+3].x==(i==4?0.0f:1.0f) && values[i*4+3].y==float(records[i].metadata.z) &&
                values[i*4+3].z==float(records[i].metadata.w),"fallback mutated visibility context");
            if(i==5)Check(close(actual,glm::vec4(0)),"unpublished probes fabricated light/support");
            Check(std::abs(values[i*4+3].w-(i==5?0.f:1.f))<1e-5f,"Published support confused occlusion or legal black with an unpublished probe");
        }
        if(variant==0)
        {
            Check(values[0].w>0 && values[0].x>0 && values[1].w==0,"all-blocked fallback did not restore original DDGI");
            Check(values[6*4].w>0 && values[6*4].x==0,"legitimate black irradiance triggered fallback");
            Check(!close(values[1*4],values[1*4+2]),"partial visibility fixture must distinguish baseline from strict sampling");
        }
        if(variant==1)Check(close(values[0],glm::vec4(0)),"baseline distance moments were bypassed");
        if(variant==2)Check(values[0].w>0 && values[0].x==0,"black published DDGI lost support");
        for(uint32_t i=0;i<4;++i)
        {
            Check(close(values[(12+i)*4],glm::vec4(glm::vec3(previewPixels[i]),1)),"GI RGB preview alpha is not opaque");
            Check(close(values[(12+i)*4+1],previewPixels[i]),"preview changed runtime RGBA metadata");
        }
    }
    std::cout<<"[GIReceiverZeroSupportGPU] 36 sampling cases + RGB-only preview: PASS"<<std::endl;
}

void Fixture::TestBias()
{
    struct BiasRecord { glm::vec4 surface,limits; };
    const auto saved=params;
    params={};params.screenSize=glm::vec4(Width,Height,0,0);params.regionInfo=glm::vec4(1,0,0,0);
    params.regions[0].volumeMin=glm::vec4(-1.25f,-2,-2,0);
    params.regions[0].volumeSizeAndBias=glm::vec4(4,4,4,.25f);
    params.regions[0].gridDimensionsAndPriority=glm::vec4(8,1,1,0);
    auto* shader=VansShaderManager::Get().FindRayTracingShader("GIReceiverBias");
    auto* pipeline=shader->GetRayTracingPipeline(device.get(),{rootLayout,passLayout});
    Check(pipeline!=nullptr,"production geometry bias pipeline");
    const float gaps[]={.0287428f,.099937f,.24f,.30f,.3125f,.32f};
    double milliseconds=0.0;
    for(float gap:gaps)
    {
        std::vector<glm::vec4> positions(Width*Height),normals(Width*Height,glm::vec4(1,0,0,0));
        for(uint32_t y=0;y<Height;++y) for(uint32_t x=0;x<Width;++x)
            positions[y*Width+x]=glm::vec4(-gap,(float(x)-256)*.001f,(float(y)-256)*.001f,1);
        BindInput(0,positions);BindInput(1,normals);BindInput(2,std::vector<glm::vec4>(Width*Height,glm::vec4(0)));
        std::memcpy(info->GetMappedPtr(),&params,sizeof(params));
        Begin();
        vkCmdResetQueryPool(Command().GetVKCommandBuffer(),timing,0,2);
        vkCmdWriteTimestamp(Command().GetVKCommandBuffer(),VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timing,0);
        Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        Command().BindRayTracingPipeline(*pipeline);Command().BindRayTracingDescriptorSets(*pipeline,0,{rootSets[0],passSets[0]});
        Command().TraceRays(*pipeline,CacheWidth,CacheHeight,1);
        Barrier(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT|VK_ACCESS_SHADER_READ_BIT);
        vkCmdWriteTimestamp(Command().GetVKCommandBuffer(),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timing,1);End();
        uint64_t ticks[2]{};
        Check(vkGetQueryPoolResults(device->GetLogicDevice(),timing,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS,"bias timestamps");
        milliseconds+=double(ticks[1]-ticks[0])*VansVKDevice::GetTimestampPeriodMs(device->GetPhysicalDevice());
        auto* values=reinterpret_cast<BiasRecord*>(static_cast<char*>(bias->GetMappedPtr())+16);
        constexpr uint32_t target=64*CacheWidth+64;
        float expectedDDGI=std::min(.25f,.8f*gap),expectedScreen=std::min(.05f,.8f*gap);
        Check(std::abs(values[target].limits.x-expectedDDGI)<1e-5f && std::abs(values[target].limits.y-expectedScreen)<1e-5f,
            "bias must preserve clear space and clamp the 2.87 cm gap before geometry");
        std::cout<<"[GIReceiverBiasGPU] gap="<<gap<<" ddgi="<<values[target].limits.x<<" screen="<<values[target].limits.y<<std::endl;
        if(gap==gaps[0])
        {
            std::memset(layout->GetMappedPtr(),0,layout->GetBufferSize());
            for(auto& state:probeStates)state={};
            std::memcpy(states->GetMappedPtr(),probeStates.data(),sizeof(probeStates));
            Frame(0,true,false,false);
            auto* records=reinterpret_cast<Record*>(static_cast<char*>(current->GetMappedPtr())+16);
            Check(records[target].probes[0].x==1u && records[target].probes[0].y==2u,
                "Prepare must choose the cell on the receiver side using measured bias");
            *static_cast<glm::uvec4*>(bias->GetMappedPtr())=glm::uvec4(0);
            Frame(1,true,false,false);
            Check(records[target].probes[0].x==2u && records[target].probes[0].y==3u,
                "disabled bias module must restore the original candidate query");
        }
    }
    // 侧向法线需要独立净空；前方透明裁剪不应压缩原偏移。
    for(uint32_t variant=0;variant<3;++variant)
    {
        const float gap=.0287428f;
        const float wall=variant==1?5.0f:0.0f;
        params.regions[0].volumeMin.x=wall-1.25f;
        std::memcpy(info->GetMappedPtr(),&params,sizeof(params));
        std::vector<glm::vec4> positions(Width*Height);
        for(uint32_t y=0;y<Height;++y)for(uint32_t x=0;x<Width;++x)
            positions[y*Width+x]=glm::vec4(wall-gap,(float(x)-256)*.001f,(float(y)-256)*.001f,1);
        BindInput(0,positions);
        BindInput(1,std::vector<glm::vec4>(Width*Height,glm::vec4(glm::normalize(glm::vec3(1,1,0)),0)));
        BindInput(2,std::vector<glm::vec4>(Width*Height,glm::vec4(0,0,variant==2?8:0,0)));
        Begin();Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        Command().BindRayTracingPipeline(*pipeline);Command().BindRayTracingDescriptorSets(*pipeline,0,{rootSets[0],passSets[0]});
        Command().TraceRays(*pipeline,CacheWidth,CacheHeight,1);
        Barrier(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);End();
        auto value=reinterpret_cast<BiasRecord*>(static_cast<char*>(bias->GetMappedPtr())+16)[64*CacheWidth+64];
        if(variant==0)Check(std::abs(value.limits.x-.8f*gap)<1e-5f && std::abs(value.limits.y-.8f*gap*std::sqrt(2.0f))<1e-5f,
            "normal and shading offsets must use their own ray directions");
        if(variant==1)Check(value.limits.x==.25f && value.limits.y==.05f,"alpha cutout must preserve both offsets");
        if(variant==2)Check(value.limits.w==0.0f,"vegetation must retain existing bias path");
    }
    params=saved;std::memcpy(info->GetMappedPtr(),&params,sizeof(params));
    std::cout<<"[GIReceiverBiasGPU] moving clearance, smooth boundary, directions, candidate cell, disabled path and vegetation passed; meanMs="<<milliseconds/6.0<<std::endl;
}


}
bool TestGIReceiverVisibilityGpuContract()
{
    try
    {
        Fixture gpu;gpu.Initialize();
        struct Case { glm::vec3 P,N,Q; bool visible; };
        const std::vector<Case> cases={
            {{-1,0,0},{-1,0,0},{1,0,0},false},{{1,0,0},{1,0,0},{-1,0,0},false},
            {{-1,2,0},{-1,0,0},{1,2,0},true},{{-1,0,2},{-1,0,0},{1,0,2},true},
            {{0,1,4},{0,1,0},{0,-1,4},false},{{0,-1,4},{0,-1,0},{0,1,4},false},
            {{0,0,4},{0,1,0},{0,2,4},true},{{0,0,4},{0,1,0},{0,-2,4},false},
            {{4,0,0},{-1,0,0},{6,0,0},true},{{-1,0,0},{-1,0,0},{-.5f,0,0},true},
            {{0,.5f,6},{0,0,-1},{0,.5f,8},false},{{2,.5f,6},{0,0,-1},{2,.5f,8},true},
            {{0,2,7.5f},{0,1,0},{0,0,7.5f},false},
            {{9,0,0},{-1,0,0},{11,0,0},false}, // alpha .5 >= cutoff .25
            {{14,0,0},{-1,0,0},{16,0,0},true}, // 同 BLAS、同纹理，cutoff .75
            {{19,0,0},{-1,0,0},{21,0,0},false}, // 透明纹理但未启用裁剪：实体阻挡
            {{24,0,0},{-1,0,0},{26,0,0},false}, // alpha 等于 cutoff 时保留
            {{6,0,0},{1,0,0},{4,0,0},true}}; // 反面同样执行透明裁剪
        gpu.params.regions[0].volumeMin=glm::vec4(-.5f,-.5f,-.5f,0);
        gpu.params.regions[0].volumeSizeAndBias=glm::vec4(float(cases.size()),1,1,0);
        gpu.params.regions[0].gridDimensionsAndPriority=glm::vec4(float(cases.size()),1,1,0);
        std::memcpy(gpu.info->GetMappedPtr(),&gpu.params,sizeof(gpu.params));
        auto* records=reinterpret_cast<Record*>(static_cast<char*>(gpu.current->GetMappedPtr())+16);
        auto* tasks=reinterpret_cast<glm::uvec2*>(static_cast<char*>(gpu.work->GetMappedPtr())+16);
        std::memset(gpu.work->GetMappedPtr(),0xff,VansGIReceiverVisibility::WorkBytes);
        for(uint32_t i=0;i<cases.size();++i)
        {
            records[i]={};records[i].surface=Surface(cases[i].P,cases[i].N);records[i].probes[0].x=i;
            gpu.probeStates[i].published=glm::vec4(cases[i].Q-glm::vec3(float(i),0,0),0);tasks[i]=glm::uvec2(i,0);
        }
        std::memcpy(gpu.states->GetMappedPtr(),gpu.probeStates.data(),sizeof(gpu.probeStates));
        gpu.Begin();gpu.Barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        gpu.Trace();gpu.Barrier(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);gpu.End();
        for(uint32_t i=0;i<cases.size();++i)
        {
            if(records[i].metadata.z!=1u || ((records[i].metadata.w&1u)!=0u)!=cases[i].visible)
                throw std::runtime_error("HW occlusion mismatch at finite-geometry case "+std::to_string(i));
        }
        std::cout<<"[GIReceiverVisibilityGPU] 18 opaque-BLAS wall/floor/stair, both-face, endpoint, self-hit, shared-mesh alpha-cutoff cases passed\n";
        gpu.params.screenSize=glm::vec4(Fixture::Width,Fixture::Height,1.f/Fixture::Width,1.f/Fixture::Height);
        gpu.params.regionInfo=glm::vec4(1,0,0,0);gpu.params.regions[0].volumeMin=glm::vec4(-2,-2,-2,0);
        gpu.params.regions[0].volumeSizeAndBias=glm::vec4(4,4,4,0);gpu.params.regions[0].gridDimensionsAndPriority=glm::vec4(2,2,2,0);
        std::memcpy(gpu.info->GetMappedPtr(),&gpu.params,sizeof(gpu.params));
        for(auto& state:gpu.probeStates) state.published=glm::vec4(0);
        std::memcpy(gpu.states->GetMappedPtr(),gpu.probeStates.data(),sizeof(gpu.probeStates));
        uint32_t total=0;for(uint32_t frame=0;frame<4;++frame) total+=gpu.Frame(frame);
        for(uint32_t i=0;i<Fixture::Count;++i) Check(records[i].metadata.z==255u,"cold-start candidate coverage");
        Check(gpu.Frame(4)==0u,"unchanged surface must reuse visibility");
        // 保存真实 RT 生成的表面与遮挡，后续只改变历史布局或输入视图。
        std::vector<Record> baseline(records, records + Fixture::Count);
        auto* historyRecords = reinterpret_cast<Record*>(static_cast<char*>(gpu.history->GetMappedPtr()) + 16);
        constexpr uint32_t target = 64u * Fixture::CacheWidth + 64u;
        auto seedHistory = [&]()
        {
            *static_cast<glm::uvec4*>(gpu.history->GetMappedPtr()) = glm::uvec4(1, Fixture::CacheWidth, Fixture::CacheHeight, 0);
            std::memcpy(historyRecords, baseline.data(), baseline.size() * sizeof(Record));
            for (uint32_t i = 0; i < Fixture::Count; ++i) historyRecords[i].metadata.y = 10;
            std::memset(gpu.world->GetMappedPtr(), 0, VansGIReceiverVisibility::WorldBytes);
            for (auto& state : gpu.probeStates) state.published = glm::vec4(0);
            std::memcpy(gpu.states->GetMappedPtr(), gpu.probeStates.data(), sizeof(gpu.probeStates));
            std::memcpy(gpu.previous->GetMappedPtr(), gpu.probeStates.data(), sizeof(gpu.probeStates));
        };
        seedHistory();
        for (uint32_t i = 0; i < 8; ++i)
            historyRecords[target].probes[i / 4][i % 4] = 7u - i;
        historyRecords[target].metadata.w = 0xaau;
        gpu.Frame(20, true, false, false, false);
        Check(records[target].metadata.z == 255u && records[target].metadata.w == 0x55u,
            "candidate reorder must remap visibility by probe identity");
        seedHistory();
        for (uint32_t i=0;i<Fixture::Count;++i) historyRecords[i].anchor.z=glm::uintBitsToFloat(1u);
        gpu.Frame(20, true, true, false, false);
        Check(records[target].metadata.z==0u,"Old scrolling epoch reused visibility for another world position");
        seedHistory();
        historyRecords[target].metadata.z &= ~128u;
        historyRecords[target].metadata.w &= ~128u;
        gpu.Frame(20, true, false, false, false);
        Check(records[target].metadata.z == 127u && records[target].metadata.y == 10u,
            "missing candidate must retain known intersection and real trace age");
        Check(gpu.Frame(21, true, false, false) == 1u && records[target].metadata.z == 255u &&
            records[target].metadata.y == 10u && records[target].surface == baseline[target].surface,
            "partial repair must trace only missing probe from the original anchor without refreshing old age");
        seedHistory();
        gpu.probeStates[0].published.x = .25f;
        std::memcpy(gpu.states->GetMappedPtr(), gpu.probeStates.data(), sizeof(gpu.probeStates));
        gpu.Frame(20, true, false, false, false);
        Check(records[target].metadata.z == 254u, "one moved probe must not invalidate the other seven");
        seedHistory();
        historyRecords[target - 1] = historyRecords[target];
        historyRecords[target].metadata.z = 0u;
        historyRecords[target].metadata.w = 0u;
        gpu.Frame(20, true, false, false, false);
        Check(records[target].metadata.z == 255u && records[target].metadata.y == 10u,
            "3x3 fallback must recover an eligible anchor outside the basic 2x2");
        seedHistory();
        historyRecords[target - 8] = historyRecords[target];
        historyRecords[target].metadata.z = 0;
        gpu.Frame(20, true, true, false, false);
        Check(records[target].metadata.z == 255u && records[target].surface == baseline[target].surface,
            "forward reprojection must recover a stable world anchor outside the history search neighborhood");
        seedHistory();
        historyRecords[target].surface.y += .1f;
        gpu.Frame(20, true, true, false, false);
        Check(records[target].metadata.z == 0u, "different plane must reject historical occlusion");
        seedHistory();
        gpu.Frame(20); // 发布世界缓存。
        std::vector<unsigned char> persistentSnapshot(VansGIReceiverVisibility::WorldBytes);
        std::memcpy(persistentSnapshot.data(), gpu.world->GetMappedPtr(), persistentSnapshot.size());
        std::memset(gpu.history->GetMappedPtr(), 0, Fixture::CacheBytes); // 视图历史已丢失。
        gpu.Frame(21, true, true, true, false);
        uint32_t recovered = 0;
        for (uint32_t i = 0; i < Fixture::Count; ++i)
        {
            if (records[i].metadata.z == 0u) continue;
            ++recovered;
            Check(records[i].metadata.y == 10u, "persistent lookup must not refresh trace age");
        }
        Check(recovered > Fixture::Count / 2, "persistent cache must recover most of a recently visible plane");
        // Probe 在离屏期间移动，下一帧状态已相同；缓存仍必须记得该位已失效。
        std::memcpy(gpu.world->GetMappedPtr(), persistentSnapshot.data(), persistentSnapshot.size());
        gpu.BindInput(0, std::vector<glm::vec4>(Fixture::Width * Fixture::Height, glm::vec4(0)));
        gpu.probeStates[0].published.x = .25f;
        std::memcpy(gpu.states->GetMappedPtr(), gpu.probeStates.data(), sizeof(gpu.probeStates));
        gpu.Frame(22);
        gpu.PlaneView(glm::vec2(0), glm::vec2(0));
        gpu.Frame(23, true, true, true, false);
        uint32_t partialRecovered = 0;
        for (uint32_t i = 0; i < Fixture::Count; ++i)
            if (records[i].metadata.z != 0u)
            {
                ++partialRecovered;
                Check((records[i].metadata.z & 1u) == 0u, "offscreen relocation must not resurrect an invalid probe bit");
            }
        Check(partialRecovered > Fixture::Count / 2, "offscreen relocation must preserve unaffected cached probes");
        gpu.BindInput(0, std::vector<glm::vec4>(Fixture::Width * Fixture::Height, glm::vec4(0)));
        gpu.Frame(0); // 模拟几何/场景 reset；清除持久缓存，且无表面可重新发布。
        auto* worldRecords = reinterpret_cast<Record*>(static_cast<char*>(gpu.world->GetMappedPtr()) + 16);
        for (uint32_t i = 0; i < VansGIReceiverVisibility::WorldCapacity; ++i)
            Check(worldRecords[i].metadata.z == 0u, "geometry reset must invalidate offscreen cache");
        std::memcpy(gpu.world->GetMappedPtr(), persistentSnapshot.data(), persistentSnapshot.size());
        gpu.Frame(251);
        for (uint32_t i = 0; i < VansGIReceiverVisibility::WorldCapacity; ++i)
            Check(worldRecords[i].metadata.z == 0u, "persistent cache must expire without lighting or visibility updates");
        gpu.PlaneView(glm::vec2(0), glm::vec2(0));
        seedHistory();
        uint32_t movingRays = 0, movingKnown = 0;
        double movingGpuMilliseconds = 0.0;
        glm::vec2 previousShift(0);
        glm::vec2 previousJitter(0);
        for (uint32_t step = 1; step <= 12; ++step)
        {
            glm::vec2 shift(float(step) * .65f, float(step) * -.35f);
            glm::vec2 jitter(step % 2u != 0u ? .375f : -.375f, step % 3u != 0u ? .25f : -.25f);
            gpu.PlaneView(shift - jitter, previousShift - previousJitter);
            gpu.BindInput(3, std::vector<glm::vec4>(Fixture::Width * Fixture::Height,
                glm::vec4((previousShift - shift) / glm::vec2(Fixture::Width, Fixture::Height), 0, 0)));
            movingRays += gpu.Frame(20 + step, true, true, true, true,
                (previousJitter - jitter) / glm::vec2(Fixture::Width, Fixture::Height));
            movingGpuMilliseconds += gpu.lastGpuMilliseconds;
            for (uint32_t i = 0; i < Fixture::Count; ++i)
            {
                movingKnown += records[i].metadata.z == 255u ? 1u : 0u;
                const auto& record = records[i];
                if (std::abs(record.surface.x) < .0002f) continue;
                for (uint32_t p = 0; p < 8u; ++p)
                {
                    if ((record.metadata.z & (1u << p)) == 0u) continue;
                    uint32_t probe = record.probes[p / 4][p % 4];
                    float probeX = (probe & 1u) != 0u ? 1.0f : -1.0f;
                    bool expectedVisible = record.surface.x * probeX > 0;
                    Check(((record.metadata.w & (1u << p)) != 0u) == expectedVisible,
                        "moving-camera cached bit must match finite-wall visibility at its actual trace origin");
                }
            }
            previousShift = shift;
            previousJitter = jitter;
        }
        Check(movingKnown > Fixture::Count * 10u, "continuous nonzero motion must retain high complete visibility coverage");
        std::cout << "[GIReceiverVisibilityGPU] identity/partial/search/stable-anchor/persistent/reset/age checks passed; recovered="
            << recovered << ", offscreenPartial=" << partialRecovered << ", movingRays=" << movingRays
            << ", movingComplete=" << movingKnown << "/" << Fixture::Count * 12u
            << ", movingGpuMeanMs=" << movingGpuMilliseconds / 12.0 << std::endl;
        gpu.PlaneView(glm::vec2(0), glm::vec2(0));
        seedHistory();
        for (uint32_t i = 0; i < Fixture::Count; ++i) historyRecords[i].metadata.y = 0u;
        for(auto& state:gpu.probeStates) ++state.metadata.y;
        std::memcpy(gpu.states->GetMappedPtr(),gpu.probeStates.data(),sizeof(gpu.probeStates));
        Check(gpu.Frame(5)==0u,"lighting update must not invalidate geometry history");
        gpu.probeStates[0].published.x=.25f;std::memcpy(gpu.states->GetMappedPtr(),gpu.probeStates.data(),sizeof(gpu.probeStates));
        Check(gpu.Frame(6)>0u,"published probe origin change must invalidate history");
        Check(gpu.Frame(0)>0u,"camera/geometry reset must reject old history");
        Check(gpu.Frame(1,false)==0u && static_cast<uint32_t*>(gpu.current->GetMappedPtr())[0]==0u,"disabled path must clear visibility override");
        // 当前点已移出有限墙边缘，旧同平面锚点仍全遮挡；必须在当前点重查。
        gpu.params={};gpu.params.regionInfo=glm::vec4(1,1,.75f,0);
        gpu.params.screenSize=glm::vec4(Fixture::Width,Fixture::Height,0,0);
        gpu.params.regions[0].volumeMin=glm::vec4(-1,-1,.9f,0);
        gpu.params.regions[0].volumeSizeAndBias=glm::vec4(2,2,.2f,0);
        gpu.params.regions[0].gridDimensionsAndPriority=glm::vec4(1,1,1,0);
        std::memcpy(gpu.info->GetMappedPtr(),&gpu.params,sizeof(gpu.params));
        std::memset(gpu.layout->GetMappedPtr(),0,gpu.layout->GetBufferSize());
        gpu.probeStates[0].published=glm::vec4(.1f,.4f,.002f,0);
        gpu.probeStates[0].next=glm::vec4(.1f,.4f,.002f,1);
        gpu.probeStates[0].metadata=glm::uvec4(1,0,256,0);
        std::memcpy(gpu.states->GetMappedPtr(),gpu.probeStates.data(),sizeof(gpu.probeStates));
        std::memcpy(gpu.previous->GetMappedPtr(),gpu.probeStates.data(),sizeof(gpu.probeStates));
        gpu.PlaneView(glm::vec2(-3,999.2f),glm::vec2(-3,999.2f));
        std::memset(gpu.history->GetMappedPtr(),0,Fixture::CacheBytes);
        *static_cast<glm::uvec4*>(gpu.history->GetMappedPtr())=glm::uvec4(1,Fixture::CacheWidth,Fixture::CacheHeight,0);
        constexpr uint32_t edgeIndex=64*Fixture::CacheWidth+64;
        auto* oldEdge=reinterpret_cast<Record*>(static_cast<char*>(gpu.history->GetMappedPtr())+16)+edgeIndex;
        oldEdge->surface=Surface(glm::vec3(-.001f,.2f,.9992f),glm::vec3(0,1,0));
        oldEdge->surface.w-=float(1u<<16u); // 当前夹具的材质类型为 0。
        oldEdge->anchor=glm::vec4(.001f,0,0,0);oldEdge->metadata=glm::uvec4(0,1,1,0);
        for(auto& ids:oldEdge->probes)ids=glm::uvec4(~0u);oldEdge->probes[0].x=0;
        gpu.Frame(2,true,false,false);
        auto* newEdge=reinterpret_cast<Record*>(static_cast<char*>(gpu.current->GetMappedPtr())+16)+edgeIndex;
        Check(newEdge->metadata.z==1u && newEdge->metadata.w==1u,
            "Old fully blocked receiver remained black after moving outside the wall edge");
        Check(std::abs(newEdge->surface.z-1.0012f)<1e-5f,"Blocked receiver was retraced from its stale origin");
        std::cout<<"[GIReceiverVisibilityGPU] current receiver crosses occlusion edge: PASS"<<std::endl;

        gpu.TestBias();
        gpu.TestZeroSupportFallback();
        Check(errors==0u,"Vulkan validation failed");
        std::cout<<"[GIReceiverVisibilityGPU] cold-start coverage="<<Fixture::Count<<", traced="<<total<<", history/light/origin/reset/budget checks passed\n";
        return true;
    }
    catch(const std::exception& error) { std::cerr<<"[GIReceiverVisibilityGPU] "<<error.what()<<std::endl;return false; }
}
