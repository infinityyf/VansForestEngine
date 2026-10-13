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
#include <glm/gtc/packing.hpp>

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

bool TestParticleAlphaFade()
{
    using namespace VansGraphics;
    const auto check = [](bool ok, const char* message)
    { if (!ok) std::cerr << "[ParticleAlphaFade] " << message << '\n'; return ok; };
    const auto near = [](float a, float b) { return std::abs(a-b) < 1.e-5f; };
    VansParticlePool pool; pool.Resize(32); pool.AllocSeedRandom(); pool.m_AliveCount=32;
    const glm::vec4 initial(.2f,.4f,.6f,.8f);
    for (uint32_t i=0;i<32;++i)
    { pool.m_Color[i]=initial; pool.m_SeedRandom[i]=1234u+i*2654435761u; pool.m_LifeTime[i]=.25f; }
    const auto seeds=pool.m_SeedRandom;
    VansUpdateAlphaFadeModule fade;
    fade.ExecuteInit(pool,0,32,glm::mat4(1)); fade.Execute(pool,1.f,glm::mat4(1));
    if (!check(pool.m_AlphaFade.empty() && pool.m_Color[0]==initial,
        "Disabled fades allocated state or changed existing color")) return false;
    fade.m_FadeOut.enabled=true; fade.m_FadeOut.timeDomain=VansParticleFadeTimeDomain::Seconds;
    fade.m_FadeOut.durationMin=fade.m_FadeOut.durationMax=.4f;
    fade.m_FadeOut.easing=VansParticleFadeEasing::Linear;
    fade.ExecuteInit(pool,0,32,glm::mat4(1)); fade.Execute(pool,.016f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.5f) && glm::vec3(pool.m_Color[0])==glm::vec3(initial),
        "Longer-than-life seconds fade was clamped or RGB changed")) return false;
    pool.m_Age[0]=.1f; pool.m_NormalizedAge[0]=.4f;
    fade.Execute(pool,.1f,glm::mat4(1)); fade.Execute(pool,.001f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.3f),"Fade accumulated alpha or used normalized seconds")) return false;
    pool.m_Color[0]=initial; pool.m_Age[0]=.05f; pool.m_NormalizedAge[0]=.1f;
    fade.m_FadeOut.enabled=false; fade.m_FadeIn.enabled=true;
    fade.m_FadeIn.timeDomain=VansParticleFadeTimeDomain::Seconds;
    fade.m_FadeIn.durationMin=fade.m_FadeIn.durationMax=.2f;
    fade.ExecuteInit(pool,0,1,glm::mat4(1)); fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.125f),"Seconds fade-in did not use smoothstep")) return false;
    pool.m_Color[0]=initial; pool.m_NormalizedAge[0]=.0625f;
    fade.m_FadeIn.timeDomain=VansParticleFadeTimeDomain::Lifetime;
    fade.m_FadeIn.durationMin=fade.m_FadeIn.durationMax=.25f;
    fade.m_FadeOut.enabled=true; fade.m_FadeOut.timeDomain=VansParticleFadeTimeDomain::Lifetime;
    fade.m_FadeOut.durationMin=fade.m_FadeOut.durationMax=.25f;
    fade.m_FadeOut.easing=VansParticleFadeEasing::SmoothStep;
    fade.ExecuteInit(pool,0,1,glm::mat4(1)); fade.Execute(pool,1.f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.125f),"Normalized fade-in used the wrong time domain")) return false;
    pool.m_NormalizedAge[0]=.2f; fade.Execute(pool,.01f,glm::mat4(1));
    pool.m_NormalizedAge[0]=.25f; fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.7168f),"Fade-in boundary reset the held alpha")) return false;
    pool.m_NormalizedAge[0]=.5f; fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.7168f),"Hold interval overwrote the current alpha")) return false;
    pool.m_NormalizedAge[0]=.8125f; fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.675f),"Normalized fade-out easing is incorrect")) return false;
    pool.m_Color[0]=initial; pool.m_NormalizedAge[0]=.2f;
    fade.m_FadeIn.durationMin=fade.m_FadeIn.durationMax=.5f;
    fade.m_FadeOut.durationMin=fade.m_FadeOut.durationMax=.9f;
    fade.m_FadeOut.easing=VansParticleFadeEasing::Linear;
    fade.ExecuteInit(pool,0,1,glm::mat4(1)); fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.8f*8.f/9.f),
        "Overlapping fade-out multiplied instead of overwriting fade-in")) return false;
    fade.m_FadeInFirst=false; fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.2816f),"Authored Out-to-In write order was changed")) return false;
    // splash 的 Out→In 两区间不重叠；后执行的 In 不得撤销末端淡出。
    fade.m_FadeIn.durationMin=fade.m_FadeIn.durationMax=.1f;
    fade.m_FadeOut.durationMin=fade.m_FadeOut.durationMax=.75f;
    pool.m_Color[0]=initial; fade.ExecuteInit(pool,0,1,glm::mat4(1));
    pool.m_NormalizedAge[0]=.625f; fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.4f),"Late fade-in undid splash fade-out")) return false;
    pool.m_NormalizedAge[0]=.25f; pool.m_Color[0].a=.37f;
    fade.Execute(pool,.01f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.37f),"Fade-out boundary overwrote an inactive lane")) return false;
    fade.m_FadeInFirst=true;
    fade.m_FadeOut.enabled=false; fade.m_FadeIn.durationMin=.1f; fade.m_FadeIn.durationMax=.05f;
    fade.m_FadeIn.randomExponent=1.12f;
    for (auto& color : pool.m_Color) color=initial;
    fade.ExecuteInit(pool,0,32,glm::mat4(1)); const auto sampled=pool.m_AlphaFade;
    fade.m_FadeIn.randomExponent=1.f; fade.ExecuteInit(pool,0,32,glm::mat4(1));
    float minimum=1,maximum=0;
    for (uint32_t i=0;i<32;++i)
    {
        uint32_t seed=seeds[i]^0x9e3779b9u;
        const float u=RandFloat(seed);
        if (!check(pool.m_AlphaFade[i]==sampled[i] && near(sampled[i].y,.1f-.05f*u),
            "Random exponent quantization or reversed endpoint direction changed")) return false;
        minimum=std::min(minimum,sampled[i].y); maximum=std::max(maximum,sampled[i].y);
    }
    if (!check(minimum>=.05f && maximum<=.1f && maximum-minimum>.02f && pool.m_SeedRandom==seeds,
        "Random fades were not varied/stable or advanced the atlas seed")) return false;
    pool.m_Age[31]=.03f; pool.m_NormalizedAge[31]=.03f;
    fade.Execute(pool,.01f,glm::mat4(1)); const float lastAlpha=pool.m_Color[31].a;
    pool.SwapRemoveAt(0); fade.Execute(pool,.5f,glm::mat4(1));
    if (!check(pool.m_AlphaFade[0]==sampled[31] && near(pool.m_Color[0].a,lastAlpha),
        "Swap removal changed the surviving particle fade")) return false;
    pool.m_Color[0]=glm::vec4(.1f,.2f,.3f,.4f); fade.ExecuteInit(pool,0,1,glm::mat4(1));
    if (!check(near(pool.m_AlphaFade[0].x,.4f),"Reused slot retained old initial alpha")) return false;

    // 实际发射器延迟出生与帧分割：到集合 .25 秒时，粒子只活了 .05 秒。
    ParticleTestJson json={{"name","FadeLateBirth"},{"global",{{"duration",1},{"fixedStep",0}}},
        {"emitters",ParticleTestJson::array({{{"name","Fade"},{"maxParticles",8},
            {"spawn",{{"type","Burst"},{"bursts",ParticleTestJson::array({{{"time",.2},{"count",1}}})}}},
            {"initialize",ParticleTestJson::array({{{"module","InitLifetime"},{"lifetime",{{"mode","Constant"},{"value",1}}}},
                {{"module","InitColor"},{"color",{.2,.4,.6,.8}}}})},
            {"update",ParticleTestJson::array({{{"module","UpdateAlphaFade"},{"fadeIn",{{"enabled",true},
                {"durationMin",.2},{"durationMax",.2},{"timeDomain","Seconds"}}}}})}}})}};
    auto asset=std::make_shared<VansParticleAsset>(); std::string error;
    if (!check(DecodeParticleAsset(json,{},*asset,error),error.c_str())) return false;
    VansParticleRuntime coarse,fine; coarse.SetAsset(asset); fine.SetAsset(asset);
    coarse.Play();fine.Play();
    coarse.Update(.25f);coarse.SwapBuffers();
    for (int i=0;i<5;++i) { fine.Update(.05f);fine.SwapBuffers(); }
    if (!check(coarse.GetRenderBuffer().size()==1 && fine.GetRenderBuffer().size()==1 &&
        near(coarse.GetRenderBuffer()[0].m_Color.a,.125f) &&
        near(fine.GetRenderBuffer()[0].m_Color.a,.125f),"Delayed birth or frame partition changed fade")) return false;
    // 淡入延迟使用同一时间域；零默认值的现有测试继续验证原写入语义。
    json["emitters"][0]["update"][0]["fadeInDelay"]=.1;
    auto delayed=std::make_shared<VansParticleAsset>();
    if (!check(DecodeParticleAsset(json,{},*delayed,error),error.c_str())) return false;
    if (!check(std::abs(double(EncodeParticleAsset(*delayed)["emitters"][0]["update"][0]["fadeInDelay"])-.1)<1.e-6,
        "Fade-in delay was lost during authoring round trip")) return false;
    VansParticleRuntime delayRuntime;delayRuntime.SetAsset(delayed);delayRuntime.Play();
    delayRuntime.Update(.3f);delayRuntime.SwapBuffers();
    if (!check(near(delayRuntime.GetEmitter(0)->ParticlePool().m_Color[0].a,0),
        "Fade-in delay started from collection age instead of particle birth")) return false;
    delayRuntime.Update(.1f);delayRuntime.SwapBuffers();
    if (!check(near(delayRuntime.GetEmitter(0)->ParticlePool().m_Color[0].a,.4f),
        "Delayed smoothstep did not start at the authored age")) return false;
    auto negativeDelay=json;negativeDelay["emitters"][0]["update"][0]["fadeInDelay"]=-.1;
    VansParticleAsset badDelay;
    if (!check(!DecodeParticleAsset(negativeDelay,{},badDelay,error),"Negative fade delay accepted")) return false;
    for (const auto& invalid : std::vector<ParticleTestJson>{
        {{"durationMin",0}},{{"durationMax",-1}},{{"randomExponent",-1}},
        {{"timeDomain","Frames"}},{{"easing","Bias"}},{{"fadeBias",.75}}})
    {
        auto bad=json;
        for (const auto& field : invalid.items()) bad["emitters"][0]["update"][0]["fadeIn"][field.key()]=field.value();
        VansParticleAsset rejected;
        if (!check(!DecodeParticleAsset(bad,{},rejected,error),"Invalid fade schema was accepted")) return false;
    }
    std::cout<<"[ParticleAlphaFade] seconds=1 longerThanLifetime=1 normalized=1 smoothstep=1 overwriteOrder=1 inactiveLanePreserved=1 strictBoundaries=1 splashOrder=1 stableRandom=1 reversedBounds=1 quantizedExponent=1 atlasSeedPreserved=1 swapRemove=1 reusedSlot=1 delayedBirth=1 framePartition=1 fadeInDelay=1 disabledAllocation=0 schemaReject=1\n";
    return true;
}

