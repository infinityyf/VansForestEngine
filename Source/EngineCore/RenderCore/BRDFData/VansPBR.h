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
		float				m_OzoneLevelWidth;
	};

}
