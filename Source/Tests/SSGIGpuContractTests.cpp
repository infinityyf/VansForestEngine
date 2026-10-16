#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VansCameraFrameData.h"
#include "../EngineCore/RenderCore/VansTemporalProjection.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VansRenderSceneSnapshot.h"
#include "../EngineCore/RenderCore/VegetationCore/GrassEnergyLUT.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationSystem.h"
#include "../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/gtc/packing.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
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
    enum ImageSlot { Position, Motion, Normal, Material, Raw, HistoryA, HistoryB, MomentsA, MomentsB, SurfaceA, SurfaceB, Atrous, GrassEnergy,
        GatherDepth, GatherHiZ, GatherCache, GatherSurface, GatherSky, GatherIrradiance, GatherVisibility, ImageCount };
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
    struct Image { VkImage image{}; VkImageView view{}; VkDeviceMemory memory{}; VkFormat format{}; uint32_t components = 4, width = Width, height = Height, mipLevels = 1; bool half = false; };
    struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped{}; VkDeviceSize size{}; };
    struct Pipeline { VkDescriptorSetLayout setLayout{}; VkPipelineLayout layout{}; VkShaderModule shader{}; VkPipeline pipeline{}; };
    // 直接调度正式 Gather / Temporal / A-trous SPIR-V，17x13 同时覆盖越界 workgroup。
    struct Fixture
    {
        VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
        uint32_t family = 0; VkDebugUtilsMessengerEXT messenger{};
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
        VkCommandPool commands{}; VkCommandBuffer command{}; VkDescriptorPool pool{}; VkSampler sampler{};
        VkSampler grassEnergySampler{};
        VkDescriptorSetLayout cameraLayout{}; VkDescriptorSet cameraSet{};
        std::array<VkDescriptorSet, 2> temporalSets{}, atrousSets{};
        std::array<VkDescriptorSet, 2> gatherSets{};
        std::array<Image, ImageCount> images{};
        std::array<Buffer, 22> buffers{}; // 0–3 SSGI history; 4–17 grass; 18–21 gather inputs.
        std::array<Pipeline, 6> pipelines{};
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
                if (grassEnergySampler) vkDestroySampler(device, grassEnergySampler, nullptr);
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
        void CreateBuffer(uint32_t slot, VkDeviceSize size,
            VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            auto& b = buffers[slot]; b.size = size;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = size; info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            Require(vkCreateBuffer(device, &info, nullptr, &b.buffer), "create buffer");
            VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, b.buffer, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, memoryFlags);
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
        void Upload(ImageSlot slot, const std::vector<glm::vec4>& data, uint32_t mip = 0)
        {
            auto& image = images[slot];
            Expect(mip < image.mipLevels, "Upload mip outside image");
            const uint32_t width = std::max(image.width >> mip, 1u), height = std::max(image.height >> mip, 1u);
            const uint32_t count = width * height;
            const VkDeviceSize bytes = count * image.components * (image.half ? 2u : 4u);
            Expect(data.size() == count && uploadOffset + bytes <= buffers[2].size, "Upload overflow");
            auto* destination = static_cast<uint8_t*>(buffers[2].mapped) + uploadOffset;
            for (uint32_t i = 0; i < count; ++i) for (uint32_t c = 0; c < image.components; ++c)
            {
                if (image.half) reinterpret_cast<uint16_t*>(destination)[i * image.components + c] = glm::packHalf1x16(data[i][c]);
                else reinterpret_cast<float*>(destination)[i * image.components + c] = data[i][c];
            }
            VkBufferImageCopy copy{}; copy.bufferOffset = uploadOffset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1}; copy.imageExtent = {width, height, 1};
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
        void BindImage(VkDescriptorSet set, uint32_t binding, ImageSlot slot, bool storage = false, uint32_t count = 1)
        {
            std::vector<VkDescriptorImageInfo> info(count,
                {slot==GrassEnergy?grassEnergySampler:sampler, images[slot].view, VK_IMAGE_LAYOUT_GENERAL});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = binding; write.descriptorCount = count;
            write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = info.data(); vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        void BindBuffer(VkDescriptorSet set, uint32_t binding, uint32_t slot,
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VkDeviceSize range = VK_WHOLE_SIZE, uint32_t count = 1)
        {
            std::vector<VkDescriptorBufferInfo> info(count, {buffers[slot].buffer, 0,
                range == VK_WHOLE_SIZE ? buffers[slot].size : range});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = binding; write.descriptorCount = count;
            write.descriptorType = type; write.pBufferInfo = info.data();
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        void CreatePipeline(uint32_t index, const char* file, const std::vector<VkDescriptorType>& types, uint32_t pushBytes)
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            for (uint32_t i = 0; i < types.size(); ++i) bindings.push_back({i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
            CreatePipelineBindings(index, file, bindings, pushBytes);
        }
        void CreatePipelineBindings(uint32_t index, const char* file,
            const std::vector<VkDescriptorSetLayoutBinding>& bindings, uint32_t pushBytes = 0)
        {
            auto& p = pipelines[index];
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
        void VerifyGather();
        void VerifyGatherCoverage();
        void UploadGatherHiZ()
        {
            // 与硬件深度分开：正式 Hi-Z 查询读取正线性深度及保守 min mip。
            for (uint32_t mip = 0; mip < images[GatherHiZ].mipLevels; ++mip)
            {
                const uint32_t width = std::max(Width >> mip, 1u), height = std::max(Height >> mip, 1u);
                std::vector<glm::vec4> values(width * height, glm::vec4(1e5f));
                for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x)
                    for (uint32_t sy = y * Height / height; sy < ((y + 1) * Height + height - 1) / height; ++sy)
                    for (uint32_t sx = x * Width / width; sx < ((x + 1) * Width + width - 1) / width; ++sx)
                        if (position[sy * Width + sx].w > 0)
                            values[y * width + x].x = std::min(values[y * width + x].x, position[sy * Width + sx].w);
                Upload(GatherHiZ, values, mip);
            }
        }
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
            VansSSGIHistoryParameters params{}; params.screenSize = glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height);
            params.frameParams = glm::vec4(frame ? 1.0f : 0.0f, jitterDelta, 0);
            Frame(frame % 2u, params);
        }
        void Frame(VansSSGIHistory& history)
        {
            const auto write=history.WriteIndex();
            Frame(write,history.PrepareFrame(Width,Height,{}));
            history.ResolveRecordedFrame(true);
        }
        void Frame(uint32_t writeIndex, const VansSSGIHistoryParameters& params)
        {
            std::memcpy(buffers[0].mapped, &camera, sizeof(camera)); std::memcpy(buffers[1].mapped, &params, sizeof(params));
            Begin(); Upload(Position, position); Upload(Normal, normal); Upload(Material, material); Upload(Motion, motion); Upload(Raw, raw); Barrier();
            const VkDescriptorSet sets[] = {cameraSet, temporalSets[writeIndex]};
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
            VansSSGIAtrousPushConstants params{}; params.stepWidth = step; params.normalPower = 24.0f;
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
        VkPhysicalDeviceFeatures features{}; features.shaderStorageImageExtendedFormats = VK_TRUE; features.imageCubeArray = VK_TRUE;
        features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
        features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
        VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
        indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        indexing.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pEnabledFeatures = &features; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.pNext = &indexing;
        Require(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
        if (!LoadVulkanDeviceLevelFunctions(device)) throw std::runtime_error("Vulkan device functions unavailable");
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; commandPool.queueFamilyIndex = family;
        Require(vkCreateCommandPool(device, &commandPool, nullptr, &commands), "command pool");
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; commandInfo.commandPool = commands; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
        Require(vkAllocateCommandBuffers(device, &commandInfo, &command), "command buffer");
        const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; poolInfo.maxSets = 12; poolInfo.poolSizeCount = 4; poolInfo.pPoolSizes = poolSizes;
        Require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "descriptor pool");
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; samplerInfo.minFilter = samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Require(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "sampler");
        samplerInfo.minFilter=samplerInfo.magFilter=VK_FILTER_LINEAR;
        Require(vkCreateSampler(device,&samplerInfo,nullptr,&grassEnergySampler),"grass energy sampler");
        CreateBuffer(0, sizeof(camera)); CreateBuffer(1, sizeof(VansSSGIHistoryParameters)); CreateBuffer(2, 1024 * 1024); CreateBuffer(3, Count * sizeof(glm::vec4));
        CreateBuffer(18, sizeof(glm::vec4));
        *static_cast<glm::vec4*>(buffers[18].mapped) = glm::vec4(1, 0, 0, 0);
        Begin();
        for (uint32_t slot = 0; slot < ImageCount; ++slot)
        {
            auto& image = images[slot];
            image.components = slot == MomentsA || slot == MomentsB ? 2u : 4u;
            image.half = slot == Raw || slot == HistoryA || slot == HistoryB || slot == MomentsA || slot == MomentsB || slot == Atrous || slot == GrassEnergy || slot == GatherCache || slot == GatherSurface;
            image.format = image.half ? (image.components == 2 ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT) : VK_FORMAT_R32G32B32A32_SFLOAT;
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType = VK_IMAGE_TYPE_2D; info.format = image.format;
            info.extent = {Width, Height, 1}; info.mipLevels = 1; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
            if(slot==GrassEnergy) info.extent={GrassEnergyLUT::Size,GrassEnergyLUT::Size,1};
            if(slot==GatherSky) { info.extent={1,1,1}; info.arrayLayers=6; info.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; }
            if(slot==GatherIrradiance) info.extent={16,8,1};
            if(slot==GatherVisibility) info.extent={16,16,1};
            if(slot==GatherHiZ) info.mipLevels=4; // 17x13 -> 8x6 -> 4x3 -> 2x1。
            image.width=info.extent.width; image.height=info.extent.height;
            image.mipLevels=info.mipLevels;
            info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            Require(vkCreateImage(device, &info, nullptr, &image.image), "create image");
            VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device, image.image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Require(vkAllocateMemory(device, &allocation, nullptr, &image.memory), "allocate image");
            Require(vkBindImageMemory(device, image.image, image.memory, 0), "bind image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format; view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if(slot==GatherSky) { view.viewType=VK_IMAGE_VIEW_TYPE_CUBE; view.subresourceRange.layerCount=6; }
            view.subresourceRange.levelCount=image.mipLevels;
            Require(vkCreateImageView(device, &view, nullptr, &image.view), "image view");
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.image = image.image; barrier.subresourceRange = view.subresourceRange; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkClearColorValue zero{}; vkCmdClearColorImage(command, image.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &view.subresourceRange);
        }
        Barrier();
        std::memcpy(buffers[2].mapped,GrassEnergyLUT::Pixels,sizeof(GrassEnergyLUT::Pixels));
        VkBufferImageCopy energyCopy{};
        energyCopy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        energyCopy.imageExtent={GrassEnergyLUT::Size,GrassEnergyLUT::Size,1};
        vkCmdCopyBufferToImage(command,buffers[2].buffer,images[GrassEnergy].image,VK_IMAGE_LAYOUT_GENERAL,1,&energyCopy);
        Barrier();Submit();
        VkDescriptorSetLayoutBinding bindings[]={
            {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {37,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {GLOBAL_BINDING_GRASS_ENERGY_LUT,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo cameraInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; cameraInfo.bindingCount = 3; cameraInfo.pBindings = bindings;
        Require(vkCreateDescriptorSetLayout(device, &cameraInfo, nullptr, &cameraLayout), "camera set layout");
        cameraSet = Allocate(cameraLayout); BindBuffer(cameraSet, 0, 0);
        BindBuffer(cameraSet, 37, 18);
        BindImage(cameraSet,GLOBAL_BINDING_GRASS_ENERGY_LUT,GrassEnergy);
        constexpr auto texture = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        constexpr auto storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        CreatePipeline(0, "SSGITemporal/SSGITemporalcomp.spv", {texture, texture, texture, storage, storage, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, texture, storage, texture, texture, texture, storage}, 0);
        CreatePipeline(1, "SSGIAtrous/SSGIAtrouscomp.spv", {texture, texture, texture, texture, storage}, sizeof(VansSSGIAtrousPushConstants));
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

    void Fixture::VerifyGather()
    {
        // Dispatch both production gather shaders. The odd extent exercises
        // partial cache tiles and full-resolution shared-memory edge loads.
        constexpr auto texture = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        constexpr auto storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        constexpr auto uniform = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        constexpr auto buffer = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        auto bindings = [](std::initializer_list<VkDescriptorSetLayoutBinding> values)
        {
            std::vector<VkDescriptorSetLayoutBinding> result(values);
            for (auto& value : result) value.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return result;
        };
        CreatePipelineBindings(4, "SSGIProbeCache/SSGIProbeCachecomp.spv", bindings({
            {0,texture,1},{1,texture,1},{2,texture,1},{3,texture,1},{4,texture,1},
            {5,storage,1},{6,storage,1},{7,uniform,1},
            {8,texture,kGIMaximumRegionCount},{9,texture,kGIMaximumRegionCount},{10,buffer,kGIMaximumRegionCount},
            {18,texture,1},{19,texture,1},{20,buffer,1},
            {21,texture,1},{22,texture,1},{23,uniform,1},{24,buffer,1},{30,buffer,1}}));
        CreatePipelineBindings(5, "SSGI/SSGIcomp.spv", bindings({
            {0,texture,1},{1,texture,1},{2,texture,1},{3,texture,1},{4,texture,1},
            {5,storage,1},{6,uniform,1},{10,texture,1},{11,texture,1},
            {20,texture,kGIMaximumRegionCount},{21,texture,kGIMaximumRegionCount},{22,buffer,kGIMaximumRegionCount},
            {23,texture,1},{24,texture,1},{25,buffer,1},{26,buffer,1},{27,buffer,1}}));
        CreateBuffer(19, sizeof(VansSSGIParameters));
        CreateBuffer(20, 4096); // 无自适应布局/接收者记录；常规区域直接读取 UBO。
        CreateBuffer(21, sizeof(glm::vec4) * 6); // 两个已发布 probe，各三行 GPU state。
        static_cast<glm::uvec4*>(buffers[21].mapped)[2] = glm::uvec4(1, 0, 1, 0);
        static_cast<glm::uvec4*>(buffers[21].mapped)[5] = glm::uvec4(1, 0, 1, 0);
        VansSSGIParameters info{};
        info.screenSize = glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height);
        info.regionInfo = glm::vec4(0, 32, 0.8f, 0);
        std::memcpy(buffers[19].mapped, &info, sizeof(info));
        VansSSGIHistoryParameters history{};
        history.screenSize = info.screenSize;
        std::memcpy(buffers[1].mapped, &history, sizeof(history));
        auto set = gatherSets[0] = Allocate(pipelines[4].setLayout);
        BindImage(set, 0, Normal); BindImage(set, 1, GatherDepth);
        BindImage(set, 2, Position); BindImage(set, 3, Material); BindImage(set, 4, GatherSky);
        BindImage(set, 5, GatherCache, true); BindImage(set, 6, GatherSurface, true);
        BindBuffer(set, 7, 19);
        BindImage(set, 8, GatherIrradiance, false, kGIMaximumRegionCount); BindImage(set, 9, GatherVisibility, false, kGIMaximumRegionCount);
        BindBuffer(set, 10, 21, buffer, VK_WHOLE_SIZE, kGIMaximumRegionCount);
        BindImage(set, 18, HistoryA); BindImage(set, 19, GatherHiZ);
        BindBuffer(set, 20, 20, buffer);
        BindImage(set, 21, SurfaceA); BindImage(set, 22, Motion); BindBuffer(set, 23, 1);
        BindBuffer(set, 24, 20, buffer); BindBuffer(set, 30, 20, buffer);
        set = gatherSets[1] = Allocate(pipelines[5].setLayout);
        BindImage(set, 0, Normal); BindImage(set, 1, GatherDepth); BindImage(set, 2, HistoryA);
        BindImage(set, 3, Position); BindImage(set, 4, GatherSky); BindImage(set, 5, Raw, true);
        BindBuffer(set, 6, 19); BindImage(set, 10, GatherHiZ); BindImage(set, 11, Material);
        BindImage(set, 20, GatherIrradiance, false, kGIMaximumRegionCount); BindImage(set, 21, GatherVisibility, false, kGIMaximumRegionCount);
        BindBuffer(set, 22, 21, buffer, VK_WHOLE_SIZE, kGIMaximumRegionCount);
        BindImage(set, 23, GatherCache); BindImage(set, 24, GatherSurface);
        for (uint32_t binding : {25u, 26u, 27u}) BindBuffer(set, binding, 20, buffer);

        const auto savedCamera = camera;
        camera.viewMatrix = glm::mat4(1);
        camera.projectionMatrix = glm::perspective(glm::radians(60.0f), float(Width) / Height, 0.1f, 1000.0f);
        camera.viewProjectionMatrix = camera.projectionMatrix;
        std::memcpy(buffers[0].mapped, &camera, sizeof(camera));
        std::array<glm::vec3, 6> sky;
        sky.fill(glm::vec3(0.25f, 0.5f, 1.0f));
        auto gather = [&](bool useCache)
        {
            Begin();
            Upload(Position, position); Upload(Normal, normal); Upload(Material, material); Upload(Motion, motion);
            std::vector<glm::vec4> depths(Count, glm::vec4(0.5f));
            for (uint32_t i = 0; i < Count; ++i) if (position[i].w <= 0) depths[i].x = 1;
            Upload(GatherDepth, depths);
            UploadGatherHiZ();
            for (uint32_t face = 0; face < 6; ++face)
            {
                // Sky diffuse stores E; callers consume E/pi.
                const auto value = sky[face] * glm::pi<float>();
                VkClearColorValue clear{{value.r, value.g, value.b, 1}};
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1};
                vkCmdClearColorImage(command, images[GatherSky].image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
            }
            Barrier();
            auto dispatch = [&](uint32_t pipeline, VkDescriptorSet descriptors, uint32_t x, uint32_t y)
            {
                const VkDescriptorSet sets[] = {cameraSet, descriptors};
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pipeline].pipeline);
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pipeline].layout, 0, 2, sets, 0, nullptr);
                vkCmdDispatch(command, x, y, 1);
            };
            dispatch(4, gatherSets[0], (Width + 31) / 32, (Height + 31) / 32);
            Barrier();
            if (!useCache)
            {
                VkClearColorValue zero{};
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdClearColorImage(command, images[GatherCache].image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
                Barrier();
            }
            dispatch(5, gatherSets[1], (Width + 7) / 8, (Height + 7) / 8);
            Submit();
            return Read(Raw);
        };
        auto colorMatches = [](glm::vec3 actual, glm::vec3 expected)
        {
            return glm::all(glm::lessThanEqual(glm::abs(actual - expected), glm::vec3(0.004f)));
        };
        Plane(90);
        for (bool useCache : {true, false})
        {
            const auto result = gather(useCache);
            for (const auto& value : result)
                Expect(colorMatches(glm::vec3(value), sky[0]) && value.a == 0,
                    "Gather constant E/pi or cold-history confidence is incorrect");
        }
        position.front().w = position.back().w = 0;
        const auto edge = gather(true);
        Expect(edge.front() == glm::vec4(0) && edge.back() == glm::vec4(0),
            "Gather sky pixels or partial edge tile wrote invalid lighting");
        std::cout << "SSGI production gather constant sky, forced cache miss, cold history and odd edges: PASS\n";

        // Strong angular variation isolates the receiver normal contract.
        // Each 1x1 cube face supplies known E/pi; the tilted material normal
        // samples +X while the unchanged geometric plane samples +Z.
        sky.fill(glm::vec3(1.2f, 2.4f, 4.8f));
        sky[0] = glm::vec3(0.4f, 0.8f, 1.6f);
        const glm::vec4 tilted(std::sqrt(0.75f), 0, 0.5f, 0);
        constexpr uint32_t center = (Height / 2) * Width + Width / 2;
        Plane(90);
        normal.assign(Count, tilted);
        const auto tiltedCache = gather(true)[center];
        const auto tiltedFallback = gather(false)[center];
        Plane(90);
        normal[center] = tilted; // Not a cache anchor: neighbors retain +Z.
        const auto normalDetail = gather(true)[center];
        std::cout << "SSGI normal reference=" << sky[0].r
            << " cache=" << tiltedCache.r << " fallback=" << tiltedFallback.r
            << " subTile=" << normalDetail.r << '\n';
        Expect(colorMatches(glm::vec3(tiltedCache), sky[0]) && colorMatches(glm::vec3(tiltedFallback), sky[0]),
            "Production gather integrates a different normal on screen misses");
        Expect(colorMatches(glm::vec3(normalDetail), sky[0]),
            "Production reconstruction reused lighting integrated for another material normal");

        // 同一平面上的薄叶透射响应也不能从相邻 PBR 锚点传播过来。
        Plane(90);
        normal.assign(Count, tilted);
        material[center].z = 10.0f; // Common.glsl 的 MATERIAL_ID_TREE。
        normal[center].w = 1.0f;
        const glm::vec3 leafReference = glm::mix(sky[0], sky[1], 0.7f);
        const auto leafDetail = gather(true)[center];
        const auto leafFallback = gather(false)[center];
        Expect(colorMatches(glm::vec3(leafDetail), leafReference) && colorMatches(glm::vec3(leafFallback), leafReference),
            "Production reconstruction reused another surface's vegetation response");
        std::cout << "SSGI gather normal detail and coplanar vegetation response: PASS\n";

        // 正式 DDGI 图集同样必须沿 Ns 查询；两个已发布 probe 提供相同方向分布。
        // 每个 8x8 tile 的 Ng 和倾斜 Ns 采样位置分别给定独立 E/pi。
        std::vector<glm::vec4> atlas(128, glm::vec4(sky[1] * glm::pi<float>(), 1));
        for (uint32_t y=0; y<8; ++y) for (uint32_t x=0; x<16; ++x)
            if (x%8>=5) atlas[y*16+x] = glm::vec4(sky[0] * glm::pi<float>(), 1);
        Begin(); Upload(GatherIrradiance, atlas);
        Upload(GatherVisibility, std::vector<glm::vec4>(256, glm::vec4(1000,1000000,0,0)));
        Submit();
        info.regionInfo.x = 1;
        info.regions[0].volumeMin = glm::vec4(-2,-2,-12,2);
        info.regions[0].volumeSizeAndBias = glm::vec4(4,4,4,0.2f);
        info.regions[0].traceParams = glm::vec4(0,0,0,1);
        info.regions[0].gridDimensionsAndPriority = glm::vec4(2,1,1,0);
        std::memcpy(buffers[19].mapped, &info, sizeof(info));
        Plane(90);
        normal.assign(Count, tilted);
        for (bool useCache : {true,false})
            Expect(colorMatches(glm::vec3(gather(useCache)[center]), sky[0]),
                "Production DDGI gather sampled the geometric instead of material normal");
        Plane(90);
        normal[center]=tilted;
        Expect(colorMatches(glm::vec3(gather(true)[center]), sky[0]),
            "Production DDGI reconstruction lost sub-tile normal detail");
        std::cout << "SSGI production DDGI atlas normal direction: PASS\n";
        // 只在末槽提供有效区域，验证扩容后的 UBO、描述符与混合循环均到达第 16 区。
        info.regions[kGIMaximumRegionCount-1] = info.regions[0];
        info.regions[0] = {};
        info.regionInfo.x = float(kGIMaximumRegionCount);
        std::memcpy(buffers[19].mapped, &info, sizeof(info));
        for (bool useCache : {true,false})
            Expect(colorMatches(glm::vec3(gather(useCache)[center]), sky[0]),
                "Production DDGI gather ignored the final region slot");
        info.regions[0] = info.regions[kGIMaximumRegionCount-1];
        info.regions[kGIMaximumRegionCount-1] = {};
        info.regionInfo.x = 1;
        std::memcpy(buffers[19].mapped, &info, sizeof(info));
        raw=gather(true);
        Frame(0);
        // 现行 Temporal 的 3x3 核对 60 度邻居保留 0.5^8 权重，并非无滤波输出。
        // 八个邻居等亮且共面，可独立计算闭式参考；Atrous 的 24 次幂只产生半精度以下变化。
        const glm::vec3 temporalReference=(glm::vec3(raw[center])+glm::vec3(raw[center+1])*(8.0f/256.0f))/(1.0f+8.0f/256.0f);
        const auto temporalDetail=Read(HistoryA)[center];
        const auto atrousDetail=Filter(0,1)[center];
        std::cout << "SSGI normal filter reference=" << temporalReference.r << " temporal=" << temporalDetail.r << " atrous=" << atrousDetail.r << '\n';
        Expect(colorMatches(glm::vec3(temporalDetail),temporalReference) &&
            colorMatches(glm::vec3(atrousDetail),temporalReference),
            "Production gather normal detail differs from the defined spatial filter response");

        // 每个 probe 方向恒定但颜色不同：只改变 Ns 时，Ng 的偏移/朝向/空间权重应完全不变。
        for (uint32_t y=0; y<8; ++y) for (uint32_t x=0; x<16; ++x)
            atlas[y*16+x]=glm::vec4(glm::vec3(x<8?0.3f:0.9f)*glm::pi<float>(),1);
        Begin();Upload(GatherIrradiance,atlas);Submit();
        Plane(90);
        const auto geometricReference=gather(false)[center];
        normal.assign(Count,tilted);
        const auto changedShading=gather(false)[center];
        Expect(colorMatches(glm::vec3(geometricReference),glm::vec3(0.6f)) &&
            changedShading==geometricReference,
            "Material normal changed DDGI geometry bias, facing or spatial candidates");
        std::cout << "SSGI gather-to-filter detail and fixed geometric weights: PASS\n";
        camera = savedCamera;
    }

    void Fixture::VerifyGatherCoverage()
    {
        const auto savedCamera = camera;
        constexpr uint32_t anchorX = 10, anchorY = 6, anchor = anchorY * Width + anchorX;
        constexpr uint32_t cachePixel = (anchorY / 4) * Width + anchorX / 4;
        constexpr double amplitude = 0.8, planeDepth = 5.0, originZ = -9.95;
        camera.viewMatrix = glm::mat4(1);
        camera.projectionMatrix = glm::perspective(glm::radians(90.0f), float(Width) / Height, 0.1f, 1000.0f);
        // 将接收点放在真实 cache anchor，投影偏移只移动屏幕采样网格。
        camera.projectionMatrix[2][0] = -(2.0f * (anchorX + 0.5f) / Width - 1.0f);
        camera.viewProjectionMatrix = camera.projectionMatrix;
        camera.lastViewProjectionMatrix = camera.projectionMatrix;
        camera.lastViewMatrix = camera.viewMatrix;
        Plane(90);
        normal.assign(Count, glm::vec4(0, 0, -1, 0));
        std::vector<glm::vec4> depths(Count), radiance(Count);
        std::vector<double> angularValue(Count), cosine(Count);
        auto p3 = [](double z) { return (5.0 * z * z * z - 3.0 * z) * 0.5; };
        const auto worldX = [&](double x) { return ((2.0 * x / Width - 1.0) + camera.projectionMatrix[2][0]) * planeDepth / camera.projectionMatrix[0][0]; };
        const auto worldY = [&](double y) { return (1.0 - 2.0 * y / Height) * planeDepth / camera.projectionMatrix[1][1]; };
        const double distance = -planeDepth - originZ;
        for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x)
        {
            const auto i = y * Width + x;
            position[i] = glm::vec4(worldX(x + 0.5), worldY(y + 0.5), -planeDepth, planeDepth);
            const double r2 = double(position[i].x) * position[i].x + double(position[i].y) * position[i].y + distance * distance;
            cosine[i] = distance / std::sqrt(r2);
            angularValue[i] = p3(cosine[i]);
            radiance[i] = glm::vec4(1.0 + amplitude * angularValue[i], 1.0 - amplitude * angularValue[i], 1, 1);
        }
        position[anchor] = glm::vec4(0, 0, -10, 10);
        normal[anchor] = glm::vec4(0, 0, 1, 0);
        for (uint32_t i = 0; i < Count; ++i)
        {
            const auto clip = camera.projectionMatrix * glm::vec4(glm::vec3(position[i]), 1);
            depths[i] = glm::vec4(clip.z / clip.w);
        }

        // 两个正值角场 1 +/- 0.8 P3 在完整余弦半球上的积分都为 1。
        // 历史图像为逐像素常量，独立面积积分补上它相对连续角场的离散误差。
        // d^2/(pi*r^4) 是平面面积到接收者余弦立体角的 Jacobian；不复制 Hi-Z 或融合公式。
        auto correction = [&](const auto& nodes, const auto& weights)
        {
            double result = 0;
            for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x)
            {
                const auto i = y * Width + x;
                if (i == anchor) continue;
                const double x0 = worldX(x), x1 = worldX(x + 1), y0 = worldY(y + 1), y1 = worldY(y);
                for (size_t a = 0; a < nodes.size(); ++a) for (size_t b = 0; b < nodes.size(); ++b)
                {
                    const double px = (x0 + x1 + nodes[a] * (x1 - x0)) * 0.5;
                    const double py = (y0 + y1 + nodes[b] * (y1 - y0)) * 0.5;
                    const double r2 = px * px + py * py + distance * distance;
                    result += (angularValue[i] - p3(distance / std::sqrt(r2))) *
                        distance * distance / (glm::pi<double>() * r2 * r2) *
                        weights[a] * weights[b] * (x1 - x0) * (y1 - y0) * 0.25;
                }
            }
            return result;
        };
        const double lowOrder = correction(
            std::array<double, 4>{-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526},
            std::array<double, 4>{0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538});
        const double highOrder = correction(
            std::array<double, 8>{-0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                0.1834346424956498, 0.5255324099163290, 0.7966664774136267, 0.9602898564975363},
            std::array<double, 8>{0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763});
        Expect(std::abs(lowOrder - highOrder) < 1e-6, "Screen radiance reference quadrature did not converge");
        const glm::vec3 reference(1.0 + amplitude * highOrder, 1.0 - amplitude * highOrder, 1);
        std::cout << "SSGI coverage reference=" << reference.r << ',' << reference.g << ',' << reference.b
            << " quadratureDelta=" << std::abs(lowOrder - highOrder) << '\n';

        // 使用正式 Temporal writer 生成表面历史，避免测试另写一套打包协议。
        Frame(0);
        const auto historySurface = Read(SurfaceA);
        VansSSGIParameters info{};
        info.screenSize = glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height);
        info.regionInfo = glm::vec4(0, 32, 0.8f, 0);
        std::memcpy(buffers[19].mapped, &info, sizeof(info));
        Begin(); Upload(GatherDepth, depths); UploadGatherHiZ();
        Upload(GatherVisibility, std::vector<glm::vec4>(256, glm::vec4(1000, 1000000, 0, 0)));
        Submit();
        float maximumError = 0;
        for (uint32_t test = 0; test < 10; ++test)
        {
            const bool probe = test >= 5;
            const uint32_t mode = test % 5;
            // 恒亮、冷历史、全部有效表面、上方角帽、其补集。物体和实际光场不随 mask 改变。
            const bool constant = mode == 0;
            const glm::vec3 expected = constant ? glm::vec3(1) : reference;
            info.regionInfo.x = probe ? 1.0f : 0.0f;
            info.regions[0].volumeMin = glm::vec4(-2, -2, -12, 2);
            info.regions[0].volumeSizeAndBias = glm::vec4(4, 4, 4, 0.2f);
            info.regions[0].traceParams = glm::vec4(0, 0, 0, 1);
            info.regions[0].gridDimensionsAndPriority = glm::vec4(2, 1, 1, 0);
            std::memcpy(buffers[19].mapped, &info, sizeof(info));
            auto acceptedSurface = historySurface;
            for (uint32_t i = 0; i < Count; ++i)
                if ((mode == 3 && cosine[i] < 0.8) || (mode == 4 && cosine[i] >= 0.8)) acceptedSurface[i].w = 0;
            VansSSGIHistoryParameters history{};
            history.screenSize = info.screenSize;
            history.frameParams.x = mode == 1 ? 0.0f : 1.0f;
            std::memcpy(buffers[1].mapped, &history, sizeof(history));
            Begin();
            Upload(HistoryA, constant ? std::vector<glm::vec4>(Count, glm::vec4(1)) : radiance);
            Upload(SurfaceA, acceptedSurface);
            Upload(GatherIrradiance, std::vector<glm::vec4>(128, glm::vec4(expected * glm::pi<float>(), 1)));
            for (uint32_t face = 0; face < 6; ++face)
            {
                // DDGI 用例故意提供不同天空，冷历史对照可确认实际读到已发布 probe。
                const glm::vec3 value = (probe ? glm::vec3(0.125f, 0.25f, 0.5f) : expected) * glm::pi<float>();
                VkClearColorValue clear{{value.r, value.g, value.b, 1}};
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1};
                vkCmdClearColorImage(command, images[GatherSky].image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
            }
            Submit();
            glm::dvec3 sum(0); double coverage = 0;
            // 完整 512 方向周期；不会把四条射线的单帧噪声当作系统偏差。
            for (uint32_t frame = 0; frame < 128; ++frame)
            {
                camera.frameParams.x = float(frame);
                std::memcpy(buffers[0].mapped, &camera, sizeof(camera));
                Begin();
                const VkDescriptorSet sets[] = {cameraSet, gatherSets[0]};
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[4].pipeline);
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[4].layout, 0, 2, sets, 0, nullptr);
                vkCmdDispatch(command, (Width + 31) / 32, (Height + 31) / 32, 1);
                Submit();
                const auto sample = Read(GatherCache)[cachePixel];
                sum += glm::dvec3(sample); coverage += sample.a - 1.0;
            }
            const glm::vec3 average(sum / 128.0);
            coverage /= 128.0;
            const glm::vec3 error = glm::abs(average - expected);
            maximumError = std::max(maximumError, std::max(error.r, std::max(error.g, error.b)));
            std::cout << "SSGI coverage source=" << (probe ? "DDGI" : "sky") << " mode=" << mode << " acceptedFraction=" << coverage
                << " mean=" << average.r << ',' << average.g << ',' << average.b
                << " error=" << error.r << ',' << error.g << ',' << error.b << '\n';
            Expect(mode == 1 ? coverage == 0 : coverage > 0.01 && coverage < 1,
                "Screen coverage fixture did not exercise the intended production history/Hi-Z path");
            if (mode < 2) Expect(std::max(error.r, std::max(error.g, error.b)) < 0.002f,
                "Constant field or cold-history gather changed reference energy");
        }
        camera = savedCamera;
        std::cout << "SSGI coverage maximumError=" << maximumError << " validationErrors=" << validationErrors << '\n';
        Expect(maximumError < 0.008f, "Screen coverage changed known hemispherical irradiance");
    }
}

