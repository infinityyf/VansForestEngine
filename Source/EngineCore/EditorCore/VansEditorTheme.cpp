#include "VansEditorTheme.h"
#include "VansEditorConfiguration.h"

#include "../Util/VansLog.h"
#include "imgui.h"
#include <Windows.h>

#include <array>
#include <filesystem>

namespace
{
std::filesystem::path ResolveSystemFont(std::string_view candidate)
{
	const std::filesystem::path configured(candidate);
	if (configured.is_absolute())
		return configured;
	std::array<char, MAX_PATH> windowsDirectory{};
	const UINT length = GetWindowsDirectoryA(
		windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
	if (length == 0 || length >= windowsDirectory.size())
		return configured;
	return std::filesystem::path(std::string(windowsDirectory.data(), length)) / "Fonts" / configured;
}
}

void VansGraphics::VansEditorTheme::Apply(const VansEditorConfiguration& configuration)
{
	ImGuiIO& io = ImGui::GetIO();

	ImFont* primaryFont = nullptr;
	for (const std::string& candidate : configuration.fonts.primaryCandidates)
	{
		const std::string path = ResolveSystemFont(candidate).string();
		primaryFont = io.Fonts->AddFontFromFileTTF(path.c_str(), configuration.fonts.sizePixels);
		if (primaryFont)
			break;
	}
	if (!primaryFont)
		primaryFont = io.Fonts->AddFontDefault();

	ImFontConfig chineseFontConfig;
	chineseFontConfig.MergeMode = true;
	chineseFontConfig.PixelSnapH = true;

	const ImWchar* chineseGlyphRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
	ImFont* fallbackFont = nullptr;
	for (const std::string& candidate : configuration.fonts.fallbackCandidates)
	{
		const std::string path = ResolveSystemFont(candidate).string();
		fallbackFont = io.Fonts->AddFontFromFileTTF(
			path.c_str(),
			configuration.fonts.sizePixels,
			&chineseFontConfig,
			chineseGlyphRanges);
		if (fallbackFont)
			break;
	}

	if (!fallbackFont)
	{
		VANS_LOG_WARN("[ImGui] Failed to load a Chinese fallback font. UTF-8 Chinese text may render as '?'");
	}

	io.Fonts->Build();

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	const VansEditorStyleConfiguration& configuredStyle = configuration.style;

	style.WindowRounding = configuredStyle.windowRounding;
	style.ChildRounding = configuredStyle.childRounding;
	style.FrameRounding = configuredStyle.frameRounding;
	style.PopupRounding = configuredStyle.popupRounding;
	style.ScrollbarRounding = configuredStyle.scrollbarRounding;
	style.GrabRounding = configuredStyle.grabRounding;
	style.TabRounding = configuredStyle.tabRounding;

	style.WindowPadding = ImVec2(configuredStyle.windowPadding[0], configuredStyle.windowPadding[1]);
	style.FramePadding = ImVec2(configuredStyle.framePadding[0], configuredStyle.framePadding[1]);
	style.ItemSpacing = ImVec2(configuredStyle.itemSpacing[0], configuredStyle.itemSpacing[1]);
	style.ItemInnerSpacing = ImVec2(configuredStyle.itemInnerSpacing[0], configuredStyle.itemInnerSpacing[1]);
	style.IndentSpacing = configuredStyle.indentSpacing;
	style.ScrollbarSize = configuredStyle.scrollbarSize;
	style.GrabMinSize = configuredStyle.grabMinSize;

	style.WindowBorderSize = configuredStyle.windowBorderSize;
	style.ChildBorderSize = configuredStyle.childBorderSize;
	style.FrameBorderSize = configuredStyle.frameBorderSize;
	style.PopupBorderSize = configuredStyle.popupBorderSize;
	style.TabBorderSize = configuredStyle.tabBorderSize;

	style.WindowTitleAlign = ImVec2(configuredStyle.windowTitleAlign[0], configuredStyle.windowTitleAlign[1]);
	style.SeparatorTextAlign = ImVec2(configuredStyle.separatorTextAlign[0], configuredStyle.separatorTextAlign[1]);

	ImVec4* c = style.Colors;
	c[ImGuiCol_WindowBg] = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
	c[ImGuiCol_ChildBg] = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
	c[ImGuiCol_PopupBg] = ImVec4(0.082f, 0.082f, 0.090f, 0.98f);
	c[ImGuiCol_MenuBarBg] = ImVec4(0.055f, 0.055f, 0.055f, 1.00f);
	c[ImGuiCol_Border] = ImVec4(0.16f, 0.16f, 0.18f, 0.50f);
	c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	c[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
	c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.28f, 0.50f, 0.80f);
	c[ImGuiCol_TitleBg] = ImVec4(0.047f, 0.047f, 0.047f, 1.00f);
	c[ImGuiCol_TitleBgActive] = ImVec4(0.059f, 0.059f, 0.059f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.047f, 0.047f, 0.047f, 0.75f);
	c[ImGuiCol_Tab] = ImVec4(0.067f, 0.067f, 0.075f, 1.00f);
	c[ImGuiCol_TabHovered] = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
	c[ImGuiCol_TabActive] = ImVec4(0.12f, 0.28f, 0.48f, 1.00f);
	c[ImGuiCol_TabUnfocused] = ImVec4(0.055f, 0.055f, 0.060f, 1.00f);
	c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
	c[ImGuiCol_Button] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
	c[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
	c[ImGuiCol_ButtonActive] = ImVec4(0.11f, 0.27f, 0.48f, 1.00f);
	c[ImGuiCol_Header] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
	c[ImGuiCol_HeaderActive] = ImVec4(0.12f, 0.28f, 0.48f, 1.00f);
	c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
	c[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.34f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.42f, 0.44f, 1.00f);
	c[ImGuiCol_SliderGrab] = ImVec4(0.22f, 0.46f, 0.73f, 1.00f);
	c[ImGuiCol_SliderGrabActive] = ImVec4(0.28f, 0.52f, 0.80f, 1.00f);
	c[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 0.88f, 1.00f);
	c[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.24f, 0.50f);
	c[ImGuiCol_SeparatorHovered] = ImVec4(0.18f, 0.38f, 0.62f, 0.78f);
	c[ImGuiCol_SeparatorActive] = ImVec4(0.14f, 0.34f, 0.58f, 1.00f);
	c[ImGuiCol_ResizeGrip] = ImVec4(0.22f, 0.46f, 0.73f, 0.20f);
	c[ImGuiCol_ResizeGripHovered] = ImVec4(0.22f, 0.46f, 0.73f, 0.67f);
	c[ImGuiCol_ResizeGripActive] = ImVec4(0.22f, 0.46f, 0.73f, 0.95f);
	c[ImGuiCol_DockingPreview] = ImVec4(0.15f, 0.35f, 0.60f, 0.70f);
	c[ImGuiCol_DockingEmptyBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	c[ImGuiCol_Text] = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
	c[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.46f, 0.48f, 1.00f);
	c[ImGuiCol_TextSelectedBg] = ImVec4(0.18f, 0.40f, 0.68f, 0.43f);
	c[ImGuiCol_NavHighlight] = ImVec4(0.22f, 0.46f, 0.73f, 1.00f);
	c[ImGuiCol_DragDropTarget] = ImVec4(0.22f, 0.46f, 0.73f, 0.90f);
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.58f);
	c[ImGuiCol_TableHeaderBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
	c[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
	c[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
	c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
}

void VansGraphics::VansEditorTheme::PushToolbarActionColors(VansEditorToolbarAction action)
{
	ImVec4 base;
	ImVec4 hovered;
	ImVec4 active;
	switch (action)
	{
	case VansEditorToolbarAction::Play:
		base = ImVec4(0.13f, 0.45f, 0.13f, 1.00f);
		hovered = ImVec4(0.18f, 0.60f, 0.18f, 1.00f);
		active = ImVec4(0.10f, 0.36f, 0.10f, 1.00f);
		break;
	case VansEditorToolbarAction::Pause:
		base = ImVec4(0.50f, 0.40f, 0.05f, 1.00f);
		hovered = ImVec4(0.70f, 0.55f, 0.08f, 1.00f);
		active = ImVec4(0.40f, 0.32f, 0.04f, 1.00f);
		break;
	case VansEditorToolbarAction::Stop:
		base = ImVec4(0.45f, 0.10f, 0.10f, 1.00f);
		hovered = ImVec4(0.65f, 0.14f, 0.14f, 1.00f);
		active = ImVec4(0.36f, 0.08f, 0.08f, 1.00f);
		break;
	}
	ImGui::PushStyleColor(ImGuiCol_Button, base);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
}

void VansGraphics::VansEditorTheme::PopToolbarActionColors()
{
	ImGui::PopStyleColor(3);
}
