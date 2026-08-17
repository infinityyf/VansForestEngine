#pragma once

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace VansGraphics
{
    // V2 contract: geometry density and spectral frequency bands are independent.
    enum class VansWaveMode : std::uint32_t
    {
        Gerstner = 0,
        FFT = 1,
        WaveParticle = 2,
    };

    struct VansWaterMediumConfig
    {
        glm::vec3 m_AbsorptionCoeff = { 0.25f, 0.08f, 0.02f };
        glm::vec3 m_ScatteringCoeff = { 0.02f, 0.04f, 0.06f };
        float m_IOR = 1.33f;
        float m_Anisotropy = 0.85f;
        float m_WaterRoughness = 0.02f;
    };

    // Fixed 2:1 geometry clipmap. A mesh rebuild is a frame-boundary operation.
    struct VansWaterGeometryConfig
    {
        int m_LodCount = 10;
        float m_BasePatchSize = 16.0f;
        int m_MeshDim = 65;
        float m_MorphStartRatio = 0.5f;
    };

    // Band-limited spectral cascades. Resolution is a runtime invariant in V2.
    struct VansWaterSpectrumConfig
    {
        VansWaveMode m_Mode = VansWaveMode::WaveParticle;
        int m_CascadeCount = 4;
        float m_BaseCoverage = 64.0f;
        float m_CascadeScale = 4.0f;

        glm::vec2 m_WindDirection = { 0.7071f, 0.7071f };
        float m_WindSpeed = 16.0f;
        float m_SwellAmplitude = 0.35f;
        float m_Choppiness = 1.65f;
        int m_GerstnerWaveCount = 32;

        float m_SpectrumAmplitude = 0.001f;
        // The low wavelength clamp is a spectral quality control, not a
        // geometry LOD knob.
        float m_MinWavelength = 1.0f;
        float m_SmallWaveDamping = 0.003f;
        float m_WindDependency = 0.07f;
        float m_Depth = 10000.0f;
        float m_RepeatPeriod = 0.0f;
        std::uint32_t m_RandomSeed = 1337;
    };

    struct VansWaterWaveParticleConfig
    {
        // 每个频谱 cascade 使用独立的波包集合。固定上限使 SSBO 可以按
        // cascade 直接寻址，也避免运行时维护第二套索引表。
        int m_ParticlesPerCascade = 128;
        // 每个 cascade 的目标高度 RMS；后续 cascade 按能量衰减系数递减。
        float m_RmsAmplitude = 0.32f;
        // 紧支撑包络半径相对载波波长的倍数。半径始终额外限制在周期域的一半内。
        float m_PacketWidth = 1.5f;
        // 同时缩放物理相速度和群速度；1 为标准重力波色散，0 为暂停。
        float m_DispersionScale = 1.0f;
        float m_DirectionSpread = 0.7f;
        float m_CascadeAmplitudeFalloff = 0.62f;
        float m_FoamThreshold = 0.28f;
        float m_FoamSoftness = 0.25f;
        std::uint32_t m_RandomSeed = 20260724u;
    };

    struct VansWaterFlowMapConfig
    {
        bool m_Enabled = false;
        float m_Strength = 10.0f;
        float m_Speed = 0.65f;
        float m_PhaseLength = 1.0f;
        float m_NoiseAmount = 0.5f;
        glm::vec2 m_WorldOrigin = { -256.0f, -256.0f };
        glm::vec2 m_WorldSize = { 512.0f, 512.0f };
        glm::vec2 m_FallbackDirection = { 1.0f, 0.0f };
    };

    struct VansWaterCausticsConfig
    {
        bool m_Enabled = false;
        float m_Intensity = 1.0f;
        float m_MaxDistance = 20.0f;
        float m_MaxGain = 3.0f;
        float m_FilterRadius = 0.5f;
    };

    struct VansWaterRefractionConfig
    {
        bool m_Enabled = true;
        // Maximum screen-height UV displacement at normalized thickness 1.
        float m_DistortionStrength = 0.025f;
    };

    struct VansWaterOpticsConfig
    {
        float m_MaxCrossDistance = 40.0f;
        float m_MaxRefractionCrossDistance = 20.0f;
        float m_MultiScatterScale = 1.0f;
        float m_WaterDispersionStrength = 0.2f;
        float m_SSSPathScale = 20.0f;
        float m_SSSNonlinearStrength = 0.5f;
        float m_SSSScatterBoost = 2.0f;
        float m_BacklitPathScale = 20.0f;
        float m_BacklitPhaseG = 0.9998f;
    };

    struct VansWaterVolumeConfig
    {
        float m_ResolutionScale = 0.5f;
        int m_SampleCount = 12;
        int m_SpatialFilterIterations = 2;
        float m_SpatialDepthSensitivity = 2.0f;
    };

    struct VansWaterSSRConfig
    {
        bool m_Enabled = true;
        float m_MaxDistance = 500.0f;
        float m_MaxRoughness = 0.3f;
    };

    struct VansWaterSSSConfig
    {
        bool m_Enabled = true;
        float m_MaxThicknessDistance = 15.0f;
        float m_DeepWaterThicknessFallback = 0.8f;
    };

    struct VansWaterConfig
    {
        static constexpr int SPECTRUM_RESOLUTION = 256;
        static constexpr int MAX_GEOMETRY_LODS = 10;
        static constexpr int MAX_SPECTRUM_CASCADES = 4;
        static constexpr int FLOW_MAP_TEXTURE_SIZE = 256;
        static constexpr int MAX_WAVE_PARTICLES_PER_CASCADE = 256;
        static constexpr int MAX_WAVE_PARTICLE_COUNT =
            MAX_SPECTRUM_CASCADES * MAX_WAVE_PARTICLES_PER_CASCADE;
        static constexpr float GEOMETRY_LOD_RATIO = 2.0f;

        float m_WaterLevel = 3.4f;
        float m_SpecularIntensity = 1.0f;

        VansWaterMediumConfig m_Medium;
        VansWaterGeometryConfig m_Geometry;
        VansWaterSpectrumConfig m_Spectrum;
        VansWaterWaveParticleConfig m_WaveParticle;
        VansWaterFlowMapConfig m_FlowMap;
        VansWaterCausticsConfig m_Caustics;
        VansWaterRefractionConfig m_Refraction;
        VansWaterOpticsConfig m_Optics;
        VansWaterVolumeConfig m_Volume;
        VansWaterSSRConfig m_SSR;
        VansWaterSSSConfig m_SSS;

        void Validate()
        {
            m_Geometry.m_LodCount = std::clamp(m_Geometry.m_LodCount, 1, MAX_GEOMETRY_LODS);
            m_Geometry.m_BasePatchSize = (std::max)(m_Geometry.m_BasePatchSize, 0.25f);
            m_Geometry.m_MeshDim = std::clamp(m_Geometry.m_MeshDim, 17, 257);
            if (((m_Geometry.m_MeshDim - 1) & 1) != 0)
                ++m_Geometry.m_MeshDim;
            m_Geometry.m_MorphStartRatio = std::clamp(m_Geometry.m_MorphStartRatio, 0.05f, 0.95f);

            m_Spectrum.m_CascadeCount = std::clamp(m_Spectrum.m_CascadeCount, 1, MAX_SPECTRUM_CASCADES);
            m_Spectrum.m_BaseCoverage = (std::max)(m_Spectrum.m_BaseCoverage, 4.0f);
            m_Spectrum.m_CascadeScale = std::clamp(m_Spectrum.m_CascadeScale, 2.0f, 8.0f);
            m_Spectrum.m_WindSpeed = (std::max)(m_Spectrum.m_WindSpeed, 0.0f);
            m_Spectrum.m_SwellAmplitude = (std::max)(m_Spectrum.m_SwellAmplitude, 0.0f);
            m_Spectrum.m_Choppiness = std::clamp(m_Spectrum.m_Choppiness, 0.0f, 3.0f);
            m_Spectrum.m_GerstnerWaveCount = std::clamp(m_Spectrum.m_GerstnerWaveCount, 0, 64);
            m_Spectrum.m_SpectrumAmplitude = std::clamp(m_Spectrum.m_SpectrumAmplitude, 0.0f, 0.02f);
            // 双线性顶点采样至少保留每个最短波长四个 texel，避免把 Nyquist
            // 极限处的交替样本误当作可稳定重建的几何波形。
            const float macroNyquist = 4.0f * m_Spectrum.m_BaseCoverage / float(SPECTRUM_RESOLUTION);
            m_Spectrum.m_MinWavelength = std::clamp(
                m_Spectrum.m_MinWavelength, macroNyquist, m_Spectrum.m_BaseCoverage);
            m_Spectrum.m_SmallWaveDamping = std::clamp(m_Spectrum.m_SmallWaveDamping, 0.0f, 0.1f);
            m_Spectrum.m_WindDependency = std::clamp(m_Spectrum.m_WindDependency, 0.0f, 1.0f);
            m_Spectrum.m_Depth = (std::max)(m_Spectrum.m_Depth, 0.1f);
            m_Spectrum.m_RepeatPeriod = (std::max)(m_Spectrum.m_RepeatPeriod, 0.0f);
            if (glm::length(m_Spectrum.m_WindDirection) < 0.001f)
                m_Spectrum.m_WindDirection = { 0.7071f, 0.7071f };
            else
                m_Spectrum.m_WindDirection = glm::normalize(m_Spectrum.m_WindDirection);

            m_WaveParticle.m_ParticlesPerCascade = std::clamp(
                m_WaveParticle.m_ParticlesPerCascade, 0, MAX_WAVE_PARTICLES_PER_CASCADE);
            m_WaveParticle.m_RmsAmplitude = std::clamp(
                m_WaveParticle.m_RmsAmplitude, 0.0f, 4.0f);
            m_WaveParticle.m_PacketWidth = std::clamp(
                m_WaveParticle.m_PacketWidth, 0.5f, 4.0f);
            m_WaveParticle.m_DispersionScale = std::clamp(
                m_WaveParticle.m_DispersionScale, 0.0f, 4.0f);
            m_WaveParticle.m_DirectionSpread = std::clamp(m_WaveParticle.m_DirectionSpread, 0.0f, 3.14159265f);
            m_WaveParticle.m_CascadeAmplitudeFalloff = std::clamp(
                m_WaveParticle.m_CascadeAmplitudeFalloff, 0.0f, 1.0f);
            m_WaveParticle.m_FoamThreshold = std::clamp(m_WaveParticle.m_FoamThreshold, 0.0f, 2.0f);
            m_WaveParticle.m_FoamSoftness = std::clamp(m_WaveParticle.m_FoamSoftness, 0.01f, 2.0f);

            m_FlowMap.m_Strength = std::clamp(m_FlowMap.m_Strength, 0.0f, 256.0f);
            m_FlowMap.m_Speed = std::clamp(m_FlowMap.m_Speed, 0.0f, 16.0f);
            m_FlowMap.m_PhaseLength = std::clamp(m_FlowMap.m_PhaseLength, 0.05f, 32.0f);
            m_FlowMap.m_NoiseAmount = std::clamp(m_FlowMap.m_NoiseAmount, 0.0f, 2.0f);
            m_FlowMap.m_WorldSize.x = (std::max)(m_FlowMap.m_WorldSize.x, 1.0f);
            m_FlowMap.m_WorldSize.y = (std::max)(m_FlowMap.m_WorldSize.y, 1.0f);
            if (glm::length(m_FlowMap.m_FallbackDirection) < 0.001f)
                m_FlowMap.m_FallbackDirection = { 1.0f, 0.0f };
            else
                m_FlowMap.m_FallbackDirection = glm::normalize(m_FlowMap.m_FallbackDirection);

            m_Caustics.m_Intensity = std::clamp(m_Caustics.m_Intensity, 0.0f, 10.0f);
            m_Caustics.m_MaxDistance = std::clamp(m_Caustics.m_MaxDistance, 1.0f, 200.0f);
            m_Caustics.m_MaxGain = std::clamp(m_Caustics.m_MaxGain, 0.0f, 16.0f);
            m_Caustics.m_FilterRadius = std::clamp(m_Caustics.m_FilterRadius, 0.1f, 4.0f);

            m_Refraction.m_DistortionStrength = std::clamp(
                m_Refraction.m_DistortionStrength, 0.0f, 0.1f);

            m_Medium.m_IOR = std::clamp(m_Medium.m_IOR, 1.01f, 2.0f);
            m_Medium.m_Anisotropy = std::clamp(m_Medium.m_Anisotropy, -0.95f, 0.98f);
            m_Medium.m_WaterRoughness = std::clamp(m_Medium.m_WaterRoughness, 0.002f, 0.3f);
            m_Medium.m_AbsorptionCoeff = glm::max(m_Medium.m_AbsorptionCoeff, glm::vec3(0.0f));
            m_Medium.m_ScatteringCoeff = glm::max(m_Medium.m_ScatteringCoeff, glm::vec3(0.0f));

            m_Optics.m_MaxCrossDistance = std::clamp(m_Optics.m_MaxCrossDistance, 1.0f, 200.0f);
            m_Optics.m_MaxRefractionCrossDistance =
                std::clamp(m_Optics.m_MaxRefractionCrossDistance, 1.0f, 200.0f);
            m_Optics.m_MultiScatterScale = std::clamp(m_Optics.m_MultiScatterScale, 0.0f, 8.0f);
            m_Optics.m_WaterDispersionStrength =
                std::clamp(m_Optics.m_WaterDispersionStrength, 0.0f, 2.0f);
            m_Optics.m_SSSPathScale = std::clamp(m_Optics.m_SSSPathScale, 0.1f, 200.0f);
            m_Optics.m_SSSNonlinearStrength =
                std::clamp(m_Optics.m_SSSNonlinearStrength, 0.0f, 4.0f);
            m_Optics.m_SSSScatterBoost = std::clamp(m_Optics.m_SSSScatterBoost, 0.0f, 16.0f);
            m_Optics.m_BacklitPathScale = std::clamp(m_Optics.m_BacklitPathScale, 0.1f, 200.0f);
            m_Optics.m_BacklitPhaseG = std::clamp(m_Optics.m_BacklitPhaseG, 0.0f, 0.9999f);

            m_Volume.m_ResolutionScale = std::clamp(m_Volume.m_ResolutionScale, 0.25f, 1.0f);
            m_Volume.m_SampleCount = std::clamp(m_Volume.m_SampleCount, 1, 64);
            m_Volume.m_SpatialFilterIterations = std::clamp(m_Volume.m_SpatialFilterIterations, 0, 4);
            m_Volume.m_SpatialDepthSensitivity =
                std::clamp(m_Volume.m_SpatialDepthSensitivity, 0.0f, 32.0f);
        }
    };
}
