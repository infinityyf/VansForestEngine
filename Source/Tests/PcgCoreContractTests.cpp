#include "PcgCoreContractTests.h"
#include "../EngineCore/PcgCore/VansPcgMask.h"
#include "../EngineCore/PcgCore/VansPcgMaskBrush.h"
#include "../EngineCore/PcgCore/VansPcgPointGenerator.h"
#include "../EngineCore/PcgCore/VansPcgBatchPlan.h"
#include "../EngineCore/PcgCore/VansPcgSplineField.h"
#include "../EngineCore/PcgCore/Serialization/VansPcgSplineAssetCodec.h"
#include "../EngineCore/PcgCore/Storage/VansPcgSplineFieldStorage.h"
#include "../EngineCore/RenderCore/WaterCore/VansWaterGeometryClipmap.h"
#include "../EngineCore/RenderCore/WaterCore/VansRiverWaveSimulation.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationCollection.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
using namespace Vans;

bool Check(bool condition, const std::string& message)
{
	if (!condition) std::cerr << "[PcgCore] " << message << '\n';
	return condition;
}

VansPcgMask MakeMask(const std::string& layer, std::uint32_t width = 256, std::uint32_t height = 128)
{
	VansPcgMask mask;
	std::string error;
	VansPcgMask::CreateBlank({ "region", layer, layer + "-density" }, { { -16, -8 }, { 16, 8 } }, width, height, mask, error);
	return mask;
}

bool Paint(VansPcgMask& mask, const VansPcgBrushSettings& settings,
	const std::vector<std::array<float, 2>>& points, VansPcgMaskEdit& edit)
{
	VansPcgMaskStroke stroke;
	std::string error;
	if (!Check(stroke.Begin(mask, settings, error), error)) return false;
	for (const auto& point : points)
	{
		VansPcgPixelRect dirty;
		if (!Check(stroke.AddPoint(mask, point[0], point[1], false, dirty, error), error)) return false;
	}
	return Check(stroke.Finish(mask, edit, error), error);
}

bool TestBlankAndSampling()
{
	auto a = MakeMask("first", 2, 2);
	if (!Check(a.IsValid() && a.Sample(0, 0) == 0 && VansPcgMask().Sample(0, 0) == 0,
		"New or missing masks must never permit vegetation")) return false;
	a.pixels = { 0, 65535, 65535, 0 };
	if (!Check(std::abs(a.Sample(0, 0) - 0.5f) < 0.00001f && a.Sample(-8, -4) == 0 &&
		a.Sample(8, -4) == 1 && a.Sample(16, 0) == 0 && a.Sample(0, 8) == 0,
		"Mask world mapping, linear interpolation or outside rejection is incorrect")) return false;
	if (!Check(a.Sample(std::numeric_limits<float>::quiet_NaN(), 0) == 0,
		"Nonfinite sample coordinates must fail closed")) return false;
	const auto original = a.ContentHash();
	auto b = a;
	b.target.layerId = "second";
	b.target.maskId = "second-density";
	b.pixels[0] = 65535;
	if (!Check(a.ContentHash() == original && b.ContentHash() != original,
		"Copied masks must have independent pixels and identity")) return false;
	std::string error;
	VansPcgMask invalid = a;
	if (!Check(!VansPcgMask::CreateBlank({}, a.bounds, 2, 2, invalid, error) && invalid.ContentHash() == original,
		"Invalid mask creation must preserve the previous result")) return false;
	return Check(!VansPcgMask::CreateBlank(a.target, a.bounds, 0, 2, invalid, error), "Zero-sized mask accepted");
}

bool TestIndependentStrokeAndUndo()
{
	auto grassA = MakeMask("first"), grassB = MakeMask("second");
	const auto otherHash = grassB.ContentHash(), blankHash = grassA.ContentHash();
	VansPcgBrushSettings brush;
	brush.radius = 1;
	brush.strength = 0.7f;
	VansPcgMaskEdit edit;
	if (!Paint(grassA, brush, { { 0, 0 } }, edit)) return false;
	if (!Check(grassA.Sample(0, 0) > 0.6f && grassA.Sample(4, 0) == 0 && grassB.ContentHash() == otherHash,
		"Painting one grass type changed another type or pixels outside the brush")) return false;
	std::size_t recorded = 0;
	for (const auto& tile : edit.tiles) recorded += tile.before.size();
	if (!Check(!edit.Empty() && recorded < grassA.pixels.size() / 2 && !edit.dirtyRect.Empty(),
		"Local strokes must record changed tiles and a dirty rectangle, not the entire mask")) return false;
	std::string error;
	const auto paintedHash = grassA.ContentHash();
	if (!Check(!edit.Apply(grassB, false, error) && grassB.ContentHash() == otherHash,
		"Undo must reject a different grass target without mutation")) return false;
	if (!Check(edit.Apply(grassA, false, error) && grassA.ContentHash() == blankHash,
		"Undo did not restore the exact original pixels")) return false;
	if (!Check(edit.Apply(grassA, true, error) && grassA.ContentHash() == paintedHash,
		"Redo did not restore the exact painted pixels")) return false;
	if (!Check(!edit.Apply(grassA, true, error) && grassA.ContentHash() == paintedHash,
		"Out-of-order redo should fail atomically")) return false;
	return Check(grassB.ContentHash() == otherHash, "Undo/redo changed another mask");
}

bool TestInputRateAndGaps()
{
	auto coarse = MakeMask("first"), fine = coarse;
	VansPcgBrushSettings brush;
	brush.radius = 1;
	brush.strength = 0.15f;
	brush.spacingFraction = 0.25f;
	VansPcgMaskEdit edit;
	if (!Paint(coarse, brush, { { -8, 0 }, { 8, 0 } }, edit)) return false;
	std::vector<std::array<float, 2>> points;
	for (int i = 0; i <= 128; ++i) points.push_back({ -8 + i * 0.125f, 0 });
	if (!Paint(fine, brush, points, edit)) return false;
	if (!Check(coarse.pixels == fine.pixels, "Stroke strength or coverage depends on input frequency")) return false;
	auto gap = MakeMask("gap");
	VansPcgMaskStroke stroke;
	std::string error;
	VansPcgPixelRect dirty;
	if (!stroke.Begin(gap, brush, error) || !stroke.AddPoint(gap, -8, 0, false, dirty, error)) return false;
	stroke.BreakSegment();
	if (!stroke.AddPoint(gap, 8, 0, false, dirty, error) || !stroke.Finish(gap, edit, error)) return false;
	return Check(gap.Sample(0, 0) == 0 && gap.Sample(-8, 0) > 0 && gap.Sample(8, 0) > 0,
		"A missing surface hit must break the stroke rather than paint across the gap");
}

