#pragma once

#include <cstdint>

namespace VansGraphics
{
	enum class VansBloomShapeMode : int32_t
	{
		Standard = 0,
		Anamorphic = 1,
		Star = 2,
	};

	struct alignas(16) VansPostProcessParamsGPU
	{
		float   m_ExposureCompensation = 0.0f;
		float   _pad0 = 0.0f;
		float   _pad1 = 0.0f;
		float   _pad2 = 0.0f;

		float   m_BloomIntensity = 0.12f;
		float   m_BloomScatter = 0.7f;
		float   _pad3 = 0.0f;
		float   _pad4 = 0.0f;

		int32_t m_ToneMapperType = 1;
		float   m_WhitePoint = 11.2f;
		float   _pad5 = 0.0f;
		float   _pad6 = 0.0f;

		int32_t m_EnableColorGrading = 1;
		float   m_Contrast = 1.0f;
		float   m_Saturation = 1.0f;
		float   m_HueShift = 0.0f;

		float   m_Temperature = 0.0f;
		float   m_Tint = 0.0f;
		float   _pad7 = 0.0f;
		float   m_DebugPassthrough = 0.0f;

		float   m_TimelineFadeColorR = 0.0f;
		float   m_TimelineFadeColorG = 0.0f;
		float   m_TimelineFadeColorB = 0.0f;
		float   m_TimelineFadeOpacity = 0.0f;

		int32_t m_EnableDOF = 0;
		int32_t m_EnableAutoExposure = 0;
		float   _pad9 = 0.0f;
		float   _pad10 = 0.0f;
	};
	static_assert(sizeof(VansPostProcessParamsGPU) == 112,
		"Post-process CPU UBO layout must match PostProcess.frag");

	struct alignas(16) VansExposureAdaptParamsGPU
	{
		float   m_MinEV100 = -6.0f;
		float   m_MaxEV100 = 16.0f;
		float   m_AdaptationSpeedUp = 3.0f;
		float   m_AdaptationSpeedDown = 1.0f;

		float   m_DeltaTime = 0.016f;
		float   m_ExposureCompensation = 0.0f;
		int32_t m_EnableAutoExposure = 0;
		float   _pad1 = 0.0f;
	};
	static_assert(sizeof(VansExposureAdaptParamsGPU) == 32,
		"Exposure adaptation CPU UBO layout must match ExposureAdapt.comp");

	struct alignas(16) VansBloomParamsGPU
	{
		float   m_Threshold = 1.0f;
		float   m_Knee = 0.5f;
		float   m_Scatter = 0.7f;
		float   _pad0 = 0.0f;

		float   m_Clamp = 64.0f;
		float   m_TintR = 1.0f;
		float   m_TintG = 1.0f;
		float   m_TintB = 1.0f;
	};
	static_assert(sizeof(VansBloomParamsGPU) == 32,
		"Bloom CPU UBO layout must match BloomPrefilter.comp and BloomUpsample.comp");

	struct alignas(16) VansBloomShapeParamsGPU
	{
		int32_t m_Mode = static_cast<int32_t>(VansBloomShapeMode::Standard);
		float   m_ShapeIntensity = 0.35f;
		float   m_ShapeBlend = 1.0f;
		float   m_ShapeAngleRadians = 0.0f;

		float   m_AnamorphicStretch = 4.0f;
		float   m_StreakLength = 24.0f;
		float   m_StreakAttenuation = 0.72f;
		int32_t m_StreakCount = 4;
	};
	static_assert(sizeof(VansBloomShapeParamsGPU) == 32,
		"Bloom shape CPU UBO layout must match BloomShape.comp");

	struct alignas(16) VansDepthOfFieldParamsGPU
	{
		int32_t m_EnableDOF = 0;
		float   m_FocusDistance = 5.0f;
		float   m_FocalLengthMm = 50.0f;
		float   m_FStop = 2.8f;

		float   m_SensorHeightMm = 24.0f;
		float   m_MaxCoC = 16.0f;
		float   m_InvRenderWidth = 0.0f;
		float   m_InvRenderHeight = 0.0f;
	};
	static_assert(sizeof(VansDepthOfFieldParamsGPU) == 32,
		"Depth-of-field CPU UBO layout must match DepthOfField.comp");

	class VansPostProcessProfile
	{
	public:
		bool    m_EnablePostProcess = true;
		bool    m_EnableHDR = true;

		bool    m_EnableAutoExposure = false;
		float   m_ExposureCompensation = 0.0f;
		float   m_MinEV100 = -6.0f;
		float   m_MaxEV100 = 16.0f;
		float   m_AdaptationSpeedUp = 3.0f;
		float   m_AdaptationSpeedDown = 1.0f;

		bool    m_EnableBloom = true;
		float   m_BloomThreshold = 1.0f;
		float   m_BloomKnee = 0.5f;
		float   m_BloomIntensity = 0.12f;
		float   m_BloomScatter = 0.7f;
		float   m_BloomClamp = 64.0f;
		float   m_BloomTintR = 1.0f;
		float   m_BloomTintG = 1.0f;
		float   m_BloomTintB = 1.0f;
		int32_t m_BloomShapeMode = static_cast<int32_t>(VansBloomShapeMode::Standard);
		float   m_BloomShapeIntensity = 0.35f;
		float   m_BloomShapeBlend = 1.0f;
		float   m_BloomShapeAngleDeg = 0.0f;
		float   m_BloomAnamorphicStretch = 4.0f;
		int32_t m_BloomStreakCount = 4;
		float   m_BloomStreakLength = 24.0f;
		float   m_BloomStreakAttenuation = 0.72f;

		int32_t m_ToneMapperType = 1;
		float   m_WhitePoint = 11.2f;

		bool    m_EnableColorGrading = true;
		float   m_Contrast = 1.0f;
		float   m_Saturation = 1.0f;
		float   m_HueShift = 0.0f;
		float   m_Temperature = 0.0f;
		float   m_Tint = 0.0f;

		float   m_TimelineFadeColorR = 0.0f;
		float   m_TimelineFadeColorG = 0.0f;
		float   m_TimelineFadeColorB = 0.0f;
		float   m_TimelineFadeOpacity = 0.0f;

		bool    m_EnableDOF = false;
		float   m_FocusDistance = 5.0f;
		float   m_FocalLengthMm = 50.0f;
		float   m_FStop = 2.8f;
		float   m_SensorHeightMm = 24.0f;
		float   m_MaxCoC = 16.0f;
		bool    m_DOFBlurTransmissionBackground = true;

		bool    m_EnableMotionBlur = false;
		float   m_ShutterScale = 0.5f;
		int32_t m_MotionBlurSamples = 12;

		bool    m_EnableChromaticAberration = false;
		float   m_ChromaticAberrationIntensity = 0.02f;

		bool    m_EnableSharpen = true;
		float   m_SharpenIntensity = 0.15f;

		VansPostProcessParamsGPU ToGPUParams() const;
		VansExposureAdaptParamsGPU ToExposureAdaptParams(float deltaTime) const;
		VansBloomParamsGPU ToBloomParams() const;
		VansBloomShapeParamsGPU ToBloomShapeParams() const;
		VansDepthOfFieldParamsGPU ToDepthOfFieldParams(uint32_t renderWidth, uint32_t renderHeight) const;
		void ResetToDefaults();

		bool m_IsDirty = true;
	};
}
