#include "VansPostProcessProfile.h"
#include "VansGraphicsDevice.h"
#include <algorithm>

namespace VansGraphics
{
	// ============================================================
	// GPU 参数打包
	// ============================================================

	VansPostProcessParamsGPU VansPostProcessProfile::ToGPUParams() const
	{
		VansPostProcessParamsGPU p;
		p.m_ExposureCompensation = m_ExposureCompensation;
		p.m_BloomIntensity       = m_EnableBloom ? m_BloomIntensity : 0.0f;
		p.m_BloomScatter         = m_BloomScatter;
		p.m_ToneMapperType       = m_ToneMapperType;
		p.m_WhitePoint           = m_WhitePoint;
		p.m_EnableColorGrading   = m_EnableColorGrading ? 1 : 0;
		p.m_Contrast             = m_Contrast;
		p.m_Saturation           = m_Saturation;
		p.m_HueShift             = m_HueShift;
		p.m_Temperature          = m_Temperature;
		p.m_Tint                 = m_Tint;
		p.m_TimelineFadeColorR   = m_TimelineFadeColorR;
		p.m_TimelineFadeColorG   = m_TimelineFadeColorG;
		p.m_TimelineFadeColorB   = m_TimelineFadeColorB;
		p.m_TimelineFadeOpacity  = m_TimelineFadeOpacity;
		p.m_EnableDOF            = m_EnableDOF ? 1 : 0;
		p.m_EnableAutoExposure   = m_EnableAutoExposure ? 1 : 0;
		return p;
	}

	VansExposureAdaptParamsGPU VansPostProcessProfile::ToExposureAdaptParams(float deltaTime) const
	{
		VansExposureAdaptParamsGPU p;
		p.m_MinEV100 = (std::min)(m_MinEV100, m_MaxEV100);
		p.m_MaxEV100 = (std::max)(m_MinEV100, m_MaxEV100);
		p.m_AdaptationSpeedUp = (std::max)(m_AdaptationSpeedUp, 0.0f);
		p.m_AdaptationSpeedDown = (std::max)(m_AdaptationSpeedDown, 0.0f);
		p.m_DeltaTime = std::clamp(deltaTime, 0.0f, 0.25f);
		p.m_ExposureCompensation = std::clamp(m_ExposureCompensation, -24.0f, 24.0f);
		p.m_EnableAutoExposure = m_EnableAutoExposure ? 1 : 0;
		return p;
	}

	VansBloomParamsGPU VansPostProcessProfile::ToBloomParams() const
	{
		VansBloomParamsGPU p;
		p.m_Threshold = std::clamp(m_BloomThreshold, 0.0f, 64.0f);
		p.m_Knee      = std::clamp(m_BloomKnee, 0.0f, 1.0f);
		p.m_Scatter   = std::clamp(m_BloomScatter, 0.0f, 1.0f);
		p.m_Clamp     = std::max(m_BloomClamp, 0.0f);
		p.m_TintR     = std::clamp(m_BloomTintR, 0.0f, 8.0f);
		p.m_TintG     = std::clamp(m_BloomTintG, 0.0f, 8.0f);
		p.m_TintB     = std::clamp(m_BloomTintB, 0.0f, 8.0f);
		return p;
	}

	VansBloomShapeParamsGPU VansPostProcessProfile::ToBloomShapeParams() const
	{
		constexpr float kPi = 3.14159265358979323846f;
		VansBloomShapeParamsGPU p;
		p.m_Mode = std::clamp(m_BloomShapeMode, 0, static_cast<int32_t>(VansBloomShapeMode::Star));
		p.m_ShapeIntensity = std::clamp(m_BloomShapeIntensity, 0.0f, 4.0f);
		p.m_ShapeBlend = std::clamp(m_BloomShapeBlend, 0.0f, 1.0f);
		p.m_ShapeAngleRadians = m_BloomShapeAngleDeg * (kPi / 180.0f);
		p.m_AnamorphicStretch = std::clamp(m_BloomAnamorphicStretch, 0.0f, 16.0f);
		p.m_StreakLength = std::clamp(m_BloomStreakLength, 0.0f, 128.0f);
		p.m_StreakAttenuation = std::clamp(m_BloomStreakAttenuation, 0.0f, 0.98f);
		p.m_StreakCount = std::clamp(m_BloomStreakCount, 2, 8);
		return p;
	}

	VansDepthOfFieldParamsGPU VansPostProcessProfile::ToDepthOfFieldParams(uint32_t renderWidth, uint32_t renderHeight) const
	{
		VansDepthOfFieldParamsGPU p;
		p.m_EnableDOF = m_EnableDOF ? 1 : 0;
		p.m_FocusDistance = std::clamp(m_FocusDistance, 0.01f, 100000.0f);
		p.m_FocalLengthMm = std::clamp(m_FocalLengthMm, 8.0f, 300.0f);
		p.m_FStop = std::clamp(m_FStop, 0.7f, 32.0f);
		p.m_SensorHeightMm = std::clamp(m_SensorHeightMm, 1.0f, 80.0f);
		p.m_MaxCoC = std::clamp(m_MaxCoC, 0.0f, 64.0f);
		p.m_InvRenderWidth = renderWidth > 0 ? 1.0f / static_cast<float>(renderWidth) : 0.0f;
		p.m_InvRenderHeight = renderHeight > 0 ? 1.0f / static_cast<float>(renderHeight) : 0.0f;
		return p;
	}

	void VansPostProcessProfile::ResetToDefaults()
	{
		*this = VansPostProcessProfile{};
		m_IsDirty = true;
	}
}