bool TestGeometryAndModes()
{
	// 非方形世界范围和像素比例下，世界半径仍为圆形。
	auto mask = MakeMask("aspect", 128, 128);
	VansPcgBrushSettings brush;
	brush.radius = 3;
	brush.hardness = 1;
	brush.strength = 1;
	brush.operation = VansPcgBrushOperation::Set;
	brush.targetValue = 0.5f;
	VansPcgMaskEdit edit;
	if (!Paint(mask, brush, { { 0, 0 } }, edit)) return false;
	if (!Check(std::abs(mask.Sample(2, 0) - mask.Sample(0, 2)) < 0.0001f &&
		mask.Sample(4, 0) == 0 && mask.Sample(0, 4) == 0,
		"A world-space circular brush became elliptical")) return false;
	const auto filled = mask;
	brush.operation = VansPcgBrushOperation::Subtract;
	brush.strength = 0.25f;
	if (!Paint(mask, brush, { { 0, 0 } }, edit) || !Check(std::abs(mask.Sample(0, 0) - 0.25f) < 0.0001f, "Subtract failed")) return false;
	brush.operation = VansPcgBrushOperation::Erase;
	brush.strength = 1;
	if (!Paint(mask, brush, { { 0, 0 } }, edit) || !Check(mask.Sample(0, 0) == 0, "Erase failed")) return false;
	mask = filled;
	const std::size_t spike = static_cast<std::size_t>(64) * mask.width + 64;
	mask.pixels[spike] = 65535;
	brush.operation = VansPcgBrushOperation::Smooth;
	if (!Paint(mask, brush, { { 0, 0 } }, edit)) return false;
	return Check(mask.pixels[spike] < 65535 && mask.pixels[spike] > 32767,
		"Smooth must use neighboring values from the same pre-dab mask");
}

bool TestTargetLockAndCancellation()
{
	auto mask = MakeMask("first"), other = MakeMask("second");
	const auto blank = mask.ContentHash(), otherHash = other.ContentHash();
	VansPcgMaskStroke stroke;
	VansPcgBrushSettings brush;
	brush.strength = 0.5f;
	std::string error;
	VansPcgPixelRect dirty;
	if (!Check(stroke.Begin(mask, brush, error) && stroke.AddPoint(mask, 0, 0, false, dirty, error), error)) return false;
	if (!Check(!stroke.AddPoint(other, 4, 0, false, dirty, error) && other.ContentHash() == otherHash,
		"Switching selection mid-stroke wrote to another grass")) return false;
	if (!Check(!stroke.Begin(other, brush, error), "A second active stroke was accepted")) return false;
	if (!Check(stroke.Cancel(mask, error) && mask.ContentHash() == blank && !stroke.IsActive(),
		"Cancel did not restore the original target")) return false;
	brush.strength = std::numeric_limits<float>::quiet_NaN();
	if (!Check(!stroke.Begin(mask, brush, error), "Nonfinite brush settings accepted")) return false;
	brush.strength = 0;
	VansPcgMaskEdit edit;
	if (!Paint(mask, brush, { { 0, 0 }, { 1, 0 } }, edit)) return false;
	return Check(edit.Empty() && mask.ContentHash() == blank, "Zero-strength brush must produce no edit");
}

bool SamePoints(const std::vector<VansPcgPoint>& a, const std::vector<VansPcgPoint>& b)
{
	if (a.size() != b.size()) return false;
	for (std::size_t i = 0; i < a.size(); ++i)
		if (a[i].id != b[i].id || a[i].variantIndex != b[i].variantIndex ||
			a[i].position != b[i].position || a[i].scale != b[i].scale || a[i].rotation != b[i].rotation) return false;
	return true;
}

bool TestDensityAndDeterminism()
{
	auto mask = MakeMask("first");
	std::fill(mask.pixels.begin(), mask.pixels.end(), 65535);
	VansPcgDistributionSettings settings;
	settings.regionId = mask.target.regionId;
	settings.layerId = mask.target.layerId;
	settings.bounds = mask.bounds;
	settings.density = 4;
	settings.positionJitter = 1;
	settings.seed = 18293;
	settings.variants = { { "variant-b", 1, 0 }, { "variant-a", 3, 0 } };
	const VansPcgGenerationBudget budget{ 100000, 100000 };
	const VansPcgSurfaceSampler flat = [](float, float, VansPcgSurfacePoint& point) { point.height = 3; return true; };
	auto generate = [&]() { return VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, settings.bounds, flat, budget); };
	const auto full = generate();
	if (!Check(full && full.points.size() == 2048, "Density must represent instances per projected square unit")) return false;
	// 扩区只增加新区域的点，原区域的密度、ID、位置和变体必须完全不变。
	auto expandedSettings=settings;expandedSettings.bounds={{-32,-16},{32,16}};
	auto expandedMask=mask;expandedMask.bounds=expandedSettings.bounds;
	const auto expanded=VansPcgPointGenerator::GenerateDensity(expandedSettings,expandedMask,nullptr,
		expandedSettings.bounds,flat,budget);
	if (!Check(expanded && expanded.points.size()==full.points.size()*4,"Larger grass region diluted density")) return false;
	std::vector<VansPcgPoint> originalArea;
	for (const auto& point:expanded.points)
		if (settings.bounds.Contains(point.position[0],point.position[2])) originalArea.push_back(point);
	if (!Check(SamePoints(full.points,originalArea),"Expanding a grass region redistributed existing points")) return false;
	settings.density = 2;
	const auto sparse = generate();
	if (!Check(sparse && sparse.points.size() == 1024, "Halving density must halve population without count refill")) return false;
	settings.density = 4;
	std::fill(mask.pixels.begin(), mask.pixels.end(), 32768);
	const auto grey = generate();
	if (!Check(grey && grey.points.size() > 900 && grey.points.size() < 1150, "Uniform grey mask did not reduce population")) return false;
	std::fill(mask.pixels.begin(), mask.pixels.end(), 0);
	const auto empty = generate();
	if (!Check(empty && empty.points.empty(), "Black mask produced instances")) return false;
	settings.invertMask = true;
	mask.bounds.max[0] = 0;
	const auto inverted = generate();
	if (!Check(inverted && inverted.points.size() == 1024 && std::all_of(inverted.points.begin(), inverted.points.end(),
		[](const VansPcgPoint& point) { return point.position[0] < 0; }),
		"Mask inversion generated outside the mask's defined world range")) return false;
	mask.bounds = settings.bounds;
	settings.invertMask = false;
	std::fill(mask.pixels.begin(), mask.pixels.end(), 65535);
	std::reverse(settings.variants.begin(), settings.variants.end());
	const auto reordered = generate();
	if (!Check(reordered && SamePoints(full.points, reordered.points), "Reordering variants changed stable points or choices")) return false;
	const auto high = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, settings.bounds,
		[](float, float, VansPcgSurfacePoint& point) { point.height = 1003; return true; }, budget);
	if (!Check(high && high.points.size() == full.points.size(), "Altitude changed vegetation eligibility")) return false;
	for (std::size_t i = 0; i < full.points.size(); ++i)
		if (!Check(full.points[i].id == high.points[i].id && full.points[i].position[0] == high.points[i].position[0] &&
			full.points[i].position[2] == high.points[i].position[2] && high.points[i].position[1] == 1003,
			"Surface height should only update grounding")) return false;
	const auto overBudget = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, settings.bounds, flat, { 100000, 1 });
	if (!Check(!overBudget && overBudget.points.empty(), "Instance budget failure published partial results")) return false;
	const auto noSurface = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, settings.bounds, {}, budget);
	if (!Check(!noSurface, "Missing user surface binding was silently replaced")) return false;
	auto wrongMask = mask;
	wrongMask.target.layerId = "second";
	return Check(!VansPcgPointGenerator::GenerateDensity(settings, wrongMask, nullptr, settings.bounds, flat, budget),
		"Generator accepted another grass type's Mask");
}

