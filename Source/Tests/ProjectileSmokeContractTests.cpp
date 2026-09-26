#include "../EngineCore/SceneRuntime/Transform/VansTransformStore.h"
#include "../EngineCore/GameplayActionAdapters/Projectile/VansProjectilePhysics.h"
#include "../EngineCore/PhysicsCore/VansPhysics.h"
#include "../EngineCore/PhysicsCore/VansPhysicsNativeAccess.h"
#include "../EngineCore/ParticleCore/VansParticleRuntime.h"
#include "../EngineCore/ParticleCore/Serialization/VansParticleAssetJsonCodec.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/GameplayActionCore/VansGameplayRuntime.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>

bool TestProjectileSmokeContract()
{
    using namespace VansGraphics;
    using namespace VansEngine;
    namespace fs = std::filesystem;
    auto workspace = fs::current_path();
    while (!workspace.empty() && !fs::exists(workspace / "DemoHallProject"))
    { if (workspace == workspace.root_path()) return false; workspace = workspace.parent_path(); }
    const auto project = workspace / "DemoHallProject";
    auto read = [](const fs::path& path) { std::ifstream file(path); return nlohmann::ordered_json::parse(file); };
    auto check = [](bool ok, const char* error) { if (!ok) std::cerr << "[ProjectileSmoke] " << error << '\n'; return ok; };
    std::string error;
    const auto particleJson = read(project / "Assets/Particles/VolumetricSmokeTest.particle");
    auto assetOwner = std::make_shared<VansParticleAsset>();
    auto& asset = *assetOwner;
    if (!VansParticleAssetJsonCodec::Decode(
        Vans::DecodeSerializedValueJson(particleJson), {}, asset, error))
        return check(false, error.c_str());
    if (!check(asset.m_StartDelay == 2.0f && !asset.m_Prewarm && asset.m_EmissionFrame == VansGraphics::VansParticleEmissionFrame::World && asset.m_Loop,
        "Smoke must delay two seconds, start empty, remain upright and emit continuously")) return false;
    const auto roundTrip = Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
        VansParticleAssetJsonCodec::Encode(asset));
    if (!check(roundTrip["global"]["startDelay"] == 2.0f && roundTrip["global"]["emissionFrame"] == "World",
        "Particle authoring codec lost delayed playback or alignment")) return false;
    auto invalid = roundTrip;
    invalid["global"]["startDelay"] = -1;
    VansParticleAsset rejected;
    if (!check(!VansParticleAssetJsonCodec::Decode(
        Vans::DecodeSerializedValueJson(invalid), {}, rejected, error),
        "Negative delay accepted")) return false;
    VansParticleRuntime particles;
    particles.SetAsset(assetOwner);
    particles.SetEmitterPositionLocal({0,100,0});
    const auto owner = glm::translate(glm::mat4(1), glm::vec3(4,2,6))
        * glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(0,0,1))
        * glm::scale(glm::mat4(1), glm::vec3(0.01f));
    particles.SetOwnerWorldTransform(owner);
    if (!check(glm::length(glm::vec3(particles.OwnerWorldTransform()[3])-glm::vec3(3,2,6)) < 0.0001f
        && particles.OwnerWorldTransform()[1] == glm::vec4(0,1,0,0), "Emitter ignored mesh origin offset or inherited tiny tilted axes")) return false;
    particles.Play();
    particles.DeferFirstUpdate();
    particles.Update(3.0f);
    particles.Update(1.0f);
    particles.Pause(); particles.Update(5.0f); particles.Play();
    particles.Update(0.99f);
    if (!check(particles.AliveInstanceCount() == 0 && particles.GetPlayTime() == 0,
        "Smoke emitted before two seconds or pause/resume restarted the clock")) return false;
    particles.Update(0.21f); particles.SwapBuffers();
    if (!check(particles.AliveInstanceCount() >= 5 && !particles.GetVolumetricRenderBuffer().empty(),
        "Smoke did not inject particles after crossing the delay")) return false;
    const auto firstCount = particles.AliveInstanceCount();
    for (int i=0;i<120;++i) particles.Update(1.0f/120);
    if (!check(particles.AliveInstanceCount() > firstCount+20, "Smoke emission was a one-shot burst")) return false;
    for (int i=0;i<3600;++i) particles.Update(1.0f/60);
    const auto& pool = particles.GetEmitter(0)->ParticlePool();
    if (!check(pool.m_AliveCount > 180 && pool.m_AliveCount < 320, "Long-running smoke stopped emitting or stopped recycling particles")) return false;
    bool fresh = false;
    for (uint32_t i=0; i<pool.m_AliveCount; ++i)
    {
        if (!check(pool.m_LifeTime[i] >= 7 && pool.m_LifeTime[i] <= 10 && pool.m_Age[i] <= pool.m_LifeTime[i],
            "Individual smoke particles lost their original 7-10 second lifetime")) return false;
        fresh |= pool.m_Age[i] < 0.2f;
    }
    if (!check(fresh, "Smoke did not create fresh particles after 60 seconds")) return false;
    std::cout << "[ProjectileSmoke] continuousAfter60Seconds=1 particleLifetime=7-10 recycling=1\n";
    particles.Restart(); particles.Update(1.9f);
    if (!check(particles.AliveInstanceCount() == 0, "Restart bypassed emission delay")) return false;
    particles.Stop();
    std::cout << "[ProjectileSmoke] delay=2 noPrewarm=1 continuous=1 worldEmissionFrame=1 offset=1 pauseResume=1\n";

    const auto graph = read(project / "Assets/GAF/PlayerThrow/ThrowSmoke.vactiongraph");
    nlohmann::ordered_json inputs;
    for (const auto& node : graph["nodes"]) if (node["guid"] == "spawn-projectile") inputs = node["properties"]["inputs"];
    if (!check(!inputs.empty() && inputs["particle"]["value"]["asset"]["guid"] == "4b23a225-8cb1-406d-9006-bd981b4e69c8",
        "Throw is missing its attached Particle component configuration")) return false;
    if (!check(inputs["lifetime"]["value"] == 0, "Smoke grenade must not expire automatically")) return false;

    // 使用真实寿命服务，分别检查有限寿命、无限寿命和显式清理。
    Vans::VansRuntimeWorld world;
    Vans::VansGameplayRuntime gameplay;
    Vans::VansEntityHandle spawned;
    int sequence = 0, destroyed = 0;
    Vans::VansProjectileActionService projectiles(world, gameplay, {
        [&](const Vans::VansProjectileSpawnRequest&, std::string&) {
            return spawned = world.CreateEntity({"smoke-lifetime-" + std::to_string(++sequence), "Smoke", {}, true});
        },
        [&](Vans::VansEntityHandle entity) { ++destroyed; return world.DestroyEntity(entity); }, {}
    });
    auto spawn = [&](double lifetime) {
        using V = Vans::VansSerializedValue;
        Vans::VansActionCommand command;
        command.stableName = "Projectile.Spawn";
        command.payload = V::Object({{"lifetime", V::Float(lifetime)}, {"velocity", V::Object({{"x", V::Float(0)}})}});
        return projectiles.Execute(command);
    };
    const auto infinite = spawn(0);
    const auto infiniteEntity = spawned;
    if (!check(static_cast<bool>(infinite), "Zero lifetime was not accepted")) return false;
    projectiles.Tick(0);
    projectiles.Tick(86400);
    if (!check(world.IsAlive(infiniteEntity) && destroyed == 0, "Infinite projectile expired")) return false;
    const auto finite = spawn(12);
    const auto finiteEntity = spawned;
    if (!check(static_cast<bool>(finite), "Finite lifetime was not accepted")) return false;
    projectiles.Tick(0); projectiles.Tick(11);
    if (!check(world.IsAlive(finiteEntity), "Finite projectile expired early")) return false;
    projectiles.Tick(1.1);
    if (!check(!world.IsAlive(finiteEntity) && world.IsAlive(infiniteEntity) && destroyed == 1,
        "Finite expiry affected persistent projectiles")) return false;
    if (!check(!spawn(-1) && !spawn(std::numeric_limits<double>::infinity()), "Invalid lifetime accepted")) return false;
    if (!check(projectiles.Release(infinite.resource, error) && !world.IsAlive(infiniteEntity), "Explicit persistent projectile cleanup failed")) return false;
    projectiles.Release(finite.resource, error);
    std::cout << "[ProjectileSmoke] noExpiryAfter86400Seconds=1 finiteExpiry=1 explicitCleanup=1\n";
    const auto scene = read(project / "Scenes/DemoHall.json");
    for (const auto& entity : scene["entities"])
        if (!check(entity["id"] != "05c4cab0-ed7e-4f59-8786-4b1e41d41f6f", "Standalone scene smoke was not removed")) return false;

    auto& physics = VansPhysicsSystem::GetInstance();
    if (!physics.Initialize()) return check(false, "PhysX initialization failed");
    const auto groundTransform = Vans::VansTransformStore::Allocate();
    const auto bodyTransform = Vans::VansTransformStore::Allocate();
    struct Cleanup
    {
        VansPhysicsSystem& system; uint32_t a,b;
        ~Cleanup() { Vans::VansTransformStore::Release(a); Vans::VansTransformStore::Release(b); system.Shutdown(); }
    } cleanup{physics,groundTransform,bodyTransform};
    Vans::VansTransform floorPose = Vans::VansTransformStore::Read(groundTransform);
    floorPose.m_Position = {0,-0.1f,0}; floorPose.m_Scale = glm::vec3(1);
    Vans::VansTransformStore::Write(groundTransform, floorPose);
    PhysicsNodeProperties floorProps;
    floorProps.enabled=true; floorProps.boxExtents={5,0.1f,5};
    floorProps.material.restitution=0; floorProps.material.staticFriction=0.8f; floorProps.material.dynamicFriction=0.8f;
    VansPhysicsNode floor; floor.SetName("SmokeBounceContractFloor"); floor.Initialize(floorProps,groundTransform);
    const auto drop = [&](bool oldMaterial)
    {
        Vans::VansTransform pose = Vans::VansTransformStore::Read(bodyTransform);
        pose.m_Position={0,1.2f,0}; pose.m_Rotation={0,0,0}; pose.m_Scale=glm::vec3(1);
        Vans::VansTransformStore::Write(bodyTransform, pose);
        Vans::VansProjectileSpawnRequest request;
        request.restitution=oldMaterial ? 0.25f : inputs["restitution"]["value"].get<float>();
        request.friction=oldMaterial ? 0.6f : inputs["friction"]["value"].get<float>();
        request.restitutionCombine=oldMaterial ? "Average" : inputs["restitutionCombine"]["value"].get<std::string>();
        request.frictionCombine=oldMaterial ? "Average" : inputs["frictionCombine"]["value"].get<std::string>();
        const auto properties = Vans::VansBuildProjectilePhysicsProperties(request,{-0.025f,-0.0725f,-0.025f},{0.025f,0.0725f,0.025f},glm::vec3(1));
        VansPhysicsNode body; body.SetName(oldMaterial ? "OldSmokeMaterial" : "ConfiguredSmokeMaterial");
        body.Initialize(properties,bodyTransform);
        const auto* actorIdentity = static_cast<const physx::PxRigidActor*>(body.GetActorIdentity());
        const auto* actor = actorIdentity ? actorIdentity->is<physx::PxRigidDynamic>() : nullptr;
        if (!actor) return -1.0f;
        bool touched=false;
        float height=0;
        for (int i=0;i<360;++i)
        {
            auto* scene = VansPhysicsNativeAccess::Scene(physics);
            scene->simulate(1.0f/120);
            scene->fetchResults(true);
            float y=actor->getGlobalPose().p.y;
            if (y<0.1f) touched=true;
            if (touched) height=std::max(height,y-0.0725f);
        }
        // Entity teardown can deactivate/remove the actor before Node shutdown.
        // Releasing the remaining body/material must not remove it a second time.
        body.SetEnabled(false);
        body.Shutdown();
        return height;
    };
    const float oldHeight=drop(true), newHeight=drop(false);
    std::cout << "[ProjectileSmoke] reboundHeightOld=" << oldHeight << " reboundHeightConfigured=" << newHeight << '\n';
    return check(newHeight>0.20f && newHeight>oldHeight*2, "Configured grenade material did not rebound visibly on zero-restitution ground");
}
