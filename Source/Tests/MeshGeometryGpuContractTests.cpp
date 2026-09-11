#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/GeometryCore/VansMeshGeometryReadback.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMesh.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <crtdbg.h>

namespace
{
    using namespace VansGraphics;
    uint32_t errors = 0;
    void Check(bool value, const std::string& message)
    {
        if (!value) { std::cerr << "[MeshGeometryGPU] check failed: " << message << std::endl; throw std::runtime_error(message); }
    }
    struct Window final : INativeWindowProvider
    {
        GLFWwindow* handle = nullptr;
        ~Window() { if (handle) glfwDestroyWindow(handle); glfwTerminate(); }
        void* GetNativeWindowHandle() const override { return handle; }
    };
    struct Messenger
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT destroy = nullptr;
        ~Messenger() { if (handle && destroy) destroy(instance, handle, nullptr); }
    };
    VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        { ++errors; std::cerr << "[MeshGeometryGPU validation] " << message->pMessage << '\n'; }
        return VK_FALSE;
    }
    struct InputFile
    {
        std::filesystem::path path;
        ~InputFile() { std::error_code error; if (!path.empty()) std::filesystem::remove(path, error); }
    };

    void CheckBufferRecreation(VansVKDevice& device)
    {
        struct OwnedBuffer
        {
            VkDevice device;
            VansVKBuffer buffer;
            ~OwnedBuffer() { buffer.DestroyVulkanBuffer(device); }
        } owned{device.GetLogicDevice()};
        // 复现索引随布局缩小、恢复和扩容；只用真实 VMA 分配和写入检查生命周期。
        for (const uint32_t size : {4096u, 256u, 4096u, 8192u})
        {
            Check(owned.buffer.GetNativeBuffer() == VK_NULL_HANDLE && owned.buffer.GetBufferSize() == 0 &&
                owned.buffer.GetNativeAllocation() == nullptr && !owned.buffer.IsMapped(), "Destroyed buffer retained live capacity or mapping");
            Check(owned.buffer.CreatVulkanBuffer(owned.device, size, VK_FORMAT_R32_UINT,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "Buffer recreation allocation failed");
            std::vector<uint8_t> bytes(size, static_cast<uint8_t>(size / 256u));
            Check(owned.buffer.SetBufferData(bytes.data(), 0, bytes.size()), "Recreated buffer upload failed");
            Check(owned.buffer.IsMapped() && std::memcmp(owned.buffer.GetMappedPtr(), bytes.data(), size) == 0,
                "Recreated buffer did not retain uploaded data");
            owned.buffer.DestroyVulkanBuffer(owned.device);
            owned.buffer.DestroyVulkanBuffer(owned.device);
            Check(!owned.buffer.SetBufferData(bytes.data(), 0, bytes.size()), "Destroyed buffer accepted an upload");
        }
        Check(owned.buffer.GetBufferSize() == 0, "Final buffer destruction retained capacity");
        std::cout << "[BufferLifetimeGPU] PASS: 4096 -> 256 -> 4096 -> 8192 bytes, writes, double destruction, empty capacity" << std::endl;
    }
}

