#pragma once
#include "VansBaseWindowComponent.h"

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

namespace VansGraphics
{
	class VansProjectWindow : public VansBaseWindowComponent
	{
	public:

		static std::filesystem::path m_CurrentSelectedFile;

	private:
		void ShowWindow(VansVKDevice& device) override;
	};
} 