#ifndef CLOTH_AREA_LIGHTING_INCLUDED
#define CLOTH_AREA_LIGHTING_INCLUDED

#include "ClothLightingLUT.generated.glsl"
layout(set = 0, binding = 44) uniform sampler2D ClothLightingLUT;

struct ClothLTCDistribution
{
    mat3 inverseMatrix;
    float hemisphereMass;
};

struct ClothLTCLobe
{
    ClothLTCDistribution first;
    ClothLTCDistribution second;
    vec2 weights;
    float amplitude;
};

// 对数正对角的三角回代保持窄峰可逆；不使用任意矩阵插值或通用 inverse。
ClothLTCDistribution DecodeClothLTC(vec4 a, vec4 b, float axisZ, mat3 orientation)
{
    vec3 d = exp(vec3(a.x, a.z, b.y));
    mat3 factor = mat3(vec3(d.x, a.y, a.w), vec3(0.0, d.y, b.x), vec3(0.0, 0.0, d.z));
    mat3 inverseFactor = mat3(
        vec3(1.0 / d.x, -a.y / d.x / d.y, (a.y * b.x / d.y - a.w) / d.x / d.z),
        vec3(0.0, 1.0 / d.y, -b.x / d.y / d.z), vec3(0.0, 0.0, 1.0 / d.z));
    vec3 axis = normalize(vec3(b.zw, axisZ));
    vec3 helper = abs(axis.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(axis, helper));
    mat3 rotation = mat3(tangent, cross(axis, tangent), axis);
    mat3 matrix = orientation * factor * rotation;
    vec3 physicalNormal = vec3(matrix[0].z, matrix[1].z, matrix[2].z);
    ClothLTCDistribution result;
    result.inverseMatrix = transpose(rotation) * inverseFactor * transpose(orientation);
    result.hemisphereMass = 0.5 * (1.0 + matrix[2].z / length(physicalNormal));
    return result;
}

mat3 ClothReflectionFrame(float NoV, float phi)
{
    float c = cos(phi), s = sin(phi), radial = sqrt(max(1.0 - NoV * NoV, 0.0));
    return mat3(vec3(NoV*c, NoV*s, radial), vec3(-s, c, 0.0), vec3(-radial*c, -radial*s, NoV));
}

ClothLTCLobe SampleClothLTCLobe(float roughness, float NoV, float anisotropy, float phi,
    bool charlie, mat3 orientation)
{
    vec2 coordinate = vec2(clamp((roughness - 0.045) / 0.955, 0.0, 1.0),
        pow(clamp((NoV - 0.0001) / 0.9999, 0.0, 1.0), 1.0 / 3.0));
    vec4 coefficients[6];
    for (int k = 0; k < 6; ++k) coefficients[k] = vec4(0.0);
    vec2 position = vec2(clamp(anisotropy / 0.95, 0.0, 1.0) * float(CLOTH_LTC_ANISOTROPYNODES - 1),
        clamp(phi * 2.0 / PI, 0.0, 1.0) * float(CLOTH_LTC_AZIMUTHNODES - 1));
    ivec2 low = ivec2(floor(position));
    ivec2 high = min(low + 1, ivec2(CLOTH_LTC_ANISOTROPYNODES - 1, CLOTH_LTC_AZIMUTHNODES - 1));
    vec2 blend = position - vec2(low);
    int corners = charlie ? 1 : 4;
    for (int corner = 0; corner < corners; ++corner)
    {
        bool upperA = (corner & 1) != 0, upperP = (corner & 2) != 0;
        int a = upperA ? high.x : low.x, p = upperP ? high.y : low.y;
        float weight = charlie ? 1.0 : (upperA ? blend.x : 1.0 - blend.x) * (upperP ? blend.y : 1.0 - blend.y);
        int startRow = charlie ? CLOTH_LTC_ANISOTROPYNODES * CLOTH_LTC_AZIMUTHNODES * CLOTH_LTC_NOVNODES :
            (a * CLOTH_LTC_AZIMUTHNODES + p) * CLOTH_LTC_NOVNODES;
        int viewNodes = charlie ? CLOTH_LTC_CHARLIENOVNODES : CLOTH_LTC_NOVNODES;
        for (int k = 0; k < 6; ++k)
        {
            vec2 uv = (vec2(float(k * CLOTH_LTC_ROUGHNESSNODES) + coordinate.x * float(CLOTH_LTC_ROUGHNESSNODES - 1),
                float(startRow) + coordinate.y * float(viewNodes - 1)) + 0.5) / vec2(CLOTH_LTC_WIDTH, CLOTH_LTC_HEIGHT);
            coefficients[k] += textureLod(ClothLightingLUT, uv, 0.0) * weight;
        }
    }
    ClothLTCLobe result;
    result.first = DecodeClothLTC(coefficients[0], coefficients[1], coefficients[2].x, orientation);
    result.second = DecodeClothLTC(vec4(coefficients[2].zw, coefficients[3].xy),
        vec4(coefficients[3].zw, coefficients[4].xy), coefficients[4].z, orientation);
    result.weights = vec2(coefficients[2].y, coefficients[4].w);
    result.amplitude = coefficients[5].x;
    // 近似幅值共享直接 BSDF 的方向能量预算，避免粗网格插值超过该预算。
    if (charlie)
        result.amplitude = min(result.amplitude,ClothDirectionalAlbedo(NoV,roughness));
    else
    {
        const float Fmean = 0.04 + 0.96 / 21.0;
        float Fv = ClothFresnelSchlick(NoV,vec3(0.04)).x;
        result.amplitude = min(result.amplitude,1.0-(1.0-Fv)*(1.0-Fmean));
    }
    return result;
}

