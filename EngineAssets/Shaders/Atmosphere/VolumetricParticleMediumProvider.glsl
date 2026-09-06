// 粒子介质提供者只依赖粒子候选与 NearMedia 的通用介质接口。
// 它不读取 LocalVolumetricFog 组件、候选表或字段纹理。
struct VolumetricParticle
{
    vec4 worldPositionRadius;
    vec4 scatteringAlbedoExtinction;
    vec4 emissiveAnisotropy;
    vec4 lightingEdgeCloud;
    vec4 distanceAndPadding;
    uvec4 metadata;
};

layout(set = 1, binding = 13, std430) readonly buffer VolumetricParticles
{
    VolumetricParticle particles[];
} uParticles;
layout(set = 1, binding = 14, std430) readonly buffer VolumetricParticleTileHeaders
{
    uvec2 headers[];
} uParticleTileHeaders;
layout(set = 1, binding = 15, std430) readonly buffer VolumetricParticleTileIndices
{
    uint indices[];
} uParticleTileIndices;
layout(set = 1, binding = 16) uniform VolumetricParticleParams
{
    uvec4 particleCountCandidateLimitAndGrid;
} uParticleParams;
layout(set = 1, binding = 17, r16f) uniform writeonly image3D currentParticleActivity;

float ParticleRadialDensity(float axialDistance,
    float perpendicularDistance, float radius, float edgeSoftness)
{
    float normalizedRadius = sqrt(
        perpendicularDistance * perpendicularDistance +
        axialDistance * axialDistance) / max(radius, 1.0e-5);
    if (edgeSoftness <= 1.0e-5)
        return normalizedRadius < 1.0 ? 1.0 : 0.0;
    float edgeStart = max(1.0 - edgeSoftness, 0.0);
    return 1.0 - smoothstep(edgeStart, 1.0, normalizedRadius);
}

// 对粒子球体与 Froxel 深度段的实际相交弦做确定性积分。
// 四点 Gauss-Legendre 积分不会把整个体素的密度交给一个随机点决定，
// 并且保持每帧稳定；时间历史只需处理真实的粒子运动和生命周期变化。
float IntegrateParticleDensityOverFroxel(
    float overlapStartDistance, float overlapEndDistance,
    float particleDistanceAlongRay, float perpendicularDistance,
    float radius, float edgeSoftness)
{
    const float nodes[4] = float[](
        -0.8611363116, -0.3399810436,
         0.3399810436,  0.8611363116);
    const float weights[4] = float[](
        0.3478548451, 0.6521451549,
        0.6521451549, 0.3478548451);
    float midpoint = 0.5 * (overlapStartDistance + overlapEndDistance);
    float halfLength = 0.5 * (overlapEndDistance - overlapStartDistance);
    float averageDensity = 0.0;
    for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
    {
        float distance = midpoint + halfLength * nodes[sampleIndex];
        averageDensity += weights[sampleIndex] * ParticleRadialDensity(
            distance - particleDistanceAlongRay,
            perpendicularDistance, radius, edgeSoftness);
    }
    return 0.5 * averageDensity;
}

float InjectVolumetricParticleMedium(
    inout NearMediaMaterialAccumulation accumulation,
    ivec3 voxel,
    ivec3 gridSize,
    vec3 rayDirection,
    float sliceStartDistance,
    float sliceEndDistance,
    float sliceThickness,
    float froxelFootprintRadius)
{
    float activity = 0.0;
    uint tileIndex = uint(voxel.y) * uint(gridSize.x) + uint(voxel.x);
	uvec2 header = uParticleTileHeaders.headers[tileIndex];
	uint particleCount =
		uParticleParams.particleCountCandidateLimitAndGrid.x;
	bool candidateOverflow = header.y == 0xffffffffu;
	uint candidateCount = candidateOverflow
		? particleCount
		: min(header.y, uParticleParams.particleCountCandidateLimitAndGrid.y);
	for (uint candidateIndex = 0u;
		candidateIndex < candidateCount; ++candidateIndex)
	{
		// 溢出时扫描全部粒子并沿用下面的深度范围与球/Froxel
		// 相交测试，保证介质贡献完整且没有 Tile 边界断层。
		uint particleIndex = candidateOverflow
			? candidateIndex
			: uParticleTileIndices.indices[header.x + candidateIndex];
        if (particleIndex >= particleCount)
            continue;
        VolumetricParticle particle = uParticles.particles[particleIndex];
        if (uint(voxel.z) < particle.metadata.y ||
            uint(voxel.z) > particle.metadata.z)
        {
            continue;
        }
        float radius = max(particle.worldPositionRadius.w, 1.0e-5);
        vec3 cameraToParticle =
            particle.worldPositionRadius.xyz - cameraPosition.xyz;
        float particleDistanceAlongRay = dot(cameraToParticle, rayDirection);
        float perpendicularDistanceSquared = max(
            dot(cameraToParticle, cameraToParticle) -
            particleDistanceAlongRay * particleDistanceAlongRay, 0.0);
        float perpendicularDistance = sqrt(perpendicularDistanceSquared);

        // Froxel 是有限截面的锥台而不是一条中心射线。用当前深度的
        // 半对角足迹保守扩张相交测试，并以横向覆盖率平滑边界体素。
        float footprintRadius = max(froxelFootprintRadius, 0.0);
        if (perpendicularDistance >= radius + footprintRadius)
            continue;
        float effectivePerpendicularDistance = max(
            perpendicularDistance - footprintRadius, 0.0);
        float halfChord = sqrt(max(radius * radius -
            effectivePerpendicularDistance * effectivePerpendicularDistance, 0.0));
        float overlapStartDistance = max(sliceStartDistance,
            particleDistanceAlongRay - halfChord);
        float overlapEndDistance = min(sliceEndDistance,
            particleDistanceAlongRay + halfChord);
        if (overlapEndDistance <= overlapStartDistance)
            continue;

        float overlapDistance = overlapEndDistance - overlapStartDistance;
        float overlapFraction = clamp(
            overlapDistance / sliceThickness, 0.0, 1.0);
        float lateralCoverage = footprintRadius > 1.0e-6
            ? 1.0 - smoothstep(max(radius - footprintRadius, 0.0),
                radius + footprintRadius, perpendicularDistance)
            : (perpendicularDistance < radius ? 1.0 : 0.0);
        float integratedDensity = IntegrateParticleDensityOverFroxel(
            overlapStartDistance, overlapEndDistance,
            particleDistanceAlongRay, effectivePerpendicularDistance,
            radius, particle.lightingEdgeCloud.z);
        float weight = overlapFraction * lateralCoverage * integratedDensity;
        if (weight <= 1.0e-7)
            continue;

        NearMediaMediumSample medium;
        medium.scatteringAlbedo =
            particle.scatteringAlbedoExtinction.rgb;
        medium.extinctionPerMeter =
            particle.scatteringAlbedoExtinction.a;
        medium.emissivePerMeter = particle.emissiveAnisotropy.rgb;
        medium.anisotropy = particle.emissiveAnisotropy.a;
        medium.directLightingScale = particle.lightingEdgeCloud.x;
        medium.skyLightingScale = particle.lightingEdgeCloud.y;
        medium.receiveCloudShadows = particle.lightingEdgeCloud.w;
        activity += AccumulateNearMediaMaterial(
            accumulation, medium, weight);
    }
    return activity;
}
