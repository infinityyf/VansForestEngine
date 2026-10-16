#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VansVideoManager.h"
#include "../EngineCore/SceneCore/VansSceneResourcePlan.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
using namespace VansGraphics;
using Clock=std::chrono::steady_clock;
uint32_t validationErrors=0;
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*)
{
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validationErrors;std::cerr<<"[VideoGPU validation] "<<data->pMessage<<'\n';}
    return VK_FALSE;
}
struct Window final:INativeWindowProvider
{
    GLFWwindow* value=nullptr;
    void* GetNativeWindowHandle()const override{return value;}
    ~Window(){if(value)glfwDestroyWindow(value);glfwTerminate();}
};
struct Command
{
    VkDevice device;VansVKCommandBuffer value;
    Command(VansVKDevice& backend,uint32_t family):device(backend.GetLogicDevice())
    {
        CommandBufferCreateParams info{VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
        Check(value.CreateVulkanCommandBuffer(backend,family,info),"Video command allocation failed");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        Check(vkCreateFence(device,&fence,nullptr,&value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"Video fence allocation failed");
    }
    ~Command(){if(value.m_CommandBufferFinishSubmitFence)vkDestroyFence(device,value.m_CommandBufferFinishSubmitFence,nullptr);value.DestroyVulkanCommandBuffer(device);}
};
struct Fixture
{
    std::filesystem::path root=std::filesystem::temp_directory_path()/("forest-video-ring-"+std::to_string(Clock::now().time_since_epoch().count()));
    Fixture(){std::filesystem::create_directories(root);}
    ~Fixture(){std::error_code error;std::filesystem::remove_all(root,error);}
    std::filesystem::path Write(int width,int height,int frames,int phase)
    {
        const auto path=root/(std::to_string(phase)+".y4m");
        std::ofstream file(path,std::ios::binary);
        file<<"YUV4MPEG2 W"<<width<<" H"<<height<<" F1:1 Ip A1:1 C420jpeg\n";
        std::vector<uint8_t> luma(size_t(width)*height),chroma(size_t(width)*height/4,128);
        for(int frame=0;frame<frames;++frame)
        {
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                luma[size_t(y)*width+x]=((frame-frame/3+phase+(x>=width/2)+(y>=height/2))%2)?235:16;
            file<<"FRAME\n";file.write(reinterpret_cast<const char*>(luma.data()),luma.size());
            for(int plane=0;plane<2;++plane)file.write(reinterpret_cast<const char*>(chroma.data()),chroma.size());
        }
        Check(bool(file),"Video pressure fixture write failed");return path;
    }
};
struct Harness
{
    VansVKDevice& backend;VkDevice device;
    VansVideoManager videos;VansTexture target;
    VansQueueTimelines timelines;VansCrossFrameResourceLedger ledger;
    std::array<VansFrameSubmitOrchestrator,2> writers,readers;
    std::array<std::unique_ptr<Command>,2> writeCommands,readCommands;
    VansVKBuffer readback;
    VkSemaphore gate=VK_NULL_HANDLE;uint64_t gateValue=0;
    PFN_vkSignalSemaphore signalSemaphore=nullptr;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;VkShaderModule shader=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
    explicit Harness(VansVKDevice& value):backend(value),device(value.GetLogicDevice()){}
    ~Harness()
    {
        // 失败路径也先释放测试门闩，避免清理时等待被测试夹具永久阻塞的队列。
        if(gate&&signalSemaphore){VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};signal.semaphore=gate;signal.value=gateValue+1;signalSemaphore(device,&signal);}
        backend.WaitForDevice();
        for(auto& graph:readers)graph.Shutdown();for(auto& graph:writers)graph.Shutdown();timelines.Shutdown();
        readback.DestroyVulkanBuffer(device);
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(shader)vkDestroyShaderModule(device,shader,nullptr);
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        if(setLayout)vkDestroyDescriptorSetLayout(device,setLayout,nullptr);
        if(gate)vkDestroySemaphore(device,gate,nullptr);
    }
};
double Milliseconds(Clock::time_point begin){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}
double Percentile(std::vector<double> values,double percentile)
{std::sort(values.begin(),values.end());return values[std::min(values.size()-1,size_t(std::ceil(values.size()*percentile)-1))];}

void Run(VansVKDevice& device,bool async,int width,int height,uint32_t videoCount,int frames)
{
    Fixture fixture;std::array<std::filesystem::path,2> paths{fixture.Write(width,height,frames,0),fixture.Write(width,height,frames,1)};
    Harness h(device);auto& command=device.GetImmediateGraphicsCommandBuffer();
    VkQueue compute=device.GetGraphicsQueue();
    if(async)vkGetDeviceQueue(h.device,device.GetComputeQueueFamilyIndex(),0,&compute);
    Check(!async||compute!=device.GetGraphicsQueue(),"Dedicated compute queue unavailable for video cross-queue contract");
    Check(h.timelines.Initialize(h.device,device.GetGraphicsQueue(),compute),"Video timelines initialization failed");
    for(uint32_t slot=0;slot<2;++slot)
    {
        h.writeCommands[slot]=std::make_unique<Command>(device,device.GetGraphicsQueueFamilyIndex());
        h.readCommands[slot]=std::make_unique<Command>(device,async?device.GetComputeQueueFamilyIndex():device.GetGraphicsQueueFamilyIndex());
        for(auto* graph:{&h.writers[slot],&h.readers[slot]})
        {graph->Bind(h.device,device.GetGraphicsQueue(),compute);graph->SetQueueTimelines(&h.timelines);graph->SetCrossFrameResourceLedger(&h.ledger);}
    }
    VkSemaphoreTypeCreateInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};timeline.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
    h.signalSemaphore=reinterpret_cast<PFN_vkSignalSemaphore>(vkGetDeviceProcAddr(h.device,"vkSignalSemaphore"));
    Check(h.signalSemaphore!=nullptr,"Video host timeline signal unavailable");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};semaphore.pNext=&timeline;
    Check(vkCreateSemaphore(h.device,&semaphore,nullptr,&h.gate)==VK_SUCCESS,"Video test gate allocation failed");
    std::vector<Vans::VansSceneVideoResourceRequest> requests;
    for(uint32_t i=0;i<videoCount;++i)
    {Vans::VansSceneVideoResourceRequest request;request.name="pressure-"+std::to_string(i);request.assetGuid=request.name;request.path=paths[i%2].string();request.autoplay=true;request.loop=false;request.srgb=true;requests.push_back(request);}
    h.videos.Load(requests,&device);
    std::vector<VansVideoTexture*> videos;std::vector<VansRenderRectLightVideoFrameData> bindings;
    for(uint32_t i=0;i<videoCount;++i)
    {
        auto* video=h.videos.Get(requests[i].name);Check(video&&video->IsReady(),"Video pressure decode initialization failed");videos.push_back(video);
        const auto index=h.videos.FindRuntimeIndex(video);bindings.push_back({index,int(i*2)});bindings.push_back({index,int(i*2+1)});
    }
    h.target.InitTextureArray(command,256,256,int(videoCount*2),VK_FORMAT_R8G8B8A8_SRGB,true);
    const uint32_t mips=h.target.GetImage().GetImageCreateInfo().mipLevels,samples=4+8*mips;
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(),&properties);
    const VkDeviceSize alignment=properties.limits.minStorageBufferOffsetAlignment;
    const VkDeviceSize stride=(samples*sizeof(glm::vec4)+alignment-1)/alignment*alignment;
    const VkDeviceSize bytes=VkDeviceSize(2)*videoCount*stride;
    Check(h.readback.CreatVulkanBuffer(h.device,bytes,VK_FORMAT_R32G32B32A32_SFLOAT,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&h.readback.PersistentMap(),"Video sampling buffer allocation failed");
    const std::array<VkDescriptorSetLayoutBinding,3> layoutBindings{{
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};setInfo.bindingCount=3;setInfo.pBindings=layoutBindings.data();
    Check(vkCreateDescriptorSetLayout(h.device,&setInfo,nullptr,&h.setLayout)==VK_SUCCESS,"Video sampling layout failed");
    const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,videoCount*4},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,videoCount*2}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=videoCount*2;pool.poolSizeCount=2;pool.pPoolSizes=sizes.data();
    Check(vkCreateDescriptorPool(h.device,&pool,nullptr,&h.pool)==VK_SUCCESS,"Video sampling pool failed");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipelineLayout.setLayoutCount=1;pipelineLayout.pSetLayouts=&h.setLayout;pipelineLayout.pushConstantRangeCount=1;pipelineLayout.pPushConstantRanges=&range;
    Check(vkCreatePipelineLayout(h.device,&pipelineLayout,nullptr,&h.layout)==VK_SUCCESS,"Video pipeline layout failed");
    std::ifstream shaderFile("EngineAssets/Validation/GIProbe/VideoUploadContractcomp.spv",std::ios::binary|std::ios::ate);
    Check(bool(shaderFile),"Video validation shader missing");const auto length=shaderFile.tellg();Check(length>0&&size_t(length)%4==0,"Video shader size invalid");
    std::vector<uint32_t> spirv(size_t(length)/4);shaderFile.seekg(0);shaderFile.read(reinterpret_cast<char*>(spirv.data()),length);
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=size_t(length);module.pCode=spirv.data();
    Check(vkCreateShaderModule(h.device,&module,nullptr,&h.shader)==VK_SUCCESS,"Video shader module failed");
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=h.layout;
    pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,h.shader,"main",nullptr};
    Check(vkCreateComputePipelines(h.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&h.pipeline)==VK_SUCCESS,"Video compute pipeline failed");
    std::vector<VkDescriptorSet> sets(videoCount*2);
    for(uint32_t i=0;i<videoCount*2;++i)
    {
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=h.pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&h.setLayout;
        Check(vkAllocateDescriptorSets(h.device,&allocate,&sets[i])==VK_SUCCESS,"Video sampling descriptor allocation failed");
        const std::array<VkDescriptorImageInfo,2> images{{
            {videos[i%videoCount]->GetSampler(),videos[i%videoCount]->GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {h.target.GetImage().GetSampler(),h.target.GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
        VkDescriptorBufferInfo buffer{h.readback.GetNativeBuffer(),i*stride,samples*sizeof(glm::vec4)};std::array<VkWriteDescriptorSet,3> writes{};
        for(uint32_t binding=0;binding<3;++binding)
        {auto& write=writes[binding];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[i];write.dstBinding=binding;write.descriptorCount=1;write.descriptorType=layoutBindings[binding].descriptorType;if(binding<2)write.pImageInfo=&images[binding];else write.pBufferInfo=&buffer;}
        vkUpdateDescriptorSets(h.device,3,writes.data(),0,nullptr);
    }
    std::array<int,2> pending{-1,-1};std::vector<double> recordMs,selectMs,decodeWaitMs;uint32_t gatedPairs=0,unchangedUploads=0;
    const auto retire=[&](uint32_t slot)
    {
        if(pending[slot]<0)return;
        Check(h.readers[slot].WaitForCompletion()&&h.writers[slot].WaitForCompletion(),"Video slot retirement failed");
        h.readback.InvalidateMappedRange(0,bytes);
        for(uint32_t video=0;video<videoCount;++video)for(uint32_t sample=0;sample<samples;++sample)
        {
            const auto* data=reinterpret_cast<const glm::vec4*>(static_cast<const uint8_t*>(h.readback.GetMappedPtr())+(slot*videoCount+video)*stride);
            const uint32_t corner=sample<4?sample:(sample-4)%4,mip=sample<4?0:((sample-4)/4)%mips;
            const float expected=sample>=4&&mip==mips-1?.5f:float((pending[slot]-pending[slot]/3+video+(corner&1)+((corner>>1)&1))%2);
            const auto actual=data[sample];
            Check(std::abs(actual.x-expected)<.006f&&std::abs(actual.y-expected)<.006f&&std::abs(actual.z-expected)<.006f&&actual.w==1,
                "Video frame/layer/mip overwritten: frame="+std::to_string(pending[slot])+" video="+std::to_string(video)+" sample="+std::to_string(sample)+" expected="+std::to_string(expected)+" actual="+std::to_string(actual.x));
        }
        for(auto* cmd:{h.writeCommands[slot].get(),h.readCommands[slot].get()})
            Check(vkResetFences(h.device,1,&cmd->value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"Video slot fence reset failed");
        pending[slot]=-1;
    };
    for(int frame=0;frame<frames;++frame)
    {
        const uint32_t slot=uint32_t(frame)%2;retire(slot);double selectedMs=0,waitingMs=0;
        for(auto* video:videos)
        {
            auto begin=Clock::now();bool selected=video->Tick(frame?1.0:0.0);selectedMs+=Milliseconds(begin);
            const auto deadline=Clock::now()+std::chrono::seconds(10);
            while(!selected&&Clock::now()<deadline)
            {begin=Clock::now();std::this_thread::sleep_for(std::chrono::milliseconds(1));waitingMs+=Milliseconds(begin);begin=Clock::now();selected=video->Tick(0);selectedMs+=Milliseconds(begin);}
            Check(selected,"Video pressure decoder did not supply the next frame");
        }
        auto& writer=h.writeCommands[slot]->value;auto& reader=h.readCommands[slot]->value;
        Check(writer.ResetCommandBuffer(false)&&writer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Video writer begin failed");
        if(frame==0)
        {
            bool rejected=false;try{h.videos.RecordPendingUploads(writer,2,&h.target,bindings);}catch(const std::invalid_argument&){rejected=true;}
            Check(rejected,"Invalid frame slot accepted");
        }
        const auto begin=Clock::now();const auto& uploads=h.videos.RecordPendingUploads(writer,slot,&h.target,bindings);const auto elapsed=Milliseconds(begin);
        Check(uploads.size()==videoCount,"Multi-video upload lost a pending frame");
        for(const auto& upload:uploads)
        {
            const bool unchanged=frame>0&&frame%3==0;
            Check((upload.result.radianceChange==0)==unchanged,"Video content response disagrees with the selected pixels");
            unchangedUploads+=unchanged?1u:0u;
        }
        Check(writer.EndCommandBufferRecord(),"Video writer end failed");
        h.writers[slot].Reset();VansFrameSubmitNode producer;producer.name="VideoUpload";producer.commandBuffers={writer.GetVKCommandBuffer()};producer.fence=writer.m_CommandBufferFinishSubmitFence;
        h.videos.AppendFrameResources(producer,&h.target,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,true);
        if(slot==0){h.gateValue=uint64_t(frame/2)+1;producer.externalWaits.push_back({h.gate,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,h.gateValue,true});}
        h.writers[slot].AddNode(std::move(producer));Check(h.writers[slot].Execute(),h.writers[slot].GetLastError());h.videos.ResolveRecordedUploads(true);
        Check(reader.ResetCommandBuffer(false)&&reader.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Video reader begin failed");
        vkCmdBindPipeline(reader.GetVKCommandBuffer(),VK_PIPELINE_BIND_POINT_COMPUTE,h.pipeline);
        for(uint32_t video=0;video<videoCount;++video)
        {
            const std::array<uint32_t,4> params{0,video*2,video*2+1,mips};
            vkCmdBindDescriptorSets(reader.GetVKCommandBuffer(),VK_PIPELINE_BIND_POINT_COMPUTE,h.layout,0,1,&sets[slot*videoCount+video],0,nullptr);
            vkCmdPushConstants(reader.GetVKCommandBuffer(),h.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,params.data());vkCmdDispatch(reader.GetVKCommandBuffer(),(samples+63)/64,1,1);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        reader.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{host});Check(reader.EndCommandBufferRecord(),"Video reader end failed");
        h.readers[slot].Reset();VansFrameSubmitNode consumer;consumer.name="VideoConsumer";consumer.queue=async?VansQueueRole::Compute:VansQueueRole::Graphics;
        consumer.commandBuffers={reader.GetVKCommandBuffer()};consumer.fence=reader.m_CommandBufferFinishSubmitFence;
        h.videos.AppendFrameResources(consumer,&h.target,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,false);h.readers[slot].AddNode(std::move(consumer));
        Check(h.readers[slot].Execute(),h.readers[slot].GetLastError());pending[slot]=frame;
        if(slot==1)
        {
            Check(pending[0]>=0&&vkGetFenceStatus(h.device,h.writeCommands[0]->value.m_CommandBufferFinishSubmitFence)==VK_NOT_READY&&
                vkGetFenceStatus(h.device,writer.m_CommandBufferFinishSubmitFence)==VK_NOT_READY,"Two frames were not actually queued behind the GPU gate");
            VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};signal.semaphore=h.gate;signal.value=h.gateValue;
            Check(h.signalSemaphore(h.device,&signal)==VK_SUCCESS,"Video test gate release failed");++gatedPairs;
        }
        if(frame>=4){recordMs.push_back(elapsed);selectMs.push_back(selectedMs);decodeWaitMs.push_back(waitingMs);}
    }
    retire(0);retire(1);Check(validationErrors==0,"Video synchronization validation failed");
    std::cout<<"[VideoGPU] PASS queue="<<(async?"compute":"graphics")<<" size="<<width<<"x"<<height<<" videos="<<videoCount<<" frames="<<frames<<" gatedPairs="<<gatedPairs
        <<" unchangedUploads="<<unchangedUploads<<" stagingBytes="<<uint64_t(width)*height*4*videoCount*2<<" recordCpuMedianMs="<<Percentile(recordMs,.5)<<" recordCpuP95Ms="<<Percentile(recordMs,.95)
        <<" selectCpuP95Ms="<<Percentile(selectMs,.95)<<" decodeWaitP95Ms="<<Percentile(decodeWaitMs,.95)<<" validationErrors="<<validationErrors<<'\n';
}
}

bool TestVideoUploadGpuContract()
{
    try
    {
        Window window;Check(glfwInit()==GLFW_TRUE,"Video GLFW initialization failed");glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window.value=glfwCreateWindow(64,64,"Video upload contract",nullptr,nullptr);Check(window.value!=nullptr,"Video hidden window failed");
        auto device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);Check(device->IsInitialized(),"Video Vulkan initialization failed");m_GraphicsDevice=device.get();
        VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
        const auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkCreateDebugUtilsMessengerEXT"));
        const auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkDestroyDebugUtilsMessengerEXT"));
        struct Messenger{VkInstance instance;VkDebugUtilsMessengerEXT& handle;PFN_vkDestroyDebugUtilsMessengerEXT destroy;~Messenger(){if(handle&&destroy)destroy(instance,handle,nullptr);}} lifetime{device->GetInstance(),messenger,destroy};
#ifdef _DEBUG
        const char* layers=std::getenv("VK_INSTANCE_LAYERS");Check(layers&&std::string(layers).find("VK_LAYER_KHRONOS_validation")!=std::string::npos,"Debug video contract requires explicit validation layer");
        Check(create&&destroy,"Video debug utils unavailable");VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;info.pfnUserCallback=Validation;
        Check(create(device->GetInstance(),&info,nullptr,&messenger)==VK_SUCCESS,"Video validation messenger failed");
#endif
        // 基础发布用例的象限边界与数组格点对齐；缩放过滤已有独立颜色合同。
        Run(*device,false,256,256,4,16);Run(*device,true,256,256,4,16);
        if(std::getenv("FORESTENGINE_VIDEO_UPLOAD_BENCHMARK")){Run(*device,true,1920,1080,4,64);Run(*device,true,3840,2160,2,64);}
        Check(validationErrors==0,"Video validation errors observed");return true;
    }
    catch(const std::exception& error){std::cerr<<"[VideoGPU] FAIL: "<<error.what()<<'\n';return false;}
}
