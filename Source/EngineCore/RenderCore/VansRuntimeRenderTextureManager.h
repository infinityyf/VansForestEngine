#pragma once

#include "VulkanCore/VansTexture.h"
#include <unordered_map>
#include <string>

namespace VansGraphics
{
	class VansRuntimeRenderTextureManager
	{
	public:
		bool Add(const std::string& name, VansTexture* texture, bool replaceExisting = true);

		VansTexture* Get(const std::string& name) const;

		bool Has(const std::string& name) const;

		bool Remove(const std::string& name);

		// 仅从注册表移除条目，不 delete 纹理（所有权归外部管理）
		bool Unregister(const std::string& name);

		void Clear();

	private:
		std::unordered_map<std::string, VansTexture*> m_RuntimeRenderTextures;
	};
}
