#pragma once

#include <string>
#include <vector>

namespace VansGraphics
{
	class VansAsset
	{
	public:
		virtual ~VansAsset() = default;

		std::string m_AssetName;

		void SetName(const std::string& name)
		{
			m_AssetName = name;
		}

		//需要支持监视路径，并在文件修改时重新导入

	};
}