bool TestBatchReplacement()
{
	using Collection=VansGraphics::VansVegetationCollection;
	const VansPcgBounds roots{{10,-2},{20,2}};
	if (!Check(Collection::WithinDistance(roots,0,0,10) && !Collection::WithinDistance(roots,0,0,9.99f) &&
		Collection::WithinDistance(roots,15,0,0) && !Collection::WithinDistance(roots,100,0,50) &&
		Collection::WithinDistance({{3,4},{3,4}},0,0,5),
		"Vegetation range selection lost boundary/single-root cells or retained distant cells")) return false;
	auto mask=MakeMask("first");
	std::fill(mask.pixels.begin(),mask.pixels.end(),65535);
	VansPcgRegion region;
	region.id="region";region.bounds=mask.bounds;region.cellSize=4;
	VansPcgLayer layer;
	layer.id="first";layer.placement.density=3;layer.placement.minimumSpacing=.6f;
	VansPcgDistributionSettings settings;
	static_cast<VansPcgPlacementSettings&>(settings)=layer.placement;
	settings.regionId=region.id;settings.layerId=layer.id;settings.bounds=region.bounds;
	settings.variants={{"model-a",1,.2f},{"model-b",2,.3f}};
	auto plant=std::make_shared<VansPlantTypeAsset>();
	for (const auto& variant : settings.variants) {
		VansPlantVariant item;item.id=variant.id;item.footprintRadius=variant.footprintRadius;
		plant->variants.push_back(item);
	}
	const VansPcgSurfaceSampler flat=[](float,float,VansPcgSurfacePoint&){return true;};
	const auto generate=[&](const std::optional<VansPcgBounds>& bounds,VansPcgBatchUpdate& update) {
		const auto generated=VansPcgPointGenerator::GenerateDensity(settings,mask,nullptr,bounds.value_or(region.bounds),flat,{100000,100000});
		if (!Check(static_cast<bool>(generated),generated.error)) return false;
		VansPcgLayerResult result;
		result.regionId=region.id;result.layerId=layer.id;result.plant=plant;
		result.variantIds=generated.variantIds;result.points=generated.points;
		std::reverse(result.points.begin(),result.points.end());
		std::string error;
		return Check(BuildPcgBatchUpdate(region,result,bounds,update,error),error);
	};
	VansPcgBatchUpdate before;
	if (!generate(std::nullopt,before) || !Check(before.batches.size()>8,"Models were not divided into world cells")) return false;
	VansPcgBrushSettings brush;
	brush.operation=VansPcgBrushOperation::Erase;brush.radius=3;brush.strength=1;brush.hardness=1;
	VansPcgMaskEdit edit;
	if (!Paint(mask,brush,{{-10,0}},edit)) return false;
	const auto coverage=PcgMaskUpdateCoverage(region,layer,*plant,mask,edit.dirtyRect);
	if (!Check(coverage.has_value(),"Local density edit unexpectedly requires a full update")) return false;
	VansPcgBatchUpdate partial,full;
	if (!generate(coverage,partial) || !generate(std::nullopt,full)) return false;
	auto merged=before.batches;
	std::size_t untouched=0;
	for (auto it=merged.begin();it!=merged.end();) {
		if (partial.Contains(it->first)) it=merged.erase(it);
		else {++untouched;++it;}
	}
	merged.insert(partial.batches.begin(),partial.batches.end());
	if (!Check(untouched>0 && merged.size()==full.batches.size(),"Local replacement lost cells or rebuilt the whole region")) return false;
	for (const auto& entry : full.batches) {
		const auto found=merged.find(entry.first);
		if (!Check(found!=merged.end() && EqualPcgBatchSources(*found->second,*entry.second),
			"Brush-local batch replacement differs from full regeneration")) return false;
	}
	const auto key=before.batches.begin()->first;
	auto foreign=key;foreign.layer="second";
	if (!Check(!partial.Contains(foreign),"A batch update can erase a different grass layer")) return false;
	VansPcgLayerResult empty;
	empty.regionId=region.id;empty.layerId=layer.id;empty.plant=plant;
	std::string error;
	VansPcgBatchUpdate clear;
	if (!Check(BuildPcgBatchUpdate(region,empty,std::nullopt,clear,error) && clear.batches.empty() &&
		clear.Contains(key) && !clear.Contains(foreign),"An empty update failed to remove only its own old instances")) return false;
	const auto& source=*before.batches.begin()->second;
	auto same=source;
	if (!Check(EqualPcgBatchSources(source,same),"Unchanged batches would reset their simulation")) return false;
	same.points.front().scale[0]*=2;
	if (!Check(!EqualPcgBatchSources(source,same),"Changed transforms were incorrectly retained")) return false;
	layer.source=VansPcgSourceMode::Count;
	if (!Check(!PcgMaskUpdateCoverage(region,layer,*plant,mask,edit.dirtyRect),"Count refill was incorrectly truncated")) return false;
	layer.source=VansPcgSourceMode::Density;layer.placement.rootOffset=1;
	if (!Check(!PcgMaskUpdateCoverage(region,layer,*plant,mask,edit.dirtyRect),"Shifted cell ownership was incorrectly truncated")) return false;
	std::int64_t cell=0;
	return Check(PcgCellCoordinate(-.1f,4,cell) && cell==-1 &&
		!PcgCellCoordinate(std::numeric_limits<float>::max(),.001f,cell),"Invalid or negative world-cell conversion");
}

