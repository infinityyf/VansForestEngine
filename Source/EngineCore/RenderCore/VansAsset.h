#pragma once

#include <string>
#include <vector>
namespace VansGraphics
{
	class VansAsset
	{
	public:
		std::string m_AssetName;

		void SetName(const std::string& name)
		{
			m_AssetName = name;
		}
	};
}