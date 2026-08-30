#ifndef CLOUD_LIGHTING_HP_GLSL
#define CLOUD_LIGHTING_HP_GLSL

// HanPi/HPVolumeCloud-inspired volume cloud lighting model.
// Reference: AshenOneArt/HPVolumeCloud - https://github.com/AshenOneArt/HPVolumeCloud
// This file ports the lighting model shape, not HP's weather-map density path.

const float HP_CLOUD_PI = 3.14159265359;

struct CloudSampleProps
{
    float density;
    float heightFrac;
    float sigmaTView;
    float sigmaTLight;
};

struct CloudLightingEnv
{
    vec3 sunDir;
    vec3 sunColor;
    vec3 ambientTop;
    vec3 ambientBottom;
    float cosTheta;
};

float CloudSaturate(float value)
{
    return clamp(value, 0.0, 1.0);
}

CloudSampleProps EvaluateCloudSampleProps(vec3 pos)
{
    CloudSampleProps props;
    props.density = SampleCloudDensity(pos);
    float heightAbove = length(pos) - uCloud.planetBottomRadius;
    props.heightFrac = GetCloudHeightFraction(
        heightAbove,
        uCloud.cloudMinHeight,
        uCloud.cloudMaxHeight);
    props.sigmaTView = props.density * max(uCloud.sigmaTRef, 0.0) * max(uCloud.viewAbsorption, 0.0);
    props.sigmaTLight = props.density * max(uCloud.sigmaTRef, 0.0) * max(uCloud.lightAbsorption, 0.0);
    return props;
}

float HPHG(float cosTheta, float g)
{
    float g2 = g * g;
    float d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-5);
    return (1.0 - g2) / (4.0 * HP_CLOUD_PI * pow(d, 1.5));
}

float HPPhaseOctave(float cosTheta, int octave)
{
    float e = pow(clamp(uCloud.msEccentricity, 0.0, 1.0), float(octave));
    return HPHG(cosTheta, clamp(uCloud.forwardEccentricity, 0.0, 0.95) * e)
         + HPHG(cosTheta, -clamp(uCloud.backwardEccentricity, 0.0, 0.95) * e);
}

CloudLightingEnv EvaluateCloudLightingEnv(vec3 rayDir)
{
    CloudLightingEnv env;
    env.sunDir = normalize(
        uAtmosphereFrame.preparedMainLightDirectionAndValidity.xyz);
    env.sunColor = uAtmosphereFrame.preparedMainLightColorAndIntensity.rgb *
        uAtmosphereFrame.preparedMainLightColorAndIntensity.w *
        uCloud.sunBrightness;
    env.cosTheta = dot(rayDir, env.sunDir);
    vec3 cameraUp = normalize(-uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz);
    float rawSunElevation = dot(env.sunDir, cameraUp);
    float sunElevation = clamp(rawSunElevation, 0.0, 1.0);
    float duskFactor = 1.0 - smoothstep(0.02, 0.28, sunElevation);
    vec3 daySky = vec3(0.55, 0.72, 0.95);
    vec3 duskSky = vec3(0.95, 0.62, 0.28);
    vec3 skyTint = mix(daySky, duskSky,
        duskFactor * clamp(uCloud.ambientDuskWarmth, 0.0, 1.0));
    env.ambientTop = skyTint * max(uCloud.ambientTopStrength, 0.0) * env.sunColor;
    env.ambientBottom = skyTint * max(uCloud.ambientBottomStrength, 0.0) * env.sunColor;
    return env;
}

float CloudDistanceToShellExit(vec3 pos, vec3 dir)
{
    CloudShellResult shell = IntersectCloudShell(
        pos,
        dir,
        uCloud.planetBottomRadius,
        uCloud.cloudMinHeight,
        uCloud.cloudMaxHeight);
    return shell.hit ? max(shell.tEnd, 0.0) : 0.0;
}

