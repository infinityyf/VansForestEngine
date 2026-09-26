#include "../EngineCore/ParticleCore/VansParticleManager.h"
#include "../EngineCore/ParticleCore/Serialization/VansParticleAssetJsonCodec.h"
#include "../EngineCore/ParticleCore/Serialization/VansParticleEmitterJsonCodec.h"
#include "../EngineCore/RenderCore/GeometryCore/VansPolylineMeshBuilder.h"
#include "../EngineCore/RenderCore/VansSceneAssetRegistry.h"
#include "../EngineCore/RenderCore/VulkanCore/VansRenderGraphVulkanSync.h"
#include "../EngineCore/GameplayActionAdapters/VFX/VansVFXActionService.h"
#include "../EngineCore/GameplayActionCore/VansGameplayRuntime.h"
#include "../EngineCore/ParticleCore/Authoring/VansParticleAuthoringSchema.h"
#include "../EngineCore/ParticleCore/Storage/VansParticleAssetStorage.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedPathPattern.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace
{
using ParticleTestJson = nlohmann::json;

ParticleTestJson EncodeParticleAsset(const VansGraphics::VansParticleAsset& asset)
{
    return Vans::EncodeSerializedValueJson<ParticleTestJson>(
        VansGraphics::VansParticleAssetJsonCodec::Encode(asset));
}

bool DecodeParticleAsset(
    const ParticleTestJson& root,
    const std::filesystem::path& path,
    VansGraphics::VansParticleAsset& asset,
    std::string& error)
{
    return VansGraphics::VansParticleAssetJsonCodec::Decode(
        Vans::DecodeSerializedValueJson(root), path, asset, error);
}

class UnsupportedParticleModule final : public VansGraphics::VansParticleModule
{
public:
    void Execute(VansGraphics::VansParticlePool&, float, const glm::mat4&) const override {}
};

bool TestParticleAuthoring()
{
    using namespace VansGraphics;
    const auto check = [](bool ok, const std::string& error)
    { if (!ok) std::cerr << "[ParticleAuthoring] " << error << '\n'; return ok; };
    const auto& moduleSchemas = VansParticleEmitterJsonCodec::ModuleSchemas();
    const auto defaults = Vans::EncodeSerializedValueJson<ParticleTestJson>(
        VansParticleAuthoringSchema::Defaults());
    std::unordered_set<std::string> moduleNames;
    size_t initializeCount = 0;
    for (size_t index = 0; index < moduleSchemas.size(); ++index)
    {
        const auto& schema = moduleSchemas[index];
        const auto& moduleDefault = defaults.at("modules").at(index);
        const bool initialize = schema.phase == VansParticleModulePhase::Initialize;
        initializeCount += initialize ? 1u : 0u;
        if (!check(moduleNames.insert(schema.name).second &&
            moduleDefault.at("phase") == (initialize ? "initialize" : "update") &&
            moduleDefault.at("definition").at("module") == schema.name,
            "Module catalog identity, phase or constructor default diverged")) return false;
    }
    if (!check(moduleSchemas.size() == 12 && initializeCount == 6,
        "Module catalog does not own all six initialize and six update modules")) return false;
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
    if (!check(DecodeParticleAsset(json,{},asset,error),error)) return false;
    const auto roundTrip = EncodeParticleAsset(asset);
    VansParticleAsset second;
    if (!check(DecodeParticleAsset(roundTrip,{},second,error) &&
        EncodeParticleAsset(second) == roundTrip,"Module constructor defaults do not round-trip")) return false;
    const Vans::VansSerializedValue serializedDefinition =
        VansParticleAssetJsonCodec::Encode(asset);
    VansParticleAsset serializedSecond;
    if (!check(serializedDefinition.kind == Vans::VansSerializedValue::Kind::Object &&
        VansParticleAssetJsonCodec::Decode(
            serializedDefinition, {}, serializedSecond, error) &&
        Vans::SerializedValuesEqual(
            serializedDefinition, VansParticleAssetJsonCodec::Encode(serializedSecond)),
        "The public particle codec did not round-trip through VansSerializedValue")) return false;
    VansParticleEmitter unsupportedEmitter;
    unsupportedEmitter.m_InitModules.push_back(std::make_unique<UnsupportedParticleModule>());
    bool unsupportedEncodeRejected = false;
    try
    {
        (void)VansParticleEmitterJsonCodec::EncodeEmitter(unsupportedEmitter);
    }
    catch (const std::invalid_argument&)
    {
        unsupportedEncodeRejected = true;
    }
    if (!check(unsupportedEncodeRejected,
        "Unsupported runtime module was silently omitted while saving")) return false;
    VansParticleAsset unsupportedAsset;
    auto unsupportedAssetEmitter = std::make_unique<VansParticleEmitter>();
    unsupportedAssetEmitter->m_InitModules.push_back(std::make_unique<UnsupportedParticleModule>());
    unsupportedAsset.m_Emitters.push_back(std::move(unsupportedAssetEmitter));
    if (!check(!VansParticleAssetStorage::SaveAtomic(
            "UnsupportedParticleModule.particle", unsupportedAsset, error)
        && error.find("Unsupported particle module type") != std::string::npos,
        "Particle storage did not report an unsupported module encode failure")) return false;
    for (const auto& invalid : std::vector<ParticleTestJson>{
        {{"renderer",{{"type","Mesh"}}}}, {{"renderer",{{"textureGuid","Assets/smoke.png"}}}},
        {{"renderer",{{"sortMode","ByDistance"}}}},
        {{"renderer",{{"simulationOrder","OldestFirst"},{"renderSortMode","ByDistance"}}}},
        {{"initialize",ParticleTestJson::array({{{"module","UpdateGravity"}}})}},
        {{"update",ParticleTestJson::array({{{"module","UnknownModule"}}})}},
        {{"renderer",{{"type","Ribbon"},{"lightingMode","SixWayLit"}}}} })
    {
        auto bad = json;
        for (const auto& field : invalid.items()) bad["emitters"][0][field.key()] = field.value();
        if (!check(!DecodeParticleAsset(bad,{},second,error),"Invalid renderer, GUID or module phase was accepted")) return false;
    }
    json["emitters"][0]["renderer"]["ribbon"]["rootWidth"] = 0.023;
    json["global"]["fixedStep"] = 1.0f/30.0f;
    json["emitters"][0]["renderer"]["volumetric"]["anisotropy"] = -0.9;
    if (!check(DecodeParticleAsset(json,{},asset,error) &&
        std::abs(EncodeParticleAsset(asset)["emitters"][0]["renderer"]["ribbon"]["rootWidth"].get<float>()-0.023f)<1.0e-6f,
        "Inactive Ribbon parameters were lost when editing Billboard")) return false;
    if (!check(DecodeParticleAsset(EncodeParticleAsset(asset),{},second,error),
        "Float precision at a supported scalar boundary did not round-trip: " + error)) return false;
    for (const auto& invalidModuleField : std::vector<ParticleTestJson>{
        {{"phase", "initialize"}, {"index", 1}, {"field", "speed"}, {"value", 10001.0}},
        {{"phase", "update"}, {"index", 3}, {"field", "drag"}, {"value", 101.0}},
        {{"phase", "update"}, {"index", 5}, {"field", "columns"}, {"value", 257}}
    })
    {
        auto bad = json;
        const auto phase = invalidModuleField.at("phase").get<std::string>();
        const auto index = invalidModuleField.at("index").get<size_t>();
        const auto field = invalidModuleField.at("field").get<std::string>();
        bad["emitters"][0][phase][index][field] = invalidModuleField.at("value");
        if (!check(!DecodeParticleAsset(bad, {}, second, error),
            "A module field outside its catalog constraint was accepted")) return false;
    }
    if (!check(Vans::MatchSerializedPathPattern("/emitters/*/renderer/type","/emitters/12/renderer/type") &&
        !Vans::MatchSerializedPathPattern("/emitters/*/type","/emitters/12/renderer/type"),"Authoring path wildcard crossed an object boundary")) return false;
    namespace fs = std::filesystem;
    auto workspace = fs::current_path();
    for (int depth=0; depth<6 && !fs::exists(workspace/"DemoHallProject"); ++depth) workspace=workspace.parent_path();
    const fs::path particleCore = workspace / "ForestEngine" / "ForestEngine" /
        "Source" / "EngineCore" / "ParticleCore";
    const auto readText = [](const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    };
    const std::string assetCodecHeader = readText(
        particleCore / "Serialization" / "VansParticleAssetJsonCodec.h");
    const std::string emitterCodecHeader = readText(
        particleCore / "Serialization" / "VansParticleEmitterJsonCodec.h");
    const std::string authoringHeader = readText(
        particleCore / "Authoring" / "VansParticleAuthoringSchema.h");
    const std::string storageSource = readText(
        particleCore / "Storage" / "VansParticleAssetStorage.cpp");
    if (!check(!fs::exists(particleCore / "VansParticleJson.h") &&
        assetCodecHeader.find("VansSerializedValue") != std::string::npos &&
        emitterCodecHeader.find("VansSerializedValue") != std::string::npos &&
        authoringHeader.find("VansSerializedValue") != std::string::npos &&
        assetCodecHeader.find("nlohmann") == std::string::npos &&
        emitterCodecHeader.find("nlohmann") == std::string::npos &&
        authoringHeader.find("nlohmann") == std::string::npos &&
        storageSource.find("VansAssetDocument document") != std::string::npos &&
        storageSource.find("VansJsonFileStorage::StageWrite") != std::string::npos,
        "Particle serialization still leaks the retired public JSON value model")) return false;
    unsigned count=0;
    unsigned renderSortedEmitterCount=0;
    for (const auto* path : {"AnimationV2Project/Assets", "DustV3Project/Assets",
        "DemoHallProject/Assets", "SponzaProject/Assets"})
    {
        if (!fs::is_directory(workspace/path)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(workspace/path))
        {
            if (entry.path().extension()!=".particle") continue;
            if (!check(VansParticleAssetStorage::Load(entry.path(),asset,error),error)) return false;
            const auto encoded=EncodeParticleAsset(asset);
            for (const auto& emitterRoot : encoded.at("emitters"))
            {
                const auto& rendererRoot = emitterRoot.at("renderer");
                if (!check(!rendererRoot.contains("sortMode") &&
                    rendererRoot.contains("simulationOrder") &&
                    rendererRoot.contains("renderSortMode"),
                    entry.path().string()+" retained the retired mixed sort policy")) return false;
                if (rendererRoot.at("renderSortMode") == "ByDistance")
                    ++renderSortedEmitterCount;
            }
            if (!check(DecodeParticleAsset(encoded,entry.path(),second,error) &&
                EncodeParticleAsset(second)==encoded,entry.path().string()+" changed on a second round trip")) return false;
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
    if (!check(count>=8 && renderSortedEmitterCount>=1,
        "Production particle assets or render-owned sort policies were not validated")) return false;
    std::cout << "[ParticleAuthoring] moduleCatalog=12 constructorDefaults=1 catalogConstraints=1 activeAndInactiveRoundTrip=1 invalidConfiguration=1 unsupportedEncodeRejected=1 serializedValueCodec=1 assets="
        << count << " renderSortedEmitters=" << renderSortedEmitterCount << '\n';
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
        const auto result = VansPolylineMeshBuilder::Append(points,camera,{1,0,0},{0,1,0},mesh);
        if (!check(result.viewValid && result.verticesAdded==6 && result.indicesAdded==12
            && result.rejectedPointCount==0 && result.splitRunCount==0
            && mesh.vertices.size()==6 && mesh.indices.size()==12,
            "Coincident points or camera-aligned segment broke topology")) return false;
        for (const auto& vertex : mesh.vertices)
            if (!check(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y)
                && std::isfinite(vertex.position.z) && std::abs(vertex.position.x) <= 0.021f
                && vertex.uv.x >= 0 && vertex.uv.x <= 2,"Degenerate camera generated invalid geometry or UV")) return false;
        for (auto index : mesh.indices) if (!check(index<mesh.vertices.size(),"Out-of-range mesh index")) return false;
    }
    VansPolylineMesh fold;
    const auto foldResult = VansPolylineMeshBuilder::Append(
        {point({0,0,0},0),point({0,1,0},1),point({0,0,0},2)}, {0,0,3},{1,0,0},{0,1,0},fold);
    if (!check(foldResult.viewValid && foldResult.verticesAdded==8 && foldResult.indicesAdded==12,
        "Reversal result did not report appended geometry")) return false;
    if (!check(fold.vertices.size()==8 && fold.indices.size()==12,"Reversal created a bridging triangle")) return false;
    VansPolylineMesh large;
    std::vector<VansPolylinePoint> straight;
    for (int i=0;i<64;++i) straight.push_back(point({0,float(i)*0.01f,0},float(i)*0.01f));
    const auto largeResult = VansPolylineMeshBuilder::Append(straight,{0,0,3},{1,0,0},{0,1,0},large);
    if (!check(largeResult.verticesAdded==128 && largeResult.indicesAdded==378,
        "Straight strip result did not report linear geometry")) return false;
    if (!check(large.vertices.size()==128 && large.indices.size()==378,"Straight strip exceeded the linear geometry budget")) return false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    VansPolylineMesh rejected;
    const auto rejectedResult = VansPolylineMeshBuilder::Append(
        {point({0,0,0},0),point({0,1,0},1),point({nan,0,0},2),point({0,2,0},2),point({0,3,0},3)},
        {0,0,3},{1,0,0},{0,1,0},rejected);
    if (!check(rejectedResult.viewValid && rejectedResult.rejectedPointCount==1
        && rejectedResult.splitRunCount==1 && rejectedResult.verticesAdded==8
        && rejectedResult.indicesAdded==12,
        "Invalid point did not report and split the two valid runs")) return false;
    VansPolylineMesh invalidView;
    const auto invalidViewResult = VansPolylineMeshBuilder::Append(straight,{nan,0,0},{1,0,0},{0,1,0},invalidView);
    if (!check(!invalidViewResult.viewValid && invalidView.vertices.empty() && invalidView.indices.empty(),
        "Invalid view did not fail without mutating output")) return false;
    const auto sync = VansRenderGraphVulkanSyncMapper::MapResourceUsage(VansRenderResourceUsage::DepthStencilAttachmentSampledRead);
    if (!check(sync.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        && (sync.accessMask&VK_ACCESS_SHADER_READ_BIT) && (sync.accessMask&VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
        && (sync.stageMask&VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),"Soft particle depth dependency is incomplete")) return false;
	std::cout << "[Polyline] cameraDegeneracy=1 duplicatePoints=1 reversalBreak=1 linearBudget=1 invalidInput=1 depthRead=1\n";
    return true;
}
bool TestParticleDebugSnapshot()
{
    using namespace VansGraphics;
    const auto check = [](bool ok, const char* message) {
        if (!ok) std::cerr << "[ParticleDebug] " << message << '\n'; return ok;
    };
    ParticleTestJson ribbon = {
        {"name", "RibbonA"}, {"maxParticles", 4096},
        {"spawn", {{"type", "RateOverTime"}, {"rate", 40}}},
        {"initialize", ParticleTestJson::array({
            {{"module", "InitLifetime"}, {"lifetime", {{"mode", "Constant"}, {"value", 5}}}},
            {{"module", "InitVelocity"}, {"mode", "Cone"}, {"angle", 0}, {"speed", .2}}
        })},
        {"renderer", {{"type", "Ribbon"}, {"ribbon", {{"rootMode", "FollowSource"}}}}}
    };
    auto billboard = ribbon; billboard["name"] = "Billboard"; billboard["renderer"]["type"] = "Billboard";
    auto other = ribbon; other["name"] = "RibbonB";
    ParticleTestJson json = {{"name", "DebugEffect"}, {"global", {{"duration", 5}, {"loop", true}}},
        {"emitters", ParticleTestJson::array({ribbon, billboard, other})}};
    auto asset = std::make_shared<VansParticleAsset>(); std::string error;
    if (!check(DecodeParticleAsset(json, {}, *asset, error), error.c_str())) return false;
    VansParticleManager manager;
    const auto first = manager.Create(asset), second = manager.Create(asset);
    manager.SetOwnerWorldTransform(first,glm::mat4(1));
    auto world = glm::mat4(1); world[3] = {2, 3, 4, 1};
    manager.SetOwnerWorldTransform(second,world);
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
    json["emitters"] = ParticleTestJson::array({ribbon});
    asset = std::make_shared<VansParticleAsset>();
    if (!check(DecodeParticleAsset(json, {}, *asset, error), error.c_str())) return false;
    for (int i = 0; i < 3; ++i) manager.Queue(manager.Create(asset), VansParticleControl::Play);
    manager.TickMainThread(.5f); manager.WaitForUpdateAndSwap();
    const auto capped = manager.CaptureDebugSnapshot();
    if (!check(capped.truncated && capped.totalPoints > VansParticleDebugSnapshot::MaxPoints &&
        capped.capturedPoints == VansParticleDebugSnapshot::MaxPoints && manager.ActiveCount() == 3,
        "Debug point budget altered simulation or failed to bound the snapshot")) return false;
    manager.Shutdown();
    ribbon["spawn"]["rate"] = 20;
    ribbon["maxParticles"] = 16;
    json["emitters"] = ParticleTestJson::array({ribbon});
    asset = std::make_shared<VansParticleAsset>();
    if (!check(DecodeParticleAsset(json, {}, *asset, error), error.c_str())) return false;
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
    ParticleTestJson json = {
        {"name", "RibbonSmoke"},
        {"global", {{"duration", 0.75}, {"loop", false}, {"emissionFrame", "World"},
            {"fixedStep", 1.0/120}, {"maxSubsteps", 8}, {"drainFade", 0.45}}},
        {"emitters", ParticleTestJson::array({{
            {"name", "Smoke"}, {"maxParticles", 64},
            {"spawn", {{"type", "RateOverTime"}, {"rate", 48}}},
            {"initialize", ParticleTestJson::array({
                {{"module", "InitLifetime"}, {"lifetime", {{"mode", "Constant"}, {"value", 1.2}}}},
                {{"module", "InitVelocity"}, {"mode", "Cone"}, {"angle", 0}, {"speed", 0.15}},
                {{"module", "InitSize"}, {"size", {{"mode", "Constant"}, {"value", 0.008}}}}
            })},
            {"renderer", {{"type", "Ribbon"}, {"ribbon", {{"rootMode", "FollowSource"}, {"tipFadeDistance", 0.025}}}}}
        }})}
    };
    const auto decode = [&](const ParticleTestJson& source) {
        auto asset = std::make_shared<VansParticleAsset>(); std::string error;
        if (!DecodeParticleAsset(source, {}, *asset, error))
        { std::cerr << error << '\n'; return std::shared_ptr<VansParticleAsset>{}; }
        return asset;
    };
    auto asset = decode(json);
    if (!asset) return false;
    VansParticleManager refreshedManager;
    const auto refreshedHandle=refreshedManager.Create(asset);
    const auto* refreshed=refreshedManager.Resolve(refreshedHandle);
    refreshedManager.SetOwnerWorldTransform(refreshedHandle,glm::mat4(1));
    refreshedManager.Queue(refreshedHandle,VansParticleControl::Play);
    for (int frame=0; frame<1200; ++frame)
    {
        if (frame>0 && frame%24==0)
        {
            const auto& before=refreshed->GetEmitter(0)->ParticlePool();
            const auto ids=before.m_SpawnSequence;
            const auto positions=before.m_Position;
            const auto ages=before.m_Age;
            const auto count=before.m_AliveCount;
            const auto playTime=refreshed->GetPlayTime();
            refreshedManager.Queue(refreshedHandle,VansParticleControl::RefreshEmission);
            refreshedManager.TickMainThread(0); refreshedManager.WaitForUpdateAndSwap();
            const auto& after=refreshed->GetEmitter(0)->ParticlePool();
            if (!check(after.m_AliveCount==count && after.m_SpawnSequence==ids && after.m_Position==positions
                && after.m_Age==ages && refreshed->GetPlayTime()==playTime,
                "Refresh restarted or rewound existing smoke instead of extending its effect")) return false;
        }
        refreshedManager.TickMainThread(1.0f/120); refreshedManager.WaitForUpdateAndSwap();
        if (!check(refreshedManager.ActiveCount()==1 && refreshed->GetState()==VansParticlePlaybackState::Emitting,
            "Repeated pulses created or expired an active effect")) return false;
    }
    if (!check(refreshed->GetPlayTime()>9.99f && refreshed->AliveInstanceCount()>0
        && refreshed->GetEmitter(0)->DroppedSpawns()==0,"Sustained emission exhausted the point budget")) return false;
    for (int frame=0; frame<84; ++frame) { refreshedManager.TickMainThread(1.0f/120); refreshedManager.WaitForUpdateAndSwap(); }
    if (!check(refreshed->GetState()==VansParticlePlaybackState::Draining,"Last pulse did not govern the emission deadline")) return false;
    const auto drainingCount=refreshed->AliveInstanceCount();
    refreshedManager.Queue(refreshedHandle,VansParticleControl::RefreshEmission);
    refreshedManager.TickMainThread(0); refreshedManager.WaitForUpdateAndSwap();
    if (!check(refreshed->GetState()==VansParticlePlaybackState::Emitting && refreshed->AliveInstanceCount()==drainingCount,
        "A still-alive draining effect was cleared instead of revived")) return false;
    for (int frame=0; frame<180; ++frame) { refreshedManager.TickMainThread(1.0f/120); refreshedManager.WaitForUpdateAndSwap(); }
    if (!check(refreshed->IsFinished() && refreshed->GetFrameData().ribbons.empty(),
        "Unrefreshed smoke did not expire")) return false;
    refreshedManager.Queue(refreshedHandle,VansParticleControl::RefreshEmission);
    refreshedManager.TickMainThread(0); refreshedManager.WaitForUpdateAndSwap();
    if (!check(refreshed->IsFinished() && refreshed->GetFrameData().ribbons.empty(),
        "A finished effect was silently restarted")) return false;
    std::cout << "[ParticlePulse] sameInstance=1 pointsPreserved=1 sustained10s=1 reviveDrain=1 finalExpiry=1\n";
    for (const int fps : {30,60,120})
    {
        VansParticleRuntime runtime;
        runtime.SetAsset(asset); runtime.SetOwnerWorldTransform(glm::mat4(1)); runtime.Play();
        for (int frame=0; frame<fps/2; ++frame) { runtime.Update(1.0f/fps); runtime.SwapBuffers(); }
        const auto& data = runtime.GetFrameData();
        const auto& pool = runtime.GetEmitter(0)->ParticlePool();
        if (!check(pool.m_AliveCount == 24 && data.ribbons.size() == 1 && data.emitters.size() == 1
            && data.emitters[0].ribbonCount == 1 && data.instances.empty(),
            "Ribbon birth count or per-emitter output depends on frame rate")) return false;
        const auto& strip = data.ribbons[0];
        if (!check(strip.hasSourceRoot && strip.points.front().position == glm::vec3(0)
            && strip.points.back().position.y > 0.06f,
            "Stationary source did not form a rooted rising strip")) return false;
        for (std::size_t pointIndex = 1; pointIndex < strip.points.size(); ++pointIndex)
            if (!check(strip.points[pointIndex - 1].sequence ==
                strip.points[pointIndex].sequence + 1,
                "ParticleCore published a Ribbon strip outside the descending sequence contract")) return false;
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
        const auto aliveBeforeStop = runtime.AliveInstanceCount();
        runtime.StopEmitting();
        runtime.Update(1.0f/fps); runtime.SwapBuffers();
        if (!check(runtime.GetState() == VansParticlePlaybackState::Draining
            && runtime.AliveInstanceCount() == aliveBeforeStop
            && runtime.GetFrameData().ribbons.front().points.front().position.x == 0.1f,
            "StopEmitting cleared the strip, emitted points or detached a retained root")) return false;
        for (int frame=0; frame<fps; ++frame) { runtime.Update(1.0f/fps); runtime.SwapBuffers(); }
        if (!check(runtime.IsFinished() && runtime.GetFrameData().ribbons.empty()
            && runtime.AliveInstanceCount() == 0, "Root or invisible smoke never completed")) return false;
    }
    VansParticleRuntime broken;
    broken.SetAsset(asset); broken.SetOwnerWorldTransform(glm::mat4(1)); broken.Play();
    for (int frame=0; frame<30; ++frame) broken.Update(1.0f/120);
    auto gapJson = json;
    gapJson["global"]["duration"] = 1.0;
    gapJson["emitters"][0]["initialize"][0]["lifetime"] =
        {{"mode","RandomBetween"},{"min",0.05},{"max",0.5}};
    auto gapAsset = decode(gapJson); if (!gapAsset) return false;
    VansParticleRuntime gapRuntime;
    gapRuntime.SetAsset(gapAsset); gapRuntime.SetOwnerWorldTransform(glm::mat4(1)); gapRuntime.Play();
    bool observedMiddleGap = false;
    for (int frame=0; frame<55 && !observedMiddleGap; ++frame)
    {
        gapRuntime.Update(1.0f/120); gapRuntime.SwapBuffers();
        observedMiddleGap = gapRuntime.GetFrameData().ribbons.size() > 1;
    }
    if (!check(observedMiddleGap,
        "Natural particle expiry erased a middle-point gap and connected unrelated neighbors")) return false;
    broken.Update(2); broken.SwapBuffers();
    if (!check(broken.SubstepOverruns() == 1 && broken.IsFinished(),
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
    if (!check(lowFps.SubstepOverruns() == 4 && lowFps.GetEmitter(0)->DroppedSpawns() > 0,
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
    const auto& delayedPool = delayed.GetEmitter(0)->ParticlePool();
    if (!check(delayedPool.m_AliveCount > 0,"Delayed emission never started")) return false;
    for (uint32_t i=0; i<delayedPool.m_AliveCount; ++i)
        if (!check(delayedPool.m_Position[i].x >= 24.99f,
            "Delayed emitter generated points on source history before emission started")) return false;
    auto burstJson = json;
    burstJson["global"] = {{"duration",5},{"loop",false}};
    burstJson["emitters"][0]["renderer"] = {{"type","Billboard"}};
    burstJson["emitters"][0]["spawn"] = {{"type","Burst"},{"bursts",ParticleTestJson::array({
        {{"time",0.1},{"count",3},{"cycles",3},{"interval",0.2}}
    })}};
    auto burstAsset = decode(burstJson); if (!burstAsset) return false;
    VansParticleRuntime burst; burst.SetAsset(burstAsset); burst.Play();
    burst.Update(0.05f); burst.Update(0.049f);
    if (!check(burst.AliveInstanceCount() == 0,"Burst fired before its scheduled time")) return false;
    burst.Update(0.002f);
    if (!check(burst.AliveInstanceCount() == 3,"Scheduled Burst did not fire exactly once")) return false;
    burst.Update(0.4f);
    if (!check(burst.AliveInstanceCount() == 9,"A frame spanning two Burst events lost or duplicated events")) return false;
    burstJson["emitters"][0]["spawn"]["bursts"][0]["cycles"] = 0;
    burstJson["emitters"][0]["spawn"]["bursts"][0]["interval"] = 0;
    VansParticleAsset invalid; std::string error;
    if (!check(!DecodeParticleAsset(burstJson,{},invalid,error),
        "An infinite zero-interval Burst was accepted")) return false;
    std::cout << "[ParticleRibbon] fps30_60_120=1 rootedWorldMotion=1 sequenceContract=1 stopDrain=1 middleGap=1 hitchBudget=1 scheduledBurst=1\n";
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
    const ParticleTestJson json = {
        {"name", "SharedDefinition"},
        {"global", {{"duration", 5}, {"loop", true}, {"emissionFrame", "World"}}},
        {"emitters", ParticleTestJson::array({{
            {"name", "Smoke"}, {"maxParticles", 16},
            {"spawn", {{"type", "RateOverTime"}, {"rate", 20}}},
            {"initialize", ParticleTestJson::array({
                {{"module", "InitLifetime"}, {"lifetime", {{"mode", "Constant"}, {"value", 5}}}}
            })},
            {"renderer", {{"type", "Billboard"},
                {"simulationOrder", "Stable"}, {"renderSortMode", "ByDistance"}}}
        }})}
    };
    if (!check(DecodeParticleAsset(json, {}, *asset, error), error.c_str())) return false;
    const auto definitionBefore = EncodeParticleAsset(*asset);
    VansParticleManager manager;
    const auto a = manager.Create(asset), b = manager.Create(asset);
    glm::mat4 firstTransform(1); firstTransform[3] = {1, 2, 3, 1};
    glm::mat4 secondTransform(1); secondTransform[3] = {7, 8, 9, 1};
    manager.SetOwnerWorldTransform(a,firstTransform);
    manager.SetOwnerWorldTransform(b,secondTransform);
    manager.Queue(a, VansParticleControl::Play);
    manager.Queue(b, VansParticleControl::Play);
    manager.TickMainThread(0.11f); manager.WaitForUpdateAndSwap();
    const auto* first = manager.Resolve(a);
    const auto* second = manager.Resolve(b);
    if (!check(first->GetAsset().get() == second->GetAsset().get()
        && first->GetRenderBuffer().size() == 2 && second->GetRenderBuffer().size() == 2
        && first->GetFrameData().emitters.size() == 1
        && first->GetFrameData().emitters[0].renderSortMode == VansParticleRenderSortMode::ByDistance
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
        && EncodeParticleAsset(*asset) == definitionBefore,
        "Playing one instance changed another instance or its shared definition")) return false;
    const auto editorFrozenFirst = first->GetRenderBuffer();
    const auto editorFrozenSecond = second->GetRenderBuffer();
    const float editorFrozenTime = second->GetPlayTime();
    manager.SetSimulationFrozen(true);
    manager.Queue(b, VansParticleControl::Seed, 0, 777);
    if (!check(manager.TickMainThread(0.11f),
        "Frozen particle manager did not dispatch an explicit command")) return false;
    manager.WaitForUpdateAndSwap();
    if (!check(second->GetRandomSeed() == 777
        && second->GetPlayTime() == editorFrozenTime,
        "Frozen particle manager advanced clock time or dropped an explicit command")) return false;
    for (int frame = 0; frame < 4; ++frame)
    {
        if (!check(!manager.TickMainThread(0.11f),
            "Frozen clean particle state still dispatched the worker")) return false;
        manager.WaitForUpdateAndSwap();
        if (!check(first->GetRenderBuffer().size() == editorFrozenFirst.size()
            && second->GetRenderBuffer().size() == editorFrozenSecond.size()
            && first->GetRenderBuffer()[0].m_WorldPosition == editorFrozenFirst[0].m_WorldPosition
            && second->GetRenderBuffer()[0].m_WorldPosition == editorFrozenSecond[0].m_WorldPosition
            && manager.SimulationMilliseconds() == 0
            && manager.MainThreadOverlapMilliseconds() == 0
            && manager.WaitMilliseconds() == 0,
            "Frozen clean particle state changed its snapshot or diagnostics")) return false;
    }
    manager.SetSimulationFrozen(false);
    if (!check(manager.TickMainThread(0.11f),
        "Unfreezing did not resume particle worker scheduling")) return false;
    manager.WaitForUpdateAndSwap();
    if (!check(second->GetRenderBuffer().size() > editorFrozenSecond.size(),
        "Unfreezing did not resume particle simulation")) return false;
    manager.Queue(a, VansParticleControl::Seed, 0, 12345);
    if (!check(manager.Destroy(a), "Instance release failed")) return false;
    const auto replacement = manager.Create(asset);
    if (!check(!manager.QueueBatch({
            { replacement, VansParticleControl::Play, 0.0f, 0 },
            { replacement, VansParticleControl::Seek, 301.0f, 0 }
        }), "Invalid particle command batch was accepted")) return false;
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    if (!check(replacement.index == a.index && replacement.generation != a.generation
        && !manager.Resolve(a) && !manager.Queue(a, VansParticleControl::Play)
        && manager.Resolve(replacement)->GetRandomSeed() != 12345
        && !manager.Resolve(replacement)->IsPlaying(),
        "Stale handle or rejected command batch reached a replacement instance")) return false;
    if (!check(manager.QueueBatch({
            { b, VansParticleControl::Seek, 300.0f, 0 },
            { replacement, VansParticleControl::Seek, 300.0f, 0 }
        }), "Valid long particle seek batch was rejected")) return false;
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    if (!check(manager.ResimulationSteps() == VansParticleManager::MaxResimulationStepsPerTick
        && manager.PendingResimulations() == 2
        && manager.Resolve(b)->GetPlayTime() > 0.0f && manager.Resolve(b)->GetPlayTime() < 300.0f
        && manager.Resolve(replacement)->GetPlayTime() > 0.0f
        && manager.Resolve(replacement)->GetPlayTime() < 300.0f,
        "Long particle seeks bypassed or monopolized the worker resimulation budget")) return false;
    manager.QueueBatch({
        { b, VansParticleControl::Stop, 0.0f, 0 },
        { replacement, VansParticleControl::Stop, 0.0f, 0 }
    });
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    if (!check(manager.ResimulationSteps() == 0 && manager.PendingResimulations() == 0
        && manager.Resolve(b)->IsFinished() && manager.Resolve(replacement)->IsFinished(),
        "Stop did not cancel an in-progress particle resimulation")) return false;
    ParticleTestJson prewarmJson = json;
    prewarmJson["global"]["duration"] = 30.0f;
    prewarmJson["global"]["prewarm"] = true;
    prewarmJson["global"]["fixedStep"] = 1.0f/240.0f;
    auto prewarmAsset = std::make_shared<VansParticleAsset>();
    if (!check(DecodeParticleAsset(
        prewarmJson, {}, *prewarmAsset, error), error.c_str())) return false;
    const auto prewarm = manager.Create(prewarmAsset);
    manager.Queue(prewarm, VansParticleControl::Play);
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    if (!check(manager.ResimulationSteps() == VansParticleManager::MaxResimulationStepsPerTick
        && manager.PendingResimulations() == 1
        && manager.Resolve(prewarm)->GetPlayTime() < 30.0f,
        "Worst-case 30 second prewarm bypassed the worker resimulation budget")) return false;
    manager.Queue(prewarm, VansParticleControl::Stop);
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();

    ParticleTestJson deterministicJson = json;
    deterministicJson["emitters"][0]["update"] = ParticleTestJson::array({
        {{"module", "UpdateRotationOverLifetime"},
         {"angularVelocity", {{"mode", "RandomBetween"}, {"min", -90.0f}, {"max", 90.0f}}}}
    });
    auto deterministicAsset = std::make_shared<VansParticleAsset>();
    if (!check(DecodeParticleAsset(
        deterministicJson, {}, *deterministicAsset, error), error.c_str())) return false;
    const auto angularVelocityForSeed = [&](std::uint32_t seed)
    {
        VansParticleRuntime runtime;
        runtime.SetAsset(deterministicAsset);
        runtime.SetRandomSeed(seed);
        runtime.Burst(1);
        return runtime.GetEmitter(0)->ParticlePool().m_AngularVelocity[0];
    };
    const float angularA = angularVelocityForSeed(17);
    const float angularB = angularVelocityForSeed(17);
    const float angularC = angularVelocityForSeed(29);
    if (!check(angularA == angularB && angularA != angularC,
        "Random angular velocity ignored the runtime seed")) return false;

    VansParticleEmitter zeroCapacityEmitter;
    zeroCapacityEmitter.m_MaxParticles = 0;
    zeroCapacityEmitter.m_RendererConfig.m_Type = VansParticleRendererType::Ribbon;
    zeroCapacityEmitter.m_RendererConfig.m_Ribbon.rootMode = VansRibbonRootMode::FollowSource;
    VansParticleEmitterRuntime zeroCapacityRuntime(zeroCapacityEmitter);
    zeroCapacityRuntime.EmitBurst(1, glm::mat4(1.0f));
    if (!check(zeroCapacityRuntime.AliveCount() == 0
        && zeroCapacityRuntime.DroppedSpawns() == 1,
        "Zero-capacity FollowSource Ribbon underflowed its particle pool")) return false;

    ParticleTestJson disabledRibbonJson = json;
    disabledRibbonJson["emitters"][0]["enabled"] = false;
    disabledRibbonJson["emitters"][0]["renderer"]["type"] = "Ribbon";
    auto disabledRibbonAsset = std::make_shared<VansParticleAsset>();
    if (!check(DecodeParticleAsset(
        disabledRibbonJson, {}, *disabledRibbonAsset, error), error.c_str())) return false;
    VansParticleManager seekManager;
    const auto seekHandle = seekManager.Create(disabledRibbonAsset);
    seekManager.SetOwnerWorldTransform(seekHandle,glm::mat4(1.0f));
    glm::mat4 movedOwner(1.0f);
    movedOwner[3].x = 1.0f;
    seekManager.SetOwnerWorldTransform(seekHandle,movedOwner);
    seekManager.TickMainThread(0); seekManager.WaitForUpdateAndSwap();
    if (!check(seekManager.Resolve(seekHandle)->CanSeek(),
        "Disabled Ribbon incorrectly blocked deterministic seek")) return false;
    seekManager.Queue(seekHandle,VansParticleControl::EmitterEnabled,1.0f,0);
    seekManager.TickMainThread(0); seekManager.WaitForUpdateAndSwap();
    if (!check(!seekManager.Resolve(seekHandle)->CanSeek(),
        "Enabled moving Ribbon incorrectly allowed deterministic seek")) return false;
    seekManager.Queue(seekHandle,VansParticleControl::Stop);
    seekManager.TickMainThread(0); seekManager.WaitForUpdateAndSwap();
    if (!check(seekManager.Resolve(seekHandle)->CanSeek(),
        "Stopped Ribbon retained stale moving-source history")) return false;
    if (!check(!manager.Queue(b,VansParticleControl::SetOwnerWorldTransform),
        "Transform control without its typed payload was accepted")) return false;
    bool crossThreadResolveRejected = false;
    bool crossThreadMutationRejected = false;
    std::thread unauthorizedAccess([&] {
        crossThreadResolveRejected = manager.Resolve(b) == nullptr;
        crossThreadMutationRejected = !manager.SetOwnerWorldTransform(b,glm::mat4(1))
            && !manager.Queue(b,VansParticleControl::Pause);
    });
    unauthorizedAccess.join();
    if (!check(crossThreadResolveRejected && crossThreadMutationRejected,
        "Release build accepted particle access from a non-owner thread")) return false;
    manager.TickMainThread(0.01f);
    if (!check(!manager.Resolve(b)
        && !manager.SetOwnerWorldTransform(b,glm::mat4(1))
        && !manager.Queue(b,VansParticleControl::Pause),
        "Release build access was not rejected while particle simulation was in flight")) return false;
    const auto overlapDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    while (std::chrono::steady_clock::now() < overlapDeadline) std::this_thread::yield();
    manager.WaitForUpdateAndSwap();
    if (!check(manager.MainThreadOverlapMilliseconds() >= 1.0
        && manager.WaitMilliseconds() < manager.MainThreadOverlapMilliseconds(),
        "Particle main-thread overlap window or residual wait was not measured")) return false;
    manager.Shutdown();
    if (!check(manager.ActiveCount() == 0 && !manager.Resolve(b),
        "Shutdown did not join simulation and invalidate all instances")) return false;
    std::cout << "[ParticleCore] immutableDefinition=1 isolatedInstances=1 renderSortBoundary=1 pausedSnapshot=1 frozenDispatch=1 staleHandle=1 atomicBatch=1 boundedSeek=1 boundedPrewarm=1 overlapTiming=1 commandPayloadGuard=1 ownerThreadGuard=1 releaseInFlightGuard=1 deterministicRotation=1 zeroCapacityGuard=1 seekReset=1 shutdown=1\n";
    return TestRibbonSimulation() && TestPolylineGeometry() && TestVFXServiceLifetime() && TestParticleAuthoring() && TestParticleDebugSnapshot();
}
