#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VansCameraFrameData.h"
#include "../EngineCore/RenderCore/AmbientOcclusionCore/VansGTAO.h"
#include <glm/gtc/matrix_transform.hpp>
#include "../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/gtc/packing.hpp>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace VansGraphics;
    constexpr uint32_t Width = 17, Height = 13, Count = Width * Height;
    enum ImageSlot { Position, Normal, Material, Depth, Raw, Edges, Result, ImageCount };
    std::atomic<uint32_t> validationErrors{0};
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        ++validationErrors;
        std::cerr << "[GTAO Vulkan] " << message->pMessage << '\n';
        return VK_FALSE;
    }
    void Require(VkResult result, const char* action)
    {
        if (result != VK_SUCCESS) throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
    }
    void Expect(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    std::filesystem::path ShaderPath(const char* name)
    {
        auto root = std::filesystem::current_path();
        for (;;)
        {
            const auto relative = std::filesystem::path("EngineAssets/Shaders") / name;
            if (std::filesystem::exists(root / relative)) return root / relative;
            if (std::filesystem::exists(root / "ForestEngine" / relative)) return root / "ForestEngine" / relative;
            if (root == root.root_path()) throw std::runtime_error("GTAO shader not found");
            root = root.parent_path();
        }
    }
    struct Image { VkImage image{}; VkImageView view{}; VkDeviceMemory memory{}; VkFormat format{}; uint32_t components = 4; bool half = false; uint32_t levels = 1; std::array<VkImageView, 5> mipViews{}; };
    struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped{}; VkDeviceSize size{}; };
    struct Pipeline { VkDescriptorSetLayout setLayout{}; VkPipelineLayout layout{}; VkShaderModule shader{}; VkPipeline pipeline{}; };
    // Run production GTAO SPIR-V with sync validation, odd dimensions and GPU readback.
    struct Fixture
    {
        VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
        uint32_t family = 0; VkDebugUtilsMessengerEXT messenger{};
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
        VkCommandPool commands{}; VkCommandBuffer command{}; VkDescriptorPool pool{}; VkSampler sampler{};
        VkDescriptorSetLayout cameraLayout{}; VkDescriptorSet cameraSet{};
        std::array<VkDescriptorSet, 5> depthSets{}; VkDescriptorSet mainSet{}, denoiseSet{};
        std::array<Image, ImageCount> images{};
        std::array<Buffer, 3> buffers{}; // camera, upload, readback
        std::array<Pipeline, 3> pipelines{};
        VkDeviceSize uploadOffset = 0;
        VansCameraDataGPU camera{};
        std::vector<glm::vec4> position, normal, material, raw;
        ~Fixture()
        {
            if (device)
            {
                vkDeviceWaitIdle(device);
                for (auto& p : pipelines)
                {
                    if (p.pipeline) vkDestroyPipeline(device, p.pipeline, nullptr);
                    if (p.layout) vkDestroyPipelineLayout(device, p.layout, nullptr);
                    if (p.shader) vkDestroyShaderModule(device, p.shader, nullptr);
                    if (p.setLayout) vkDestroyDescriptorSetLayout(device, p.setLayout, nullptr);
                }
                if (commands) vkDestroyCommandPool(device, commands, nullptr);
                if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
                if (cameraLayout) vkDestroyDescriptorSetLayout(device, cameraLayout, nullptr);
                if (sampler) vkDestroySampler(device, sampler, nullptr);
                for (auto& i : images)
                {
                    for (auto view : i.mipViews) if (view) vkDestroyImageView(device, view, nullptr);
                    if (i.view) vkDestroyImageView(device, i.view, nullptr);
                    if (i.image) vkDestroyImage(device, i.image, nullptr);
                    if (i.memory) vkFreeMemory(device, i.memory, nullptr);
                }
                for (auto& b : buffers)
                {
                    if (b.mapped) vkUnmapMemory(device, b.memory);
                    if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
                    if (b.memory) vkFreeMemory(device, b.memory, nullptr);
                }
                vkDestroyDevice(device, nullptr);
            }
            if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
            if (instance) vkDestroyInstance(instance, nullptr);
        }
        uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags)
        {
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(physical, &properties);
            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
            throw std::runtime_error("Required GPU memory type unavailable");
        }
        void CreateBuffer(uint32_t slot, VkDeviceSize size)
        {
            auto& b = buffers[slot]; b.size = size;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = size; info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            Require(vkCreateBuffer(device, &info, nullptr, &b.buffer), "create buffer");
            VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, b.buffer, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Require(vkAllocateMemory(device, &allocation, nullptr, &b.memory), "allocate buffer");
            Require(vkBindBufferMemory(device, b.buffer, b.memory, 0), "bind buffer");
            Require(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped), "map buffer");
            std::memset(b.mapped, 0, size);
        }
        void Begin()
        {
            Require(vkResetCommandPool(device, commands, 0), "reset commands");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            Require(vkBeginCommandBuffer(command, &begin), "begin commands");
            uploadOffset = 0;
            Barrier();
        }
        void Barrier()
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
        void Submit()
        {
            Require(vkEndCommandBuffer(command), "end commands");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            Require(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit");
            Require(vkQueueWaitIdle(queue), "wait GPU");
        }
        void Upload(ImageSlot slot, const std::vector<glm::vec4>& data)
        {
            auto& image = images[slot];
            const VkDeviceSize bytes = Count * image.components * (image.half ? 2u : 4u);
            Expect(data.size() == Count && uploadOffset + bytes <= buffers[1].size, "Upload overflow");
            auto* destination = static_cast<uint8_t*>(buffers[1].mapped) + uploadOffset;
            for (uint32_t i = 0; i < Count; ++i) for (uint32_t c = 0; c < image.components; ++c)
            {
                if (image.half) reinterpret_cast<uint16_t*>(destination)[i * image.components + c] = glm::packHalf1x16(data[i][c]);
                else reinterpret_cast<float*>(destination)[i * image.components + c] = data[i][c];
            }
            VkBufferImageCopy copy{}; copy.bufferOffset = uploadOffset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {Width, Height, 1};
            vkCmdCopyBufferToImage(command, buffers[1].buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
            uploadOffset = (uploadOffset + bytes + 15u) & ~VkDeviceSize(15u);
        }
        std::vector<glm::vec4> Read(ImageSlot slot, uint32_t mip = 0)
        {
            const uint32_t width = std::max(1u, Width >> mip), height = std::max(1u, Height >> mip);
            const uint32_t count = width * height;
            Begin();
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1}; copy.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(command, images[slot].image, VK_IMAGE_LAYOUT_GENERAL, buffers[2].buffer, 1, &copy);
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
            Submit();
            std::vector<glm::vec4> result(count, glm::vec4(0));
            const auto& image = images[slot];
            for (uint32_t i = 0; i < count; ++i) for (uint32_t c = 0; c < image.components; ++c)
                result[i][c] = image.half ? glm::unpackHalf1x16(static_cast<uint16_t*>(buffers[2].mapped)[i * image.components + c]) :
                    static_cast<float*>(buffers[2].mapped)[i * image.components + c];
            return result;
        }
        VkDescriptorSet Allocate(VkDescriptorSetLayout layout)
        {
            VkDescriptorSet set{};
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool = pool; info.descriptorSetCount = 1; info.pSetLayouts = &layout;
            Require(vkAllocateDescriptorSets(device, &info, &set), "allocate set"); return set;
        }
        void BindImage(VkDescriptorSet set, uint32_t binding, ImageSlot slot, bool storage = false, VkImageView view = VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo info{sampler, view ? view : images[slot].view, VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1;
            write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &info; vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        void BindBuffer(VkDescriptorSet set, uint32_t binding, uint32_t slot)
        {
            VkDescriptorBufferInfo info{buffers[slot].buffer, 0, buffers[slot].size};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        void CreatePipeline(uint32_t index, const char* file, const std::vector<VkDescriptorType>& types, uint32_t pushBytes)
        {
            auto& p = pipelines[index];
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            for (uint32_t i = 0; i < types.size(); ++i) bindings.push_back({i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
            VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            layout.bindingCount = uint32_t(bindings.size()); layout.pBindings = bindings.data();
            Require(vkCreateDescriptorSetLayout(device, &layout, nullptr, &p.setLayout), "shader set layout");
            VkDescriptorSetLayout layouts[] = {cameraLayout, p.setLayout};
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
            VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pipelineLayout.setLayoutCount = 2; pipelineLayout.pSetLayouts = layouts;
            pipelineLayout.pushConstantRangeCount = pushBytes ? 1u : 0u; pipelineLayout.pPushConstantRanges = pushBytes ? &push : nullptr;
            Require(vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &p.layout), "pipeline layout");
            std::ifstream stream(ShaderPath(file), std::ios::binary | std::ios::ate);
            const size_t size = size_t(stream.tellg()); std::vector<uint32_t> code(size / 4); stream.seekg(0);
            stream.read(reinterpret_cast<char*>(code.data()), size);
            Expect(stream.good() && size % 4 == 0, "Invalid shader artifact");
            VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shader.codeSize = size; shader.pCode = code.data();
            Require(vkCreateShaderModule(device, &shader, nullptr, &p.shader), "shader module");
            VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipeline.layout = p.layout; pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module = p.shader; pipeline.stage.pName = "main";
            Require(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &p.pipeline), "compute pipeline");
        }
        void Initialize();
        VansGTAOParameters parameters{};
        void Plane(float depth = 2.0f)
        {
            position.resize(Count); normal.assign(Count, glm::vec4(0, 0, 1, 0));
            material.assign(Count, glm::vec4(0, 1, 1, 0)); raw.assign(Count, glm::vec4(1));
            for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x)
            {
                const glm::vec2 uv = (glm::vec2(x,y) + 0.5f) / glm::vec2(Width,Height);
                const glm::vec4 ray = camera.inverseProjectionMatrix * glm::vec4(uv * glm::vec2(2,-2) + glm::vec2(-1,1),1,1);
                position[y*Width+x] = glm::vec4(glm::vec3(ray) * (depth / -ray.z), depth);
            }
        }
        void Dispatch(uint32_t pipeline, VkDescriptorSet set, uint32_t mip = 0)
        {
            const auto& p = pipelines[pipeline]; const VkDescriptorSet sets[] = {cameraSet,set};
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,2,sets,0,nullptr);
            if (pipeline != 2) vkCmdPushConstants(command,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
            vkCmdDispatch(command,(std::max(1u,Width >> mip)+7)/8,(std::max(1u,Height >> mip)+7)/8,1);
            Barrier();
        }
        void Frame()
        {
            std::memcpy(buffers[0].mapped,&camera,sizeof(camera));
            Begin(); Upload(Position,position); Upload(Normal,normal); Upload(Material,material); Barrier();
            for (uint32_t mip = 0; mip < images[Depth].levels; ++mip)
            {
                parameters.sourceMip = int32_t(mip)-1; Dispatch(0,depthSets[mip],mip);
            }
            Dispatch(1,mainSet); Dispatch(2,denoiseSet); Submit();
        }
        std::vector<glm::vec4> Filter()
        {
            Begin(); Upload(Raw,raw); Barrier(); Dispatch(2,denoiseSet); Submit(); return Read(Result);
        }
    };
    void Fixture::Initialize()
    {
        if (!vulkan_library) vulkan_library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!vulkan_library || !LoadVulkanExportedFunction() || !LoadVulkanGlobalLevelFunctions()) throw std::runtime_error("Vulkan loader unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "Forest GTAO GPU Contract"; app.apiVersion = VK_API_VERSION_1_2;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* extensions[] = {"VK_EXT_debug_utils", "VK_EXT_validation_features"};
        VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT}; validation.enabledValidationFeatureCount = 1; validation.pEnabledValidationFeatures = &sync;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT; debug.pfnUserCallback = Validation;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app; instanceInfo.enabledLayerCount = 1; instanceInfo.ppEnabledLayerNames = &layer;
        instanceInfo.enabledExtensionCount = 2; instanceInfo.ppEnabledExtensionNames = extensions;
        validation.pNext = &debug; instanceInfo.pNext = &validation;
        Require(vkCreateInstance(&instanceInfo, nullptr, &instance), "create instance");
        if (!LoadVulkanInstanceLevelFunctions(instance)) throw std::runtime_error("Vulkan instance functions unavailable");
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        Require(createMessenger(instance, &debug, nullptr, &messenger), "validation messenger");
        uint32_t count = 0; Require(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count); Require(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "read GPUs");
        Expect(!devices.empty(), "No Vulkan GPU"); physical = devices.front();
        VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
        std::cout << "GTAO GPU: " << properties.deviceName << '\n';
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
        Expect(family < count, "No compute queue");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{}; features.shaderStorageImageExtendedFormats = VK_TRUE; features.imageCubeArray = VK_TRUE;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pEnabledFeatures = &features; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        Require(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
        if (!LoadVulkanDeviceLevelFunctions(device)) throw std::runtime_error("Vulkan device functions unavailable");
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; commandPool.queueFamilyIndex = family;
        Require(vkCreateCommandPool(device, &commandPool, nullptr, &commands), "command pool");
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; commandInfo.commandPool = commands; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
        Require(vkAllocateCommandBuffers(device, &commandInfo, &command), "command buffer");
        const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; poolInfo.maxSets = 12; poolInfo.poolSizeCount = 3; poolInfo.pPoolSizes = poolSizes;
        Require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "descriptor pool");
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; samplerInfo.minFilter = samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.maxLod = 5.0f;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Require(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "sampler");
        CreateBuffer(0, sizeof(camera)); CreateBuffer(1, 1024 * 1024); CreateBuffer(2, Count * sizeof(glm::vec4));
        Begin();
        for (uint32_t slot = 0; slot < ImageCount; ++slot)
        {
            auto& image = images[slot];
            image.components = slot == Depth || slot == Edges ? 1u : (slot == Raw || slot == Result ? 2u : 4u);
            image.half = slot == Raw || slot == Result;
            image.levels = slot == Depth ? 5u : 1u;
            image.format = image.half ? VK_FORMAT_R16G16_SFLOAT : (image.components == 1 ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT);
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType = VK_IMAGE_TYPE_2D; info.format = image.format;
            info.extent = {Width, Height, 1}; info.mipLevels = image.levels; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            Require(vkCreateImage(device, &info, nullptr, &image.image), "create image");
            VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device, image.image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Require(vkAllocateMemory(device, &allocation, nullptr, &image.memory), "allocate image");
            Require(vkBindImageMemory(device, image.image, image.memory, 0), "bind image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format; view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.levels, 0, 1};
            Require(vkCreateImageView(device, &view, nullptr, &image.view), "image view");
            if (slot == Depth) for (uint32_t mip = 0; mip < image.levels; ++mip)
            {
                view.subresourceRange.baseMipLevel = mip; view.subresourceRange.levelCount = 1;
                Require(vkCreateImageView(device,&view,nullptr,&image.mipViews[mip]),"depth mip view");
            }
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,image.levels,0,1};
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.image = image.image; barrier.subresourceRange = view.subresourceRange; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkClearColorValue zero{}; vkCmdClearColorImage(command, image.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &view.subresourceRange);
        }
        Barrier(); Submit();
        VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo cameraInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; cameraInfo.bindingCount = 1; cameraInfo.pBindings = &binding;
        Require(vkCreateDescriptorSetLayout(device,&cameraInfo,nullptr,&cameraLayout),"camera layout");
        cameraSet = Allocate(cameraLayout); BindBuffer(cameraSet,0,0);
        constexpr auto texture = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        CreatePipeline(0,"GTAO/Depth/GTAODepthcomp.spv",{texture,texture,storage},sizeof(parameters));
        CreatePipeline(1,"GTAO/Main/GTAOMaincomp.spv",{texture,texture,texture,texture,storage,storage},sizeof(parameters));
        CreatePipeline(2,"GTAO/Denoise/GTAODenoisecomp.spv",{texture,texture,texture,texture,texture,storage},0);
        for (uint32_t mip = 0; mip < images[Depth].levels; ++mip)
        {
            auto set = depthSets[mip] = Allocate(pipelines[0].setLayout);
            BindImage(set,GTAO_DEPTH_POSITION,Position); BindImage(set,GTAO_DEPTH_SOURCE,Depth,false,images[Depth].mipViews[mip > 0 ? mip-1 : 0]);
            BindImage(set,GTAO_DEPTH_RESULT,Depth,true,images[Depth].mipViews[mip]);
        }
        mainSet = Allocate(pipelines[1].setLayout);
        BindImage(mainSet,GTAO_MAIN_NORMAL,Normal); BindImage(mainSet,GTAO_MAIN_MATERIAL,Material);
        BindImage(mainSet,GTAO_MAIN_POSITION,Position); BindImage(mainSet,GTAO_MAIN_DEPTH,Depth);
        BindImage(mainSet,GTAO_MAIN_RAW,Raw,true); BindImage(mainSet,GTAO_MAIN_EDGES,Edges,true);
        denoiseSet = Allocate(pipelines[2].setLayout);
        BindImage(denoiseSet,GTAO_DENOISE_RAW,Raw); BindImage(denoiseSet,GTAO_DENOISE_EDGES,Edges);
        BindImage(denoiseSet,GTAO_DENOISE_NORMAL,Normal); BindImage(denoiseSet,GTAO_DENOISE_MATERIAL,Material);
        BindImage(denoiseSet,GTAO_DENOISE_POSITION,Position); BindImage(denoiseSet,GTAO_DENOISE_RESULT,Result,true);
        camera.viewMatrix = glm::mat4(1); camera.inverseViewMatrix = glm::mat4(1);
        camera.projectionMatrix = glm::perspectiveRH_ZO(glm::radians(30.0f),float(Width)/Height,0.1f,100.0f);
        camera.inverseProjectionMatrix = glm::inverse(camera.projectionMatrix);
        camera.cameraParams = glm::vec4(0.1f,100.0f,30.0f,1.0f);
        camera.screenParams = glm::vec4(Width,Height,1.0f/Width,1.0f/Height);
        parameters.mipCount = int32_t(images[Depth].levels);
    }
}