int ClipClothPolygon(vec3 vertices[8], int count, out vec3 result[8])
{
    int size = 0;
    if (count == 0) return size;
    vec3 previous = vertices[count - 1];
    for (int i = 0; i < count; ++i)
    {
        vec3 current = vertices[i];
        bool oldIn = previous.z >= 0.0, newIn = current.z >= 0.0;
        if (oldIn != newIn)
            result[size++] = previous + (previous.z / (previous.z - current.z)) * (current - previous);
        if (newIn) result[size++] = current;
        previous = current;
    }
    return size;
}

float IntegrateClothLTC(vec3 corners[4], ClothLTCDistribution distribution)
{
    vec3 vertices[8], clipped[8];
    for (int i = 0; i < 4; ++i) vertices[i] = corners[i];
    int count = ClipClothPolygon(vertices, 4, clipped);
    for (int i = 0; i < count; ++i) vertices[i] = distribution.inverseMatrix * clipped[i];
    count = ClipClothPolygon(vertices, count, clipped);
    if (count < 3) return 0.0;
    for (int i = 0; i < count; ++i) clipped[i] = normalize(clipped[i]);
    float integral = 0.0;
    for (int i = 0; i < count; ++i)
    {
        vec3 a = clipped[i], b = clipped[(i + 1) % count], edge = cross(a, b);
        float sine = length(edge);
        integral += edge.z * atan(sine, clamp(dot(a, b), -1.0, 1.0)) / max(sine, 1e-20);
    }
    return clamp(abs(integral) / (2.0 * PI * distribution.hemisphereMass), 0.0, 1.0);
}

float IntegrateClothLobe(vec3 corners[4], ClothLTCLobe lobe)
{
    return lobe.amplitude * (lobe.weights.x * IntegrateClothLTC(corners, lobe.first) +
        lobe.weights.y * IntegrateClothLTC(corners, lobe.second));
}

// 球面多边形边界递推：I[k] = integral(NoL^k dOmega)。Fresnel 五次项可直接积分。
void ClothCosineMoments(vec3 corners[4], out float moments[7])
{
    vec3 vertices[8], clipped[8];
    for (int i = 0; i < 4; ++i) vertices[i] = corners[i];
    int count = ClipClothPolygon(vertices, 4, clipped);
    for (int k = 0; k < 7; ++k) moments[k] = 0.0;
    if (count < 3) return;
    for (int i = 0; i < count; ++i) clipped[i] = normalize(clipped[i]);
    float boundary[6];
    for (int k = 0; k < 6; ++k) boundary[k] = 0.0;
    for (int i = 0; i < count; ++i)
    {
        vec3 a = clipped[i], b = clipped[(i + 1) % count], crossAB = cross(a, b);
        float sine = length(crossAB), cosine = dot(a, b);
        if (sine <= 1e-12) continue;
        float nz = crossAB.z / sine;
        float edge[6];
        edge[0] = atan(sine, cosine);
        float start = (b.z - cosine * a.z) / sine, end = (cosine * b.z - a.z) / sine;
        edge[1] = start - end;
        float az = a.z, bz = b.z;
        for (int k = 2; k < 6; ++k)
        {
            edge[k] = float(k - 1) / float(k) * (1.0 - nz*nz) * edge[k-2] - (bz*end - az*start) / float(k);
            az *= a.z; bz *= b.z;
        }
        for (int k = 0; k < 6; ++k) boundary[k] += nz * edge[k];
    }
    for (int i = 1; i < count - 1; ++i)
    {
        vec3 a = clipped[0], b = clipped[i], c = clipped[i + 1];
        moments[0] += 2.0 * atan(dot(a, cross(b, c)), 1.0 + dot(a,b) + dot(b,c) + dot(c,a));
    }
    for (int k = 1; k < 7; ++k)
    {
        moments[k] = boundary[k-1] / float(k+1);
        if (k > 1) moments[k] += float(k-1) / float(k+1) * moments[k-2];
    }
    float signValue = moments[1] < 0.0 ? -1.0 : 1.0;
    for (int k = 0; k < 7; ++k) moments[k] *= signValue;
}

