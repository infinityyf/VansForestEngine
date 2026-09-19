#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/GICore/VansGIProbeLayout.h"
#include "../EngineCore/RenderCore/GICore/VansGIScrollingGrid.h"
#include <algorithm>
#include <limits>
#include <iomanip>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <map>
#include <glm/gtc/packing.hpp>
#include "../EngineCore/RenderCore/GICore/VansGIProbeWorkScheduler.h"
#include "../EngineCore/RenderCore/BRDFData/VansLight.h"
#include <stdexcept>

namespace
{
    using namespace VansGraphics;
    uint32_t validationErrors = 0;
    void Require(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
    }
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            ++validationErrors;
            std::cerr << "[GIProbeLayoutGPU validation] " << message->pMessage << '\n';
        }
        return VK_FALSE;
    }

    struct GPU
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t queueFamily = 0;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkShaderModule shader = VK_NULL_HANDLE;
        VkCommandPool commands = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
        std::array<VkBuffer, 9> buffers{};
        std::array<VkDeviceMemory, 9> memories{};
        std::array<VkImage, 3> images{};
        std::array<VkDeviceMemory, 3> imageMemories{};
        std::array<VkImageView, 3> imageViews{};
        VkSampler sampler = VK_NULL_HANDLE;
        ~GPU()
        {
            if (device)
            {
                vkDeviceWaitIdle(device);
                if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
                if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                if (shader) vkDestroyShaderModule(device, shader, nullptr);
                if (commands) vkDestroyCommandPool(device, commands, nullptr);
                if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
                if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
                if (sampler) vkDestroySampler(device, sampler, nullptr);
                for (size_t i = 0; i < images.size(); ++i)
                {
                    if (imageViews[i]) vkDestroyImageView(device, imageViews[i], nullptr);
                    if (images[i]) vkDestroyImage(device, images[i], nullptr);
                    if (imageMemories[i]) vkFreeMemory(device, imageMemories[i], nullptr);
                }
                for (size_t i = 0; i < buffers.size(); ++i)
                {
                    if (buffers[i]) vkDestroyBuffer(device, buffers[i], nullptr);
                    if (memories[i]) vkFreeMemory(device, memories[i], nullptr);
                }
                vkDestroyDevice(device, nullptr);
            }
            if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
            if (instance) vkDestroyInstance(instance, nullptr);
        }
        void Buffer(size_t slot, VkDeviceSize size, const void* data)
        {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = size; info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            Require(vkCreateBuffer(device, &info, nullptr, &buffers[slot]), "create buffer");
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(device, buffers[slot], &requirements);
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(physical, &properties);
            uint32_t type = ~0u;
            constexpr VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
                { type = i; break; }
            if (type == ~0u) throw std::runtime_error("Host-visible coherent memory unavailable");
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
            Require(vkAllocateMemory(device, &allocation, nullptr, &memories[slot]), "allocate buffer memory");
            Require(vkBindBufferMemory(device, buffers[slot], memories[slot], 0), "bind buffer memory");
            void* mapped = nullptr;
            Require(vkMapMemory(device, memories[slot], 0, size, 0, &mapped), "map upload");
            if (data) std::memcpy(mapped, data, size); else std::memset(mapped, 0xff, size);
            vkUnmapMemory(device, memories[slot]);
        }
    };

    struct alignas(16) QueryResult
    {
        glm::uvec4 metadata;
        glm::vec4 minimumAndSpacing;
        std::array<uint32_t, 8> corners;
        std::array<glm::vec4, 8> positions;
        glm::vec4 samplePositionAndLeaf;
        std::array<float, 8> weights;
    };
    static_assert(sizeof(QueryResult) == 240);
    std::filesystem::path ShaderPath()
    {
        auto root = std::filesystem::current_path();
        const auto relative = std::filesystem::path("EngineAssets/Validation/GIProbe/GIProbeLayoutContractcomp.spv");
        for (;;)
        {
            if (std::filesystem::exists(root / relative)) return root / relative;
            if (std::filesystem::exists(root / "ForestEngine" / relative)) return root / "ForestEngine" / relative;
            if (root == root.root_path()) break;
            root = root.parent_path();
        }
        throw std::runtime_error("GI layout contract shader not found");
    }
    void InitializeGPU(GPU& gpu)
    {
        if (!vulkan_library) vulkan_library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!vulkan_library || !LoadVulkanExportedFunction() || !LoadVulkanGlobalLevelFunctions())
            throw std::runtime_error("Vulkan loader unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Forest GI Layout GPU Contract"; app.apiVersion = VK_API_VERSION_1_2;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* extensions[] = {"VK_EXT_debug_utils", "VK_EXT_validation_features"};
        VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        validation.enabledValidationFeatureCount = 1; validation.pEnabledValidationFeatures = &sync;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback = Validation;
        VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance.pApplicationInfo = &app; instance.enabledLayerCount = 1; instance.ppEnabledLayerNames = &layer;
        instance.enabledExtensionCount = 2; instance.ppEnabledExtensionNames = extensions;
        validation.pNext = &debug; instance.pNext = &validation;
        Require(vkCreateInstance(&instance, nullptr, &gpu.instance), "create instance");
        if (!LoadVulkanInstanceLevelFunctions(gpu.instance)) throw std::runtime_error("Vulkan instance functions unavailable");
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(gpu.instance, "vkCreateDebugUtilsMessengerEXT"));
        gpu.destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(gpu.instance, "vkDestroyDebugUtilsMessengerEXT"));
        Require(createMessenger(gpu.instance, &debug, nullptr, &gpu.messenger), "create validation messenger");
        uint32_t count = 0;
        Require(vkEnumeratePhysicalDevices(gpu.instance, &count, nullptr), "enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        Require(vkEnumeratePhysicalDevices(gpu.instance, &count, devices.data()), "read GPUs");
        if (devices.empty()) throw std::runtime_error("No Vulkan GPU");
        gpu.physical = devices.front();
        vkGetPhysicalDeviceQueueFamilyProperties(gpu.physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu.physical, &count, families.data());
        while (gpu.queueFamily < count && !(families[gpu.queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++gpu.queueFamily;
        if (gpu.queueFamily == count) throw std::runtime_error("No compute queue");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = gpu.queueFamily; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        VkPhysicalDevice16BitStorageFeatures storage{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
        storage.storageBuffer16BitAccess = VK_TRUE;
        device.pNext = &storage;
        VkPhysicalDeviceFeatures features{}; features.shaderStorageImageExtendedFormats = VK_TRUE;
        device.pEnabledFeatures = &features;
        device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
        Require(vkCreateDevice(gpu.physical, &device, nullptr, &gpu.device), "create device");
        if (!LoadVulkanDeviceLevelFunctions(gpu.device)) throw std::runtime_error("Vulkan device functions unavailable");
        vkGetDeviceQueue(gpu.device, gpu.queueFamily, 0, &gpu.queue);


    }
    void CreateCompute(GPU& gpu, VkDescriptorSet& set, const std::vector<VkDeviceSize>& sizes,
        const std::filesystem::path& shaderPath, uint32_t pushBytes,
        const std::vector<VkDescriptorImageInfo>& textures = {},
        const std::vector<uint32_t>& storageImages = {}, bool skyUniform = false)
    {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(sizes.size() + textures.size());
        for (size_t i = 0; i < sizes.size(); ++i)
            layoutBindings[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        for (uint32_t binding : storageImages)
            layoutBindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        for (size_t i = 0; i < textures.size(); ++i)
            layoutBindings[sizes.size() + i] = {uint32_t(sizes.size() + i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        if (skyUniform) layoutBindings.push_back({37u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = uint32_t(layoutBindings.size()); setLayout.pBindings = layoutBindings.data();
        Require(vkCreateDescriptorSetLayout(gpu.device, &setLayout, nullptr, &gpu.setLayout), "create descriptor layout");
        const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(sizes.size())},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, std::max(1u,uint32_t(textures.size()))},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, std::max(1u,uint32_t(storageImages.size()))},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1; pool.poolSizeCount = skyUniform ? 4u : (storageImages.empty() ? (textures.empty() ? 1u : 2u) : 3u); pool.pPoolSizes = poolSizes;
        Require(vkCreateDescriptorPool(gpu.device, &pool, nullptr, &gpu.pool), "create descriptor pool");
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = gpu.pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &gpu.setLayout;

        Require(vkAllocateDescriptorSets(gpu.device, &allocation, &set), "allocate descriptor set");
        std::vector<VkDescriptorBufferInfo> bufferInfo(sizes.size());
        std::vector<VkWriteDescriptorSet> writes(sizes.size());
        std::vector<VkDescriptorImageInfo> storageInfo(storageImages.size());
        for (size_t i = 0; i < writes.size(); ++i)
        {
            const auto found = std::find(storageImages.begin(), storageImages.end(), uint32_t(i));
            if (found != storageImages.end())
            {
                size_t slot = size_t(found - storageImages.begin());
                storageInfo[slot] = {VK_NULL_HANDLE, gpu.imageViews[slot], VK_IMAGE_LAYOUT_GENERAL};
                writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[i].dstSet = set; writes[i].dstBinding = uint32_t(i); writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[i].pImageInfo = &storageInfo[slot];
                continue;
            }
            bufferInfo[i] = {gpu.buffers[i], 0, sizes[i]};
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set; writes[i].dstBinding = uint32_t(i); writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &bufferInfo[i];
        }
        for (size_t i = 0; i < textures.size(); ++i)
        {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = uint32_t(sizes.size() + i); write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &textures[i];
            writes.push_back(write);
        }
        VkDescriptorBufferInfo skyInfo{gpu.buffers[8], 0, sizeof(glm::vec4)};
        if (skyUniform)
        {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = 37; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &skyInfo;
            writes.push_back(write);
        }
        vkUpdateDescriptorSets(gpu.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
        std::ifstream file(shaderPath, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot read contract SPIR-V");
        const size_t byteCount = size_t(file.tellg());
        if (!byteCount || byteCount % 4) throw std::runtime_error("Invalid contract SPIR-V length");
        std::vector<uint32_t> code(byteCount / 4);
        file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), byteCount);
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = byteCount; shader.pCode = code.data();
        Require(vkCreateShaderModule(gpu.device, &shader, nullptr, &gpu.shader), "create shader module");
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0u, pushBytes};
        pipelineLayout.pushConstantRangeCount = pushBytes ? 1u : 0u;
        pipelineLayout.pPushConstantRanges = pushBytes ? &push : nullptr;
        pipelineLayout.setLayoutCount = 1; pipelineLayout.pSetLayouts = &gpu.setLayout;
        Require(vkCreatePipelineLayout(gpu.device, &pipelineLayout, nullptr, &gpu.pipelineLayout), "create pipeline layout");
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.layout = gpu.pipelineLayout; pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module = gpu.shader; pipeline.stage.pName = "main";
        Require(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &gpu.pipeline), "create compute pipeline");
    }
    std::vector<QueryResult> Dispatch(const std::vector<glm::uvec4>& packet, const std::vector<glm::vec4>& queries)
    {
        GPU gpu;
        InitializeGPU(gpu);
        const std::array<VkDeviceSize, 3> sizes = {packet.size() * sizeof(glm::uvec4),
            queries.size() * sizeof(glm::vec4), queries.size() * sizeof(QueryResult)};
        gpu.Buffer(0, sizes[0], packet.data()); gpu.Buffer(1, sizes[1], queries.data()); gpu.Buffer(2, sizes[2], nullptr);
        VkDescriptorSet set;
        CreateCompute(gpu, set, std::vector<VkDeviceSize>(sizes.begin(), sizes.end()), ShaderPath(), 0u);
        VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commands.queueFamilyIndex = gpu.queueFamily;
        Require(vkCreateCommandPool(gpu.device, &commands, nullptr, &gpu.commands), "create command pool");
        VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocation.commandPool = gpu.commands; commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandAllocation.commandBufferCount = 1;
        VkCommandBuffer command;
        Require(vkAllocateCommandBuffers(gpu.device, &commandAllocation, &command), "allocate command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Require(vkBeginCommandBuffer(command, &begin), "begin commands");
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(command, uint32_t((queries.size() + 63u) / 64u), 1, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        Require(vkEndCommandBuffer(command), "end commands");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        Require(vkQueueSubmit(gpu.queue, 1, &submit, VK_NULL_HANDLE), "submit queries");
        Require(vkQueueWaitIdle(gpu.queue), "wait queries");
        void* mapped;
        Require(vkMapMemory(gpu.device, gpu.memories[2], 0, sizes[2], 0, &mapped), "map results");

        std::vector<QueryResult> result(queries.size());
        std::memcpy(result.data(), mapped, size_t(sizes[2]));
        vkUnmapMemory(gpu.device, gpu.memories[2]);
        return result;
    }
    // 独立几何判定：直接检查所有叶盒，不调用 CPU 或 GPU 的树寻址函数。
    // 两侧共用由整数格点生成的世界边界，避免 lo + spacing 的二次舍入产生缝隙。
    uint32_t Oracle(const VansGIProbeLayout& layout, uint32_t regionIndex, glm::vec3 point, float minimumSpacing)
    {
        if (regionIndex >= layout.Regions().size() || glm::any(glm::isnan(point)) || glm::any(glm::isinf(point))) return GIInvalidAddress;
        const auto& region = layout.Regions()[regionIndex];
        const glm::vec3 origin(region.volumeMinAndRootSpacing), maximum = origin + glm::vec3(region.volumeSizeAndBias);
        if (glm::any(glm::lessThan(point, origin)) || glm::any(glm::greaterThan(point, maximum))) return GIInvalidAddress;
        for (uint32_t i = 0; i < layout.Leaves().size(); ++i)
        {
            const auto& leaf = layout.Leaves()[i];
            if (leaf.metadata.x != regionIndex) continue;
            bool inside = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                const auto lattice = std::llround((double(leaf.minimumAndSpacing[axis]) - origin[axis]) / minimumSpacing);
                const auto units = int64_t(1) << (region.metadata.x - leaf.metadata.y);
                const float upper = float(double(origin[axis]) + double(lattice + units) * minimumSpacing);
                inside &= point[axis] >= leaf.minimumAndSpacing[axis] &&
                    (point[axis] < upper || (point[axis] == maximum[axis] && point[axis] <= upper));
            }
            if (inside) return i;
        }
        return GIInvalidAddress;
    }
    uint32_t SelectRegion(const std::vector<GIResolvedRegion>& regions, glm::vec3 point)
    {
        if (glm::any(glm::isnan(point)) || glm::any(glm::isinf(point))) return GIInvalidAddress;
        uint32_t best = GIInvalidAddress;
        float bestPriority = -std::numeric_limits<float>::max(), bestWeight = -1;
        for (uint32_t i = 0; i < regions.size(); ++i)
        {
            const auto& r = regions[i];
            if (glm::any(glm::lessThan(point, r.volumeMin)) || glm::any(glm::greaterThan(point, r.volumeMin + r.volumeSize))) continue;
            const auto edge = glm::min(point - r.volumeMin, r.volumeMin + r.volumeSize - point);
            const float distance = std::min(edge.x, std::min(edge.y, edge.z));
            const float weight = r.volumeFadeDistance > 0 ? glm::clamp(distance / r.volumeFadeDistance, 0.0f, 1.0f) : 1.0f;
            if (r.priority > bestPriority || (r.priority == bestPriority && weight > bestWeight))
            { best = i; bestPriority = r.priority; bestWeight = weight; }
        }
        return best;
    }
    struct SamplingAtlas
    {
        uint32_t tile = 0, width = 0, height = 0, channels = 0;
        std::vector<glm::vec4> texels;
        std::vector<uint16_t> halfTexels;
        SamplingAtlas(uint32_t count, bool visibility)
        {
            tile = visibility ? 16u : 8u; channels = visibility ? 2u : 4u;
            width = 32u * tile; height = ((count + 31u) / 32u) * tile;
            texels.resize(size_t(width) * height, glm::vec4(0));
            halfTexels.resize(texels.size() * channels, 0u);
            for (uint32_t probe = 0; probe < count; ++probe)
            for (uint32_t y = 0; y < tile; ++y) for (uint32_t x = 0; x < tile; ++x)
            {
                const float u = (float(x) - 1.0f) / float(tile - 3u), v = (float(y) - 1.0f) / float(tile - 3u);
                glm::vec4 value;
                if (visibility)
                {
                    const float mean = 0.06f + float(probe % 9u) * 0.07f + u * 0.03f + v * 0.02f;
                    value = {mean, mean * mean + 0.012f + float(probe % 5u) * 0.011f, 0, 0};
                }
                else value = {0.2f + float(probe % 13u) * 0.09f + u * 0.16f,
                    0.25f + float(probe % 7u) * 0.13f + v * 0.21f, 0.5f + float(probe % 5u) * 0.11f + u * v * 0.08f,
                    probe % 17u ? 0.3f + float(probe % 4u) * 0.17f + u * 0.05f : 0.0f};
                const size_t index = size_t((probe / 32u) * tile + y) * width + (probe % 32u) * tile + x;
                for (uint32_t channel = 0; channel < channels; ++channel)
                {
                    const auto half = glm::packHalf1x16(value[channel]);
                    halfTexels[index * channels + channel] = half;
                    texels[index][channel] = glm::unpackHalf1x16(half);
                }
            }
        }
        glm::dvec4 Sample(uint32_t probe, glm::dvec3 direction) const
        {
            direction /= std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
            glm::dvec2 oct(direction.x, direction.z);
            if (direction.y < 0.0) oct = glm::dvec2((1.0 - std::abs(oct.y)) * (oct.x >= 0 ? 1.0 : -1.0),
                (1.0 - std::abs(oct.x)) * (oct.y >= 0 ? 1.0 : -1.0));
            const auto coordinate = (oct * 0.5 + 0.5) * double(tile - 2u) + 0.5;
            const glm::uvec2 lower(glm::floor(coordinate)); const auto fraction = coordinate - glm::floor(coordinate);
            glm::dvec4 result(0);
            for (uint32_t y = 0; y < 2; ++y) for (uint32_t x = 0; x < 2; ++x)
            {
                const size_t index = size_t((probe / 32u) * tile + lower.y + y) * width + (probe % 32u) * tile + lower.x + x;
                result += glm::dvec4(texels[index]) * (x ? fraction.x : 1.0 - fraction.x) * (y ? fraction.y : 1.0 - fraction.y);
            }
            return result;
        }
    };
    struct alignas(16) SamplingState { glm::vec4 relocation{0}, distance{0}; glm::uvec4 metadata{0}; };
    struct alignas(16) SamplingQuery { glm::vec4 positionAndWeight, normalAndBias; };
    static_assert(sizeof(SamplingState) == 48 && sizeof(SamplingQuery) == 32);
    void CreateSkyCube(GPU& gpu, uint32_t slot)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = slot == 0 ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;
        info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        info.extent = {1, 1, 1}; info.mipLevels = 1; info.arrayLayers = 6;
        info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        Require(vkCreateImage(gpu.device, &info, nullptr, &gpu.images[slot]), "create sampling atlas");
        VkMemoryRequirements required; vkGetImageMemoryRequirements(gpu.device, gpu.images[slot], &required);
        VkPhysicalDeviceMemoryProperties properties; vkGetPhysicalDeviceMemoryProperties(gpu.physical, &properties);
        uint32_t memoryType = ~0u;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((required.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            { memoryType = i; break; }
        if (memoryType == ~0u) throw std::runtime_error("No device-local atlas memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = required.size; allocation.memoryTypeIndex = memoryType;
        Require(vkAllocateMemory(gpu.device, &allocation, nullptr, &gpu.imageMemories[slot]), "allocate atlas memory");
        Require(vkBindImageMemory(gpu.device, gpu.images[slot], gpu.imageMemories[slot], 0), "bind atlas memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = gpu.images[slot]; view.viewType = VK_IMAGE_VIEW_TYPE_CUBE; view.format = info.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
        Require(vkCreateImageView(gpu.device, &view, nullptr, &gpu.imageViews[slot]), "create atlas view");
    }
    void CreateSamplingImage(GPU& gpu, uint32_t slot, const SamplingAtlas& atlas)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = atlas.channels == 4u ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R16G16_SFLOAT;
        info.extent = {atlas.width, atlas.height, 1}; info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        Require(vkCreateImage(gpu.device, &info, nullptr, &gpu.images[slot]), "create sampling atlas");
        VkMemoryRequirements required; vkGetImageMemoryRequirements(gpu.device, gpu.images[slot], &required);
        VkPhysicalDeviceMemoryProperties properties; vkGetPhysicalDeviceMemoryProperties(gpu.physical, &properties);
        uint32_t memoryType = ~0u;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((required.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            { memoryType = i; break; }
        if (memoryType == ~0u) throw std::runtime_error("No device-local atlas memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = required.size; allocation.memoryTypeIndex = memoryType;
        Require(vkAllocateMemory(gpu.device, &allocation, nullptr, &gpu.imageMemories[slot]), "allocate atlas memory");
        Require(vkBindImageMemory(gpu.device, gpu.images[slot], gpu.imageMemories[slot], 0), "bind atlas memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = gpu.images[slot]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Require(vkCreateImageView(gpu.device, &view, nullptr, &gpu.imageViews[slot]), "create atlas view");
        gpu.Buffer(4u + slot, atlas.halfTexels.size() * sizeof(uint16_t), atlas.halfTexels.data());
    }
    std::vector<glm::vec4> DispatchSampling(const VansGIProbeLayout& layout, const GIResolvedRegion& region,
        const std::vector<SamplingQuery>& queries, const std::vector<SamplingState>& states, const SamplingAtlas& irradiance, const SamplingAtlas& visibility, int mode = 0)
    {
        GPU gpu; InitializeGPU(gpu);
        const auto packet = BuildGIProbeLayoutGPUData({region}, &layout);
        const std::vector<VkDeviceSize> sizes = {packet.size() * 16u, queries.size() * sizeof(SamplingQuery),
            queries.size() * sizeof(glm::vec4), states.size() * sizeof(SamplingState)};
        gpu.Buffer(0, sizes[0], packet.data()); gpu.Buffer(1, sizes[1], queries.data());
        gpu.Buffer(2, sizes[2], nullptr); gpu.Buffer(3, sizes[3], states.data());
        CreateSamplingImage(gpu, 0, irradiance); CreateSamplingImage(gpu, 1, visibility);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR; sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Require(vkCreateSampler(gpu.device, &sampler, nullptr, &gpu.sampler), "create atlas sampler");
        const std::vector<VkDescriptorImageInfo> textures = {
            {gpu.sampler, gpu.imageViews[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gpu.sampler, gpu.imageViews[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkDescriptorSet set;
        CreateCompute(gpu, set, sizes, ShaderPath().parent_path() / "GIProbeSamplingContractcomp.spv", 16u, textures);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = gpu.queueFamily;
        Require(vkCreateCommandPool(gpu.device, &pool, nullptr, &gpu.commands), "create sampling command pool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = gpu.commands; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        VkCommandBuffer command; Require(vkAllocateCommandBuffers(gpu.device, &allocate, &command), "allocate sampling commands");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Require(vkBeginCommandBuffer(command, &begin), "begin sampling commands");
        for (uint32_t slot = 0; slot < 2; ++slot)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.image = gpu.images[slot]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            const auto& atlas = slot ? visibility : irradiance;
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {atlas.width, atlas.height, 1};
            vkCmdCopyBufferToImage(command, gpu.buffers[4u + slot], gpu.images[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipelineLayout, 0, 1, &set, 0, nullptr);
        const glm::ivec4 dimensions(32, int(irradiance.height / irradiance.tile), mode, 0);
        vkCmdPushConstants(command, gpu.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dimensions), &dimensions);
        vkCmdDispatch(command, uint32_t((queries.size() + 63u) / 64u), 1, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        Require(vkEndCommandBuffer(command), "end sampling commands");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        Require(vkQueueSubmit(gpu.queue, 1, &submit, VK_NULL_HANDLE), "submit sampling");
        Require(vkQueueWaitIdle(gpu.queue), "wait sampling");
        void* mapped; Require(vkMapMemory(gpu.device, gpu.memories[2], 0, sizes[2], 0, &mapped), "map sampling result");
        std::vector<glm::vec4> results(queries.size()); std::memcpy(results.data(), mapped, sizes[2]);
        vkUnmapMemory(gpu.device, gpu.memories[2]); return results;
    }
    glm::dvec4 SamplingReference(const VansGIProbeLayout& layout, const SamplingQuery& query, float minimumSpacing,
        const std::vector<SamplingState>& states, const SamplingAtlas& irradiance, const SamplingAtlas& visibility, bool applyStencil)
    {
        const glm::vec3 world(query.positionAndWeight), normal(query.normalAndBias);
        if (glm::any(glm::isnan(world)) || glm::any(glm::isinf(world)) || glm::any(glm::isnan(normal))) return glm::dvec4(0);
        const auto& region = layout.Regions()[0]; const glm::vec3 minimum(region.volumeMinAndRootSpacing);
        const auto maximum = minimum + glm::vec3(region.volumeSizeAndBias);
        if (glm::any(glm::lessThan(world, minimum)) || glm::any(glm::greaterThan(world, maximum))) return glm::dvec4(0);
        const auto sample = glm::clamp(world + normal * (std::max)(query.normalAndBias.w, 0.0f), minimum, maximum);
        const auto leafIndex = Oracle(layout, 0, sample, minimumSpacing);
        const GIProbeLayoutCell* selected = leafIndex == GIInvalidAddress ? nullptr : &layout.Leaves()[leafIndex];
        const auto hasSources = [&](const GIProbeLayoutCell& cell, bool published)
        {
            const auto fraction = glm::clamp((glm::dvec3(sample) - glm::dvec3(cell.minimumAndSpacing)) / double(cell.minimumAndSpacing.w), glm::dvec3(0), glm::dvec3(1));
            for (uint32_t slot=0; slot<8; ++slot)
            {
                const uint32_t probe = cell.probes[slot];
                if (probe == GIInvalidAddress || (published && states[probe].metadata.x != 1u)) continue;
                double weight = 0;
                for (uint32_t corner=0; corner<8; ++corner)
                {
                    const auto blend = glm::mix(1.0-fraction, fraction, glm::dvec3(corner&1u,(corner>>1u)&1u,(corner>>2u)&1u));
                    weight += blend.x*blend.y*blend.z * (cell.metadata.w == GIInvalidAddress ? double(corner==slot) : layout.Stencils()[cell.metadata.w].weights[slot*8u+corner]);
                }
                if (weight > 0) return true;
            }
            return false;
        };
        if (!selected || !hasSources(*selected, false))
        {
            selected = nullptr;
            for (const auto& cell : layout.ParentCells())
            {
                const auto lo = glm::vec3(cell.minimumAndSpacing), hi = lo + cell.minimumAndSpacing.w;
                if (glm::any(glm::lessThan(sample,lo))) continue;
                bool outside = false;
                for (int axis=0;axis<3;++axis)
                    outside |= sample[axis] >= hi[axis] && !(sample[axis] == maximum[axis] && hi[axis] >= maximum[axis]);
                if (outside || !hasSources(cell,true)) continue;
                if (!selected || cell.minimumAndSpacing.w < selected->minimumAndSpacing.w) selected = &cell;
            }
        }
        if (!selected) return glm::dvec4(0);
        const auto& leaf = *selected;
        const auto fraction = glm::clamp((glm::dvec3(sample) - glm::dvec3(leaf.minimumAndSpacing)) / double(leaf.minimumAndSpacing.w), glm::dvec3(0), glm::dvec3(1));
        glm::dvec3 sum(0); double weightSum = 0;
        for (uint32_t slot = 0; slot < 8; ++slot)
        {
            const auto probe = leaf.probes[slot];
            if (probe == GIInvalidAddress || states[probe].metadata.x != 1u) continue;
            double weight = 0;
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const auto blend = glm::mix(1.0 - fraction, fraction, glm::dvec3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u));
                const double coefficient = !applyStencil || leaf.metadata.w == GIInvalidAddress ? double(corner == slot) :
                    layout.Stencils()[leaf.metadata.w].weights[slot * 8u + corner];
                weight += blend.x * blend.y * blend.z * coefficient;
            }
            const auto value = irradiance.Sample(probe, glm::dvec3(normal));
            if (weight <= 0.0) continue;
            const auto position = layout.Positions()[probe].positionAndSpacing;
            const auto delta = glm::dvec3(sample) - glm::dvec3(glm::vec3(position) + glm::vec3(states[probe].distance));
            const double distance = glm::length(delta);
            const auto direction = distance > 0.0 ? delta / distance : -glm::dvec3(normal);
            const double facing = std::clamp(glm::dot(-direction, glm::dvec3(normal)) * 0.5 + 0.5, 0.0, 1.0);
            weight *= facing * facing;
            const auto moments = visibility.Sample(probe, direction);
            const double deltaDistance = (std::max)(distance - moments.x, 0.0);
            if (deltaDistance > 0.0)
            {
                const double variance = (std::max)(moments.y - moments.x * moments.x,
                    (std::max)(moments.y, moments.x * moments.x) * 1e-6);
                const double probability = variance / (variance + deltaDistance * deltaDistance);
                weight *= probability * probability;
            }
            sum += glm::max(glm::dvec3(value), glm::dvec3(0)) * weight; weightSum += weight;
        }
        return glm::dvec4(weightSum > 0.0 ? sum / weightSum * double(query.positionAndWeight.w) * 0.31830988618 : glm::dvec3(0), weightSum);
    }
    void RunParentFallbackCase()
    {
        GIProbeRegionDesc description; description.center = {0,-8,0}; description.gridDimensions = {4,2,4};
        description.overrideGridDimensions = true; description.probeSpacing = 4;
        const auto region = ResolveGIRegion(description);
        VansSceneGeometrySnapshot geometry;
        geometry.opaque.Build({
            {{-32,-9.25f,-32},{-32,-9.25f,32},{32,-9.25f,32}},
            {{-32,-9.25f,-32},{32,-9.25f,32},{32,-9.25f,-32}},
            {{.75f,-15,-32},{.75f,4,32},{.75f,-15,32},{1,0,0}},
            {{.75f,-15,-32},{.75f,4,-32},{.75f,4,32},{1,0,0}}});
        GIProbePlacementSettings settings; settings.enabled = true;
        VansGIProbeLayout layout; std::string error;
        if (!layout.Build({region}, settings, geometry, error)) throw std::runtime_error(error);
        const uint32_t count = uint32_t(layout.Positions().size());
        SamplingAtlas irradiance(count,false), visibility(count,true);
        std::vector<SamplingState> states(count);
        const auto tile = [](SamplingAtlas& atlas, uint32_t probe, glm::vec4 value)
        {
            for (uint32_t y=0;y<atlas.tile;++y) for (uint32_t x=0;x<atlas.tile;++x)
            {
                const size_t index = size_t((probe/32u)*atlas.tile+y)*atlas.width+(probe%32u)*atlas.tile+x;
                for (uint32_t channel=0;channel<atlas.channels;++channel)
                { const auto half = glm::packHalf1x16(value[channel]); atlas.halfTexels[index*atlas.channels+channel]=half;
                  atlas.texels[index][channel]=glm::unpackHalf1x16(half); }
            }
        };
        for (uint32_t i=0;i<count;++i)
        {
            const auto& p=layout.Positions()[i]; states[i].metadata.x=1;
            tile(irradiance,i,p.metadata.z ? glm::vec4(p.positionAndSpacing.w,1,0,1) : glm::vec4(0,0,2,1));
            tile(visibility,i,{100,10000,0,0});
        }
        glm::vec3 miss(0); uint32_t nearest=GIInvalidAddress, ancestor=GIInvalidAddress;
        for (uint32_t n=0;n<layout.Nodes().size();++n)
        {
            const auto& node=layout.Nodes()[n];
            if (node.IsBranch() || node.LeafAddress()!=GIInvalidAddress || node.lighting.x==GIInvalidAddress) continue;
            uint32_t parent=node.lighting.y;
            while (parent!=GIInvalidAddress && layout.Nodes()[parent].lighting.x==GIInvalidAddress) parent=layout.Nodes()[parent].lighting.y;
            if (parent==GIInvalidAddress) continue;
            nearest=node.lighting.x; ancestor=layout.Nodes()[parent].lighting.x;
            const auto& c=layout.ParentCells()[nearest]; miss=glm::vec3(c.minimumAndSpacing)+c.minimumAndSpacing.w*.5f;
            if (std::any_of(c.probes.begin(),c.probes.end(),[](uint32_t p){return p!=GIInvalidAddress;})) break;
            nearest=GIInvalidAddress;
        }
        if (nearest==GIInvalidAddress) throw std::runtime_error("Parent fallback fixture has no nested empty cell");
        glm::vec3 hit(0); bool foundHit=false;
        for (const auto& cell:layout.Leaves())
            if (std::any_of(cell.probes.begin(),cell.probes.end(),[](uint32_t p){return p!=GIInvalidAddress;}))
            { hit=glm::vec3(cell.minimumAndSpacing)+cell.minimumAndSpacing.w*.5f;foundHit=true;break; }
        if (!foundHit || layout.LocateLeaf(0,miss)!=GIInvalidAddress) throw std::runtime_error("Invalid parent miss fixture");
        const std::vector<SamplingQuery> queries={
            {glm::vec4(miss,1),{0,1,0,0}}, {glm::vec4(hit,1),{0,1,0,0}},
            {glm::vec4(region.volumeMin-1.0f,1),{0,1,0,0}}};
        const auto expect = [](glm::vec4 value,glm::vec3 radiance,const char* message)
        { if (glm::length(glm::vec3(value)-radiance*.31830988618f)>.0001f) throw std::runtime_error(message); };
        auto result=DispatchSampling(layout,region,queries,states,irradiance,visibility);
        expect(result[0],{layout.ParentCells()[nearest].minimumAndSpacing.w,1,0},"miss did not select nearest independently stored parent");
        expect(result[1],{0,0,2},"parent replaced an existing leaf hit");
        expect(result[2],{0,0,0},"parent lookup escaped the GI volume");
        for (uint32_t p:layout.ParentCells()[nearest].probes) if(p!=GIInvalidAddress) states[p].metadata.x=0;
        result=DispatchSampling(layout,region,queries,states,irradiance,visibility);
        expect(result[0],{layout.ParentCells()[ancestor].minimumAndSpacing.w,1,0},"unpublished parent did not ascend to ready ancestor");
        for (uint32_t i=0;i<count;++i) states[i].metadata.x=layout.Positions()[i].metadata.z ? 0u:1u;
        result=DispatchSampling(layout,region,queries,states,irradiance,visibility);
        expect(result[0],{0,0,0},"exhausted parent search did not preserve the old miss result");
        for (uint32_t i=0;i<count;++i)
        { states[i].metadata.x=1u; if (!layout.Positions()[i].metadata.z) tile(irradiance,i,{0,0,0,1}); }
        result=DispatchSampling(layout,region,queries,states,irradiance,visibility);
        expect(result[1],{0,0,0},"valid black leaf incorrectly fell back to a bright parent");
        if (result[1].w<=0) throw std::runtime_error("Black leaf fixture has no valid support");
        for (uint32_t i=0;i<count;++i) if (!layout.Positions()[i].metadata.z) states[i].metadata.x=0;
        result=DispatchSampling(layout,region,queries,states,irradiance,visibility);
        if (result[1]!=glm::vec4(0)) throw std::runtime_error("Existing unpublished leaf incorrectly used a parent");
        for (uint32_t i=0;i<count;++i) if (!layout.Positions()[i].metadata.z)
        { states[i].metadata.x=1; tile(irradiance,i,{0,0,2,1}); tile(visibility,i,{0,0,0,0}); }
        result=DispatchSampling(layout,region,queries,states,irradiance,visibility);
        if (result[1]!=glm::vec4(0)) throw std::runtime_error("Fully occluded leaf bypassed visibility through a parent");
        std::cout << "[GIParentFallbackGPU] PASS: nearest real parent, ready ancestor, exhausted miss, outside volume, original leaf hit, valid black, unpublished leaf and fully occluded leaf\n";
    }
    bool RunSamplingCase(const char* label, float spacing, glm::vec3 offset)
    {
        GIProbeRegionDesc description; description.center = offset + glm::vec3(0,-8,0);
        description.gridDimensions = {4,2,4}; description.overrideGridDimensions = true; description.probeSpacing = 4;
        const auto region = ResolveGIRegion(description);
        std::vector<VansGeometryTriangle> triangles = {
            {{-32,-9.25f,-32},{-32,-9.25f,32},{32,-9.25f,32}},
            {{-32,-9.25f,-32},{32,-9.25f,32},{32,-9.25f,-32}},
            {{0.75f,-15,-32},{0.75f,4,32},{0.75f,-15,32},{1,0,0}},
            {{0.75f,-15,-32},{0.75f,4,-32},{0.75f,4,32},{1,0,0}}};
        for (auto& t : triangles) { t.a += offset; t.b += offset; t.c += offset; }
        VansSceneGeometrySnapshot geometry; geometry.opaque.Build(std::move(triangles));
        GIProbePlacementSettings settings; settings.enabled = true; settings.minProbeSpacing = spacing; settings.maxProbeCount = 4096;
        VansGIProbeLayout layout; std::string error;
        if (!layout.Build({region}, settings, geometry, error)) throw std::runtime_error(error);
        const uint32_t probeCount = uint32_t(layout.Positions().size());
        SamplingAtlas irradiance(probeCount, false), visibility(probeCount, true);
        std::vector<SamplingState> states(probeCount);
        for (uint32_t i = 0; i < probeCount; ++i)
        {
            const float reach = layout.Positions()[i].positionAndSpacing.w * 0.04f;
            states[i].relocation = glm::vec4(glm::vec3(int(i % 3u) - 1, int((i / 3u) % 3u) - 1, int((i / 9u) % 3u) - 1) * reach,
                i % 19u ? 0.15f + float(i % 7u) * 0.12f : 0.0f);
            states[i].distance = glm::vec4(glm::vec3(states[i].relocation),0.0f);
            states[i].metadata = glm::uvec4(i % 11u ? 1u : 0u, 0, 0, 0);
        }
        // 恒定距离矩的解析夹具：沿接收距离连续扫描，不能在偏置阈值处突然切换。
        // 此性质独立于 CPU 镜像公式，旧公式即使与 CPU 完全相同也必须失败。
        std::vector<SamplingQuery> visibilityQueries;
        for (float scale : {0.1f, 1.0f, 10.0f})
            for (uint32_t i = 0; i <= 20000u; ++i)
                visibilityQueries.push_back({glm::vec4(scale, 1.005f * scale * scale,
                    (0.5f + float(i) * 0.0001f) * scale, 0.5f * scale), glm::vec4(0, 1, 0, .25f * scale)});
        const auto visibilityResults = DispatchSampling(layout, region, visibilityQueries, states, irradiance, visibility, 1);
        float maximumJump = 0.0f;
        for (size_t i = 0; i < visibilityResults.size(); ++i)
        {
            const float value = visibilityResults[i].x;
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                throw std::runtime_error("DDGI visibility outside probability range");
            if (i % 20001u)
            {
                if (value > visibilityResults[i - 1].x + 1e-6f)
                    throw std::runtime_error("DDGI visibility is not monotone in receiver distance");
                maximumJump = (std::max)(maximumJump, std::abs(value - visibilityResults[i - 1].x));
            }
        }
        std::cout << "[GIProbeVisibilityGPU] maxAdjacentJump=" << maximumJump << " step=0.0001*scale\n";
        if (maximumJump > 0.003f) throw std::runtime_error("DDGI visibility has a hard distance cutoff");
        for (size_t i = 0; i < 20001u; ++i)
            if (std::abs(visibilityResults[i].x - visibilityResults[i + 20001u].x) > 0.001f ||
                std::abs(visibilityResults[i].x - visibilityResults[i + 40002u].x) > 0.001f)
                throw std::runtime_error("DDGI visibility changes under uniform scene scaling");
        std::vector<SamplingQuery> queries;
        std::vector<size_t> pairs;
        const auto add = [&](glm::vec3 sample, uint32_t variant)
        {
            const auto normal = glm::normalize(glm::vec3(variant % 3u ? 0.31f : 0.77f, 0.91f, -0.27f));
            const float bias = variant % 2u ? 0.1f : 0.25f;
            queries.push_back({glm::vec4(sample - normal * (std::max)(bias, 0.0f), variant % 2u ? 0.73f : 0.91f), glm::vec4(normal, bias)});
        };
        const uint32_t stride = (std::max)(1u, uint32_t(layout.Leaves().size() / 128u));
        for (uint32_t i = 0; i < layout.Leaves().size(); i += stride)
        {
            const auto& leaf = layout.Leaves()[i];
            add(glm::vec3(leaf.minimumAndSpacing) + leaf.minimumAndSpacing.w * 0.5f, i);
            for (uint32_t face = 0; face < 6; ++face)
            {
                if (layout.CoarseNeighbors()[i][face] == GIInvalidAddress) continue;
                auto point = glm::vec3(leaf.minimumAndSpacing) + leaf.minimumAndSpacing.w * 0.5f;
                point[face / 2u] = leaf.minimumAndSpacing[face / 2u] + ((face & 1u) ? leaf.minimumAndSpacing.w : 0.0f);
                pairs.push_back(queries.size());
                auto lower = point, upper = point;
                lower[face / 2u] -= spacing * 0.0001f; upper[face / 2u] += spacing * 0.0001f;
                add(lower, i); add(point, i); add(upper, i);
            }
        }
        queries.push_back({glm::vec4(region.volumeMin - 1.0f, 1), glm::vec4(0,1,0,0.25f)});
        queries.push_back({glm::vec4(NAN,0,0,1), glm::vec4(0,1,0,0.25f)});
        const auto results = DispatchSampling(layout, region, queries, states, irradiance, visibility);
        // 改变尚未追踪的目标偏移，不应改变已经发布图集的查询结果。
        for(auto& state:states) state.relocation += glm::vec4(spacing*.2f,spacing*.1f,-spacing*.2f,0.0f);
        const auto pendingResults = DispatchSampling(layout, region, queries, states, irradiance, visibility);
        for(size_t i=0;i<results.size();++i)
            if(glm::length(results[i]-pendingResults[i])>1e-6f)
                throw std::runtime_error("pending relocation changed the published irradiance query");
        SamplingAtlas constantIrradiance(probeCount, false), lowVisibility(probeCount, true);
        const auto fillAtlas = [](SamplingAtlas& atlas, glm::vec4 value)
        {
            for (size_t texel = 0; texel < atlas.texels.size(); ++texel)
                for (uint32_t channel = 0; channel < atlas.channels; ++channel)
                {
                    auto half = glm::packHalf1x16(value[channel]);
                    atlas.halfTexels[texel * atlas.channels + channel] = half;
                    atlas.texels[texel][channel] = glm::unpackHalf1x16(half);
                }
        };
        fillAtlas(constantIrradiance, {1, 2, .5f, 1});
        fillAtlas(lowVisibility, {.001f, .00000101f, 0, 0});
        const auto constantResults = DispatchSampling(layout, region, queries, states, constantIrradiance, lowVisibility);
        uint32_t smallWeightChecks = 0;
        for (size_t i = 0; i < constantResults.size(); ++i)
        {
            const auto& result = constantResults[i];
            if (result.w <= 0.0f) continue;
            const auto expectedConstant = glm::vec3(1, 2, .5f) * queries[i].positionAndWeight.w * 0.31830988618f;
            if (glm::length(glm::vec3(result) - expectedConstant) > .0001f)
                throw std::runtime_error("Small DDGI visibility introduced a black normalization cutoff");
            if (result.w < 1e-5f) ++smallWeightChecks;
        }
        if (smallWeightChecks < 10u) throw std::runtime_error("Low-visibility fixture has insufficient coverage");
        std::cout << "[GIProbeSamplingGPU] constant energy with tiny weights checks=" << smallWeightChecks << '\n';
        uint32_t mismatches = 0, sensitive = 0, lit = 0, dark = 0, boundaryChecks = 0;
        double maximumError = 0, maximumBoundaryError = 0;
        std::vector<glm::dvec4> expected;
        for (size_t i = 0; i < queries.size(); ++i)
        {
            expected.push_back(SamplingReference(layout, queries[i], spacing, states, irradiance, visibility, true));
            const auto alternate = SamplingReference(layout, queries[i], spacing, states, irradiance, visibility, false);
            sensitive += glm::length(expected.back() - alternate) > 0.01;
            lit += glm::length(glm::dvec3(expected.back())) > 0.01; dark += glm::length(glm::dvec3(expected.back())) == 0;
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                const double difference = std::abs(double(results[i][channel]) - expected.back()[channel]);
                maximumError = (std::max)(maximumError, difference);
                if (!std::isfinite(results[i][channel]) || difference > 0.003)
                {
                    if (mismatches++ < 8) std::cerr << "[GIProbeSamplingGPU] mismatch query=" << i << " channel=" << channel
                        << " GPU=" << results[i][channel] << " CPU=" << expected.back()[channel] << '\n';
                }
            }
        }
        for (size_t first : pairs)
        {
            const auto sampleLeaf = [&](size_t index)
            {
                const auto& query = queries[index];
                const auto point = glm::clamp(glm::vec3(query.positionAndWeight) + glm::vec3(query.normalAndBias) *
                    (std::max)(query.normalAndBias.w, 0.0f), region.volumeMin, region.volumeMin + region.volumeSize);
                return Oracle(layout, 0, point, spacing);
            };
            const auto lowerLeaf = sampleLeaf(first), upperLeaf = sampleLeaf(first + 2);
            if (lowerLeaf == GIInvalidAddress || upperLeaf == GIInvalidAddress || lowerLeaf == upperLeaf) continue;
            // 这里验证跨层 stencil；独立的连续距离扫描另行约束 visibility 公式。
            const auto oracleChange = expected[first + 2] - expected[first];
            const double error = glm::length(glm::dvec4(results[first + 2] - results[first]) - oracleChange);
            maximumBoundaryError = (std::max)(maximumBoundaryError, error);
            mismatches += error > 0.004; ++boundaryChecks;
        }
        if (sensitive < 10u || lit < 10u || !dark || boundaryChecks < 10u) ++mismatches;
        std::cout << "[GIProbeSamplingGPU] " << (mismatches || validationErrors ? "FAIL " : "PASS ") << label
            << " queries=" << queries.size() << " probes=" << probeCount << " lit=" << lit << " dark=" << dark
            << " stencilSensitive=" << sensitive << " boundaryChecks=" << boundaryChecks << " maxError=" << maximumError
            << " maxBoundaryError=" << maximumBoundaryError << " mismatches=" << mismatches << " validationErrors=" << validationErrors << '\n';
        return mismatches == 0 && validationErrors == 0;
    }
    bool RunCase(const char* label, float spacing, glm::vec3 offset, bool sparse)
    {
        // 不要求设备保留 subnormal；零点附近使用正常浮点数，其余边界严格使用相邻 ULP。
        const auto adjacentFloat = [spacing](float value, float towards)
        { return value == 0.0f ? std::copysign(spacing * std::numeric_limits<float>::epsilon(), towards) : std::nextafter(value, towards); };
        std::vector<GIResolvedRegion> regions;
        for (uint32_t i = 0; i < 3; ++i)
        {
            GIProbeRegionDesc r; r.center = offset + glm::vec3(i == 1 ? 4.5f : 0, -8, i == 2 ? 3.125f : 0);
            r.stableId = 40 + i; r.probeSpacing = 4; r.gridDimensions = {4,2,4}; r.overrideGridDimensions = true;
            r.priority = i == 2 ? 3.0f : 0; r.volumeFadeDistance = i == 1 ? 0 : 2;
            regions.push_back(ResolveGIRegion(r));
        }
        std::vector<VansGeometryTriangle> triangles = {
            {{-32,-9.25f,-32},{-32,-9.25f,32},{32,-9.25f,32}},
            {{-32,-9.25f,-32},{32,-9.25f,32},{32,-9.25f,-32}},
            {{0.75f,-15,-32},{0.75f,4,32},{0.75f,-15,32},{1,0,0}},
            {{0.75f,-15,-32},{0.75f,4,-32},{0.75f,4,32},{1,0,0}}};
        for (auto& t : triangles) { t.a += offset; t.b += offset; t.c += offset; }
        VansSceneGeometrySnapshot geometry; geometry.opaque.Build(std::move(triangles));
        GIProbePlacementSettings settings; settings.enabled = true; settings.minProbeSpacing = spacing; settings.maxProbeCount = 8192;
        VansGIProbeLayout layout; std::string error;
        if (sparse && !layout.Build(regions, settings, geometry, error)) throw std::runtime_error(error);
        const auto packet = BuildGIProbeLayoutGPUData(regions, sparse ? &layout : nullptr);
        std::mt19937 random(20260908); std::uniform_real_distribution<float> unit(-0.05f, 1.05f);
        std::vector<glm::vec4> queries;
        for (uint32_t r = 0; r < regions.size(); ++r)
        {
            for (uint32_t n = 0; n < 2048; ++n)
                queries.emplace_back(regions[r].volumeMin + glm::vec3(unit(random), unit(random), unit(random)) * regions[r].volumeSize, float(r));
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 p = regions[r].volumeMin + glm::vec3(corner&1u, (corner>>1u)&1u, (corner>>2u)&1u) * regions[r].volumeSize;
                queries.emplace_back(p, float(r));
                for (int axis = 0; axis < 3; ++axis) for (float towards : {-INFINITY, INFINITY})
                { auto q = p; q[axis] = adjacentFloat(q[axis], towards); queries.emplace_back(q, float(r)); }
            }
        }
        for (const auto& leaf : layout.Leaves())
        {
            const auto p = glm::vec3(leaf.minimumAndSpacing) + glm::vec3(leaf.minimumAndSpacing.w * 0.5f);
            queries.emplace_back(p, float(leaf.metadata.x));
            for (int axis = 0; axis < 3; ++axis)
            {
                auto q = p; q[axis] = leaf.minimumAndSpacing[axis]; queries.emplace_back(q, float(leaf.metadata.x));
                for (float towards : {-INFINITY, INFINITY})
                { auto adjacent = q; adjacent[axis] = adjacentFloat(adjacent[axis], towards); queries.emplace_back(adjacent, float(leaf.metadata.x)); }
            }
        }
        std::vector<size_t> transitionPairs;
        for (uint32_t leafIndex = 0; leafIndex < layout.Leaves().size(); ++leafIndex)
        for (uint32_t face = 0; face < 6; ++face)
        {
            if (layout.CoarseNeighbors()[leafIndex][face] == GIInvalidAddress) continue;
            const auto& leaf = layout.Leaves()[leafIndex]; const uint32_t r = leaf.metadata.x;
            const auto origin = regions[r].volumeMin; glm::vec3 p = glm::vec3(leaf.minimumAndSpacing) + leaf.minimumAndSpacing.w * 0.5f;
            const uint32_t axis = face / 2u;
            const float units = float(std::llround((double(leaf.minimumAndSpacing[axis]) - origin[axis]) / spacing)) +
                ((face & 1u) ? float(std::llround(double(leaf.minimumAndSpacing.w) / spacing)) : 0.0f);
            p[axis] = std::fma(units, spacing, origin[axis]);
            while (queries.size() % 3u) queries.emplace_back(p, float(r));
            transitionPairs.push_back(queries.size());
            auto lower = p, upper = p;
            lower[axis] = adjacentFloat(p[axis], -INFINITY); upper[axis] = adjacentFloat(p[axis], INFINITY);
            queries.emplace_back(lower, float(r)); queries.emplace_back(p, float(r)); queries.emplace_back(upper, float(r));
        }
        queries.emplace_back(NAN, 0, 0, 0);
        queries.emplace_back(0, INFINITY, 0, 0);
        queries.emplace_back(0, 0, -INFINITY, 0);
        queries.emplace_back(regions[0].center, 999.0f);
        const auto results = Dispatch(packet, queries);
        uint32_t mismatches = 0, cpuMismatches = 0, invalidLeaves = 0;
        uint64_t checkedCorners = 0, movedToAnotherLeaf = 0, sampleChecks = 0, checkedWeights = 0;
        for (size_t i = 0; i < queries.size(); ++i)
        {
            const auto& result = results[i]; const uint32_t r = uint32_t(queries[i].w);
            const uint32_t leaf = sparse ? Oracle(layout, r, glm::vec3(queries[i]), spacing) : GIInvalidAddress;
            bool match = result.metadata.x == leaf && result.metadata.y == SelectRegion(regions, glm::vec3(queries[i])) && result.metadata.w == uint32_t(sparse);
            glm::vec3 expectedSample(0.0f); uint32_t expectedSampleLeaf = GIInvalidAddress;
            if (sparse && r < regions.size() && !glm::any(glm::isnan(glm::vec3(queries[i]))) && !glm::any(glm::isinf(glm::vec3(queries[i]))))
            {
                const auto& region = regions[r]; const glm::vec3 point(queries[i]);
                const glm::vec3 maximum = region.volumeMin + region.volumeSize;
                if (glm::all(glm::greaterThanEqual(point, region.volumeMin)) && glm::all(glm::lessThanEqual(point, maximum)))
                {
                    glm::vec3 normal(0.0f); normal[(i / 3u) % 3u] = ((i / 9u) & 1u) == 0u ? 1.0f : -1.0f;
                    const float bias = float((i / 18u) % 4u) * 0.25f;
                    // 独立使用设置给定的最小间距，不从 GPU packet 反推期望值。
                    expectedSample = glm::clamp(point + normal * (std::max)(bias, 0.0f), region.volumeMin, maximum);
                    expectedSampleLeaf = Oracle(layout, r, expectedSample, spacing);
                    ++sampleChecks;
                    if (leaf != GIInvalidAddress && expectedSampleLeaf != GIInvalidAddress && leaf != expectedSampleLeaf) ++movedToAnotherLeaf;
                }
            }
            match &= glm::vec3(result.samplePositionAndLeaf) == expectedSample && glm::floatBitsToUint(result.samplePositionAndLeaf.w) == expectedSampleLeaf;
            if (sparse && layout.LocateLeaf(r, glm::vec3(queries[i])) != leaf) ++cpuMismatches;
            if (leaf == GIInvalidAddress) { ++invalidLeaves; match &= result.metadata.z == 0; }
            else if (result.metadata.x == leaf)
            {
                const auto& expected = layout.Leaves()[leaf]; uint32_t valid = 0;
                match &= result.minimumAndSpacing == expected.minimumAndSpacing;
                const auto fraction = glm::clamp((glm::dvec3(queries[i]) - glm::dvec3(expected.minimumAndSpacing)) /
                    double(expected.minimumAndSpacing.w), glm::dvec3(0), glm::dvec3(1));
                for (uint32_t c = 0; c < 8; ++c)
                {
                    double expectedWeight = 0;
                    for (uint32_t corner = 0; corner < 8; ++corner)
                    {
                        const auto blend = glm::mix(1.0 - fraction, fraction,
                            glm::dvec3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u));
                        const double coefficient = expected.metadata.w == GIInvalidAddress ? double(c == corner) :
                            layout.Stencils()[expected.metadata.w].weights[c * 8u + corner];
                        expectedWeight += coefficient * blend.x * blend.y * blend.z;
                    }
                    match &= std::isfinite(result.weights[c]) && std::abs(result.weights[c] - expectedWeight) < 0.000003;
                    ++checkedWeights;
                    const uint32_t global = expected.probes[c];
                    if (global == GIInvalidAddress) { match &= result.corners[c] == GIInvalidAddress; continue; }
                    ++valid; ++checkedCorners;
                    match &= result.corners[c] == global - layout.Regions()[r].metadata.z &&
                        result.positions[c] == layout.Positions()[global].positionAndSpacing;
                }
                match &= result.metadata.z == valid;
            }
            if (!match && mismatches++ < 12)
                std::cerr << std::setprecision(10) << "[GIProbeLayoutGPU] " << label << " query=" << i << " region=" << r
                    << " p=" << queries[i].x << ',' << queries[i].y << ',' << queries[i].z << " expectedLeaf=" << leaf
                    << " actualLeaf=" << result.metadata.x << '\n';
        }
        uint32_t oldDiscontinuities = 0, continuousPairs = 0, continuousWeightPairs = 0;
        double maximumWeightJump = 0.0;
        float maximumOldJump = 0.0f, maximumNewJump = 0.0f;
        for (size_t first : transitionPairs)
        {
            const glm::vec3 a(queries[first]), b(queries[first + 2]);
            const uint32_t r = uint32_t(queries[first].w);
            const auto la = Oracle(layout, r, a, spacing), lb = Oracle(layout, r, b, spacing);
            if (la == GIInvalidAddress || lb == GIInvalidAddress || la == lb) continue;
            glm::vec3 normal(0.0f); normal[(first / 3u) % 3u] = ((first / 9u) & 1u) == 0u ? 1.0f : -1.0f;
            const float bias = float((first / 18u) % 4u) * 0.25f;
            const auto oldPosition = [&](glm::vec3 point, uint32_t leaf) {
                const auto cell = layout.Leaves()[leaf].minimumAndSpacing;
                return glm::clamp(point + normal * (std::max)(bias, cell.w * 0.35f), glm::vec3(cell), glm::vec3(cell) + cell.w);
            };
            const float oldJump = glm::length(oldPosition(a, la) - oldPosition(b, lb));
            const float newJump = glm::length(glm::vec3(results[first].samplePositionAndLeaf) - glm::vec3(results[first + 2].samplePositionAndLeaf));
            const float tolerance = 8.0f * std::numeric_limits<float>::epsilon() * (std::max)(1.0f, glm::length(a));
            if (newJump > glm::length(a - b) + tolerance) ++mismatches;
            maximumOldJump = (std::max)(maximumOldJump, oldJump); maximumNewJump = (std::max)(maximumNewJump, newJump);
            // 两侧查询来自不同叶；比较真实源地址的权重，不仅比较查到的几何位置。
            std::map<uint32_t, double> weights;
            for (uint32_t slot = 0; slot < 8; ++slot)
            {
                if (results[first].corners[slot] != GIInvalidAddress)
                    weights[results[first].corners[slot]] += results[first].weights[slot];
                if (results[first + 2].corners[slot] != GIInvalidAddress)
                    weights[results[first + 2].corners[slot]] -= results[first + 2].weights[slot];
            }
            const double weightTolerance = 0.00001 + glm::length(glm::dvec3(a) - glm::dvec3(b)) / spacing * 4.0;
            for (const auto& [probe, difference] : weights)
            {
                maximumWeightJump = (std::max)(maximumWeightJump, std::abs(difference));
                if (std::abs(difference) > weightTolerance) ++mismatches;
            }
            ++continuousWeightPairs;
            oldDiscontinuities += oldJump > glm::length(a - b) + (std::max)(tolerance, spacing * 0.01f);
            ++continuousPairs;
        }
        if (sparse && (continuousPairs == 0 || oldDiscontinuities == 0)) ++mismatches;
        std::cout << "[GIProbeLayoutGPU] " << (mismatches || validationErrors ? "FAIL " : "PASS ") << label
            << " queries=" << queries.size() << " probes=" << layout.Positions().size() << " leaves=" << layout.Leaves().size()
            << " checkedCorners=" << checkedCorners << " emptyOrOutside=" << invalidLeaves << " mismatches=" << mismatches
            << " sampleChecks=" << sampleChecks << " offsetCrossedLeaves=" << movedToAnotherLeaf
            << " transitionPairs=" << continuousPairs << " oldDiscontinuities=" << oldDiscontinuities
            << " maxOldJump=" << maximumOldJump << " maxNewJump=" << maximumNewJump
            << " checkedWeights=" << checkedWeights << " continuousWeightPairs=" << continuousWeightPairs << " maxWeightJump=" << maximumWeightJump
            << " cpuMismatches=" << cpuMismatches << " validationErrors=" << validationErrors << '\n';
        return mismatches == 0 && cpuMismatches == 0 && validationErrors == 0;
    }
}
namespace
{
    bool RunScrollingGPU()
    {
        VansGIScrollingGrid grid;
        std::string error;
        if (!grid.Initialize({9,10,11}, 0.75f, {-4.1f,-7.7f,2.1f}, error)) throw std::runtime_error(error);
        const std::array<glm::vec3,6> centers = {glm::vec3(-4.1f,-7.7f,2.1f), glm::vec3(-3.1f,-6.7f,1.1f),
            glm::vec3(-8.1f,-10.7f,-0.1f), glm::vec3(70,-99,30), glm::vec3(0.7499f,0,0), glm::vec3(0.7501f,0,0)};
        uint64_t samples = 0, corners = 0;
        float previousFade = 0, fadeJump = 0;
        for (uint32_t frame = 0; frame < centers.size(); ++frame)
        {
            std::vector<uint32_t> entering;
            if (!grid.Move(centers[frame], entering, error)) throw std::runtime_error(error);
            GIResolvedRegion region;
            region.scrolling = true; region.worldOnly = true; region.scrollOffset = grid.RingOffset(); region.blendCenter = grid.Center();
            region.volumeMin = grid.Minimum(); region.volumeSize = glm::vec3(grid.Dimensions())*grid.Spacing();
            region.gridDimensions = grid.Dimensions(); region.probeSpacing = grid.Spacing(); region.probeCount = grid.ProbeCount();
            region.volumeFadeDistance = 0.75f;
            std::vector<glm::vec4> queries;
            for (uint32_t id = 0; id < grid.ProbeCount(); ++id)
            {
                queries.emplace_back(grid.Position(id), 0.0f);
                queries.emplace_back(grid.Position(id) + glm::vec3(.2f,.37f,-.13f), 0.0f);
            }
            queries.emplace_back(-.85f,0,0,0); // 同一个接收点跨越离散滚动边界。
            const auto results = Dispatch(BuildGIProbeLayoutGPUData({region}, nullptr), queries);
            for (size_t query = 0; query < queries.size(); ++query)
            {
                const auto& value = results[query]; const glm::vec3 point(queries[query]);
                const auto expectedSample = glm::clamp(point, grid.Minimum()+0.5f*grid.Spacing(),
                    grid.Minimum()+(glm::vec3(grid.Dimensions())-0.5f)*grid.Spacing());
                const bool inside = glm::all(glm::greaterThanEqual(point,grid.BlendMinimum())) &&
                    glm::all(glm::lessThanEqual(point,grid.BlendMinimum()+grid.BlendSize()));
                if (value.metadata != glm::uvec4(GIInvalidAddress,inside?0u:GIInvalidAddress,8u,2u) ||
                    glm::length(glm::vec3(value.samplePositionAndLeaf)-expectedSample)>0.0001f ||
                    glm::length(glm::vec3(value.minimumAndSpacing)-grid.BlendMinimum())>0.0001f)
                    throw std::runtime_error("GPU scrolling bounds or envelope differ from CPU world grid");
                glm::vec3 reconstructed(0); float weightSum=0;
                for (uint32_t i=0;i<8;++i)
                {
                    const uint32_t id=value.corners[i];
                    if (id>=grid.ProbeCount() || glm::length(glm::vec3(value.positions[i])-grid.Position(id))>0.0001f ||
                        value.positions[i].w>0.0001f || value.weights[i]<0.0f)
                        throw std::runtime_error("GPU receiver candidates and ray origins disagree on recycled physical slots");
                    reconstructed+=glm::vec3(value.positions[i])*value.weights[i]; weightSum+=value.weights[i]; ++corners;
                }
                if (std::abs(weightSum-1.0f)>0.00001f || glm::length(reconstructed-expectedSample)>0.0001f)
                    throw std::runtime_error("GPU scrolling interpolation changed a linear world-space lighting field");
                const auto edge=glm::min(point-grid.BlendMinimum(),grid.BlendMinimum()+grid.BlendSize()-point);
                const float t=glm::clamp(std::min(edge.x,std::min(edge.y,edge.z))/region.volumeFadeDistance,0.0f,1.0f);
                if (std::abs(value.samplePositionAndLeaf.w-t*t*(3.0f-2.0f*t))>0.00001f)
                    throw std::runtime_error("GPU scrolling fade does not follow continuous camera envelope");
                ++samples;
            }
            if (frame==4) previousFade=results.back().samplePositionAndLeaf.w;
            if (frame==5) fadeJump=std::abs(results.back().samplePositionAndLeaf.w-previousFade);
        }
        if (fadeJump>0.001f) throw std::runtime_error("GPU scrolling one cell snaps fade to a new boundary");
        std::cout << "[GIProbeLayoutGPU] scrolling samples=" << samples << " corners=" << corners
            << " singleCellFadeJump=" << fadeJump << " validationErrors=" << validationErrors << '\n';
        return validationErrors==0;
    }
}
bool TestGIProbeLayoutGpuContract()
{
    try
    {
        validationErrors = 0;
        bool passed = RunScrollingGPU();
        passed = RunCase("regular_off", 0.5f, glm::vec3(0), false) && passed;
        passed = RunCase("adaptive_0.5", 0.5f, glm::vec3(0), true) && passed;
        passed = RunCase("adaptive_0.3", 0.3f, glm::vec3(0), true) && passed;
        passed = RunCase("offset_0.125", 0.125f, {1024.25f, -64.5f, -256.375f}, true) && passed;
        return passed;
    }
    catch (const std::exception& error) { std::cerr << "[GIProbeLayoutGPU] FAIL: " << error.what() << '\n'; return false; }
}
bool TestGIProbeSamplingGpuContract()
{
    try
    {
        validationErrors = 0;
        RunParentFallbackCase();
        bool passed = RunSamplingCase("directional_0.5", 0.5f, glm::vec3(0));
        passed = RunSamplingCase("directional_0.3", 0.3f, glm::vec3(0)) && passed;
        passed = RunSamplingCase("directional_offset_0.125", 0.125f, {128.25f,-64.5f,-96.375f}) && passed;
        return passed && validationErrors == 0;
    }
    catch (const std::exception& error) { std::cerr << "[GIProbeSamplingGPU] FAIL: " << error.what() << '\n'; return false; }
}
namespace
{
    uint32_t RegressionHash(uint32_t value)
    {
        value ^= value >> 16u; value *= 0x7feb352du;
        value ^= value >> 15u; value *= 0x846ca68bu;
        return value ^ (value >> 16u);
    }
    glm::dvec3 RegressionFixedDirection(uint32_t probe, uint32_t ray)
    {
        const uint32_t gridSeed = 41u * 73856093u ^ 41u * 19349663u ^ 39u * 83492791u;
        const uint32_t seed = RegressionHash(probe ^ (gridSeed + 0x9e3779b9u + (probe << 6u) + (probe >> 2u)));
        const auto unit = [](uint32_t v) { return double(RegressionHash(v)) / 4294967296.0; };
        const double a = unit(seed), b = unit(seed ^ 0x68bc21ebu), c = unit(seed ^ 0x02e5be93u);
        constexpr double pi2 = 6.28318530718;
        const glm::dvec4 q(std::sqrt(1-a)*std::sin(pi2*b), std::sqrt(1-a)*std::cos(pi2*b),
            std::sqrt(a)*std::sin(pi2*c), std::sqrt(a)*std::cos(pi2*c));
        const double y = 1.0 - (double(ray) + 0.5) / 32.0 * 2.0, r = std::sqrt(1-y*y);
        const double theta = pi2 * double(ray) / 1.61803398875;
        const glm::dvec3 direction(std::cos(theta)*r,y,std::sin(theta)*r);
        const auto t = 2.0 * glm::cross(glm::dvec3(q), direction);
        return glm::normalize(direction + q.w*t + glm::cross(glm::dvec3(q),t));
    }

    // 所有已放置 probe 都必须完成发布，连续校正和背面命中不得撤销光照支持。
    // 命中来自解析射线/平面相交，状态与反馈由正式 GPU shader 计算。
    void RunGIProbePublicationRegression()
    {
        GPU gpu; InitializeGPU(gpu);
        constexpr uint32_t floorCount = 128, workCount = 131, stateCount = 132, rays = 256;
        std::vector<SamplingState> states(stateCount);
        states.back().relocation = {3,4,5,.75f}; states.back().distance = {6,7,8,9};
        states.back().metadata = {1,10,11,12};
        const auto untouched = states.back();
        std::vector<float> hitT(workCount*rays,-1.0f);
        std::vector<glm::uvec2> normals(workCount*rays);
        const auto facing = [](float sign) {
            return glm::uvec2(glm::packHalf2x16({0,1}),glm::packHalf2x16({0,sign}));
        };
        std::vector<glm::uvec4> work(workCount+1u);
        work[0] = {workCount,rays,0,1};
        std::vector<glm::uvec4> layout(9u+stateCount*2u,glm::uvec4(0));
        layout[0] = {1,1,stateCount,0}; layout[1].z=9; layout[7]={0,1,0,stateCount};
        for(uint32_t i=0;i<stateCount;++i) layout[9u+i*2u]=glm::floatBitsToUint(glm::vec4(0,0,0,.5f));
        const std::vector<VkDeviceSize> sizes={hitT.size()*4u,normals.size()*8u,states.size()*sizeof(SamplingState),
            work.size()*16u,layout.size()*16u,workCount*sizeof(GIProbeFeedback)};
        gpu.Buffer(0,sizes[0],hitT.data()); gpu.Buffer(1,sizes[1],normals.data()); gpu.Buffer(2,sizes[2],states.data());
        gpu.Buffer(3,sizes[3],work.data()); gpu.Buffer(4,sizes[4],layout.data()); gpu.Buffer(5,sizes[5],nullptr);
        VkDescriptorSet set;
        CreateCompute(gpu,set,sizes,ShaderPath().parent_path().parent_path().parent_path()/"Shaders/GIProbeState/GIProbeStatecomp.spv",96u);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex=gpu.queueFamily; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Require(vkCreateCommandPool(gpu.device,&pool,nullptr,&gpu.commands),"publication command pool");
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool=gpu.commands;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
        VkCommandBuffer command;Require(vkAllocateCommandBuffers(gpu.device,&allocation,&command),"publication command");
        std::array<glm::vec4,6> constants={glm::vec4(.5f,.5f,.5f,300),glm::vec4(41,41,39,256),
            glm::vec4(0),glm::vec4(0),glm::vec4(1,2,8,0),glm::vec4(.97f,.95f,12,2)};
        uint32_t movingPublications=0;
        for(uint32_t iteration=0;iteration<32;++iteration)
        {
            const auto before=states;
            std::fill(hitT.begin(),hitT.end(),-1.0f);
            std::fill(normals.begin(),normals.end(),facing(1.0f));
            for(uint32_t probe=0;probe<workCount;++probe)
            {
                work[probe+1] = {probe,glm::floatBitsToUint(iteration%2?10.0f:.016f),iteration,iteration?0u:GIWorkResetLighting};
                for(uint32_t ray=0;ray<32;++ray)
                {
                    const auto index=probe*rays+ray;
                    if(probe<floorCount)
                    {
                        const double heights[]={.01,.025,.05,.35};
                        const auto direction=RegressionFixedDirection(probe,ray);
                        const double distance=direction.y<0?-(heights[probe%4]+states[probe].relocation.y)/direction.y:301.0;
                        if(distance>=.001 && distance<=300.0)hitT[index]=float(distance);
                    }
                    else if(probe==128 && ray<2) // 一背面、一正面、三十个 miss，探针位于开放空间。
                    { hitT[index]=10.0f; normals[index]=facing(ray?1.0f:-1.0f); }
                    else if(probe==129) // 即使稳定几何射线全部命中近距背面，也必须更新并发布。
                    { hitT[index]=.01f; normals[index]=facing(-1.0f); }
                }
            }
            for(size_t slot:{0u,1u,3u})
            {
                const void* input=slot==0?static_cast<void*>(hitT.data()):slot==1?static_cast<void*>(normals.data()):static_cast<void*>(work.data());
                void* mapped;Require(vkMapMemory(gpu.device,gpu.memories[slot],0,sizes[slot],0,&mapped),"publication upload");
                std::memcpy(mapped,input,size_t(sizes[slot]));vkUnmapMemory(gpu.device,gpu.memories[slot]);
            }
            Require(vkResetCommandBuffer(command,0),"publication reset");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Require(vkBeginCommandBuffer(command,&begin),"publication begin");
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipelineLayout,0,1,&set,0,nullptr);
            vkCmdPushConstants(command,gpu.pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,96,constants.data());
            vkCmdDispatch(command,workCount,1,1);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,1,&barrier,0,nullptr,0,nullptr);
            Require(vkEndCommandBuffer(command),"publication end");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
            Require(vkQueueSubmit(gpu.queue,1,&submit,VK_NULL_HANDLE),"publication submit");Require(vkQueueWaitIdle(gpu.queue),"publication wait");
            std::vector<GIProbeFeedback> feedback(workCount);
            for(size_t slot:{2u,5u})
            {
                void* destination=slot==2?static_cast<void*>(states.data()):static_cast<void*>(feedback.data());
                void* mapped;Require(vkMapMemory(gpu.device,gpu.memories[slot],0,sizes[slot],0,&mapped),"publication readback");
                std::memcpy(destination,mapped,size_t(sizes[slot]));vkUnmapMemory(gpu.device,gpu.memories[slot]);
            }
            if(std::memcmp(&states.back(),&untouched,sizeof(untouched)))throw std::runtime_error("publication touched unscheduled state");
            for(uint32_t probe=0;probe<workCount;++probe)
            {
                if(states[probe].metadata.x!=1u||feedback[probe].status!=GIProbeComplete||
                    states[probe].relocation.w!=1.0f||!states[probe].metadata.z)
                    throw std::runtime_error("placed probe lost publication during relocation or backface hits");
                if(probe==129 && (states[probe].metadata.w!=32u || states[probe].distance.w!=1.0f))
                    throw std::runtime_error("backface diagnostic was lost while preserving publication");
                if(glm::vec3(states[probe].distance)!=glm::vec3(before[probe].relocation))
                    throw std::runtime_error("published atlas origin differs from actual ray origin");
                const float movement = glm::length(glm::vec3(states[probe].relocation) - glm::vec3(states[probe].distance));
                if (probe == 128 && movement > 1e-6f)
                    throw std::runtime_error("Distant backface moved a probe in open space");
                if (probe < floorCount && movement > .001f)
                {
                    ++movingPublications;
                    if (iteration >= 24u) throw std::runtime_error("Static plane relocation did not converge");
                }
                if (glm::length(glm::vec3(states[probe].relocation)) > .5f * .45001f)
                    throw std::runtime_error("Relocation exceeded spatial bound");
            }
        }
        if(!movingPublications)throw std::runtime_error("publication fixture did not exercise moving probes");
        std::cout<<"[GIProbePublicationGPU] 131 placed probes x32 updates, published=131/131 each update; moving publications="
            <<movingPublications<<"; sparse distant backfaces, solid interior, sky, atlas origin and untouched state PASS\n";
    }
}


bool TestGIProbePublicationGpuContract()
{
    try { validationErrors=0; RunGIProbePublicationRegression(); return validationErrors==0; }
    catch(const std::exception& error) { std::cerr<<"[GIProbePublicationGPU] FAIL: "<<error.what()<<'\n'; return false; }
}

bool TestGIProbeFeedbackGpuContract()
{
    try
    {
        validationErrors = 0;
        for(bool world : {false,true})
        {
            GPU gpu; InitializeGPU(gpu);
            struct alignas(16) State { glm::vec4 relocation, distance; glm::uvec4 metadata; };
            static_assert(sizeof(State) == 48);
            std::array<State,4> states{};
            states[0] = {{0,0,0,0.9375f}, {1,1,1,0}, {1,0,15,8}};
            states[3] = {{3,4,5,0.75f}, {6,7,8,9}, {1,10,11,12}};
            const State untouched = states[3];
            std::vector<float> positions(768, 0.05f); // 小于 0.1 × spacing 的局部间隙。
            std::vector<glm::uvec2> normals(768);
            const auto half = [](glm::vec4 value) {
                return glm::uvec2(glm::packHalf2x16(glm::vec2(value)), glm::packHalf2x16(glm::vec2(value.z,value.w)));
            };
            for (uint32_t ray=0; ray<768; ++ray)
            {
                normals[ray]=half({0,1,0,ray>=256 && ray<512 ? 1.0f : -1.0f});
            }
            std::vector<glm::uvec4> work = {{3,256,0,1},
                {0,glm::floatBitsToUint(1.0f),3,0}, {1,0,7,GIWorkResetLighting}, {2,0,8,GIWorkResetLighting}};
            std::vector<glm::uvec4> layout(17,glm::uvec4(0));
            layout[0]={1,1,4,0}; layout[1].z=9; layout[7]={0,1,0,4};
            for (uint32_t probe=0; probe<4; ++probe) layout[9+probe*2]=glm::floatBitsToUint(glm::vec4(0,0,0,1));
            const std::vector<VkDeviceSize> sizes={positions.size()*4u,normals.size()*8u,sizeof(states),
                work.size()*16u,layout.size()*16u,3u*sizeof(GIProbeFeedback)};
            gpu.Buffer(0,sizes[0],positions.data()); gpu.Buffer(1,sizes[1],normals.data());
            gpu.Buffer(2,sizes[2],states.data()); gpu.Buffer(3,sizes[3],work.data()); gpu.Buffer(4,sizes[4],layout.data());
            gpu.Buffer(5,sizes[5],nullptr); gpu.Buffer(6,sizes[5],nullptr);
            VkDescriptorSet set;
            const auto shaderPath=ShaderPath().parent_path().parent_path().parent_path()/
                (world?"Shaders/GIWorld/GIWorldStatecomp.spv":"Shaders/GIProbeState/GIProbeStatecomp.spv");
            CreateCompute(gpu,set,sizes,shaderPath,96u);
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.queueFamilyIndex=gpu.queueFamily; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            Require(vkCreateCommandPool(gpu.device,&pool,nullptr,&gpu.commands),"feedback command pool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocation.commandPool=gpu.commands; allocation.commandBufferCount=1; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            VkCommandBuffer command; Require(vkAllocateCommandBuffers(gpu.device,&allocation,&command),"feedback command");
            const std::array<glm::vec4,6> constants={glm::vec4(1,1,1,10),glm::vec4(4,1,1,256),
                glm::vec4(0,2,16,0),glm::vec4(0),glm::vec4(0),glm::vec4(0)};
            const auto read = [&](size_t slot, void* destination, size_t bytes) {
                void* memory; Require(vkMapMemory(gpu.device,gpu.memories[slot],0,bytes,0,&memory),"feedback map read");
                std::memcpy(destination,memory,bytes); vkUnmapMemory(gpu.device,gpu.memories[slot]);
            };
            const auto upload = [&](size_t slot, const void* source, size_t bytes) {
                void* memory; Require(vkMapMemory(gpu.device,gpu.memories[slot],0,bytes,0,&memory),"feedback map update");
                std::memcpy(memory,source,bytes); vkUnmapMemory(gpu.device,gpu.memories[slot]);
            };
            for (uint32_t phase=0; phase<(world?8u:2u); ++phase)
            {
                const uint32_t failureReasons[]={GIProbeHeightBudget,GIProbeVoxelPage,GIProbeVoxelBudget,GIProbeVoxelCoverage,GIProbeFailureMask};
                const uint32_t failure=phase>=2 && phase<7?failureReasons[phase-2]:0u;
                const auto retained=states;
                if (phase)
                {
                    work[0]={1,256,0,1}; work[1]={1,0,8+phase,GIWorkResetLighting};
                    upload(3,work.data(),sizes[3]);
                    for (auto& normal:normals) normal=half({0,1,0,1});
                    upload(1,normals.data(),sizes[1]);
                    std::fill(positions.begin(), positions.end(), 2.0f);
                    if(failure)positions[17]=-2.0f-float(failure);
                    upload(0,positions.data(),sizes[0]);
                }
                Require(vkResetCommandBuffer(command,0),"reset feedback command");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                Require(vkBeginCommandBuffer(command,&begin),"begin feedback command");
                VkMemoryBarrier previous{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                previous.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                previous.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&previous,0,nullptr,0,nullptr);
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipelineLayout,0,1,&set,0,nullptr);
                vkCmdPushConstants(command,gpu.pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,96,constants.data());
                vkCmdDispatch(command,work[0].x,1,1);
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,
                    0,1,&barrier,0,nullptr,0,nullptr);
                VkBufferCopy copy{0,0,work[0].x*sizeof(GIProbeFeedback)};
                vkCmdCopyBuffer(command,gpu.buffers[5],gpu.buffers[6],1,&copy);
                barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
                Require(vkEndCommandBuffer(command),"end feedback command");
                VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
                Require(vkCreateFence(gpu.device,&fenceInfo,nullptr,&fence),"feedback fence");
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
                Require(vkQueueSubmit(gpu.queue,1,&submit,fence),"submit feedback");
                Require(vkWaitForFences(gpu.device,1,&fence,VK_TRUE,UINT64_MAX),"wait feedback completion");
                vkDestroyFence(gpu.device,fence,nullptr);
                std::array<GIProbeFeedback,3> feedback{};
                read(6,feedback.data(),sizeof(feedback)); read(2,states.data(),sizeof(states));
                if (std::memcmp(&states[3],&untouched,sizeof(State))) throw std::runtime_error("feedback changed untouched physical probe");
                if (!phase)
                {
                    for (uint32_t i=0;i<3;++i)
                    {
                        if (feedback[i].probeIndex!=i || feedback[i].status!=GIProbeComplete ||
                            glm::floatBitsToUint(feedback[i].elapsedSeconds)!=work[1+i].y || feedback[i].cycleIndex!=work[1+i].z ||
                            states[i].metadata.x!=1u || states[i].relocation.w!=1.0f ||
                            states[i].metadata.w!=(i==1?0u:32u))
                            throw std::runtime_error("complete update publication or packed ray offset mismatch");
                    }
                }
                else if(failure)
                {
                    if(feedback[0].probeIndex!=1 || feedback[0].status!=failure || feedback[0].cycleIndex!=work[1].z ||
                        std::memcmp(states.data(),retained.data(),sizeof(states)))
                        throw std::runtime_error("World failure reason lost identity or overwrote published state");
                }
                else if (feedback[0].probeIndex!=1 || feedback[0].status!=GIProbeComplete || states[1].metadata.x!=1 ||
                    states[1].metadata.z!=1 || std::abs(states[1].relocation.w-1.0f)>1e-6f)
                    throw std::runtime_error("reset did not rebuild confidence from the complete lighting update");
            }
        }
        std::cout<<"[GIProbeFeedbackGPU] "<<(validationErrors?"FAIL":"PASS")
            <<" production hardware/world state shaders, full 256-ray work, backface publication, reset, failure reasons, retained state and recovery; validationErrors="
            <<validationErrors<<'\n';
        return validationErrors==0;
    }
    catch (const std::exception& error) { std::cerr<<"[GIProbeFeedbackGPU] FAIL: "<<error.what()<<'\n'; return false; }
}

// 使用正式更新 shader；输入为已知球面辐射与 300 m 距离，回读完整图集和边界。
bool TestGIProbeIntegrationGpuContract()
{
    try
    {
        validationErrors = 0;
        for (bool world : {false, true})
        for (bool sparse : {false, true})
        for (float spacing : {0.5f, 1000.0f})
        for (uint32_t rays : {33u, 256u, 4096u})
        {
            GPU gpu; InitializeGPU(gpu);
            const uint32_t probeCount = 3;
            std::vector<float> hitT(rays * 2u, 300.0f);
            std::vector<glm::uvec2> radiance(rays * 2u);
            const auto packed = glm::uvec2(glm::packHalf2x16({1.0f,2.0f}), glm::packHalf2x16({0.5f,1.0f}));
            std::fill(radiance.begin(),radiance.end(),packed);
            std::array<SamplingState,3> states{};
            std::vector<glm::uvec4> work={{2,rays,0,sparse ? 1u : 0u},
                {0,glm::floatBitsToUint(.1f),0,GIWorkResetLighting},{2,glm::floatBitsToUint(.1f),5,GIWorkResetLighting}};
            std::array<glm::uvec4,15> layout{};
            layout[0] = {sparse ? 1u : 0u, 1, 3, 0}; layout[1].z = 9; layout[7] = {0, 1, 0, 3};
            for (uint32_t i = 0; i < 3; ++i)
            {
                layout[9 + i * 2] = glm::floatBitsToUint(glm::vec4(0, 0, 0, spacing));
                // 模拟比规则网格更大的跨层支持区，验证 GPU 确实读取 CPU 范围。
                layout[10 + i * 2].y = glm::floatBitsToUint(spacing * 3.0f);
            }
            std::vector<VkDeviceSize> sizes={hitT.size()*4u,0,radiance.size()*8u,0,sizeof(states),work.size()*16u,sizeof(layout)};
            gpu.Buffer(0,sizes[0],hitT.data()); gpu.Buffer(2,sizes[2],radiance.data());
            gpu.Buffer(4,sizes[4],states.data()); gpu.Buffer(5,sizes[5],work.data()); gpu.Buffer(6,sizes[6],layout.data());
            const std::array<uint32_t,2> tile={16,8};
            for (size_t slot=0;slot<2;++slot)
            {
                VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                info.imageType=VK_IMAGE_TYPE_2D; info.format=slot==0?VK_FORMAT_R32G32_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
                info.extent={tile[slot]*probeCount,tile[slot],1}; info.mipLevels=info.arrayLayers=1;
                info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
                info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                Require(vkCreateImage(gpu.device,&info,nullptr,&gpu.images[slot]),"integration image");
                VkMemoryRequirements requirement; vkGetImageMemoryRequirements(gpu.device,gpu.images[slot],&requirement);
                VkPhysicalDeviceMemoryProperties properties; vkGetPhysicalDeviceMemoryProperties(gpu.physical,&properties);
                uint32_t type=0; while(type<properties.memoryTypeCount && !(requirement.memoryTypeBits&(1u<<type))) ++type;
                if(type==properties.memoryTypeCount) throw std::runtime_error("integration memory type");
                VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocation.allocationSize=requirement.size; allocation.memoryTypeIndex=type;
                Require(vkAllocateMemory(gpu.device,&allocation,nullptr,&gpu.imageMemories[slot]),"integration image memory");
                Require(vkBindImageMemory(gpu.device,gpu.images[slot],gpu.imageMemories[slot],0),"integration image bind");
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view.image=gpu.images[slot];view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
                view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                Require(vkCreateImageView(gpu.device,&view,nullptr,&gpu.imageViews[slot]),"integration image view");
            }
            constexpr VkDeviceSize readBytes=(16u*16u+8u*8u)*3u*8u;
            gpu.Buffer(8,readBytes,nullptr);
            VkDescriptorSet set;
            CreateCompute(gpu,set,sizes,ShaderPath().parent_path().parent_path().parent_path()/(world?"Shaders/GIWorld/GIWorldAtlascomp.spv":"Shaders/GIVisibilityUpdate/GIVisibilityUpdatecomp.spv"),96u,{}, {1u,3u});
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex=gpu.queueFamily;
            pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            Require(vkCreateCommandPool(gpu.device,&pool,nullptr,&gpu.commands),"integration command pool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocation.commandPool=gpu.commands;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
            VkCommandBuffer cmd;Require(vkAllocateCommandBuffers(gpu.device,&allocation,&cmd),"integration command");
            std::array<glm::vec4,6> constants={glm::vec4(spacing,spacing,spacing,300),glm::vec4(3,1,1,float(rays)),glm::vec4(0),
                glm::vec4(0),glm::vec4(1,2,8,0),glm::vec4(.97f,.95f,12,2)};
            for(uint32_t iteration=0;iteration<3;++iteration)
            {
                if(iteration)
                {
                    for(auto& r:radiance) r=glm::uvec2(glm::packHalf2x16({0.5f,1.0f}),glm::packHalf2x16({.25f,1.0f}));
                    for(auto& st:states) st.metadata.z=8;
                    std::fill(hitT.begin(),hitT.end(),200.0f);
                    if(world&&iteration==2)hitT.back()=-2.f;
                    // 同一次完整估计，相隔 1/60 秒或 10 秒都应保留相同的采样历史。
                    work[1].y=glm::floatBitsToUint(1.0f/60.0f);work[2].y=glm::floatBitsToUint(10.0f);
                    // 下一轮真的从新位置追踪后，应重建旧位置的距离/辐照度历史。
                    if(iteration==2)states[0].relocation.x=spacing*.05f;
                    work[1].w=work[2].w=0;
                    for(size_t slot:{0u,2u,4u,5u})
                    {
                        void* mapped;Require(vkMapMemory(gpu.device,gpu.memories[slot],0,sizes[slot],0,&mapped),"integration update map");
                        const void* input=slot==0?static_cast<void*>(hitT.data()):slot==2?static_cast<void*>(radiance.data()):slot==4?static_cast<void*>(states.data()):static_cast<void*>(work.data());
                        std::memcpy(mapped,input,size_t(sizes[slot]));vkUnmapMemory(gpu.device,gpu.memories[slot]);
                    }
                }
                Require(vkResetCommandBuffer(cmd,0),"integration reset");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Require(vkBeginCommandBuffer(cmd,&begin),"integration begin");
                if(!iteration)
                {
                    for(size_t slot=0;slot<2;++slot)
                    {
                        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
                        barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                        barrier.image=gpu.images[slot];barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
                        VkClearColorValue clear{};clear.float32[0]=clear.float32[1]=clear.float32[2]=clear.float32[3]=.125f;
                        vkCmdClearColorImage(cmd,gpu.images[slot],VK_IMAGE_LAYOUT_GENERAL,&clear,1,&barrier.subresourceRange);
                    }
                }
                VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                ready.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,nullptr,0,nullptr);
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipelineLayout,0,1,&set,0,nullptr);
                vkCmdPushConstants(cmd,gpu.pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,96,constants.data());
                vkCmdDispatch(cmd,1,2,1); // 覆盖跨二维 dispatch 的工作索引。
                ready.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&ready,0,nullptr,0,nullptr);
                VkDeviceSize offset=0;
                for(size_t slot=0;slot<2;++slot)
                {
                    VkBufferImageCopy copy{};copy.bufferOffset=offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                    copy.imageExtent={tile[slot]*probeCount,tile[slot],1};
                    vkCmdCopyImageToBuffer(cmd,gpu.images[slot],VK_IMAGE_LAYOUT_GENERAL,gpu.buffers[8],1,&copy);
                    offset+=uint64_t(tile[slot])*tile[slot]*probeCount*8u;
                }
                ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&ready,0,nullptr,0,nullptr);
                Require(vkEndCommandBuffer(cmd),"integration end");
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
                Require(vkQueueSubmit(gpu.queue,1,&submit,VK_NULL_HANDLE),"integration submit");Require(vkQueueWaitIdle(gpu.queue),"integration wait");
                void* mapped;Require(vkMapMemory(gpu.device,gpu.memories[8],0,readBytes,0,&mapped),"integration readback");
                std::vector<uint8_t> data(readBytes);std::memcpy(data.data(),mapped,size_t(readBytes));vkUnmapMemory(gpu.device,gpu.memories[8]);
                offset=0;
                for(size_t slot=0;slot<2;++slot)
                {
                    uint32_t size=tile[slot],width=size*probeCount;
                    for(uint32_t probe=0;probe<probeCount;++probe)
                    for(uint32_t y=0;y<size;++y) for(uint32_t x=0;x<size;++x)
                    {
                        size_t index=(y*width+probe*size+x)*8u+size_t(offset);
                        if(probe==1)
                        {
                            if(slot==0)
                            { float value[2];std::memcpy(value,data.data()+index,8);if(value[0]!=.125f||value[1]!=.125f)throw std::runtime_error("untouched distance changed"); }
                            else { uint16_t value[4];std::memcpy(value,data.data()+index,8);for(auto v:value)if(glm::unpackHalf1x16(v)!=.125f)throw std::runtime_error("untouched irradiance changed"); }
                            continue;
                        }
                        const uint32_t sampleIteration=world&&iteration==2&&probe==2?1u:iteration;
                        if(slot==0)
                        {
                            float value[2];std::memcpy(value,data.data()+index,8);
                            const float range = std::min(300.0f, spacing * (sparse ? 3.0f : std::sqrt(3.0f) + .45f));
                            const float first = std::min(300.0f, range), later = std::min(200.0f, range);
                            const float historyWeight = iteration == 2 && probe == 0 ? 0.0f : std::pow(.95f, float(sampleIteration));
                            const float mean = later + (first - later) * historyWeight;
                            const float second = later * later + (first * first - later * later) * historyWeight;
                            if(!std::isfinite(value[0])||!std::isfinite(value[1])||std::abs(value[0]-mean)>0.01f||std::abs(value[1]-second)>4.0f)
                                throw std::runtime_error("full weighted distance ratio overflowed or lost units");
                        }
                        else
                        {
                            uint16_t value[4];std::memcpy(value,data.data()+index,8);
                            const float scale=iteration==2&&probe==0?.5f:.5f+.5f*std::pow(.97f,float(sampleIteration));
                            const float tolerance=rays==33?.15f:.025f;
                            const float expected[3]={3.14159265f,6.2831853f,1.5707963f};
                            for(uint32_t c=0;c<3;++c)
                                if(!std::isfinite(glm::unpackHalf1x16(value[c]))||std::abs(glm::unpackHalf1x16(value[c])-expected[c]*scale)>expected[c]*tolerance)
                                    throw std::runtime_error("sphere integral or per-update history disagrees with analytic pi L");
                            if(glm::unpackHalf1x16(value[3])!=1.0f)throw std::runtime_error("complete irradiance lost support");
                        }
                        if(x==0||x==size-1||y==0||y==size-1)
                        {
                            uint32_t sx=std::clamp(x,1u,size-2),sy=std::clamp(y,1u,size-2);
                            if((x==0||x==size-1)&&(y==0||y==size-1)){sx=size-1-sx;sy=size-1-sy;}
                            else if(x==0||x==size-1)sy=size-1-sy;else sx=size-1-sx;
                            const size_t source=(sy*width+probe*size+sx)*8u+size_t(offset);
                            if(std::memcmp(data.data()+index,data.data()+source,8))throw std::runtime_error("octahedral border mismatch");
                        }
                    }
                    offset+=uint64_t(size)*size*probeCount*8u;
                }
            }
            std::cout<<"[GIProbeIntegrationGPU] world="<<world<<" sparse="<<sparse<<" spacing="<<spacing<<" rays="<<rays<<" analytic pi L, 300m/90000m2, 1/60s vs 10s history, origin reset, borders, untouched tiles PASS\n";
        }
        return validationErrors==0;
    }
    catch(const std::exception& error){std::cerr<<"[GIProbeIntegrationGPU] FAIL: "<<error.what()<<'\n';return false;}
}

// 真实 GPU 运行公共天空采样接口；oracle 由恒定球面 L 的解析积分 E=pi*L 给出。
bool TestSkyLightingGpuContract()
{
    try
    {
        validationErrors = 0;
        struct SkyCase
        {
            float skyIntensity;
            glm::vec3 sunDirection;
            float mainLightIntensity;
            float expectedScale;
        };
        // 解析期望独立于帧构建函数；改变或关闭主光强度不能改变静态天空亮度。
        const std::array<SkyCase, 10> cases = {{
            {0.0f, {0, 1, 0}, 1.0f, 0.0f},
            {0.5f, {0, 1, 0}, 1.0f, 0.5f},
            {1.0f, {0, 1, 0}, 1.0f, 1.0f},
            {2.0f, {0, 1, 0}, 1.0f, 2.0f},
            {1.0f, {0, 1, 0}, 0.0f, 1.0f},
            {0.5f, {0, 1, 0}, 2.0f, 0.5f},
            {1.06f, {0, 1, 0}, 6.7f, 1.06f},
            {1.0f, {0, -1, 0}, 2.0f, 0.065f},
            {0.5f, {1, 0, 0}, 2.0f, 0.183415712f},
            {1.0f, {0, -1, 0}, 0.0f, 0.065f}
        }};
        for (const auto& skyCase : cases)
        for (const glm::vec3 radiance : {glm::vec3(0.2f, 0.4f, 0.6f), glm::vec3(248.0f, 238.0f, 213.0f)})
        {
            VansDirectionalLight mainLight{};
            mainLight.m_Direction = skyCase.sunDirection;
            mainLight.m_Color = glm::vec3(1.0f);
            mainLight.m_Intensity = skyCase.mainLightIntensity;
            const auto celestial = VansLightManager::ComputeCelestialLightingState(mainLight);
            const auto frame = BuildSkyLightingFrame(skyCase.skyIntensity,
                celestial.skyDiffuseScale);
            const float intensity = skyCase.expectedScale;
            if (frame.intensity != skyCase.skyIntensity ||
                std::abs(frame.radianceScale - intensity) > 0.000002f)
                throw std::runtime_error("Static sky angle scale or main-light intensity independence mismatch");
            GPU gpu; InitializeGPU(gpu);
            gpu.Buffer(0, sizeof(glm::vec4) * 4, nullptr);
            const glm::vec4 parameters(frame.radianceScale, 0, 0, 0);
            gpu.Buffer(8, sizeof(parameters), &parameters);
            std::vector<VkDescriptorImageInfo> textures;
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            Require(vkCreateSampler(gpu.device, &sampler, nullptr, &gpu.sampler), "create sky sampler");
            for (uint32_t i = 0; i < 3; ++i)
            {
                CreateSkyCube(gpu, i);
                textures.push_back({gpu.sampler, gpu.imageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            }
            VkDescriptorSet set;
            CreateCompute(gpu, set, {sizeof(glm::vec4) * 4}, ShaderPath().parent_path().parent_path() /
                "SkyLighting/SkyLightingContractcomp.spv", 0u, textures, {}, true);
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = gpu.queueFamily;
            Require(vkCreateCommandPool(gpu.device, &pool, nullptr, &gpu.commands), "create sky commands");
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = gpu.commands; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
            VkCommandBuffer command; Require(vkAllocateCommandBuffers(gpu.device, &allocate, &command), "allocate sky commands");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            Require(vkBeginCommandBuffer(command, &begin), "begin sky commands");
            for (uint32_t slot = 0; slot < 3; ++slot)
            {
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.image = gpu.images[slot]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                const glm::vec3 value = radiance * (slot == 1u ? 3.14159265358979323846f : 1.0f);
                const VkClearColorValue color{{value.x, value.y, value.z, 1.0f}};
                vkCmdClearColorImage(command, gpu.images[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
                barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipelineLayout, 0, 1, &set, 0, nullptr);
            vkCmdDispatch(command, 1, 1, 1);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            Require(vkEndCommandBuffer(command), "end sky commands");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            Require(vkQueueSubmit(gpu.queue, 1, &submit, VK_NULL_HANDLE), "submit sky sampling");
            Require(vkQueueWaitIdle(gpu.queue), "wait sky sampling");
            void* mapped;
            Require(vkMapMemory(gpu.device, gpu.memories[0], 0, sizeof(glm::vec4) * 4, 0, &mapped), "read sky sampling");
            std::array<glm::vec4, 4> result;
            std::memcpy(result.data(), mapped, sizeof(result)); vkUnmapMemory(gpu.device, gpu.memories[0]);
            for (size_t source = 0; source < result.size(); ++source)
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    const float expected = radiance[channel] * intensity;
                    // RGBA16F 上传允许一个 half ULP；卷积输出仍按 RGBA32F 精度验证。
                    const float tolerance = source == 0 ? intensity * std::exp2(std::floor(std::log2(radiance[channel])) - 10.0f) + 0.00001f :
                        0.0001f * std::max(1.0f, expected);
                    if (!std::isfinite(result[source][channel]) || std::abs(result[source][channel] - expected) > tolerance)
                        throw std::runtime_error("Sky energy mismatch: source=" + std::to_string(source) +
                            " expected=" + std::to_string(expected) + " actual=" + std::to_string(result[source][channel]));
                }
        }
        if (validationErrors) throw std::runtime_error("Sky sampling validation errors");
        std::cout << "[SkyLightingGPU] PASS: 4 source interfaces, 10 angle/sky cases, main-light intensity independent, day/twilight/night, LDR/HDR, validationErrors=0\n";
        return true;
    }
    catch (const std::exception& error) { std::cerr << "[SkyLightingGPU] FAIL: " << error.what() << '\n'; return false; }
}
