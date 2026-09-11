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
		// VAN_PBR 使用该槽传递可选的 alpha cutoff；其他材质类型可保留各自语义。
		float			padding = 0.0f;
	};

	struct  alignas(16)  VansCoatPBRParam
	{
	};

}
