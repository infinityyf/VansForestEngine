#include "TerrainInteractionContractTests.h"
#include "../EngineCore/TerrainCore/VansTerrainInteraction.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include <filesystem>
#include <iostream>
#include <limits>

bool RunTerrainInteractionContractTests()
{
    using namespace Vans;
    const auto check=[](bool value,const char* message)
    { if (!value) std::cerr<<"Terrain interaction: "<<message<<'\n';return value; };
    VansTerrainInteraction input;std::string error;
    if (!check(!input.enabled && input.albedo.empty() && input.normal.empty() && input.height.empty() &&
        ValidateTerrainInteraction(input,error),"neutral defaults")) return false;
    input.enabled=true;input.window.cameraCentered=true;
    input.window.worldRect={-10,-5,20,10};input.window.resolution={2048,1024};
    glm::vec4 first,subpixel,negative;std::array<std::int64_t,2> origin;
    if (!check(ResolveWorldTextureWindow(input.window,{0,9,0},first,origin) &&
        ResolveWorldTextureWindow(input.window,{0.001f,-9,0.001f},subpixel,origin) && first==subpixel &&
        ResolveWorldTextureWindow(input.window,{-0.001f,0,-0.001f},negative,origin) &&
        origin[0]==-1025 && origin[1]==-513 && negative.x==first.x-20.0f/2048,
        "camera translation must be integer texels and ignore Y")) return false;
    const auto preserved=negative;
    if (!check(!ResolveWorldTextureWindow(input.window,{std::numeric_limits<float>::infinity(),0,0},negative,origin) &&
        negative==preserved,"failed window resolution must be atomic")) return false;
    VansGraphics::VansScene scene;
    input.albedo="external:custom-color";input.height="external:signed-height";
    if (!check(scene.SetTerrainInteraction(input,error),"generic external producer must be accepted")) return false;
    auto invalid=input;invalid.window.worldRect.z=0;
    if (!check(!scene.SetTerrainInteraction(invalid,error) && scene.GetTerrainInteraction().albedo==input.albedo,"invalid range must preserve binding")) return false;
    invalid=input;invalid.heightWeights.x=std::numeric_limits<float>::quiet_NaN();
    if (!check(!scene.SetTerrainInteraction(invalid,error),"invalid height decoding must fail")) return false;
    invalid=input;invalid.normalStrength=1.1f;
    if (!check(!scene.SetTerrainInteraction(invalid,error),"invalid normal strength must fail")) return false;
    VansScriptContext scripts;
    scripts.SetActiveProjectRoot(std::filesystem::current_path().string());scripts.SetScene(&scene);scripts.VansScriptSetup();
    const char* lua=R"lua(
        local t=vans.terrain
        assert(type(t)=='table' and t.setInteraction==nil)
        assert(t.set_interaction{enabled=true})
        local function rejected(...) local ok,err=...;assert(not ok and #err>0) end
        rejected(t.set_interaction{enabled=1})
        rejected(t.set_interaction{unknown=true})
        rejected(t.set_interaction{world_rect={0,0,0,10}})
        rejected(t.set_interaction{resolution={512.5,512}})
        rejected(t.set_interaction{height_weights={0,math.huge,0,0}})
        assert(t.set_interaction{enabled=true,world_rect={1,2,20,10},resolution={2048,1024},
            camera_centered=true,albedo='external:test-color',normal='external:test-normal',height='external:test-height',
            height_weights={-1,1,0,0},max_displacement_meters=0.12})
        assert(t.set_interaction_enabled(false))
        rejected(t.set_interaction_enabled(1))
    )lua";
    const auto status=luaL_dostring(scripts.GetLuaState(),lua);
    if (status!=LUA_OK) std::cerr<<lua_tostring(scripts.GetLuaState(),-1)<<'\n';
    if (!check(status==LUA_OK,"real Lua binding")) return false;
    const auto& published=scene.GetTerrainInteraction();
    if (!check(!published.enabled && published.normal=="external:test-normal" && published.heightWeights.y==1 &&
        published.window.resolution==glm::uvec2(2048,1024),"toggle must retain inputs")) return false;
    auto* foreign=luaL_newstate();
    lua_getglobal(scripts.GetLuaState(),"vans");lua_getfield(scripts.GetLuaState(),-1,"terrain");lua_getfield(scripts.GetLuaState(),-1,"set_interaction_enabled");
    lua_pushcfunction(foreign,lua_tocfunction(scripts.GetLuaState(),-1));lua_pop(scripts.GetLuaState(),3);
    lua_pushboolean(foreign,true);
    const bool isolated=lua_pcall(foreign,1,2,0)==LUA_OK && !lua_toboolean(foreign,-2);lua_close(foreign);
    if (!check(isolated && !scene.GetTerrainInteraction().enabled,"foreign Lua state isolation")) return false;
    scripts.SetScene(nullptr);
    std::cout<<"[TerrainInteraction] neutral/window/atomic validation/Lua/toggle/isolation PASS\n";
    return true;
}