bool TestParticleSourceRandomInit()
{
    using namespace VansGraphics;
    const auto check=[](bool ok,const char* why) {
        if (!ok) std::cerr<<"[ParticleSourceRandom] "<<why<<'\n';return ok;
    };
    const auto near=[](float a,float b) { return std::abs(a-b)<1.e-5f; };
    VansFloatCurve scalar;scalar.m_Mode=FloatCurveMode::RandomBetween;
    scalar.m_Min=.5f;scalar.m_Max=1.5f;
    for (const float u : {0.f,.25f,.5f,.75f,1.f})
        if (!check(scalar.Evaluate(0,u)==.5f+u,"Default uniform changed")) return false;
    scalar.m_RandomDistribution=VansParticleRandomDistribution::ExponentialBias;
    scalar.m_RandomBias=-.052632f;
    if (!check(near(scalar.Evaluate(0,0),.5f) && near(scalar.Evaluate(0,1),1.5f)
        && near(scalar.Evaluate(0,.5f),.75f),"Source exponential lifetime anchors incorrect")) return false;
    scalar.m_RandomDistribution=VansParticleRandomDistribution::StandardBias;scalar.m_RandomBias=-.19f;
    if (!check(near(scalar.Evaluate(0,.5f),.905f),"Source standard negative bias anchor incorrect")) return false;
    VansParticlePool pool;pool.Resize(4096);pool.AllocSeedRandom();pool.m_AliveCount=4096;
    for (uint32_t i=0;i<4096;++i)pool.m_SeedRandom[i]=17u+i*2654435761u;
    VansInitLifetimeModule life;life.m_Lifetime=scalar;
    life.m_Lifetime.m_RandomDistribution=VansParticleRandomDistribution::ExponentialBias;
    life.m_Lifetime.m_RandomBias=-.052632f;
    life.ExecuteInit(pool,0,4096,glm::mat4(1));
    auto distribution=pool.m_LifeTime;std::sort(distribution.begin(),distribution.end());
    // CDF 分位数来自 u² 分布，独立检查真实初始化采样，不用待测资产生成期望值。
    for (const float q : {.25f,.5f,.75f})
        if (!check(std::abs(distribution[uint32_t(q*4095)]-(.5f+q*q))<.025f,
            "Biased population became uniform or lost its source distribution")) return false;
    VansInitColorModule tint;tint.m_Color={.2f,.4f,.6f,.8f};
    const auto before=pool.m_SeedRandom;tint.ExecuteInit(pool,0,4096,glm::mat4(1));
    if (!check(pool.m_SeedRandom==before && pool.m_Color[0]==tint.m_Color,"Default tint consumed RNG")) return false;
    VansFloatCurve first;first.m_Mode=FloatCurveMode::RandomBetween;first.m_Min=.6f;first.m_Max=.4f;
    VansFloatCurve second;second.m_Mode=FloatCurveMode::RandomBetween;second.m_Min=.5f;second.m_Max=.8f;
    tint.m_Color.a=1;tint.m_AlphaScales={first,second};tint.ExecuteInit(pool,0,4096,glm::mat4(1));
    bool varied=false;
    for (uint32_t i=0;i<4096;++i) {
        varied|=pool.m_Color[i].a!=pool.m_Color[0].a;
        if (!check(pool.m_Color[i].a>=.2f && pool.m_Color[i].a<=.48f
            && glm::vec3(pool.m_Color[i])==glm::vec3(tint.m_Color),"Independent alpha scales changed RGB or escaped bounds")) return false;
    }
    if (!check(varied,"Random Alpha/Alpha2 were folded into one mean")) return false;
    VansUpdateColorOverLifetime gradient;
    gradient.m_Gradient.m_Stops={{0,{.2f,.4f,.6f,1}},{1,{.2f,.4f,.6f,0}}};
    gradient.ExecuteInit(pool,0,4096,glm::mat4(1));
    if (!check(pool.m_InitialAlpha.empty(),"Default gradient allocated initial alpha")) return false;
    gradient.m_ScaleInitialAlpha=true;gradient.ExecuteInit(pool,0,4096,glm::mat4(1));
    const float initial=pool.m_Color[0].a;
    std::fill(pool.m_NormalizedAge.begin(),pool.m_NormalizedAge.end(),.5f);
    gradient.Execute(pool,.1f,glm::mat4(1));gradient.Execute(pool,.1f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,initial*.5f),"Gradient overwrote or repeatedly multiplied initial alpha")) return false;
    const float last=pool.m_InitialAlpha.back();pool.SwapRemoveAt(0);
    if (!check(pool.m_InitialAlpha[0]==last,"Swap removal lost initial alpha")) return false;
    pool.m_Color[0].a=.123f;gradient.ExecuteInit(pool,0,1,glm::mat4(1));gradient.Execute(pool,.1f,glm::mat4(1));
    if (!check(near(pool.m_Color[0].a,.0615f),"Reused slot retained a previous initial alpha")) return false;
    auto workspace=std::filesystem::current_path();
    while (!std::filesystem::exists(workspace/"DustV3Project")) {
        if (workspace==workspace.root_path())return check(false,"DustV3 fixture root unavailable");
        workspace=workspace.parent_path();
    }
    uint32_t layers=0,biased=0;
    for (const auto* name : {"Fire","Ignition","Air","Extinguish"}) {
        const auto path=workspace/(std::string("DustV3Project/Assets/CS2/Throwables/Utility/")+name+".particle");
        std::ifstream file(path);ParticleTestJson json;file>>json;
        VansParticleAsset decoded;std::string error;
        if (!check(DecodeParticleAsset(json,path,decoded,error),error.c_str())) return false;
        for (const auto& emitter : decoded.m_Emitters) {
            ++layers;bool colorFound=false;
            for (const auto& module : emitter->m_InitModules) {
                if (const auto* l=dynamic_cast<const VansInitLifetimeModule*>(module.get()))
                    biased+=l->m_Lifetime.m_RandomDistribution!=VansParticleRandomDistribution::Uniform;
                if (const auto* c=dynamic_cast<const VansInitColorModule*>(module.get())) {
                    colorFound=true;if (!check(c->m_Color.a==1 && c->m_AlphaScales.size()<=3,"Formal layer folded or duplicated alpha sampling"))return false;
                    if (emitter->m_Name=="incendiary_groundfire_main_center"
                        && !check(c->m_AlphaScales.size()==2,"Main-center lost its independent Alpha2"))return false;
                }
            }
            if (!check(colorFound,"Formal layer missing tint initializer"))return false;
        }
        auto encoded=EncodeParticleAsset(decoded);VansParticleAsset again;
        if (!check(DecodeParticleAsset(encoded,path,again,error) && encoded==EncodeParticleAsset(again),"Source scalar codec roundtrip failed"))return false;
        auto invalid=encoded;invalid["emitters"][0]["initialize"][0]["lifetime"]["randomDistribution"]="Unknown";
        if (!check(!DecodeParticleAsset(invalid,path,again,error),"Unknown distribution accepted"))return false;
        invalid=encoded;invalid["emitters"][0]["initialize"][0]["lifetime"]["randomBias"]=2;
        if (!check(!DecodeParticleAsset(invalid,path,again,error),"Invalid bias accepted"))return false;
    }
    if (!check(layers==23 && biased==8,"Formal source layer/biased lifetime coverage changed"))return false;
    std::cout<<"[ParticleSourceRandom] sourceLayers=23 biasedLifetimes=8 exponentialCDF=1 standardNegativeBias=1 reversedAlphaBounds=1 independentAlpha=1 gradientInitial=1 noRepeatedMultiply=1 swapReuse=1 defaultsPreserved=1 typedRoundtrip=1 invalidRejected=1\n";
    return true;
}