bool TestSSGIGpuContract()
{
    try
    {
        validationErrors = 0;
        {
            Fixture gpu; gpu.Initialize();
            gpu.VerifyGather();
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

            // 通过原生 owner 上传正式参数和实际 A/B 图像，验证改灯不再清除表面或重启槽位。
            VansSSGIHistory lightingHistory;
            auto latest=[&]() { return gpu.Read(lightingHistory.WriteIndex() ? HistoryA : HistoryB); };
            gpu.Plane(90.0f); gpu.raw.assign(Count,glm::vec4(10,10,10,1));
            for(uint32_t i=0;i<48;++i) gpu.Frame(lightingHistory);
            VansGILightingChanges local;
            local.local=VansGILightingBounds{{-100,-100,-100},{-.001f,100,100}};
            lightingHistory.NotifyLighting(local,0); lightingHistory.SetProbeLightingPending(true);
            gpu.raw.assign(Count,glm::vec4(1)); gpu.Frame(lightingHistory);
            const auto localResult=latest();
            Expect(std::abs(localResult[center-3].x-1.0f)<.002f,"Local light-off retained stale irradiance");
            Expect(std::abs(localResult[center+3].x-9.64f)<.02f,"Local light change altered unrelated temporal weights");
            Expect(std::abs(localResult[center-3].a*255.0f-49.0f)<.15f &&
                std::abs(localResult[center+3].a*255.0f-49.0f)<.15f,"Light response destroyed valid history identity");

            VansGILightingChanges global; global.global=true;
            lightingHistory.Reset(); gpu.raw.assign(Count,glm::vec4(.02f,.02f,.02f,1));
            for(uint32_t i=0;i<48;++i) gpu.Frame(lightingHistory);
            lightingHistory.NotifyLighting(global,0); lightingHistory.SetProbeLightingPending(true);
            gpu.raw.assign(Count,glm::vec4(16,16,16,1)); gpu.Frame(lightingHistory);
            Expect(glm::length(glm::vec3(latest()[center])-glm::vec3(16))<.02f,"New bright lighting was held by old history");
            lightingHistory.NotifyLighting(global,0);
            gpu.raw.assign(Count,glm::vec4(0,0,0,1)); gpu.Frame(lightingHistory);
            Expect(glm::length(glm::vec3(latest()[center]))<.001f,"Light-off left a temporal afterglow");
            for(const auto color:{glm::vec3(2,0,0),glm::vec3(0,0,2)})
            {
                lightingHistory.NotifyLighting(global,0); gpu.raw.assign(Count,glm::vec4(color,1)); gpu.Frame(lightingHistory);
                Expect(glm::length(glm::vec3(latest()[center])-color)<.01f,"Chromatic change failed to respond");
            }

            // 使用正式光源分类器产生连续变化权重，而非测试直接指定 Shader 参数。
            VansRenderLightFrameData lightFrame;
            VansDirectionalLight sun{}; sun.m_Color=glm::vec3(1); sun.m_Intensity=1; sun.m_Direction=glm::vec3(0,1,0);
            lightFrame.directionalLights.push_back(sun);
            VansGILightingChangeTracker lightTracker; lightTracker.Update(lightFrame);
            auto continuousChange=[&]()
            {
                lightFrame.directionalLights[0].m_Intensity+=.001f;
                const auto changed=lightTracker.Update(lightFrame);
                Expect(changed.global && changed.changeAmount>0.0f && changed.changeAmount<.125f,"Continuous light input did not produce a bounded response");
                lightingHistory.NotifyLighting(changed,0); lightingHistory.SetProbeLightingPending(true);
            };
            lightingHistory.Reset(); float continuousNoise=0.0f;
            for(uint32_t frame=0;frame<120;++frame)
            {
                continuousChange();
                const float value=frame%2u ? 1.4f:.6f;
                gpu.raw.assign(Count,glm::vec4(value,value,value,1)); gpu.Frame(lightingHistory);
                const auto valueAfter=latest()[center];
                Expect(std::abs(valueAfter.a*255.0f-float(frame+1))<.2f,"Continuous changes restarted history age");
                if(frame>=40) continuousNoise=std::max(continuousNoise,std::abs(valueAfter.x-1.0f));
            }
            Expect(continuousNoise<.12f,"Continuous lighting failed to accumulate correlated sample noise");
            lightingHistory.Reset(); float rampLag=0.0f;
            for(uint32_t frame=0;frame<140;++frame)
            {
                continuousChange();
                const float value=1.0f+.02f*float(frame);
                gpu.raw.assign(Count,glm::vec4(value,value,value,1)); gpu.Frame(lightingHistory);
                rampLag=std::max(rampLag,std::abs(latest()[center].x-value));
            }
            Expect(rampLag<.18f,"Continuous light ramp accumulated excessive temporal lag");
            lightingHistory.Reset(); double activeSparseEnergy=0.0;
            for(uint32_t frame=0;frame<512;++frame)
            {
                continuousChange();
                const float value=frame%32u==31u?4.0f:.2f;
                gpu.raw.assign(Count,glm::vec4(value,value,value,1)); gpu.Frame(lightingHistory);
                if(frame>=256) activeSparseEnergy+=latest()[center].x;
            }
            const double activeSparseMean=activeSparseEnergy/256.0;
            Expect(std::abs(activeSparseMean-.31875)<.015,"Lighting response amplified rare HDR samples");
            const auto cancelledWrite=lightingHistory.WriteIndex();
            gpu.Frame(cancelledWrite,lightingHistory.PrepareFrame(Width,Height,{}));
            lightingHistory.ResolveRecordedFrame(false);
            gpu.raw.assign(Count,glm::vec4(.07f,.09f,.11f,1)); gpu.Frame(lightingHistory);
            Expect(glm::length(glm::vec3(latest()[center])-glm::vec3(.07f,.09f,.11f))<.001f &&
                std::abs(latest()[center].a*255.0f-1.0f)<.01f,"Untrusted submission reused a partially written history");
            std::cout<<"SSGI lighting local/static, on/off/color, continuous noise="<<continuousNoise
                <<" ramp lag="<<rampLag<<" active sparse mean="<<activeSparseMean<<" and failed submission recovery: PASS\n";
            gpu.VerifyGatherCoverage();
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

// Grass 与 SSGI 共用图像夹具，测试入口独立，不更改原 SSGI 用例。
bool TestGrassLightingGpuContract()
{
    try {
        {
            VansMaterialManager materials;
            VansGrassMaterial grass;
            Expect(grass.m_GrassParams.indirectDiffuseStrength==1.0f,"Grass indirect default must preserve unit lighting");
            Expect(materials.ApplyMaterialParameter(grass,"indirectDiffuseStrength",0.35f)
                && grass.m_GrassParams.indirectDiffuseStrength==0.35f
                && grass.m_GrassParams.transmissionStrength==0.5f
                && grass.m_GrassParams.normalStrength==1.0f,"Grass indirect parameter affects unrelated material controls");
            Expect(materials.ApplyMaterialParameter(grass,"indirectDiffuseStrength",-1.0f)
                && grass.m_GrassParams.indirectDiffuseStrength==0.0f,"Grass indirect strength lower bound");
            Expect(materials.ApplyMaterialParameter(grass,"indirectDiffuseStrength",3.0f)
                && grass.m_GrassParams.indirectDiffuseStrength==2.0f,"Grass indirect strength upper bound");
            std::cout<<"Grass indirect material parameter default, runtime edit, bounds, isolation: PASS\n";
        }
        validationErrors=0;
        {
            Fixture gpu;gpu.Initialize();
            constexpr auto texture=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gpu.CreatePipeline(2,"../../Source/Tests/Shaders/GrassLightingContract.comp.spv",
                {texture,texture,texture,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},4);
            auto set=gpu.Allocate(gpu.pipelines[2].setLayout);
            gpu.BindImage(set,0,Position);gpu.BindImage(set,1,Material);gpu.BindImage(set,2,ImageSlot::Normal);gpu.BindImage(set,3,Raw,true);
            gpu.camera.viewMatrix=glm::mat4(1);
            gpu.camera.projectionMatrix=glm::perspective(glm::radians(60.0f),float(Width)/Height,0.1f,100.0f);
            gpu.position.assign(Count,glm::vec4(0));gpu.material.assign(Count,glm::vec4(0,0,8,0));gpu.normal.assign(Count,glm::vec4(0,0,1,0));
            const uint32_t center=(Height/2)*Width+Width/2;
            auto dispatch=[&](uint32_t mode) {
                std::memcpy(gpu.buffers[0].mapped,&gpu.camera,sizeof(gpu.camera));
                gpu.Begin();gpu.Upload(Position,gpu.position);gpu.Upload(Material,gpu.material);gpu.Upload(ImageSlot::Normal,gpu.normal);gpu.Barrier();
                VkDescriptorSet sets[]={gpu.cameraSet,set};auto& p=gpu.pipelines[2];
                vkCmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
                vkCmdBindDescriptorSets(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,2,sets,0,nullptr);
                vkCmdPushConstants(gpu.command,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&mode);
                vkCmdDispatch(gpu.command,(Width+7)/8,(Height+7)/8,1);gpu.Submit();return gpu.Read(Raw);
            };
            auto open=dispatch(0);Expect(open[center].x==1 && open[center].y==1,"Empty screen must leave both hemispheres open");
            for(uint32_t y=0;y<Height;++y) for(uint32_t x=0;x<Width;++x) {
                const float depth=0.8f;
                const float nx=(float(x)+0.5f)/Width*2-1,ny=1-(float(y)+0.5f)/Height*2;
                gpu.position[y*Width+x]=glm::vec4(nx*depth/gpu.camera.projectionMatrix[0][0],ny*depth/gpu.camera.projectionMatrix[1][1],-depth,depth);
                gpu.material[y*Width+x].z=1;
            }
            auto blocked=dispatch(0);
            Expect(blocked[center].x<0.98f,"Screen occluder must reduce front hemisphere visibility");
            Expect(blocked[center].y>blocked[center].x,"Front and back occlusion must stay independent");
            std::cout<<"Grass AO open="<<open[center].x<<","<<open[center].y<<" blocked="<<blocked[center].x<<","<<blocked[center].y<<'\n';
            for(auto& m:gpu.material) m.z=8;
            auto thin=dispatch(0);Expect(thin[center].x>=blocked[center].x,"Thin grass must not occlude more than opaque geometry");
            for(float depth:{0.61f,0.73f,0.87f,1.0f,1.19f,1.37f,2.1f})
            {
                for(uint32_t y=0;y<Height;++y) for(uint32_t x=0;x<Width;++x)
                {
                    float nx=(float(x)+0.5f)/Width*2-1,ny=1-(float(y)+0.5f)/Height*2;
                    gpu.position[y*Width+x]=glm::vec4(nx*depth/gpu.camera.projectionMatrix[0][0],ny*depth/gpu.camera.projectionMatrix[1][1],-depth,depth);
                }
                auto ray=dispatch(11)[center];
                if(depth==1.0f || depth>2.0f) Expect(ray.x==0 && ray.y==0,"Grass AO self surface / out-of-range surface falsely blocks");
                else if(depth<1.0f) Expect(ray.x>0.2f && ray.y==0,"Thin foreground missed / blocks reverse hemisphere");
                else Expect(ray.y>0.2f && ray.x==0,"Thin background missed / blocks reverse hemisphere");
            }
            std::cout<<"Grass AO segment crossings, finite thickness, receiver rejection, range: PASS\n";
            auto normals=dispatch(5);
            for(uint32_t x=0;x<Width;++x) {
                glm::vec3 expected(0.3f,0.4f,std::sqrt(0.75f));
                if(x%6==2) expected.x=-expected.x;
                if(x%6==3) expected.y=-expected.y;
                if(x%6==4) expected=glm::vec3(0,0,1);
                if(x%6==5) expected=-expected;
                const auto sample=normals[(Height/2)*Width+x];
                Expect(glm::length(glm::vec3(sample)-expected)<0.002f && std::abs(sample.w-1.0f)<0.002f,
                    "Grass pixel normal fails screen-handedness / mirrored-UV / backface / degenerate-UV contract");
            }
            std::cout<<"Grass normal screen flip, mirrored U/V, neutral degenerate UV, backside, unit length: PASS\n";
            for(const auto& n:dispatch(6)) Expect(std::abs(n.x)<0.002f && std::abs(n.y)<0.002f && std::abs(n.z-1.0f)<0.002f,
                "Grass normal is not perpendicular to scaled/sheared/mirrored deformation tangents");
            std::cout<<"Grass inverse-transpose nonuniform scale, shear, mirror: PASS\n";
            auto front=dispatch(1),back=dispatch(2),opaqueBack=dispatch(3),sweep=dispatch(4);
            Expect(back[center].x>0 && opaqueBack[center].x==0,"Backlight requires transmission");
            for(const auto& pixel:sweep) for(int c=0;c<4;++c) Expect(std::isfinite(pixel[c]) && pixel[c]>=0,"Grass BSDF produces invalid grazing/opposition values");
            Expect(front[center].w>0,"Grass must retain front specular reflection");
            Expect(back[center].w==0,"Backlight must not create front surface reflection");
            std::cout<<"Grass back transmission="<<back[center].x<<" front specular="<<front[center].w<<'\n';
            float maxFurnace=0,maxDifference=0;
            for(uint32_t mode:{7u,8u,9u}) for(const auto& sample:dispatch(mode))
            {
                maxFurnace=std::max(maxFurnace,sample.x);
                maxDifference=std::max(maxDifference,std::abs(sample.x-sample.y));
                Expect(std::isfinite(sample.x) && sample.x<=1.004f && sample.y<=1.001f,"Grass white furnace creates energy");
                Expect(std::abs(sample.x-sample.y)<0.004f,"Grass direct / ambient integration mismatch");
                Expect(std::abs(sample.z-sample.w)<0.004f,"Grass specular LUT differs from actual GGX integral");
            }
            for(const auto& sample:dispatch(10))
                Expect(std::isfinite(sample.x) && sample.x<0.002f && sample.y<0.002f,"Grass LUT endpoints wrap or are discontinuous");
            std::cout<<"Grass white furnace max="<<maxFurnace<<" direct/ambient difference="<<maxDifference<<" LUT endpoints: PASS\n";
        }
        Expect(validationErrors==0,"Grass Vulkan validation errors");
        std::cout<<"GRASS_LIGHTING_GPU_CONTRACT_PASS validationErrors=0\n";return true;
    } catch(const std::exception& e) {std::cerr<<"GRASS_LIGHTING_GPU_CONTRACT_FAIL: "<<e.what()<<'\n';return false;}
}

bool TestGrassWorksetGpuContract()
{
    try
    {
        validationErrors = 0;
        {
            Fixture gpu;
            gpu.Initialize();
            constexpr uint32_t capacity = 256, boneCount = 3;
            const std::vector<GrassInstanceRange> ranges{ {192, 1, 64}, {0, 65, 128} };
            const auto groups = BuildGrassWorkgroups(capacity, ranges);
            std::vector<GrassInstance> instances(capacity);
            std::vector<GrassBone> bones(capacity * boneCount);
            std::vector<glm::mat4> matrices(capacity * boneCount * 2u, glm::mat4(9.0f));
            std::vector<float> lod(capacity, 42.0f);
            std::vector<uint32_t> flags(capacity, 1u), indices(capacity * 2u, UINT32_MAX);
            std::array<uint32_t, 2> counts{};
            std::vector<bool> active(capacity, false), occupiedBones(capacity * boneCount, false);
            for (auto& bone : bones)
                bone = {glm::vec4(999.0f), glm::vec4(777.0f), glm::vec4(555.0f)};
            for (const auto& range : ranges)
                for (uint32_t local = 0; local < range.instanceCount; ++local)
                {
                    const auto instance = range.baseInstance + local;
                    active[instance] = true;
                    instances[instance].modelMatrix[3] = glm::vec4(float(instance) * .01f, 0, -5, 1);
                    for (uint32_t bone = 0; bone < boneCount; ++bone)
                    {
                        const auto address = range.baseInstance * boneCount + bone * range.capacity + local;
                        occupiedBones[address] = true;
                        const auto position = glm::vec3(instances[instance].modelMatrix[3]) + glm::vec3(0, .5f * bone, 0);
                        bones[address] = {glm::vec4(position, 1), glm::vec4(position, 0), glm::vec4(0, .5f, 0, 0)};
                    }
                }
            flags[3] = 0u;
            const void* payloads[] = {instances.data(), bones.data(), matrices.data(), lod.data(),
                flags.data(), groups.data(), counts.data(), indices.data()};
            const size_t bytes[] = {instances.size()*sizeof(GrassInstance), bones.size()*sizeof(GrassBone),
                matrices.size()*sizeof(glm::mat4), lod.size()*sizeof(float), flags.size()*sizeof(uint32_t),
                groups.size()*sizeof(GrassWorkgroupGPU), sizeof(counts), indices.size()*sizeof(uint32_t)};
            for (uint32_t index = 0; index < 8; ++index)
            {
                gpu.CreateBuffer(4u + index, bytes[index]);
                std::memcpy(gpu.buffers[4u + index].mapped, payloads[index], bytes[index]);
            }
            gpu.camera.cameraPosition = glm::vec4(0);
            gpu.camera.unjitteredViewProjectionMatrix = glm::mat4(1);
            std::memcpy(gpu.buffers[0].mapped, &gpu.camera, sizeof(gpu.camera));
            constexpr auto storage = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            constexpr auto uniform = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            constexpr auto texture = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gpu.CreatePipeline(2, "GrassBoneSim/GrassBoneSimcomp.spv",
                {storage, storage, storage, texture, storage, uniform, storage, storage}, sizeof(GrassSimPushConstants));
            gpu.CreatePipeline(3, "GrassCull/GrassCullcomp.spv",
                {storage, storage, storage, storage, texture, texture, storage}, sizeof(GrassCullPushConstants));
            const auto simSet = gpu.Allocate(gpu.pipelines[2].setLayout);
            const auto cullSet = gpu.Allocate(gpu.pipelines[3].setLayout);
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_INSTANCE_DATA, 4, storage);
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_BONE_DATA, 5, storage);
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_BONE_MATRICES, 6, storage);
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_LOD_FACTORS, 7, storage);
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_SCATTER_OFFSETS, 2, uniform, 32u * sizeof(glm::vec4));
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_VISIBILITY_FLAGS, 8, storage);
            gpu.BindBuffer(simSet, VEG_SIM_BINDING_WORKGROUPS, 9, storage);
            gpu.BindBuffer(cullSet, VEG_CULL_BINDING_INSTANCE_DATA, 4, storage);
            gpu.BindBuffer(cullSet, VEG_CULL_BINDING_VISIBILITY, 8, storage);
            gpu.BindBuffer(cullSet, VEG_CULL_BINDING_VISIBLE_COUNT, 10, storage);
            gpu.BindBuffer(cullSet, VEG_CULL_BINDING_VISIBLE_INDICES, 11, storage);
            gpu.BindImage(cullSet, VEG_CULL_BINDING_HIZ, ImageSlot::Normal);
            gpu.BindBuffer(cullSet, VEG_CULL_BINDING_WORKGROUPS, 9, storage);
            const auto submitRead = [&] {
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(gpu.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
                gpu.Submit();
            };
            const auto dispatch = [&](uint32_t pipeline, VkDescriptorSet set, const void* constants, uint32_t size,
                uint32_t count) {
                const auto& p = gpu.pipelines[pipeline];
                const VkDescriptorSet sets[] = {gpu.cameraSet, set};
                vkCmdBindPipeline(gpu.command, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
                vkCmdBindDescriptorSets(gpu.command, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 2, sets, 0, nullptr);
                vkCmdPushConstants(gpu.command, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, constants);
                vkCmdDispatch(gpu.command, count, 1, 1);
            };
            GrassSimPushConstants sim{};
            sim.deltaTime = 1.f/60; sim.stiffness = 15; sim.damping = .92f; sim.softness = .2f;
            sim.windDirX = 1; sim.subBladeCount = 1; sim.grassHeight = 1;
            sim.lodFullDist = .5f; sim.lodFadeDist = 1; sim.cullVisibilityEnabled = 1; sim.boneCount = boneCount;
            gpu.Begin();
            dispatch(2, simSet, &sim, sizeof(sim), static_cast<uint32_t>(groups.size()));
            submitRead();
            const auto* output = static_cast<const glm::mat4*>(gpu.buffers[6].mapped);
            for (uint32_t instance = 0; instance < capacity; ++instance)
                for (uint32_t bone = 0; bone < boneCount; ++bone)
                {
                    const auto index = instance * boneCount + bone;
                    const bool updated = active[instance] && instance != 3u;
                    Expect(output[index + capacity * boneCount] == glm::mat4(9.0f), "Grass initial history changed");
                    if (updated)
                    {
                        Expect(output[index] != glm::mat4(9.0f), "Sparse grass instance did not produce a draw matrix");
                        for (uint32_t column = 0; column < 4; ++column)
                            for (uint32_t row = 0; row < 4; ++row)
                                Expect(std::isfinite(output[index][column][row]), "Sparse grass matrix is not finite");
                    }
                    else Expect(output[index] == glm::mat4(9.0f), "Grass sim touched an inactive or culled instance");
                }
            const std::vector<glm::mat4> previous(output, output + capacity * boneCount);
            static_cast<uint32_t*>(gpu.buffers[8].mapped)[3] = 1u;
            sim.lodFullDist = 100; sim.lodFadeDist = 200;
            gpu.Begin();
            dispatch(2, simSet, &sim, sizeof(sim), static_cast<uint32_t>(groups.size()));
            submitRead();
            for (uint32_t instance = 0; instance < capacity; ++instance)
                for (uint32_t bone = 0; bone < boneCount; ++bone)
                {
                    const auto index = instance * boneCount + bone;
                    if (active[instance])
                    {
                        Expect(output[index + capacity * boneCount] == previous[index], "Grass reentry lost its last simulated motion history");
                        for (uint32_t column = 0; column < 4; ++column)
                            for (uint32_t row = 0; row < 4; ++row)
                                Expect(std::isfinite(output[index][column][row]), "Sparse Verlet update is not finite");
                    }
                    else Expect(output[index] == glm::mat4(9.0f), "Sparse Verlet update touched a pool hole");
                }
            const auto* outputBones = static_cast<const GrassBone*>(gpu.buffers[5].mapped);
            for (uint32_t address = 0; address < bones.size(); ++address)
                if (!occupiedBones[address])
                    Expect(std::memcmp(outputBones + address, bones.data() + address, sizeof(GrassBone)) == 0,
                        "Sparse Verlet update overwrote another page's reserved bone state");
            std::memset(gpu.buffers[8].mapped, 0, bytes[4]);
            GrassCullPushConstants cull{};
            cull.cullDistance = -1; cull.instanceCount = capacity; cull.subBladeCount = 2; cull.shadowEnabled = 1;
            gpu.Begin();
            dispatch(3, cullSet, &cull, sizeof(cull), 1);
            cull.workgroupOffset = 1;
            dispatch(3, cullSet, &cull, sizeof(cull), 2);
            submitRead();
            const auto* outputCounts = static_cast<const uint32_t*>(gpu.buffers[10].mapped);
            const auto* outputIndices = static_cast<const uint32_t*>(gpu.buffers[11].mapped);
            const auto* outputFlags = static_cast<const uint32_t*>(gpu.buffers[8].mapped);
            Expect(outputCounts[0] == 132 && outputCounts[1] == 132, "Sparse cull or split dispatch produced the wrong indirect instance count");
            for (uint32_t view = 0; view < 2; ++view)
            {
                std::vector<uint32_t> visits(capacity, 0);
                for (uint32_t index = 0; index < 66; ++index)
                {
                    const auto instance = outputIndices[view * capacity + index];
                    Expect(instance < capacity, "Sparse cull wrote an invalid draw index");
                    ++visits[instance];
                }
                for (uint32_t instance = 0; instance < capacity; ++instance)
                    Expect(visits[instance] == (active[instance] ? 1u : 0u) &&
                        outputFlags[instance] == (active[instance] ? 1u : 0u),
                        "Sparse cull skipped, repeated or included a pool hole");
            }

            // Grow and relocate both pages using the production copy regions.
            // Current/history bases differ between pool owners; unchanged Verlet
            // state must survive before the next real simulation dispatch.
            constexpr uint32_t grownCapacity = 384;
            const std::vector<GrassInstanceRange> relocated{ {0, 1, 128}, {256, 65, 128} };
            const auto relocatedGroups = BuildGrassWorkgroups(grownCapacity, relocated);
            const size_t grownBytes[] = {grownCapacity*boneCount*sizeof(GrassBone),
                grownCapacity*boneCount*2u*sizeof(glm::mat4), grownCapacity*sizeof(float),
                grownCapacity*sizeof(GrassInstance), relocatedGroups.size()*sizeof(GrassWorkgroupGPU),
                grownCapacity*sizeof(uint32_t)};
            for (uint32_t index=0; index<6; ++index) gpu.CreateBuffer(12u+index,grownBytes[index]);
            std::vector<GrassInstance> relocatedInstances(grownCapacity);
            std::vector<glm::mat4> sentinels(grownCapacity*boneCount*2u,glm::mat4(11.0f));
            std::memcpy(gpu.buffers[13].mapped,sentinels.data(),grownBytes[1]);
            const std::vector<glm::mat4> sourceMatrices(output,output+capacity*boneCount*2u);
            const std::vector<GrassBone> sourceBones(outputBones,outputBones+capacity*boneCount);
            const auto* sourceLod=static_cast<const float*>(gpu.buffers[7].mapped);
            gpu.Begin();
            for (uint32_t page=0; page<ranges.size(); ++page)
            {
                for (uint32_t local=0;local<ranges[page].instanceCount;++local)
                    relocatedInstances[relocated[page].baseInstance+local]=instances[ranges[page].baseInstance+local];
                for (const auto& region:BuildGrassStateCopyRegions(capacity,grownCapacity,boneCount,ranges[page],relocated[page]))
                {
                    const uint32_t sourceSlot=region.buffer==GrassStateBuffer::Bones?5u:
                        region.buffer==GrassStateBuffer::Matrices?6u:7u;
                    const uint32_t destinationSlot=region.buffer==GrassStateBuffer::Bones?12u:
                        region.buffer==GrassStateBuffer::Matrices?13u:14u;
                    const VkDeviceSize stride=region.buffer==GrassStateBuffer::Bones?sizeof(GrassBone):
                        region.buffer==GrassStateBuffer::Matrices?sizeof(glm::mat4):sizeof(float);
                    const VkBufferCopy copy{region.sourceElement*stride,region.destinationElement*stride,region.elementCount*stride};
                    vkCmdCopyBuffer(gpu.command,gpu.buffers[sourceSlot].buffer,gpu.buffers[destinationSlot].buffer,1,&copy);
                }
            }
            VkMemoryBarrier copyHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            copyHost.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;copyHost.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                0,1,&copyHost,0,nullptr,0,nullptr);
            gpu.Submit();
            const auto* relocatedMatrices=static_cast<const glm::mat4*>(gpu.buffers[13].mapped);
            const auto* relocatedBones=static_cast<const GrassBone*>(gpu.buffers[12].mapped);
            for (uint32_t page=0;page<ranges.size();++page)
            {
                const auto& from=ranges[page];const auto& to=relocated[page];
                for (uint32_t bone=0;bone<boneCount;++bone)
                    Expect(std::memcmp(relocatedBones+to.baseInstance*boneCount+bone*to.capacity,
                        sourceBones.data()+from.baseInstance*boneCount+bone*from.capacity,
                        from.instanceCount*sizeof(GrassBone))==0,
                        "Grass pool replacement reset persistent Verlet state");
                for (uint32_t local=0;local<from.instanceCount;++local)
                {
                    const uint32_t oldInstance=from.baseInstance+local,newInstance=to.baseInstance+local;
                    Expect(static_cast<const float*>(gpu.buffers[14].mapped)[newInstance]==sourceLod[oldInstance],
                        "Grass pool replacement lost LOD state");
                    for (uint32_t history=0;history<2;++history)
                        for (uint32_t bone=0;bone<boneCount;++bone)
                            Expect(relocatedMatrices[(history*grownCapacity+newInstance)*boneCount+bone]==
                                sourceMatrices[(history*capacity+oldInstance)*boneCount+bone],
                                "Grass pool growth or relocation lost current/history matrices");
                }
            }
            std::memcpy(gpu.buffers[15].mapped,relocatedInstances.data(),grownBytes[3]);
            std::memcpy(gpu.buffers[16].mapped,relocatedGroups.data(),grownBytes[4]);
            std::fill_n(static_cast<uint32_t*>(gpu.buffers[17].mapped),grownCapacity,1u);
            gpu.BindBuffer(simSet,VEG_SIM_BINDING_INSTANCE_DATA,15,storage);
            gpu.BindBuffer(simSet,VEG_SIM_BINDING_BONE_DATA,12,storage);
            gpu.BindBuffer(simSet,VEG_SIM_BINDING_BONE_MATRICES,13,storage);
            gpu.BindBuffer(simSet,VEG_SIM_BINDING_LOD_FACTORS,14,storage);
            gpu.BindBuffer(simSet,VEG_SIM_BINDING_VISIBILITY_FLAGS,17,storage);
            gpu.BindBuffer(simSet,VEG_SIM_BINDING_WORKGROUPS,16,storage);
            gpu.Begin();
            dispatch(2,simSet,&sim,sizeof(sim),static_cast<uint32_t>(relocatedGroups.size()));
            submitRead();
            for (uint32_t page=0;page<ranges.size();++page)
                for (uint32_t local=0;local<ranges[page].instanceCount;++local)
                    for (uint32_t bone=0;bone<boneCount;++bone)
                        Expect(relocatedMatrices[(grownCapacity+relocated[page].baseInstance+local)*boneCount+bone]==
                            sourceMatrices[(ranges[page].baseInstance+local)*boneCount+bone],
                            "Simulation after pool replacement reset motion history");
            Expect(relocatedMatrices[128u*boneCount]==glm::mat4(11.0f),
                "Simulation after pool replacement touched a capacity hole");

            // Update only the larger page in the same physical arena. Verify
            // every untouched byte, including neighbouring bone planes and
            // both current/history matrix blocks, against the previous state.
            const auto pageUpload=BuildGrassPageUploadLayout(grownCapacity,boneCount,relocated[1]);
            Expect(pageUpload.byteCount==65u*(sizeof(GrassInstance)+boneCount*(sizeof(GrassBone)+2u*sizeof(glm::mat4))+sizeof(float)),
                "Grass local staging included the whole pool or reserved page holes");
            std::vector<uint8_t> payload(static_cast<size_t>(pageUpload.byteCount),0);
            std::vector<GrassInstance> edited(65);
            std::vector<GrassBone> editedBones(65u*boneCount);
            std::vector<glm::mat4> resetMatrices(65u*boneCount*2u,glm::mat4(1));
            for (uint32_t local=0;local<65;++local)
            {
                edited[local]=relocatedInstances[256u+local];
                edited[local].randomSeed+=7u;
                for (uint32_t bone=0;bone<boneCount;++bone)
                    editedBones[bone*65u+local]=relocatedBones[256u*boneCount+bone*128u+local];
            }
            std::memcpy(payload.data(),edited.data(),edited.size()*sizeof(GrassInstance));
            std::memcpy(payload.data()+pageUpload.boneOffset,editedBones.data(),editedBones.size()*sizeof(GrassBone));
            std::memcpy(payload.data()+pageUpload.matrixOffset,resetMatrices.data(),resetMatrices.size()*sizeof(glm::mat4));
            std::memcpy(gpu.buffers[2].mapped,payload.data(),payload.size());
            const auto destinationSlot=[](GrassStateBuffer buffer) {
                return buffer==GrassStateBuffer::Bones?12u:buffer==GrassStateBuffer::Matrices?13u:
                    buffer==GrassStateBuffer::Lod?14u:15u;
            };
            std::array<std::vector<uint8_t>,4> expected;
            for (uint32_t slot=12;slot<16;++slot)
            {
                const auto* begin=static_cast<const uint8_t*>(gpu.buffers[slot].mapped);
                expected[slot-12].assign(begin,begin+gpu.buffers[slot].size);
            }
            gpu.Begin();
            for (const auto& region:pageUpload.regions)
            {
                const auto slot=destinationSlot(region.buffer);
                Expect(region.sourceOffset+region.byteCount<=payload.size() &&
                    region.destinationOffset+region.byteCount<=expected[slot-12].size(),
                    "Grass local upload exceeded its staging or destination buffer");
                std::memcpy(expected[slot-12].data()+region.destinationOffset,
                    payload.data()+region.sourceOffset,static_cast<size_t>(region.byteCount));
                const VkBufferCopy copy{region.sourceOffset,region.destinationOffset,region.byteCount};
                vkCmdCopyBuffer(gpu.command,gpu.buffers[2].buffer,gpu.buffers[slot].buffer,1,&copy);
            }
            vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                0,1,&copyHost,0,nullptr,0,nullptr);
            gpu.Submit();
            for (uint32_t slot=12;slot<16;++slot)
                Expect(std::memcmp(gpu.buffers[slot].mapped,expected[slot-12].data(),expected[slot-12].size())==0,
                    "Grass local update corrupted an unchanged page, reserved bone plane or history block");
            gpu.Begin();
            dispatch(2,simSet,&sim,sizeof(sim),static_cast<uint32_t>(relocatedGroups.size()));
            submitRead();
            for (uint32_t local=0;local<65;++local)
                for (uint32_t bone=0;bone<boneCount;++bone)
                    Expect(relocatedMatrices[(grownCapacity+256u+local)*boneCount+bone]==glm::mat4(1),
                        "Grass simulation after local upload used another page's motion history");
        }
        Expect(validationErrors == 0, "Grass workset Vulkan validation errors");
        std::cout << "GRASS_WORKSET_GPU_CONTRACT_PASS sparsePages=2 tailThreads=1 reentry=1 splitDispatch=1 poolGrowth=1 statePreservation=1 localPageUpload=1 validationErrors=0\n";
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "GRASS_WORKSET_GPU_CONTRACT_FAIL: " << error.what() << '\n';
        return false;
    }
}
