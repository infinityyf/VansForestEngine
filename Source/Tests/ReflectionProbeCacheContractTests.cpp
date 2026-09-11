#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeCache.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace VansGraphics;
    using Cache = VansReflectionProbeCache;
    using Data = VansReflectionProbeCacheData;
    void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
    void VK(VkResult result, const char* message) { Check(result == VK_SUCCESS, std::string(message) + ": " + std::to_string(result)); }
    uint32_t validationErrors = 0;
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        { ++validationErrors; std::cerr << "[ReflectionCache validation] " << message->pMessage << '\n'; }
        return VK_FALSE;
    }
    Data Blank(uint32_t resolution)
    {
        Data data; data.resolution = resolution; std::string error; uint64_t bytes = 0;
        std::vector<VansReflectionProbeCacheSurface> surfaces;
        Check(Cache::Describe(resolution, surfaces, bytes, data.mipCount, error), error);
        data.texels.resize(size_t(bytes)); return data;
    }
    uint32_t Word(const std::string& bytes, size_t index)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(bytes.data()) + index * 4;
        return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    void SetWord(std::string& bytes, size_t index, uint32_t value)
    { for (uint32_t b = 0; b < 4; ++b) bytes[index * 4 + b] = char(value >> (8 * b)); }
    struct OutputDirectory
    {
        const std::filesystem::path path = std::filesystem::current_path() / "LOG" /
            ("reflection_cache_contract_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()));
        OutputDirectory()
        {
            std::filesystem::create_directories(path.parent_path());
            Check(std::filesystem::create_directory(path), "Test output directory already exists");
        }
        OutputDirectory(const OutputDirectory&) = delete;
        OutputDirectory& operator=(const OutputDirectory&) = delete;
        ~OutputDirectory()
        {
            // 异常退出也清理本次测试独占创建的目录。
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        void Clear() const
        {
            std::filesystem::remove_all(path);
            Check(!std::filesystem::exists(path), "Test output cleanup failed");
        }
    };

    // 独立 Vulkan 装置执行生产 GGX shader；不启动编辑器，也不增加运行时格式分支。
    struct GPU
    {
        VkInstance instance = VK_NULL_HANDLE; VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE; VkQueue queue = VK_NULL_HANDLE; uint32_t family = 0;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
        VkCommandPool commandPool = VK_NULL_HANDLE; VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE; VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE; VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkShaderModule shader = VK_NULL_HANDLE; VkPipeline pipeline = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE; VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        std::vector<VkImageView> views; std::vector<VkDescriptorSet> sets;
        VkSampler sampler = VK_NULL_HANDLE; VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr; uint32_t resolution = 0, mips = 0, sourceMip = 0;
        std::vector<VkBufferImageCopy> regions; VkDeviceSize bytes = 0;
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ~GPU()
        {
            if (device)
            {
                vkDeviceWaitIdle(device); ClearImage();
                if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
                if (shader) vkDestroyShaderModule(device, shader, nullptr);
                if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr);
                if (fence) vkDestroyFence(device, fence, nullptr);
                if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
                vkDestroyDevice(device, nullptr);
            }
            if (messenger) destroyMessenger(instance, messenger, nullptr);
            if (instance) vkDestroyInstance(instance, nullptr);
        }
        void ClearImage()
        {
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE; sets.clear();
            for (auto view : views) vkDestroyImageView(device, view, nullptr);
            views.clear();
            if (sampler) vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE;
            if (image) vkDestroyImage(device, image, nullptr); image = VK_NULL_HANDLE;
            if (imageMemory) vkFreeMemory(device, imageMemory, nullptr); imageMemory = VK_NULL_HANDLE;
            if (mapped) vkUnmapMemory(device, memory); mapped = nullptr;
            if (buffer) vkDestroyBuffer(device, buffer, nullptr); buffer = VK_NULL_HANDLE;
            if (memory) vkFreeMemory(device, memory, nullptr); memory = VK_NULL_HANDLE;
            currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags)
        {
            VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(physical, &properties);
            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
            throw std::runtime_error("Required GPU memory type unavailable");
        }
        void Init()
        {
            if (!vulkan_library) vulkan_library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            Check(vulkan_library && LoadVulkanExportedFunction() && LoadVulkanGlobalLevelFunctions(), "Vulkan loader unavailable");
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app.pApplicationName = "Forest Reflection Cache Contract"; app.apiVersion = VK_API_VERSION_1_2;
            const char* layer = "VK_LAYER_KHRONOS_validation";
            const char* extensions[] = {"VK_EXT_debug_utils", "VK_EXT_validation_features"};
            VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            debug.pfnUserCallback = Validation;
            const VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
            VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
            validation.enabledValidationFeatureCount = 1; validation.pEnabledValidationFeatures = &sync; validation.pNext = &debug;
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
            info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer;
            info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions; info.pNext = &validation;
            VK(vkCreateInstance(&info, nullptr, &instance), "create instance");
            Check(LoadVulkanInstanceLevelFunctions(instance), "Load instance functions");
            auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            VK(createMessenger(instance, &debug, nullptr, &messenger), "create debug messenger");
            uint32_t count = 0; VK(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
            std::vector<VkPhysicalDevice> devices(count); VK(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "read devices");
            Check(!devices.empty(), "No GPU"); physical = devices.front();
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
            while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
            Check(family < count, "No compute queue");
            float priority = 1; VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
            VkPhysicalDeviceFeatures features{}; features.imageCubeArray = VK_TRUE;
            VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; d.queueCreateInfoCount = 1; d.pQueueCreateInfos = &q; d.pEnabledFeatures = &features;
            VK(vkCreateDevice(physical, &d, nullptr, &device), "create device"); Check(LoadVulkanDeviceLevelFunctions(device), "Load device functions");
            vkGetDeviceQueue(device, family, 0, &queue);
            VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.queueFamilyIndex = family; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VK(vkCreateCommandPool(device, &cp, nullptr, &commandPool), "create command pool");
            VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = commandPool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
            VK(vkAllocateCommandBuffers(device, &ca, &command), "allocate command");
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VK(vkCreateFence(device, &fi, nullptr, &fence), "create fence");
            VkDescriptorSetLayoutBinding bindings[] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
            VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dl.bindingCount = 2; dl.pBindings = bindings;
            VK(vkCreateDescriptorSetLayout(device, &dl, nullptr, &layout), "create descriptor layout");
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 20};
            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount = 1; pl.pSetLayouts = &layout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
            VK(vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout), "create pipeline layout");
            std::ifstream input("EngineAssets/Shaders/ReflectionProbePrefilter/ReflectionProbePrefiltercomp.spv", std::ios::binary | std::ios::ate);
            Check(input.good(), "Production prefilter shader missing"); auto length = input.tellg();
            Check(length > 0 && length % 4 == 0, "Invalid shader bytes"); std::vector<uint32_t> code(size_t(length) / 4); input.seekg(0); input.read(reinterpret_cast<char*>(code.data()), length);
            VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; sm.codeSize = size_t(length); sm.pCode = code.data();
            VK(vkCreateShaderModule(device, &sm, nullptr, &shader), "create shader");
            VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pi.layout = pipelineLayout;
            pi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
            VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline), "create prefilter pipeline");
        }
        void Resize(uint32_t size, uint32_t baseMip)
        {
            if (resolution == size && sourceMip == baseMip) return;
            ClearImage(); resolution = size; sourceMip = baseMip; std::vector<VansReflectionProbeCacheSurface> surfaces; std::string error;
            Check(Cache::Describe(size, surfaces, bytes, mips, error), error); regions.clear();
            Check(sourceMip < mips, "Workspace source mip outside image");
            for (const auto& s : surfaces)
            { VkBufferImageCopy r{}; r.bufferOffset = s.offset; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, s.mip, s.face, 1}; r.imageExtent = {s.size, s.size, 1}; regions.push_back(r); }
            VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_R16G16B16A16_SFLOAT; ic.extent = {size, size, 1}; ic.mipLevels = mips; ic.arrayLayers = 6;
            ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VK(vkCreateImage(device, &ic, nullptr, &image), "create cube");
            VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, image, &req);
            VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize = req.size; ma.memoryTypeIndex = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK(vkAllocateMemory(device, &ma, nullptr, &imageMemory), "allocate cube"); VK(vkBindImageMemory(device, image, imageMemory, 0), "bind cube");
            VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bc.size = bytes; bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VK(vkCreateBuffer(device, &bc, nullptr, &buffer), "create staging"); vkGetBufferMemoryRequirements(device, buffer, &req);
            ma.allocationSize = req.size; ma.memoryTypeIndex = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK(vkAllocateMemory(device, &ma, nullptr, &memory), "allocate staging"); VK(vkBindBufferMemory(device, buffer, memory, 0), "bind staging");
            VK(vkMapMemory(device, memory, 0, bytes, 0, &mapped), "map staging");
            VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sc.magFilter = sc.minFilter = VK_FILTER_LINEAR; sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sc.maxLod = float(mips - 1);
            VK(vkCreateSampler(device, &sc, nullptr, &sampler), "create sampler");
            for (uint32_t mip = 0; mip < mips; ++mip)
            {
                VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = image; vi.format = ic.format;
                vi.viewType = mip == sourceMip ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6}; VkImageView view;
                VK(vkCreateImageView(device, &vi, nullptr, &view), "create mip view"); views.push_back(view);
            }
            const uint32_t setCount = mips - sourceMip - 1u;
            if (!setCount) return;
            VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount}};
            VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pc.maxSets = setCount; pc.poolSizeCount = 2; pc.pPoolSizes = ps;
            VK(vkCreateDescriptorPool(device, &pc, nullptr, &descriptorPool), "create descriptor pool");
            std::vector<VkDescriptorSetLayout> layouts(setCount, layout); sets.resize(setCount);
            VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = descriptorPool; da.descriptorSetCount = setCount; da.pSetLayouts = layouts.data();
            VK(vkAllocateDescriptorSets(device, &da, sets.data()), "allocate sets");
            for (uint32_t mip = sourceMip + 1u; mip < mips; ++mip)
            {
                VkDescriptorImageInfo images[] = {{sampler, views[sourceMip], VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, views[mip], VK_IMAGE_LAYOUT_GENERAL}};
                VkWriteDescriptorSet w[2]{};
                for (uint32_t i = 0; i < 2; ++i)
                { w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = sets[mip - sourceMip - 1u]; w[i].dstBinding = i; w[i].descriptorCount = 1;
                  w[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[i].pImageInfo = &images[i]; }
                vkUpdateDescriptorSets(device, 2, w, 0, nullptr);
            }
        }
        void Begin()
        {
            VK(vkResetCommandBuffer(command, 0), "reset command"); VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; VK(vkBeginCommandBuffer(command, &bi), "begin command");
        }
        void Barrier(VkImageLayout next, VkPipelineStageFlags from, VkPipelineStageFlags to, VkAccessFlags read, VkAccessFlags write)
        {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image = image;
            b.oldLayout = currentLayout; b.newLayout = next; b.srcAccessMask = read; b.dstAccessMask = write;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 6};
            vkCmdPipelineBarrier(command, from, to, 0, 0, nullptr, 0, nullptr, 1, &b); currentLayout = next;
        }
        void Submit()
        {
            VK(vkEndCommandBuffer(command), "end command"); VK(vkResetFences(device, 1, &fence), "reset fence");
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &command;
            VK(vkQueueSubmit(queue, 1, &si, fence), "submit"); VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "wait fence");
        }
        Data Run(const Data& input, bool filter, uint32_t baseMip = 0)
        {
            Resize(input.resolution, baseMip); Check(input.texels.size() == bytes, "GPU input size"); std::memcpy(mapped, input.texels.data(), size_t(bytes)); Begin();
            const bool empty = currentLayout == VK_IMAGE_LAYOUT_UNDEFINED;
            Barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, empty ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, empty ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            std::vector<VkBufferImageCopy> copies;
            for (const auto& region : regions) if (!filter || region.imageSubresource.mipLevel <= baseMip) copies.push_back(region);
            vkCmdCopyBufferToImage(command, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(copies.size()), copies.data());
            if (filter && mips > baseMip + 1u)
            {
                Barrier(VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                struct Push { float roughness; uint32_t outputSize, cubeCount, sampleCount, baseCube; } p{};
                p.cubeCount = 1; p.sampleCount = 128;
                for (uint32_t mip = sourceMip + 1u; mip < mips; ++mip)
                {
                    p.roughness = float(mip - baseMip) / float(mips - baseMip - 1u); p.outputSize = resolution >> mip;
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &sets[mip - sourceMip - 1u], 0, nullptr);
                    vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p), &p);
                    vkCmdDispatch(command, (p.outputSize + 7) / 8, (p.outputSize + 7) / 8, 6);
                }
            }
            Barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            // 同一 staging 缓冲先上传再回读，先结束 transfer 读取。
            VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; reuse.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &reuse, 0, nullptr, 0, nullptr);
            vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, uint32_t(regions.size()), regions.data());
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
            Submit(); Data output = Blank(resolution); std::memcpy(output.texels.data(), mapped, size_t(bytes)); return output;
        }
    };
}

