#include "../EngineCore/RenderCore/Decal/VansImpactDecalSystem.h"
#include "../EngineCore/RenderCore/VansRenderNode.h"
#include "../EngineCore/GameplayActionAdapters/Decal/VansDecalActionService.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/PhysicsCore/VansPhysicsNode.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/RenderCore/GeometryCore/VansTriangleGeometryQuery.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

bool TestImpactDecalRuntimeContract()
{
    using namespace Vans;
    using namespace VansGraphics;
    const auto check = [](bool ok, const char* message) { if (!ok) std::cerr << "[ImpactDecal] " << message << '\n'; return ok; };
    auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
    if (!check(physics.Initialize(), "PhysX initialization failed")) return false;
    struct Cleanup { ~Cleanup() { VansEngine::VansPhysicsSystem::GetInstance().Shutdown(); } } cleanup;
    VkDevice device = VK_NULL_HANDLE;
    VansRuntimeWorld world;
    VansPBRMaterial material; material.m_MaterialType = VAN_PBR;
    VansCommonRenderNode target(device, OPAQUE_NODE), neighbor(device, OPAQUE_NODE);
    target.m_Material = neighbor.m_Material = &material;
    target.SetTransformData({3,2,1},{15,40,25},{2,3,0.5f});
    const auto targetEntity = world.CreateEntity({"decal-target", "Decal Target"});
    const auto neighborEntity = world.CreateEntity({"decal-neighbor", "Neighbor"});
    world.AddComponent(targetEntity, VansRuntimeComponentType_Render, VansRuntimeRenderComponent{&target}, "decal-render");
    world.AddComponent(neighborEntity, VansRuntimeComponentType_Render, VansRuntimeRenderComponent{&neighbor}, "neighbor-render");
    VansEngine::PhysicsNodeProperties properties;
    properties.enabled = true; properties.bodyType = VansEngine::PhysicsBodyType::Kinematic;
    VansEngine::VansPhysicsNode body;
    body.Initialize(properties, target.m_TransformID);
    const auto collider = world.AddComponent(targetEntity, VansRuntimeComponentType_Physics, VansRuntimePhysicsComponent{&body}, "decal-collider");
    VansSceneImpactDecalConfig config; config.capacity = 64;
    std::vector<std::unique_ptr<VansDecalRenderNode>> owned;
    std::vector<VansDecalRenderNode*> nodes;
    for (size_t i=0; i<config.capacity; ++i)
    { owned.push_back(std::make_unique<VansDecalRenderNode>(device)); nodes.push_back(owned.back().get()); }
    VansImpactDecalSystem system(world, nullptr);
    std::string error;
    if (!check(system.AddPool("template", config, nodes, error), error.c_str())) return false;
    const auto aliveBefore = world.Entities().AliveCount();
    const auto transformsBefore = VansTransformStore::GlobalTransforms.size();
    auto anchor = VansTransformStore::GetTransform(target.m_TransformID).GetModelMatrix();
    const glm::vec3 localPoint(.1f,.2f,.3f), localNormal(0,1,0);
    const glm::vec3 position = glm::vec3(anchor*glm::vec4(localPoint,1));
    const glm::vec3 normal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(anchor)))*localNormal);
    VansSurfaceImpact impact;
    impact.kind = VansSurfaceImpactKind::Rigid;
    impact.hit.hitEntity = impact.hit.entity = targetEntity;
    impact.hit.componentGuid = "decal-collider";
    impact.hit.position = {position.x,position.y,position.z};
    impact.hit.normal = {normal.x,normal.y,normal.z};
    impact.hit.distance = 5;
    VansSurfaceImpact decoded;
    if (!check(VansDecodeSurfaceImpact(VansEncodeSurfaceImpact(impact), decoded, error) && decoded.hit.hitEntity == targetEntity,
        "Surface impact codec lost collider identity")) return false;
    auto malformed = impact; malformed.hit.normal = {0,0,0};
    if (!check(!VansDecodeSurfaceImpact(VansEncodeSurfaceImpact(malformed), decoded, error), "Zero normal accepted")) return false;
    if (!check(system.Spawn("template", impact, error), "First rigid impact rejected")) return false;
    system.Tick(.31);
    auto debug = system.CaptureDebug();
    if (!check(debug[0].active && glm::distance(debug[0].position,position)<1e-4f && glm::dot(debug[0].normal,normal)>.9999f &&
        debug[0].scale == glm::vec3(.04f,.01f,.04f) && target.m_DecalReceiverId>0 && neighbor.m_DecalReceiverId==0,
        "Placement, action-independent lifetime, size or receiver isolation failed")) return false;
    target.SetTransformData({-2,4,7},{-35,90,65},{.5f,4,2});
    anchor = VansTransformStore::GetTransform(target.m_TransformID).GetModelMatrix();
    system.Tick(.1);
    debug = system.CaptureDebug();
    const glm::vec3 moved = glm::vec3(anchor*glm::vec4(localPoint,1));
    const glm::vec3 movedNormal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(anchor)))*localNormal);
    if (!check(glm::distance(debug[0].position,moved)<1e-4f && glm::dot(debug[0].normal,movedNormal)>.9999f &&
        debug[0].scale==glm::vec3(.04f,.01f,.04f), "Moving nonuniform receiver distorted or detached decal")) return false;
    const auto begin = std::chrono::steady_clock::now();
    for (int i=0; i<70; ++i) if (!system.Spawn("template", impact, error)) return check(false,"Pool rollover rejected a shot");
    const double spawnMicros = std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/70;
    debug = system.CaptureDebug();
    size_t active = 0;
    for (const auto& entry : debug) active += entry.active;
    if (!check(active==64 && debug[0].receiver==debug[63].receiver && system.SpawnCount()==71 &&
        world.Entities().AliveCount()==aliveBefore && VansTransformStore::GlobalTransforms.size()==transformsBefore,
        "Pool grew entities/transforms or lost bounded reuse")) return false;
    const auto tickBegin=std::chrono::steady_clock::now();
    for (int i=0; i<1000; ++i) system.Tick(0);
    const double tickMicros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-tickBegin).count()/1000;
    world.SetComponentEnabled(collider,false); system.Tick(0);
    for (const auto& entry : system.CaptureDebug()) if (entry.active) return check(false,"Disabled collider retained a decal");
    if (!check(target.m_DecalReceiverId==0, "Last mark failed to release receiver group")) return false;
    world.SetComponentEnabled(collider,true);
    if (!system.Spawn("template",impact,error)) return false;
    system.Tick(30.01);
    for (const auto& entry : system.CaptureDebug()) if (entry.active) return check(false,"Expired mark remained enabled");
    if (!system.Spawn("template",impact,error)) return false;
    world.RemoveComponent(collider);
    world.AddComponent(targetEntity,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&body},"replacement-collider");
    system.Tick(0);
    for (const auto& entry : system.CaptureDebug()) if (entry.active) return check(false,"Reused component slot retained stale attachment");
    if (!check(!system.Spawn("template",impact,error),"Removed collider GUID remained usable")) return false;
    // 真实 DemoHall 墙面三角形必须落入 2 cm 投影体，不能再使用包含装饰的外包盒表面。
    auto workspace = std::filesystem::current_path();
    for (int i=0; i<6 && !std::filesystem::exists(workspace/"DemoHallProject"); ++i) workspace=workspace.parent_path();
    target.SetTransformData({0,0,0},{0,0,0},{1,1,1});
    for (const char* wall : {"Hall_Wall_3M_Hall_Wall_3M_4300000_sm0.obj", "Hall_Wall_6M_Hall_Wall_6M_4300000_sm0.obj"})
    {
        std::ifstream file(workspace/"DemoHallProject/Assets/Models/DemoHall/Runtime"/wall);
        if (!check(bool(file),"Production wall mesh unavailable")) return false;
        std::vector<glm::vec3> vertices;
        std::vector<VansGeometryTriangle> triangles;
        float maximumZ = 0;
        for (std::string line; std::getline(file,line);)
        {
            std::istringstream stream(line); std::string type; stream >> type;
            if (type=="v") { glm::vec3 v; stream>>v.x>>v.y>>v.z; vertices.push_back(v); maximumZ=(std::max)(maximumZ,v.z); }
            if (type=="f")
            {
                std::vector<uint32_t> indices;
                for (std::string token; stream>>token;) indices.push_back(static_cast<uint32_t>(std::stoul(token))-1);
                for (size_t i=1; i+1<indices.size(); ++i)
                { VansGeometryTriangle t; t.a=vertices.at(indices[0]);t.b=vertices.at(indices[i]);t.c=vertices.at(indices[i+1]);triangles.push_back(t); }
            }
        }
        VansTriangleGeometryQuery query; query.Build(std::move(triangles)); VansGeometryHit hit;
        if (!check(query.Raycast({.25f,1.5f,2},{0,0,-1},10,hit) && std::abs(hit.position.z-.1f)<.0001f &&
            maximumZ-hit.position.z>.1f,"Production wall ray did not distinguish rendered face from outer box")) return false;
        impact.kind=VansSurfaceImpactKind::Render; impact.hit.componentGuid="decal-render";
        impact.hit.position={hit.position.x,hit.position.y,hit.position.z};
        impact.hit.normal={hit.normal.x,hit.normal.y,hit.normal.z}; impact.hit.distance=hit.distance;
        if (!check(system.Spawn("template",impact,error),"Precise render surface without physics anchor did not spawn")) return false;
        for (const auto& entry : system.CaptureDebug()) if (entry.active)
            if (!check(glm::distance(entry.position,hit.position)<.0001f && std::abs(glm::dot(entry.normal,hit.normal))>.999f,
                "Precise decal missed rendered wall or normal")) return false;
        system.Tick(30.01);
    }
    if (!check(system.Spawn("template",impact,error),"Render receiver re-spawn failed")) return false;
    const auto renderStorage=world.FindStorage(VansRuntimeComponentType_Render);
    world.SetComponentEnabled(renderStorage->FindByStableGuid("decal-render"),false); system.Tick(0);
    for (const auto& entry : system.CaptureDebug()) if (entry.active) return check(false,"Disabled render receiver retained decal");
    // 不同表面法线和负向投影，以及不受支持的材质/人物命中。
    for (glm::vec3 n : {glm::vec3(1,0,0),glm::vec3(0,-1,0),glm::vec3(0,0,1),glm::normalize(glm::vec3(1,2,3))})
    {
        const glm::vec3 reference=std::abs(n.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0);
        VansTransform pose;
        if (!check(VansImpactDecalSystem::BuildPose(glm::mat4(1),{},n,glm::normalize(glm::cross(reference,n)),config,pose) &&
            glm::dot(glm::normalize(glm::vec3(pose.GetModelMatrix()[1])),n)>.9999f,"Wall/ceiling/sloped normal orientation failed")) return false;
    }
    size_t backendCalls=0;
    VansDecalActionService service({[&](const auto&,const auto&,auto&) {++backendCalls;return true;}});
    VansActionCommand command; command.command=VansMakeStableId<VansActionFieldIdTag>("Decal.SpawnImpact");
    for (auto kind : {VansSurfaceImpactKind::None,VansSurfaceImpactKind::Regional,VansSurfaceImpactKind::Unmapped})
    {
        impact.kind=kind;
        command.payload=VansSerializedValue::Object({{"template",VansSerializedValue::String("template")},{"surfaceImpact",VansEncodeSurfaceImpact(impact)}});
        if (!service.Execute(command)) return check(false,"Non-surface shot broke action execution");
    }
    if (!check(backendCalls==0,"Miss or regional hurt body spawned rigid decal")) return false;
    std::cout << "[ImpactDecal] PASS capacity=64 rollover=70 localAttachment=1 normalMatrix=1 staleGeneration=1 expiry=1 "
        << "spawnCpuUs=" << spawnMicros << " static64TickCpuUs=" << tickMicros << '\n';
    return true;
}
