#ifndef VANS_IES_PROFILE_GLSL
#define VANS_IES_PROFILE_GLSL
// 调用方显式定义 IES_PROFILE_SET 与 IES_PROFILE_BINDING，所有材质共用方向参数化。
layout(set=IES_PROFILE_SET,binding=IES_PROFILE_BINDING) uniform sampler2DArray iesProfileTexture;
float SampleIESProfile(int profileIndex, vec3 lightDir, vec3 nadirDir)
{
    // 垂直角 φ：lightDir 与 nadirDir 的夹角
    // dot(lightDir, nadirDir) = cos(φ)，范围 [-1, 1]
    float cosVert = clamp(dot(lightDir, nadirDir), -1.0, 1.0);
    float uv_y    = cosVert * 0.5 + 0.5;  // [0,1]，0 = zenith，1 = nadir

    // 水平角 θ：将 lightDir 投影到垂直于 nadirDir 的平面，再用 atan2 计算角度
    vec3  projOnPlane = normalize(lightDir - cosVert * nadirDir + vec3(1e-8));

    // 构建参考系（任意与 nadirDir 正交的向量作为 θ=0 参考方向）
    vec3  refX = normalize(abs(nadirDir.z) < 0.99 ? cross(nadirDir, vec3(0.0, 0.0, 1.0))
                                                   : cross(nadirDir, vec3(0.0, 1.0, 0.0)));
    vec3  refY = cross(nadirDir, refX);
    float theta = atan(dot(projOnPlane, refY), dot(projOnPlane, refX));  // [-π, π]
    float uv_x  = theta * INV_TWO_PI + 0.5;  // [0, 1]

    return texture(iesProfileTexture, vec3(uv_x, uv_y, float(profileIndex))).r;
}

#endif