bool TestReflectionProbeCacheContract()
{
    try
    {
        std::string error, encoded; uint64_t totalBytes = 0, malformed = 0;
        for (uint32_t resolution : {1u, 8u, 32u, 128u, 512u})
        {
            Data data = Blank(resolution); std::vector<VansReflectionProbeCacheSurface> surfaces; uint64_t bytes; uint32_t mips;
            Check(Cache::Describe(resolution, surfaces, bytes, mips, error), error);
            uint64_t offset = 0;
            for (uint32_t face = 0; face < 6; ++face) for (uint32_t mip = 0, size = resolution; size; ++mip, size >>= 1)
            {
                const auto& s = surfaces[face * mips + mip];
                Check(s.face == face && s.mip == mip && s.size == size && s.offset == offset && s.bytes == uint64_t(size) * size * 8, "DDS face/mip order");
                for (uint64_t i = offset; i < offset + s.bytes; i += 2)
                { data.texels[size_t(i)] = uint8_t(face * 16 + mip); data.texels[size_t(i + 1)] = 0x38; }
                offset += s.bytes;
            }
            Check(Cache::Encode(data, encoded, error), error);
            Check(Word(encoded, 0) == 0x20534444 && Word(encoded, 1) == 124 && Word(encoded, 19) == 32 && Word(encoded, 21) == 0x30315844 &&
                Word(encoded, 32) == 10 && Word(encoded, 33) == 3 && Word(encoded, 34) == 4 && Word(encoded, 35) == 1 && encoded.size() == offset + 148, "DDS standard header");
            Data decoded; Check(Cache::Decode(encoded, decoded, error) && decoded.texels == data.texels, "Byte exact codec round trip");
            auto reject = [&](const std::string& bad) { Data preserved = data; Check(!Cache::Decode(bad, preserved, error) && preserved.texels == data.texels && preserved.resolution == resolution, "Malformed input replaced valid data"); ++malformed; };
            reject(encoded.substr(0, 147)); reject(encoded.substr(0, encoded.size() - 1)); reject(encoded + '\0');
            for (size_t word : {size_t(0),size_t(1),size_t(4),size_t(5),size_t(7),size_t(19),size_t(21),size_t(28),size_t(32),size_t(33),size_t(34),size_t(35)})
            { auto bad = encoded; SetWord(bad, word, 0); reject(bad); }
            auto bad = encoded; bad[149] = char(0x7c); reject(bad); bad[148] = 1; reject(bad);
            totalBytes += bytes;
        }
        OutputDirectory output; const auto file = output.path / Cache::FileName; Data data = Blank(8), loaded;
        Check(Cache::Save(file, data, error) && Cache::Load(file, loaded, error) && data.texels == loaded.texels, "Atomic save/load");
        HANDLE lock = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(lock != INVALID_HANDLE_VALUE, "Lock cache target"); Data changed = data; changed.texels[0] = 42;
        bool saved = Cache::Save(file, changed, error); CloseHandle(lock);
        Check(!saved && Cache::Load(file, loaded, error) && loaded.texels == data.texels, "Failed atomic replacement lost old cache");
        changed.texels[1] = 0x7c; Check(!Cache::Save(file, changed, error), "Non-finite save accepted");
        Check(Cache::Load(file, loaded, error) && loaded.texels == data.texels, "Rejected save changed target");
        output.Clear();
        std::cout << "Reflection cache CPU PASS resolutions=5 payloadBytes=" << totalBytes << " malformed=" << malformed << " atomicFailurePreservesTarget=1\n";
        return true;
    }
    catch (const std::exception& e) { std::cerr << "Reflection cache CPU FAIL: " << e.what() << '\n'; return false; }
}