float EvaluateHPBoundaryConfidence(vec3 pos, float heightFrac, vec3 sunDir)
{
    float wrap = clamp(uCloud.boundaryWrap, 0.0, 1.0);
    float topBoundary = CloudSaturate((sunDir.y + wrap) / (1.0 + wrap));
    float depthBase = max(heightFrac + uCloud.phiFwdDepthBias, 0.0);
    float bottomConfidence = 1.0 - exp(-pow(depthBase, max(uCloud.phiFwdDepthPow, 0.01)));
    float baseConfidence = topBoundary * bottomConfidence;

    float gradientConfidence = baseConfidence;
    if (uCloud.boundaryGradientStrength > 0.001)
    {
        float h = max(uCloud.boundaryGradientStep, 1.0);
        float dL = SampleCloudDensity(pos - vec3(h, 0.0, 0.0));
        float dR = SampleCloudDensity(pos + vec3(h, 0.0, 0.0));
        float dD = SampleCloudDensity(pos - vec3(0.0, h, 0.0));
        float dU = SampleCloudDensity(pos + vec3(0.0, h, 0.0));
        float dB = SampleCloudDensity(pos - vec3(0.0, 0.0, h));
        float dF = SampleCloudDensity(pos + vec3(0.0, 0.0, h));
        vec3 gradient = vec3(dR - dL, dU - dD, dF - dB) / max(2.0 * h, 1.0);
        float gradientLen = length(gradient);
        if (gradientLen > 1e-5)
        {
            vec3 outwardNormal = normalize(-gradient);
            float litBoundary = CloudSaturate((dot(outwardNormal, sunDir) + wrap) / (1.0 + wrap));
            float gradientWeight = smoothstep(1e-5, 6e-4, gradientLen);
            gradientConfidence = mix(1.0, litBoundary, gradientWeight) * bottomConfidence;
        }
    }

    float shapedConfidence = mix(baseConfidence, gradientConfidence, clamp(uCloud.boundaryGradientStrength, 0.0, 1.0));
    return mix(1.0, shapedConfidence, clamp(uCloud.boundaryConfidence, 0.0, 1.0));
}

float CompressHPPhi(float phiValue)
{
    float compress = max(uCloud.phiFwdCompress, 0.0);
    if (compress <= 0.0)
        return phiValue;
    return (1.0 - exp(-phiValue * compress)) / compress;
}

vec3 EvaluateHPSunLuminance(vec3 pos, float heightFrac, CloudLightingEnv env,
                            out float lightOD, out float phiFwd, out float boundaryOut)
{
    float coverDist = min(
        CloudDistanceToShellExit(pos, env.sunDir),
        max(uCloud.phiFwdMaxDistance, 1.0));
    int steps = clamp(int(round(uCloud.lightStepCount)), 1, 16);

    lightOD = 0.0;
    phiFwd = 0.0;
    boundaryOut = EvaluateHPBoundaryConfidence(pos, heightFrac, env.sunDir);

    if (coverDist <= 0.0)
    {
        vec3 clearLuminance = vec3(0.0);
        for (int octave = 0; octave < 3; ++octave)
        {
            clearLuminance += env.sunColor
                * HPPhaseOctave(env.cosTheta, octave)
                * pow(clamp(uCloud.msContribution, 0.0, 1.0), float(octave));
        }
        return clearLuminance;
    }

    float coneRatio = max(uCloud.phiFwdConeRatio, 1.01);
    float coneDenom = max(pow(coneRatio, float(steps)) - 1.0, 1e-4);
    float stepWidth = max(coverDist * (coneRatio - 1.0) / coneDenom, max(uCloud.phiFwdMinStep, 1.0));
    float traveled = 0.0;

    float omega0 = clamp(uCloud.singleScatteringAlbedo, 0.0, 0.9999);
    float kappaODScale = sqrt(3.0 * max(1.0 - omega0, 1e-5));
    float kappaODSum = 0.0;
    float absSurvival = 1.0;

    for (int i = 0; i < 16; ++i)
    {
        if (i >= steps || traveled >= coverDist)
            break;

        float width = min(stepWidth, coverDist - traveled);
        float distToCenter = traveled + width * 0.5;
        vec3 samplePos = pos + env.sunDir * distToCenter;
        CloudSampleProps lightProps = EvaluateCloudSampleProps(samplePos);

        float localOD = lightProps.sigmaTLight * width;
        float sigmaS = lightProps.sigmaTLight * omega0;
        float qSrc = sigmaS * width;
        float kappaStep = localOD * kappaODScale;
        float propagation = exp(-(kappaODSum + kappaStep * 0.5));
        float msBuild = 1.0 - exp(-(lightOD + localOD * 0.5) * max(uCloud.phiFwdMSBuildScale, 0.0));
        float invR = 1.0 / max(distToCenter, width * 0.5);
        float sourceBoundary = EvaluateHPBoundaryConfidence(samplePos, lightProps.heightFrac, env.sunDir);

        phiFwd += absSurvival
                * qSrc
                * lightProps.sigmaTLight
                * sourceBoundary
                * msBuild
                * propagation
                * invR;

        absSurvival *= exp(-localOD * (1.0 - omega0));
        kappaODSum += kappaStep;
        lightOD += localOD;
        traveled += width;
        stepWidth *= coneRatio;
    }

    vec3 scatteringTint = max(vec3(uCloud.scatteringTintR, uCloud.scatteringTintG, uCloud.scatteringTintB), vec3(0.0));
    vec3 luminance = vec3(0.0);
    for (int octave = 0; octave < 3; ++octave)
    {
        float octaveAtten = pow(clamp(uCloud.msAttenuation, 0.0, 1.0), float(octave));
        float octaveContribution = pow(clamp(uCloud.msContribution, 0.0, 1.0), float(octave));
        vec3 transmittance = exp(-scatteringTint * lightOD * octaveAtten);
        luminance += transmittance * env.sunColor * HPPhaseOctave(env.cosTheta, octave) * octaveContribution;
    }

    return luminance;
}

