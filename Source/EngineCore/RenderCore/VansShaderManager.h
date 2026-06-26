#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "vulkan/vulkan.h"
#include "VulkanCore/VansShader.h"
#include "VansMaterial.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class VansAssetsFileWatcher;

namespace VansGraphics
{
    enum class VansManagedShaderKind
    {
        Graphics,
        Compute,
        RayTracing
    };

    enum class VansShaderStatus
    {
        Unloaded,
        Valid,
        Fallback,
        Broken
    };

    struct VansShaderEntry
    {
        std::string name;
        std::string relativePath;
        VkBool32 depthTest = VK_TRUE;
        VkBool32 depthWrite = VK_TRUE;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        int pushConstantSize = 0;
        bool enableAlphaBlend = false;
        bool enableDecalBlend = false;
        int colorAttachmentCount = -1;
        VansManagedShaderKind kind = VansManagedShaderKind::Graphics;
    };

    struct VansShaderRecord
    {
        VansShaderEntry entry;
        VansShaderStatus status = VansShaderStatus::Unloaded;
        std::string lastError;
        std::unique_ptr<VansShader> shader;
    };

    class VansShaderManager
    {
    public:
        static VansShaderManager& Get();

        void RegisterShader(VansShaderEntry entry);
        void RegisterGraphicsShader(const std::string& shaderName, VansShaderEntry entry);
        void RegisterComputeShader(const std::string& shaderName, const std::string& relativePath, int pushConstantSize = 0);
        void RegisterRayTracingShader(const std::string& shaderName, const std::string& relativePath, int pushConstantSize = 0);

        const VansShaderEntry* FindShaderEntry(const std::string& shaderName) const;
        VansShader* FindShader(const std::string& shaderName) const;
        VansGraphicsShader* FindGraphicsShader(const std::string& shaderName) const;
        VansComputeShader* FindComputeShader(const std::string& shaderName) const;
        VansRayTracingShader* FindRayTracingShader(const std::string& shaderName) const;

        void RegisterMaterialPasses(VansMaterialType type, std::unordered_map<std::string, std::string> passMap);
        const std::unordered_map<std::string, std::string>& GetMaterialPassMap(VansMaterialType type) const;

        bool LoadAll(const std::string& pathPrefix, VkDevice& device);
        bool ReloadShader(const std::string& shaderName);
        void ReloadUpdatedShaders(VansAssetsFileWatcher& watcher);

        void ForEachShader(const std::function<void(const VansShaderRecord&)>& fn) const;
        std::vector<VansShader*> GetLoadedShaderAssets() const;

        void Clear();

    private:
        VansShaderManager() = default;

        bool LoadShaderRecord(VansShaderRecord& record, const std::string& pathPrefix, VkDevice& device);
        void ApplyGraphicsState(VansGraphicsShader& shader, const VansShaderEntry& entry);

        std::unordered_map<std::string, VansShaderRecord> m_Shaders;
        std::unordered_map<int, std::unordered_map<std::string, std::string>> m_MaterialPasses;

        static const std::unordered_map<std::string, std::string> s_EmptyPassMap;
    };
}

void RegisterEngineShaders();
