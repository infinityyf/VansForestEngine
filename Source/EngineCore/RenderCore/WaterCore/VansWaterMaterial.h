#pragma once
#include "../VansMaterial.h"
#include "VansWaterConfig.h"

// ============================================================
// VansWaterMaterial.h
// 水面材质类，继承 VansMaterial。
// 持有从 VansWaterConfig 展开的所有 GPU 可读字段，
// 以及从场景纹理管理器解析的纹理指针。
// 由 VansSceneLoader::AddWaterNode() 创建并注册到 m_Materials。
// ============================================================

namespace VansGraphics
{
	class VansWaterMaterial : public VansMaterial
	{
	public:
		explicit VansWaterMaterial() = default;
		~VansWaterMaterial() override = default;

		// ── 参与介质 ───────────────────────────────────────────
		glm::vec3 m_AbsorptionCoeffs  = {0.25f, 0.08f, 0.02f};  // R>G>B, 消光: 0.27>0.12>0.08
		glm::vec3 m_ScatteringCoeffs  = {0.02f, 0.04f, 0.06f};  // B>G>R, 蓝光穿透最深
		float     m_WaterIOR          = 1.33f;
		float     m_FresnelPower      = 5.0f;
		float     m_Anisotropy        = 0.85f;
		float     m_SpecularIntensity = 0.6f;
		float     m_WaterRoughness    = 0.02f;   // 水面微面元粗糙度（0=镜面, 1=漫反射）
		glm::vec4 m_DeepWaterColor    = {0.01f, 0.04f, 0.18f, 1.0f};
		glm::vec4 m_ShallowWaterColor = {0.05f, 0.18f, 0.55f, 1.0f};

		// ── 纹理资源（由 AddWaterNode 通过 GetTextureAsset 赋值）──
		VansTexture* m_FoamTexture        = nullptr;
		VansTexture* m_WaterNormalTexture = nullptr;

		// ── 波形配置 ───────────────────────────────────────────
		float m_OceanBaseScale      = 64.0f;
		int   m_GerstnerWaveCount   = 64;
		int   m_FftLODCount         = 4;
		int   m_FftResolution       = 256;
		bool  m_FFTUseDerivativeNormal = true;
		float m_FFTSpectrumAmplitude = 1.0f;
		float m_FFTChoppiness       = 1.0f;
		float m_FFTSmallWaveDamping = 0.001f;
		float m_FFTWindDependency   = 0.07f;
		float m_FFTDepth            = 10000.0f;
		float m_FFTRepeatPeriod     = 0.0f;
		float m_FFTFoamSlopeScale   = 0.25f;
		float m_FFTFoamFoldScale    = 1.0f;
		float m_FFTFoamFoldThreshold = 0.0f;
		uint32_t m_FFTRandomSeed    = 1337;
		float m_WindSpeed           = 12.0f;
		float m_SwellAmplitude      = 0.2f;
		float m_ChopScale           = 1.5f;
		glm::vec2 m_WindDirection   = {0.7071f, 0.7071f};

		int   m_MaxLODCount         = 10;
		float m_LODBasePatchSize    = 16.0f;
		int   m_LODMeshDim          = 65;
		float m_LODDetailBalance    = 2.0f;
		float m_LODMorphWidthRatio  = 0.5f;

		// ── 泡沫 ───────────────────────────────────────────────
		bool  m_EnableFoam    = true;
		float m_FoamIntensity = 1.0f;

		// ── 法线贴图平铺 ───────────────────────────────────────
		glm::vec2 m_NormalMapTiling = {0.1f, 0.03f};

		// ── SSS 次表面散射 ─────────────────────────────────────
		bool  m_SSSEnabled                = true;
		float m_MaxThicknessDistance      = 15.0f;    // 最大厚度 (m)，超过此值 clamp
		float m_DeepWaterThicknessFallback = 0.8f;     // 深水 fallback 归一化厚度 [0,1]

		// ── 焦散 ───────────────────────────────────────────────
		bool  m_EnableCaustics   = true;
		float m_CausticsIntensity = 1.0f;
		float m_CausticsScale    = 0.5f;

		// ── 折射 ───────────────────────────────────────────────
		bool  m_EnableRefraction  = true;
		float m_RefractionMaxDist = 50.0f;
		float m_RefractionScale   = 0.5f;

		// ── SSR ────────────────────────────────────────────────
		bool  m_EnableSSR        = true;
		float m_SSRMaxDistance   = 500.0f;  // SSR 最大追踪距离（m）
		float m_SSRMaxRoughness  = 0.3f;

		// ── N-01: Detail Normal ───────────────────────────────
		bool  m_DetailNormalEnabled     = true;
		float m_DetailNormalIntensity   = 1.0f;
		float m_DetailNormalScale       = 1.0f;
		int   m_DetailNormalOctaves     = 4;
		float m_DetailNormalTimeOffset  = 0.0f;
		float m_DetailNormalBaseScale   = 32.0f;   // 世界空间平铺覆盖范围（m），越小细节越密

		// ── 完整配置备份（供运行时 VansWaterSystem 读取）──────
		VansWaterConfig m_Config;
	};

} // namespace VansGraphics