bool TestGTAOGpuContract()
{
    try
    {
        validationErrors = 0;
        {
            Fixture gpu; gpu.Initialize(); gpu.Plane(); gpu.Frame();
            const auto flat = gpu.Read(Result);
            for (const auto& v : flat) Expect(std::isfinite(v.x) && v.x > 0.96f && v.x <= 1.0f && v.x == v.y,"Unoccluded plane darkened or channels differ");
            for (uint32_t mip = 0; mip < 5; ++mip)
                for (const auto& v : gpu.Read(Depth,mip)) Expect(std::abs(v.x-2.0f)<1e-5f,"Depth prefilter lost constant depth or odd tail");
            std::cout << "plane visibility and all five odd depth mips: PASS\n";
            // A closer wall must occlude its neighbour, while removing it restores white immediately.
            for (uint32_t y=0;y<Height;++y) for (uint32_t x=Width/2+1;x<Width;++x)
                gpu.position[y*Width+x] *= 0.9f;
            gpu.Frame(); const auto contact = gpu.Read(Raw);
            const uint32_t center = (Height/2)*Width+Width/2;
            std::cout << "contact visibility=" << contact[center].x << " flat=" << flat[center].x << '\n';
            Expect(contact[center].x < flat[center].x - 0.05f,"Nearby wall produced no GTAO contact shadow");
            const auto stable = gpu.Read(Result); gpu.Frame(); const auto repeated = gpu.Read(Result);
            for (uint32_t i=0;i<Count;++i) Expect(stable[i]==repeated[i],"Static GTAO changed between identical frames");
            gpu.Plane(); gpu.Frame(); Expect(gpu.Read(Result)[center].x>0.96f,"Removed occluder retained stale AO");
            // Single valid texel in the last odd row/column must survive reduction.
            gpu.position.assign(Count,glm::vec4(0)); gpu.position.back()=glm::vec4(0,0,-2,2); gpu.Frame();
            Expect(std::abs(gpu.Read(Depth,4)[0].x-2.0f)<1e-5f,"Odd border texel was dropped by depth reduction");
            for (const auto& v : gpu.Read(Result)) Expect(std::isfinite(v.x)&&v.x>=0&&v.x<=1,"Invalid background output");
            gpu.position.assign(Count,glm::vec4(0)); gpu.Frame();
            for (const auto& v : gpu.Read(Result)) Expect(v.x==1 && v.y==1,"Background is not unoccluded");
            std::cout << "contact, stable frames, removal, odd-tail and background: PASS\n";
            // The filter may smooth noise within a surface, but may not cross a depth cliff.
            gpu.Plane(); for (uint32_t y=0;y<Height;++y) for (uint32_t x=Width/2+1;x<Width;++x) gpu.position[y*Width+x]*=0.5f;
            gpu.Frame(); for (uint32_t i=0;i<Count;++i) gpu.raw[i]=glm::vec4(i%Width<=Width/2?0.2f:0.9f);
            const auto filtered = gpu.Filter();
            Expect(std::abs(filtered[center].x-0.2f)<0.01f && std::abs(filtered[center+1].x-0.9f)<0.01f,"Denoise crossed a depth edge");
            // Grass front/back meanings stay independent and do not mix with opaque material.
            gpu.Plane(); gpu.material.assign(Count,glm::vec4(0,1,8,0)); gpu.Frame();
            gpu.raw.assign(Count,glm::vec4(0.2f,0.8f,0,0));
            const auto dual = gpu.Filter(); Expect(std::abs(dual[center].x-0.2f)<0.001f && std::abs(dual[center].y-0.8f)<0.001f,"Dual AO channels were merged");
            std::cout << "depth edges and independent front/back channels: PASS\n";
        }
        Expect(validationErrors==0,"GTAO Vulkan validation errors");
        std::cout << "GTAO production GPU contract: PASS\n"; return true;
    }
    catch (const std::exception& error) { std::cerr << "GTAO GPU contract failed: " << error.what() << '\n'; return false; }
}

