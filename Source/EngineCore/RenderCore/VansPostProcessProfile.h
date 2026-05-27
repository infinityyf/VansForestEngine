#pragma once

#include <cstdint>
#include <string>

namespace VansGraphics
{
	// ============================================================
	// GPU 对齐 UBO 结构体
	// ============================================================

	// Final Composite Fragment Shader 使用的 UBO（Set 1, Binding 3）
	// 对应 PostProcess.frag 中读取的所有单像素操作参数。
	struct alignas(16) VansPostProcessParamsGPU
	{
		// ---------- Exposure ----------
		float  m_ExposureCompensation = 0.0f;   // EV 偏移量（手动曝光补偿）
		float  _pad0 = 0.0f;
		float  _pad1 = 0.0f;
		float  _pad2 = 0.0f;

		// ---------- Bloom ----------
		float  m_BloomIntensity = 0.12f;        // Bloom 混合强度
		float  m_BloomScatter   = 0.7f;         // Upsample scatter 权重
		float  _pad3 = 0.0f;
		float  _pad4 = 0.0f;

		// ---------- Tone Mapping ----------
		int32_t m_ToneMapperType  = 1;           // 0=Linear, 1=ACES, 2=Reinhard
		float   m_WhitePoint      = 11.2f;       // 白点（Filmic/Reinhard 等）
		float   _pad5 = 0.0f;
		float   _pad6 = 0.0f;

		// ---------- Color Grading ----------
		int32_t m_EnableColorGrading = 1;        // 1=开启
		float   m_Contrast    = 1.0f;
		float   m_Saturation  = 1.0f;
		float   m_HueShift    = 0.0f;

		float   m_Temperature = 0.0f;           // 色温偏移（-1.0 ~ 1.0）
		float   m_Tint        = 0.0f;           // 色调偏移（-1.0 ~ 1.0）
		float   _pad7 = 0.0f;
		float   _pad8 = 0.0f;

		// ---------- Vignette ----------
		int32_t m_EnableVignette      = 1;
		float   m_VignetteIntensity   = 0.2f;
		float   m_VignetteSmoothness  = 0.5f;
		float   _pad9 = 0.0f;

		// ---------- Film Grain ----------
		int32_t m_EnableFilmGrain      = 1;
		float   m_FilmGrainIntensity   = 0.04f;
		float   m_Time                 = 0.0f;   // 用于 Film Grain 动态随机种子
		float   _pad10 = 0.0f;

		// ---------- Dithering ----------
		int32_t m_EnableDithering = 1;
		float   _pad11 = 0.0f;
		float   _pad12 = 0.0f;
		float   _pad13 = 0.0f;
	};

	// Exposure Adapt Compute Shader 使用的 UBO（Set 1, Binding 2）
	struct alignas(16) VansExposureAdaptParamsGPU
	{
		float   m_MinEV100           = -6.0f;    // 自动曝光最小 EV100
		float   m_MaxEV100           = 16.0f;    // 自动曝光最大 EV100
		float   m_AdaptationSpeedUp  = 3.0f;     // 亮场收敛速度（单位/秒）
		float   m_AdaptationSpeedDown= 1.0f;     // 暗场收敛速度（单位/秒）

		float   m_DeltaTime          = 0.016f;   // 当前帧 delta（秒），CPU 每帧写入
		float   m_ExposureCompensation = 0.0f;   // EV 偏移量（与 GPU 端一致）
		int32_t m_EnableAutoExposure = 1;        // 0=手动曝光，1=自动适应（复用原 _pad0 位置，不改变结构体大小）
		float   _pad1 = 0.0f;
	};

	// Bloom（Prefilter + Upsample）共用 UBO
	struct alignas(16) VansBloomParamsGPU
	{
		float   m_Threshold  = 1.0f;    // 亮度阈值（超过此值才参与 Bloom）
		float   m_Knee       = 0.5f;    // Knee 软化范围（0=硬裁剪）
		float   m_Intensity  = 0.12f;   // 最终 Bloom 强度（Prefilter 时用于幅度控制）
		float   m_Scatter    = 0.7f;    // Upsample 加法权重（scatter 扩散程度）
	};

