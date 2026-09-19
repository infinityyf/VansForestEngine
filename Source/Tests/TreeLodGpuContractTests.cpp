#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKImage.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationSystem.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <crtdbg.h>

namespace
{
using namespace VansGraphics;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
uint32_t validationErrors=0;
VKAPI_ATTR VkBool32 VKAPI_CALL OnValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* message,void*)
{
    if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validationErrors;std::cerr<<message->pMessage<<'\n';}
    return VK_FALSE;
}
struct Window final:INativeWindowProvider
{
    GLFWwindow* handle=nullptr;
    void* GetNativeWindowHandle()const override{return handle;}
    ~Window(){if(handle)glfwDestroyWindow(handle);glfwTerminate();}
};
struct Resources
{
    VkDevice device;
    std::array<VansVKBuffer,5> buffers;
    VansVKImage image;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout,2> layouts{};
    VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkCommandPool commands=VK_NULL_HANDLE;
    ~Resources(){
        vkDeviceWaitIdle(device);
        if(commands)vkDestroyCommandPool(device,commands,nullptr);
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(shader)vkDestroyShaderModule(device,shader,nullptr);
        if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        for(auto layout:layouts)if(layout)vkDestroyDescriptorSetLayout(device,layout,nullptr);
        image.DestroyVulkanImage(device);
        for(auto& buffer:buffers)buffer.DestroyVulkanBuffer(device);
    }
};
}

