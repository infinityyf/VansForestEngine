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
		p.m_EnableAutoExposure = m_EnableAutoExposure ? 1 : 0;
		return p;
	}

	VansBloomParamsGPU VansPostProcessProfile::ToBloomParams() const
	{
		VansBloomParamsGPU p;
		p.m_Threshold = m_BloomThreshold;
		p.m_Knee      = m_BloomKnee;
		p.m_Intensity = m_BloomIntensity;
		p.m_Scatter   = m_BloomScatter;
		return p;
	}

	void VansPostProcessProfile::ResetToDefaults()
	{
		*this = VansPostProcessProfile{};
		m_IsDirty = true;
	}
}
