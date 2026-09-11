#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include <string>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>

std::filesystem::path DecalContractShaderPath();

namespace
{
using namespace VansGraphics;
void Require(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
uint32_t validationErrors = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        ++validationErrors;
        std::cerr << "[DecalGPU validation] " << message->pMessage << '\n';
    }
    return VK_FALSE;
}
struct GPU
{
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
    ~GPU()
    {
        if (device)
        {
            vkDeviceWaitIdle(device);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
            if (shader) vkDestroyShaderModule(device, shader, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            if (buffer) vkDestroyBuffer(device, buffer, nullptr);
            if (memory) vkFreeMemory(device, memory, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};
}

bool TestDecalGpuContract()
{
    using namespace VansGraphics;
    try
    {
        GPU gpu;
        validationErrors = 0;
        // 独立 shader 测试直接使用系统 Vulkan，避免初始化与此测试无关的上采样插件。
        vulkan_library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!vulkan_library || !LoadVulkanExportedFunction() || !LoadVulkanGlobalLevelFunctions())
            throw std::runtime_error("Vulkan loader unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Forest Decal GPU Contract";
        app.apiVersion = VK_API_VERSION_1_2;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback = DebugMessage;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pNext = &debug; instanceInfo.pApplicationInfo = &app;
        instanceInfo.enabledLayerCount = 1; instanceInfo.ppEnabledLayerNames = &layer;
        instanceInfo.enabledExtensionCount = 1; instanceInfo.ppEnabledExtensionNames = &extension;
        Require(vkCreateInstance(&instanceInfo, nullptr, &gpu.instance), "create instance");
        if (!LoadVulkanInstanceLevelFunctions(gpu.instance)) throw std::runtime_error("Instance functions unavailable");
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(gpu.instance, "vkCreateDebugUtilsMessengerEXT"));
        gpu.destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(gpu.instance, "vkDestroyDebugUtilsMessengerEXT"));
        Require(createMessenger(gpu.instance, &debug, nullptr, &gpu.messenger), "create validation messenger");
        uint32_t count = 0;
        Require(vkEnumeratePhysicalDevices(gpu.instance, &count, nullptr), "enumerate devices");
        std::vector<VkPhysicalDevice> physicals(count);
        Require(vkEnumeratePhysicalDevices(gpu.instance, &count, physicals.data()), "read devices");
        if (physicals.empty()) throw std::runtime_error("No Vulkan GPU");
        const auto physical = physicals.front();
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::cout << "[DecalGPU] " << properties.deviceName << '\n';
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        uint32_t family = 0;
        while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
        if (family == count) throw std::runtime_error("No compute queue");
        const float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        Require(vkCreateDevice(physical, &deviceInfo, nullptr, &gpu.device), "create device");
        if (!LoadVulkanDeviceLevelFunctions(gpu.device)) throw std::runtime_error("Device functions unavailable");
        VkQueue queue;
        vkGetDeviceQueue(gpu.device, family, 0, &queue);
        constexpr VkDeviceSize outputBytes = 34 * 4 * sizeof(float);
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = outputBytes; bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Require(vkCreateBuffer(gpu.device, &bufferInfo, nullptr, &gpu.buffer), "create output buffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(gpu.device, gpu.buffer, &requirements);
        VkPhysicalDeviceMemoryProperties memoryProperties;
        vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
        uint32_t memoryType = 0;
        const VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        while (memoryType < memoryProperties.memoryTypeCount &&
            (!(requirements.memoryTypeBits & (1u << memoryType)) || (memoryProperties.memoryTypes[memoryType].propertyFlags & needed) != needed)) ++memoryType;
        if (memoryType == memoryProperties.memoryTypeCount) throw std::runtime_error("No host coherent memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memoryType;
        Require(vkAllocateMemory(gpu.device, &allocation, nullptr, &gpu.memory), "allocate output memory");
        Require(vkBindBufferMemory(gpu.device, gpu.buffer, gpu.memory, 0), "bind output memory");
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = 1; setInfo.pBindings = &binding;
        Require(vkCreateDescriptorSetLayout(gpu.device, &setInfo, nullptr, &gpu.setLayout), "create descriptor layout");
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = &poolSize;
        Require(vkCreateDescriptorPool(gpu.device, &poolInfo, nullptr, &gpu.descriptorPool), "create descriptor pool");
        VkDescriptorSetAllocateInfo setAllocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAllocation.descriptorPool = gpu.descriptorPool; setAllocation.descriptorSetCount = 1; setAllocation.pSetLayouts = &gpu.setLayout;
        VkDescriptorSet set;
        Require(vkAllocateDescriptorSets(gpu.device, &setAllocation, &set), "allocate descriptor");
        VkDescriptorBufferInfo bufferBinding{gpu.buffer, 0, outputBytes};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &bufferBinding;
        vkUpdateDescriptorSets(gpu.device, 1, &write, 0, nullptr);
        std::ifstream file(DecalContractShaderPath(), std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Compile DecalContract.comp before running the GPU contract");
        const size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> code(size/4); file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), size);
        VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = size; shaderInfo.pCode = code.data();
        Require(vkCreateShaderModule(gpu.device, &shaderInfo, nullptr, &gpu.shader), "create shader");
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &gpu.setLayout;
        Require(vkCreatePipelineLayout(gpu.device, &layoutInfo, nullptr, &gpu.layout), "create pipeline layout");
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, gpu.shader, "main", nullptr};
        pipelineInfo.layout = gpu.layout;
        Require(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gpu.pipeline), "create compute pipeline");
        VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPoolInfo.queueFamilyIndex = family;
        Require(vkCreateCommandPool(gpu.device, &commandPoolInfo, nullptr, &gpu.commandPool), "create command pool");
        VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocation.commandPool = gpu.commandPool; commandAllocation.commandBufferCount = 1;
        VkCommandBuffer command;
        Require(vkAllocateCommandBuffers(gpu.device, &commandAllocation, &command), "allocate command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Require(vkBeginCommandBuffer(command, &begin), "begin commands");
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(command, 1, 1, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        Require(vkEndCommandBuffer(command), "end commands");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        Require(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit");
        Require(vkQueueWaitIdle(queue), "wait GPU");
        void* mapped;
        Require(vkMapMemory(gpu.device, gpu.memory, 0, outputBytes, 0, &mapped), "map results");
        const float* values = static_cast<const float*>(mapped);
        bool success = true;
        auto expect = [&](size_t index, std::array<float,4> expected) {
            for (size_t c=0; c<4; ++c)
                if (!std::isfinite(values[index*4+c]) || std::abs(values[index*4+c]-expected[c]) > 0.0001f)
                { success = false; std::cerr << "[DecalGPU] result=" << index << " channel=" << c << " actual=" << values[index*4+c] << " expected=" << expected[c] << '\n'; }
        };
        for (int id=0; id<12; ++id)
        {
            const bool full = id==1 || id==2 || id==3 || id==5 || id==7;
            const bool colorOnly = id==8 || id==10;
            expect(id*2, full ? std::array<float,4>{0.4f,0.25f,0.4f,0.7f} :
                colorOnly ? std::array<float,4>{0.4f,0.25f,0.4f,0.8f} : std::array<float,4>{0.2f,0.4f,0.6f,0.8f});
            expect(id*2+1, full ? std::array<float,4>{0,0.70710678f,0.70710678f,float(id)} : std::array<float,4>{0,1,0,float(id)});
        }
        for (size_t index=24; index<27; ++index) expect(index, {0,1,0,0});
        expect(27, {0.05f,0,0.2f,0});
        expect(28, {0,-1,0,0.8f}); expect(29, {0,1,0,0.8f}); expect(30, {0.2f,0.4f,0.6f,0.8f});
        expect(31, {1,0,0,1}); expect(32, {1,0,0,1}); expect(33, {1,1,0,0});
        vkUnmapMemory(gpu.device, gpu.memory);
        success &= validationErrors == 0;
        if (success) std::cout << "[DecalGPU] PASS 12 material IDs, independent coverage, zero/cancelled/NaN/Inf normals; validation errors=0\n";
        return success;
    }
    catch (const std::exception& error) { std::cerr << "[DecalGPU] " << error.what() << '\n'; return false; }
}
