#include "VansLuaProjectRenderPassBridge.h"
#include "VansScriptContext.h"
#include "../RenderCore/VansScene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
extern "C"
{
#include <lauxlib.h>
}

namespace VansRuntime
{
namespace
{
VansGraphics::VansProjectRenderPassState* Passes(lua_State* state)
{
    auto* context = VansScriptContext::GetInstance();
    if (!context || context->GetLuaState() != state || !context->GetScene()) return nullptr;
    return &context->GetScene()->ProjectRenderPasses();
}
int Result(lua_State* state, bool accepted, const std::string& error)
{
    lua_pushboolean(state, accepted);
    lua_pushlstring(state, error.data(), error.size());
    return 2;
}
bool Name(lua_State* state, int index, std::string& value)
{
    if (lua_type(state,index) != LUA_TSTRING) return false;
    std::size_t size = 0; const auto* text = lua_tolstring(state,index,&size);
    if (!size || size > 256 || std::memchr(text,0,size)) return false;
    value.assign(text,size); return true;
}
bool Floats(lua_State* state, int index, std::size_t count, float* result)
{
    if (!lua_istable(state,index) || lua_rawlen(state,index) != count) return false;
    index = lua_absindex(state,index);
    for (std::size_t i = 0; i < count; ++i)
    {
        lua_rawgeti(state,index,static_cast<lua_Integer>(i+1));
        const auto value = lua_tonumber(state,-1);
        const bool valid = lua_type(state,-1) == LUA_TNUMBER && std::isfinite(value) &&
            std::abs(value) <= std::numeric_limits<float>::max();
        lua_pop(state,1);
        if (!valid) return false;
        result[i] = static_cast<float>(value);
    }
    return true;
}
int SetEnabled(lua_State* state)
{
    auto* passes = Passes(state); std::string id,error;
    if (!passes || !Name(state,1,id) || !lua_isboolean(state,2)) return Result(state,false,"Expected active project Pass, passId and boolean");
    const bool ok = passes->SetEnabled(id,lua_toboolean(state,2) != 0,error);
    return Result(state,ok,error);
}
int GetEnabled(lua_State* state)
{
    auto* passes = Passes(state); std::string id; bool enabled = false;
    if (!passes || !Name(state,1,id) || !passes->TryGetEnabled(id,enabled)) { lua_pushnil(state); return 1; }
    lua_pushboolean(state,enabled); return 1;
}
int SetParameter(lua_State* state)
{
    auto* passes = Passes(state); std::string id,name,error;
    if (!passes || !Name(state,1,id) || !Name(state,2,name) || !lua_istable(state,3)) return Result(state,false,"Expected passId, parameter name and float array");
    const auto count = lua_rawlen(state,3);
    if (!count || count > 16384) return Result(state,false,"Parameter array exceeds the declared UBO limit");
    std::vector<float> values(count);
    if (!Floats(state,3,count,values.data())) return Result(state,false,"Parameter requires finite float values");
    const bool ok = passes->SetParameter(id,name,values,error); return Result(state,ok,error);
}
int SetBuffer(lua_State* state)
{
    auto* passes = Passes(state); std::string id,name,error;
    if (!passes || !Name(state,1,id) || !Name(state,2,name) || lua_type(state,3) != LUA_TSTRING) return Result(state,false,"Expected passId, buffer name and binary string");
    std::size_t count = 0; const auto* data = lua_tolstring(state,3,&count);
    const auto& config = passes->Configuration();
    if (!config) return Result(state,false,"Project Pass configuration is unavailable");
    const auto pass = std::find_if(config->passes.begin(),config->passes.end(),[&](const auto& p) { return p.definition.passId == id; });
    if (pass == config->passes.end()) return Result(state,false,"Unknown project Pass");
    const auto buffer = std::find_if(pass->definition.buffers.begin(),pass->definition.buffers.end(),[&](const auto& b) { return b.name == name; });
    if (buffer == pass->definition.buffers.end() || count > std::uint64_t(buffer->stride)*buffer->maxElements) return Result(state,false,"Buffer exceeds declaration");
    const std::vector<std::uint8_t> bytes(data,data+count);
    const bool ok = passes->SetBuffer(id,name,bytes,error); return Result(state,ok,error);
}
int SubmitInstances(lua_State* state)
{
    auto* passes = Passes(state); std::string id,error;
    if (!passes || !Name(state,1,id) || !lua_istable(state,2)) return Result(state,false,"Expected passId and projectDrawInstance array");
    const auto& config = passes->Configuration();
    if (!config) return Result(state,false,"Project Pass configuration is unavailable");
    const auto pass = std::find_if(config->passes.begin(),config->passes.end(),[&](const auto& p) { return p.definition.passId == id; });
    if (pass == config->passes.end()) return Result(state,false,"Unknown project Pass");
    const auto stream = std::find_if(pass->definition.buffers.begin(),pass->definition.buffers.end(),[](const auto& b) { return b.role == "instanceInput"; });
    const auto count = lua_rawlen(state,2);
    if (stream == pass->definition.buffers.end() || count > stream->maxElements) return Result(state,false,"Instance count exceeds declared stream capacity");
    std::vector<VansGraphics::VansProjectDrawInstance> instances(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        lua_rawgeti(state,2,static_cast<lua_Integer>(i+1));
        if (!lua_istable(state,-1)) { lua_pop(state,1); return Result(state,false,"Instance must contain model, color and data arrays"); }
        float values[24]; bool valid = true;
        const char* fields[] = {"model","color","data"};
        const std::size_t sizes[] = {16,4,4}; std::size_t offset = 0;
        for (std::size_t f = 0; f < 3; ++f)
        {
            lua_getfield(state,-1,fields[f]); valid = Floats(state,-1,sizes[f],values+offset) && valid;
            lua_pop(state,1); offset += sizes[f];
        }
        lua_pop(state,1);
        if (!valid) return Result(state,false,"Instance model[16], color[4], data[4] must be finite floats");
        std::memcpy(&instances[i],values,sizeof(instances[i]));
    }
    const bool ok = passes->SubmitInstances(id,instances,error); return Result(state,ok,error);
}
int OutputResource(lua_State* state)
{
    auto* passes = Passes(state); std::string id,target,key;
    if (!passes || !Name(state,1,id) || !Name(state,2,target) || !passes->GetOutputKey(id,target,key)) { lua_pushnil(state); return 1; }
    lua_pushlstring(state,key.data(),key.size()); return 1;
}
int OutputWindow(lua_State* state)
{
    auto* passes=Passes(state); std::string id,target; Vans::VansWorldTextureWindow window;
    if (!passes || !Name(state,1,id) || !Name(state,2,target) || !passes->GetOutputWindow(id,target,window))
    { lua_pushnil(state); return 1; }
    lua_newtable(state);
    lua_createtable(state,4,0);
    for (int i=0; i<4; ++i) {lua_pushnumber(state,window.worldRect[i]);lua_rawseti(state,-2,i+1);}
    lua_setfield(state,-2,"world_rect");
    lua_createtable(state,2,0);
    for (int i=0; i<2; ++i) {lua_pushinteger(state,window.resolution[i]);lua_rawseti(state,-2,i+1);}
    lua_setfield(state,-2,"resolution");
    lua_pushboolean(state,window.cameraCentered);lua_setfield(state,-2,"camera_centered");
    return 1;
}
}
void VansLuaProjectRenderPassBridge::Register(lua_State* state)
{
    const luaL_Reg bindings[] = {{"set_enabled",SetEnabled},{"get_enabled",GetEnabled},
        {"set_parameter",SetParameter},{"set_buffer",SetBuffer},{"submit_instances",SubmitInstances},
        {"get_output_resource",OutputResource},{"get_output_window",OutputWindow},{nullptr,nullptr}};
    lua_newtable(state); luaL_setfuncs(state,bindings,0); lua_setfield(state,-2,"project_render_pass");
}
}