bool TestReflectionProbeCacheGpuContract()
{
    validationErrors = 0;
    try
    {
        uint64_t checkedBytes = 0, count = 0, workspaceCases = 0; OutputDirectory output; const auto& root = output.path;
        {
            GPU gpu; gpu.Init(); std::string error;
            auto run = [&](const Data& input, const std::filesystem::path& file, bool constant) {
                Data filtered = gpu.Run(input, true);
                if (constant)
                {
                    // 过滤浮点累积允许 1 个 half 步长；下面缓存/GPU 往返仍严格逐字节比较。
                    uint32_t maxHalfSteps = 0;
                    for (size_t i = 0; i < filtered.texels.size(); i += 2)
                    {
                        int value = filtered.texels[i] | (int(filtered.texels[i + 1]) << 8);
                        maxHalfSteps = (std::max)(maxHalfSteps, uint32_t(std::abs(value - 0x3c00)));
                    }
                    std::cout << "[ReflectionCacheGPU] constant resolution=" << input.resolution << " maxHalfSteps=" << maxHalfSteps << '\n';
                    Check(maxHalfSteps <= 1, "Constant radiance exceeded half precision during production GGX");
                }
                Check(Cache::Save(file, filtered, error), error); Data loaded;
                Check(Cache::Load(file, loaded, error) && loaded.texels == filtered.texels, "Saved mip chain changed");
                Data uploaded = gpu.Run(loaded, false); Check(uploaded.texels == filtered.texels, "GPU complete mip upload/readback changed texels");
                checkedBytes += filtered.texels.size(); ++count;
                if (!constant && input.resolution <= 128)
                {
                    for (uint32_t baseMip : {1u, 2u})
                    {
                        Data workspace = Blank(input.resolution << baseMip);
                        // 大 mip 使用不同值；验证复用工作图像时不会从错误的 mip 读取或覆盖前缀。
                        for (size_t i = 1; i < workspace.texels.size(); i += 2) workspace.texels[i] = 0x38;
                        std::vector<VansReflectionProbeCacheSurface> probeSurfaces, workspaceSurfaces; uint64_t ignoredBytes; uint32_t ignoredMips;
                        Check(Cache::Describe(input.resolution, probeSurfaces, ignoredBytes, ignoredMips, error), error);
                        Check(Cache::Describe(workspace.resolution, workspaceSurfaces, ignoredBytes, ignoredMips, error), error);
                        for (const auto& surface : probeSurfaces)
                        {
                            const auto& target = workspaceSurfaces[surface.face * workspace.mipCount + baseMip + surface.mip];
                            std::memcpy(workspace.texels.data() + target.offset, input.texels.data() + surface.offset, size_t(surface.bytes));
                        }
                        Data workspaceFiltered = gpu.Run(workspace, true, baseMip);
                        for (const auto& surface : workspaceSurfaces)
                        {
                            const uint8_t* expected = workspace.texels.data() + surface.offset;
                            if (surface.mip >= baseMip)
                            {
                                const auto& source = probeSurfaces[surface.face * input.mipCount + surface.mip - baseMip];
                                expected = filtered.texels.data() + source.offset;
                            }
                            Check(std::memcmp(expected, workspaceFiltered.texels.data() + surface.offset, size_t(surface.bytes)) == 0,
                                "Workspace mip filtering differs from standalone cube or changed larger mips");
                        }
                        checkedBytes += workspaceFiltered.texels.size(); ++workspaceCases;
                        std::cout << "[ReflectionCacheGPU] workspace=" << workspace.resolution << " sourceMip=" << baseMip
                            << " probeResolution=" << input.resolution << " exactFilterAndUntouchedPrefix=1\n";
                    }
                }
                std::cout << "[ReflectionCacheGPU] checked=" << count << " resolution=" << input.resolution << " bytes=" << filtered.texels.size() << " output=" << file.string() << std::endl;
            };
            for (uint32_t size : {1u, 8u, 32u, 128u, 512u})
            {
                Data input = Blank(size);
                for (size_t i = 1; i < input.texels.size(); i += 2) input.texels[i] = 0x3c;
                run(input, root / ("constant_" + std::to_string(size) + ".dds"), true);
                // 六面空间梯度和高亮点，避免常量场掩盖 mip/方向错误。
                for (size_t i = 0; i < input.texels.size(); i += 8) { input.texels[i] = uint8_t(i / 8); input.texels[i + 1] = uint8_t(0x30 + (i / 8 % 20)); }
                run(input, root / ("gradient_" + std::to_string(size) + ".dds"), false);
            }
        }
        Check(validationErrors == 0, "Vulkan validation errors including resource destruction");
        output.Clear();
        std::cout << "Reflection cache GPU PASS cubes=" << count << " workspaceCases=" << workspaceCases << " comparedBytes=" << checkedBytes << " validationErrors=" << validationErrors << " loadedPrefilterDispatches=0\n";
        return true;
    }
    catch (const std::exception& e) { std::cerr << "Reflection cache GPU FAIL: " << e.what() << " validationErrors=" << validationErrors << '\n'; return false; }
}
