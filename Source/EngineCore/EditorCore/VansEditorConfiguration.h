#pragma once

#include "VansEditorWindowCatalog.h"
#include "../PackagingCore/VansGamePackageBuilder.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace VansGraphics
{
struct VansEditorFontConfiguration
{
	float sizePixels = 0.0f;
	std::vector<std::string> primaryCandidates;
	std::vector<std::string> fallbackCandidates;
};

struct VansEditorStyleConfiguration
{
	float windowRounding = 0.0f;
	float childRounding = 0.0f;
	float frameRounding = 0.0f;
	float popupRounding = 0.0f;
	float scrollbarRounding = 0.0f;
	float grabRounding = 0.0f;
	float tabRounding = 0.0f;
	std::array<float, 2> windowPadding{};
	std::array<float, 2> framePadding{};
	std::array<float, 2> itemSpacing{};
	std::array<float, 2> itemInnerSpacing{};
	float indentSpacing = 0.0f;
	float scrollbarSize = 0.0f;
	float grabMinSize = 0.0f;
	float windowBorderSize = 0.0f;
	float childBorderSize = 0.0f;
	float frameBorderSize = 0.0f;
	float popupBorderSize = 0.0f;
	float tabBorderSize = 0.0f;
	std::array<float, 2> windowTitleAlign{};
	std::array<float, 2> separatorTextAlign{};
};

struct VansEditorToolbarConfiguration
{
	float buttonWidth = 0.0f;
	float buttonHeight = 0.0f;
	float buttonSpacing = 0.0f;
};

struct VansEditorConfiguration
{
	VansEditorFontConfiguration fonts;
	VansEditorStyleConfiguration style;
	VansEditorToolbarConfiguration toolbar;
	std::array<bool, static_cast<std::size_t>(VansEditorWindowId::Count)> windowDefaults{};
	Vans::VansGamePackagePlatform packagePlatform = Vans::VansGamePackagePlatform::Unknown;

	static std::filesystem::path ResolveBuiltInPath();
	static bool Decode(std::string_view bytes, VansEditorConfiguration& configuration, std::string& error);
	static bool Load(
		const std::filesystem::path& path,
		VansEditorConfiguration& configuration,
		std::string& error);
};
}
