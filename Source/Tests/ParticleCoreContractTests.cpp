#include "../EngineCore/ParticleCore/VansParticleManager.h"
#include "../EngineCore/ParticleCore/Serialization/VansParticleAssetJsonCodec.h"
#include "../EngineCore/RenderCore/GeometryCore/VansPolylineMeshBuilder.h"
#include "../EngineCore/RenderCore/VansSceneAssetRegistry.h"
#include "../EngineCore/RenderCore/VulkanCore/VansRenderGraphVulkanSync.h"
#include "../EngineCore/GameplayActionAdapters/VFX/VansVFXActionService.h"
#include "../EngineCore/GameplayActionCore/VansGameplayRuntime.h"
#include "../EngineCore/ParticleCore/Authoring/VansParticleAuthoringSchema.h"
#include "../EngineCore/ParticleCore/Storage/VansParticleAssetStorage.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedPathPattern.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>

namespace
{
bool TestParticleAuthoring()
{
    using namespace VansGraphics;
    const auto check = [](bool ok, const std::string& error)
    { if (!ok) std::cerr << "[ParticleAuthoring] " << error << '\n'; return ok; };
    const auto defaults = VansParticleAuthoringSchema::Defaults();
    VansSceneAssetRegistry registry;
    VansAsset texture; texture.SetName("particleWhite");
    registry.AddTexture(&texture,"34a4e372-93c5-44a1-a985-768a099c3c95");
    registry.RebuildLookup();
    if (!check(registry.FindTextureByGuid("34A4E372-93C5-44A1-A985-768A099C3C95")==&texture &&
        registry.FindTexture("particleWhite")==&texture,"GPU texture identity was lost behind its runtime name")) return false;
    registry.ClearTextures();
    if (!check(!registry.FindTextureByGuid("34a4e372-93c5-44a1-a985-768a099c3c95"),"Texture GUID retained an unloaded resource")) return false;
    auto json = defaults.at("asset");
    json["emitters"].push_back(defaults.at("emitter"));
    for (const auto& module : defaults.at("modules"))
        json["emitters"][0][module.at("phase").get<std::string>()].push_back(module.at("definition"));
    VansParticleAsset asset; std::string error;
    if (!check(VansParticleAssetJsonCodec::Decode(json,{},asset,error),error)) return false;
    const auto roundTrip = VansParticleAssetJsonCodec::Encode(asset);
    VansParticleAsset second;
    if (!check(VansParticleAssetJsonCodec::Decode(roundTrip,{},second,error) &&
        VansParticleAssetJsonCodec::Encode(second) == roundTrip,"Module constructor defaults do not round-trip")) return false;
    for (const auto& invalid : std::vector<Vans::ParticleJson>{
        {{"renderer",{{"type","Mesh"}}}}, {{"renderer",{{"textureGuid","Assets/smoke.png"}}}},
        {{"initialize",Vans::ParticleJson::array({{{"module","UpdateGravity"}}})}},
        {{"update",Vans::ParticleJson::array({{{"module","UnknownModule"}}})}},
        {{"renderer",{{"type","Ribbon"},{"lightingMode","SixWayLit"}}}} })
    {
        auto bad = json;
        for (const auto& field : invalid.items()) bad["emitters"][0][field.key()] = field.value();
        if (!check(!VansParticleAssetJsonCodec::Decode(bad,{},second,error),"Invalid renderer, GUID or module phase was accepted")) return false;
    }
    json["emitters"][0]["renderer"]["ribbon"]["rootWidth"] = 0.023;
    json["global"]["fixedStep"] = 1.0f/30.0f;
    json["emitters"][0]["renderer"]["volumetric"]["anisotropy"] = -0.9;
    if (!check(VansParticleAssetJsonCodec::Decode(json,{},asset,error) &&
        std::abs(VansParticleAssetJsonCodec::Encode(asset)["emitters"][0]["renderer"]["ribbon"]["rootWidth"].get<float>()-0.023f)<1.0e-6f,
        "Inactive Ribbon parameters were lost when editing Billboard")) return false;
    if (!check(VansParticleAssetJsonCodec::Decode(VansParticleAssetJsonCodec::Encode(asset),{},second,error),
        "Float precision at a supported scalar boundary did not round-trip: " + error)) return false;
    if (!check(Vans::MatchSerializedPathPattern("/emitters/*/renderer/type","/emitters/12/renderer/type") &&
        !Vans::MatchSerializedPathPattern("/emitters/*/type","/emitters/12/renderer/type"),"Authoring path wildcard crossed an object boundary")) return false;
    namespace fs = std::filesystem;
    auto workspace = fs::current_path();
    for (int depth=0; depth<6 && !fs::exists(workspace/"DemoHallProject"); ++depth) workspace=workspace.parent_path();
    unsigned count=0;
    for (const auto* path : {"AnimationV2Project/Assets/Particles", "DemoHallProject/Assets/Particles",
        "DustV3Project/Assets/Survival/Particles", "TestV2Project/Assets/Particles", "DemoHallProject/Assets/Particles/Impacts"})
    {
        if (!fs::is_directory(workspace/path)) continue;
        for (const auto& entry : fs::directory_iterator(workspace/path))
        {
            if (entry.path().extension()!=".particle") continue;
            if (!check(VansParticleAssetStorage::Load(entry.path(),asset,error),error)) return false;
            const auto encoded=VansParticleAssetJsonCodec::Encode(asset);
            if (!check(VansParticleAssetJsonCodec::Decode(encoded,entry.path(),second,error) &&
                VansParticleAssetJsonCodec::Encode(second)==encoded,entry.path().string()+" changed on a second round trip")) return false;
            if (entry.path().parent_path().filename() == "Impacts")
            {
                auto definition = std::make_shared<VansParticleAsset>();
                if (!check(VansParticleAssetStorage::Load(entry.path(), *definition, error), error)) return false;
                VansParticleRuntime runtime; runtime.SetAsset(definition);
                glm::mat4 pose(1); pose[1]=glm::vec4(0,0,1,0); pose[2]=glm::vec4(0,-1,0,0); pose[3]=glm::vec4(1,2,3,1);
                runtime.SetOwnerWorldTransform(pose); runtime.Play();
                // 160ms 帧不能把本次短 Burst 当作固定步长预算外的旧历史丢弃。
                runtime.Update(.16f); runtime.SwapBuffers();
                if (!check(!runtime.GetRenderBuffer().empty(), entry.path().string()+" dropped its impact burst at a long frame")) return false;
                for (const auto& point : runtime.GetRenderBuffer())
                    if (!check(point.m_WorldPosition.z > 3, "Impact source orientation was lost")) return false;
                pose[3].x += 100; runtime.SetOwnerWorldTransform(pose); runtime.Update(.01f); runtime.SwapBuffers();
                for (const auto& point : runtime.GetRenderBuffer())
                    if (!check(point.m_WorldPosition.x < 3, "Existing blood/dust followed the moved source")) return false;
                runtime.Update(2); runtime.SwapBuffers();
                if (!check(runtime.IsFinished() && runtime.GetRenderBuffer().empty(), "Impact effect did not expire")) return false;
            }
            ++count;
        }
    }
    if (!check(count>=8,"Production particle assets were not validated")) return false;
    std::cout << "[ParticleAuthoring] constructorDefaults=1 activeAndInactiveRoundTrip=1 invalidConfiguration=1 assets=" << count << '\n';
    return true;
}

bool TestVFXServiceLifetime()
{
    using namespace Vans;
    const auto check = [](bool result,const char* message)
    { if (!result) std::cerr << "[ParticleVFX] " << message << '\n'; return result; };
    VansGameplayRuntime gameplay;
    VansGenerationPool<bool> instances;
    int stopCount=0, destroyCount=0;
    VansVFXSceneBackend backend;
    backend.spawn = [&](const auto&,std::string&) { return instances.Emplace(false); };
    backend.stop = [&](auto handle,VansVFXStopMode mode) {
        auto* done = instances.Resolve(handle); if (!done) return false;
        ++stopCount; *done = mode == VansVFXStopMode::Immediate; return true;
    };
    backend.finished = [&](auto handle) { const auto* done=instances.Resolve(handle); return !done || *done; };
    backend.destroy = [&](auto handle) { ++destroyCount; return instances.Release(handle); };
    VansVFXActionService service(gameplay,backend);
    VansActionCommand command; command.stableName = "VFX.Spawn";
    command.context.SetEntity(VansActionContextSlots::Owner,{0,1});
    command.payload = VansSerializedValue::Object({
        {"effect",VansSerializedValue::String("0a11469b-9cc0-40e7-8fa7-8f21329e8931")},
        {"source",WriteEntityParentReference("ea552d97-eec4-4664-9cda-445d14d65312")}
    });
    auto first = service.Execute(command);
    if (!check(bool(first) && first.resource.IsValid() && instances.ActiveCount()==1,"Spawn did not create an owned service resource")) return false;
    VansActionCommand stop; stop.stableName="VFX.Stop";
    stop.payload = VansSerializedValue::Object({{"resource",VansSerializedValue::Object({
        {"index",VansSerializedValue::Int(first.resource.index)}, {"generation",VansSerializedValue::Int(first.resource.generation)}})}});
    service.Tick(0); // 首次更新只跨过已排队、尚未模拟的出生边界。
    if (!check(bool(service.Execute(stop)) && stopCount==1 && instances.ActiveCount()==1,"Drain stop released the simulation immediately")) return false;
    instances.ForEach([](auto,bool& done) { done=true; });
    service.Tick(0);
    if (!check(instances.ActiveCount()==0 && destroyCount==1,"Completed effect did not retire its simulation")) return false;
    // 未转交 World 的令牌仍可由动作账本释放，避免完成与 TransferResource 同帧竞态。
    std::string error;
    if (!check(service.Release(first.resource,error) && destroyCount==1,"Completed action-owned token was freed twice or prematurely")) return false;
    auto second=service.Execute(command);
    if (!check(bool(second) && second.resource.index==first.resource.index && second.resource.generation!=first.resource.generation
        && !service.Execute(stop),"A stale VFX token controlled a reused slot")) return false;
    if (!check(service.Release(second.resource,error) && instances.ActiveCount()==0,"Action cleanup leaked an effect")) return false;
    const auto& schemas=service.Capability().commandSchemas;
    const auto stopSchema=std::find_if(schemas.begin(),schemas.end(),[](const auto& schema) { return schema.stableName=="VFX.Stop"; });
    if (!check(stopSchema!=schemas.end() && stopSchema->resourcePolicy==VansActionCommandResourcePolicy::Update,"Stop was declared as resource release")) return false;
    const auto pulseSchema=std::find_if(schemas.begin(),schemas.end(),[](const auto& schema) { return schema.stableName=="VFX.Pulse"; });
    if (!check(pulseSchema!=schemas.end() && pulseSchema->resourcePolicy==VansActionCommandResourcePolicy::None,
        "Pulse would create duplicate action-owned resource receipts")) return false;
    int pulses=0;
    backend.pulse = [&](const auto&,std::string&) { ++pulses; return true; };
    VansVFXActionService pulseService(gameplay,backend);
    command.stableName="VFX.Pulse";
    for (int i=0; i<8; ++i)
    {
        const auto result=pulseService.Execute(command);
        if (!check(bool(result) && !result.resource.IsValid(),"Pulse returned an action-owned resource")) return false;
        pulseService.Tick(0);
    }
    if (!check(pulses==8 && instances.ActiveCount()==0 && destroyCount==2,"Pulse duplicated service resource ownership")) return false;
    std::cout << "[ParticleVFX] drainOwnership=1 completedActionToken=1 staleToken=1 cleanup=1\n";
    return true;
}
bool TestPolylineGeometry()
{
    using namespace VansGraphics;
    const auto check = [](bool result, const char* message)
    { if (!result) std::cerr << "[Polyline] " << message << '\n'; return result; };
    const auto point = [](glm::vec3 position, float u) { return VansPolylinePoint{position,0.02f,glm::vec4(1),u}; };
    for (const auto camera : {glm::vec3(0,0,3),glm::vec3(0,3,0),glm::vec3(0)})
    {
        VansPolylineMesh mesh;
        const std::vector<VansPolylinePoint> points = {point({0,0,0},0),point({0,0,0},0),point({0,1,0},1),point({0,2,0},2)};
        VansPolylineMeshBuilder::Append(points,camera,{1,0,0},{0,1,0},mesh);
        if (!check(mesh.vertices.size()==6 && mesh.indices.size()==12,"Coincident points or camera-aligned segment broke topology")) return false;
        for (const auto& vertex : mesh.vertices)
            if (!check(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y)
                && std::isfinite(vertex.position.z) && std::abs(vertex.position.x) <= 0.021f
                && vertex.uv.x >= 0 && vertex.uv.x <= 2,"Degenerate camera generated invalid geometry or UV")) return false;
        for (auto index : mesh.indices) if (!check(index<mesh.vertices.size(),"Out-of-range mesh index")) return false;
    }
    VansPolylineMesh fold;
    VansPolylineMeshBuilder::Append({point({0,0,0},0),point({0,1,0},1),point({0,0,0},2)}, {0,0,3},{1,0,0},{0,1,0},fold);
    if (!check(fold.vertices.size()==8 && fold.indices.size()==12,"Reversal created a bridging triangle")) return false;
    VansPolylineMesh large;
    std::vector<VansPolylinePoint> straight;
    for (int i=0;i<64;++i) straight.push_back(point({0,float(i)*0.01f,0},float(i)*0.01f));
    VansPolylineMeshBuilder::Append(straight,{0,0,3},{1,0,0},{0,1,0},large);
    if (!check(large.vertices.size()==128 && large.indices.size()==378,"Straight strip exceeded the linear geometry budget")) return false;
    const auto sync = VansRenderGraphVulkanSyncMapper::MapResourceUsage(VansRenderResourceUsage::DepthStencilAttachmentSampledRead);
    if (!check(sync.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        && (sync.accessMask&VK_ACCESS_SHADER_READ_BIT) && (sync.accessMask&VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
        && (sync.stageMask&VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),"Soft particle depth dependency is incomplete")) return false;
    std::cout << "[Polyline] cameraDegeneracy=1 duplicatePoints=1 reversalBreak=1 linearBudget=1 depthRead=1\n";
    return true;
}
bool TestParticleDebugSnapshot()
{
    using namespace VansGraphics;
    const auto check = [](bool ok, const char* message) {
        if (!ok) std::cerr << "[ParticleDebug] " << message << '\n'; return ok;
    };
    Vans::ParticleJson ribbon = {
        {"name", "RibbonA"}, {"maxParticles", 4096},
        {"spawn", {{"type", "RateOverTime"}, {"rate", 40}}},
        {"initialize", Vans::ParticleJson::array({
            {{"module", "InitLifetime"}, {"lifetime", {{"mode", "Constant"}, {"value", 5}}}},
            {{"module", "InitVelocity"}, {"mode", "Cone"}, {"angle", 0}, {"speed", .2}}
        })},
        {"renderer", {{"type", "Ribbon"}, {"ribbon", {{"rootMode", "FollowSource"}}}}}
    };
    auto billboard = ribbon; billboard["name"] = "Billboard"; billboard["renderer"]["type"] = "Billboard";
    auto other = ribbon; other["name"] = "RibbonB";
    Vans::ParticleJson json = {{"name", "DebugEffect"}, {"global", {{"duration", 5}, {"loop", true}}},
        {"emitters", Vans::ParticleJson::array({ribbon, billboard, other})}};
    auto asset = std::make_shared<VansParticleAsset>(); std::string error;
    if (!check(VansParticleAssetJsonCodec::Decode(json, {}, *asset, error), error.c_str())) return false;
    VansParticleManager manager;
    const auto first = manager.Create(asset), second = manager.Create(asset);
    manager.Resolve(first)->SetOwnerWorldTransform(glm::mat4(1));
    auto world = glm::mat4(1); world[3] = {2, 3, 4, 1};
    manager.Resolve(second)->SetOwnerWorldTransform(world);
    manager.Queue(first, VansParticleControl::Play); manager.Queue(second, VansParticleControl::Play);
    manager.TickMainThread(.2f); manager.WaitForUpdateAndSwap();
    const auto snapshot = manager.CaptureDebugSnapshot();
    if (!check(snapshot.emitters.size() == 4 && !snapshot.truncated,
        "Snapshot omitted a non-VFX instance or included a Billboard emitter")) return false;
    for (const auto& emitter : snapshot.emitters)
    {
        const auto* runtime = manager.Resolve(emitter.instance);
        const auto& frame = runtime->GetFrameData();
        const auto& range = frame.emitters[emitter.emitterIndex];
        if (!check(emitter.effectName == "DebugEffect" && emitter.emitterIndex != 1 &&
            emitter.ribbons.size() == range.ribbonCount, "Emitter identity/ranges lost")) return false;
        for (std::size_t i = 0; i < emitter.ribbons.size(); ++i)
        {
            const auto& actual = emitter.ribbons[i]; const auto& expected = frame.ribbons[range.ribbonFirst + i];
            if (!check(actual.ribbonId == expected.ribbonId && actual.hasSourceRoot == expected.hasSourceRoot &&
                actual.points.size() == expected.points.size(), "Debug topology differs from published render frame")) return false;
            for (std::size_t p = 0; p < actual.points.size(); ++p)
                if (!check(actual.points[p].position == expected.points[p].position &&
                    actual.points[p].width == expected.points[p].width && actual.points[p].sequence == expected.points[p].sequence,
                    "Debug node differs from published render frame")) return false;
        }
    }
    const auto retainedPosition = snapshot.emitters.front().ribbons.front().points.front().position;
    manager.Shutdown();
    if (!check(manager.CaptureDebugSnapshot().emitters.empty() &&
        snapshot.emitters.front().ribbons.front().points.front().position == retainedPosition,
        "Scene teardown invalidated an owned debug snapshot or left stale debug nodes")) return false;
    ribbon["spawn"]["rate"] = 4096;
    json["emitters"] = Vans::ParticleJson::array({ribbon});
    asset = std::make_shared<VansParticleAsset>();
    if (!check(VansParticleAssetJsonCodec::Decode(json, {}, *asset, error), error.c_str())) return false;
    for (int i = 0; i < 3; ++i) manager.Queue(manager.Create(asset), VansParticleControl::Play);
    manager.TickMainThread(.5f); manager.WaitForUpdateAndSwap();
    const auto capped = manager.CaptureDebugSnapshot();
    if (!check(capped.truncated && capped.totalPoints > VansParticleDebugSnapshot::MaxPoints &&
        capped.capturedPoints == VansParticleDebugSnapshot::MaxPoints && manager.ActiveCount() == 3,
        "Debug point budget altered simulation or failed to bound the snapshot")) return false;
    manager.Shutdown();
    ribbon["spawn"]["rate"] = 20;
    ribbon["maxParticles"] = 16;
    json["emitters"] = Vans::ParticleJson::array({ribbon});
    asset = std::make_shared<VansParticleAsset>();
    if (!check(VansParticleAssetJsonCodec::Decode(json, {}, *asset, error), error.c_str())) return false;
    for (int i = 0; i < 260; ++i) manager.Queue(manager.Create(asset), VansParticleControl::Play);
    manager.TickMainThread(.11f); manager.WaitForUpdateAndSwap();
    const auto emitterCapped = manager.CaptureDebugSnapshot();
    if (!check(emitterCapped.totalEmitters == 260 && emitterCapped.emitters.size() == VansParticleDebugSnapshot::MaxEmitters &&
        emitterCapped.truncated, "Debug emitter budget is not enforced")) return false;
    std::cout << "[ParticleDebug] allInstances=1 mixedEmitters=1 exactPublishedNodes=1 ownedSnapshot=1 teardown=1 pointAndEmitterCaps=1\n";
    return true;
}
bool TestRibbonSimulation()
{
    using namespace VansGraphics;
    const auto check = [](bool result, const char* message)
    { if (!result) std::cerr << "[ParticleRibbon] " << message << '\n'; return result; };
    Vans::ParticleJson json = {
        {"name", "RibbonSmoke"},
        {"global", {{"duration", 0.75}, {"loop", false}, {"emissionFrame", "World"},
            {"fixedStep", 1.0/120}, {"maxSubsteps", 8}, {"drainFade", 0.45}}},
        {"emitters", Vans::ParticleJson::array({{
            {"name", "Smoke"}, {"maxParticles", 64},
            {"spawn", {{"type", "RateOverTime"}, {"rate", 48}}},
            {"initialize", Vans::ParticleJson::array({
                {{"module", "InitLifetime"}, {"lifetime", {{"mode", "Constant"}, {"value", 1.2}}}},
                {{"module", "InitVelocity"}, {"mode", "Cone"}, {"angle", 0}, {"speed", 0.15}},
                {{"module", "InitSize"}, {"size", {{"mode", "Constant"}, {"value", 0.008}}}}
            })},
            {"renderer", {{"type", "Ribbon"}, {"ribbon", {{"rootMode", "FollowSource"}, {"tipFadeDistance", 0.025}}}}}
        }})}
    };
    const auto decode = [&](const Vans::ParticleJson& source) {
        auto asset = std::make_shared<VansParticleAsset>(); std::string error;
        if (!VansParticleAssetJsonCodec::Decode(source, {}, *asset, error))
        { std::cerr << error << '\n'; return std::shared_ptr<VansParticleAsset>{}; }
        return asset;
    };
    auto asset = decode(json);
    if (!asset) return false;
    VansParticleManager refreshedManager;
    const auto refreshedHandle=refreshedManager.Create(asset);
    auto* refreshed=refreshedManager.Resolve(refreshedHandle);
    refreshed->SetOwnerWorldTransform(glm::mat4(1));
    refreshedManager.Queue(refreshedHandle,VansParticleControl::Play);
    for (int frame=0; frame<1200; ++frame)
    {
        if (frame>0 && frame%24==0)
        {
            const auto& before=refreshed->GetEmitter(0)->m_ParticlePool;
            const auto ids=before.m_SpawnSequence;
            const auto positions=before.m_Position;
            const auto ages=before.m_Age;
            const auto count=before.m_AliveCount;
            const auto playTime=refreshed->GetPlayTime();
            refreshedManager.Queue(refreshedHandle,VansParticleControl::RefreshEmission);
            refreshedManager.Prepare();
            const auto& after=refreshed->GetEmitter(0)->m_ParticlePool;
            if (!check(after.m_AliveCount==count && after.m_SpawnSequence==ids && after.m_Position==positions
                && after.m_Age==ages && refreshed->GetPlayTime()==playTime,
                "Refresh restarted or rewound existing smoke instead of extending its effect")) return false;
        }
        refreshedManager.TickMainThread(1.0f/120); refreshedManager.WaitForUpdateAndSwap();
        if (!check(refreshedManager.ActiveCount()==1 && refreshed->GetState()==VansParticlePlaybackState::Emitting,
            "Repeated pulses created or expired an active effect")) return false;
    }
    if (!check(refreshed->GetPlayTime()>9.99f && refreshed->m_AliveInstanceCount>0
        && refreshed->GetEmitter(0)->m_DroppedSpawns==0,"Sustained emission exhausted the point budget")) return false;
    for (int frame=0; frame<84; ++frame) { refreshedManager.TickMainThread(1.0f/120); refreshedManager.WaitForUpdateAndSwap(); }
    if (!check(refreshed->GetState()==VansParticlePlaybackState::Draining,"Last pulse did not govern the emission deadline")) return false;
    const auto drainingCount=refreshed->m_AliveInstanceCount.load();
    refreshedManager.Queue(refreshedHandle,VansParticleControl::RefreshEmission); refreshedManager.Prepare();
    if (!check(refreshed->GetState()==VansParticlePlaybackState::Emitting && refreshed->m_AliveInstanceCount==drainingCount,
        "A still-alive draining effect was cleared instead of revived")) return false;
    for (int frame=0; frame<180; ++frame) { refreshedManager.TickMainThread(1.0f/120); refreshedManager.WaitForUpdateAndSwap(); }
    if (!check(refreshed->IsFinished() && !refreshed->RefreshEmission() && refreshed->GetFrameData().ribbons.empty(),
        "Unrefreshed smoke did not expire or a finished effect was silently restarted")) return false;
    std::cout << "[ParticlePulse] sameInstance=1 pointsPreserved=1 sustained10s=1 reviveDrain=1 finalExpiry=1\n";
    for (const int fps : {30,60,120})
    {
        VansParticleRuntime runtime;
        runtime.SetAsset(asset); runtime.SetOwnerWorldTransform(glm::mat4(1)); runtime.Play();
        for (int frame=0; frame<fps/2; ++frame) { runtime.Update(1.0f/fps); runtime.SwapBuffers(); }
        const auto& data = runtime.GetFrameData();
        const auto& pool = runtime.GetEmitter(0)->m_ParticlePool;
        if (!check(pool.m_AliveCount == 24 && data.ribbons.size() == 1 && data.emitters.size() == 1
            && data.emitters[0].ribbonCount == 1 && data.instances.empty(),
            "Ribbon birth count or per-emitter output depends on frame rate")) return false;
        const auto& strip = data.ribbons[0];
        if (!check(strip.hasSourceRoot && strip.points.front().position == glm::vec3(0)
            && strip.points.back().position.y > 0.06f,
            "Stationary source did not form a rooted rising strip")) return false;
        if (!check(strip.points.back().color.a == 0 && strip.points.front().color.a > 0,
            "Ribbon free tip did not soften independently of its source root")) return false;
        const float oldestAge = (0.5f - 1.0f/48);
        if (!check(std::abs(strip.points.back().u-oldestAge) < 2.0e-5f
            && std::abs(strip.points.back().position.y-oldestAge*0.15f) < 2.0e-5f,
            "Subframe birth age or velocity integration is wrong")) return false;
        glm::mat4 moved(1); moved[3] = {0.1f,0,0,1};
        runtime.SetOwnerWorldTransform(moved); runtime.Update(1.0f/fps); runtime.SwapBuffers();
        const auto& shifted = runtime.GetFrameData().ribbons[0];
        if (!check(shifted.points.front().position.x == 0.1f && shifted.points.back().position.x == 0
            && !runtime.CanSeek() && !runtime.Seek(0.1f),
            "Moving source moved free points or allowed a history-free Seek")) return false;
        const auto aliveBeforeStop = runtime.m_AliveInstanceCount.load();
        runtime.StopEmitting();
        runtime.Update(1.0f/fps); runtime.SwapBuffers();
        if (!check(runtime.GetState() == VansParticlePlaybackState::Draining
            && runtime.m_AliveInstanceCount == aliveBeforeStop
            && runtime.GetFrameData().ribbons.front().points.front().position.x == 0.1f,
            "StopEmitting cleared the strip, emitted points or detached a retained root")) return false;
        for (int frame=0; frame<fps; ++frame) { runtime.Update(1.0f/fps); runtime.SwapBuffers(); }
        if (!check(runtime.IsFinished() && runtime.GetFrameData().ribbons.empty()
            && runtime.m_AliveInstanceCount == 0, "Root or invisible smoke never completed")) return false;
    }
    VansParticleRuntime broken;
    broken.SetAsset(asset); broken.SetOwnerWorldTransform(glm::mat4(1)); broken.Play();
    for (int frame=0; frame<30; ++frame) broken.Update(1.0f/120);
    auto& pool = broken.GetEmitter(0)->m_ParticlePool;
    pool.SwapRemoveAt(5);
    broken.Update(0); broken.SwapBuffers();
    if (!check(broken.GetFrameData().ribbons.size() == 2,
        "SwapRemove erased a middle-point gap and connected unrelated neighbors")) return false;
    broken.Update(2); broken.SwapBuffers();
    if (!check(broken.m_SubstepOverruns == 1 && broken.IsFinished(),
        "A long hitch accumulated an unbounded backlog or retained expired smoke")) return false;
    VansParticleRuntime lowFps;
    lowFps.SetAsset(asset); lowFps.SetOwnerWorldTransform(glm::mat4(1)); lowFps.Play();
    for (int frame=0; frame<4; ++frame)
    {
        lowFps.Update(0.1f); lowFps.SwapBuffers();
        const auto& strips = lowFps.GetFrameData().ribbons;
        if (!check(std::any_of(strips.begin(),strips.end(),[](const auto& strip) { return strip.hasSourceRoot; }),
            "Sustained low frame rate discarded the latest source root instead of old history")) return false;
    }
    if (!check(lowFps.m_SubstepOverruns == 4 && lowFps.GetEmitter(0)->m_DroppedSpawns > 0,
        "Low frame rate bypassed the bounded catch-up policy")) return false;
    auto delayedJson = json;
    delayedJson["global"]["startDelay"] = 0.95;
    delayedJson["emitters"][0]["spawn"]["rate"] = 240;
    auto delayedAsset = decode(delayedJson); if (!delayedAsset) return false;
    VansParticleRuntime delayed;
    delayed.SetAsset(delayedAsset); delayed.SetOwnerWorldTransform(glm::mat4(1)); delayed.Play();
    delayed.Update(0.9f);
    glm::mat4 movedDuringDelay(1); movedDuringDelay[3] = {100,0,0,1};
    delayed.SetOwnerWorldTransform(movedDuringDelay); delayed.Update(0.2f);
    const auto& delayedPool = delayed.GetEmitter(0)->m_ParticlePool;
    if (!check(delayedPool.m_AliveCount > 0,"Delayed emission never started")) return false;
    for (uint32_t i=0; i<delayedPool.m_AliveCount; ++i)
        if (!check(delayedPool.m_Position[i].x >= 24.99f,
            "Delayed emitter generated points on source history before emission started")) return false;
    auto burstJson = json;
    burstJson["global"] = {{"duration",5},{"loop",false}};
    burstJson["emitters"][0]["renderer"] = {{"type","Billboard"}};
    burstJson["emitters"][0]["spawn"] = {{"type","Burst"},{"bursts",Vans::ParticleJson::array({
        {{"time",0.1},{"count",3},{"cycles",3},{"interval",0.2}}
    })}};
    auto burstAsset = decode(burstJson); if (!burstAsset) return false;
    VansParticleRuntime burst; burst.SetAsset(burstAsset); burst.Play();
    burst.Update(0.05f); burst.Update(0.049f);
    if (!check(burst.m_AliveInstanceCount == 0,"Burst fired before its scheduled time")) return false;
    burst.Update(0.002f);
    if (!check(burst.m_AliveInstanceCount == 3,"Scheduled Burst did not fire exactly once")) return false;
    burst.Update(0.4f);
    if (!check(burst.m_AliveInstanceCount == 9,"A frame spanning two Burst events lost or duplicated events")) return false;
    burstJson["emitters"][0]["spawn"]["bursts"][0]["cycles"] = 0;
    burstJson["emitters"][0]["spawn"]["bursts"][0]["interval"] = 0;
    VansParticleAsset invalid; std::string error;
    if (!check(!VansParticleAssetJsonCodec::Decode(burstJson,{},invalid,error),
        "An infinite zero-interval Burst was accepted")) return false;
    std::cout << "[ParticleRibbon] fps30_60_120=1 rootedWorldMotion=1 stopDrain=1 middleGap=1 hitchBudget=1 scheduledBurst=1\n";
    return true;
}
}