bool TestParticleCollectionAlphaClock()
{
    using namespace VansGraphics;
    const auto check=[](bool ok,const char* why) {
        if (!ok)std::cerr<<"[ParticleCollectionAlpha] "<<why<<'\n';return ok;
    };
    for (const int fps : {30,120}) {
        auto asset=std::make_shared<VansParticleAsset>();asset->m_Duration=10;asset->m_Loop=false;
        asset->m_DrainFade=0;asset->m_FixedStep=0;
        auto emitter=std::make_unique<VansParticleEmitter>();emitter->m_MaxParticles=128;emitter->m_EmissionChannel=1;
        emitter->m_SpawnConfig.m_Rate=2;emitter->m_RendererConfig.m_LightingMode=VansParticleLightingMode::SpriteCard;
        auto life=std::make_unique<VansInitLifetimeModule>();life->m_Lifetime.m_Mode=FloatCurveMode::Constant;life->m_Lifetime.m_Value=20;
        emitter->m_InitModules.push_back(std::move(life));
        auto& card=emitter->m_RendererConfig.m_Card;
        if (!check(card.alphaClock==VansParticleCardAlphaClock::NormalizedEffect,"Default clock changed"))return false;
        card.alphaClock=VansParticleCardAlphaClock::EmissionPointSeconds;
        card.alphaOverEffect.m_Mode=FloatCurveMode::Curve;card.alphaOverEffect.m_Keys={{0,1},{7,1},{8,0}};
        asset->m_Emitters.push_back(std::move(emitter));
        const auto json=EncodeParticleAsset(*asset);VansParticleAsset decoded;std::string error;
        if (!check(DecodeParticleAsset(json,{},decoded,error),error.c_str()))return false;
        auto invalid=json;invalid["emitters"][0]["renderer"]["spriteCard"]["alphaClock"]="Unknown";
        if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Unknown clock accepted"))return false;
        invalid=json;invalid["emitters"][0]["renderer"]["spriteCard"]["alphaClock"]="NormalizedEffect";
        if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Seconds keys accepted in normalized domain"))return false;
        VansParticleManager manager;auto handle=manager.Create(asset);
        VansParticleEmissionPoint first;first.id=1;first.channel=1;first.position={1,0,0};
        auto later=first;later.id=2;later.position={2,0,0};later.bornTime=2;
        if (!check(manager.SetEmissionPoints(handle,{first,later}) && manager.Queue(handle,VansParticleControl::Play),"Point initialization failed"))return false;
        const auto tick=[&](float dt) { manager.TickMainThread(dt);manager.WaitForUpdateAndSwap(); };
        for (int frame=0;frame<fps*15/2;++frame)tick(1.f/fps);
        auto* runtime=manager.Resolve(handle);bool fresh=false,late=false;
        for (const auto& value : runtime->GetRenderBuffer()) {
            const float expected=value.m_WorldPosition.x==1 ? .5f : 1.f;
            if (!check(std::abs(value.m_Color.a-expected)<.001f,"Collection age became particle age or global age"))return false;
            late|=value.m_WorldPosition.x==2;
        }
        const auto& pool=runtime->GetEmitter(0)->ParticlePool();
        for (uint32_t i=0;i<pool.m_AliveCount;++i)
            fresh|=pool.m_Position[i].x==1 && pool.m_Age[i]<.01f;
        if (!check(late && fresh,"Late point or newly born particle was not covered"))return false;
        manager.Queue(handle,VansParticleControl::Pause);tick(0);const double paused=runtime->GetPlayTime();tick(1);
        if (!check(runtime->GetPlayTime()==paused,"Paused collection age advanced"))return false;
        manager.Queue(handle,VansParticleControl::Play);tick(0);
        for (int frame=0;frame<fps;++frame)tick(1.f/fps);
        manager.Queue(handle,VansParticleControl::StopEmitting);tick(0);const auto before=runtime->AliveInstanceCount();
        for (int frame=0;frame<fps;++frame)tick(1.f/fps);
        if (!check(runtime->AliveInstanceCount()==before && runtime->GetState()==VansParticlePlaybackState::Draining,
            "Drain emitted or prematurely removed particles"))return false;
        for (const auto& value : runtime->GetRenderBuffer())
            if (!check(std::abs(value.m_Color.a-(value.m_WorldPosition.x==1 ? 0.f : .5f))<.001f,
                "Local collection clock froze or restarted during drain"))return false;
        manager.Queue(handle,VansParticleControl::Stop);tick(0);
        if (!check(runtime->IsFinished() && runtime->GetRenderBuffer().empty(),"Clock-owned particles failed to clear"))return false;
    }
    std::cout<<"[ParticleCollectionAlpha] fps30_120=1 localPointClock=1 lateBirth=1 newParticleInheritsCollectionAge=1 pauseResume=1 drainContinues=1 explicitStop=1 defaultClockPreserved=1 typedDomainReject=1\n";
    return true;
}

