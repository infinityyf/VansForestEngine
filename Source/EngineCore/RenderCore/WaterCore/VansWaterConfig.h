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
        Hybrid = 2, // FFT spectrum plus authored Gerstner components at every cascade.
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
        VansWaveMode m_Mode = VansWaveMode::Hybrid;
        int m_CascadeCount = 4;
        float m_BaseCoverage = 64.0f;
        float m_CascadeScale = 4.0f;

        glm::vec2 m_WindDirection = { 0.7071f, 0.7071f };
        float m_WindSpeed = 12.0f;
        float m_SwellAmplitude = 0.2f;
        float m_Choppiness = 1.0f;
        int m_GerstnerWaveCount = 32;

        float m_SpectrumAmplitude = 0.001f;
        // Shorter wavelengths are owned exclusively by the spectral slope
        // fields.  This is a power-spectrum split, not a geometry LOD knob.
        float m_MinWavelength = 0.5f;
        float m_SmallWaveDamping = 0.003f;
        float m_WindDependency = 0.07f;
        float m_Depth = 10000.0f;
        float m_RepeatPeriod = 0.0f;
        std::uint32_t m_RandomSeed = 1337;
    };

    // Two decorrelated, world-anchored FFT slope fields cover the same short-
    // wave spectrum.  Different torus periods and rotations make the combined
    // repetition period large while every source field remains four-edge
    // periodic.  Their upper wavelength is m_Spectrum.m_MinWavelength.
    struct VansWaterMicroSlopeConfig
    {
        bool m_Enabled = true;
        float m_Intensity = 0.35f;
        float m_MinWavelength = 0.09f;
        float m_PrimaryCoverage = 8.0f;
        float m_SecondaryCoverage = 11.313708f;
        float m_RotationDegrees = 31.0f;
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
        static constexpr std::uint32_t SCHEMA_VERSION = 3;
        static constexpr int SPECTRUM_RESOLUTION = 256;
        static constexpr int MAX_GEOMETRY_LODS = 10;
        static constexpr int MAX_SPECTRUM_CASCADES = 4;
        static constexpr int MICRO_SLOPE_BAND_COUNT = 2;
        static constexpr int MICRO_SLOPE_DOMAIN_COUNT = 2;
        static constexpr int MICRO_SLOPE_LAYER_COUNT =
            MICRO_SLOPE_BAND_COUNT * MICRO_SLOPE_DOMAIN_COUNT;
        static constexpr float GEOMETRY_LOD_RATIO = 2.0f;

        std::uint32_t m_SchemaVersion = SCHEMA_VERSION;
        float m_WaterLevel = 3.4f;
        float m_SpecularIntensity = 1.0f;

        VansWaterMediumConfig m_Medium;
        VansWaterGeometryConfig m_Geometry;
        VansWaterSpectrumConfig m_Spectrum;
        VansWaterMicroSlopeConfig m_MicroSlope;
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

            const float maxMicroCoverage = (std::max)(2.0f,
                m_Spectrum.m_MinWavelength * 0.95f * float(SPECTRUM_RESOLUTION) * 0.5f);
            m_MicroSlope.m_PrimaryCoverage = std::clamp(
                m_MicroSlope.m_PrimaryCoverage, 2.0f, (std::min)(64.0f, maxMicroCoverage));
            m_MicroSlope.m_SecondaryCoverage = std::clamp(
                m_MicroSlope.m_SecondaryCoverage, 2.0f, (std::min)(64.0f, maxMicroCoverage));
            const float microNyquist = 2.0f * (std::max)(
                m_MicroSlope.m_PrimaryCoverage, m_MicroSlope.m_SecondaryCoverage)
                / float(SPECTRUM_RESOLUTION);
            m_MicroSlope.m_MinWavelength = std::clamp(
                m_MicroSlope.m_MinWavelength,
                microNyquist,
                m_Spectrum.m_MinWavelength * 0.95f);
            m_MicroSlope.m_Intensity = std::clamp(m_MicroSlope.m_Intensity, 0.0f, 3.0f);
            m_MicroSlope.m_RotationDegrees = std::clamp(
                m_MicroSlope.m_RotationDegrees, 0.0f, 89.0f);
            m_Refraction.m_DistortionStrength = std::clamp(
                m_Refraction.m_DistortionStrength, 0.0f, 0.1f);
        }
    };
}
