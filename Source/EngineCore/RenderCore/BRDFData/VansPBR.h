#pragma once
#include "../../ScriptCore/VansCommonUtils.h"

namespace VansGraphics
{
	struct alignas(16)  VansBasePBRParam
	{
		glm::vec3		m_albedo;
		alignas(16)		float m_roughness;
		float			m_metallic;
		float			m_ao;
		float			padding;
	};

	struct  alignas(16)  VansCoatPBRParam
	{
	};

	struct alignas(16)  VansAtmospherePBRParam
	{
		glm::vec3			m_SunDirection;
		alignas(16) float	m_SunLuminance;
		float				m_PlanetRadius;
		float				m_InitSeaLevel;
		float				m_AtmosphereWidth;
		float				m_RayleighScalarHeight;
		float				m_MieScalarHeight;
		float				m_MieAnisotropy;
		float				m_OzoneLevelCenterHeight;
		float				m_OzoneLevelWidth;		// 补齐 3 个 float（offset 52/56/60），使下面字段落在 offset 64（与 GLSL std140 对齐）
		float				_pad0 = 0, _pad1 = 0, _pad2 = 0;
		// CPU 预计算：baseColor × 大气仰角衰减，供无 LightsData 的 shader（如 VolumeCloud.frag）通过 AtmosphereUBO 读取
		glm::vec3			m_EffectiveSunColor = glm::vec3(1.0f);
		float				_pad3 = 0;
	};

}
