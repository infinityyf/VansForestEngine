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
    // VansMaterialType is used as the key in the typed table, so no
    // targetMaterialTypes list is needed here.
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
    };

    // ---------------------------------------------------------------------------
    // VansShaderRegistry — singleton with two lookup tables:
    //   m_TypedEntries  : VansMaterialType -> entry  (one entry per type)
    //   m_NamedEntries  : name string      -> entry  (shaders without type binding)
    // ---------------------------------------------------------------------------
    class VansShaderRegistry
    {
    public:
        static VansShaderRegistry& Get();

        // Register a shader that is the sole default for a specific material type.
        // Warns and ignores if the same type is registered twice.
        void RegisterForType(VansMaterialType type, VansShaderRegistryEntry entry);

        // Register a shader that has no material-type binding (loaded by name only).
        void RegisterNamed(VansShaderRegistryEntry entry);

        // O(1) lookup — returns nullptr if this type has no registered shader.
        const VansShaderRegistryEntry* FindForType(VansMaterialType type) const;

        // Iterate all entries (typed + named) for the scene-load shader loop.
        // Already-loaded shaders are skipped via the caller's GetShaderAsset check.
        void ForEach(const std::function<void(const VansShaderRegistryEntry&)>& fn) const;

        // Clear all registrations (useful for re-initialisation in tests).
        void Clear();

    private:
        VansShaderRegistry() = default;

        std::unordered_map<int, VansShaderRegistryEntry>         m_TypedEntries; // key = (int)VansMaterialType
        std::unordered_map<std::string, VansShaderRegistryEntry> m_NamedEntries;
    };

} // namespace VansGraphics

// ---------------------------------------------------------------------------
// Free function — defined in VansShaderRegistrations.cpp.
// Call once (e.g. at the start of LoadScene) to populate the registry.
// Duplicate calls are safe — the registry ignores re-registration of the
// same type.
// ---------------------------------------------------------------------------
void RegisterEngineShaders();
