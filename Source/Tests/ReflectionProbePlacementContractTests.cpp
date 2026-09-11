#include <set>
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbePlacement.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeLayout.h"
#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeSystem.h"
#include "../EngineCore/SceneCore/VansSceneReflectionProbeConfigReader.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <chrono>
#include <random>

// 读取运行时几何快照的本机测试夹具；不参与项目配置、资源加载或发布。
bool MeasureReflectionProbePlacement(const char* geometryPath, const char* scenePath, const char* outputPath)
{
    using namespace VansGraphics;
    try
    {
        std::ifstream input(geometryPath, std::ios::binary);
        uint32_t header[4]{}; input.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!input || header[0] != sizeof(VansGeometryTriangle) || header[1] > 20000000u
            || header[2] > 2000000u || header[3] > 1000000u) throw std::runtime_error("Invalid geometry fixture");
        VansSceneGeometrySnapshot geometry;
        std::vector<VansGeometryTriangle> triangles(header[1]);
        geometry.transmissionReceivers.resize(header[2]); geometry.dynamicReceivers.resize(header[3]);
        const auto read = [&](auto& items) { input.read(reinterpret_cast<char*>(items.data()), items.size() * sizeof(items[0])); };
        read(triangles); read(geometry.transmissionReceivers); read(geometry.dynamicReceivers);
        if (!input) throw std::runtime_error("Truncated geometry fixture");
        geometry.opaque.Build(std::move(triangles));
        std::ifstream sceneFile(scenePath); nlohmann::json scene; sceneFile >> scene;
        VansReflectionProbeSystem system;
        system.LoadFromSceneConfig(Vans::VansSceneReflectionProbeConfigReader::Read(Vans::DecodeSerializedValueJson(scene.at("settings"))), scenePath);
        const auto settings = system.GetPlacementSettings();
        VansReflectionProbePlacementResult result; std::string error;
        const auto begin = std::chrono::steady_clock::now();
        if (!VansReflectionProbePlacement::Generate(geometry, settings, system.GetPlacementOverrides(), result, error)) throw std::runtime_error(error);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        nlohmann::json report{{"seconds", seconds}, {"triangles", header[1]}, {"receivers", result.receiverCount},
            {"coveredReceivers", result.coveredReceiverCount}, {"surfaceCoverage", result.coveredSurfaceFraction},
            {"count", result.probes.size()}, {"candidates", result.candidateCount},
            {"reachableSurfaceCoverage",result.reachableSurfaceFraction}, {"budgetExhausted", result.budgetExhausted}};
        const auto vec = [](glm::vec3 p) { return nlohmann::json::array({p.x, p.y, p.z}); };
        report["probes"] = nlohmann::json::array();
        for (const auto& p : result.probes) report["probes"].push_back({{"position", vec(p.capturePosition)},
            {"minimum", vec(p.boxMin)}, {"maximum", vec(p.boxMax)}, {"blend", p.blendDistance}});
        // 独立的面积加权随机表面采样，避免用求解器自己的代表点验收自己。
        std::vector<double> areas; double area = 0;
        for (const auto& t : geometry.opaque.GetTriangles())
        {
            glm::vec3 centroid; float clipped;
            if (VansTriangleGeometryQuery::ClipSurfaceToBox(t, settings.volumeMin, settings.volumeMax, centroid, clipped))
                area += clipped * (t.twoSided ? 2.0 : 1.0);
            areas.push_back(area);
        }
        std::mt19937 random(90710); std::uniform_real_distribution<double> uniform(0, 1);
        uint32_t samples = 0, visible = 0, inside = 0, influenced = 0;
        for (uint32_t attempt = 0; samples < 50000 && attempt < 500000 && area > 0; ++attempt)
        {
            size_t index = std::upper_bound(areas.begin(), areas.end(), uniform(random) * area) - areas.begin();
            const auto& t = geometry.opaque.GetTriangles().at(index);
            const float u = std::sqrt(float(uniform(random))), v = float(uniform(random));
            const glm::vec3 surface = (1-u)*t.a + (u*(1-v))*t.b + (u*v)*t.c;
            if (glm::any(glm::lessThan(surface, settings.volumeMin)) || glm::any(glm::greaterThan(surface, settings.volumeMax))) continue;
            ++samples;
            glm::vec3 normal = t.normal;
            if (t.twoSided && uniform(random) < 0.5) normal = -normal;
            bool anyVisible = false, anyInside = false, anyInfluence = false;
            for (const auto& probe : result.probes)
            {
                const float distance = glm::length(glm::max(glm::max(probe.boxMin - surface, surface - probe.boxMax), glm::vec3(0)));
                if (distance >= probe.blendDistance) continue;
                anyInfluence = true;
                if (glm::dot(probe.capturePosition-surface, normal) < -0.0001f) continue;
                const glm::vec3 origin = surface + normal * 0.005f, ray = probe.capturePosition - origin;
                VansGeometryHit hit;
                if (geometry.opaque.Raycast(origin, ray, glm::length(ray)-0.005f, hit, 0.0001f)) continue;
                anyVisible = true; anyInside |= distance <= 0.02f;
            }
            visible += anyVisible; inside += anyInside; influenced += anyInfluence;
        }
        report["independent"] = {{"samples",samples},{"visibleCoverage",samples ? double(visible)/samples : 1},
            {"interiorCoverage",samples ? double(inside)/samples : 1},{"influenceCoverage",samples ? double(influenced)/samples : 1}};
        std::ofstream output(outputPath); output << report.dump(2);
        report.erase("probes"); std::cout << report.dump(2) << '\n';
        return bool(output);
    }
    catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return false; }
}

