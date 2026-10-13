#include "ProjectRenderPassContractTests.h"
#include "../EngineCore/AssetCore/VansRenderPassAsset.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AssetCore/VansAssetObjectRepository.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/ProjectSystem/Serialization/VansProjectSettingsJsonCodec.h"
#include "../EngineCore/ProjectSystem/VansProjectSettingsData.h"
#include "../EngineCore/RenderCore/VansProjectRenderPassState.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VulkanCore/VansPipelineDescriptor.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include <cstring>
#include <limits>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
using namespace Vans;
using Json = nlohmann::json;
bool Check(bool ok, const char* message)
{
    if (!ok) std::cerr << "Project RenderPass: " << message << '\n';
    return ok;
}
VansRenderPassAsset Fixture()
{
    VansRenderPassAsset asset;
    asset.passId = "project-mask";
    asset.source = VansRenderPassSource::ProjectInstances;
    asset.instanceStream = "marks";
    asset.instanceMesh = VansAssetGuid::FromStableName("ProjectPassTest", "mesh");
    asset.shaders = {VansAssetGuid::FromStableName("ProjectPassTest", "shader")};
    asset.previousFrameHistory = true;
    VansRenderTargetAsset target;
    target.name = "mask";
    target.exportChannel = "project.mask";
    target.exportSlot = 0;
    target.width = 64; target.height = 64;
    target.lifetime = VansRenderTargetLifetime::Persistent;
    target.load = VansRenderTargetLoad::CopyPrevious;
    target.usage = RenderTargetColorAttachment | RenderTargetSampled | RenderTargetTransferSource | RenderTargetTransferDestination;
    asset.targets = {target};
    asset.buffers = {{"marks", "instanceInput", "projectDrawInstance", 1, 96, 32}};
    asset.parameters = {{"projection", 0, {1, 2, 3, 4}}};
    return asset;
}
bool CheckCameraWindowContract()
{
    using namespace VansGraphics;
    auto asset = Fixture();
    asset.cameraWorldRect = "projection";
    asset.parameters[0].initialValue = {-32,-8,64,16};
    asset.targets[0].height = 32;
    VansRenderPassAsset decoded; std::string error;
    if (!Check(ReadRenderPassAsset(WriteRenderPassAsset(asset),decoded,error) && decoded.cameraWorldRect == "projection",
        "camera window asset must roundtrip")) return false;
    const auto reject = [&](VansRenderPassAsset invalid) {
        return Check(!ReadRenderPassAsset(WriteRenderPassAsset(invalid),decoded,error) && !error.empty(),"invalid camera window must fail");
    };
    auto invalid=asset; invalid.cameraWorldRect="missing"; if(!reject(invalid))return false;
    invalid=asset; invalid.parameters[0].initialValue[2]=0; if(!reject(invalid))return false;
    invalid=asset; invalid.parameters[0].initialValue.pop_back(); if(!reject(invalid))return false;
    invalid=asset; invalid.targets[0].followsRenderResolution=true; if(!reject(invalid))return false;
    invalid=asset; invalid.targets[0].load=VansRenderTargetLoad::Load; if(!reject(invalid))return false;
    VansSceneResourceBuildPlan resources;
    resources.projectRenderPasses.push_back({VansAssetGuid::FromStableName("ProjectPassTest","window"),asset});
    VansSceneShaderResourceRequest shader; shader.name="window-test"; shader.assetGuid=asset.shaders[0].ToString(); shader.pushConstantSize=0; resources.shaders.push_back(shader);
    VansSceneMeshResourceRequest mesh; mesh.name="window-mesh"; mesh.assetGuid=asset.instanceMesh.ToString(); resources.meshes.push_back(mesh);
    VansProjectRenderPassState state;
    if(!Check(state.Configure(resources,error),error.c_str()))return false;
    if(!Check(!state.SetParameter(asset.passId,"projection",{0,0,64,16},error),"script must not overwrite camera-owned window"))return false;
    Vans::VansWorldTextureWindow outputWindow;
    if(!Check(state.GetOutputWindow(asset.passId,asset.targets[0].name,outputWindow) &&
        outputWindow.cameraCentered && outputWindow.worldRect==glm::vec4(-32,-8,64,16) &&
        outputWindow.resolution==glm::uvec2(asset.targets[0].width,32),
        "consumer initialization must use the producer's declared range and physical resolution"))return false;
    const auto first=state.CaptureFrame({0,20,0});
    const auto subpixel=state.CaptureFrame({.49f,-20,.24f});
    const auto negative=state.CaptureFrame({-.01f,0,-.01f});
    std::array<float,4> rectangle{}; std::memcpy(rectangle.data(),negative.passes[0].parameters.data(),sizeof(rectangle));
    if(!Check(first.passes[0].worldTexelOrigin==std::array<std::int64_t,2>{-32,-16} &&
        first.passes[0].parameters==subpixel.passes[0].parameters &&
        negative.passes[0].worldTexelOrigin==std::array<std::int64_t,2>{-33,-17} &&
        rectangle==std::array<float,4>{-33,-8.5f,64,16},"snap must use floor, independent X/Z texel size and ignore camera height"))return false;
    const auto bad=state.CaptureFrame({std::numeric_limits<float>::infinity(),0,0});
    return Check(!bad.passes[0].worldWindowValid && first.passes[0].worldWindowValid,
        "invalid camera must be rejected without changing prior snapshots");
}
bool CheckFrameInputContract()
{
    using namespace VansGraphics;
    auto asset = Fixture();
    asset.buffers.push_back({"auxiliary", "uniformInput", "vec4", 2, 16, 2});
    VansSceneResourceBuildPlan resources;
    resources.projectRenderPasses.push_back({VansAssetGuid::FromStableName("ProjectPassTest", "state"), asset});
    VansSceneShaderResourceRequest shader;
    shader.name="test-program"; shader.assetGuid=asset.shaders.front().ToString(); shader.pushConstantSize=0;
    resources.shaders.push_back(shader);
    VansSceneMeshResourceRequest mesh; mesh.name="test-mesh"; mesh.assetGuid=asset.instanceMesh.ToString();
    resources.meshes.push_back(mesh);
    VansProjectRenderPassState state;
    std::string error;
    if (!Check(state.Configure(resources,error),error.c_str())) return false;
    const auto published=state.Configuration();
    auto invalid=resources; invalid.projectRenderPasses.push_back(invalid.projectRenderPasses.front());
    if (!Check(!state.Configure(invalid,error)&&!error.empty()&&state.Configuration()==published,
        "failed configuration must preserve published configuration")) return false;
    invalid=resources; invalid.shaders.push_back(invalid.shaders.front());
    if (!Check(!state.Configure(invalid,error)&&state.Configuration()==published,
        "ambiguous shader runtime identity must not publish")) return false;
    if (!Check(state.SetParameter(asset.passId,"projection",{9,8,7,6},error)&&
        !state.SetParameter(asset.passId,"projection",{0},error)&&
        !state.SetParameter(asset.passId,"projection",{0,0,0,std::numeric_limits<float>::infinity()},error),
        "parameter shape and nonfinite update must be rejected atomically")) return false;
    std::vector<std::uint8_t> auxiliary(16,71);
    if (!Check(state.SetBuffer(asset.passId,"auxiliary",auxiliary,error)&&
        !state.SetBuffer(asset.passId,"auxiliary",std::vector<std::uint8_t>(17),error)&&
        !state.SetBuffer(asset.passId,"auxiliary",std::vector<std::uint8_t>(48),error)&&
        !state.SetBuffer(asset.passId,"marks",std::vector<std::uint8_t>(96),error),
        "input buffers must enforce role, alignment and capacity")) return false;
    std::vector<VansProjectDrawInstance> instances(31);
    instances.front().data.x=17;
    if (!Check(state.SubmitInstances(asset.passId,instances,error)&&
        !state.SubmitInstances(asset.passId,std::vector<VansProjectDrawInstance>(2),error)&&
        state.SubmitInstances(asset.passId,std::vector<VansProjectDrawInstance>(1),error),
        "instance overflow must reject the whole append and retain prior records")) return false;
    const auto frame=state.CaptureFrame();
    if (!Check(frame.passes.size()==1&&frame.passes[0].instanceCount==32&&frame.passes[0].buffers[0].size()==32*96&&
        frame.passes[0].buffers[1]==auxiliary,"captured frame must retain complete bounded inputs")) return false;
    std::array<float,4> parameters{};
    std::memcpy(parameters.data(),frame.passes[0].parameters.data(),sizeof(parameters));
    if (!Check(parameters==std::array<float,4>{9,8,7,6},"rejected updates must not replace accepted parameters")) return false;
    auxiliary.front()=0; instances.front().data.x=0;
    if (!Check(state.SetParameter(asset.passId,"projection",{1,1,1,1},error)&&state.SetEnabled(asset.passId,false,error),error.c_str())) return false;
    const auto next=state.CaptureFrame();
    if (!Check(frame.passes[0].enabled&&frame.passes[0].buffers[1][0]==71&&next.passes[0].instanceCount==0&&
        next.passes[0].buffers[0].empty()&&next.passes[0].buffers[1].empty()&&!next.passes[0].enabled,
        "snapshot must be isolated; per-frame data must expire while persistent settings remain")) return false;
    auto nonfinite=VansProjectDrawInstance{}; nonfinite.model[1][2]=std::numeric_limits<float>::quiet_NaN();
    if (!Check(!state.SubmitInstances(asset.passId,{nonfinite},error),"nonfinite instance matrix must fail")) return false;
    if (!Check(state.SubmitInstances(asset.passId,{VansProjectDrawInstance{}},error)&&
        state.SetBuffer(asset.passId,"auxiliary",auxiliary,error),error.c_str())) return false;
    state.ResetForScene();
    const auto reset=state.CaptureFrame();
    std::memcpy(parameters.data(),reset.passes[0].parameters.data(),sizeof(parameters));
    if (!Check(state.Configuration()==published&&reset.passes[0].enabled&&
        reset.passes[0].instanceCount==0&&reset.passes[0].buffers[0].empty()&&reset.passes[0].buffers[1].empty()&&
        parameters==std::array<float,4>{1,2,3,4},
        "scene reload must retain project configuration and reset pending inputs and authored defaults")) return false;
    std::string key;
    if (!Check(state.GetOutputKey(asset.passId,"mask",key)&&key=="project:"+resources.projectRenderPasses[0].guid.ToString()+"/mask"&&
        !state.GetOutputKey(asset.passId,"unknown",key),"output key must use asset identity and declared target")) return false;
    auto earlier=asset; earlier.passId="earlier"; earlier.order=-10; earlier.targets[0].exportChannel="earlier.mask"; earlier.targets[0].exportSlot=1;
    resources.projectRenderPasses.push_back({VansAssetGuid::FromStableName("ProjectPassTest","earlier"),earlier});
    if (!Check(state.Configure(resources,error)&&state.Configuration()->passes.front().definition.passId=="earlier",
        "pass order must come from configuration")) return false;
    state.Clear();
    return Check(!state.Configuration()&&state.CaptureFrame().passes.empty(),"unload must remove configuration and pending inputs");
}
bool CheckLuaInputContract()
{
    using namespace VansGraphics;
    VANS_INIT_MAIN_THREAD();
    auto asset = Fixture();
    asset.buffers.push_back({"auxiliary", "uniformInput", "vec4", 2, 16, 2});
    VansSceneResourceBuildPlan resources;
    const auto guid = VansAssetGuid::FromStableName("ProjectPassTest", "lua");
    resources.projectRenderPasses.push_back({guid, asset});
    VansSceneShaderResourceRequest shader;
    shader.name = "lua-program"; shader.assetGuid = asset.shaders.front().ToString(); shader.pushConstantSize = 0;
    resources.shaders.push_back(shader);
    VansSceneMeshResourceRequest mesh; mesh.name = "lua-mesh"; mesh.assetGuid = asset.instanceMesh.ToString();
    resources.meshes.push_back(mesh);
    VansScene scene;
    std::string error;
    if (!Check(scene.ProjectRenderPasses().Configure(resources, error), error.c_str())) return false;
    VansScriptContext scripts;
    scripts.SetActiveProjectRoot(std::filesystem::current_path().string());
    scripts.SetScene(&scene);
    scripts.VansScriptSetup();
    const auto run = [&](const char* code) {
        if (luaL_dostring(scripts.GetLuaState(), code) == LUA_OK) return true;
        std::cerr << "Project RenderPass Lua: " << lua_tostring(scripts.GetLuaState(), -1) << '\n';
        lua_pop(scripts.GetLuaState(), 1);
        return false;
    };
    if (!run(R"lua(
        local p = vans.project_render_pass
        assert(type(p) == 'table' and p.setEnabled == nil)
        assert(p.get_enabled('project-mask') == true and p.get_enabled('missing') == nil)
        local function rejected(...) local ok, err = ...; assert(ok == false and type(err) == 'string' and #err > 0) end
        assert(p.set_enabled('project-mask', false)); assert(p.get_enabled('project-mask') == false)
        rejected(p.set_enabled('project-mask', 1)); rejected(p.set_enabled('missing', true))
        assert(p.set_enabled('project-mask', true))
        assert(p.set_parameter('project-mask', 'projection', {9,8,7,6}))
        rejected(p.set_parameter('project-mask', 'projection', {1}))
        rejected(p.set_parameter('project-mask', 'projection', {0,0,0,math.huge}))
        rejected(p.set_parameter('project-mask', 'projection', {'1',2,3,4}))
        rejected(p.set_parameter('project-mask\0extra', 'projection', {1,2,3,4}))
        assert(p.set_buffer('project-mask', 'auxiliary', string.rep(string.char(71),16)))
        rejected(p.set_buffer('project-mask', 'auxiliary', string.rep('x',17)))
        rejected(p.set_buffer('project-mask', 'auxiliary', string.rep('x',48)))
        rejected(p.set_buffer('project-mask', 'marks', string.rep('x',96)))
        local instance = {model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}, color={1,0,0,1}, data={17,0,0,0}}
        local list = {}; for i=1,32 do list[i]=instance end
        assert(p.submit_instances('project-mask',list))
        rejected(p.submit_instances('project-mask',{instance}))
        instance.data[1] = math.huge
        rejected(p.submit_instances('project-mask',{instance}))
        assert(type(p.get_output_resource('project-mask','mask')) == 'string')
        assert(p.get_output_resource('project-mask','missing') == nil)
    )lua")) return false;
    const auto frame = scene.ProjectRenderPasses().CaptureFrame();
    std::array<float, 4> parameters{};
    std::memcpy(parameters.data(), frame.passes[0].parameters.data(), sizeof(parameters));
    VansProjectDrawInstance first;
    std::memcpy(&first, frame.passes[0].buffers[0].data(), sizeof(first));
    if (!Check(parameters == std::array<float,4>{9,8,7,6} && frame.passes[0].instanceCount == 32 &&
        frame.passes[0].buffers[1] == std::vector<std::uint8_t>(16,71) && first.data.x == 17,
        "real Lua bridge must publish bounded, copied values to the frame snapshot")) return false;
    const auto next = scene.ProjectRenderPasses().CaptureFrame();
    if (!Check(next.passes[0].instanceCount == 0 && next.passes[0].buffers[1].empty(),
        "Lua per-frame inputs must expire after capture")) return false;
    // 另一个 Lua state 不能借用活动项目的配置或帧输入。
    auto* foreign = luaL_newstate();
    lua_getglobal(scripts.GetLuaState(), "vans");
    lua_getfield(scripts.GetLuaState(), -1, "project_render_pass");
    lua_getfield(scripts.GetLuaState(), -1, "set_enabled");
    const auto function = lua_tocfunction(scripts.GetLuaState(), -1);
    lua_pop(scripts.GetLuaState(), 3);
    lua_pushcfunction(foreign, function); lua_pushliteral(foreign, "project-mask"); lua_pushboolean(foreign, false);
    const bool isolated = lua_pcall(foreign, 2, 2, 0) == LUA_OK && !lua_toboolean(foreign, -2);
    lua_close(foreign);
    if (!Check(isolated && scene.ProjectRenderPasses().CaptureFrame().passes[0].enabled,
        "foreign Lua state must not mutate the active project")) return false;
    scripts.SetScene(nullptr);
    return run(R"lua(
        local p=vans.project_render_pass
        local ok,err=p.set_enabled('project-mask',false)
        assert(ok==false and #err>0 and p.get_enabled('project-mask')==nil)
        assert(p.get_output_resource('project-mask','mask')==nil)
    )lua");
}
bool CheckSceneLayerContract()
{
    VansSceneData scene;
    scene.sceneGuid=VansAssetGuid::New();scene.settings=VansSceneSchema::MakeDefaultSettings();
    VansSceneEntityData entity;entity.id=VansAssetGuid::New();entity.name="Layer contract";
    entity.components={VansSceneSchema::MakeTransform(),VansSceneSchema::MakeModelRenderer(VansAssetGuid::New())};
    scene.entities.push_back(entity);
    auto entities=VansSceneSchema::SerializeSceneJson(scene)["entities"];
    const auto valid=[&](const Json& value) { return VansSceneSchema::ValidateEntityComponents(DecodeSerializedValueJson(value)).empty(); };
    if (!Check(valid(entities),"default render layer must satisfy scene schema")) return false;
    entities[0]["components"][1]["data"].erase("renderLayer");
    if (!Check(valid(entities),"existing scenes without renderLayer must remain valid")) return false;
    for (const Json layer : {Json(7),Json(""),Json(std::string(257,'x')),Json(std::string("a\0b",3))})
    {
        auto invalid=entities;invalid[0]["components"][1]["data"]["renderLayer"]=layer;
        if (!Check(!valid(invalid),"malformed scene renderLayer must fail before projection")) return false;
    }
    entities[0]["components"][1]["data"]["renderLayer"]="Selected";
    return Check(valid(entities),"configured scene renderLayer must be accepted");
}
}

bool CheckSampledTextureInputContract()
{
    auto asset=Fixture();
    const auto texture=VansAssetGuid::FromStableName("ProjectPassTest","texture");
    asset.textures={{"tread",texture,2}};
    VansRenderPassAsset decoded;std::string error;
    if(!Check(ReadRenderPassAsset(WriteRenderPassAsset(asset),decoded,error) && decoded.textures.size()==1 &&
        decoded.textures[0].asset==texture && decoded.textures[0].binding==2,"sampled Texture must roundtrip")) return false;
    const auto dependencies=CollectRenderPassAssetDependencies(decoded);
    if(!Check(dependencies.size()==3 && std::find(dependencies.begin(),dependencies.end(),texture)!=dependencies.end(),
        "sampled Texture must join authoring and packaged dependency closure")) return false;
    const auto reject=[&](VansRenderPassAsset invalid){return Check(!ReadRenderPassAsset(WriteRenderPassAsset(invalid),decoded,error),"invalid sampled Texture must fail");};
    auto invalid=asset;invalid.textures[0].binding=0;if(!reject(invalid))return false;
    invalid=asset;invalid.textures[0].binding=1;if(!reject(invalid))return false;
    invalid=asset;invalid.textures[0].asset={};if(!reject(invalid))return false;
    invalid=asset;invalid.textures.push_back(invalid.textures.front());if(!reject(invalid))return false;
    using namespace VansGraphics;
    VansSceneResourceBuildPlan resources;
    const auto passGuid=VansAssetGuid::FromStableName("ProjectPassTest","textures-pass");
    resources.projectRenderPasses={{passGuid,asset}};
    VansSceneMeshResourceRequest mesh;mesh.name="mesh";mesh.assetGuid=asset.instanceMesh.ToString();resources.meshes={mesh};
    VansSceneShaderResourceRequest shader;shader.name="shader";shader.assetGuid=asset.shaders[0].ToString();shader.kind="graphics";shader.pushConstantSize=0;resources.shaders={shader};
    VansProjectRenderPassState state;
    if(!Check(!state.Configure(resources,error),"unresolved Texture must prevent Pass publication"))return false;
    VansSceneTextureResourceRequest request;request.name="tread";request.assetGuid=texture.ToString();resources.textures={request};
    if(!Check(state.Configure(resources,error) && state.Configuration()->passes[0].textureNames==std::vector<std::string>{"tread"},error.c_str()))return false;
    resources.textures[0].textureType=2;
    if(!Check(!state.Configure(resources,error) && state.Configuration()->passes[0].textureNames[0]=="tread","non-2D Texture must retain published configuration"))return false;
    return true;
}

bool RunProjectRenderPassAssetContractTests()
{
    using namespace VansGraphics;
    VansPipelineProgramDesc blendProgram;
    blendProgram.graphicsState.colorAttachmentCount=3;
    blendProgram.graphicsState.colorAttachmentBlends={"alpha","alpha","max"};
    std::string blendError;
    if (!Check(ValidateGraphicsAttachmentBlends(blendProgram.graphicsState,blendError),"valid MRT blend state")) return false;
    const auto maxKey=VansPipelineDescriptorBuilder::BuildPipelineKey(blendProgram,{});
    blendProgram.graphicsState.colorAttachmentBlends.back()="min";
    if (!Check(maxKey.hash!=VansPipelineDescriptorBuilder::BuildPipelineKey(blendProgram,{}).hash,"blend operations must identify separate Pipeline variants")) return false;
    for(const auto& invalid:std::vector<std::vector<std::string>>{{"alpha"},{"alpha","alpha","unknown"}})
    { blendProgram.graphicsState.colorAttachmentBlends=invalid;
      if (!Check(!ValidateGraphicsAttachmentBlends(blendProgram.graphicsState,blendError),"invalid MRT blend layout must fail")) return false; }
    if (!CheckCameraWindowContract() || !CheckFrameInputContract() || !CheckLuaInputContract() || !CheckSceneLayerContract() || !CheckSampledTextureInputContract()) return false;
    const auto fixture = Fixture();
    const auto root = WriteRenderPassAsset(fixture);
    const auto json = EncodeSerializedValueJson<Json>(root);
    VansRenderPassAsset decoded;
    std::string error;
    if (!Check(ReadRenderPassAsset(root, decoded, error), error.c_str()) ||
        !Check(EncodeSerializedValueJson<Json>(WriteRenderPassAsset(decoded)) == json, "canonical asset roundtrip lost declarations")) return false;
    const auto dependencies = CollectRenderPassAssetDependencies(decoded);
    if (!Check(dependencies.size() == 2, "mesh and shader dependencies must be retained")) return false;
    const auto reject = [&](Json invalid) {
        VansRenderPassAsset output = fixture;
        std::string diagnostic;
        return Check(!ReadRenderPassAsset(DecodeSerializedValueJson(invalid), output, diagnostic) && !diagnostic.empty() &&
            EncodeSerializedValueJson<Json>(WriteRenderPassAsset(output)) == json, "invalid candidate must fail with diagnostic and preserve published asset");
    };
    auto invalid = json; invalid["anchor"] = "BeforeGBuffer"; if (!reject(invalid)) return false;
    invalid = json; invalid["executor"] = "Compute"; if (!reject(invalid)) return false;
    invalid = json; invalid["targets"]["color"][0]["lifetime"] = "frameLocal"; if (!reject(invalid)) return false;
    invalid = json; invalid["targets"]["color"][0]["usage"] = {"colorAttachment", "sampled"}; if (!reject(invalid)) return false;
    invalid = json; invalid["history"] = "none"; if (!reject(invalid)) return false;
    invalid = json; invalid["shaders"].push_back(invalid["shaders"][0]); if (!reject(invalid)) return false;
    invalid = json; invalid["shaders"][0] = fixture.shaders[0].ToString(); if (!reject(invalid)) return false;
    invalid = json; invalid["buffers"][0]["maxElements"] = 0; if (!reject(invalid)) return false;
    invalid = json; invalid["buffers"][0]["binding"] = 0; if (!reject(invalid)) return false;
    invalid = json; invalid["parameters"]["overlap"] = {{"offset",0},{"value",{1}}}; if (!reject(invalid)) return false;
    invalid = json; invalid.erase("source"); if (!reject(invalid)) return false;
    invalid = json; invalid["targets"]["color"][0].erase("size"); if (!reject(invalid)) return false;
    invalid = json; invalid["unexpected"] = true; if (!reject(invalid)) return false;
    invalid = json; invalid["targets"]["color"][0].erase("exportSlot"); if (!reject(invalid)) return false;
    invalid = json; invalid["targets"]["color"][0]["exportSlot"]=16; if (!reject(invalid)) return false;
    invalid = json; invalid["targets"]["color"][0].erase("exportChannel"); if (!reject(invalid)) return false;
    invalid = json; invalid["filter"]["layers"]={std::string(257,'x')}; if (!reject(invalid)) return false;

    VansAssetRecord record;
    record.guid = VansAssetGuid::FromStableName("ProjectPassTest", "pass");
    record.type = VansAssetType::RenderPass;
    VansAssetObjectRepository repository;
    if (!Check(VansAssetDatabase::Classify("mask.vrenderpass") == VansAssetType::RenderPass,
        "asset extension must resolve to RenderPass") ||
        !Check(VansAssetObjectBootstrapper::PublishSerialized(record, root, 10, repository, error), error.c_str())) return false;
    VansAssetObjectSnapshotInfo info;
    if (!Check(repository.FindInfo(record.guid, info) && info.dependencies == dependencies,
        "published pass must own exactly the shader and mesh dependency closure")) return false;
    if (!Check(!VansAssetObjectBootstrapper::PublishSerialized(record, DecodeSerializedValueJson(invalid), 11, repository, error) &&
        repository.ResolveLatest<VansRenderPassAsset>(record.guid)->passId == fixture.passId, "failed reload must retain old generation")) return false;

    VansProjectRenderSettingsData settings;
    settings.projectRenderPasses = {record.guid};
    const auto settingsJson = VansProjectSettingsJsonCodec::EncodeRenderSettings(settings);
    VansProjectRenderSettingsData settingsRoundtrip;
    std::vector<std::string> warnings;
    if (!Check(VansProjectSettingsJsonCodec::DecodeRenderSettings(settingsJson, settingsRoundtrip, warnings, error) &&
        settingsRoundtrip.projectRenderPasses == settings.projectRenderPasses, "project pass references must roundtrip")) return false;
    auto duplicated = settingsJson; duplicated["projectRenderPasses"].push_back(duplicated["projectRenderPasses"][0]);
    if (!Check(!VansProjectSettingsJsonCodec::DecodeRenderSettings(duplicated, settingsRoundtrip, warnings, error), "duplicate project pass roots must fail")) return false;
    auto empty = settingsJson; empty.erase("projectRenderPasses");
    if (!Check(VansProjectSettingsJsonCodec::DecodeRenderSettings(empty, settingsRoundtrip, warnings, error) &&
        settingsRoundtrip.projectRenderPasses.empty(), "existing projects must default to empty project pass list")) return false;

    const auto temp = std::filesystem::temp_directory_path() / ("ForestProjectPassContract_" + VansAssetGuid::New().ToString());
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{temp};
    VansPackagedResourcePlan plan, restored;
    plan.resourcePlan.projectRenderPasses.push_back({record.guid, fixture});
    VansSceneShaderResourceRequest shaderRequest;shaderRequest.name="MRT";shaderRequest.colorAttachmentCount=3;
    shaderRequest.colorAttachmentBlends={"alpha","alpha","max"};plan.resourcePlan.shaders.push_back(shaderRequest);
    if (!Check(VansPackagedResourcePlanIO::Save(temp, plan, temp.parent_path(), error), error.c_str()) ||
        !Check(VansPackagedResourcePlanIO::Load(temp, temp.parent_path(), restored, error), error.c_str()) ||
        !Check(restored.resourcePlan.projectRenderPasses.size() == 1 && restored.resourcePlan.projectRenderPasses[0].guid == record.guid &&
            EncodeSerializedValueJson<Json>(WriteRenderPassAsset(restored.resourcePlan.projectRenderPasses[0].definition)) == json,
            "packaged runtime definition must survive without authoring filesystem access")) return false;
    if (!Check(restored.resourcePlan.shaders.size()==1 && restored.resourcePlan.shaders[0].colorAttachmentBlends==shaderRequest.colorAttachmentBlends,
        "packaged Shader must retain independent MRT blending")) return false;
    return true;
}