vec3 EvaluateHPCloudSourceRadiance(
    vec3 pos,
    float stepSize,
    CloudLightingEnv env,
    CloudSampleProps props)
{
    if (props.density <= 0.0 || props.sigmaTView <= 0.0)
		return vec3(0.0);

    float lightOD = 0.0;
    float phiFwd = 0.0;
    float boundary = 1.0;
    vec3 directional = EvaluateHPSunLuminance(pos, props.heightFrac, env, lightOD, phiFwd, boundary);

    float omega0 = clamp(uCloud.singleScatteringAlbedo, 0.0, 0.9999);
    float stepT = exp(-props.sigmaTView * stepSize);
    float scatterOD = props.sigmaTView * omega0 * stepSize;
    float sourceAmount = 1.0 - exp(-scatterOD / max(uCloud.scatterSourceODScale, 1e-3));
    sourceAmount = pow(CloudSaturate(sourceAmount), max(uCloud.scatterSourceCurvePow, 0.01));

    float phiScalar = phiFwd * max(uCloud.phiFwdIntensity, 0.0);
    float phiMapped = CompressHPPhi(phiScalar);

    float upwardAO = exp(-(lightOD * max(env.sunDir.y, 0.05)) * max(uCloud.aoUpwardScale, 0.0));
    vec3 ambient = env.ambientTop * upwardAO
                 + env.ambientBottom * (1.0 - props.heightFrac);

    vec3 sampleScattering = directional * sourceAmount
						  + phiMapped * env.sunColor
                          + ambient * sourceAmount;

    int debugMode = int(round(uCloud.shadingDebugMode));
    if (debugMode == 1)
        sampleScattering = vec3(props.density);
    else if (debugMode == 2)
        sampleScattering = vec3(1.0 - stepT);
    else if (debugMode == 3)
        sampleScattering = vec3(1.0 - exp(-lightOD));
    else if (debugMode == 4)
        sampleScattering = directional;
    else if (debugMode == 5)
        sampleScattering = vec3(CompressHPPhi(phiScalar));
    else if (debugMode == 6)
        sampleScattering = vec3(boundary);
    else if (debugMode == 7)
        sampleScattering = vec3(upwardAO);
    else if (debugMode == 8)
        sampleScattering = vec3(sourceAmount);

	return sampleScattering;
}

#endif // CLOUD_LIGHTING_HP_GLSL
