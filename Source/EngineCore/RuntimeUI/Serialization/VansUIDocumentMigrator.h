#pragma once

#include "VansUIAssetDocument.h"

#include <string>
#include <vector>

namespace VansRuntime
{
	enum class VansUIDocumentKind
	{
		Screen,
		Component,
		ThemeTokens,
		Localization
	};

	class VansUIDocumentMigrator
	{
	public:
		static constexpr std::uint32_t CurrentSchemaVersion = 1;

		static bool MigrateToCurrent(
			VansUIAssetDocument& document,
			VansUIDocumentKind kind,
			std::vector<std::string>& diagnostics);
	};
}