float IntegrateClothDiffuse(vec3 corners[4], float NoV, ClothMaterialPayload cloth)
{
    float m[7]; ClothCosineMoments(corners, m);
    if (m[1] <= 1e-12) return 0.0;
    float mean = clamp(m[2] / m[1], 0.0, 1.0);
    float variance = max(m[3] / m[1] - mean*mean, 0.0);
    vec2 NoL = vec2(mean), weights = vec2(0.5);
    // 接近点光的方差低于浮点消减尺度时，余量用该方向的局部近似。
    if (variance > 1e-5)
    {
        float skew = (m[4] / m[1] - 3.0 * mean * m[3] / m[1] + 2.0 * mean*mean*mean) / variance;
        float spread = sqrt(skew*skew + 4.0*variance);
        NoL = clamp(vec2(mean + (skew-spread)*0.5, mean + (skew+spread)*0.5), 0.0, 1.0);
        float delta = NoL.y - NoL.x;
        if (delta > 1e-6) weights = vec2(NoL.y-mean, mean-NoL.x) / delta;
    }
    float Fmean = clamp(0.04 + 0.96 * (m[1]-5.0*m[2]+10.0*m[3]-10.0*m[4]+5.0*m[5]-m[6]) / m[1], 0.04, 1.0);
    float Ev = ClothDirectionalAlbedo(NoV, cloth.transmittanceRoughness.a);
    vec2 residual = max(vec2(ClothDirectionalAlbedo(NoL.x, cloth.transmittanceRoughness.a),
        ClothDirectionalAlbedo(NoL.y, cloth.transmittanceRoughness.a)) - Ev, 0.0);
    vec2 Fl = vec2(ClothFresnelSchlick(NoL.x, vec3(0.04)).x, ClothFresnelSchlick(NoL.y, vec3(0.04)).x);
    float sw = Max3Cloth(ClothSheenColor(cloth)), spec = cloth.surface.x;
    float attenuation = (1.0-spec*Fmean) * (1.0-sw*Ev) - sw * dot(weights, (1.0-spec*Fl)*residual);
    return max((1.0-spec*ClothFresnelSchlick(NoV, vec3(0.04)).x)*attenuation*m[1]/PI, 0.0);
}

struct ClothAreaLightingData
{
    mat3 worldToFrame;
    vec3 view;
    float NoV;
    ClothLTCLobe ggx;
    ClothLTCLobe charlie;
};

ClothAreaLightingData PrepareClothAreaLighting(ClothShadingData surface, ClothMaterialPayload cloth, vec3 tangent)
{
    vec3 N, T, B;
    BuildClothOpticalFrame(surface.normal, surface.viewDirection, tangent, N, T, B);
    ClothAreaLightingData result;
    result.worldToFrame = transpose(mat3(T, B, N));
    result.view = result.worldToFrame * normalize(surface.viewDirection);
    result.NoV = clamp(result.view.z, 0.0001, 1.0);
    float phi = atan(abs(result.view.y), abs(result.view.x));
    if (dot(result.view.xy, result.view.xy) <= 1e-12) phi = 0.0;
    mat3 signs = mat3(vec3(result.view.x < 0.0 ? -1.0 : 1.0, 0.0, 0.0),
        vec3(0.0, result.view.y < 0.0 ? -1.0 : 1.0, 0.0), vec3(0.0, 0.0, 1.0));
    result.ggx = SampleClothLTCLobe(clamp(surface.roughness, 0.045, 1.0), result.NoV,
        cloth.surface.y, phi, false, signs * ClothReflectionFrame(result.NoV, phi));
    float azimuth = dot(result.view.xy, result.view.xy) > 1e-12 ? atan(result.view.y, result.view.x) : 0.0;
    mat3 rotation = mat3(vec3(cos(azimuth), sin(azimuth), 0.0),
        vec3(-sin(azimuth), cos(azimuth), 0.0), vec3(0.0, 0.0, 1.0));
    result.charlie = SampleClothLTCLobe(cloth.transmittanceRoughness.a, result.NoV, 0.0, 0.0, true, rotation);
    return result;
}