bool TestChunkSpacingAndLocality()
{
	auto mask = MakeMask("first");
	std::fill(mask.pixels.begin(), mask.pixels.end(), 65535);
	VansPcgDistributionSettings settings;
	settings.regionId = mask.target.regionId;
	settings.layerId = mask.target.layerId;
	settings.bounds = mask.bounds;
	settings.density = 3;
	settings.positionJitter = 1;
	settings.minimumSpacing = 1.25f;
	settings.scaleMin = { 0.8f, 0.8f, 0.8f };
	settings.scaleMax = { 1.2f, 1.2f, 1.2f };
	settings.variants = { { "user-model", 1, 0.2f } };
	const VansPcgGenerationBudget budget{ 100000, 100000 };
	const VansPcgSurfaceSampler surface = [](float, float, VansPcgSurfacePoint&) { return true; };
	const VansPcgBounds left{ { -16, -8 }, { 0, 8 } }, right{ { 0, -8 }, { 16, 8 } };
	const auto full = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, settings.bounds, surface, budget);
	const auto a = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, left, surface, budget);
	const auto b = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, right, surface, budget);
	if (!Check(full && a && b && !full.points.empty(), "Chunk generation failed")) return false;
	auto combined = b.points;
	combined.insert(combined.end(), a.points.begin(), a.points.end());
	std::sort(combined.begin(), combined.end(), [](const auto& x, const auto& y) { return x.id < y.id; });
	if (!Check(SamePoints(full.points, combined), "Separately generated chunks differ from full-region generation")) return false;
	for (std::size_t i = 0; i < full.points.size(); ++i) for (std::size_t j = i + 1; j < full.points.size(); ++j)
	{
		const float dx = full.points[i].position[0] - full.points[j].position[0];
		const float dz = full.points[i].position[2] - full.points[j].position[2];
		if (!Check(dx * dx + dz * dz >= 1.25f * 1.25f, "Cross-chunk spacing constraint violated")) return false;
	}
	VansPcgBrushSettings brush;
	brush.operation = VansPcgBrushOperation::Erase;
	brush.radius = 2;
	brush.strength = 1;
	brush.hardness = 1;
	VansPcgMaskEdit edit;
	if (!Paint(mask, brush, { { -10, 0 } }, edit)) return false;
	const auto after = VansPcgPointGenerator::GenerateDensity(settings, mask, nullptr, right, surface, budget);
	if (!Check(after && SamePoints(b.points, after.points), "Local brush changed a distant unaffected chunk")) return false;
	settings.layerId = "second";
	auto other = MakeMask("second");
	std::fill(other.pixels.begin(), other.pixels.end(), 65535);
	const auto independent = VansPcgPointGenerator::GenerateDensity(settings, other, nullptr, settings.bounds, surface, budget);
	if (!Check(independent && !independent.points.empty() && independent.points[0].id != full.points[0].id,
		"Independent grass layers shared candidate identity")) return false;
	return true;
}
}

