#include "VansShaderRegistry.h"
#include "../Util/VansLog.h"

namespace VansGraphics
{

const std::unordered_map<std::string, std::string> VansShaderRegistry::s_EmptyPassMap;

VansShaderRegistry& VansShaderRegistry::Get()
{
    static VansShaderRegistry instance;
    return instance;
}

// ── New Shader Table API ──────────────────────────────────────────────────────

void VansShaderRegistry::RegisterShader(const std::string& shaderName, VansShaderRegistryEntry entry)
{
    if (m_Shaders.count(shaderName))
    {
        VANS_LOG_WARN("[VansShaderRegistry] RegisterShader: '" << shaderName
            << "' is already registered. Second registration ignored.");
        return;
    }
    m_Shaders[shaderName] = std::move(entry);
}

const VansShaderRegistryEntry* VansShaderRegistry::FindShader(const std::string& shaderName) const
{
    auto it = m_Shaders.find(shaderName);
    if (it == m_Shaders.end())
        return nullptr;
    return &it->second;
}

// ── New Material Pass Table API ───────────────────────────────────────────────

void VansShaderRegistry::RegisterMaterialPasses(VansMaterialType type,
                                                 std::unordered_map<std::string, std::string> passMap)
{
    int key = static_cast<int>(type);
    if (m_MaterialPasses.count(key))
    {
        VANS_LOG_WARN("[VansShaderRegistry] RegisterMaterialPasses: type " << key
            << " already registered. Overwriting.");
    }
    m_MaterialPasses[key] = std::move(passMap);
}

const std::unordered_map<std::string, std::string>&
VansShaderRegistry::GetMaterialPassMap(VansMaterialType type) const
{
    auto it = m_MaterialPasses.find(static_cast<int>(type));
    if (it == m_MaterialPasses.end())
        return s_EmptyPassMap;
    return it->second;
}

void VansShaderRegistry::ForEachShader(const std::function<void(const VansShaderRegistryEntry&)>& fn) const
{
    for (const auto& pair : m_Shaders)
        fn(pair.second);
}

void VansShaderRegistry::Clear()
{
    m_Shaders.clear();
    m_MaterialPasses.clear();
}

} // namespace VansGraphics
