#include "VansPcgSplineField.h"
#include "Serialization/VansPcgSplineAssetCodec.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace Vans
{
namespace
{
struct Curve
{
    const VansPcgSpline* source = nullptr;
    VansPcgEvaluatedSpline evaluated;
    std::uint64_t hash = 0;
    std::uint64_t terrainShapeHash = 0;
    std::uint64_t roadHash = 0;
};
struct Candidate
{
    std::size_t curve = 0;
    std::vector<std::size_t> segments;
};
struct Contribution
{
    bool valid = false;
    VansPcgSplineSample sample;
    float lateral = 0;
    float edgeDistance = 0;
    float endDistance = 0;
    glm::vec4 jacobian{};
};
glm::vec2 XZ(glm::vec3 v) { return {v.x,v.z}; }
void Hash(std::uint64_t& h, std::uint64_t value)
{
    for (unsigned i=0;i<8;++i) {h^=(value>>(i*8))&255;h*=1099511628211ull;}
}

Contribution EvaluateAt(const Curve& curve, const Candidate& candidate, glm::vec2 world)
{
    Contribution result;
    float best = std::numeric_limits<float>::max();
    std::size_t nearest=0;float nearestT=0,nearestRaw=0,nearestLength2=0;
    for (const auto segment : candidate.segments)
    {
        const auto& a=curve.evaluated.samples[segment];const auto& b=curve.evaluated.samples[segment+1];
        const float dx=b.position.x-a.position.x,dz=b.position.z-a.position.z;
        const float length2=dx*dx+dz*dz;
        if (length2<1e-10f) continue;
        const float raw=((world.x-a.position.x)*dx+(world.y-a.position.z)*dz)/length2;
        const float t=std::clamp(raw,0.f,1.f);
        const float ox=world.x-a.position.x-t*dx,oz=world.y-a.position.z-t*dz;
        const float distance2=ox*ox+oz*oz;
        if (distance2>=best) continue;
        best=distance2;result.valid=true;nearest=segment;nearestT=t;nearestRaw=raw;nearestLength2=length2;
    }
    if (result.valid)
    {
        const auto segment=nearest;const float t=nearestT,raw=nearestRaw,length2=nearestLength2;
        const auto& a=curve.evaluated.samples[segment];const auto& b=curve.evaluated.samples[segment+1];
        const auto offset=world-glm::mix(XZ(a.position),XZ(b.position),t);
        auto& p=result.sample;
        p.position=glm::mix(a.position,b.position,t);
        p.tangent=glm::normalize(glm::mix(a.tangent,b.tangent,t));
        p.right=glm::normalize(glm::cross(p.tangent,glm::vec3(0,1,0)));
        p.distance=glm::mix(a.distance,b.distance,t);
        p.leftWidth=glm::mix(a.leftWidth,b.leftWidth,t);p.rightWidth=glm::mix(a.rightWidth,b.rightWidth,t);
        p.speed=glm::mix(a.speed,b.speed,t);p.depth=glm::mix(a.depth,b.depth,t);
        p.bankSteepness=glm::mix(a.bankSteepness,b.bankSteepness,t);
        p.bankAngleDegrees=glm::mix(a.bankAngleDegrees,b.bankAngleDegrees,t);
        result.lateral=glm::dot(offset,XZ(p.right));
        result.edgeDistance=(result.lateral<0?p.leftWidth:p.rightWidth)-std::abs(result.lateral);
        result.endDistance=0;
        if (segment==0 && raw<0) result.endDistance=-raw*std::sqrt(length2);
        if (segment+2==curve.evaluated.samples.size() && raw>1) result.endDistance=(raw-1)*std::sqrt(length2);
        const float ds=b.distance-a.distance;
        const auto longitudinal=(XZ(b.position-a.position)+result.lateral*XZ(b.right-a.right))/std::max(ds,1e-6f);
        const auto transverse=XZ(p.right);
        const float determinant=longitudinal.x*transverse.y-longitudinal.y*transverse.x;
        if (determinant>0.02f)
        {
            const float factor=curve.source->coordinateSign/determinant;
            result.jacobian={transverse.y*factor,-transverse.x*factor,-longitudinal.y*factor,longitudinal.x*factor};
        }
        else result.jacobian=glm::vec4(0);
    }
    return result;
}

float DeformationWeight(const Curve& curve, const Contribution& c)
{
    if (!c.valid) return 0;
    const float outside=std::max(-c.edgeDistance,c.endDistance);
    if (outside<=0) return 1;
    return curve.source->shoulder>0?1-VansPcgSplineEvaluator::SmoothWeight(outside/curve.source->shoulder):0;
}

float DeformHeight(float base, const Curve& curve, const Contribution& c)
{
    const float weight=DeformationWeight(curve,c);
    if (weight<=0) return base;
    float target=c.sample.position.y;
    if (curve.source->kind==VansPcgSplineKind::Road)
        target+=std::tan(glm::radians(c.sample.bankAngleDegrees))*c.lateral;
    else
    {
        const float half=c.lateral<0?c.sample.leftWidth:c.sample.rightWidth;
        const float ramp=std::max(half*(1-c.sample.bankSteepness),.005f);
        const float edge=VansPcgSplineEvaluator::SmoothWeight((std::abs(c.lateral)-(half-ramp))/ramp);
        float terminal=1;
        const float cap=std::max(curve.source->shoulder,.05f);
        if (!curve.source->continuation || curve.source->envelopeOffset<=0)
            terminal*=VansPcgSplineEvaluator::SmoothWeight(c.sample.distance/cap);
        if (!curve.source->continuation || curve.source->envelopeOffset+curve.evaluated.length>=curve.source->envelopeLength-.001f)
            terminal*=VansPcgSplineEvaluator::SmoothWeight((curve.evaluated.length-c.sample.distance)/cap);
        // 岸沿和开放端保持作者样条高度，实际水面下移；水线在坡面内部相交，
        // 不再在水平收坡处形成共面带。中心深度始终相对于下移后的实际水面。
        target-=(curve.source->waterSurfaceDrop+c.sample.depth)*(1-edge)*terminal;
        // 河流只挖低，路基则可以抬高或压低；始终从基础地形重放。
        target=std::min(target,base);
    }
    return glm::mix(base,target,weight);
}

std::shared_ptr<VansPcgRoadMesh> BuildRoad(const Curve& curve)
{
    auto mesh=std::make_shared<VansPcgRoadMesh>();
    const auto& spline=*curve.source;
    mesh->splineId=spline.id;mesh->material=spline.material;mesh->fingerprint=curve.roadHash;
    for (const auto& p:curve.evaluated.samples)
    {
        auto right=p.right; right.y=std::tan(glm::radians(p.bankAngleDegrees));
        const auto normal=glm::normalize(glm::cross(right,p.tangent));
        for (const float d:{-p.leftWidth,p.rightWidth})
        {
            VansPcgRoadVertex vertex;
            vertex.position=p.position+right*d+glm::vec3(0,spline.surfaceOffset,0);
            vertex.normal=normal;
            // U 横跨整幅路面，V 沿道路里程重复，标线沿样条而不是横穿道路。
            // 切线必须随 U 方向转到横断面；双轴符号保留反转点序后的纹理朝向。
            vertex.tangent=glm::vec4(glm::normalize(right)*spline.coordinateSign,1);
            const float across=(d+p.leftWidth)/(p.leftWidth+p.rightWidth);
            vertex.uv={.5f+spline.coordinateSign*(across-.5f),
                (spline.coordinateOffset+spline.coordinateSign*p.distance)/spline.textureRepeat};
            mesh->vertices.push_back(vertex);
        }
    }
    for (std::uint32_t i=0;i+3<mesh->vertices.size();i+=2)
        mesh->indices.insert(mesh->indices.end(),{i,i+1,i+2,i+1,i+3,i+2});
    return mesh;
}
}

const VansPcgSplineFieldTile* VansPcgSplineFieldSnapshot::FindTile(std::uint32_t x, std::uint32_t z) const
{
    const auto found=tiles.find(TileKey(x,z)); return found==tiles.end()?nullptr:found->second.get();
}

float VansPcgSplineFieldSnapshot::SampleVegetationExclusion(float x,float z) const
{
    if (!hasVegetationExclusion || !std::isfinite(x) || !std::isfinite(z) ||
        x < -worldSize*.5f || z < -worldSize*.5f || x >= worldSize*.5f || z >= worldSize*.5f) return 0;
    const float gx=(x+worldSize*.5f)/texelSize-.5f,gz=(z+worldSize*.5f)/texelSize-.5f;
    const int ix=static_cast<int>(std::floor(gx)),iz=static_cast<int>(std::floor(gz));
    const float fx=gx-ix,fz=gz-iz;
    float value=0;
    for (int dz=0;dz<2;++dz) for (int dx=0;dx<2;++dx)
    {
        const int px=ix+dx,pz=iz+dz;
        if (px<0 || pz<0 || px>=int(resolution) || pz>=int(resolution)) continue;
        const auto* tile=FindTile(px/VANS_SPLINE_TILE_SIZE,pz/VANS_SPLINE_TILE_SIZE);
        if (!tile) continue;
        const auto pixel=std::size_t(pz%VANS_SPLINE_TILE_SIZE+VANS_SPLINE_TILE_BORDER)*VANS_SPLINE_TILE_EXTENT+px%VANS_SPLINE_TILE_SIZE+VANS_SPLINE_TILE_BORDER;
        value+=(dx?fx:1-fx)*(dz?fz:1-fz)*tile->vegetationExclusion[pixel];
    }
    return std::clamp(value,0.f,1.f);
}

std::shared_ptr<const VansPcgSplineFieldSnapshot> VansPcgSplineFieldBuilder::Build(
    const VansPcgSplineAsset& asset, std::shared_ptr<const VansTerrainAsset> terrain,
    std::shared_ptr<const VansPcgSplineFieldSnapshot> previous, std::string& error)
{
    error.clear();
    const auto errors=ValidatePcgSplineAsset(asset,true);
    if (!errors.empty()) {error=errors.front();return {};}
    if (!terrain || !terrain->HasPixelData()) {error="Spline terrain pixels are unavailable.";return {};}
    const float size=terrain->settings.terrainSize;
    if (!(size>0) || !(terrain->settings.maxHeight>0)) {error="Terrain height mapping is invalid.";return {};}
    const double resolution=std::ceil(double(size)/asset.fieldTexelSize);
    if (resolution>16384 || resolution<2) {error="Spline field resolution must be between 2 and 16384.";return {};}
    auto output=std::make_shared<VansPcgSplineFieldSnapshot>();
    output->terrainGuid=asset.terrain;output->sourceFingerprint=VansPcgSplineAssetCodec::ContentHash(asset);
    output->terrainFingerprint=HashTerrainAssetContent(*terrain);output->worldSize=size;
    output->resolution=static_cast<std::uint32_t>(resolution);output->texelSize=size/output->resolution;
    if (previous && previous->terrainGuid==asset.terrain && previous->sourceFingerprint==output->sourceFingerprint &&
        previous->terrainFingerprint==output->terrainFingerprint) return previous;
    if (previous && (previous->resolution!=output->resolution || previous->worldSize!=size || previous->terrainGuid!=asset.terrain)) previous.reset();

    output->hasVegetationExclusion=std::any_of(asset.splines.begin(),asset.splines.end(),[](const auto& s){return s.enabled && s.points.size()>=2 && s.excludeVegetation;});
    std::vector<Curve> curves;
    for (const auto& spline:asset.splines) if (spline.enabled && spline.points.size()>=2)
    {
        if (spline.kind==VansPcgSplineKind::River && spline.shoulder<4*output->texelSize)
        {error=spline.name+": bank transition must span at least four field texels to bury the water mask edge.";return {};}
        Curve curve;curve.source=&spline;
        if (!VansPcgSplineEvaluator::Evaluate(spline,asset.sampleSpacing,asset.curveTolerance,curve.evaluated,error))
        {error=spline.name+": "+error;return {};}
        auto identity=asset;identity.splines={spline};curve.hash=VansPcgSplineAssetCodec::ContentHash(identity);
        auto& shape=identity.splines.front();
        shape.excludeVegetation=false;shape.vegetationFade=2;shape.name.clear();shape.locked=false;
        curve.roadHash=VansPcgSplineAssetCodec::ContentHash(identity);
        shape.excludeVegetation=false;shape.vegetationFade=2;
        shape.name.clear();shape.locked=false;shape.material={};shape.surfaceOffset=0;shape.textureRepeat=1;
        shape.flowSign=1;shape.fadeInDistance=shape.fadeOutDistance=0;shape.coordinateOffset=0;shape.coordinateSign=1;
        shape.continuation=false;shape.envelopeOffset=shape.envelopeLength=0;shape.flowCycleSeconds=2;shape.normalFlowEnabled=false;
        for (auto& point:shape.points) point.speed=0;
        curve.terrainShapeHash=VansPcgSplineAssetCodec::ContentHash(identity);
        curves.push_back(std::move(curve));
    }
    std::sort(curves.begin(),curves.end(),[](const auto& a,const auto& b) {
        return a.source->priority!=b.source->priority?a.source->priority<b.source->priority:a.source->id<b.source->id;
    });

    const auto tileCount=(output->resolution+VANS_SPLINE_TILE_SIZE-1)/VANS_SPLINE_TILE_SIZE;
    const float tileMeters=output->texelSize*VANS_SPLINE_TILE_SIZE;
    std::map<std::uint64_t,std::vector<Candidate>> index;
    for (std::size_t ci=0;ci<curves.size();++ci)
    {
        const auto& curve=curves[ci];
        std::map<std::uint64_t,std::vector<std::size_t>> segments;
        for (std::size_t si=0;si+1<curve.evaluated.samples.size();++si)
        {
            const auto& a=curve.evaluated.samples[si];const auto& b=curve.evaluated.samples[si+1];
            const float radius=std::max({a.leftWidth,a.rightWidth,b.leftWidth,b.rightWidth})+
                std::max(curve.source->shoulder,curve.source->excludeVegetation?curve.source->vegetationFade:0.f)+2*output->texelSize;
            const auto minimum=glm::min(XZ(a.position),XZ(b.position))-radius;
            const auto maximum=glm::max(XZ(a.position),XZ(b.position))+radius;
            if (maximum.x < -size*.5f || maximum.y < -size*.5f || minimum.x >= size*.5f || minimum.y >= size*.5f) continue;
            const auto minTile=glm::clamp(glm::ivec2(glm::floor((minimum+size*.5f)/tileMeters)),glm::ivec2(0),glm::ivec2(tileCount-1));
            const auto maxTile=glm::clamp(glm::ivec2(glm::floor((maximum+size*.5f)/tileMeters)),glm::ivec2(0),glm::ivec2(tileCount-1));
            for (int z=minTile.y;z<=maxTile.y;++z) for (int x=minTile.x;x<=maxTile.x;++x)
                segments[VansPcgSplineFieldSnapshot::TileKey(x,z)].push_back(si);
        }
        for (auto& [key,list]:segments) index[key].push_back({ci,std::move(list)});
        if (curve.source->kind==VansPcgSplineKind::Road)
        {
            const auto found=previous?previous->roads.find(curve.source->id):output->roads.end();
            if (previous && found!=previous->roads.end() && found->second->fingerprint==curve.roadHash)
                output->roads.emplace(curve.source->id,found->second);
            else output->roads.emplace(curve.source->id,BuildRoad(curve));
        }
    }
    if (index.size()>VANS_SPLINE_MAX_ATLAS_PAGES) {error="Spline field resident tile budget exceeded (2048).";return {};}
    std::size_t domainPages=0;
    for (const auto& [key,candidates]:index)
    {
        const auto count=std::count_if(candidates.begin(),candidates.end(),[&](const auto& candidate) {
            return curves[candidate.curve].source->kind==VansPcgSplineKind::River;
        });
        if (count>VANS_SPLINE_MAX_DOMAINS_PER_TILE)
        {error="River coordinate domains exceed tile budget (8).";return {};}
        domainPages+=count;
        if (domainPages>VANS_SPLINE_MAX_ATLAS_PAGES)
        {error="River coordinate page budget exceeded (2048).";return {};}
    }
    auto effective=std::make_shared<VansTerrainAsset>(*terrain);
    const auto terrainRegion=[&](std::uint32_t tx,std::uint32_t tz) {
        const double x0=double(tx)*VANS_SPLINE_TILE_SIZE/output->resolution;
        const double z0=double(tz)*VANS_SPLINE_TILE_SIZE/output->resolution;
        const double x1=double(tx+1)*VANS_SPLINE_TILE_SIZE/output->resolution;
        const double z1=double(tz+1)*VANS_SPLINE_TILE_SIZE/output->resolution;
        return std::array<int,4>{std::max(0,int(std::ceil(x0*terrain->width-.5))),
            std::max(0,int(std::ceil(z0*terrain->height-.5))),
            std::min(int(terrain->width),int(std::ceil(x1*terrain->width-.5))),
            std::min(int(terrain->height),int(std::ceil(z1*terrain->height-.5)))};
    };
    for (const auto& [key,candidates]:index)
    {
        const auto tx=static_cast<std::uint32_t>(key),tz=static_cast<std::uint32_t>(key>>32);
        std::uint64_t fingerprint=14695981039346656037ull;
        std::uint64_t terrainShapeFingerprint=fingerprint;
        for (const auto& c:candidates)
        {
            Hash(fingerprint,curves[c.curve].hash);
            Hash(terrainShapeFingerprint,curves[c.curve].terrainShapeHash);
        }
        const auto old=previous?previous->FindTile(tx,tz):nullptr;
        const bool reuse=old && old->fingerprint==fingerprint;
        if (reuse) output->tiles[key]=previous->tiles.at(key);
        else
        {
            auto tile=std::make_shared<VansPcgSplineFieldTile>();tile->x=tx;tile->z=tz;tile->fingerprint=fingerprint;
            tile->terrainShapeFingerprint=terrainShapeFingerprint;
            constexpr auto count=VANS_SPLINE_TILE_EXTENT*VANS_SPLINE_TILE_EXTENT;
            tile->vegetationExclusion.resize(count);
            tile->heights.resize(count);tile->velocities.resize(count);tile->coverage.resize(count);
            std::vector<int> domainIndices(candidates.size(),-1);
            for (std::size_t ci=0;ci<candidates.size();++ci) if (curves[candidates[ci].curve].source->kind==VansPcgSplineKind::River)
            {
                if (tile->domains.size()>=VANS_SPLINE_MAX_DOMAINS_PER_TILE)
                {error="River coordinate domains exceed tile budget (8); split spatial coverage before publishing.";return {};}
                const auto& source=*curves[candidates[ci].curve].source;
                domainIndices[ci]=static_cast<int>(tile->domains.size());
                VansPcgRiverCoordinateTile domain;domain.splineId=source.id;
                domain.cycleSeconds=source.flowCycleSeconds;domain.flowEnabled=source.normalFlowEnabled;
                domain.coordinates.resize(count);domain.jacobians.resize(count);tile->domains.push_back(std::move(domain));
            }
            tile->minimumWaterHeight=std::numeric_limits<float>::max();tile->maximumWaterHeight=-tile->minimumWaterHeight;
            tile->minimumRiverWidth=std::numeric_limits<float>::max();
            for (std::uint32_t z=0;z<VANS_SPLINE_TILE_EXTENT;++z) for (std::uint32_t x=0;x<VANS_SPLINE_TILE_EXTENT;++x)
            {
                const auto pixel=std::size_t(z)*VANS_SPLINE_TILE_EXTENT+x;
                const glm::vec2 world=(glm::vec2(tx*VANS_SPLINE_TILE_SIZE,tz*VANS_SPLINE_TILE_SIZE)+
                    glm::vec2(x,z)+.5f-float(VANS_SPLINE_TILE_BORDER))*output->texelSize-size*.5f;
                float weightSum=0,flowWeightSum=0,heightSum=0,minimum=std::numeric_limits<float>::max(),maximum=-minimum;
                glm::vec2 velocitySum(0);
                for (std::size_t ci=0;ci<candidates.size();++ci)
                {
                    const auto& candidate=candidates[ci];const auto& curve=curves[candidate.curve];
                    const auto c=EvaluateAt(curve,candidate,world);
                    if (!c.valid) continue;
                    if (curve.source->excludeVegetation)
                    {
                        const float outside=std::max(-c.edgeDistance,c.endDistance);
                        const float exclusion=1-VansPcgSplineEvaluator::SmoothWeight(outside/curve.source->vegetationFade);
                        tile->vegetationExclusion[pixel]=std::max(tile->vegetationExclusion[pixel],exclusion);
                    }
                    const float deformation=DeformationWeight(curve,c);
                    tile->coverage[pixel].z=std::max(tile->coverage[pixel].z,deformation);
                    tile->coverage[pixel].w=std::max(tile->coverage[pixel].w,deformation);
                    if (curve.source->kind==VansPcgSplineKind::Road)
                    {
                        if (c.endDistance==0 && c.edgeDistance>=0)
                        {
                            tile->heights[pixel].x=c.sample.position.y+std::tan(glm::radians(c.sample.bankAngleDegrees))*c.lateral;
                            tile->coverage[pixel].x=1;
                        }
                        continue;
                    }
                    const float flowWeight=c.endDistance==0?VansPcgSplineEvaluator::SmoothWeight(c.edgeDistance/curve.source->blendWidth):0;
                    // Height coverage continues underneath the banks. The return to
                    // the original global water level must occur behind terrain.
                    const float buriedWidth=curve.source->shoulder*.5f;
                    const float surfaceBlend=std::min(curve.source->blendWidth,buriedWidth);
                    const float weight=VansPcgSplineEvaluator::SmoothWeight((c.edgeDistance+buriedWidth)/surfaceBlend)*
                        VansPcgSplineEvaluator::SmoothWeight((buriedWidth-c.endDistance)/surfaceBlend);
                    if (c.edgeDistance >= -2*output->texelSize && c.endDistance <= 2*output->texelSize)
                    {
                        auto& domain=tile->domains[domainIndices[ci]];
                        if (flowWeight>0 && glm::dot(c.jacobian,c.jacobian)<1e-9f)
                        {error="River coordinate frame folds inside coverage: "+curve.source->name;return {};}
                        domain.coordinates[pixel]={curve.source->coordinateOffset+curve.source->coordinateSign*c.sample.distance,
                            curve.source->coordinateSign*c.lateral,flowWeight,1};
                        domain.jacobians[pixel]=c.jacobian;
                    }
                    if (weight<=0) continue;
                    const float waterHeight=c.sample.position.y-curve.source->waterSurfaceDrop;
                    weightSum+=weight;heightSum+=weight*waterHeight;
                    velocitySum+=flowWeight*VansPcgSplineEvaluator::Velocity(*curve.source,c.sample,curve.evaluated.length);
                    flowWeightSum+=flowWeight;
                    minimum=std::min(minimum,waterHeight);maximum=std::max(maximum,waterHeight);
                    tile->coverage[pixel].y=std::max(tile->coverage[pixel].y,weight);
                    tile->minimumRiverWidth=std::min(tile->minimumRiverWidth,c.sample.leftWidth+c.sample.rightWidth);
                }
                if (weightSum>0)
                {
                    tile->heights[pixel].y=heightSum/weightSum;tile->velocities[pixel]=flowWeightSum>0?velocitySum/flowWeightSum:glm::vec2(0);
                    tile->hasRiver=true;tile->minimumWaterHeight=std::min(tile->minimumWaterHeight,tile->heights[pixel].y);
                    tile->maximumWaterHeight=std::max(tile->maximumWaterHeight,tile->heights[pixel].y);
                    tile->maximumHeightConflict=std::max(tile->maximumHeightConflict,maximum-minimum);
                }
            }
            if (!tile->hasRiver) tile->minimumWaterHeight=tile->maximumWaterHeight=tile->minimumRiverWidth=0;
            output->tiles[key]=std::move(tile);output->changedTiles.push_back(key);++output->rebuiltTileCount;
        }
        const auto& tile=*output->tiles.at(key);
        if (tile.maximumHeightConflict>asset.heightConflictThreshold)
            output->warnings.push_back("River height difference exceeds threshold in tile "+std::to_string(tx)+","+std::to_string(tz)+
                " ("+std::to_string(tile.maximumHeightConflict)+" m).");
        const auto region=terrainRegion(tx,tz);
        for (int z=region[1];z<region[3];++z) for (int x=region[0];x<region[2];++x)
        {
            const auto pixel=std::size_t(z)*terrain->width+x;
            if (old && old->terrainShapeFingerprint==terrainShapeFingerprint && previous->terrainFingerprint==output->terrainFingerprint)
            {effective->heights[pixel]=previous->effectiveTerrain->heights[pixel];continue;}
            const glm::vec2 world((float(x)+.5f)/terrain->width*size-size*.5f,(float(z)+.5f)/terrain->height*size-size*.5f);
            float h=terrain->heights[pixel]*(terrain->settings.maxHeight/65535.0f)+terrain->settings.heightOffset;
            for (const auto& candidate:candidates) h=DeformHeight(h,curves[candidate.curve],EvaluateAt(curves[candidate.curve],candidate,world));
            const float normalized=(h-terrain->settings.heightOffset)/terrain->settings.maxHeight;
            if (normalized<0 || normalized>1) {error="Spline deformation lies outside terrain's encodable height range.";return {};}
            effective->heights[pixel]=static_cast<std::uint16_t>(std::lround(normalized*65535));
        }
    }
    if (previous) for (const auto& [key,tile]:previous->tiles)
        if (!output->tiles.count(key)) output->changedTiles.push_back(key);
    const auto surfaceHeight=[&](glm::vec2 world) {
        const auto coordinate=(world/size+.5f)*glm::vec2(effective->width,effective->height)-.5f;
        const auto origin=glm::ivec2(glm::floor(coordinate));const auto fraction=glm::fract(coordinate);
        float height=0;
        for (int z=0;z<2;++z) for (int x=0;x<2;++x)
        {
            const auto at=glm::clamp(origin+glm::ivec2(x,z),glm::ivec2(0),glm::ivec2(effective->width-1,effective->height-1));
            height+=(x?fraction.x:1-fraction.x)*(z?fraction.y:1-fraction.y)*
                effective->heights[std::size_t(at.y)*effective->width+at.x];
        }
        return height*(effective->settings.maxHeight/65535.f)+effective->settings.heightOffset;
    };
    for (const auto& [key,tile]:output->tiles) if (tile->hasRiver)
    {
        float deficit=0;glm::vec3 position{};
        for (std::uint32_t z=1;z<=VANS_SPLINE_TILE_SIZE;++z) for (std::uint32_t x=1;x<=VANS_SPLINE_TILE_SIZE;++x)
        {
            const auto pixel=std::size_t(z)*VANS_SPLINE_TILE_EXTENT+x;
            if (tile->coverage[pixel].y<=0 || tile->coverage[pixel].y>.15f) continue;
            const glm::vec2 world=(glm::vec2(tile->x*VANS_SPLINE_TILE_SIZE,tile->z*VANS_SPLINE_TILE_SIZE)+glm::vec2(x,z)-.5f)*output->texelSize-size*.5f;
            const float missing=tile->heights[pixel].y+.05f-surfaceHeight(world);
            if (missing>deficit) {deficit=missing;position={world.x,tile->heights[pixel].y+.1f,world.y};}
        }
        if (deficit>0)
        {
            output->uncoveredBankPoints.push_back(position);
            output->warnings.push_back("Bank cannot cover river water near ("+std::to_string(position.x)+", "+std::to_string(position.z)+
                "). Lower the spline water level; terrain is never raised (clearance short by "+std::to_string(deficit)+" m).");
        }
    }
    // Motion-only edits must not invalidate terrain collision, uploads or vegetation.
    if (previous && previous->terrainFingerprint==output->terrainFingerprint &&
        previous->effectiveTerrain->heights==effective->heights)
        output->effectiveTerrain=previous->effectiveTerrain;
    else output->effectiveTerrain=std::move(effective);
    return output;
}
}
