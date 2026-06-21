#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vulkan/vulkan.h"
#include "VansMaterial.h"
#include <string>
#include <functional>
#include <unordered_map>

namespace VansGraphics
{
    // ---------------------------------------------------------------------------
    // Per-shader metadata stored in the registry.
    // ---------------------------------------------------------------------------
    struct VansShaderRegistryEntry
    {
        std::string      name;                                          // e.g. "Unlit"
        std::string      relativePath;                                  // e.g. "EngineAssets/Shaders/UnLit"
        VkBool32         depthTest      = VK_TRUE;
        VkBool32         depthWrite     = VK_TRUE;
        VkCompareOp      depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkCullModeFlags  cullMode       = VK_CULL_MODE_BACK_BIT;
        int              pushConstantSize = 0;
        bool             enableAlphaBlend = false;
        // 贴花专用：3 附件 MRT Alpha Blend，GBuffer1 写入掩码仅 R+G
        bool             enableDecalBlend = false;
    };

    // ---------------------------------------------------------------------------
    // VansShaderRegistry — singleton with two lookup tables:
    //   Shader Table:        shader name → VansShaderRegistryEntry
    //   Material Pass Table: VansMaterialType → { pass name → shader name }
    // ---------------------------------------------------------------------------
    class VansShaderRegistry
    {
    public:
        static VansShaderRegistry& Get();

        // ── Shader Table ───────────────────────────────────────────────────────
        // Register a shader by its unique name.
        void RegisterShader(const std::string& shaderName, VansShaderRegistryEntry entry);

        // Lookup shader entry by name. Returns nullptr if not registered.
        const VansShaderRegistryEntry* FindShader(const std::string& shaderName) const;

        // ── Material Pass Table ────────────────────────────────────────────────
        // Register which passes a material type participates in and which shader each pass uses.
        // passMap: { pass name → shader name }
        void RegisterMaterialPasses(VansMaterialType type,
                                    std::unordered_map<std::string, std::string> passMap);

        // Get the pass→shader map for a material type. Returns empty map if not registered.
        const std::unordered_map<std::string, std::string>&
            GetMaterialPassMap(VansMaterialType type) const;

        // ── Iteration ──────────────────────────────────────────────────────────
        // Iterate all shaders for bulk loading at scene startup.
        void ForEachShader(const std::function<void(const VansShaderRegistryEntry&)>& fn) const;

        void Clear();

    private:
        VansShaderRegistry() = default;

        // shader name → shader entry
        std::unordered_map<std::string, VansShaderRegistryEntry>               m_Shaders;
        // material type → { pass name → shader name }
        std::unordered_map<int, std::unordered_map<std::string, std::string>>  m_MaterialPasses;

        static const std::unordered_map<std::string, std::string> s_EmptyPassMap;
    };

} // namespace VansGraphics

// ---------------------------------------------------------------------------
// Free function — defined in VansShaderRegistrations.cpp.
// Call once before uploading an AssetDatabase dependency closure.
// Duplicate calls are safe — the registry ignores re-registration of the
// same type.
// ---------------------------------------------------------------------------
void RegisterEngineShaders();