bool TestMeshGeometryGpuContract(bool deviceOnly)
{
    try
    {
#ifndef _DEBUG
        Check(false, "This native Vulkan validation contract requires a Debug build");
#endif
        errors = 0;
        std::cout << "[MeshGeometryGPU] PID=" << GetCurrentProcessId() << " deviceOnly=" << deviceOnly << std::endl;
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        const char* layers = std::getenv("VK_INSTANCE_LAYERS");
        Check(layers && std::string(layers).find("VK_LAYER_KHRONOS_validation") != std::string::npos,
            "Run this contract with VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation");
        Window window;
        Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window.handle = glfwCreateWindow(64, 64, "Forest mesh geometry contract", nullptr, nullptr);
        Check(window.handle != nullptr, "Hidden Vulkan window creation failed");
        auto device = std::make_unique<VansVKDevice>(VkExtent2D{64, 64}, &window);
        Check(device->IsInitialized(), "Native engine Vulkan device initialization failed");
        Messenger messenger{device->GetInstance()};
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(messenger.instance, "vkCreateDebugUtilsMessengerEXT"));
        messenger.destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(messenger.instance, "vkDestroyDebugUtilsMessengerEXT"));
        Check(createMessenger && messenger.destroy, "Vulkan debug-utils extension is unavailable");
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        info.pfnUserCallback = Validation;
        Check(createMessenger(messenger.instance, &info, nullptr, &messenger.handle) == VK_SUCCESS, "Validation messenger creation failed");
        CheckBufferRecreation(*device);
        if (!deviceOnly)
        {
            struct Vertex { glm::vec3 position, normal; };
            std::array<Vertex, 3> vertices{{{{-2, -12, -2}, {0, 1, 0}}, {{2, -12, -2}, {0, 1, 0}}, {{-2, -12, 2}, {0, 1, 0}}}};
            const std::array<uint32_t, 3> indices{{0, 1, 2}};
            VansMesh procedural;
            procedural.InitFromRawData(device->GetLogicDevice(), vertices.data(), 3, sizeof(Vertex), indices.data(), 3,
                {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}},
                {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)}});

            InputFile input;
            input.path = std::filesystem::temp_directory_path() / ("ForestGeometryContract_" + std::to_string(GetCurrentProcessId()) + ".obj");
            { std::ofstream file(input.path); file << "v -2 -12 -2\nv 2 -12 -2\nv -2 -12 2\nvn 0 1 0\nf 1//1 2//1 3//1\n"; Check(bool(file), "Mesh test input write failed"); }
            VansMesh imported(false, false);
            imported.LoadMesh(device->GetLogicDevice(), device->GetGraphicsQueue(), &device->GetImmediateGraphicsCommandBuffer(), input.path.string());
            std::cerr << "[MeshGeometryGPU] Native mesh upload finished" << std::endl;
            Check(!imported.HasCPUPlacementData(), "Imported mesh unexpectedly retained CPU placement data");
            Check(imported.GetIndexBufferParameter().IndexType == VK_INDEX_TYPE_UINT16, "Imported mesh must exercise 16-bit indices");
            Check(device->WaitForIdle(), "GPU idle failed");
            std::cerr << "[MeshGeometryGPU] Device idle finished" << std::endl;
            std::vector<VansMeshGeometryData> output;
            std::string error;
            Check(VansMeshGeometryReadback::Read(*device, {&procedural, &imported}, output, error), error);
            std::cerr << "[MeshGeometryGPU] Batched readback finished" << std::endl;
            Check(output.size() == 2, "Batch readback lost a mesh");
            for (const auto& mesh : output)
            {
                Check(mesh.positions.size() == 3 && mesh.normals.size() == 3 && mesh.indices.size() == 3, "Readback geometry sizes");
                for (uint32_t i = 0; i < 3; ++i)
                {
                    Check(glm::length(mesh.positions[mesh.indices[i]] - vertices[i].position) < 0.0001f, "Decoded position/index mismatch");
                    Check(glm::length(mesh.normals[mesh.indices[i]] - vertices[i].normal) < 0.0001f, "Decoded normal mismatch");
                }
            }
            // 再次读取必须反映当前 GPU 内容，不可使用先前导入数据冒充有效快照。
            vertices[0].position.x = -3.0f;
            Check(procedural.GetBLASVertexBuffer().SetBufferData(vertices.data(), 0, sizeof(vertices)), "GPU vertex mutation failed");
            Check(VansMeshGeometryReadback::Read(*device, {&procedural}, output, error), error);
            Check(output[0].positions[0].x == -3.0f, "Readback did not observe current GPU data");
            procedural.m_VertexInputAttributeDescriptions[0].format = VK_FORMAT_R8G8B8_UNORM;
            Check(!VansMeshGeometryReadback::Read(*device, {&procedural}, output, error) && output.empty() && !error.empty(), "Unsupported geometry must fail atomically");
        }
        Check(errors == 0, "Vulkan validation errors observed during geometry readback");
        std::cout << (deviceOnly ? "[MeshGeometryGPU] DEVICE BASELINE READY: validationErrors=0"
            : "[MeshGeometryGPU] PASS: native mesh upload/readback, float16/float32 vertices, uint16/uint32 indices, no retained CPU data, changed GPU contents, atomic failure, validationErrors=0") << std::endl;
        return true;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[MeshGeometryGPU] FAIL: " << exception.what() << '\n';
        return false;
    }
}
