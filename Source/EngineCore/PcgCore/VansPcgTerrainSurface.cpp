#include "VansPcgTerrainSurface.h"
#include "../TerrainCore/VansTerrainAsset.h"
#include <algorithm>
#include <cmath>
namespace Vans
{
VansPcgSurfaceSampler CreatePcgTerrainSurface(std::shared_ptr<const VansTerrainAsset> terrain, std::string& error)
{
    error.clear();
    if (!terrain || terrain->width < 2 || terrain->height < 2 ||
        terrain->heights.size() != static_cast<size_t>(terrain->width) * terrain->height ||
        !std::isfinite(terrain->settings.terrainSize) || terrain->settings.terrainSize <= 0 ||
        !std::isfinite(terrain->settings.maxHeight) || !std::isfinite(terrain->settings.heightOffset))
    {
        error = "The bound terrain heightfield is unavailable in memory.";
        return {};
    }
    return [asset = std::move(terrain)](float x, float z, VansPcgSurfacePoint& point) {
        const float size = asset->settings.terrainSize;
        const float half = size * 0.5f;
        if (!std::isfinite(x) || !std::isfinite(z) || x < -half || x > half || z < -half || z > half) return false;
        const float px = std::clamp((x / size + 0.5f) * asset->width - 0.5f, 0.0f, static_cast<float>(asset->width - 1));
        const float pz = std::clamp((z / size + 0.5f) * asset->height - 0.5f, 0.0f, static_cast<float>(asset->height - 1));
        const uint32_t x0 = static_cast<uint32_t>(px), z0 = static_cast<uint32_t>(pz);
        const uint32_t x1 = std::min(x0 + 1, asset->width - 1), z1 = std::min(z0 + 1, asset->height - 1);
        const float tx = px - x0, tz = pz - z0;
        const auto sample = [&](uint32_t ix, uint32_t iz) {
            return static_cast<float>(asset->heights[static_cast<size_t>(iz) * asset->width + ix]) *
                (asset->settings.maxHeight / 65535.0f) + asset->settings.heightOffset;
        };
        const float h00 = sample(x0,z0), h10 = sample(x1,z0), h01 = sample(x0,z1), h11 = sample(x1,z1);
        point.height = (h00 + (h10-h00)*tx) * (1-tz) + (h01 + (h11-h01)*tx) * tz;
        const float dx = (x / size + 0.5f) * asset->width < 0.5f ? 0.0f :
            ((h10-h00)*(1-tz) + (h11-h01)*tz) * asset->width / size;
        const float dz = (z / size + 0.5f) * asset->height < 0.5f ? 0.0f :
            ((h01-h00)*(1-tx) + (h11-h10)*tx) * asset->height / size;
        const float length = std::sqrt(dx*dx + 1 + dz*dz);
        point.normal = {-dx/length, 1/length, -dz/length};
        return true;
    };
}

bool RaycastPcgTerrainSurface(std::shared_ptr<const VansTerrainAsset> terrain,
    const std::array<float,3>& origin, const std::array<float,3>& direction,
    float maximumDistance, VansPcgSurfaceHit& hit)
{
    std::string error;
    const auto sample = CreatePcgTerrainSurface(terrain, error);
    double norm = 0;
    for (std::size_t axis=0; axis<3; ++axis) {
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return false;
        norm += static_cast<double>(direction[axis])*direction[axis];
    }
    if (!sample || norm < 1e-20 || !std::isfinite(maximumDistance) || maximumDistance <= 0) return false;
    norm = std::sqrt(norm);
    std::array<double,3> ray{direction[0]/norm,direction[1]/norm,direction[2]/norm};
    const double size = terrain->settings.terrainSize, half = size*0.5;
    const double h0 = terrain->settings.heightOffset, h1 = h0+terrain->settings.maxHeight;
    const std::array<double,3> minimum{-half,std::min(h0,h1),-half};
    const std::array<double,3> maximum{half,std::max(h0,h1),half};
    double enter=0, leave=maximumDistance;
    for (std::size_t axis=0;axis<3;++axis) {
        if (std::abs(ray[axis])<1e-12) {
            if (origin[axis]<minimum[axis] || origin[axis]>maximum[axis]) return false;
        } else {
            double a=(minimum[axis]-origin[axis])/ray[axis], b=(maximum[axis]-origin[axis])/ray[axis];
            if (a>b) std::swap(a,b);
            enter=std::max(enter,a); leave=std::min(leave,b);
            if (enter>leave) return false;
        }
    }
    const auto distance = [&](double t) {
        VansPcgSurfacePoint point;
        const float x=static_cast<float>(std::clamp(origin[0]+ray[0]*t,-half,half));
        const float z=static_cast<float>(std::clamp(origin[2]+ray[2]*t,-half,half));
        sample(x,z,point);
        return origin[1]+ray[1]*t-point.height;
    };
    const auto publish = [&](double t) {
        hit.position={static_cast<float>(origin[0]+ray[0]*t),
            static_cast<float>(origin[1]+ray[1]*t),static_cast<float>(origin[2]+ray[2]*t)};
        VansPcgSurfacePoint point;
        if (!sample(std::clamp(hit.position[0],static_cast<float>(-half),static_cast<float>(half)),
            std::clamp(hit.position[2],static_cast<float>(-half),static_cast<float>(half)),point)) return false;
        hit.position[1]=point.height; hit.normal=point.normal;
        return true;
    };
    // 按线性采样单元切分射线。每段高度是二次函数，可找到同一单元内两次交点，
    // 不会像固定步长前后符号检测那样漏掉薄脊或切线接触。
    std::vector<double> cuts{enter,leave};
    for (std::size_t axis : {std::size_t(0),std::size_t(2)}) {
        if (std::abs(ray[axis])<1e-12) continue;
        const uint32_t count=axis==0?terrain->width:terrain->height;
        const double p0=(origin[axis]+ray[axis]*enter+half)/size*count-.5;
        const double p1=(origin[axis]+ray[axis]*leave+half)/size*count-.5;
        const int first=static_cast<int>(std::max(0.0,std::ceil(std::min(p0,p1))));
        const int last=static_cast<int>(std::min(static_cast<double>(count-1),std::floor(std::max(p0,p1))));
        for (int grid=first;grid<=last;++grid) {
            const double t=(((grid+.5)/count-.5)*size-origin[axis])/ray[axis];
            if (t>enter && t<leave) cuts.push_back(t);
        }
    }
    std::sort(cuts.begin(),cuts.end());
    constexpr double tolerance=1e-5;
    if (std::abs(distance(enter))<=tolerance) return publish(enter);
    for (std::size_t i=1;i<cuts.size();++i) {
        const double begin=cuts[i-1], end=cuts[i];
        if (end-begin<1e-10) continue;
        const double c=distance(begin), mid=distance((begin+end)*.5), f1=distance(end);
        const double a=2*(c+f1-2*mid), b=f1-c-a;
        double roots[2]{2,2};
        if (std::abs(a)<1e-8) {
            if (std::abs(b)>1e-12) roots[0]=-c/b;
        } else {
            const double discriminant=b*b-4*a*c;
            if (discriminant>=-1e-10) {
                const double d=std::sqrt(std::max(0.0,discriminant));
                roots[0]=(-b-d)/(2*a); roots[1]=(-b+d)/(2*a);
                if (roots[0]>roots[1]) std::swap(roots[0],roots[1]);
            }
        }
        for (double root : roots)
            if (root>=-1e-7 && root<=1+1e-7)
                return publish(begin+(end-begin)*std::clamp(root,0.0,1.0));
    }
    return false;
}

}