bool RunPcgCoreContractTests()
{
	const auto splineContracts = [] {
		VansPcgSpline river;
		river.id="river";river.name="River";river.kind=VansPcgSplineKind::River;
		VansPcgSplinePoint first,last;
		first.id="first";first.position={-50,10,0};first.outgoing=VansPcgSplineSegmentMode::Line;
		last.id="last";last.position={50,10,0};river.points={first,last};
		river.fadeInDistance=10;river.fadeOutDistance=20;
		river.waterBlendStartMeters=10;river.waterBlendEndMeters=20;
		river.wetBankWidthMeters=4;river.wetnessStrength=.8f;
		std::string error;VansPcgEvaluatedSpline evaluated;
		if (!Check(VansPcgSplineEvaluator::Evaluate(river,.5f,.01f,evaluated,error),error)) return false;
		if (!Check(std::abs(evaluated.length-100)<.001f &&
			VansPcgSplineEvaluator::EndpointFade(river,0,100)==0 &&
			VansPcgSplineEvaluator::EndpointFade(river,100,100)==0 &&
			std::abs(VansPcgSplineEvaluator::EndpointFade(river,5,100)-.5f)<1e-6f,
			"Spline endpoint speed envelope is incorrect")) return false;
		auto reversed=river;VansPcgSplineEvaluator::ReversePointOrder(reversed,100);
        for(float along:{0.f,3.f,8.f,40.f,85.f,98.f,100.f})
        {
            const float weight=VansPcgSplineEvaluator::WaterEndpointWeight(river,along,100,2);
            if(!Check(std::abs(weight-VansPcgSplineEvaluator::WaterEndpointWeight(reversed,100-along,100,2))<1e-6f,
                "Point reversal moved the Water Level transition"))return false;
            auto flowReversed=river;flowReversed.flowSign=-1;
            if(!Check(weight==VansPcgSplineEvaluator::WaterEndpointWeight(flowReversed,along,100,2),
                "Changing flow direction moved the Water Level transition"))return false;
            auto continuation=river;continuation.continuation=true;continuation.envelopeOffset=along;
            continuation.envelopeLength=100;
            if(!Check(weight==VansPcgSplineEvaluator::WaterEndpointWeight(continuation,0,100-along,2),
                "Continuation restarted the Water Level transition"))return false;
        }
		VansPcgEvaluatedSpline reverseSamples;
		if (!Check(VansPcgSplineEvaluator::Evaluate(reversed,.5f,.01f,reverseSamples,error),error)) return false;
		if (!Check(glm::length(VansPcgSplineEvaluator::Velocity(river,evaluated.samples[20],100)-
			VansPcgSplineEvaluator::Velocity(reversed,reverseSamples.samples[reverseSamples.samples.size()-21],100))<1e-5f,
			"Reversing point order changed physical river flow")) return false;
		if (!Check(VansPcgSplineEvaluator::InsertPoint(river,0,.37f,"inserted",error),error)) return false;
		if (!Check(std::abs(river.points[1].position[0]+13)<1e-5f,"Preserve-shape line insertion moved the curve")) return false;
		VansPcgSplineAsset asset;asset.name="Spline contracts";asset.terrain=VansAssetGuid::New();asset.splines={river};
		VansSerializedValue encoded;VansPcgSplineAsset decoded;
		if (!Check(VansPcgSplineAssetCodec::Encode(asset,encoded,error) && VansPcgSplineAssetCodec::Decode(encoded,decoded,error) &&
			VansPcgSplineAssetCodec::ContentHash(asset)==VansPcgSplineAssetCodec::ContentHash(decoded),"Spline serialization changed author data: "+error)) return false;
		auto missingWetness=encoded;
		const auto encodedSplines=std::find_if(missingWetness.objectFields.begin(),missingWetness.objectFields.end(),
			[](const auto& field){return field.first=="splines";});
		if (!Check(encodedSplines!=missingWetness.objectFields.end() && !encodedSplines->second.arrayItems.empty(),
			"Spline wetness fixture is missing its river")) return false;
		auto& riverFields=encodedSplines->second.arrayItems.front().objectFields;
		riverFields.erase(std::remove_if(riverFields.begin(),riverFields.end(),
			[](const auto& field){return field.first=="wetnessStrength";}),riverFields.end());
		if (!Check(!VansPcgSplineAssetCodec::Decode(missingWetness,decoded,error),
			"River spline accepted a missing required wetness field")) return false;
		auto terrain=std::make_shared<VansTerrainAsset>();terrain->width=terrain->height=256;
		terrain->settings.terrainSize=128;terrain->settings.maxHeight=32;terrain->settings.heightOffset=0;
		terrain->heights.assign(256*256,32768);for(auto& splat:terrain->splatPixels)splat.resize(256*256*4);
		const auto base=terrain->heights;
		auto field=VansPcgSplineFieldBuilder::Build(asset,terrain,{},error);
		if (!Check(bool(field),error)) return false;
		const auto coverageAt=[](const VansPcgSplineFieldSnapshot& snapshot,float x,float z) {
			const float gx=(x+snapshot.worldSize*.5f)/snapshot.texelSize-.5f;
			const float gz=(z+snapshot.worldSize*.5f)/snapshot.texelSize-.5f;
			const int ix=static_cast<int>(std::floor(gx)),iz=static_cast<int>(std::floor(gz));
			const float fx=gx-ix,fz=gz-iz;glm::vec4 value(0);
			for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx)
			{
				const int px=ix+dx,pz=iz+dz;
				if(px<0||pz<0||px>=int(snapshot.resolution)||pz>=int(snapshot.resolution))continue;
				const auto* tile=snapshot.FindTile(px/VANS_SPLINE_TILE_SIZE,pz/VANS_SPLINE_TILE_SIZE);
				if(!tile)continue;
				const auto pixel=std::size_t(pz%VANS_SPLINE_TILE_SIZE+VANS_SPLINE_TILE_BORDER)*
					VANS_SPLINE_TILE_EXTENT+px%VANS_SPLINE_TILE_SIZE+VANS_SPLINE_TILE_BORDER;
				value+=(dx?fx:1-fx)*(dz?fz:1-fz)*tile->coverage[pixel];
			}
			return value;
		};
		const glm::vec4 wetCenter=coverageAt(*field,0,0);
		const glm::vec4 wetBank=coverageAt(*field,0,4);
		const glm::vec4 wetOuter=coverageAt(*field,0,6);
		const glm::vec4 dryOutside=coverageAt(*field,0,8);
		if(!Check(wetCenter.z>.79f && wetCenter.w>.99f && wetCenter.z<wetCenter.w &&
			wetBank.z<wetCenter.z && wetBank.z>wetOuter.z && wetOuter.z>0 && dryOutside.z<1e-5f,
			"River wetness must fill the bed and fade monotonically beyond the bank independently of deformation")) return false;
		auto lessWet=asset;lessWet.splines.front().wetnessStrength=.4f;
		const auto lessWetField=VansPcgSplineFieldBuilder::Build(lessWet,terrain,field,error);
		if(!Check(lessWetField && lessWetField->effectiveTerrain==field->effectiveTerrain &&
			coverageAt(*lessWetField,0,0).z>.39f && coverageAt(*lessWetField,0,0).z<.41f,
			"Wetness-only edits must rebuild coverage without invalidating terrain geometry")) return false;
        float sampledHeight;glm::vec2 sampledVelocity;glm::vec4 sampledProperties;
        if(!Check(field->SampleRiver({0,0},sampledHeight,sampledVelocity,sampledProperties) &&
            std::abs(sampledHeight-9.85f)<1e-4f && sampledVelocity.x>0 && sampledProperties.x>.99f,
            "Runtime river sampler lost water level, velocity or interior weight"))return false;
        if(!Check(!field->SampleRiver({1000,1000},sampledHeight,sampledVelocity,sampledProperties),
            "River sampling outside the field must not read stale pages"))return false;
        const auto startBlend=field->SampleWaterBlend({-50,0});
        const auto endBlend=field->SampleWaterBlend({50,0});
        if(!Check(startBlend.x<.001f && endBlend.x<.001f &&
            std::abs(field->SampleWaterBlend({-45,0}).x-.5f)<.002f &&
            std::abs(field->SampleWaterBlend({40,0}).x-.5f)<.002f &&
            field->SampleWaterBlend({0,0}).x==1 && field->SampleWaterBlend({0,10})==glm::vec3(0) &&
            field->SampleWaterBlend({1000,0})==glm::vec3(0),
            "Water Level transitions must reach ocean at endpoints, river inside, and zero outside"))return false;
        float previousBlend=-1;
        for(int i=0;i<=10000;++i)
        {
            const float weight=VansPcgSplineEvaluator::WaterEndpointWeight(river,i*.01f,100,2);
            if(!Check(weight>=0 && weight<=1,"Water blend polynomial exceeded its finite storage range"))return false;
        }
        for(int i=0;i<=100;++i)
        {
            const float value=field->SampleWaterBlend({-51+i*.12f,0}).x;
            if(!Check(value>=previousBlend && value>=0 && value<=1,"River mouth blend is not monotonic"))return false;
            previousBlend=value;
        }
        for(const glm::vec2 position:{glm::vec2(-44.9f,.1f),glm::vec2(40.1f,.1f),glm::vec2(.1f,3.1f)})
        {
            const auto blend=field->SampleWaterBlend(position);
            constexpr float epsilon=.01f;
            const glm::vec2 numeric(
                (field->SampleWaterBlend(position+glm::vec2(epsilon,0)).x-field->SampleWaterBlend(position-glm::vec2(epsilon,0)).x)/(2*epsilon),
                (field->SampleWaterBlend(position+glm::vec2(0,epsilon)).x-field->SampleWaterBlend(position-glm::vec2(0,epsilon)).x)/(2*epsilon));
            if(!Check(glm::length(glm::vec2(blend.y,blend.z)-numeric)<.001f,
                "Water transition derivatives disagree with finite differences"))return false;
        }
        if(!Check(glm::length(field->SampleWaterBlend({-.001f,3.1f})-field->SampleWaterBlend({.001f,3.1f}))<.001f,
            "Water transition changed across a PCG tile guard"))return false;
        auto wider=asset;wider.splines[0].waterBlendWidthMeters=4;wider.splines[0].waterBlendEndMeters=40;
        // 关闭旧的流速端部淡出，单独验证水面过渡会减慢真实搬运速度，不能仅减小图像权重。
        auto coastFlow=asset;coastFlow.splines[0].fadeInDistance=coastFlow.splines[0].fadeOutDistance=0;
        const auto coastFlowField=VansPcgSplineFieldBuilder::Build(coastFlow,terrain,field,error);
        if(!Check(bool(coastFlowField),error))return false;
        float previousSpeed=3;
        for(float x:{30.1f,35.1f,40.1f,45.1f,48.1f})
        {
            if(!Check(coastFlowField->SampleRiver({x,0},sampledHeight,sampledVelocity,sampledProperties),
                "Coastal flow fixture lost its river"))return false;
            const float speed=glm::length(sampledVelocity),blend=coastFlowField->SampleWaterBlend({x,0}).x;
            if(!Check(speed<previousSpeed && std::abs(speed-2*blend)<1e-5f,
                "River flow must slow with the wave transition, independently of old endpoint fades"))return false;
            previousSpeed=speed;
        }
        const auto widerField=VansPcgSplineFieldBuilder::Build(wider,terrain,field,error);
        if(!Check(widerField && widerField->effectiveTerrain==field->effectiveTerrain && widerField->rebuiltTileCount>0 &&
            widerField->SampleWaterBlend({0,3}).x<field->SampleWaterBlend({0,3}).x &&
            widerField->SampleWaterBlend({40,0}).x<field->SampleWaterBlend({40,0}).x,
            "Transition edits must affect blend without rebuilding terrain"))return false;
        auto invalidBlend=asset;invalidBlend.splines[0].waterBlendWidthMeters=-1;
        if(!Check(!ValidatePcgSplineAsset(invalidBlend,false).empty(),"Negative water blend width was accepted"))return false;
        // 河流汇流采用并集：端部消退的支流不能把另一条完整河流混回全局水面。
        auto overlap=asset;auto tributary=river;tributary.id="blend-tributary";
        for(auto& point:tributary.points)point.id="blend-"+point.id;
        tributary.waterBlendStartMeters=tributary.waterBlendEndMeters=1000;overlap.splines.push_back(tributary);
        const auto overlapField=VansPcgSplineFieldBuilder::Build(overlap,terrain,field,error);
        if(!Check(overlapField && overlapField->SampleWaterBlend({0,0}).x==1,
            "Tributary endpoint erased the main river blend"))return false;
        VansGraphics::VansRiverWaveSimulation packets60,packets30;
        for(int frame=0;frame<180;++frame)packets60.Update(1.f/60,field.get(),{0,0},1.8f,8);
        for(int frame=0;frame<90;++frame)packets30.Update(1.f/30,field.get(),{0,0},1.8f,8);
        const auto before=packets60.GpuData();
        if(!Check(before==packets30.GpuData() && before.size()>1026 &&
            before.size()<=VansGraphics::VansRiverWaveSimulation::MaxGpuVectors,
            "River packets must be frame-rate independent and fit their bounded spatial index"))return false;
        bool leftHeading=false,rightHeading=false,shortWave=false,longWave=false;
        for(int bucket=0;bucket<1024;++bucket)
        {
            const auto range=before[2+bucket];
            if(!Check(std::size_t(range.x+range.y*VansGraphics::VansRiverWaveSimulation::PacketVectorCount)<=before.size(),"Invalid river particle bucket"))return false;
            for(int i=0;i<int(range.y);++i)
            {
                const auto p=before[int(range.x)+i*VansGraphics::VansRiverWaveSimulation::PacketVectorCount];const auto wave=before[int(range.x)+i*VansGraphics::VansRiverWaveSimulation::PacketVectorCount+1];
                if(!Check(std::isfinite(p.x) && std::isfinite(wave.w) && wave.x>.945f && std::abs(wave.y)<.32f,
                    "River packet direction spread exceeded its local flow cone"))return false;
                leftHeading|=wave.y<-.1f;rightHeading|=wave.y>.1f;
                const float wavelength=6.28318530718f/wave.z;
                shortWave|=wavelength<1.5f;longWave|=wavelength>2.1f;
                const auto medium=before[int(range.x)+i*VansGraphics::VansRiverWaveSimulation::PacketVectorCount+2];
                const auto fine=before[int(range.x)+i*VansGraphics::VansRiverWaveSimulation::PacketVectorCount+3];
                if(!Check(medium.z>wave.z*2 && fine.z>medium.z*1.5f &&
                    std::isfinite(medium.w) && std::isfinite(fine.w) &&
                    glm::dot(glm::vec2(medium),glm::vec2(wave))>.9f &&
                    glm::dot(glm::vec2(fine),glm::vec2(wave))>.9f,
                    "River detail carriers must retain separate frequency bands within the flow direction cone"))return false;
            }
        }
        if(!Check(leftHeading && rightHeading && shortWave && longWave,
            "River packets lost their balanced heading spread or wavelength distribution"))return false;
        packets60.Update(1.f/60,field.get(),{0,0},1.8f,8);
        if(!Check(packets60.GpuData()!=before,"River packets did not advance"))return false;
        packets60.Update(1,nullptr,{0,0},1.8f,8);
        if(!Check(packets60.GpuData().size()==1026 && packets60.GpuData()[0].w==0,
            "Removing river fields must clear their wave simulation"))return false;
		if (!Check(terrain->heights==base && field->roads.empty() && field->effectiveTerrain->heights!=base,
			"River must carve a derived terrain without creating mesh or overwriting base pixels")) return false;
		const auto unchanged=VansPcgSplineFieldBuilder::Build(asset,terrain,field,error);
		if (!Check(unchanged==field,"Unchanged author inputs regenerated the field")) return false;
		// 手工河床模式必须保留源地形，同时保留水位、流向和湿岸；来回切换应使缓存正确失效。
		auto sculpted=asset;sculpted.splines.front().carveRiverbed=false;
		VansSerializedValue sculptedEncoded;VansPcgSplineAsset sculptedDecoded;
		if (!Check(VansPcgSplineAssetCodec::Encode(sculpted,sculptedEncoded,error) &&
			VansPcgSplineAssetCodec::Decode(sculptedEncoded,sculptedDecoded,error) &&
			!sculptedDecoded.splines.front().carveRiverbed &&
			VansPcgSplineAssetCodec::ContentHash(sculptedDecoded)!=VansPcgSplineAssetCodec::ContentHash(asset),
			"Riverbed authoring mode was lost in serialization or content hashing")) return false;
		const auto sculptedField=VansPcgSplineFieldBuilder::Build(sculptedDecoded,terrain,field,error);
		if (!Check(sculptedField && sculptedField->effectiveTerrain->heights==base && sculptedField->roads.empty() &&
			coverageAt(*sculptedField,0,0).w==0 &&
			std::abs(coverageAt(*sculptedField,0,4).z-wetBank.z)<1e-6f &&
			sculptedField->SampleRiver({0,0},sampledHeight,sampledVelocity,sampledProperties) &&
			std::abs(sampledHeight-9.85f)<1e-4f && sampledVelocity.x>0 && sampledProperties.x>.99f,
			"Disabling river carving must preserve terrain, water, flow and wet banks")) return false;
		const auto restoredCarving=VansPcgSplineFieldBuilder::Build(asset,terrain,sculptedField,error);
		if (!Check(restoredCarving && restoredCarving->effectiveTerrain->heights==field->effectiveTerrain->heights,
			"Re-enabling river carving reused stale manually sculpted terrain")) return false;
		const auto heightAt=[](const VansPcgSplineFieldSnapshot& surface,unsigned row) {
			return surface.effectiveTerrain->heights[std::size_t(row)*256+128]*(32.f/65535.f);
		};
		if (!Check(heightAt(*field,128)<heightAt(*field,130) && heightAt(*field,130)<heightAt(*field,133) &&
			heightAt(*field,133)>9.9f && heightAt(*field,136)>10.05f && field->uncoveredBankPoints.empty(),
			"River bed must rise smoothly to water level and bury the height-mask boundary under the bank")) return false;
		for (std::size_t i=0;i<base.size();++i)
			if (!Check(field->effectiveTerrain->heights[i]<=base[i],"River shaping raised the base terrain")) return false;
		auto steeper=asset;
		for(auto& p:steeper.splines.front().points)p.bankSteepness=.5f;
		const auto steepField=VansPcgSplineFieldBuilder::Build(steeper,terrain,field,error);
		if (!Check(steepField && heightAt(*steepField,131)<heightAt(*field,131) &&
			heightAt(*steepField,136)==heightAt(*field,136),"Bank steepness must change the submerged ramp without moving the shore")) return false;
        auto excluded=asset;excluded.splines.front().excludeVegetation=true;excluded.splines.front().vegetationFade=3;
        const auto exclusionField=VansPcgSplineFieldBuilder::Build(excluded,terrain,field,error);
        if (!Check(exclusionField && exclusionField->effectiveTerrain==field->effectiveTerrain &&
            exclusionField->SampleVegetationExclusion(0,0)==1 && exclusionField->SampleVegetationExclusion(0,8)==0 &&
            exclusionField->SampleVegetationExclusion(0,4)>exclusionField->SampleVegetationExclusion(0,5) &&
            exclusionField->SampleVegetationExclusion(0,5)>0,
            "Spline exclusion must leave terrain intact, clear the interior and fade outside the bank")) return false;
        const auto exclusionCache=std::filesystem::temp_directory_path()/(VansAssetGuid::New().ToString()+".pcgfields");
        struct ExclusionCleanup {std::filesystem::path path;~ExclusionCleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} exclusionCleanup{exclusionCache};
        if (!Check(VansPcgSplineFieldStorage::Save(exclusionCache,*exclusionField,error),error)) return false;
        const auto bakedExclusion=VansPcgSplineFieldStorage::Load(exclusionCache,excluded,terrain,error);
        if (!Check(bakedExclusion && bakedExclusion->hasVegetationExclusion &&
            bakedExclusion->SampleVegetationExclusion(0,0)==1 &&
            bakedExclusion->SampleVegetationExclusion(0,4)==exclusionField->SampleVegetationExclusion(0,4),
            "Baking changed the spline exclusion interior or feather")) return false;
        const auto restoredExclusion=VansPcgSplineFieldBuilder::Build(asset,terrain,exclusionField,error);
        if (!Check(restoredExclusion && !restoredExclusion->hasVegetationExclusion && restoredExclusion->SampleVegetationExclusion(0,0)==0,
            "Disabling spline exclusion did not restore the vegetation field")) return false;
        auto deeper=asset;for(auto& point:deeper.splines.front().points)point.depth+=1;
        const auto deepField=VansPcgSplineFieldBuilder::Build(deeper,terrain,field,error);
        if (!Check(deepField && heightAt(*deepField,128)<heightAt(*field,128)-.9f &&
            VansPcgSplineFieldBuilder::Build(asset,terrain,deepField,error)->effectiveTerrain->heights==field->effectiveTerrain->heights,
            "Live depth edit or restoration reused stale terrain")) return false;
        auto lowered=asset;lowered.splines.front().waterSurfaceDrop=.65f;
        const auto lowerField=VansPcgSplineFieldBuilder::Build(lowered,terrain,field,error);
        if (!Check(lowerField && std::abs(lowerField->FindTile(2,2)->heights[VANS_SPLINE_TILE_EXTENT+1].y-9.35f)<1e-4f &&
            heightAt(*lowerField,128)<heightAt(*field,128)-.49f && heightAt(*lowerField,136)==heightAt(*field,136) &&
            heightAt(*lowerField,133)>9.35f+.1f &&
            VansPcgSplineFieldBuilder::Build(asset,terrain,lowerField,error)->effectiveTerrain->heights==field->effectiveTerrain->heights,
            "Water surface drop must lower actual water and bed, retain raised banks, and restore through undo")) return false;
        auto motion=asset;motion.splines.front().points.front().speed+=1;
		const auto motionField=VansPcgSplineFieldBuilder::Build(motion,terrain,field,error);
		if (!Check(motionField && motionField->effectiveTerrain==field->effectiveTerrain,
			"Flow-only edits invalidated terrain collision and vegetation")) return false;
		auto lowGround=std::make_shared<VansTerrainAsset>(*terrain);lowGround->heights.assign(base.size(),16384);
		const auto exposed=VansPcgSplineFieldBuilder::Build(asset,lowGround,{},error);
		if (!Check(exposed && !exposed->uncoveredBankPoints.empty(),"Low banks must report insufficient cover instead of raising terrain")) return false;
		auto draft=asset;draft.splines.front().points.resize(1);
		const auto draftField=VansPcgSplineFieldBuilder::Build(draft,terrain,field,error);
		if (!Check(draftField && draftField->tiles.empty() && draftField->effectiveTerrain->heights==base,
			"Single-point authoring drafts must not deform terrain")) return false;
		auto opposing=river;opposing.id="opposing";opposing.flowSign=-1;
		for(auto& p:opposing.points){p.id+="-opposing";p.position[1]=12;}
		asset.splines.push_back(opposing);
		const auto merged=VansPcgSplineFieldBuilder::Build(asset,terrain,field,error);
		if (!Check(bool(merged),error)) return false;
		const auto* tile=merged->FindTile(2,2);
		const std::size_t center=VANS_SPLINE_TILE_EXTENT+1;
		if (!Check(tile && tile->hasRiver && std::abs(tile->heights[center].y-10.85f)<1e-4f &&
			glm::length(tile->velocities[center])<1e-5f && tile->riverProperties[center].x>0 && !merged->warnings.empty(),
			"Overlapping rivers did not blend height, cancel opposing velocities, or retain river coverage")) return false;
		const auto cachePath=std::filesystem::temp_directory_path()/(VansAssetGuid::New().ToString()+".pcgfields");
		struct CacheCleanup {std::filesystem::path path;~CacheCleanup(){std::error_code ignored;std::filesystem::remove(path,ignored);}} cleanup{cachePath};
		if (!Check(VansPcgSplineFieldStorage::Save(cachePath,*merged,error),error)) return false;
		const auto restored=VansPcgSplineFieldStorage::Load(cachePath,asset,terrain,error);
		if (!Check(restored && restored->effectiveTerrain->heights==merged->effectiveTerrain->heights &&
			restored->tiles.size()==merged->tiles.size() && restored->FindTile(2,2)->velocities==tile->velocities &&
			restored->FindTile(2,2)->riverProperties==tile->riverProperties &&
            restored->FindTile(2,2)->waterBlend==tile->waterBlend,"Baked fields changed after reload: "+error)) return false;
        auto staleBlend=asset;staleBlend.splines[0].waterBlendEndMeters+=1;
        if(!Check(!VansPcgSplineFieldStorage::Load(cachePath,staleBlend,terrain,error),"Stale water transition bake was accepted"))return false;
		auto edited=asset;edited.splines[0].points[0].speed+=1;
		if (!Check(!VansPcgSplineFieldStorage::Load(cachePath,edited,terrain,error),"Stale authored flow cache was accepted")) return false;
		auto terrainEdit=std::make_shared<VansTerrainAsset>(*terrain);terrainEdit->heights[0]+=1;
		if (!Check(!VansPcgSplineFieldStorage::Load(cachePath,asset,terrainEdit,error),"Stale terrain cache was accepted")) return false;
		VansGraphics::VansWaterGeometryClipmap geometry;geometry.GeneratePatches({100,20,100});
		if (!Check(geometry.RefineForRiverFields(*merged),"River CDLOD refinement exceeded its budget")) return false;
		for (const auto& patch:geometry.GetPatches()) if (patch.riverInfluenced)
			if (!Check(patch.worldSize/float(geometry.GetMeshDim()-1)<=merged->texelSize+1e-5f &&
				patch.minimumRiverHeight>=9.849f && patch.maximumRiverHeight<=11.851f,"River water geometry cannot resolve height coverage")) return false;
		asset.splines.clear();const auto cleared=VansPcgSplineFieldBuilder::Build(asset,terrain,merged,error);
		if (!Check(cleared && cleared->tiles.empty() && cleared->effectiveTerrain->heights==base && !cleared->changedTiles.empty(),
			"Removing all splines failed to restore terrain or clear previous field tiles")) return false;
        VansPcgSpline road=river;road.id="road-uv";road.kind=VansPcgSplineKind::Road;
        road.material=VansAssetGuid::New();road.points={first,last};road.textureRepeat=6;
        road.points.front().leftWidth=2;road.points.front().rightWidth=3;
        road.points.back().leftWidth=4;road.points.back().rightWidth=5;
        for(auto& p:road.points){p.bankAngleDegrees=12;p.linkedWidth=false;}
        asset.splines={road};const auto roadField=VansPcgSplineFieldBuilder::Build(asset,terrain,{},error);
        if (!Check(roadField && roadField->roads.size()==1,error)) return false;
        const auto& vertices=roadField->roads.at(road.id)->vertices;
        for(std::size_t i=0;i<vertices.size();i+=2)
        {
            const auto& left=vertices[i];const auto& right=vertices[i+1];
            const auto transverse=glm::normalize(right.position-left.position);
            const auto bitangent=glm::cross(left.normal,glm::vec3(left.tangent))*left.tangent.w;
            if (!Check(left.uv.x==0 && right.uv.x==1 && left.uv.y==right.uv.y &&
                glm::dot(transverse,glm::vec3(left.tangent))>.999f && bitangent.x>.999f &&
                (i==0 || left.uv.y>vertices[i-2].uv.y),
                "Road UVs must span width once, repeat longitudinally and orient normal-map tangent axes")) return false;
        }
        if (!Check(std::abs(vertices.back().uv.y-100.f/6)<1e-4f,"Road texture repeat is not measured along the spline")) return false;
        VansPcgSplineEvaluator::ReversePointOrder(asset.splines.front(),100);
        const auto reverseRoad=VansPcgSplineFieldBuilder::Build(asset,terrain,roadField,error);
        if (!Check(reverseRoad && reverseRoad->roads.at(road.id)->vertices.size()==vertices.size(),error)) return false;
        const auto& reversedVertices=reverseRoad->roads.at(road.id)->vertices;
        for(std::size_t i=0;i<vertices.size();++i)
            if (!Check(glm::length(vertices[i].position-reversedVertices[vertices.size()-1-i].position)<1e-4f &&
                glm::length(vertices[i].uv-reversedVertices[vertices.size()-1-i].uv)<1e-4f,
                "Reversing a road changed physical texture placement")) return false;
		std::cout<<"[PcgSpline] Spline serialization, projected flow, overlap, incremental reuse and terrain restoration passed\n";
        std::cout<<"[PcgSpline] Water Level blend endpoints, derivatives, tile guards, reversal, continuation and bake reload passed\n";
		return true;
	};
	if (!splineContracts()) return false;
	const bool passed = TestBlankAndSampling() && TestIndependentStrokeAndUndo() &&
		TestInputRateAndGaps() && TestGeometryAndModes() && TestTargetLockAndCancellation() &&
		TestDensityAndDeterminism() && TestChunkSpacingAndLocality() && TestBatchReplacement();
	if (passed) std::cout << "[PcgCore] Independent masks, world-space brushes and deterministic density generation passed\n";
	return passed;
}
