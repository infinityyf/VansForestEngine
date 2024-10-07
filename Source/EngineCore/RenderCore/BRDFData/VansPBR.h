#pragma once
#include "../VansCommonUtils.h"

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

}
