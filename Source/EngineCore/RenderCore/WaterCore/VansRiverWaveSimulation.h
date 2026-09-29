#pragma once
#include "../../PcgCore/VansPcgSplineField.h"
#include <array>

namespace VansGraphics
{
// 河流局部世界空间波包；只消费发布完成的 Mask，不依赖作者样条或编辑器。
class VansRiverWaveSimulation
{
public:
    static constexpr int MaxParticles=768, GridSide=32;
    static constexpr int CarrierCount=3, PacketVectorCount=1+CarrierCount;
    static constexpr float CellSize=8.f;
    static constexpr std::size_t MaxGpuVectors=2+GridSide*GridSide+MaxParticles*9*PacketVectorCount;
    struct Statistics
    {
        int activeParticles=0;
        int visibleSpawnSites=0;
        int spawnAttempts=0;
        int successfulSpawns=0;
        std::array<int,3> activeByScale{};
    };
    void Update(float delta,const Vans::VansPcgSplineFieldSnapshot* field,glm::vec2 camera,float wavelength,float lifetime);
    const std::vector<glm::vec4>& GpuData() const { return m_GpuData; }
    const Statistics& GetStatistics() const { return m_Statistics; }
    void Clear();
private:
    struct Carrier {float waveNumber=0,phase=0,heading=0;};
    struct Particle
    {
        glm::vec2 position{},direction{1,0};
        float age=0,lifetime=0,radius=0;
        std::array<Carrier,CarrierCount> carriers{};
        // 每个波包固定的小角度差异随局部流向一起转弯，避免平行波峰横贯河面。
        float headingOffset=0;
        float amplitude=1;
        int scale=0;
    };
    float Random();
    void Step(float dt,const Vans::VansPcgSplineFieldSnapshot& field,glm::vec2 camera,float wavelength,float lifetime);
    void Pack(glm::vec2 camera);
    void RefreshSpawnSites(const Vans::VansPcgSplineFieldSnapshot& field,glm::vec2 camera);
    std::array<Particle,MaxParticles> m_Particles{};
    std::vector<glm::vec4> m_GpuData;
    std::vector<glm::vec2> m_SpawnSites;
    std::vector<std::size_t> m_VisibleSpawnSites;
    const Vans::VansPcgSplineFieldSnapshot* m_SpawnField=nullptr;
    std::uint64_t m_SpawnSourceFingerprint=0;
    std::uint64_t m_SpawnTerrainFingerprint=0;
    glm::vec2 m_SpawnCenter{};
    bool m_SpawnRegionValid=false;
    Statistics m_Statistics{};
    std::uint32_t m_Random=1439857;
    float m_Accumulator=0;
};
}