	// ============================================================
	// CPU 侧后处理参数权威来源
	// Inspector / Serialize / Deserialize 操作的唯一对象
	// ============================================================
	class VansPostProcessProfile
	{
	public:
		// 版本号，用于未来字段迁移
		static constexpr int32_t PROFILE_VERSION = 1;

		// ---------- General ----------
		bool    m_EnablePostProcess    = true;
		bool    m_EnableHDR            = true;

		// ---------- Exposure ----------
		bool    m_EnableAutoExposure     = true;
		float   m_ExposureCompensation   = 0.0f;
		float   m_MinEV100               = -6.0f;
		float   m_MaxEV100               = 16.0f;
		float   m_AdaptationSpeedUp      = 3.0f;
		float   m_AdaptationSpeedDown    = 1.0f;

		// ---------- Bloom ----------
		bool    m_EnableBloom      = true;
		float   m_BloomThreshold   = 1.0f;
		float   m_BloomKnee        = 0.5f;
		float   m_BloomIntensity   = 0.12f;
		float   m_BloomScatter     = 0.7f;
		float   m_BloomClamp       = 64.0f;

		// ---------- Tone Mapping ----------
		int32_t m_ToneMapperType   = 1;      // 0=Linear, 1=ACES, 2=Reinhard
		float   m_WhitePoint       = 11.2f;

		// ---------- Color Grading ----------
		bool    m_EnableColorGrading = true;
		float   m_Contrast           = 1.0f;
		float   m_Saturation         = 1.0f;
		float   m_HueShift           = 0.0f;
		float   m_Temperature        = 0.0f;
		float   m_Tint               = 0.0f;

		// ---------- Depth of Field ----------
		bool    m_EnableDOF      = false;
		float   m_FocusDistance  = 5.0f;
		float   m_FocusRange     = 2.0f;
		float   m_Aperture       = 2.8f;
		float   m_MaxCoC         = 12.0f;

		// ---------- Motion Blur ----------
		bool    m_EnableMotionBlur   = false;
		float   m_ShutterScale       = 0.5f;
		int32_t m_MotionBlurSamples  = 12;

		// ---------- Vignette ----------
		bool    m_EnableVignette      = true;
		float   m_VignetteIntensity   = 0.2f;
		float   m_VignetteSmoothness  = 0.5f;

		// ---------- Chromatic Aberration ----------
		bool    m_EnableChromaticAberration    = false;
		float   m_ChromaticAberrationIntensity = 0.02f;

		// ---------- Film Grain ----------
		bool    m_EnableFilmGrain     = true;
		float   m_FilmGrainIntensity  = 0.04f;

		// ---------- Lens Dirt ----------
		bool    m_EnableLensDirt      = false;
		float   m_LensDirtIntensity   = 0.5f;

		// ---------- AA / Sharpen ----------
		bool    m_EnableFXAA          = false;
		bool    m_EnableSharpen       = true;
		float   m_SharpenIntensity    = 0.15f;

		// ---------- Dithering ----------
		bool    m_EnableDithering     = true;

	public:
		// 将当前参数打包为 GPU UBO 结构
		VansPostProcessParamsGPU ToGPUParams(float time) const;
		VansExposureAdaptParamsGPU ToExposureAdaptParams(float deltaTime) const;
		VansBloomParamsGPU ToBloomParams() const;

		// 序列化 / 反序列化（JSON，参考 VansProjectSettings 模式）
		bool SaveToFile(const std::string& filePath) const;
		bool LoadFromFile(const std::string& filePath);

		// 重置为出厂默认值
		void ResetToDefaults();

		// 覆盖标记：Inspector 修改后设置，由渲染器检测并上传 UBO
		bool m_IsDirty = true;
	};
}
