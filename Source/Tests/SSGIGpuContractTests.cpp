#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VansCameraFrameData.h"
#include "../EngineCore/RenderCore/VansTemporalProjection.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/gtc/packing.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace VansGraphics;
    constexpr uint32_t Width = 17, Height = 13, Count = Width * Height;
    enum ImageSlot { Position, Motion, Normal, Material, Raw, HistoryA, HistoryB, MomentsA, MomentsB, SurfaceA, SurfaceB, Atrous, ImageCount };
    std::atomic<uint32_t> validationErrors{0};
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        ++validationErrors;
        std::cerr << "[SSGI Vulkan] " << message->pMessage << '\n';
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
            if (root == root.root_path()) throw std::runtime_error("SSGI shader not found");
            root = root.parent_path();
        }
    }
    struct Image { VkImage image{}; VkImageView view{}; VkDeviceMemory memory{}; VkFormat format{}; uint32_t components = 4; bool half = false; };
    struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped{}; VkDeviceSize size{}; };
    struct Pipeline { VkDescriptorSetLayout setLayout{}; VkPipelineLayout layout{}; VkShaderModule shader{}; VkPipeline pipeline{}; };
    // 直接调度正式 Temporal / A-trous SPIR-V，17x13 同时覆盖越界 workgroup。
    struct Fixture
    {
        VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
        uint32_t family = 0; VkDebugUtilsMessengerEXT messenger{};
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
        VkCommandPool commands{}; VkCommandBuffer command{}; VkDescriptorPool pool{}; VkSampler sampler{};
        VkDescriptorSetLayout cameraLayout{}; VkDescriptorSet cameraSet{};
        std::array<VkDescriptorSet, 2> temporalSets{}, atrousSets{};
        std::array<Image, ImageCount> images{};
        std::array<Buffer, 4> buffers{}; // camera, temporal params, upload, readback
        std::array<Pipeline, 2> pipelines{};
        VkDeviceSize uploadOffset = 0;
        VansCameraDataGPU camera{};
        std::vector<glm::vec4> position, motion, normal, material, raw;
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
            Expect(data.size() == Count && uploadOffset + bytes <= buffers[2].size, "Upload overflow");
            auto* destination = static_cast<uint8_t*>(buffers[2].mapped) + uploadOffset;
            for (uint32_t i = 0; i < Count; ++i) for (uint32_t c = 0; c < image.components; ++c)
            {
                if (image.half) reinterpret_cast<uint16_t*>(destination)[i * image.components + c] = glm::packHalf1x16(data[i][c]);
                else reinterpret_cast<float*>(destination)[i * image.components + c] = data[i][c];
            }
            VkBufferImageCopy copy{}; copy.bufferOffset = uploadOffset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {Width, Height, 1};
            vkCmdCopyBufferToImage(command, buffers[2].buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
            uploadOffset = (uploadOffset + bytes + 15u) & ~VkDeviceSize(15u);
        }
        std::vector<glm::vec4> Read(ImageSlot slot)
        {
            Begin();
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {Width, Height, 1};
            vkCmdCopyImageToBuffer(command, images[slot].image, VK_IMAGE_LAYOUT_GENERAL, buffers[3].buffer, 1, &copy);
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
            Submit();
            std::vector<glm::vec4> result(Count, glm::vec4(0));
            const auto& image = images[slot];
            for (uint32_t i = 0; i < Count; ++i) for (uint32_t c = 0; c < image.components; ++c)
                result[i][c] = image.half ? glm::unpackHalf1x16(static_cast<uint16_t*>(buffers[3].mapped)[i * image.components + c]) :
                    static_cast<float*>(buffers[3].mapped)[i * image.components + c];
            return result;
        }
        VkDescriptorSet Allocate(VkDescriptorSetLayout layout)
        {
            VkDescriptorSet set{};
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool = pool; info.descriptorSetCount = 1; info.pSetLayouts = &layout;
            Require(vkAllocateDescriptorSets(device, &info, &set), "allocate set"); return set;
        }
        void BindImage(VkDescriptorSet set, uint32_t binding, ImageSlot slot, bool storage = false)
        {
            VkDescriptorImageInfo info{sampler, images[slot].view, VK_IMAGE_LAYOUT_GENERAL};
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
        void Plane(float angle, float depth = 10.0f)
        {
            position.resize(Count); normal.assign(Count, glm::vec4(0, 0, 1, 0));
            material.assign(Count, glm::vec4(0, 0, 1, 0)); motion.assign(Count, glm::vec4(0)); raw.assign(Count, glm::vec4(1));
            const glm::vec3 n(std::cos(glm::radians(angle)), 0, std::sin(glm::radians(angle)));
            for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x)
            {
                glm::vec3 ray((float(x) - float(Width / 2)) * 0.001069167f, (float(y) - float(Height / 2)) * 0.001069167f, -1);
                const float z = -depth * n.z / glm::dot(n, ray);
                position[y * Width + x] = glm::vec4(ray * z, z);
            }
        }
        void Frame(uint32_t frame, glm::vec2 jitterDelta = glm::vec2(0))
        {
            SSGITemporalParamsGPU params{}; params.screenSize = glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height);
            params.frameParams = glm::vec4(float(frame), jitterDelta, 0);
            std::memcpy(buffers[0].mapped, &camera, sizeof(camera)); std::memcpy(buffers[1].mapped, &params, sizeof(params));
            Begin(); Upload(Position, position); Upload(Normal, normal); Upload(Material, material); Upload(Motion, motion); Upload(Raw, raw); Barrier();
            const VkDescriptorSet sets[] = {cameraSet, temporalSets[frame % 2]};
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[0].pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[0].layout, 0, 2, sets, 0, nullptr);
            vkCmdDispatch(command, (Width + 7) / 8, (Height + 7) / 8, 1); Submit();
        }
        std::vector<glm::vec4> Filter(uint32_t frame, uint32_t step)
        {
            Begin();
            const VkDescriptorSet sets[] = {cameraSet, atrousSets[frame % 2]};
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[1].pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[1].layout, 0, 2, sets, 0, nullptr);
            SSGIAtrousPushConstants params{}; params.stepWidth = step; params.normalPower = 24.0f;
            vkCmdPushConstants(command, pipelines[1].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
            vkCmdDispatch(command, (Width + 7) / 8, (Height + 7) / 8, 1); Submit(); return Read(Atrous);
        }
    };
    void Fixture::Initialize()
    {
        if (!vulkan_library) vulkan_library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!vulkan_library || !LoadVulkanExportedFunction() || !LoadVulkanGlobalLevelFunctions()) throw std::runtime_error("Vulkan loader unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "Forest SSGI GPU Contract"; app.apiVersion = VK_API_VERSION_1_2;
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
        std::cout << "SSGI GPU: " << properties.deviceName << '\n';
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
        Expect(family < count, "No compute queue");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{}; features.shaderStorageImageExtendedFormats = VK_TRUE;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pEnabledFeatures = &features; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        Require(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
        if (!LoadVulkanDeviceLevelFunctions(device)) throw std::runtime_error("Vulkan device functions unavailable");
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; commandPool.queueFamilyIndex = family;
        Require(vkCreateCommandPool(device, &commandPool, nullptr, &commands), "command pool");
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; commandInfo.commandPool = commands; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
        Require(vkAllocateCommandBuffers(device, &commandInfo, &command), "command buffer");
        const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; poolInfo.maxSets = 8; poolInfo.poolSizeCount = 3; poolInfo.pPoolSizes = poolSizes;
        Require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "descriptor pool");
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; samplerInfo.minFilter = samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Require(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "sampler");
        CreateBuffer(0, sizeof(camera)); CreateBuffer(1, sizeof(SSGITemporalParamsGPU)); CreateBuffer(2, 1024 * 1024); CreateBuffer(3, Count * sizeof(glm::vec4));
        Begin();
        for (uint32_t slot = 0; slot < ImageCount; ++slot)
        {
            auto& image = images[slot];
            image.components = slot == MomentsA || slot == MomentsB ? 2u : 4u;
            image.half = slot == Raw || slot == HistoryA || slot == HistoryB || slot == MomentsA || slot == MomentsB || slot == Atrous;
            image.format = image.half ? (image.components == 2 ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT) : VK_FORMAT_R32G32B32A32_SFLOAT;
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType = VK_IMAGE_TYPE_2D; info.format = image.format;
            info.extent = {Width, Height, 1}; info.mipLevels = 1; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            Require(vkCreateImage(device, &info, nullptr, &image.image), "create image");
            VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device, image.image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Require(vkAllocateMemory(device, &allocation, nullptr, &image.memory), "allocate image");
            Require(vkBindImageMemory(device, image.image, image.memory, 0), "bind image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format; view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            Require(vkCreateImageView(device, &view, nullptr, &image.view), "image view");
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.image = image.image; barrier.subresourceRange = view.subresourceRange; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkClearColorValue zero{}; vkCmdClearColorImage(command, image.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &view.subresourceRange);
        }
        Submit();
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo cameraInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; cameraInfo.bindingCount = 1; cameraInfo.pBindings = &binding;
        Require(vkCreateDescriptorSetLayout(device, &cameraInfo, nullptr, &cameraLayout), "camera set layout");
        cameraSet = Allocate(cameraLayout); BindBuffer(cameraSet, 0, 0);
        constexpr auto texture = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        constexpr auto storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        CreatePipeline(0, "SSGITemporal/SSGITemporalcomp.spv", {texture, texture, texture, storage, storage, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, texture, storage, texture, texture, texture, storage}, 0);
        CreatePipeline(1, "SSGIAtrous/SSGIAtrouscomp.spv", {texture, texture, texture, texture, storage}, sizeof(SSGIAtrousPushConstants));
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto set = temporalSets[i] = Allocate(pipelines[0].setLayout);
            BindImage(set, SSGI_TEMPORAL_BINDING_POSITION, Position);
            BindImage(set, SSGI_TEMPORAL_BINDING_MOTION_VECTOR, Motion);
            BindImage(set, SSGI_TEMPORAL_BINDING_HISTORY_GI, i ? HistoryA : HistoryB);
            BindImage(set, SSGI_TEMPORAL_BINDING_CURRENT_GI, Raw, true);
            BindImage(set, SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, i ? HistoryB : HistoryA, true);
            BindBuffer(set, SSGI_TEMPORAL_BINDING_INFO_UBO, 1);
            BindImage(set, SSGI_TEMPORAL_BINDING_HISTORY_MOMENTS, i ? MomentsA : MomentsB);
            BindImage(set, SSGI_TEMPORAL_BINDING_OUTPUT_MOMENTS, i ? MomentsB : MomentsA, true);
            BindImage(set, SSGI_TEMPORAL_BINDING_HISTORY_SURFACE, i ? SurfaceA : SurfaceB);
            BindImage(set, SSGI_TEMPORAL_BINDING_CURRENT_NORMAL, Normal);
            BindImage(set, SSGI_TEMPORAL_BINDING_CURRENT_MATERIAL, Material);
            BindImage(set, SSGI_TEMPORAL_BINDING_OUTPUT_SURFACE, i ? SurfaceB : SurfaceA, true);
            set = atrousSets[i] = Allocate(pipelines[1].setLayout);
            BindImage(set, SSGI_ATROUS_BINDING_INPUT_GI, i ? HistoryB : HistoryA);
            BindImage(set, SSGI_ATROUS_BINDING_NORMAL, Normal);
            BindImage(set, SSGI_ATROUS_BINDING_POSITION, Position);
            BindImage(set, SSGI_ATROUS_BINDING_MATERIAL, Material);
            BindImage(set, SSGI_ATROUS_BINDING_OUTPUT_GI, Atrous, true);
        }
        camera.cameraParams = glm::vec4(0.1f, 1000.0f, 60.0f, 1.0f);
        camera.screenParams = glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height);
        camera.lastViewProjectionMatrix = glm::mat4(1.0f);
        camera.lastViewMatrix = glm::mat4(1.0f);
    }
}

