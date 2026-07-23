#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "vulkan/vulkan.h"
#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansPipelineDescriptor.h"
#include "VansMaterial.h"

#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
        bool enableAdditiveBlend = false;
        uint32_t additiveBlendAttachmentMask = 0;
        bool enablePremultipliedAlphaBlend = false;
        VansManagedShaderKind kind = VansManagedShaderKind::Graphics;
        std::map<VkShaderStageFlagBits, std::string> explicitStageFiles;
        std::vector<std::string> materialPasses;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        uint32_t patchControlPoints = 1;
    };

    struct VansShaderRecord
    {
        VansShaderEntry entry;
        VansShaderStatus status = VansShaderStatus::Unloaded;
        std::string lastError;
        VansPipelineProgramDesc pipelineDesc;
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
        const VansPipelineProgramDesc* FindPipelineDesc(const std::string& shaderName) const;
        VansShader* FindShader(const std::string& shaderName) const;
        VansGraphicsShader* FindGraphicsShader(const std::string& shaderName) const;
        VansComputeShader* FindComputeShader(const std::string& shaderName) const;
        VansRayTracingShader* FindRayTracingShader(const std::string& shaderName) const;

        void RegisterMaterialPasses(VansMaterialType type, std::unordered_map<std::string, std::string> passMap);
        const std::unordered_map<std::string, std::string>& GetMaterialPassMap(VansMaterialType type) const;

		bool LoadAll(const std::string& pathPrefix, VkDevice& device);
		bool ApplyCompiledShaderCandidate(
			const std::string& shaderName,
			const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stageSpirv,
			std::string& error);

        void ForEachShader(const std::function<void(const VansShaderRecord&)>& fn) const;
        std::vector<VansShader*> GetLoadedShaderAssets() const;
		bool ExportCookedShaderArtifacts(const std::string& destinationRoot, std::string& error) const;
        bool ConfigureGraphicsShader(VansGraphicsShader& shader, const std::string& shaderName, const std::string& fullPath) const;
        bool ConfigureGraphicsShader(VansGraphicsShader& shader, const VansShaderEntry& entry, const std::string& fullPath) const;

        void ReleaseLoadedShaderAssets();
        void Clear();

    private:
        VansShaderManager() = default;

        bool LoadShaderRecord(VansShaderRecord& record, const std::string& pathPrefix, VkDevice& device);
        VansPipelineProgramDesc BuildPipelineDesc(const VansShaderEntry& entry, const std::string& fullPath) const;
        void ApplyGraphicsState(VansGraphicsShader& shader, const VansPipelineProgramDesc& desc) const;

        std::unordered_map<std::string, VansShaderRecord> m_Shaders;
        std::unordered_map<int, std::unordered_map<std::string, std::string>> m_MaterialPasses;

        static const std::unordered_map<std::string, std::string> s_EmptyPassMap;
    };
}

void RegisterEngineShaders();
