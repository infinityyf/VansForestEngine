#include "VansLuaTerrainBridge.h"
#include "VansScriptContext.h"
#include "../RenderCore/VansScene.h"
#include <cmath>
#include <limits>
#include <string_view>
extern "C"
{
#include <lauxlib.h>
}
namespace VansRuntime
{
namespace
{
VansGraphics::VansScene* Scene(lua_State* state)
{
    auto* context=VansScriptContext::GetInstance();
    return context && context->GetLuaState()==state?context->GetScene():nullptr;
}
int Result(lua_State* state,bool accepted,const std::string& error)
{
    lua_pushboolean(state,accepted);lua_pushlstring(state,error.data(),error.size());return 2;
}
bool Number(lua_State* state,int index,float& value)
{
    const double number=lua_tonumber(state,index);
    if (lua_type(state,index)!=LUA_TNUMBER || !std::isfinite(number) || std::abs(number)>std::numeric_limits<float>::max()) return false;
    value=static_cast<float>(number);return true;
}
bool Array(lua_State* state,int index,int count,float* values)
{
    if (!lua_istable(state,index) || lua_rawlen(state,index)!=count) return false;
    index=lua_absindex(state,index);
    for (int i=0; i<count; ++i)
    {
        lua_rawgeti(state,index,i+1);const bool valid=Number(state,-1,values[i]);lua_pop(state,1);
        if (!valid) return false;
    }
    return true;
}
int SetInteraction(lua_State* state)
{
    auto* scene=Scene(state);
    if (!scene || !lua_istable(state,1)) return Result(state,false,"Expected active scene and Terrain interaction table");
    Vans::VansTerrainInteraction input;
    lua_pushnil(state);
    while (lua_next(state,1))
    {
        bool valid=lua_type(state,-2)==LUA_TSTRING;
        std::size_t size=0; const auto* key=valid?lua_tolstring(state,-2,&size):nullptr;
        const std::string_view name=key?std::string_view(key,size):std::string_view{};
        if (name=="enabled" || name=="camera_centered")
        {
            valid=lua_isboolean(state,-1);
            if (name=="enabled") input.enabled=lua_toboolean(state,-1)!=0;
            else input.window.cameraCentered=lua_toboolean(state,-1)!=0;
        }
        else if (name=="world_rect") valid=Array(state,-1,4,&input.window.worldRect.x);
        else if (name=="height_weights") valid=Array(state,-1,4,&input.heightWeights.x);
        else if (name=="resolution")
        {
            float values[2];valid=Array(state,-1,2,values);
            if (valid) for (int i=0; i<2; ++i)
            {
                if (values[i]<1 || values[i]>16384 || std::floor(values[i])!=values[i]) {valid=false;break;}
                input.window.resolution[i]=static_cast<std::uint32_t>(values[i]);
            }
        }
        else if (name=="albedo" || name=="normal" || name=="height")
        {
            valid=lua_type(state,-1)==LUA_TSTRING;
            std::size_t count=0; const char* value=valid?lua_tolstring(state,-1,&count):nullptr;
            valid=valid && count<=512;
            if (valid) (name=="albedo"?input.albedo:name=="normal"?input.normal:input.height).assign(value,count);
        }
        else if (name=="albedo_strength") valid=Number(state,-1,input.albedoStrength);
        else if (name=="normal_strength") valid=Number(state,-1,input.normalStrength);
        else if (name=="max_displacement_meters") valid=Number(state,-1,input.maxDisplacementMeters);
        else if (name=="edge_fade_meters") valid=Number(state,-1,input.edgeFadeMeters);
        else valid=false;
        lua_pop(state,1);
        if (!valid) {lua_pop(state,1);return Result(state,false,"Invalid or unknown Terrain interaction field: "+std::string(name));}
    }
    std::string error; const bool accepted=scene->SetTerrainInteraction(input,error);
    return Result(state,accepted,error);
}
int SetEnabled(lua_State* state)
{
    auto* scene=Scene(state);
    if (!scene || !lua_isboolean(state,1)) return Result(state,false,"Expected active scene and boolean");
    auto input=scene->GetTerrainInteraction();input.enabled=lua_toboolean(state,1)!=0;
    std::string error;const bool accepted=scene->SetTerrainInteraction(input,error);return Result(state,accepted,error);
}
}
void VansLuaTerrainBridge::Register(lua_State* state)
{
    const luaL_Reg bindings[]={{"set_interaction",SetInteraction},{"set_interaction_enabled",SetEnabled},{nullptr,nullptr}};
    lua_newtable(state);luaL_setfuncs(state,bindings,0);lua_setfield(state,-2,"terrain");
}
}