// 光学积分只消费相对着色点的矩形几何；发光资源、阴影、cookie 归外层灯光入口。
void EvaluateClothRectangle(ClothAreaLightingData data, ClothMaterialPayload cloth, vec3 albedo,
    vec3 center, vec3 halfRight, vec3 halfUp, out vec3 diffuse, out vec3 ggx, out vec3 sheen)
{
    vec3 corners[4];
    corners[0] = center-halfRight-halfUp; corners[1] = center+halfRight-halfUp;
    corners[2] = center+halfRight+halfUp; corners[3] = center-halfRight+halfUp;
    vec3 tau = cloth.transmittanceRoughness.rgb;
    diffuse = albedo * IntegrateClothDiffuse(corners, data.NoV, cloth) * (1.0-tau);
    if (Max3Cloth(tau) > 0.0)
    {
        vec3 back[4]; for (int i=0;i<4;++i) back[i] = -corners[i];
        ClothLTCDistribution lambert; lambert.inverseMatrix = mat3(1.0); lambert.hemisphereMass = 1.0;
        diffuse += tau * IntegrateClothLTC(back, lambert);
    }
    vec3 emitterNormal = normalize(cross(halfRight, halfUp));
    vec3 reflected = vec3(-data.view.xy, data.NoV);
    float planeDot = dot(reflected, emitterNormal);
    float distance = abs(planeDot) > 1e-10 ? dot(center, emitterNormal) / planeDot : -1.0;
    vec3 hit = distance > 0.0 ? reflected*distance-center : vec3(0.0);
    float width = length(halfRight), height = length(halfUp);
    vec3 R = halfRight/width, U = halfUp/height;
    vec3 closest = center + R*clamp(dot(hit,R),-width,width) + U*clamp(dot(hit,U),-height,height);
    float Ev = ClothDirectionalAlbedo(data.NoV, cloth.transmittanceRoughness.a);
    float baseScale = max(1.0-Max3Cloth(ClothSheenColor(cloth))*max(Ev,
        ClothDirectionalAlbedo(max(normalize(closest).z,0.0),cloth.transmittanceRoughness.a)),0.0);
    ggx = vec3(IntegrateClothLobe(corners,data.ggx) * cloth.surface.x * baseScale) * (1.0-tau);
    float sheenIntegral;
    vec3 centerDirection = normalize(center);
    float minimumDot = 1.0, minimumHeight = corners[0].z;
    for (int i=0;i<4;++i)
    {
        minimumDot = min(minimumDot,dot(normalize(corners[i]),centerDirection));
        minimumHeight = min(minimumHeight,corners[i].z);
    }
    // 小角度光源用九点 Gauss 求积保留 Sheen 尾部，大片光源用归一化 LTC。
    if (minimumDot > cos(0.75*cloth.transmittanceRoughness.a) && minimumHeight > 0.0)
    {
        sheenIntegral = 0.0;
        const float nodes[3] = float[3](-0.77459666924,0.0,0.77459666924);
        const float weights[3] = float[3](0.55555555556,0.88888888889,0.55555555556);
        float viewNormalization = ClothSheenIntegral(data.NoV,cloth.transmittanceRoughness.a).x;
        for (int j=0;j<3;++j) for (int i=0;i<3;++i)
        {
            vec3 point = center + nodes[i]*halfRight + nodes[j]*halfUp;
            vec3 L = normalize(point), H = normalize(L+data.view);
            vec2 lightEnergy = ClothSheenIntegral(L.z,cloth.transmittanceRoughness.a);
            float response = D_Charlie(cloth.transmittanceRoughness.a,max(H.z,0.0)) *
                V_Charlie(cloth.transmittanceRoughness.a,data.NoV,L.z) * min(viewNormalization,lightEnergy.x) * L.z;
            sheenIntegral += response * abs(dot(L,emitterNormal)) / dot(point,point) * width * height * weights[i] * weights[j];
        }
    }
    else sheenIntegral = IntegrateClothLobe(corners,data.charlie);
    sheen = ClothSheenColor(cloth) * sheenIntegral * (1.0-tau);
}

#endif
