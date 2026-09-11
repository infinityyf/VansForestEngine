#include "../EngineCore/RenderCore/GICore/VansGIProbeWorkScheduler.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/EngineAPILayer/Private/ScenePropertyValueBuilders.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/SceneCore/VansSceneSaveService.h"
#include "../EngineCore/SceneCore/VansSceneRenderSettingsConfigReader.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <array>
#include <set>
#include <stdexcept>

namespace
{
    using namespace VansGraphics;
    void Check(bool passed, const char* message) { if (!passed) throw std::runtime_error(message); }


    void CompleteBudgetWork()
    {
        std::vector<GIProbeWorkRegion> regions(3);
        regions[0].raysPerProbe = 256; regions[1].raysPerProbe = 1024; regions[2].raysPerProbe = 33;
        for (auto& region : regions) for (uint32_t i = 0; i < 97; ++i) region.probeIndices.push_back(i * 3u);
        VansGIProbeWorkScheduler scheduler; std::string error;
        Check(scheduler.Configure(regions, 9, 1536, error), "mixed complete probe configuration failed");
        std::array<std::map<uint32_t,uint32_t>,3> visits;
        for (uint32_t frame = 0; frame < 1200; ++frame)
        {
            uint64_t rays = 0, updates = 0;
            const auto& batch = scheduler.NextFrame(.01);
            for (size_t r = 0; r < batch.size(); ++r)
            {
                rays += batch[r].RayCount(); updates += batch[r].entries.size();
                Check(batch[r].raysPerProbeUpdate == regions[r].raysPerProbe, "probe update omitted directions");
                Check(batch[r].RayCount() <= scheduler.RegionRayCapacity(r) &&
                    batch[r].entries.size() <= scheduler.RegionCapacity(r), "allocation capacity exceeded");
                std::set<uint32_t> ids;
                for (const auto& entry : batch[r].entries)
                {
                    Check(ids.insert(entry.probeIndex).second, "duplicate update in one frame");
                    auto& ordinal = visits[r][entry.probeIndex];
                    Check(entry.cycleIndex == ordinal && entry.flags == (ordinal == 0u ? GIWorkResetLighting : 0u),
                        "budget delay changed full-sphere rotation sequence");
                    Check(entry.elapsedSeconds > 0.0f && std::isfinite(entry.elapsedSeconds), "invalid temporal duration");
                    ++ordinal;
                    Check(scheduler.ApplyFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),
                        "complete lighting feedback rejected");
                    Check(!scheduler.ApplyFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),
                        "duplicate feedback accepted");
                }
            }
            Check(rays <= 1536 && updates <= 9, "shared frame budget exceeded");
        }
        for (const auto& region : visits) Check(region.size() == 97, "region or offscreen probe starved");
        Check(!scheduler.Configure({GIProbeWorkRegion{256,{4,4}}}, 9,512,error), "duplicate addresses accepted");
        Check(!scheduler.Configure({GIProbeWorkRegion{256,{4}}}, 9,255,error), "partial probe budget accepted");
        Check(scheduler.Configure({GIProbeWorkRegion{256,{4}}}, 9,256,error), "exact probe budget rejected");
        auto old = scheduler.NextFrame()[0].entries[0];
        scheduler.ResetLighting(); auto fresh = scheduler.NextFrame()[0].entries[0];
        Check(fresh.flags == GIWorkResetLighting && fresh.cycleIndex != old.cycleIndex, "reset reused stale sequence");
        Check(!scheduler.ApplyFeedback(0, old, {4,1,old.elapsedSeconds,old.cycleIndex}), "pre-reset feedback accepted");
    }

    void AllPlacedCoverage()
    {
        // 小预算、整除/非整除数量、队尾回绕及持续静态光照都必须反复覆盖所有地址。
        for (uint32_t budget : {64u, 256u, 2048u})
        for (uint32_t count : {1u, 127u, 128u})
        {
            GIProbeWorkRegion region; region.raysPerProbe = 64;
            for (uint32_t i = 0; i < count; ++i) region.probeIndices.push_back(i * 7u);
            VansGIProbeWorkScheduler scheduler; std::string error;
            Check(scheduler.Configure({region}, 32, budget, error), "all placed setup failed");
            const uint32_t perFrame = (std::min)({count, 32u, budget / 64u});
            const uint32_t cycleFrames = (count + perFrame - 1u) / perFrame;
            for (uint32_t frame = 0; frame < cycleFrames * 3u; ++frame)
            {
                const auto& batch = scheduler.NextFrame()[0];
                Check(batch.RayCount() == perFrame * 64u && batch.entries.size() == perFrame, "complete work lost budget or directions");
                std::set<uint32_t> ids;
                for (const auto& entry : batch.entries)
                {
                    Check(ids.insert(entry.probeIndex).second, "wrapped queue updated same probe twice in a frame");
                    Check(scheduler.ApplyFeedback(0, entry, {entry.probeIndex,1u,entry.elapsedSeconds,entry.cycleIndex}),
                        "completion feedback rejected");
                }
            }
            const auto coverage = scheduler.RegionCoverage(0);
            Check(coverage.placed == count && coverage.updated == count && coverage.minCompletedUpdates >= 3u,
                "placed probe did not complete three updates under static lighting");
        }

        // 旧策略会让 1024-ray 区域永远错过只剩 768 rays 的帧尾，下一帧又先处理廉价区域。
        VansGIProbeWorkScheduler mixed; std::string error;
        Check(mixed.Configure({{256,{0,1}}, {1024,{4,8}}}, 2, 1024, error), "mixed cost setup failed");
        for (uint32_t frame = 0; frame < 12; ++frame)
        {
            const auto& work = mixed.NextFrame();
            uint64_t rays = 0;
            for (size_t r = 0; r < work.size(); ++r)
            {
                rays += work[r].RayCount();
                for (const auto& entry : work[r].entries)
                    Check(mixed.ApplyFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}), "mixed feedback rejected");
            }
            Check(rays <= 1024, "fairness exceeded the ray budget");
        }
        for (size_t r = 0; r < 2; ++r)
            Check(mixed.RegionCoverage(r).minCompletedUpdates >= 3, "mixed-cost region starved");

        // DustV3 的实际容量/预算：每帧 256 个完整 probe，256 帧覆盖全部 65536 个。
        // 每帧重置光照也不能重置游标；取消提交不算完成，仍必须继续轮到所有地址。
        for (bool resetEveryFrame : {false, true})
        {
            GIProbeWorkRegion region; region.raysPerProbe = 256;
            for (uint32_t i = 0; i < 65536; ++i) region.probeIndices.push_back(i);
            VansGIProbeWorkScheduler scheduler;
            Check(scheduler.Configure({region}, 4096, 65536, error), "DustV3 budget setup failed");
            for (uint32_t frame = 0; frame < 768; ++frame)
            {
                if (resetEveryFrame) scheduler.ResetLighting();
                const auto& batch = scheduler.NextFrame()[0];
                Check(batch.entries.size() == 256 && batch.RayCount() == 65536, "DustV3 frame budget changed");
                if (frame == 0)
                    Check(scheduler.RegionCoverage(0).updated == 0, "issued work counted as GPU completion");
                for (const auto& entry : batch.entries)
                {
                    Check(entry.flags == (resetEveryFrame || frame < 256 ? GIWorkResetLighting : 0u), "lighting reset lost its pending work");
                    Check(scheduler.ApplyFeedback(0, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}), "DustV3 completion rejected");
                }
                if ((frame + 1u) % 256u == 0u)
                {
                    const auto coverage = scheduler.RegionCoverage(0);
                    Check(coverage.updated == 65536 && coverage.minCompletedUpdates == (frame + 1u) / 256u &&
                        coverage.maxCompletedUpdates == coverage.minCompletedUpdates, "DustV3 all-placed coverage interval exceeded");
                }
            }
        }
        VansGIProbeWorkScheduler retry;
        Check(retry.Configure({{256,{}}, {256,{4}}},1,256,error), "empty region setup failed");
        const auto dropped = retry.NextFrame()[1].entries[0];
        const auto fresh = retry.NextFrame()[1].entries[0];
        Check(fresh.flags == GIWorkResetLighting && fresh.cycleIndex != dropped.cycleIndex, "lost completion suppressed retry");
        Check(!retry.ApplyFeedback(1,dropped,{4,1,dropped.elapsedSeconds,dropped.cycleIndex}), "stale completion accepted");
        Check(!retry.ApplyFeedback(1,fresh,{4,0,fresh.elapsedSeconds,fresh.cycleIndex}), "unfinished GPU work counted as complete");
        Check(retry.ApplyFeedback(1,fresh,{4,1,fresh.elapsedSeconds,fresh.cycleIndex}), "retry completion rejected");
        Check(retry.RegionCoverage(1).minCompletedUpdates == 1, "dropped work inflated coverage");
        std::cout << "[GIProbeWork] all placed repeated coverage, mixed-cost fairness, DustV3 65536 probes / 256-frame sweep, reset and completion retry PASS\n";
    }

    void ConfigurationOwnership(const Vans::VansSerializedValue& environment)
    {
        // 通过生产 API、文档编辑/保存与加载器验证，不复制一套设置投影。
        auto scene = std::make_unique<VansScene>();
        Vans::EditorAPI::EngineAPIImpl api(scene.get(), nullptr);
        struct Detach { Vans::EditorAPI::EngineAPIImpl& api; ~Detach() { api.BindRuntime(nullptr, nullptr); } } detach{api};
        VansGISettings initial;
        initial.regions.resize(2);
        initial.regions[0].size = {13.2f, 8.1f, 6.7f}; initial.regions[0].probeSpacing = 1.25f;
        initial.regions[0].center = {-7,-11,3}; initial.regions[0].overrideGridDimensions = false;
        initial.regions[1].stableId = 8; initial.regions[1].name = "Authored explicit grid";
        initial.regions[1].overrideGridDimensions = true; initial.regions[1].gridDimensions = {5,7,3};
        initial.regions[1].probeSpacing = 2.5f; initial.regions[1].enabled = false;
        scene->SetGISettings(initial);
        scene->ClearGIProbeResourcesDirty();
        api.SetGIProbeVisualization(true, true, 3);
        Check(scene->GetGISettings().showProbeGizmos && scene->GetGISettings().showProbeVolume &&
            scene->GetGISettings().gizmoStride == 3 && !scene->AreGIProbeResourcesDirty() &&
            GISettingsResourceLayoutEquals(initial, scene->GetGISettings()),
            "position display changed GI layout or requested resource recreation");
        Check(api.ConsumeScenePropertyEdits().empty() && !api.GetGIProbeDebugSnapshot(),
            "display controls staged a scene write or exposed positions before resources were ready");
        api.SetGIProbeVisualization(false, false, 0);
        Check(!scene->GetGISettings().showProbeGizmos && !scene->GetGISettings().showProbeVolume &&
            scene->GetGISettings().gizmoStride == 1 && !scene->AreGIProbeResourcesDirty(),
            "position display toggle or stride normalization failed");
        api.SaveReflectionProbeConfiguration();
        const auto reflectionEdits = api.ConsumeScenePropertyEdits();
        Check(reflectionEdits.size() == 1 && reflectionEdits[0].propertyPointer == "/settings/reflectionProbes",
            "reflection configuration saved outside the scene settings block");
        const auto reflectionBefore = Vans::EditorAPI::ScenePropertyValues::ToSerializedValue(reflectionEdits[0].value);

        const auto directory = std::filesystem::temp_directory_path() /
            ("ForestGIConfig_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        struct Cleanup
        {
            std::filesystem::path directory;
            ~Cleanup() { std::error_code error; std::filesystem::remove(directory / "Scene.json", error); std::filesystem::remove(directory, error); }
        } cleanup{directory};
        const auto path = directory / "Scene.json";
        auto fixture = nlohmann::json::parse(R"({"schemaVersion":2,"sceneGuid":"677b6e10-e731-4a26-b3a7-f15a42cd972b","name":"GI config contract","settings":{"reflectionProbes":{"placement":{"enabled":false},"probes":[]}},"entities":[]})");
        fixture["settings"]["environment"] = Vans::EncodeSerializedValueJson<nlohmann::json>(environment);
        { std::ofstream output(path); output << fixture.dump(); }
        auto document = Vans::VansSceneDocumentLoader::Load(path);
        Check(bool(document), "GI save fixture failed to load");
        Vans::VansSceneEditService edits(*document.document);
        for (bool enabled : {false, true, false})
        {
            const auto fingerprint = Vans::VansSceneDocumentLoader::Fingerprint(path);
            auto settings = api.GetGISettings();
            settings.placement.enabled = enabled;
            settings.placement.minProbeSpacing = 0.125f;
            settings.placement.maxProbeSpacing = 3.25f;
            settings.placement.parentProbeMaxSize = 2.75f;
            settings.placement.maxProbeCount = 90001;
            settings.placement.maxProbeUpdatesPerFrame = 777;
            settings.placement.maxRaysPerFrame = 23456;
            Check(api.ApplyGISettings(settings), "GI authoring settings Apply failed");
            Check(api.ConsumeScenePropertyEdits().empty(), "Apply GI implicitly staged a scene write");
            const auto& applied = scene->GetGISettings();
            Check(applied.placement.enabled == enabled && applied.placement.minProbeSpacing == 0.125f
                && applied.placement.maxProbeSpacing == 3.25f && applied.placement.parentProbeMaxSize == 2.75f && applied.placement.maxProbeCount == 90001
                && applied.placement.maxProbeUpdatesPerFrame == 777 && applied.placement.maxRaysPerFrame == 23456,
                "GI placement controls did not reach native settings");
            Check(!applied.regions[0].overrideGridDimensions && applied.regions[0].size == initial.regions[0].size
                && applied.regions[0].center == initial.regions[0].center && applied.regions[1].overrideGridDimensions
                && applied.regions[1].gridDimensions == initial.regions[1].gridDimensions && !applied.regions[1].enabled,
                "GI placement toggle modified the authored region definition");
            api.SaveGIConfiguration();
            const auto saved = api.ConsumeScenePropertyEdits();
            Check(saved.size() == 1 && saved[0].propertyPointer == "/settings/globalIllumination", "GI configuration save path is not loader-visible");
            Check(bool(edits.Set(Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, saved[0].propertyPointer),
                Vans::EditorAPI::ScenePropertyValues::ToSerializedValue(saved[0].value))), "GI configuration could not enter the scene document");
            Check(Vans::VansSceneDocumentLoader::Fingerprint(path) == fingerprint, "Apply or document staging wrote the file before Save Scene");
            Check(bool(Vans::VansSceneSaveService().Save(*document.document)), "explicit GI scene save failed");
            auto reloaded = Vans::VansSceneDocumentLoader::Load(path);
            Check(bool(reloaded), "saved GI document failed to reload");
            const auto json = Vans::EncodeSerializedValueJson<nlohmann::json>(reloaded.document->SerializedRootSnapshot());
            Check(!json.contains("globalIllumination") && !json.contains("reflectionProbes")
                && json["settings"]["reflectionProbes"]["placement"]["enabled"] == false, "GI save changed reflection settings or wrote a parallel root block");
            Vans::VansSceneRenderSettingsConfig config; std::string error;
            if (!Vans::VansSceneRenderSettingsConfigReader::Read(Vans::DecodeSerializedValueJson(json["settings"]), config, error))
                throw std::runtime_error("GI saved settings failed production decode: " + error);
            Check(config.globalIllumination.has_value(), "GI saved settings missing after production decode");
            const auto& decoded = *config.globalIllumination;
            Check(decoded.placement.enabled == enabled && decoded.placement.minProbeSpacing == 0.125f
                && decoded.placement.maxProbeSpacing == 3.25f && decoded.placement.parentProbeMaxSize == 2.75f && decoded.placement.maxProbeCount == 90001
                && decoded.placement.maxProbeUpdatesPerFrame == 777 && decoded.placement.maxRaysPerFrame == 23456,
                "GI placement lost values during explicit save/reload");
            Check(decoded.regions.size() == 2 && decoded.regions[0].size.has_value() && !decoded.regions[0].gridDimensions.has_value()
                && decoded.regions[1].gridDimensions.has_value() && !decoded.regions[1].size.has_value()
                && decoded.regions[1].enabled == false, "saved derived grid replaced authored region mode");
            api.SaveReflectionProbeConfiguration();
            const auto reflectionAfter = api.ConsumeScenePropertyEdits();
            Check(reflectionAfter.size() == 1 && Vans::EncodeSerializedValueJson<nlohmann::json>(reflectionBefore)
                == Vans::EncodeSerializedValueJson<nlohmann::json>(Vans::EditorAPI::ScenePropertyValues::ToSerializedValue(reflectionAfter[0].value)),
                "GI configuration changed reflection ownership");
        }
    }
}

bool TestGIProbeWorkContract(const Vans::VansSerializedValue& environment)
{
    try
    {
        CompleteBudgetWork(); AllPlacedCoverage(); ConfigurationOwnership(environment);
        std::cout << "[GIProbeWork] PASS: complete probe sequence, cost-aware budgets, native API controls, independent ownership and explicit save/reload\n";
        return true;
    }
    catch (const std::exception& error) { std::cerr << "[GIProbeWork] FAIL: " << error.what() << '\n'; return false; }
}