bool RunTreeLodGpuContractTests()
{
    try {
        _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
        validationErrors=0;Window window;
        Require(glfwInit()==GLFW_TRUE,"GLFW failed");glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window.handle=glfwCreateWindow(64,64,"Tree LOD GPU contract",nullptr,nullptr);Require(window.handle,"Window failed");
        auto device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);Require(device->IsInitialized(),"Vulkan device failed");
        const auto instance=device->GetInstance();
        const auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
        const auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
        struct Messenger{VkInstance instance;PFN_vkDestroyDebugUtilsMessengerEXT destroy;VkDebugUtilsMessengerEXT handle{};
            ~Messenger(){if(handle)destroy(instance,handle,nullptr);}} messenger{instance,destroy};
        Require(create && destroy,"Enable Vulkan debug validation");
        VkDebugUtilsMessengerCreateInfoEXT message{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        message.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        message.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        message.pfnUserCallback=OnValidation;
        Require(create(instance,&message,nullptr,&messenger.handle)==VK_SUCCESS,"Validation messenger failed");
        {
            Resources r{device->GetLogicDevice()};
            constexpr uint32_t count=6,lods=3;
            struct Camera {glm::vec4 position{},direction{};std::array<glm::mat4,13> matrices;glm::vec4 screen{},frame{},params{};} camera;
            for(auto& matrix:camera.matrices)matrix=glm::mat4(1);
            camera.matrices[1]=glm::perspectiveRH_ZO(glm::radians(60.f),1.f,.1f,2000.f);
            camera.matrices[11]=camera.matrices[1];
            std::array<TreeInstanceGPU,count> trees{};
            const std::array<float,count> distances{10,80,250,1000,63,500};
            for(uint32_t i=0;i<count;++i){trees[i].modelMatrix=glm::translate(glm::mat4(1),glm::vec3(i==5?10000.f:0.f,0,-distances[i]));
                trees[i].boundsSphere=glm::vec4(glm::vec3(trees[i].modelMatrix[3]),1);}
            std::array<uint32_t,lods*2> counts{};
            std::array<uint32_t,count*lods*2> indices{};
            std::array<TreeSpeciesCullInfo,lods> infos{};
            for(uint32_t i=0;i<lods;++i)infos[i]={i*count,count,{0,0}};
            const void* data[]={&camera,trees.data(),counts.data(),indices.data(),infos.data()};
            const size_t sizes[]={sizeof(camera),sizeof(trees),sizeof(counts),sizeof(indices),sizeof(infos)};
            for(size_t i=0;i<5;++i){
                Require(r.buffers[i].CreatVulkanBuffer(r.device,sizes[i],VK_FORMAT_R32_UINT,i==0?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),"GPU buffer allocation failed");
                Require(r.buffers[i].SetBufferData(data[i],0,sizes[i]),"GPU buffer upload failed");
            }
            Require(r.image.CreateVulkanImage(r.device,{1,1,1},VK_FORMAT_R32_SFLOAT,1,1,VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT,VK_SAMPLE_COUNT_1_BIT,false,false,true),"Dummy Hi-Z allocation failed");
            VkDescriptorSetLayoutBinding cameraBinding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
            std::array<VkDescriptorSetLayoutBinding,5> bindings{};
            for(uint32_t i=0;i<5;++i)bindings[i]={i,i==4?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
            VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=1;layout.pBindings=&cameraBinding;
            Require(vkCreateDescriptorSetLayout(r.device,&layout,nullptr,&r.layouts[0])==VK_SUCCESS,"Camera layout failed");
            layout.bindingCount=5;layout.pBindings=bindings.data();Require(vkCreateDescriptorSetLayout(r.device,&layout,nullptr,&r.layouts[1])==VK_SUCCESS,"Tree layout failed");
            const VkDescriptorPoolSize poolSizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=2;pool.poolSizeCount=3;pool.pPoolSizes=poolSizes;
            Require(vkCreateDescriptorPool(r.device,&pool,nullptr,&r.pool)==VK_SUCCESS,"Descriptor pool failed");
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=r.pool;allocate.descriptorSetCount=2;allocate.pSetLayouts=r.layouts.data();
            std::array<VkDescriptorSet,2> sets{};Require(vkAllocateDescriptorSets(r.device,&allocate,sets.data())==VK_SUCCESS,"Descriptor sets failed");
            for(size_t i=0;i<5;++i){VkDescriptorBufferInfo buffer{r.buffers[i].GetNativeBuffer(),0,sizes[i]};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[i==0?0:1];write.dstBinding=i==0?0:uint32_t(i-1);write.descriptorCount=1;
                write.descriptorType=i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer;vkUpdateDescriptorSets(r.device,1,&write,0,nullptr);}
            VkDescriptorImageInfo image{r.image.GetSampler(),r.image.GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet imageWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};imageWrite.dstSet=sets[1];imageWrite.dstBinding=4;imageWrite.descriptorCount=1;
            imageWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;imageWrite.pImageInfo=&image;vkUpdateDescriptorSets(r.device,1,&imageWrite,0,nullptr);
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(TreeCullPushConstants)};
            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=2;pl.pSetLayouts=r.layouts.data();pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
            Require(vkCreatePipelineLayout(r.device,&pl,nullptr,&r.pipelineLayout)==VK_SUCCESS,"Pipeline layout failed");
            std::ifstream input("EngineAssets/Shaders/TreeCull/TreeCullcomp.spv",std::ios::binary|std::ios::ate);Require(bool(input),"Run from the engine source root");
            std::vector<uint32_t> code(size_t(input.tellg())/4);input.seekg(0);input.read(reinterpret_cast<char*>(code.data()),code.size()*4);
            VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=code.size()*4;module.pCode=code.data();
            Require(vkCreateShaderModule(r.device,&module,nullptr,&r.shader)==VK_SUCCESS,"Tree shader failed");
            VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=r.pipelineLayout;
            pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,r.shader,"main",nullptr};
            Require(vkCreateComputePipelines(r.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&r.pipeline)==VK_SUCCESS,"Tree compute pipeline failed");
            VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=device->GetGraphicsQueueFamilyIndex();cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            Require(vkCreateCommandPool(r.device,&cp,nullptr,&r.commands)==VK_SUCCESS,"Command pool failed");
            VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=r.commands;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            VkCommandBuffer command{};Require(vkAllocateCommandBuffers(r.device,&ca,&command)==VK_SUCCESS,"Command buffer failed");
            TreeCullPushConstants pc{};pc.shadowDistance=120;pc.instanceCount=count;pc.lodCount=lods;pc.lodMidDistance=60;pc.lodFarDistance=180;pc.hysteresis=.1f;pc.cullingEnabled=1;
            bool first=true;
            const auto dispatch=[&](){
                counts.fill(0);Require(r.buffers[2].SetBufferData(counts.data(),0,sizeof(counts)),"Count reset failed");
                vkResetCommandBuffer(command,0);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Require(vkBeginCommandBuffer(command,&begin)==VK_SUCCESS,"Begin failed");
                if(first){VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=r.image.GetImage();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);first=false;}
                VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,nullptr,0,nullptr);
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipelineLayout,0,2,sets.data(),0,nullptr);
                vkCmdPushConstants(command,r.pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),&pc);vkCmdDispatch(command,1,1,1);
                ready.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&ready,0,nullptr,0,nullptr);
                Require(vkEndCommandBuffer(command)==VK_SUCCESS,"End failed");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
                Require(vkQueueSubmit(device->GetGraphicsQueue(),1,&submit,VK_NULL_HANDLE)==VK_SUCCESS && vkQueueWaitIdle(device->GetGraphicsQueue())==VK_SUCCESS,"Dispatch failed");
                for(auto& buffer:r.buffers)buffer.InvalidateMappedRange(0,buffer.GetBufferSize());
                std::memcpy(counts.data(),r.buffers[2].GetMappedPtr(),sizeof(counts));
            };
            dispatch();
            Require(counts==std::array<uint32_t,6>{2,1,2,0,2,1},"Near/mid/far bins, frustum or shadow grouping mismatch");
            auto* gpuTrees=static_cast<TreeInstanceGPU*>(r.buffers[1].GetMappedPtr());
            Require(gpuTrees[3].flags==2,"1000m tree disappeared instead of using LOD2");
            gpuTrees[4].flags=1;r.buffers[1].FlushMappedRange(0,sizeof(trees));dispatch();
            Require(counts[0]==1 && counts[1]==2 && gpuTrees[4].flags==1,"Distance boundary hysteresis failed");
            pc.cullingEnabled=0;dispatch();Require(counts[0]+counts[1]+counts[2]==count,"Disabled frustum culling dropped trees");
            std::cout<<"[TreeLodGPU] PASS: distance bins, 1000m LOD2, frustum, hysteresis, independent shadow LOD\n";
        }
        Require(validationErrors==0,"Vulkan validation reported errors");return true;
    }catch(const std::exception& exception){std::cerr<<"[TreeLodGPU] "<<exception.what()<<'\n';return false;}
}
