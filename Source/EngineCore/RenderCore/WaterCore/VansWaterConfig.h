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
        float m_FresnelPower = 5.0f;
        float m_Anisotropy = 0.85f;
        float m_WaterRoughness = 0.02f;
        glm::vec4 m_DeepColor = { 0.01f, 0.04f, 0.18f, 1.0f };
        glm::vec4 m_ShallowColor = { 0.05f, 0.18f, 0.55f, 1.0f };
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
        float m_WindSpeed = 12.0f;
        float m_SwellAmplitude = 0.2f;
        float m_Choppiness = 1.0f;
        int m_GerstnerWaveCount = 32;

        float m_SpectrumAmplitude = 0.001f;
        // The low wavelength clamp is a spectral quality control, not a
        // geometry LOD knob.
        float m_MinWavelength = 0.5f;
        float m_SmallWaveDamping = 0.003f;
        float m_WindDependency = 0.07f;
        float m_Depth = 10000.0f;
        float m_RepeatPeriod = 0.0f;
        std::uint32_t m_RandomSeed = 1337;
    };

    struct VansWaterWaveParticleConfig
    {
        int m_ParticleCount = 256;
        int m_OctaveCount = 4;
        int m_Profile = 1; // 0=Gaussian, 1=Compact ripple, 2=Sharp crest.
        float m_DomainSize = 256.0f;
        float m_Amplitude = 0.16f;
        float m_MinRadius = 2.0f;
        float m_MaxRadius = 64.0f;
        float m_PhaseVelocity = 1.0f;
        float m_Damping = 0.05f;
        float m_DirectionSpread = 0.45f;
        float m_Lacunarity = 2.0f;
        float m_Persistence = 0.52f;
        float m_RadiusFalloff = 0.55f;
        float m_ProfileSharpness = 2.0f;
        float m_FoamThreshold = 0.55f;
        float m_FoamSoftness = 0.35f;
        float m_Lifetime = 18.0f;
        std::uint32_t m_RandomSeed = 20260724u;
    };

    struct VansWaterFlowMapConfig
    {
        bool m_Enabled = false;
        float m_Strength = 6.0f;
        float m_Speed = 0.45f;
        float m_PhaseLength = 1.0f;
        float m_NoiseAmount = 0.35f;
        glm::vec2 m_WorldOrigin = { -256.0f, -256.0f };
        glm::vec2 m_WorldSize = { 512.0f, 512.0f };
        glm::vec2 m_FallbackDirection = { 1.0f, 0.0f };
    };

    struct VansWaterCausticsConfig
    {
        bool m_Enabled = false;
        float m_Intensity = 1.0f;
        float m_Scale = 0.5f;
    };

    struct VansWaterRefractionConfig
    {
        bool m_Enabled = true;
        // Maximum screen-height UV displacement at normalized thickness 1.
        float m_DistortionStrength = 0.025f;
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
        static constexpr std::uint32_t SCHEMA_VERSION = 4;
        static constexpr int SPECTRUM_RESOLUTION = 256;
        static constexpr int MAX_GEOMETRY_LODS = 10;
        static constexpr int MAX_SPECTRUM_CASCADES = 4;
        static constexpr int FLOW_MAP_TEXTURE_SIZE = 256;
        static constexpr int MAX_WAVE_PARTICLE_COUNT = 1024;
        static constexpr int MAX_WAVE_PARTICLE_OCTAVES = 8;
        static constexpr float GEOMETRY_LOD_RATIO = 2.0f;

        std::uint32_t m_SchemaVersion = SCHEMA_VERSION;
        float m_WaterLevel = 3.4f;
        float m_SpecularIntensity = 1.0f;

        VansWaterMediumConfig m_Medium;
        VansWaterGeometryConfig m_Geometry;
        VansWaterSpectrumConfig m_Spectrum;
        VansWaterWaveParticleConfig m_WaveParticle;
        VansWaterFlowMapConfig m_FlowMap;
        VansWaterCausticsConfig m_Caustics;
        VansWaterRefractionConfig m_Refraction;
        VansWaterSSRConfig m_SSR;
        VansWaterSSSConfig m_SSS;

        void Validate()
        {
            m_SchemaVersion = SCHEMA_VERSION;
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
            const float macroNyquist = 2.0f * m_Spectrum.m_BaseCoverage / float(SPECTRUM_RESOLUTION);
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

            m_WaveParticle.m_ParticleCount = std::clamp(
                m_WaveParticle.m_ParticleCount, 0, MAX_WAVE_PARTICLE_COUNT);
            m_WaveParticle.m_OctaveCount = std::clamp(
                m_WaveParticle.m_OctaveCount, 1, MAX_WAVE_PARTICLE_OCTAVES);
            m_WaveParticle.m_Profile = std::clamp(m_WaveParticle.m_Profile, 0, 2);
            m_WaveParticle.m_DomainSize = (std::max)(m_WaveParticle.m_DomainSize, 16.0f);
            m_WaveParticle.m_Amplitude = std::clamp(m_WaveParticle.m_Amplitude, 0.0f, 10.0f);
            m_WaveParticle.m_MinRadius = std::clamp(m_WaveParticle.m_MinRadius, 0.05f, m_WaveParticle.m_DomainSize);
            m_WaveParticle.m_MaxRadius = std::clamp(
                m_WaveParticle.m_MaxRadius, m_WaveParticle.m_MinRadius, m_WaveParticle.m_DomainSize);
            m_WaveParticle.m_PhaseVelocity = std::clamp(m_WaveParticle.m_PhaseVelocity, 0.0f, 10.0f);
            m_WaveParticle.m_Damping = std::clamp(m_WaveParticle.m_Damping, 0.0f, 2.0f);
            m_WaveParticle.m_DirectionSpread = std::clamp(m_WaveParticle.m_DirectionSpread, 0.0f, 3.14159265f);
            m_WaveParticle.m_Lacunarity = std::clamp(m_WaveParticle.m_Lacunarity, 1.01f, 4.0f);
            m_WaveParticle.m_Persistence = std::clamp(m_WaveParticle.m_Persistence, 0.0f, 1.0f);
            m_WaveParticle.m_RadiusFalloff = std::clamp(m_WaveParticle.m_RadiusFalloff, 0.1f, 1.0f);
            m_WaveParticle.m_ProfileSharpness = std::clamp(m_WaveParticle.m_ProfileSharpness, 0.25f, 8.0f);
            m_WaveParticle.m_FoamThreshold = std::clamp(m_WaveParticle.m_FoamThreshold, 0.0f, 2.0f);
            m_WaveParticle.m_FoamSoftness = std::clamp(m_WaveParticle.m_FoamSoftness, 0.01f, 2.0f);
            m_WaveParticle.m_Lifetime = std::clamp(m_WaveParticle.m_Lifetime, 0.1f, 600.0f);

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

            m_Refraction.m_DistortionStrength = std::clamp(
                m_Refraction.m_DistortionStrength, 0.0f, 0.1f);
        }
    };
}
