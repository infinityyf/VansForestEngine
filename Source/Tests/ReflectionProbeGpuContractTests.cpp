#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeSpatialIndex.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbePageLayout.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbePublication.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
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
            std::cerr << "[ReflectionProbeGPU validation] " << message->pMessage << '\n';
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
        std::array<VkBuffer, 4> buffers{};
        std::array<VkDeviceMemory, 4> memories{};
        std::vector<VkImage> pageImages;
        std::vector<VkImageView> pageViews;
        std::vector<VkDeviceMemory> pageMemory;
        std::vector<VkImageCreateInfo> pageInfo;
        VkSampler pageSampler = VK_NULL_HANDLE;
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
                if (pageSampler) vkDestroySampler(device, pageSampler, nullptr);
                for (size_t i = 0; i < pageImages.size(); ++i)
                {
                    if (pageViews[i]) vkDestroyImageView(device, pageViews[i], nullptr);
                    if (pageImages[i]) vkDestroyImage(device, pageImages[i], nullptr);
                    if (pageMemory[i]) vkFreeMemory(device, pageMemory[i], nullptr);
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
            info.size = size; info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
        void CreatePages(const VansReflectionProbePageLayout& layout, bool publication)
        {
            const size_t count = layout.Pages().size() + (publication ? 1u : 0u);
            pageImages.resize(count); pageViews.resize(count); pageMemory.resize(count); pageInfo.resize(count);
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(physical, &properties);
            for (size_t page = 0; page < count; ++page)
            {
                VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; info.imageType = VK_IMAGE_TYPE_2D;
                info.format = publication ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT; const uint32_t resolution = page < layout.Pages().size() ? layout.Pages()[page].resolution : layout.CaptureResolution();
                info.extent = {resolution, resolution, 1};
                info.mipLevels = page < layout.Pages().size() ? layout.Pages()[page].mipCount : layout.CaptureMipCount();
                info.arrayLayers = (page < layout.Pages().size() ? layout.Pages()[page].cubeCount : 1u) * 6; info.samples = VK_SAMPLE_COUNT_1_BIT;
                info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                pageInfo[page] = info;
                Require(vkCreateImage(device, &info, nullptr, &pageImages[page]), "create cube page");
                VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device, pageImages[page], &requirements);
                uint32_t type = 0;
                while (type < properties.memoryTypeCount && !(requirements.memoryTypeBits & (1u << type))) ++type;
                if (type == properties.memoryTypeCount) throw std::runtime_error("Texture page memory unavailable");
                VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
                Require(vkAllocateMemory(device, &allocation, nullptr, &pageMemory[page]), "allocate cube page");
                Require(vkBindImageMemory(device, pageImages[page], pageMemory[page], 0), "bind cube page");
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view.image = pageImages[page]; view.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY; view.format = info.format;
                view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, info.mipLevels, 0, info.arrayLayers};
                Require(vkCreateImageView(device, &view, nullptr, &pageViews[page]), "create cube page view");
            }
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter = VK_FILTER_NEAREST; sampler.minFilter = VK_FILTER_NEAREST;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; sampler.maxLod = 9;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            Require(vkCreateSampler(device, &sampler, nullptr, &pageSampler), "create cube page sampler");
        }
        void FillPages(const VansReflectionProbePageLayout& layout, VkCommandBuffer command)
        {
            std::vector<std::vector<uint32_t>> logicalIds;
            for (const auto& page : layout.Pages()) logicalIds.emplace_back(page.cubeCount, UINT32_MAX);
            for (uint32_t i = 0; i < layout.ProbeCount(); ++i)
            { const auto address = layout.Address(i); logicalIds[address.page][address.cube] = i; }
            for (uint32_t page = 0; page < layout.Pages().size(); ++page)
            {
                const uint32_t cubeCount = layout.Pages()[page].cubeCount;
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.image = pageImages[page]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, pageInfo[page].mipLevels, 0, cubeCount * 6};
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                for (uint32_t cube = 0; cube < cubeCount; ++cube) for (uint32_t face = 0; face < 6; ++face) for (uint32_t mip = 0; mip < pageInfo[page].mipLevels; ++mip)
                {
                    VkClearColorValue color{{float(logicalIds[page][cube] + 1) / 2048.0f, float(face) / 8, float(mip) / 8, 1}};
                    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, cube * 6 + face, 1};
                    vkCmdClearColorImage(command, pageImages[page], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
                }
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            if (pageImages.size() > layout.Pages().size())
            {
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.image = pageImages.back(); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, pageInfo.back().mipLevels, 0, 6};
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                VkClearColorValue clear{{0,0,0,1}};
                vkCmdClearColorImage(command, pageImages.back(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &barrier.subresourceRange);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }
        // 人工颜色用于隔离发布行为，分别模拟逐面捕获、完成 mip、以及中途取消。
        void FillCapture(VkCommandBuffer command, uint32_t phase, uint32_t baseMip)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.image = pageImages.back(); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, pageInfo.back().mipLevels, 0, 6};
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            for (uint32_t mip = baseMip; mip < pageInfo.back().mipLevels; ++mip) for (uint32_t face = 0; face < 6; ++face)
            {
                if (phase < 7 ? (mip != baseMip || face != phase - 1u) : phase == 7 ? mip == baseMip : (mip != baseMip || face != 0)) continue;
                VkClearColorValue color{{phase == 8 ? 0.25f : 0.875f, float(face) / 8, float(mip - baseMip) / 8, 1}};
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1};
                vkCmdClearColorImage(command, pageImages.back(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
            }
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    };

    std::filesystem::path ShaderPath(bool pages)
    {
        auto root = std::filesystem::current_path();
        const std::filesystem::path relative = pages
            ? "EngineAssets/Validation/ReflectionProbe/ReflectionProbePagesContractcomp.spv"
            : "EngineAssets/Validation/ReflectionProbe/ReflectionProbeIndexContractcomp.spv";
        for (;;)
        {
            if (std::filesystem::exists(root / relative)) return root / relative;
            if (std::filesystem::exists(root / "ForestEngine" / relative)) return root / "ForestEngine" / relative;
            if (root == root.root_path()) break;
            root = root.parent_path();
        }
        throw std::runtime_error("Reflection probe contract shader not found");
    }

    bool RunCase(const char* label, std::vector<VansReflectionProbeGPU> probes,
        ReflectionProbeBufferHeader header, const std::vector<glm::vec4>& queries, bool pages = false, bool publication = false, bool mixedResolution = false)
    {
        GPU gpu;
        validationErrors = 0;
        vulkan_library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!vulkan_library || !LoadVulkanExportedFunction() || !LoadVulkanGlobalLevelFunctions())
            throw std::runtime_error("Vulkan loader unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Forest Reflection Probe GPU Contract"; app.apiVersion = VK_API_VERSION_1_2;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* extensions[] = {"VK_EXT_debug_utils", "VK_EXT_validation_features"};
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback = Validation;
        const VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        validation.enabledValidationFeatureCount = 1; validation.pEnabledValidationFeatures = &enabled; validation.pNext = &debug;
        VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance.pApplicationInfo = &app; instance.enabledLayerCount = 1; instance.ppEnabledLayerNames = &layer;
        instance.enabledExtensionCount = publication ? 2 : 1; instance.ppEnabledExtensionNames = extensions;
        instance.pNext = publication ? static_cast<const void*>(&validation) : static_cast<const void*>(&debug);
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
        VkPhysicalDeviceFeatures features{};
        features.imageCubeArray = VK_TRUE;
        features.shaderSampledImageArrayDynamicIndexing = pages ? VK_TRUE : VK_FALSE;
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.pNext = pages ? &features12 : nullptr;
        device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue; device.pEnabledFeatures = &features;
        Require(vkCreateDevice(gpu.physical, &device, nullptr, &gpu.device), "create device");
        if (!LoadVulkanDeviceLevelFunctions(gpu.device)) throw std::runtime_error("Vulkan device functions unavailable");
        vkGetDeviceQueue(gpu.device, gpu.queueFamily, 0, &gpu.queue);

        VansReflectionProbePageLayout pageLayout;
        if (pages)
        {
            VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(gpu.physical, &properties);
            std::string error;
            std::vector<uint32_t> resolutions(probes.size(), 8);
            if (mixedResolution) for (uint32_t i = 0; i < probes.size(); ++i) resolutions[i] <<= i % 3u;
            if (!pageLayout.Build(resolutions, properties.limits.maxImageArrayLayers, error)) throw std::runtime_error(error);
            if (pageLayout.Pages().size() < 2) throw std::runtime_error("Page contract must exercise multiple real images");
            for (uint32_t i = 0; i < probes.size(); ++i)
            {
                const auto address = pageLayout.Address(i);
                probes[i].regionAndFlags.w = address.page; probes[i].capturePositionAndLayer.w = float(address.cube);
                probes[i].specularAndMip = {0, 1, float(pageLayout.Pages()[address.page].mipCount - 1u), 0};
            }
            gpu.CreatePages(pageLayout, publication);
        }

        VansReflectionProbeSpatialIndex index;
        index.Update(probes);
        header.probeCount = uint32_t(probes.size());
        std::vector<uint8_t> metadata(sizeof(header) + std::max(size_t(1), probes.size()) * sizeof(VansReflectionProbeGPU));
        std::memcpy(metadata.data(), &header, sizeof(header));
        if (!probes.empty()) std::memcpy(metadata.data() + sizeof(header), probes.data(), probes.size() * sizeof(VansReflectionProbeGPU));
        std::vector<uint8_t> spatial(sizeof(ReflectionProbeIndexHeader) + index.GetWords().size() * sizeof(uint32_t));
        std::memcpy(spatial.data(), &index.GetHeader(), sizeof(ReflectionProbeIndexHeader));
        std::memcpy(spatial.data() + sizeof(ReflectionProbeIndexHeader), index.GetWords().data(), index.GetWords().size() * sizeof(uint32_t));
        const std::array<VkDeviceSize, 4> sizes = {metadata.size(), spatial.size(), queries.size() * sizeof(glm::vec4), queries.size() * sizeof(glm::uvec4)};
        gpu.Buffer(0, sizes[0], metadata.data()); gpu.Buffer(1, sizes[1], spatial.data());
        gpu.Buffer(2, sizes[2], queries.data()); gpu.Buffer(3, sizes[3], nullptr);
        const std::array<uint32_t, 4> bindings = {14, 36, 37, 38};
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(4);
        for (size_t i = 0; i < bindings.size(); ++i)
            layoutBindings[i] = {bindings[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        if (pages) layoutBindings.push_back({13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ReflectionProbeMaxTexturePages, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = uint32_t(layoutBindings.size()); setLayout.pBindings = layoutBindings.data();
        Require(vkCreateDescriptorSetLayout(gpu.device, &setLayout, nullptr, &gpu.setLayout), "create descriptor layout");
        const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ReflectionProbeMaxTexturePages}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1; pool.poolSizeCount = pages ? 2 : 1; pool.pPoolSizes = poolSizes;
        Require(vkCreateDescriptorPool(gpu.device, &pool, nullptr, &gpu.pool), "create descriptor pool");
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = gpu.pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &gpu.setLayout;
        VkDescriptorSet set;
        Require(vkAllocateDescriptorSets(gpu.device, &allocation, &set), "allocate descriptor set");
        std::array<VkDescriptorBufferInfo, 4> bufferInfo{};
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (size_t i = 0; i < writes.size(); ++i)
        {
            bufferInfo[i] = {gpu.buffers[i], 0, sizes[i]};
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set; writes[i].dstBinding = bindings[i]; writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &bufferInfo[i];
        }
        vkUpdateDescriptorSets(gpu.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
        if (pages)
        {
            std::vector<VkDescriptorImageInfo> images;
            for (uint32_t page = 0; page < ReflectionProbeMaxTexturePages; ++page)
                images.push_back({gpu.pageSampler, gpu.pageViews[page < pageLayout.Pages().size() ? page : 0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set; write.dstBinding = 13; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = ReflectionProbeMaxTexturePages; write.pImageInfo = images.data();
            vkUpdateDescriptorSets(gpu.device, 1, &write, 0, nullptr);
        }
        std::ifstream file(ShaderPath(pages), std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot read contract SPIR-V");
        const size_t byteCount = size_t(file.tellg());
        std::vector<uint32_t> code(byteCount / 4);
        file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), byteCount);
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = byteCount; shader.pCode = code.data();
        Require(vkCreateShaderModule(gpu.device, &shader, nullptr, &gpu.shader), "create shader module");
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1; pipelineLayout.pSetLayouts = &gpu.setLayout;
        Require(vkCreatePipelineLayout(gpu.device, &pipelineLayout, nullptr, &gpu.pipelineLayout), "create pipeline layout");
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.layout = gpu.pipelineLayout; pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module = gpu.shader; pipeline.stage.pName = "main";
        Require(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &gpu.pipeline), "create compute pipeline");
        VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commands.queueFamilyIndex = gpu.queueFamily; commands.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Require(vkCreateCommandPool(gpu.device, &commands, nullptr, &gpu.commands), "create command pool");
        VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocation.commandPool = gpu.commands; commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandAllocation.commandBufferCount = 1;
        VkCommandBuffer command;
        Require(vkAllocateCommandBuffers(gpu.device, &commandAllocation, &command), "allocate command buffer");
        size_t mismatches = 0; uint64_t candidateCount = 0;
        const uint32_t publicationCube = publication ? uint32_t(probes.size() - 1u) : 0u;
        const uint32_t phaseCount = publication ? 9u : 1u;
        for (uint32_t phase = 0; phase < phaseCount; ++phase)
        {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Require(vkBeginCommandBuffer(command, &begin), "begin commands");
        if (pages && phase == 0) gpu.FillPages(pageLayout, command);
        if (publication && phase != 0)
        {
            const auto address = pageLayout.Address(publicationCube);
            const uint32_t sourceBaseMip = pageLayout.CaptureMipCount() - pageLayout.Pages()[address.page].mipCount;
            gpu.FillCapture(command, phase, sourceBaseMip);
            const auto source = gpu.pageImages.back(), destination = gpu.pageImages[address.page];
            const auto& sourceInfo = gpu.pageInfo.back(); const auto& destinationInfo = gpu.pageInfo[address.page];
            if (phase == 3)
            {
                auto incompatible = destinationInfo; incompatible.mipLevels = 1;
                if (RecordReflectionProbePublication(command, source, sourceInfo, sourceBaseMip, source, sourceInfo, 0) ||
                    RecordReflectionProbePublication(command, source, sourceInfo, UINT32_MAX, destination, destinationInfo, address.cube) ||
                    RecordReflectionProbePublication(command, source, sourceInfo, sourceBaseMip + 1u, destination, destinationInfo, address.cube) ||
                    RecordReflectionProbePublication(command, source, sourceInfo, sourceBaseMip, destination, destinationInfo, UINT32_MAX) ||
                    RecordReflectionProbePublication(command, source, sourceInfo, sourceBaseMip, destination, incompatible, address.cube))
                    throw std::runtime_error("Publication accepted an invalid or incomplete destination");
            }
            if (phase == 7 && !RecordReflectionProbePublication(command, source, sourceInfo, sourceBaseMip, destination, destinationInfo, address.cube))
                throw std::runtime_error("Complete cubemap publication failed");
        }
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
        Require(vkMapMemory(gpu.device, gpu.memories[3], 0, sizes[3], 0, &mapped), "map results");
        const auto* results = static_cast<const glm::uvec4*>(mapped);
        for (size_t i = 0; i < queries.size(); ++i)
        {
            if (pages)
            {
                const auto& value = static_cast<const glm::vec4*>(mapped)[i];
                const uint32_t cube = uint32_t(std::lround(queries[i].x / 12));
                const uint32_t face = uint32_t(queries[i].w) % 64u % 6u, mip = (uint32_t(queries[i].w) % 64u) / 6u;
                glm::vec4 expected(publication && phase >= 7 && cube == publicationCube ? 0.875f : float(cube + 1) / 2048, float(face) / 8, float(mip) / 8, 1);
                if (IsReflectionProbeIsolatedDebugView(static_cast<ReflectionProbeDebugView>(header.debugView)))
                {
                    // 三种位置分别在盒内、混合带中点和覆盖外；天空色与 mip 色互不相同。
                    const float coverage = queries[i].y == 0.0f ? 1.0f : (queries[i].y == 3.0f ? 0.5f : 0.0f);
                    const glm::vec3 local = coverage > 0.0f ? glm::vec3(expected.x, expected.y, 0.125f) : glm::vec3(0.0f);
                    const glm::vec3 sky(0.25f, 0.5f, 1.0f);
                    glm::vec3 color = local;
                    if (header.debugView == uint32_t(ReflectionProbeDebugView::EnvironmentRadiance)) color = local * coverage + sky * (1.0f - coverage);
                    if (header.debugView == uint32_t(ReflectionProbeDebugView::SkyFallbackRadiance)) color = sky * (1.0f - coverage);
                    color *= 2.0f;
                    if (header.debugView == uint32_t(ReflectionProbeDebugView::LocalCoverage)) color = glm::vec3(coverage);
                    expected = glm::vec4(color, coverage);
                }
                if (glm::any(glm::isnan(value)) || glm::any(glm::greaterThan(glm::abs(value - expected), glm::vec4(0.00001f))))
                    if (mismatches++ < 5) std::cerr << "[ReflectionProbeGPU] phase=" << phase << " page sample mismatch: cube=" << cube << " face=" << face << " mip=" << mip << '\n';
            }
            else
            {
                if (results[i].x && mismatches++ < 5)
                    std::cerr << "[ReflectionProbeGPU] " << label << " query=" << i << " mismatch=" << results[i].x << '\n';
                candidateCount += results[i].y;
            }
        }
        vkUnmapMemory(gpu.device, gpu.memories[3]);
        if (publication) Require(vkResetCommandBuffer(command, 0), "reset publication commands");
        }
        const bool passed = mismatches == 0 && validationErrors == 0;
        std::cout << "[ReflectionProbeGPU] " << (passed ? "PASS " : "FAIL ") << label
                  << " probes=" << probes.size() << " queries=" << queries.size() << " mismatches=" << mismatches
                  << " meanCSRCandidates=" << double(candidateCount) / queries.size()
                  << " texturePages=" << pageLayout.Pages().size() << " snapshots=" << phaseCount
                  << " validationErrors=" << validationErrors << '\n';
        return passed;
    }

    VansReflectionProbeGPU Box(glm::vec3 center, glm::vec3 halfSize)
    {
        VansReflectionProbeGPU p{};
        p.positionAndRadius = glm::vec4(center, glm::length(halfSize));
        p.boxMinAndType = glm::vec4(center - halfSize, 1);
        p.boxMaxAndPriority = glm::vec4(center + halfSize, 1);
        p.fadeAndIntensity = {2, 1, 0, 0}; p.regionAndFlags.y = 1u;
        return p;
    }
    std::vector<glm::vec4> Queries(const std::vector<VansReflectionProbeGPU>& probes)
    {
        VansReflectionProbeSpatialIndex index;
        index.Update(probes);
        const auto& header = index.GetHeader();
        std::mt19937 random(20260907u);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::vector<glm::vec4> result;
        for (int i = 0; i < 12000; ++i)
            result.emplace_back(glm::vec3(header.origin) + glm::vec3(unit(random), unit(random), unit(random)) *
                glm::vec3(header.dimensionsAndCellCount) / glm::vec3(header.inverseCellSize), 0.0f);
        for (const auto& probe : probes)
        {
            result.emplace_back(glm::vec3(probe.positionAndRadius), 0);
            result.emplace_back(glm::vec3(probe.boxMinAndType), 0);
            result.emplace_back(glm::vec3(probe.boxMaxAndPriority), 0);
        }
        return result;
    }
}

bool TestReflectionProbePagesGpuContract()
{
    try
    {
        std::vector<VansGraphics::VansReflectionProbeGPU> probes;
        std::vector<glm::vec4> queries;
        for (uint32_t i = 0; i < 1024; ++i)
        {
            auto probe = Box({float(i) * 12, 0, 0}, glm::vec3(2));
            probes.push_back(probe);
            for (uint32_t sample = 0; sample < 24; ++sample) queries.emplace_back(float(i) * 12, 0, 0, float(sample + 3u * 64u));
        }
        std::mt19937 random(4086); std::shuffle(queries.begin(), queries.end(), random);
        if (!RunCase("paged-production-sampling", probes, {}, queries, true)) return false;
        queries.clear();
        for (uint32_t i = 0; i < probes.size(); ++i)
            for (uint32_t sample = 0; sample < (4u + i % 3u) * 6u; ++sample)
                queries.emplace_back(float(i) * 12, 0, 0, float(sample + (3u + i % 3u) * 64u));
        std::shuffle(queries.begin(), queries.end(), random);
        if (!RunCase("mixed-resolution-production-sampling", probes, {}, queries, true, false, true)) return false;
        queries.clear();
        for (uint32_t i = 0; i < probes.size(); ++i)
            for (float y : {0.0f, 3.0f, 6.0f})
                for (uint32_t face = 0; face < 6; ++face)
                    queries.emplace_back(float(i) * 12, y, 0, float(face + 3u * 64u));
        for (uint32_t view = uint32_t(ReflectionProbeDebugView::LocalRadiance); view <= uint32_t(ReflectionProbeDebugView::LocalCoverage); ++view)
        {
            ReflectionProbeBufferHeader header;
            header.debugView = view; header.lightingParams.z = 1.0f / 3.0f; header.lightingParams.w = 2.0f;
            if (!RunCase(("material-independent-diagnostic-" + std::to_string(view)).c_str(), probes, header, queries, true)) return false;
        }
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ReflectionProbeGPU] page contract failed: " << error.what() << '\n';
        return false;
    }
}

bool TestReflectionProbePublicationGpuContract()
{
    try
    {
        std::vector<VansGraphics::VansReflectionProbeGPU> probes;
        std::vector<glm::vec4> queries;
        for (uint32_t i = 0; i < 1024; ++i)
        {
            probes.push_back(Box({float(i) * 12, 0, 0}, glm::vec3(2)));
            for (uint32_t sample = 0; sample < 24; ++sample) queries.emplace_back(float(i) * 12, 0, 0, float(sample + 3u * 64u));
        }
        std::mt19937 random(94086); std::shuffle(queries.begin(), queries.end(), random);
        if (!RunCase("atomic-publication", probes, {}, queries, true, true)) return false;
        queries.clear();
        for (uint32_t i = 0; i < probes.size(); ++i)
            for (uint32_t sample = 0; sample < (4u + i % 3u) * 6u; ++sample)
                queries.emplace_back(float(i) * 12, 0, 0, float(sample + (3u + i % 3u) * 64u));
        std::shuffle(queries.begin(), queries.end(), random);
        const bool passed = RunCase("mixed-resolution-atomic-publication", probes, {}, queries, true, true, true);
        std::cout << "[ReflectionProbeGPU] publication post-destruction validationErrors=" << validationErrors << '\n';
        return passed && validationErrors == 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ReflectionProbeGPU] publication contract failed: " << error.what() << '\n';
        return false;
    }
}

bool TestReflectionProbeGpuContract(const char* scenePath)
{
    try
    {
        using namespace VansGraphics;
        std::vector<VansReflectionProbeGPU> probes;
        for (int z = 0; z < 8; ++z) for (int y = 0; y < 4; ++y) for (int x = 0; x < 32; ++x)
        {
            auto probe = Box({float(x * 12 - 192), float(y * 12 - 24), float(z * 12 - 48)}, glm::vec3(6));
            if (probes.size() % 5 == 0) probe.boxMinAndType.w = 0.0f;
            if (probes.size() % 7 == 0) probe.regionAndFlags.y = 0u;
            if (probes.size() % 11 == 0) probe.fadeAndIntensity.y = 0.0f;
            if (probes.size() % 13 == 0) probe.fadeAndIntensity.x = 0.0f;
            probe.boxMaxAndPriority.w = float(int(probes.size() % 5) - 2) * 0.5f;
            probes.push_back(probe);
        }
        if (!RunCase("mixed-spatial-index", probes, {}, Queries(probes))) return false;
        probes.assign(24, Box(glm::vec3(-10), glm::vec3(3)));
        probes.back().boxMaxAndPriority.w = 8.0f;
        if (!RunCase("dense-overlap", probes, {}, Queries(probes))) return false;
        probes.clear();
        for (int z = 0; z < 4; ++z) for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            probes.push_back(Box(glm::vec3(x, y, z) * 8.0f - glm::vec3(12), glm::vec3(4)));
        ReflectionProbeBufferHeader uniform;
        uniform.uniformGridOrigin = glm::vec4(-16, -16, -16, 0);
        uniform.uniformGridInvCellSize = glm::vec4(0.125f);
        uniform.uniformGridDimensionsAndFlags = {4, 4, 4, 1};
        if (!RunCase("uniform-tie-order", probes, uniform, Queries(probes))) return false;
        if (!RunCase("empty-scene", {}, {}, {{0, 0, 0, 0}, {-20, -5, 3, 0}})) return false;
        if (scenePath && *scenePath)
        {
            std::ifstream file(scenePath);
            if (!file) throw std::runtime_error("Cannot read requested probe test scene");
            nlohmann::json scene; file >> scene;
            probes.clear();
            const auto vector = [](const nlohmann::json& value) { return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()); };
            for (const auto& source : scene.at("settings").at("reflectionProbes").at("probes"))
            {
                if (source.value("type", "baked") == "sky") continue;
                VansReflectionProbeGPU p{};
                p.positionAndRadius = glm::vec4(vector(source.at("position")), source.value("radius", 10.0f));
                p.boxMinAndType = glm::vec4(vector(source.at("boxMin")), source.value("shape", "box") == "box" ? 1.0f : 0.0f);
                p.boxMaxAndPriority = glm::vec4(vector(source.at("boxMax")), source.value("priority", 1.0f));
                p.fadeAndIntensity = {source.value("blendDistance", 1.0f), source.value("intensity", 1.0f), 0, 0};
                p.regionAndFlags.y = source.value("enabled", true) ? 1u : 0u;
                probes.push_back(p);
            }
            if (!RunCase("requested-scene", probes, {}, Queries(probes))) return false;
        }
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ReflectionProbeGPU] " << error.what() << '\n';
        return false;
    }
}