bool TestParticleInitializationAge()
{
    using namespace VansGraphics;
    const auto check=[](bool ok,const char* why) {
        if (!ok)std::cerr<<"[ParticleInitializationAge] "<<why<<'\n';return ok;
    };
    const auto near=[](float a,float b) { return std::abs(a-b)<.0002f; };
    VansParticlePool pool;pool.Resize(1);pool.AllocSeedRandom();pool.m_AliveCount=1;
    VansInitLifetimeModule life;life.m_Lifetime.m_Mode=FloatCurveMode::Constant;life.m_Lifetime.m_Value=2;
    pool.m_InitializationCollectionAge=6;pool.m_SeedRandom[0]=197;
    uint32_t expectedSeed=197;RandFloat(expectedSeed);life.ExecuteInit(pool,0,1,glm::mat4(1));
    if (!check(pool.m_LifeTime[0]==2 && pool.m_SeedRandom[0]==expectedSeed,"Default lifetime or RNG changed"))return false;
    life.m_CollectionAgeOperation=VansParticleLifetimeAgeOperation::Multiply;
    life.m_CollectionAgeCurve.m_Mode=FloatCurveMode::Curve;life.m_CollectionAgeCurve.m_Keys={{5,1},{7,.5f}};
    for (const auto anchor : {glm::vec2(4,2),glm::vec2(5,2),glm::vec2(6,1.5f),glm::vec2(7,1),glm::vec2(8,1)}) {
        pool.m_InitializationCollectionAge=anchor.x;life.ExecuteInit(pool,0,1,glm::mat4(1));
        if (!check(near(pool.m_LifeTime[0],anchor.y),"Clamped collection-age multiplier missed source anchor"))return false;
    }
    life.m_CollectionAgeOperation=VansParticleLifetimeAgeOperation::Replace;
    life.m_CollectionAgeLoopPeriod=1;life.m_CollectionAgeCurve.m_Keys={{0,.75f},{1,1.5f}};
    for (const auto anchor : {glm::vec2(0,.75f),glm::vec2(.25f,.9375f),glm::vec2(.75f,1.3125f),glm::vec2(1,.75f),glm::vec2(2,.75f)}) {
        pool.m_InitializationCollectionAge=anchor.x;life.ExecuteInit(pool,0,1,glm::mat4(1));
        if (!check(near(pool.m_LifeTime[0],anchor.y),"Looped replacement missed source cycle or seam"))return false;
    }
    VansInitColorModule tint;tint.m_Color={.2f,.4f,.6f,1};VansFloatCurve factor;factor.m_Value=2;
    tint.m_AlphaScales={factor};const auto seedBefore=pool.m_SeedRandom[0];tint.ExecuteInit(pool,0,1,glm::mat4(1));
    if (!check(pool.m_Color[0].a==2 && glm::vec3(pool.m_Color[0])==glm::vec3(tint.m_Color)
        && pool.m_SeedRandom[0]==seedBefore,"Detail multiplier was clamped, changed RGB or consumed RNG"))return false;
    const auto makeAsset=[](bool delayed=false) {
        auto asset=std::make_shared<VansParticleAsset>();asset->m_Duration=8;asset->m_Loop=false;
        asset->m_DrainFade=0;asset->m_FixedStep=0;
        asset->m_StartDelay=delayed ? .4f : 0.f;
        auto emitter=std::make_unique<VansParticleEmitter>();emitter->m_MaxParticles=64;
        emitter->m_SpawnConfig.m_Type=VansSpawnType::Burst;emitter->m_SpawnConfig.m_Bursts={{.125f,1,8,.25f}};
        auto module=std::make_unique<VansInitLifetimeModule>();module->m_Lifetime.m_Mode=FloatCurveMode::Constant;
        module->m_Lifetime.m_Value=2;module->m_CollectionAgeOperation=delayed
            ? VansParticleLifetimeAgeOperation::None : VansParticleLifetimeAgeOperation::Replace;
        module->m_CollectionAgeLoopPeriod=1;module->m_CollectionAgeCurve.m_Mode=FloatCurveMode::Curve;
        module->m_CollectionAgeCurve.m_Keys={{0,.75f},{1,1.5f}};emitter->m_InitModules.push_back(std::move(module));
        if (delayed) {
            emitter->m_RendererConfig.m_LightingMode=VansParticleLightingMode::SpriteCard;
            auto& card=emitter->m_RendererConfig.m_Card;card.alphaClock=VansParticleCardAlphaClock::EffectSeconds;
            card.alphaOverEffect.m_Mode=FloatCurveMode::Curve;card.alphaOverEffect.m_Keys={{0,1},{.5f,0}};
        }
        asset->m_Emitters.push_back(std::move(emitter));return asset;
    };
    for (const int fps : {30,120}) {
        auto asset=makeAsset();const auto json=EncodeParticleAsset(*asset);VansParticleAsset decoded;std::string error;
        if (!check(DecodeParticleAsset(json,{},decoded,error) && json==EncodeParticleAsset(decoded),"Collection-age typed roundtrip failed"))return false;
        auto invalid=json;invalid["emitters"][0]["initialize"][0]["collectionAge"]["operation"]="Unknown";
        if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Unknown age operation accepted"))return false;
        invalid=json;invalid["emitters"][0]["initialize"][0]["collectionAge"]["loopPeriod"]=-1;
        if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Negative period accepted"))return false;
        invalid=json;invalid["emitters"][0]["initialize"][0]["collectionAge"]["curve"]["keys"][1]["t"]=3601;
        if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Out-of-domain age curve accepted"))return false;
        VansParticleManager manager;const auto handle=manager.Create(asset);
        VansParticleEmissionPoint first;first.id=1;first.position={1,0,0};
        auto later=first;later.id=2;later.position={2,0,0};later.bornTime=.35f;
        if (!check(manager.SetEmissionPoints(handle,{first,later}) && manager.Queue(handle,VansParticleControl::Play),"Point initialization rejected"))return false;
        const auto tick=[&](float dt) {manager.TickMainThread(dt);manager.WaitForUpdateAndSwap();};tick(0);
        const auto& runtime=*manager.Resolve(handle);
        const auto verify=[&] {
            const auto& particles=runtime.GetEmitter(0)->ParticlePool();bool foundLater=false;
            for (uint32_t i=0;i<particles.m_AliveCount;++i) {
                const bool late=particles.m_Position[i].x==2;foundLater|=late;
                const float age=runtime.GetPlayTime()-particles.m_Age[i]-(late ? .35f : 0.f);
                const float expected=.75f+.75f*(age-std::floor(age));
                if (!check(near(particles.m_LifeTime[i],expected),"Birth used particle age, frame end, global point age or frozen emission time"))return false;
            }
            return check(particles.m_AliveCount>0 && foundLater,"Actual point births missing");
        };
        for (int frame=0;frame<fps*3/2;++frame)tick(1.f/fps);
        if (!verify())return false;
        manager.Queue(handle,VansParticleControl::Pause);tick(0);const auto paused=runtime.GetPlayTime();tick(1);
        if (!check(runtime.GetPlayTime()==paused,"Paused initialization clock advanced"))return false;
        manager.Queue(handle,VansParticleControl::Play);manager.Queue(handle,VansParticleControl::StopEmitting);tick(.2f);
        if (!check(!runtime.IsFinished() && manager.Queue(handle,VansParticleControl::RefreshEmission),"Drain ended before refresh check"))return false;
        tick(0);for (int frame=0;frame<fps/2;++frame)tick(1.f/fps);
        if (!verify())return false;
        manager.Queue(handle,VansParticleControl::Restart);tick(.5f);
        if (!verify())return false;
    }
    auto delayed=makeAsset(true);
    VansParticleRuntime delayRuntime;delayRuntime.SetAsset(delayed);delayRuntime.Play();delayRuntime.Update(.7f);delayRuntime.SwapBuffers();
    if (!check(near(delayRuntime.GetPlayTime(),.3f) && !delayRuntime.GetRenderBuffer().empty(),"Delayed clock fixture failed"))return false;
    for (const auto& value : delayRuntime.GetRenderBuffer())
        if (!check(near(value.m_Color.a,.4f),"Start delay was subtracted twice from seconds renderer clock"))return false;
    auto workspace=std::filesystem::current_path();while (!std::filesystem::exists(workspace/"DustV3Project")) {
        if (workspace==workspace.root_path())return check(false,"DustV3 root unavailable");workspace=workspace.parent_path();
    }
    const auto path=workspace/"DustV3Project/Assets/CS2/Throwables/Utility/Fire.particle";
    std::ifstream file(path);ParticleTestJson json;file>>json;VansParticleAsset formal;std::string error;
    if (!check(DecodeParticleAsset(json,path,formal,error),error.c_str()))return false;
    uint32_t ages=0,detail=0;
    for (const auto& layer : formal.m_Emitters)for (const auto& module : layer->m_InitModules) {
        if (const auto* l=dynamic_cast<const VansInitLifetimeModule*>(module.get())) {
            if (layer->m_Name=="incendiary_child_flame03a") {
                ++ages;if (!check(l->m_CollectionAgeOperation==VansParticleLifetimeAgeOperation::Replace
                    && l->m_CollectionAgeLoopPeriod==1 && near(l->m_CollectionAgeCurve.Evaluate(.25f),.9375f),"Formal flame03a cycle missing"))return false;
            }
            if (layer->m_Name=="incendiary_groundfire_snapshot_child_base") {
                ++ages;if (!check(l->m_CollectionAgeOperation==VansParticleLifetimeAgeOperation::Multiply
                    && l->m_CollectionAgeLoopPeriod==0 && near(l->m_CollectionAgeCurve.Evaluate(6),.75f),"Formal late-collection lifetime multiplier missing"))return false;
            }
        }
        if (const auto* c=dynamic_cast<const VansInitColorModule*>(module.get()))
            if (layer->m_Name=="incendiary_child_flame01a" || layer->m_Name=="incendiary_child_flame03a" || layer->m_Name=="incendiary_groundfire_climbingoutline") {
                ++detail;if (!check(c->m_AlphaScales.size()==2 && c->m_AlphaScales.back().m_Mode==FloatCurveMode::Constant
                    && c->m_AlphaScales.back().m_Value==2,"Formal LOD0/1 alpha multiplier missing or Alpha2 duplicated"))return false;
            }
    }
    if (!check(ages==2 && detail==3,"Formal dynamic initialization coverage incomplete"))return false;
    std::cout<<"[ParticleInitializationAge] sourceMultiply=1 loopReplace=1 exactSeams=1 defaultsRngPreserved=1 alphaAboveOne=1 fps30_120=1 actualBirthTime=1 localPointOrigin=1 pauseResume=1 drainRefresh=1 restart=1 startDelayCountedOnce=1 formalAgeLayers=2 formalDetailLayers=3 typedRoundtrip=1 invalidRejected=1\n";
    return true;
}

