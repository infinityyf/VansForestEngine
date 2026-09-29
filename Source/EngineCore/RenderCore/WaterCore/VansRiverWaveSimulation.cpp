#include "VansRiverWaveSimulation.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
constexpr float Pi=3.14159265359f;
glm::vec2 Rotate(glm::vec2 v,float angle)
{
    const float c=std::cos(angle),s=std::sin(angle);
    return {c*v.x-s*v.y,s*v.x+c*v.y};
}
}
float VansRiverWaveSimulation::Random()
{
    m_Random^=m_Random<<13;m_Random^=m_Random>>17;m_Random^=m_Random<<5;
    return float(m_Random&0xffffff)/16777216.f;
}
void VansRiverWaveSimulation::Clear()
{
    m_Particles={};m_Accumulator=0;m_GpuData.assign(2+GridSide*GridSide,glm::vec4(0));
    m_SpawnSites.clear();m_VisibleSpawnSites.clear();m_SpawnField=nullptr;
    m_SpawnSourceFingerprint=0;m_SpawnTerrainFingerprint=0;
    m_SpawnRegionValid=false;m_Statistics={};
}
void VansRiverWaveSimulation::RefreshSpawnSites(const Vans::VansPcgSplineFieldSnapshot& field,glm::vec2 camera)
{
    if(m_SpawnField!=&field || m_SpawnSourceFingerprint!=field.sourceFingerprint ||
        m_SpawnTerrainFingerprint!=field.terrainFingerprint)
    {
        m_SpawnField=&field;m_SpawnSourceFingerprint=field.sourceFingerprint;
        m_SpawnTerrainFingerprint=field.terrainFingerprint;
        m_SpawnSites.clear();m_SpawnRegionValid=false;
        const glm::vec2 fieldOrigin(-field.worldSize*.5f);
        for(const auto& [key,tile]:field.tiles)
        {
            if(!tile || !tile->hasRiver)continue;
            for(std::uint32_t z=0;z<Vans::VANS_SPLINE_TILE_SIZE;++z)
                for(std::uint32_t x=0;x<Vans::VANS_SPLINE_TILE_SIZE;++x)
                {
                    const std::size_t at=std::size_t(z+Vans::VANS_SPLINE_TILE_BORDER)*
                        Vans::VANS_SPLINE_TILE_EXTENT+x+Vans::VANS_SPLINE_TILE_BORDER;
                    if(at>=tile->coverage.size() || at>=tile->riverProperties.size() ||
                        at>=tile->velocities.size() || at>=tile->waterBlend.size() ||
                        tile->coverage[at].y<=.5f || tile->riverProperties[at].x<.5f ||
                        tile->waterBlend[at]<.5f ||
                        glm::dot(tile->velocities[at],tile->velocities[at])<.0009f)
                        continue;
                    const glm::vec2 texel(float(tile->x*Vans::VANS_SPLINE_TILE_SIZE+x)+.5f,
                        float(tile->z*Vans::VANS_SPLINE_TILE_SIZE+z)+.5f);
                    m_SpawnSites.push_back(fieldOrigin+texel*field.texelSize);
                }
        }
    }
    if(!m_SpawnRegionValid || glm::distance(camera,m_SpawnCenter)>=CellSize*.5f)
    {
        m_SpawnCenter=camera;m_SpawnRegionValid=true;m_VisibleSpawnSites.clear();
        const float radius=112.f-std::min(field.texelSize,4.f);
        const float radiusSquared=radius*radius;
        for(std::size_t i=0;i<m_SpawnSites.size();++i)
        {
            const glm::vec2 delta=m_SpawnSites[i]-camera;
            if(glm::dot(delta,delta)<=radiusSquared)m_VisibleSpawnSites.push_back(i);
        }
    }
    m_Statistics.visibleSpawnSites=int(m_VisibleSpawnSites.size());
}
void VansRiverWaveSimulation::Step(float dt,const Vans::VansPcgSplineFieldSnapshot& field,glm::vec2 camera,float wavelength,float lifetime)
{
    int attempts=96;
    for(int index=0;index<MaxParticles;++index)
    {
        auto& p=m_Particles[index];
        if(p.age<p.lifetime && glm::distance(p.position,camera)>120.f)
            p.age=p.lifetime;
        if(p.age>=p.lifetime)
        {
            if(m_VisibleSpawnSites.empty() || attempts<=0)continue;
            --attempts;++m_Statistics.spawnAttempts;
            const std::size_t site=m_VisibleSpawnSites[std::min(
                std::size_t(Random()*m_VisibleSpawnSites.size()),m_VisibleSpawnSites.size()-1)];
            const glm::vec2 at=m_SpawnSites[site]+(glm::vec2(Random(),Random())-.5f)*
                (field.texelSize*.9f);
            float height;glm::vec2 velocity;glm::vec4 properties;
            if(glm::length(at-camera)>112 ||
                !field.SampleRiver(at,height,velocity,properties) || properties.x<.5f || glm::length(velocity)<.03f)continue;
            ++m_Statistics.successfulSpawns;
            // 三种包络尺度分别占 1/4、1/2、1/4 的固定池槽位；
            // 细频带仍由每个波包内部的三个载波提供。
            p.scale=index%4==0?0:(index%4==3?2:1);
            const float scale=p.scale==0?2.f:(p.scale==2?.55f:1.f);
            p.amplitude=p.scale==0?.75f:(p.scale==2?.55f:1.f);
            p.headingOffset=(Random()*2-1)*.32f;
            p.position=at;p.direction=Rotate(glm::normalize(velocity),p.headingOffset);
            p.age=0;p.lifetime=lifetime*(.8f+.4f*Random());
            const float length=wavelength*scale*(.8f+.4f*Random());
            p.radius=std::min(length*1.15f,CellSize*.95f);
            const float k=2*Pi/length,side=Random()<.5f?-1.f:1.f;
            p.carriers[0]={k,Random()*2*Pi,0};
            p.carriers[1]={k*(2.1f+.5f*Random()),Random()*2*Pi,side*(.18f+.16f*Random())};
            p.carriers[2]={k*(4.2f+1.2f*Random()),Random()*2*Pi,-side*(.2f+.2f*Random())};
        }
        float height;glm::vec2 velocity;glm::vec4 properties;
        if(!field.SampleRiver(p.position,height,velocity,properties))
        {p.age=std::max(p.age,p.lifetime-.5f);velocity=glm::vec2(0);properties=glm::vec4(0,0,1,0);}
        const float speed=glm::length(velocity);
        if(speed>.015f)
        {
            const glm::vec2 target=Rotate(velocity/speed,p.headingOffset);
            float angle=std::atan2(p.direction.x*target.y-p.direction.y*target.x,glm::dot(p.direction,target));
            angle=std::clamp(angle,-1.8f*dt,1.8f*dt);
            p.direction=Rotate(p.direction,angle);
        }
        const float k=p.carriers[0].waveNumber;
        const float kh=std::min(k*std::max(properties.z,.1f),20.f),t=std::tanh(kh);
        const float omega=std::sqrt(9.81f*k*t);
        const float group=9.81f*(t+kh*(1-t*t))/(2*std::max(omega,.001f));
        // 中点积分局部流速；波包群速度和载波色散独立于水流平移。
        glm::vec2 middleVelocity;glm::vec4 middleProperties;
        if(!field.SampleRiver(p.position+(velocity+p.direction*group)*(.5f*dt),height,middleVelocity,middleProperties))middleVelocity=velocity;
        p.position+=(middleVelocity+p.direction*group)*dt;
        // 载波按各自波长演变；扣除共同包络移动产生的相位变化，内部细节会相互追越。
        for(auto& carrier:p.carriers)
        {
            const float depthFactor=std::tanh(std::min(carrier.waveNumber*std::max(properties.z,.1f),20.f));
            const float frequency=std::sqrt(9.81f*carrier.waveNumber*depthFactor);
            carrier.phase=std::remainder(carrier.phase-
                (frequency-carrier.waveNumber*group*std::cos(carrier.heading))*dt,2*Pi);
        }
        p.age+=dt;
    }
}
void VansRiverWaveSimulation::Pack(glm::vec2 camera)
{
    const glm::vec2 origin=glm::floor(camera/CellSize)*CellSize-glm::vec2(GridSide*CellSize*.5f);
    std::array<std::vector<int>,GridSide*GridSide> bins;
    for(int i=0;i<MaxParticles;++i)
    {
        const auto& p=m_Particles[i];if(p.age>=p.lifetime)continue;
        const auto lo=glm::max(glm::ivec2(glm::floor((p.position-glm::vec2(p.radius)-origin)/CellSize)),glm::ivec2(0));
        const auto hi=glm::min(glm::ivec2(glm::floor((p.position+glm::vec2(p.radius)-origin)/CellSize)),glm::ivec2(GridSide-1));
        for(int y=lo.y;y<=hi.y;++y)for(int x=lo.x;x<=hi.x;++x)bins[y*GridSide+x].push_back(i);
    }
    m_GpuData.assign(2+GridSide*GridSide,glm::vec4(0));m_GpuData.reserve(MaxGpuVectors);
    m_GpuData[0]=glm::vec4(origin,1/CellSize,float(GridSide));
    for(int b=0;b<GridSide*GridSide;++b)
    {
        m_GpuData[2+b]=glm::vec4(float(m_GpuData.size()),float(bins[b].size()),0,0);
        for(int index:bins[b])
        {
            const auto& p=m_Particles[index];
            const float fade=std::min(std::clamp(p.age/.7f,0.f,1.f),std::clamp((p.lifetime-p.age)/.7f,0.f,1.f));
            m_GpuData.emplace_back(p.position,p.radius,p.amplitude*fade*fade*(3-2*fade));
            for(const auto& carrier:p.carriers)
                m_GpuData.emplace_back(Rotate(p.direction,carrier.heading),carrier.waveNumber,carrier.phase);
        }
    }
}
void VansRiverWaveSimulation::Update(float delta,const Vans::VansPcgSplineFieldSnapshot* field,glm::vec2 camera,float wavelength,float lifetime)
{
    if(!field){Clear();return;}
    m_Statistics.spawnAttempts=0;m_Statistics.successfulSpawns=0;
    RefreshSpawnSites(*field,camera);
    // 后台恢复限制补算时间，固定小步避免跟随帧率改变流场轨迹。
    m_Accumulator+=std::clamp(delta,0.f,.1f);
    constexpr float step=1.f/60;
    while(m_Accumulator>=step){Step(step,*field,camera,wavelength,lifetime);m_Accumulator-=step;}
    Pack(camera);
    m_Statistics.activeParticles=0;m_Statistics.activeByScale={};
    for(const auto& particle:m_Particles)if(particle.age<particle.lifetime)
    {
        ++m_Statistics.activeParticles;
        ++m_Statistics.activeByScale[std::clamp(particle.scale,0,2)];
    }
}
}
