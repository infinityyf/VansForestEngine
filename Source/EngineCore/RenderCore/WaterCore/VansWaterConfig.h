#pragma once
#include <string>
#include <glm/glm.hpp>

// ============================================================
// VansWaterConfig.h
// 水面系统完整配置结构体，由 VansSceneLoader::AddWaterNode()
// 从 Scene JSON 的顶层 "water" 块解析并填充。
// 不依赖任何 Vulkan 类型，可安全被非渲染层代码包含。
// ============================================================

namespace VansGraphics
{
	// ============================================================
	// 波形生成模式
	// ============================================================
	enum class VansWaveMode
	{
		Gerstner,  // Gerstner 波叠加（快速，可控）
		FFT,       // FFT 海洋频谱（物理真实）
		Hybrid,    // 近处 FFT + 远处 Gerstner
	};

	// ============================================================
	// 水体类型
	// ============================================================
	enum class VansWaterType
	{
		Ocean,  // 无边界海洋
		Lake,   // 有边界湖泊
		River,  // 河流
		Pool,   // 水池
	};

	// ============================================================
	// 水体介质参数（参与介质 Beer-Lambert 模型）
	// ============================================================
	struct VansWaterMediumConfig
	{
		// 吸收系数 (m⁻¹)，遵循水光学：红光吸收最强，蓝光吸收最弱
		// 消光 = absorption + scattering，需保证 totalExtinction_R > totalExtinction_B
		glm::vec3 m_AbsorptionCoeff  = {0.25f, 0.08f, 0.02f};  // R>G>B
		// 散射系数 (m⁻¹)，蓝光瑞利散射最强，但需保证蓝光总消光最低
		glm::vec3 m_ScatteringCoeff  = {0.02f, 0.04f, 0.06f};  // B>G>R, totalExt_{0.27,0.12,0.08}
		// 折射率，1.0 = 空气，水约 1.33
		float     m_IOR              = 1.33f;
		// Fresnel 指数（影响掠射角反射强度）
		float     m_FresnelPower     = 5.0f;
		// Schlick 各向异性 g ∈ (-1, 1)，0 = 各向同性
		float     m_Anisotropy       = 0.85f;
		// 深水颜色
		glm::vec4 m_DeepColor        = {0.01f, 0.04f, 0.18f, 1.0f};   // 深水暗蓝
		// 浅水颜色
		glm::vec4 m_ShallowColor     = {0.05f, 0.18f, 0.55f, 1.0f};   // 浅水亮蓝（散射色）
	};

	// ============================================================
	// 波形仿真参数
	// ============================================================
	struct VansWaterWaveConfig
	{
		// 波形生成模式
		VansWaveMode m_Mode             = VansWaveMode::Gerstner;
		// Clipmap 基础缩放（oceanBaseScale，影响 LOD 覆盖范围）
		float        m_BaseScale        = 256.0f;
		// 最大 LOD 数量 [1, 10]
		int          m_MaxLOD           = 10;
		// 风向（单位向量，XZ 平面）
		glm::vec2    m_WindDirection    = {0.7071f, 0.7071f};
		// 风速 (m/s)
		float        m_WindSpeed        = 12.0f;
		// 涌浪振幅 (m)
		float        m_SwellAmplitude   = 0.2f;
		// Gerstner chop 强度 [0, 2]
		float        m_ChopScale        = 1.5f;
		// Gerstner 叠加的波分量数量 [1, 64]
		int          m_GerstnerWaveCount = 64;
		// 混合模式（Hybrid）下 FFT 覆盖的 LOD 数
		int          m_FftLODCount      = 4;
		// FFT 分辨率（128 或 256）
		int          m_FftResolution    = 256;
	};

	// ============================================================
	// 泡沫参数
	// ============================================================
	struct VansWaterFoamConfig
	{
		bool        m_Enabled     = true;
		// resource.json 中已加载纹理的名称
		std::string m_TextureName;
		// 泡沫强度乘数
		float       m_Intensity   = 1.0f;
	};

	// ============================================================
	// 法线贴图参数
	// ============================================================
	struct VansWaterNormalMapConfig
	{
		// resource.json 中已加载纹理的名称
		std::string m_TextureName;
		// UV 平铺缩放（X=空间, Y=流动速率）
		glm::vec2   m_Tiling      = {0.1f, 0.03f};
	};

	// ============================================================
	// 焦散参数
	// ============================================================
	struct VansWaterCausticsConfig
	{
		bool  m_Enabled   = true;
		float m_Intensity = 1.0f;
		// 焦散纹理 UV 缩放
		float m_Scale     = 0.5f;
	};

	// ============================================================
	// 折射参数
	// ============================================================
	struct VansWaterRefractionConfig
	{
		bool  m_Enabled     = true;
		// 最大屏幕空间折射偏移距离 (m)
		float m_MaxDistance = 50.0f;
		// 折射偏移强度
		float m_Scale       = 0.5f;
	};

	// ============================================================
	// SSR 参数
	// ============================================================
	struct VansWaterSSRConfig
	{
		bool  m_Enabled      = true;
		// 超过该粗糙度退化为 IBL（[0, 1]）
		float m_MaxRoughness = 0.3f;
	};

	// ============================================================
	// VansWaterConfig — 完整水面配置
	//
	// JSON 对应关系：
	//   "water" : {
	//     "type"       : "ocean",      // VansWaterType
	//     "level"      : 0.0,          // m_WaterLevel
	//     "medium"     : { ... },      // VansWaterMediumConfig
	//     "waves"      : { ... },      // VansWaterWaveConfig
	//     "foam"       : { ... },      // VansWaterFoamConfig
	//     "normalMap"  : { ... },      // VansWaterNormalMapConfig
	//     "caustics"   : { ... },      // VansWaterCausticsConfig
	//     "refraction" : { ... },      // VansWaterRefractionConfig
	//     "ssr"        : { ... },      // VansWaterSSRConfig
	//     "specularIntensity" : 1.0
	//   }
	// ============================================================
	struct VansWaterConfig
	{
		VansWaterType            m_Type             = VansWaterType::Ocean;
		// 水面 Y 轴高度（世界空间）
		float                    m_WaterLevel       = 3.4f;
		// 高光强度乘数
		float                    m_SpecularIntensity = 1.0f;

		VansWaterMediumConfig    m_Medium;
		VansWaterWaveConfig      m_Waves;
		VansWaterFoamConfig      m_Foam;
		VansWaterNormalMapConfig m_NormalMap;
		VansWaterCausticsConfig  m_Caustics;
		VansWaterRefractionConfig m_Refraction;
		VansWaterSSRConfig       m_SSR;
	};

} // namespace VansGraphics