namespace
{
    using namespace VansGraphics;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void Quad(std::vector<VansGeometryTriangle>& triangles, glm::vec3 origin, glm::vec3 u, glm::vec3 v,
        glm::vec3 normal, bool twoSided = false)
    {
        VansGeometryTriangle a, b;
        a.a = origin; a.b = origin + u; a.c = origin + u + v; a.normal = normal; a.twoSided = twoSided;
        b.a = origin; b.b = origin + u + v; b.c = origin + v; b.normal = normal; b.twoSided = twoSided;
        triangles.push_back(a); triangles.push_back(b);
    }
    void Box(std::vector<VansGeometryTriangle>& triangles, glm::vec3 lo, glm::vec3 hi, bool room)
    {
        const glm::vec3 size = hi - lo;
        const float sign = room ? 1.0f : -1.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const int a = (axis + 1) % 3, b = (axis + 2) % 3;
            glm::vec3 u(0), v(0), n(0); u[a] = size[a]; v[b] = size[b]; n[axis] = sign;
            Quad(triangles, lo, u, v, n);
            glm::vec3 opposite = lo; opposite[axis] = hi[axis];
            Quad(triangles, opposite, u, v, -n);
        }
    }
    ReflectionProbePlacementSettings Settings(glm::vec3 lo, glm::vec3 hi)
    {
        ReflectionProbePlacementSettings settings;
        settings.enabled = true; settings.volumeMin = lo; settings.volumeMax = hi;
        settings.cellSize = 1; settings.minCaptureClearance = 0.2f;
        settings.indoorSpacing = settings.corridorSpacing = settings.outdoorSpacing = 8;
        settings.refinementThreshold = 0;
        settings.maxProbeCount = 128;
        return settings;
    }
    VansReflectionProbePlacementResult Generate(const VansSceneGeometrySnapshot& geometry,
        const ReflectionProbePlacementSettings& settings, const std::vector<VansReflectionProbeDesc>& overrides = {})
    {
        VansReflectionProbePlacementResult result;
        std::string error;
        Check(VansReflectionProbePlacement::Generate(geometry, settings, overrides, result, error), error.c_str());
        Check(error.empty(), "successful placement left an error");
        return result;
    }
    // 独立表面验收：在原三角形上重新采样，不使用求解器的表面单元和覆盖统计。
    bool Contributes(const VansSceneGeometrySnapshot& geometry, const VansReflectionProbeDesc& probe,
        glm::vec3 surface, glm::vec3 normal)
    {
        if (!probe.enabled || glm::dot(probe.capturePosition - surface, normal) < -0.0001f) return false;
        if (glm::any(glm::lessThan(surface, probe.boxMin)) || glm::any(glm::greaterThan(surface, probe.boxMax))) return false;
        const glm::vec3 origin = surface + normal * 0.003f;
        const glm::vec3 ray = probe.capturePosition - origin;
        VansGeometryHit hit;
        return !geometry.opaque.Raycast(origin, ray, glm::length(ray) - 0.003f, hit, 0.0001f);
    }
    void VerifyUseful(const VansSceneGeometrySnapshot& geometry, const VansReflectionProbePlacementResult& result,
        const ReflectionProbePlacementSettings& settings)
    {
        for (const auto& probe : result.probes)
        {
            VansGeometryHit hit;
            Check(!geometry.opaque.NearestSurface(probe.capturePosition, settings.minCaptureClearance - 0.0001f, hit),
                "capture intersects geometry or violates clearance");
            bool contributes = false;
            for (const auto& triangle : geometry.opaque.GetTriangles())
            {
                for (int u = 1; u < 12 && !contributes; ++u) for (int v = 1; u + v < 12 && !contributes; ++v)
                {
                    const glm::vec3 p = triangle.a + (triangle.b - triangle.a) * (float(u) / 12)
                        + (triangle.c - triangle.a) * (float(v) / 12);
                    contributes = Contributes(geometry, probe, p, triangle.normal)
                        || (triangle.twoSided && Contributes(geometry, probe, p, -triangle.normal));
                }
                if (contributes) break;
            }
            Check(contributes, "generated capture has no independently verified visible receiving surface");
        }
    }
    void VerifySame(const std::vector<VansReflectionProbeDesc>& a, const std::vector<VansReflectionProbeDesc>& b)
    {
        Check(a.size() == b.size(), "layout count changed");
        for (size_t i = 0; i < a.size(); ++i)
            Check(a[i].name == b[i].name && a[i].cachePath == b[i].cachePath && a[i].enabled == b[i].enabled
                && a[i].autoGenerated == b[i].autoGenerated && a[i].shape == b[i].shape && a[i].type == b[i].type
                && a[i].position == b[i].position && a[i].capturePosition == b[i].capturePosition
                && a[i].boxMin == b[i].boxMin && a[i].boxMax == b[i].boxMax
                && a[i].resolution == b[i].resolution && a[i].intensity == b[i].intensity
                && a[i].priority == b[i].priority && a[i].boxProjection == b[i].boxProjection
                && a[i].refreshMode == b[i].refreshMode && a[i].specularIntensity == b[i].specularIntensity
                && a[i].nearPlane == b[i].nearPlane && a[i].farPlane == b[i].farPlane
                && a[i].radius == b[i].radius && a[i].blendDistance == b[i].blendDistance
                && a[i].cullingMask == b[i].cullingMask && a[i].regionId == b[i].regionId
                && a[i].realtimeFacesPerFrame == b[i].realtimeFacesPerFrame && a[i].portal == b[i].portal,
                "layout identity, geometry, cache or lighting changed");
    }
    void TestLayoutOwnership(const std::vector<VansReflectionProbeDesc>& generated)
    {
        Check(!generated.empty(), "ownership test needs generated probes");
        VansReflectionProbeDesc manual, sky;
        manual.name = "Authored underground room"; manual.position = {-3, -12, 2};
        manual.capturePosition = {-2, -11, 3}; manual.cachePath = "Saved/authored.exr";
        manual.intensity = 1.7f; manual.enabled = false;
        sky.name = "Sky"; sky.type = ReflectionProbeType::Sky;
        VansReflectionProbeLayout layout;
        const std::vector<VansReflectionProbeDesc> authored{manual, sky};
        layout.Reset(authored, {}); layout.SetGenerated(generated);
        std::vector<VansReflectionProbeDesc> active;
        std::vector<VansReflectionProbeOrigin> origins;
        layout.BuildActive(false, active, origins); VerifySame(active, authored);
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            layout.BuildActive(true, active, origins);
            Check(active.size() == generated.size() + 1 && active.back().type == ReflectionProbeType::Sky,
                "automatic layout did not replace local captures and retain sky");
            auto edit = active[0]; edit.intensity = 9;
            Check(!layout.Edit(origins[0], edit), "derived capture edited without explicit pin");
            layout.BuildActive(false, active, origins); VerifySame(active, authored); VerifySame(layout.Authored(), authored);
        }
        layout.BuildActive(true, active, origins);
        auto pinned = active[0]; pinned.intensity = 2.5f;
        auto origin = layout.Pin(origins[0], pinned);
        layout.UpdateCachePath(origin, "Saved/pinned.exr");
        layout.BuildActive(true, active, origins);
        Check(active.size() == generated.size() + 1 && active[0].name == pinned.name && !active[0].autoGenerated
            && active[0].intensity == 2.5f && active[0].cachePath == "Saved/pinned.exr", "pin duplicated or lost generated capture");
        layout.BuildActive(false, active, origins); VerifySame(active, authored);
        VansReflectionProbeLayout reload;
        reload.Reset(layout.Authored(), layout.Overrides()); reload.SetGenerated(generated);
        reload.BuildActive(true, active, origins);
        Check(active.front().intensity == 2.5f && active.front().cachePath == "Saved/pinned.exr", "saved overrides lost after reload");
        reload.BuildActive(false, active, origins); VerifySame(active, authored);
    }
    void TestConfigProjection()
    {
        nlohmann::json settings;
        settings["globalIllumination"]["placement"] = {{"enabled", true}, {"minProbeSpacing", 0.125}};
        auto& reflection = settings["reflectionProbes"];
        reflection["placement"] = {{"enabled", false}, {"minCaptureClearance", 0.125}, {"maxProbeCount", 1024}};
        reflection["placement"]["overrides"] = nlohmann::json::array({
            {{"name", "Pinned"}, {"autoGenerated", false}, {"intensity", 2.5}, {"cachePath", "Cache/Pinned"}}});
        reflection["probes"] = nlohmann::json::array({
            {{"name", "Authored"}, {"position", {-3.0, -12.0, 2.0}}, {"enabled", false}}});
        for (bool enabled : {false, true, false})
        {
            settings["reflectionProbes"]["placement"]["enabled"] = enabled;
            auto config = Vans::VansSceneReflectionProbeConfigReader::Read(Vans::DecodeSerializedValueJson(settings));
            VansReflectionProbeSystem system;
            system.LoadFromSceneConfig(config, "Memory/Scene.json");
            Check(system.GetPlacementSettings().enabled == enabled && system.GetPlacementSettings().minCaptureClearance == 0.125f
                && system.GetPlacementSettings().maxProbeCount == 1024, "reflection settings did not reach runtime");
            Check(system.GetAuthoredProbes().size() == 2 && system.GetAuthoredProbes()[0].name == "Authored"
                && !system.GetAuthoredProbes()[0].enabled, "loading placement setting overwrote saved layout");
            Check(system.GetPlacementOverrides().size() == 1 && system.GetPlacementOverrides()[0].name == "Pinned"
                && system.GetPlacementOverrides()[0].cachePath == "Cache/Pinned" && system.GetPlacementOverrides()[0].intensity == 2.5f,
                "pinned override did not reach independent saved table");
        }
        settings.erase("reflectionProbes");
        VansReflectionProbeSystem defaults;
        defaults.LoadFromSceneConfig(Vans::VansSceneReflectionProbeConfigReader::Read(Vans::DecodeSerializedValueJson(settings)), "Memory/Scene.json");
        Check(!defaults.GetPlacementSettings().enabled, "GI placement enabled reflection placement");
    }
    void TestPageLayout()
    {
        VansReflectionProbePageLayout layout;
        std::string error;
        for (uint32_t layers : {6u, 11u, 12u, 2048u})
        {
            const uint32_t capacity = layers / 6, count = capacity * 3 + 1;
            Check(layout.Build(std::vector<uint32_t>(count, 64), layers, error) && layout.Pages().size() == 4,
                "page capacity ignored six-face cube alignment");
            uint32_t total = 0;
            for (const auto& page : layout.Pages())
            { Check(page.cubeCount * 6 <= layers && page.resolution == 64 && page.mipCount == 7, "image limits or size changed"); total += page.cubeCount; }
            Check(total == count, "page allocation truncated logical probes");
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto address = layout.Address(i);
                Check(address.page < layout.Pages().size() && address.cube < layout.Pages()[address.page].cubeCount
                    && address.page * capacity + address.cube == i, "page boundary address collision or omission");
            }
        }
        Check(layout.Build(std::vector<uint32_t>(341u * ReflectionProbeMaxTexturePages, 64), 2048, error), "maximum legal page count rejected");
        const uint64_t previous = layout.ResidentBytes();
        Check(!layout.Build(std::vector<uint32_t>(341u * ReflectionProbeMaxTexturePages + 1, 64), 2048, error)
            && !error.empty() && layout.ResidentBytes() == previous && layout.Pages().size() == ReflectionProbeMaxTexturePages,
            "descriptor exhaustion did not fail atomically");
        Check(!layout.Build({64}, 5, error), "partial cubemap accepted");
        for (const auto& invalid : {std::vector<uint32_t>{64, 3}, {64, 513}, {64, UINT32_MAX}})
            Check(!layout.Build(invalid, 2048, error) && layout.ResidentBytes() == previous, "invalid resolution changed published pages");
        Check(layout.Build({}, 2048, error) && layout.ProbeCount() == 0 && layout.Pages().size() == 1
            && layout.Pages()[0].cubeCount == 1 && layout.ResidentBytes() == 48, "empty scene must bind a minimal dummy cube");
        bool rejected = false;
        try { layout.Address(0); } catch (const std::out_of_range&) { rejected = true; }
        Check(rejected, "nonexistent logical probe acquired a texture address");

        std::vector<uint32_t> resolutions(1025, 32);
        resolutions[0] = 0; resolutions[1] = 512;
        for (uint32_t i = 2; i < 25; ++i) resolutions[i] = 128;
        for (uint32_t i = 25; i < 125; ++i) resolutions[i] = 64;
        Check(layout.Build(resolutions, 2048, error), error.c_str());
        Check(layout.ProbeCount() == resolutions.size() && layout.CaptureResolution() == 512 && layout.CaptureMipCount() == 10
            && layout.Address(0).page == UINT32_MAX, "sky acquired storage or workspace sizing changed");
        std::set<std::pair<uint32_t, uint32_t>> addresses;
        uint64_t expectedBytes = 0, oldBytes = 0;
        for (uint32_t i = 1; i < resolutions.size(); ++i)
        {
            const auto address = layout.Address(i); const auto& page = layout.Pages().at(address.page);
            Check(page.resolution == resolutions[i] && address.cube < page.cubeCount
                && addresses.emplace(address.page, address.cube).second, "mixed page changed resolution or aliased probes");
            for (uint32_t size = resolutions[i]; size; size >>= 1u) expectedBytes += uint64_t(size) * size * 48u;
            for (uint32_t size = 512; size; size >>= 1u) oldBytes += uint64_t(size) * size * 48u;
        }
        Check(layout.ResidentBytes() == expectedBytes && expectedBytes < oldBytes / 100u, "mixed pages still allocate the global maximum size");
        VansReflectionProbePageLayout again;
        Check(again.Build(resolutions, 2048, error) && again.ResidentBytes() == expectedBytes, "deterministic page rebuild failed");
        for (uint32_t i = 0; i < resolutions.size(); ++i)
            Check(layout.Address(i).page == again.Address(i).page && layout.Address(i).cube == again.Address(i).cube, "mixed page rebuild changed addresses");
        std::cout << "[ReflectionPages] logical=" << layout.ProbeCount() << " local=1024 pages=" << layout.Pages().size()
            << " residentBytes=" << expectedBytes << " uniformMaxBytes=" << oldBytes << " captureWorkspaceCubes=1\n";
    }

}