bool TestDustV3SmokeEnvelope()
{
    using namespace VansGraphics;
    const auto check=[](bool ok,const char* text) {
        if (!ok) std::cerr<<"[DustV3SmokeEnvelope] "<<text<<'\n';return ok;
    };
    auto workspace=std::filesystem::current_path();
    while (!std::filesystem::exists(workspace/"DustV3Project")) {
        if (workspace==workspace.root_path()) return check(false,"DustV3 fixture root unavailable");
        workspace=workspace.parent_path();
    }
    const auto path=workspace/"DustV3Project/Assets/CS2/Throwables/Utility/Smoke.particle";
    std::ifstream file(path);ParticleTestJson json;file>>json;
    auto asset=std::make_shared<VansParticleAsset>();std::string error;
    if (!check(DecodeParticleAsset(json,path,*asset,error),error.c_str())) return false;
    if (!check(!asset->m_Loop && !asset->m_Prewarm && asset->m_StartDelay==0 && asset->m_FixedStep==0,
        "Smoke clock did not start at volume deployment")) return false;
    // 期望值直接来自部署时序证据；不从待测配置反推出正确答案。
    const auto smooth=[](double x) { x=std::clamp(x,0.0,1.0);return x*x*(3-2*x); };
    for (const bool fine : {false,true}) {
        VansParticleRuntime runtime;runtime.SetAsset(asset);runtime.SetRandomSeed(1234);runtime.Play();
        double previous=0;
        for (const double target : {.05,.1,.2,.8,1.5,5.,17.,19.5,21.5,22.1}) {
            double remaining=target-previous;
            while (remaining>1.e-9) {
                const double step=fine ? std::min(remaining,.01) : remaining;
                runtime.Update(static_cast<float>(step));runtime.SwapBuffers();remaining-=step;
            }
            previous=target;
            const double envelope=smooth((target-.1)/1.4)*smooth((22-target)/5);
            const auto& pool=runtime.GetEmitter(0)->ParticlePool();
            if (target<22) {
                if (!check(pool.m_AliveCount==48,"Finite smoke burst changed particle count")) return false;
                for (uint32_t i=0;i<pool.m_AliveCount;++i)
                    if (!check(std::abs(pool.m_Color[i].a-.85*envelope)<.0002 &&
                        glm::length(glm::vec3(pool.m_Color[i])-glm::vec3(.68f,.69f,.7f))<1.e-5f,
                        "Source deployment envelope or preserved RGB differs across frame partitions")) return false;
                const auto& medium=runtime.GetVolumetricRenderBuffer();
                if (envelope>0) {
                    if (!check(medium.size()==48,"Positive envelope did not reach published medium")) return false;
                    for (const auto& point : medium)
                        if (!check(std::abs(point.m_ScatteringAlbedoExtinction.a-1.7*envelope)<.0004,
                            "Rendered medium did not carry CPU density fade")) return false;
                    if (!check(runtime.SampleVolumetricExtinction(glm::vec3(medium[0].m_WorldPositionRadius))>0,
                        "Extinguish consumer could not sample published smoke")) return false;
                } else if (!check(medium.empty() && runtime.SampleVolumetricExtinction({0,0,0})==0,
                    "Smoke had extinction before fade-in")) return false;
                if (target==5) {
                    runtime.Pause();runtime.Update(10);runtime.SwapBuffers();runtime.Play();
                    if (!check(std::abs(runtime.GetPlayTime()-5)<.001,"Pause advanced or restarted smoke time")) return false;
                }
            } else if (!check(pool.m_AliveCount==0 && runtime.GetVolumetricRenderBuffer().empty() && runtime.IsFinished(),
                "Smoke medium or particles survived the 22-second density window")) return false;
        }
    }
    std::cout<<"[DustV3SmokeEnvelope] sourceAsset=1 fadeIn=.1-1.5 hold=1.5-17 fadeOut=17-22 smoothstep=1 partitions=2 publishedMedium=1 extinguishSampling=1 pauseResume=1 expiry=1\n";
    return true;
}

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
    if (!check(moduleSchemas.size() == 14 && initializeCount == 7,
        "Module catalog does not own all seven initialize and seven update modules")) return false;
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
        {{"initialize",ParticleTestJson::array({{{"module","InitPositionOffset"},{"minOffset",{1,2,3}},{"maxOffset",{0,1,2}}}})}},
        {{"initialize",ParticleTestJson::array({{{"module","InitPositionOffset"},{"minOffset",{0,0}}}})}},
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
        {{"phase", "initialize"}, {"index", 6}, {"field", "minOffset"}, {"value", {-10001,0,0}}},
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
            if (entry.path().filename()=="Extinguish.particle")
            {
                auto definition=std::make_shared<VansParticleAsset>();
                if (!check(VansParticleAssetStorage::Load(entry.path(),*definition,error),error)) return false;
                if (!check(definition->m_EmissionFrame==VansParticleEmissionFrame::Source,
                    "Extinguish root orientation is disabled by world emission-frame configuration")) return false;
                VansParticleRuntime reference,directed;
                reference.SetAsset(definition); directed.SetAsset(definition);
                reference.SetRandomSeed(123); directed.SetRandomSeed(123);
                glm::mat4 origin(1); origin[3]=glm::vec4(5,6,7,1);
                auto pose=origin; pose[0]=glm::vec4(0,0,-1,0); pose[2]=glm::vec4(1,0,0,0);
                reference.SetOwnerWorldTransform(origin); directed.SetOwnerWorldTransform(pose);
                reference.Play(); directed.Play(); reference.Update(.12f); directed.Update(.12f);
                const glm::mat3 rotation(pose);
                for (std::size_t i=0;i<definition->m_Emitters.size();++i) {
                    const auto& a=reference.GetEmitter(i)->ParticlePool();
                    const auto& b=directed.GetEmitter(i)->ParticlePool();
                    if (!check(a.m_AliveCount>0 && a.m_AliveCount==b.m_AliveCount,
                        "Directed extinguish changed finite child emission counts")) return false;
                    for (std::size_t j=0;j<a.m_AliveCount;++j)
                        if (!check(glm::length(b.m_Velocity[j]-rotation*a.m_Velocity[j])<1.e-4f
                            && glm::length((b.m_Position[j]-glm::vec3(origin[3]))-rotation*(a.m_Position[j]-glm::vec3(origin[3])))<1.e-4f,
                            "Extinguish child velocity or position did not inherit root orientation")) return false;
                }
                const auto oldCount=directed.GetEmitter(0)->AliveCount();
                pose[3].x+=100; directed.SetOwnerWorldTransform(pose); directed.Update(1.f/60);
                for (std::size_t j=0;j<oldCount;++j)
                    if (!check(glm::length(directed.GetEmitter(0)->ParticlePool().m_Position[j]-glm::vec3(origin[3]))<5,
                        "Existing extinguish particles moved with a changed source transform")) return false;
                directed.Update(3); directed.SwapBuffers();
                if (!check(directed.IsFinished() && directed.GetRenderBuffer().empty(),
                    "Directed extinguish did not finish and release its render points")) return false;
                std::cout << "[ExtinguishRoot] children=" << definition->m_Emitters.size()
                    << " localVelocity=1 localPosition=1 worldSimulation=1 expiry=1\n";
            }
            ++count;
        }
    }
    if (!check(count>=8 && renderSortedEmitterCount>=1,
        "Production particle assets or render-owned sort policies were not validated")) return false;
    std::cout << "[ParticleAuthoring] moduleCatalog=14 constructorDefaults=1 catalogConstraints=1 activeAndInactiveRoundTrip=1 invalidConfiguration=1 unsupportedEncodeRejected=1 serializedValueCodec=1 assets="
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
bool TestSurfaceEmissionPoints()
{
    using namespace VansGraphics;
    const auto check=[](bool ok,const char* why) { if (!ok) std::cerr<<"[ParticleCard] "<<why<<'\n'; return ok; };
    auto asset=std::make_shared<VansParticleAsset>();
    asset->m_Duration=2; asset->m_Loop=false; asset->m_DrainFade=.15f;
    auto emitter=std::make_unique<VansParticleEmitter>();
    emitter->m_EmissionChannel=1; emitter->m_MaxParticles=16;
    emitter->m_SpawnConfig.m_Rate=2;
    auto life=std::make_unique<VansInitLifetimeModule>();
    life->m_Lifetime.m_Mode=FloatCurveMode::Constant; life->m_Lifetime.m_Value=10;
    emitter->m_InitModules.push_back(std::move(life));
    emitter->m_RendererConfig.m_LightingMode=VansParticleLightingMode::SpriteCard;
    emitter->m_RendererConfig.m_SpriteSheetEnabled=true;
    emitter->m_RendererConfig.m_SpriteColumns=2; emitter->m_RendererConfig.m_SpriteRows=4;
    auto& card=emitter->m_RendererConfig.m_Card;
    if (!check(!card.lookupMultiply && !card.tintBeforeDesaturation && !card.breakupPreserveAlpha,
        "Default card color policy changed")) return false;
    card.orientation=VansParticleCardOrientation::Surface;
    card.output=VansParticleCardOutput::Additive;
    card.lookupTextureGuid="12dabecb-3fea-4f62-8b6b-c9c2c2602891"; card.lookupBlend=.5f;
    card.lookupMultiply=true; card.tintBeforeDesaturation=true;
    card.breakup=VansParticleCardBreakup::Zoom;
    card.breakupTextureGuid="1f7a1cca-02e5-4318-9c44-6b61a6ac092f";
    card.breakupPreserveAlpha=true;
    card.breakupWeightOverEffect.m_Mode=FloatCurveMode::Curve;
    card.breakupWeightOverEffect.m_Keys={{0,0},{1,1}};
    card.sequences={{0,4},{4,4}};
    card.sizeOverEffect.m_Mode=FloatCurveMode::Curve;
    card.sizeOverEffect.m_Keys={{0,1},{1,.5f}};
    card.alphaOverEffect.m_Mode=FloatCurveMode::Curve;
    card.alphaOverEffect.m_Keys={{0,1},{1,0}};
    asset->m_Emitters.push_back(std::move(emitter));
    std::string error; VansParticleAsset decoded;
    if (!check(DecodeParticleAsset(EncodeParticleAsset(*asset),{},decoded,error),error.c_str())) return false;
    card.output=VansParticleCardOutput::BlendAdd;
    if (!check(DecodeParticleAsset(EncodeParticleAsset(*asset),{},decoded,error)
        && decoded.m_Emitters[0]->m_RendererConfig.m_Card.output==VansParticleCardOutput::BlendAdd,
        "Source BlendAdd output did not survive the typed asset codec")) return false;
    if (!check(decoded.m_Emitters[0]->m_EmissionChannel==1 && decoded.TextureDependencies().size()==3
        && decoded.m_Emitters[0]->m_RendererConfig.m_Card.breakup==VansParticleCardBreakup::Zoom
        && decoded.m_Emitters[0]->m_RendererConfig.m_Card.breakupPreserveAlpha
        && decoded.m_Emitters[0]->m_RendererConfig.m_Card.lookupMultiply
        && decoded.m_Emitters[0]->m_RendererConfig.m_Card.tintBeforeDesaturation,
        "Card channel or typed layer dependency was lost")) return false;
    auto invalid=EncodeParticleAsset(*asset);
    invalid["emitters"][0]["renderer"]["spriteCard"]["breakupUseAccumulatedAlpha"]=true;
    if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Retired card alpha key was accepted")) return false;
    invalid=EncodeParticleAsset(*asset);
    invalid["emitters"][0]["renderer"]["spriteCard"]["sequences"][0]["frameCount"]=100;
    if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Out-of-bounds sequence was accepted")) return false;
    invalid=EncodeParticleAsset(*asset);
    invalid["emitters"][0]["renderer"]["spriteCard"]["softIntersection"]=-1;
    if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Invalid card material was accepted")) return false;
    invalid=EncodeParticleAsset(*asset);
    invalid["emitters"][0]["renderer"]["spriteCard"]["breakupTextureGuid"]="";
    if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Missing breakup dependency was accepted")) return false;
    invalid=EncodeParticleAsset(*asset);
    invalid["emitters"][0]["renderer"]["spriteCard"]["breakupWeightOverEffect"]["keys"][1]["value"]=2;
    if (!check(!DecodeParticleAsset(invalid,{},decoded,error),"Unbounded breakup weight was accepted")) return false;
    VansParticleManager manager; const auto handle=manager.Create(asset);
    VansParticleEmissionPoint first; first.id=7; first.channel=1; first.position={2,0,0};
    VansParticleEmissionPoint filtered=first; filtered.id=8; filtered.channel=2;
    if (!check(manager.SetEmissionPoints(handle,{first,filtered}) && manager.Queue(handle,VansParticleControl::Play),
        "Snapshot initialization failed")) return false;
    manager.TickMainThread(.5f); manager.WaitForUpdateAndSwap();
    if (!check(manager.Resolve(handle)->AliveInstanceCount()==1,"Point filtering/emission count is incorrect")) return false;
    auto later=first; later.id=9; later.position={4,0,0}; later.bornTime=.75f;
    if (!check(manager.SetEmissionPoints(handle,{later,filtered,first}),"Snapshot update failed")) return false;
    manager.TickMainThread(.5f); manager.WaitForUpdateAndSwap();
    if (!check(manager.Resolve(handle)->AliveInstanceCount()==2,"Existing point clock was reset or future point emitted early")) return false;
    manager.TickMainThread(.25f); manager.WaitForUpdateAndSwap();
    const auto* runtime=manager.Resolve(handle);
    if (!check(runtime->AliveInstanceCount()==3 && !runtime->CanSeek(),"Per-point birth time or replay guard is incorrect")) return false;
    const auto& instances=runtime->GetRenderBuffer();
    bool laterFound=false;
    for (const auto& instance : instances) {
        laterFound|=instance.m_WorldPosition.x==4;
        if (!check(instance.m_WorldPosition.x==2 || instance.m_WorldPosition.x==4,"Snapshot leaked a legacy origin spawn")) return false;
        const auto parameters=glm::unpackHalf2x16(instance.m_Padding.y);
        if (!check(glm::unpackSnorm2x16(instance.m_Padding.x)==glm::vec2(0,1)
            && std::abs(parameters.x-.625f)<.001f && parameters.y>=0 && parameters.y<=.125f
            && instance.m_FrameIndex>=0 && instance.m_FrameIndex<=7
            && std::abs(instance.m_Size-.6875f)<.001f && std::abs(instance.m_Color.a-.375f)<.001f,
            "Surface normal, sequence or collection-age curves are incorrect")) return false;
    }
    if (!check(laterFound,"Late cell was not emitted at its world position")) return false;
    if (!check(!manager.SetEmissionPoints(handle,{first,first}),"Duplicate point IDs were accepted")) return false;
    std::vector<VansParticleEmissionPoint> oversized(VansParticleMaxEmissionPoints+1,first);
    if (!check(!manager.SetEmissionPoints(handle,oversized),"Point budget was not bounded")) return false;
    if (!check(manager.SetEmissionPoints(handle,{}),"Empty snapshot was rejected")) return false;
    manager.TickMainThread(.5f); manager.WaitForUpdateAndSwap();
    if (!check(manager.Resolve(handle)->AliveInstanceCount()==3,"Empty snapshot fell back to default source emission")) return false;
    manager.TickMainThread(.5f); manager.WaitForUpdateAndSwap();
    if (!check(manager.Resolve(handle)->IsFinished(),"Finite snapshot effect did not drain")) return false;
    manager.Destroy(handle);
    if (!check(manager.PointCapacity()==0 && manager.ActiveCount()==0,"Snapshot effect retained its particle capacity")) return false;
    std::cout<<"[ParticleCard] typedDependencies=1 cardValidation=1 stablePointClocks=1 channels=1 lateBirth=1 surfaceNormal=1 globalDecay=1 emptySnapshot=1 boundedInput=1 reclaim=1\n";
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
    return TestParticleAlphaFade() && TestParticleSourceRandomInit() && TestParticleCollectionAlphaClock() && TestParticleInitializationAge() && TestDustV3SmokeEnvelope() && TestSurfaceEmissionPoints() && TestRibbonSimulation() && TestPolylineGeometry() && TestVFXServiceLifetime() && TestParticleAuthoring() && TestParticleDebugSnapshot();
}