bool TestParticleCoreContract()
{
    using namespace VansGraphics;
    const auto check = [](bool result, const char* message)
    { if (!result) std::cerr << "[ParticleCore] " << message << '\n'; return result; };
    auto asset = std::make_shared<VansParticleAsset>();
    std::string error;
    const Vans::ParticleJson json = {
        {"name", "SharedDefinition"},
        {"global", {{"duration", 5}, {"loop", true}, {"emissionFrame", "World"}}},
        {"emitters", Vans::ParticleJson::array({{
            {"name", "Smoke"}, {"maxParticles", 16},
            {"spawn", {{"type", "RateOverTime"}, {"rate", 20}}},
            {"initialize", Vans::ParticleJson::array({
                {{"module", "InitLifetime"}, {"lifetime", {{"mode", "Constant"}, {"value", 5}}}}
            })},
            {"renderer", {{"type", "Billboard"}}}
        }})}
    };
    if (!check(VansParticleAssetJsonCodec::Decode(json, {}, *asset, error), error.c_str())) return false;
    const auto definitionBefore = VansParticleAssetJsonCodec::Encode(*asset);
    VansParticleManager manager;
    const auto a = manager.Create(asset), b = manager.Create(asset);
    manager.Resolve(a)->m_LocalToWorld[3] = {1, 2, 3, 1};
    manager.Resolve(b)->m_LocalToWorld[3] = {7, 8, 9, 1};
    manager.Queue(a, VansParticleControl::Play);
    manager.Queue(b, VansParticleControl::Play);
    manager.TickMainThread(0.11f); manager.WaitForUpdateAndSwap();
    auto* first = manager.Resolve(a);
    auto* second = manager.Resolve(b);
    if (!check(first->GetAsset().get() == second->GetAsset().get()
        && first->GetRenderBuffer().size() == 2 && second->GetRenderBuffer().size() == 2
        && first->GetRenderBuffer()[0].m_WorldPosition.x == 1
        && second->GetRenderBuffer()[0].m_WorldPosition.x == 7,
        "Shared definitions contaminated independent particle pools")) return false;
    const auto frozen = first->GetRenderBuffer();
    manager.Queue(a, VansParticleControl::Pause);
    for (int frame = 0; frame < 4; ++frame)
    {
        manager.TickMainThread(0.11f); manager.WaitForUpdateAndSwap();
        if (!check(first->GetRenderBuffer().size() == frozen.size()
            && first->GetRenderBuffer()[0].m_WorldPosition == frozen[0].m_WorldPosition,
            "Paused double buffers alternated old simulation frames")) return false;
    }
    if (!check(second->GetRenderBuffer().size() > frozen.size()
        && VansParticleAssetJsonCodec::Encode(*asset) == definitionBefore,
        "Playing one instance changed another instance or its shared definition")) return false;
    manager.Queue(a, VansParticleControl::Seed, 0, 12345);
    if (!check(manager.Destroy(a), "Instance release failed")) return false;
    const auto replacement = manager.Create(asset);
    manager.Prepare();
    if (!check(replacement.index == a.index && replacement.generation != a.generation
        && !manager.Resolve(a) && !manager.Queue(a, VansParticleControl::Play)
        && manager.Resolve(replacement)->GetRandomSeed() != 12345,
        "Stale handle or queued command reached a replacement instance")) return false;
    manager.TickMainThread(0.01f);
    manager.Shutdown();
    if (!check(manager.ActiveCount() == 0 && !manager.Resolve(b),
        "Shutdown did not join simulation and invalidate all instances")) return false;
    std::cout << "[ParticleCore] immutableDefinition=1 isolatedInstances=1 pausedSnapshot=1 staleHandle=1 shutdown=1\n";
    return TestRibbonSimulation() && TestPolylineGeometry() && TestVFXServiceLifetime() && TestParticleAuthoring() && TestParticleDebugSnapshot();
}
