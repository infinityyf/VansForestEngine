#include "VansShaderRegistry.h"
#include "../Util/VansLog.h"

namespace VansGraphics
{

VansShaderRegistry& VansShaderRegistry::Get()
{
    static VansShaderRegistry instance;
    return instance;
}

void VansShaderRegistry::RegisterForType(VansMaterialType type, VansShaderRegistryEntry entry)
{
    int key = static_cast<int>(type);
    if (m_TypedEntries.count(key))
    {
        VANS_LOG_WARN("[VansShaderRegistry] RegisterForType: type " << key
            << " is already registered as '" << m_TypedEntries[key].name
            << "'. Second registration ('" << entry.name << "') ignored.");
        return;
    }
    m_TypedEntries[key] = std::move(entry);
}

void VansShaderRegistry::RegisterNamed(VansShaderRegistryEntry entry)
{
    const std::string name = entry.name;
    if (m_NamedEntries.count(name))
    {
        VANS_LOG_WARN("[VansShaderRegistry] RegisterNamed: '" << name
            << "' is already registered. Second registration ignored.");
        return;
    }
    m_NamedEntries[name] = std::move(entry);
}

const VansShaderRegistryEntry* VansShaderRegistry::FindForType(VansMaterialType type) const
{
    auto it = m_TypedEntries.find(static_cast<int>(type));
    if (it == m_TypedEntries.end())
        return nullptr;
    return &it->second;
}

void VansShaderRegistry::ForEach(const std::function<void(const VansShaderRegistryEntry&)>& fn) const
{
    for (const auto& pair : m_TypedEntries)
        fn(pair.second);
    for (const auto& pair : m_NamedEntries)
        fn(pair.second);
}

void VansShaderRegistry::Clear()
{
    m_TypedEntries.clear();
    m_NamedEntries.clear();
}

} // namespace VansGraphics