bool TestSSGIGpuContract()
{
    try
    {
        validationErrors = 0;
        {
            Fixture gpu; gpu.Initialize();
            constexpr uint32_t center = (Height / 2) * Width + Width / 2;
            // 一个缓存块偶尔命中高亮时，历史裁剪不能将整块历史拉向当前离群样本。
            // 完整周期的输入均值已知；直接调度正式 Shader 验证长期能量，而非仅检查变平滑。
            gpu.Plane(90.0f);
            double sparseEnergy = 0.0;
            for (uint32_t frame = 0; frame < 512; ++frame)
            {
                const float value = frame % 32u == 31u ? 4.0f : 0.2f;
                gpu.raw.assign(Count, glm::vec4(value, value, value, 1.0f));
                gpu.Frame(frame);
                if (frame >= 256u)
                    sparseEnergy += gpu.Read(frame % 2 ? HistoryB : HistoryA)[center].x;
            }
            const double sparseMean = sparseEnergy / 256.0;
            std::cout << "sparse sample mean=" << sparseMean << " expected=0.31875\n";
            Expect(std::abs(sparseMean - 0.31875) < 0.015,
                "Temporal clipping amplifies sparse high-energy cache samples");
            // 真实持续高亮应保留 HDR 能量与颜色；显式灯光重置不能留下旧亮度。
            gpu.Plane(90.0f);
            const glm::vec3 bright(16.0f, 4.0f, 1.0f);
            for (uint32_t frame = 0; frame < 160; ++frame)
            {
                gpu.raw.assign(Count, glm::vec4(frame < 64 ? glm::vec3(0.2f) : bright, 1.0f));
                gpu.Frame(frame);
            }
            const glm::vec3 brightResult(gpu.Read(HistoryB)[center]);
            Expect(glm::all(glm::lessThan(glm::abs(brightResult - bright) / bright, glm::vec3(0.035f))),
                "Sustained HDR illumination lost energy or color");
            gpu.raw.assign(Count, glm::vec4(0.05f, 0.1f, 0.2f, 1.0f));
            gpu.Frame(0);
            Expect(glm::length(glm::vec3(gpu.Read(HistoryA)[center]) - glm::vec3(0.05f, 0.1f, 0.2f)) < 0.001f,
                "Lighting reset retained stale bright history");
            std::cout << "sustained HDR color and immediate lighting reset: PASS\n";
            // Alpha/confidence 独立性，以及完整 40 帧真实图像 ping-pong。
            for (float confidence : {0.0f, 0.25f, 1.0f})
            {
                gpu.Plane(3.0f);
                float maxTailError = 0;
                for (uint32_t frame = 0; frame < 40; ++frame)
                {
                    const float value = frame % 2 ? 1.4f : 0.6f;
                    gpu.raw.assign(Count, glm::vec4(value, value, value, confidence)); gpu.Frame(frame);
                    const auto result = gpu.Read(frame % 2 ? HistoryB : HistoryA);
                    Expect(std::abs(result[center].a * 255.0f - float(frame + 1)) < 0.15f, "Confidence altered temporal history length");
                    if (frame >= 20) maxTailError = std::max(maxTailError, std::abs(result[center].x - 1.0f));
                }
                Expect(maxTailError < 0.1f, "Spatially correlated GI noise did not accumulate");
                std::cout << "confidence=" << confidence << " history=40 maxTailError=" << maxTailError << '\n';
            }
            // 同一采样图案在 90/30/5/3/1 度平面上的降噪响应应一致。
            std::vector<glm::vec4> reference, referenceFiltered;
            for (float angle : {90.0f, 30.0f, 5.0f, 3.0f, 1.0f})
            {
                gpu.Plane(angle);
                for (uint32_t i = 0; i < Count; ++i) gpu.raw[i] = glm::vec4(((i % Width) / 4) % 2 ? 1.4f : 0.6f);
                gpu.Frame(0); auto result = gpu.Read(HistoryA); auto filtered = gpu.Filter(0, 2);
                if (reference.empty()) { reference = result; referenceFiltered = filtered; }
                for (uint32_t i = 0; i < Count; ++i)
                {
                    Expect(std::abs(result[i].x - reference[i].x) < 0.004f, "Temporal filter rejects a grazing coplanar neighbor");
                    Expect(std::abs(filtered[i].x - referenceFiltered[i].x) < 0.004f, "A-trous rejects a grazing coplanar neighbor");
                }
                std::cout << "coplanar angle=" << angle << " temporal=" << result[center].x << " atrous=" << filtered[center].x << '\n';
            }
            // 近距离导数很小，材质法线与几何法线不同，不能退回材质法线判共面。
            gpu.Plane(1.0f, 0.3f);
            for (uint32_t i = 0; i < Count; ++i) gpu.raw[i] = glm::vec4(((i % Width) / 4) % 2 ? 1.4f : 0.6f);
            gpu.Frame(0);
            auto close = gpu.Read(HistoryA);
            Expect(std::abs(close[center].x - reference[center].x) < 0.004f, "Submillimeter derivatives lost the geometric normal");
            // 真实深度断层和不同材质不能因斜率适应而串色，step=1/2 都验证。
            for (bool materialEdge : {false, true})
            {
                gpu.Plane(3.0f);
                for (uint32_t i = 0; i < Count; ++i)
                {
                    const bool right = i % Width >= Width / 2;
                    gpu.raw[i] = glm::vec4(right ? 8.0f : 0.0f);
                    if (right && materialEdge) gpu.material[i].z = 2;
                    if (right && !materialEdge) { gpu.position[i].x += 1.0f; }
                }
                gpu.Frame(0);
                for (uint32_t step : {1u, 2u})
                {
                    auto edge = gpu.Filter(0, step);
                    for (uint32_t i = 0; i < Count; ++i)
                        Expect(std::abs(edge[i].x - gpu.raw[i].x) < 0.01f, "Spatial filter leaks through depth/material boundary");
                }
            }
            // 用正式 Surface 输出生成历史，随后验证有方向的 jitter UV 偏移。
            gpu.Plane(90.0f); gpu.Frame(0);
            std::vector<glm::vec4> history(Count), moments(Count, glm::vec4(1, 4, 0, 0));
            for (uint32_t i = 0; i < Count; ++i) history[i] = glm::vec4(float(i % Width + 1) * 0.1f, 1, 1, 20.0f / 255.0f);
            gpu.Begin(); gpu.Upload(HistoryA, history); gpu.Upload(MomentsA, moments); gpu.Submit();
            gpu.Frame(1, glm::vec2(1.0f / Width, -1.0f / Height));
            auto jitter = gpu.Read(HistoryB);
            const float expectedJitter = glm::mix(float(Width / 2 + 2) * 0.1f, 1.0f, 1.0f / 21.0f);
            Expect(std::abs(jitter[center].x - expectedJitter) < 0.003f, "Jitter history UV has incorrect sign or scale");
            Expect(std::abs(jitter[center].a * 255.0f - 21.0f) < 0.1f, "Valid jittered history was rejected");
            // 相机前进只改变 view-Z；世界表面历史仍应有效。
            for (auto& p : gpu.position) p.w += 2;
            gpu.Frame(2);
            Expect(std::abs(gpu.Read(HistoryA)[center].a * 255.0f - 22.0f) < 0.1f, "Camera depth change incorrectly resets world surface history");
            for (auto& p : gpu.position) p.z += 1;
            gpu.Frame(3);
            Expect(std::abs(gpu.Read(HistoryB)[center].a * 255.0f - 1.0f) < 0.01f, "Disoccluded surface accepted history");
            gpu.material.assign(Count, glm::vec4(0, 0, 2, 0)); gpu.Frame(4);
            Expect(std::abs(gpu.Read(HistoryA)[center].a * 255.0f - 1.0f) < 0.01f, "Changed material accepted history");
            gpu.normal.assign(Count, glm::vec4(0, 1, 0, 0)); gpu.Frame(5);
            Expect(std::abs(gpu.Read(HistoryB)[center].a * 255.0f - 1.0f) < 0.01f, "Changed normal accepted history");
            // -Y 的 oct 折叠必须使用 sign-not-zero，连续两帧应保留历史。
            gpu.normal.assign(Count, glm::vec4(0, -1, 0, 0)); gpu.Frame(0); gpu.Frame(1);
            Expect(std::abs(gpu.Read(HistoryB)[center].a * 255.0f - 2.0f) < 0.01f, "Negative-axis oct normal failed to round-trip");
            gpu.Frame(0);
            Expect(std::abs(gpu.Read(HistoryA)[center].a * 255.0f - 1.0f) < 0.01f, "Explicit history reset was ignored");
            gpu.position.assign(Count, glm::vec4(0)); gpu.normal.assign(Count, glm::vec4(0)); gpu.Frame(1);
            const auto sky = gpu.Read(HistoryB); const auto surface = gpu.Read(SurfaceB);
            for (uint32_t i = 0; i < Count; ++i) Expect(sky[i] == glm::vec4(0) && surface[i] == glm::vec4(0), "Sky history was not cleared");
            std::cout << "edges, near geometry, jitter, camera motion, disocclusion, material, normal, reset, sky: PASS\n";
        }
        // CPU 上传路径必须在透视/正交及相机变换下提取同一 jitter。
        for (bool orthographic : {false, true})
        {
            const glm::mat4 projection = orthographic ? glm::ortho(-10.0f, 10.0f, -6.0f, 6.0f, 0.1f, 1000.0f) : glm::perspective(glm::radians(60.0f), 1.6f, 0.1f, 1000.0f);
            const glm::mat4 view = glm::lookAt(glm::vec3(3, 4, 5), glm::vec3(1, 0, -1), glm::vec3(0, 1, 0));
            const glm::vec2 offset(0.001f, -0.002f);
            const glm::vec2 extracted = ExtractTemporalJitterUV(ApplyClipSpaceJitter(projection, offset) * view, projection * view);
            Expect(glm::length(extracted - offset * glm::vec2(0.5f, -0.5f)) < 1e-6f, "CPU jitter extraction failed");
        }
        Expect(validationErrors == 0, "SSGI Vulkan validation reported errors");
        std::cout << "SSGI_GPU_CONTRACT_PASS validationErrors=0\n";
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SSGI_GPU_CONTRACT_FAIL: " << error.what() << '\n'; return false;
    }
}
