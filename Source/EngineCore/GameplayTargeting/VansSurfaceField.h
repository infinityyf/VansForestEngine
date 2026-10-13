#pragma once
#include "../RuntimeCore/VansStableIdentity.h"
#include <glm/glm.hpp>
#include <array>
#include <unordered_map>

namespace Vans
{
// VFX owns placement and lifetime. Gameplay reads copies of the same supported
// surface cells; edge/climbing sprites never create extra damage zones.
struct VansSurfaceFieldCell
{
    glm::vec3 position{0}, normal{0,1,0};
    double bornTime = 0;
    bool active = true;
};
struct VansSurfaceFieldSnapshot
{
    static constexpr std::size_t MaximumCells = 16;
    VansEntityHandle owner;
    glm::vec3 origin{0};
    double age = 0, duration = 0;
    float radius = 0;
    bool active = false;
    std::size_t cellCount = 0;
    std::array<VansSurfaceFieldCell,MaximumCells> cells{};
};
class VansSurfaceFieldRegistry
{
public:
    static constexpr std::size_t MaximumFields = 16;
    bool Publish(VansGenerationHandle handle, const VansSurfaceFieldSnapshot& field)
    {
        if (!handle.IsValid() || field.cellCount > field.cells.size()) return false;
        const auto key = Key(handle);
        if (m_Fields.find(key)==m_Fields.end() && m_Fields.size() >= MaximumFields) return false;
        m_Fields.insert_or_assign(key, field);
        return true;
    }
    bool Read(VansGenerationHandle handle, VansSurfaceFieldSnapshot& field) const
    {
        const auto found = m_Fields.find(Key(handle));
        if (found == m_Fields.end()) return false;
        field = found->second;
        return true;
    }
    void Remove(VansGenerationHandle handle) { m_Fields.erase(Key(handle)); }
private:
    static std::uint64_t Key(VansGenerationHandle handle)
    { return (std::uint64_t(handle.generation) << 32) | handle.index; }
    std::unordered_map<std::uint64_t,VansSurfaceFieldSnapshot> m_Fields;
};
}