bool TestReflectionProbePlacementContract()
{
    try
    {
        using namespace VansGraphics;
        Check(!ReflectionProbePlacementSettings{}.enabled, "automatic reflection placement must default off");
        TestConfigProjection();
        TestPageLayout();
        VansReflectionProbeSystem captureState;
        captureState.GetBakeResults().resize(2);
        captureState.MarkBakeComplete(0, true, "Published");
        const auto publishedRevision = captureState.GetBakeResults()[0].revision;
        captureState.MarkBakeComplete(0, false, "Capture failed");
        captureState.MarkBakeComplete(1, false, "First capture failed");
        Check(captureState.GetBakeResults()[0].valid && captureState.GetBakeResults()[0].dirty &&
            captureState.GetBakeResults()[0].revision == publishedRevision &&
            !captureState.GetBakeResults()[1].valid && captureState.GetBakeResults()[1].revision == 0,
            "failed capture discarded published lighting or advanced its revision");
        VansSceneGeometrySnapshot geometry;
        auto settings = Settings({-32, -32, -32}, {32, 32, 32});
        auto result = Generate(geometry, settings);
        Check(result.probes.empty() && result.receiverCount == 0 && !result.budgetExhausted, "empty space consumed capture budget");

        std::vector<VansGeometryTriangle> triangles;
        Quad(triangles, {-4, -12, -4}, {8, 0, 0}, {0, 0, 8}, {0, 1, 0});
        geometry.opaque.Build(triangles);
        result = Generate(geometry, settings);
        Check(!result.probes.empty() && result.coveredSurfaceFraction > 0.99f, "open floor not covered");
        for (const auto& probe : result.probes)
            Check(probe.capturePosition.y > -12 && probe.capturePosition.y < -8, "open floor created underground or distant empty-space capture");
        VerifyUseful(geometry, result, settings);
        VerifySame(result.probes, Generate(geometry, settings).probes);
        TestLayoutOwnership(result.probes);

        // 两间负高度封闭房间：负世界高度不能被当成实体地下空间。
        triangles.clear();
        Box(triangles, {-8, -14, -4}, {-0.05f, -10, 4}, true);
        Box(triangles, {0.05f, -14, -4}, {8, -10, 4}, true);
        geometry.opaque.Build(triangles);
        result = Generate(geometry, settings);
        Check(result.coveredSurfaceFraction > 0.99f, "closed rooms have uncovered receiving surfaces");
        bool left = false, right = false;
        for (const auto& probe : result.probes)
        {
            const auto p = probe.capturePosition;
            Check(p.y > -14 && p.y < -10 && p.z > -4 && p.z < 4 && std::abs(p.x) > 0.05f && std::abs(p.x) < 8,
                "capture escaped room interior or entered separating solid wall");
            left |= p.x < 0; right |= p.x > 0;
            Check(p.x < 0 ? probe.boxMax.x < 0 : probe.boxMin.x > 0, "room influence crossed axial separating wall");
        }
        Check(left && right, "one room silently lost all captures");
        Check(result.probes.size() <= 4, "simple adjacent rooms should not need a dense capture lattice");
        VerifyUseful(geometry, result, settings);
        const auto unlimited = result;
        settings.maxProbeCount = 1;
        result = Generate(geometry, settings);
        Check(result.probes.size() == 1 && result.budgetExhausted && result.coveredReceiverCount < result.receiverCount,
            "budget-limited placement hid incomplete coverage");
        settings.maxProbeCount = 128;
        result = Generate(geometry, settings, unlimited.probes);
        Check(result.probes.empty() && result.coveredSurfaceFraction > 0.99f, "manual coverage created redundant automatic captures");

        triangles.clear(); Box(triangles, {-2, -2, -2}, {2, 2, 2}, false);
        geometry.opaque.Build(triangles); result = Generate(geometry, settings);
        Check(result.coveredSurfaceFraction > 0.99f, "solid exterior not covered");
        for (const auto& probe : result.probes)
            Check(glm::any(glm::greaterThan(glm::abs(probe.capturePosition), glm::vec3(2))), "capture inside solid cube");
        VerifyUseful(geometry, result, settings);

        triangles.clear(); Quad(triangles, {0, -2, -2}, {0, 4, 0}, {0, 0, 4}, {1, 0, 0}, true);
        geometry.opaque.Build(triangles); result = Generate(geometry, settings);
        left = false; right = false;
        for (const auto& probe : result.probes) { left |= probe.capturePosition.x < 0; right |= probe.capturePosition.x > 0; }
        Check(left && right && result.coveredSurfaceFraction > 0.99f, "two-sided thin wall lost one side");
        VerifyUseful(geometry, result, settings);

        // 有围墙但没有屋顶的院落：上半球可见即露天，不能因地面/围墙遮挡被判成室内。
        triangles.clear();
        Quad(triangles, {-15,0,-15}, {30,0,0}, {0,0,30}, {0,1,0});
        Quad(triangles, {-15,0,-15}, {0,5,0}, {0,0,30}, {1,0,0});
        Quad(triangles, {15,0,-15}, {0,5,0}, {0,0,30}, {-1,0,0});
        Quad(triangles, {-15,0,-15}, {30,0,0}, {0,5,0}, {0,0,1});
        Quad(triangles, {-15,0,15}, {30,0,0}, {0,5,0}, {0,0,-1});
        geometry.opaque.Build(triangles);
        auto courtyard = settings; courtyard.cellSize = 2; courtyard.outdoorSpacing = 32;
        result = Generate(geometry, courtyard);
        Check(result.coveredSurfaceFraction > 0.99f && result.probes.size() <= 6, "open courtyard lost sparse visible coverage");
        for (float x : {-14.0f, 0.0f, 14.0f}) for (float z : {-14.0f, 0.0f, 14.0f})
        {
            bool covered = false;
            for (const auto& probe : result.probes) covered |= Contributes(geometry, probe, {x,0,z}, {0,1,0});
            Check(covered, "independent courtyard corner or center sample has no visible reflection capture");
        }
        VerifyUseful(geometry, result, courtyard);

        triangles.clear(); Quad(triangles, {0,-2,-2}, {0,4,0}, {0,0,4}, {1,0,0}, true);
        geometry.opaque.Build({}); geometry.transmissionReceivers = triangles;
        result = Generate(geometry, settings);
        Check(!result.probes.empty() && result.coveredSurfaceFraction > 0.99f, "transparent-only receiver not served");
        geometry.transmissionReceivers.clear(); geometry.dynamicReceivers.push_back({{-1, -1, -1}, {1, 1, 1}});
        result = Generate(geometry, settings);
        Check(!result.probes.empty() && result.coveredReceiverCount == result.receiverCount, "dynamic receiver-only demand discarded");

        const auto before = result.probes;
        settings.cellSize = std::numeric_limits<float>::quiet_NaN();
        std::string error;
        Check(!VansReflectionProbePlacement::Generate(geometry, settings, {}, result, error) && !error.empty(), "invalid settings accepted");
        VerifySame(before, result.probes);
        std::cout << "[ReflectionPlacement] PASS: empty space, open floor, negative-Y rooms, solid cube, thin wall, overrides, budget, dynamic/transparent receivers, deterministic layout and off/on/off ownership\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[ReflectionPlacement] FAIL: " << exception.what() << '\n';
        return false;
    }
}

