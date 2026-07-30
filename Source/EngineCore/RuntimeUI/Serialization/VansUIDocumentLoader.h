#pragma once

#include "VansUIAssetDocument.h"

#include <filesystem>
#include <string>

namespace VansRuntime
{
	class VansUIDocumentLoader
	{
	public:
		static bool Load(const std::filesystem::path& path, VansUIAssetDocument& document, std::string& error);
	};
}