bool TestProbeSceneDefaultsContract(const char* workspaceRoot)
{
    try
    {
        using namespace VansGraphics;
        const std::filesystem::path root(workspaceRoot);
        for (const char* relative : {"DustV2Project/Scenes/MainScene.json", "SponzaProject/Scenes/MainScene.json",
            "TestV2Project/Scenes/MainScene.json", "DemoHallProject/Scenes/DemoHall.json"})
        {
            std::ifstream file(root / relative); Check(bool(file), "scene default policy file missing");
            nlohmann::json document; file >> document;
            const bool enabled = std::string(relative).find("DustV2Project/") == 0;
            Check(document["settings"]["globalIllumination"]["placement"]["enabled"] == enabled
                && document["settings"]["reflectionProbes"]["placement"]["enabled"] == enabled,
                "scene GI/reflection automatic defaults do not match user policy");
            if (std::string(relative).find("SponzaProject/") == 0)
                Check(document["settings"]["reflectionProbes"]["placement"]["maxProbeCount"] == 128,
                    "Sponza reflection budget must remain 128");
        }
        std::cout << "[ProbeSceneDefaults] PASS: current scene defaults and Sponza reflection budget\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[ProbeSceneDefaults] FAIL: " << exception.what() << '\n';
        return false;
    }
}
