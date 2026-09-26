#include "VansEditorConfiguration.h"

#include "../AssetCore/Serialization/VansJsonDocumentCodec.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace VansGraphics
{
namespace
{
using Json = nlohmann::ordered_json;

bool RequireExactFields(
	const Json& object,
	std::initializer_list<std::string_view> fields,
	std::string_view address,
	std::string& error)
{
	if (!object.is_object())
	{
		error = std::string(address) + " must be an object";
		return false;
	}
	std::set<std::string> expected;
	for (const std::string_view field : fields)
		expected.emplace(field);
	for (auto member = object.begin(); member != object.end(); ++member)
	{
		if (!expected.erase(member.key()))
		{
			error = std::string(address) + " contains unknown field '" + member.key() + "'";
			return false;
		}
	}
	if (!expected.empty())
	{
		error = std::string(address) + " is missing field '" + *expected.begin() + "'";
		return false;
	}
	return true;
}

bool ReadFiniteFloat(
	const Json& object,
	std::string_view field,
	float& value,
	std::string_view address,
	std::string& error,
	bool requirePositive = false)
{
	const auto found = object.find(std::string(field));
	if (found == object.end() || !found->is_number())
	{
		error = std::string(address) + "." + std::string(field) + " must be a number";
		return false;
	}
	value = found->get<float>();
	if (!std::isfinite(value) || (requirePositive && value <= 0.0f) || (!requirePositive && value < 0.0f))
	{
		error = std::string(address) + "." + std::string(field) +
			(requirePositive ? " must be finite and greater than zero" : " must be finite and non-negative");
		return false;
	}
	return true;
}

bool ReadVector2(
	const Json& object,
	std::string_view field,
	std::array<float, 2>& value,
	std::string_view address,
	std::string& error)
{
	const auto found = object.find(std::string(field));
	if (found == object.end() || !found->is_array() || found->size() != 2 ||
		!(*found)[0].is_number() || !(*found)[1].is_number())
	{
		error = std::string(address) + "." + std::string(field) + " must be a two-number array";
		return false;
	}
	value = { (*found)[0].get<float>(), (*found)[1].get<float>() };
	if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || value[0] < 0.0f || value[1] < 0.0f)
	{
		error = std::string(address) + "." + std::string(field) + " values must be finite and non-negative";
		return false;
	}
	return true;
}

bool ReadFontCandidates(
	const Json& fonts,
	std::string_view field,
	std::vector<std::string>& candidates,
	std::string& error)
{
	const auto found = fonts.find(std::string(field));
	if (found == fonts.end() || !found->is_array() || found->empty())
	{
		error = "fonts." + std::string(field) + " must be a non-empty array";
		return false;
	}
	candidates.clear();
	for (const Json& candidate : *found)
	{
		if (!candidate.is_string() || candidate.get_ref<const std::string&>().empty())
		{
			error = "fonts." + std::string(field) + " must contain non-empty file names";
			return false;
		}
		candidates.push_back(candidate.get<std::string>());
	}
	return true;
}
}

std::filesystem::path VansEditorConfiguration::ResolveBuiltInPath()
{
	std::array<char, 32768> executablePath{};
	const DWORD length = GetModuleFileNameA(
		nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
	if (length == 0 || length >= executablePath.size())
		return {};
	return std::filesystem::path(std::string(executablePath.data(), length)).parent_path().parent_path() /
		"EngineAssets" / "Editor" / "EditorConfiguration.json";
}

bool VansEditorConfiguration::Decode(
	std::string_view bytes,
	VansEditorConfiguration& configuration,
	std::string& error)
{
	Json root;
	if (!Vans::VansJsonDocumentCodec::Parse(std::string(bytes), root, error))
	{
		error = "Editor configuration JSON parse failed: " + error;
		return false;
	}
	if (!RequireExactFields(root, { "fonts", "style", "toolbar", "windows", "package" }, "root", error))
		return false;

	VansEditorConfiguration next;
	const Json& fonts = root["fonts"];
	if (!RequireExactFields(fonts, { "sizePixels", "primaryCandidates", "fallbackCandidates" }, "fonts", error) ||
		!ReadFiniteFloat(fonts, "sizePixels", next.fonts.sizePixels, "fonts", error, true) ||
		!ReadFontCandidates(fonts, "primaryCandidates", next.fonts.primaryCandidates, error) ||
		!ReadFontCandidates(fonts, "fallbackCandidates", next.fonts.fallbackCandidates, error))
		return false;

	const Json& style = root["style"];
	if (!RequireExactFields(style, {
		"windowRounding", "childRounding", "frameRounding", "popupRounding",
		"scrollbarRounding", "grabRounding", "tabRounding", "windowPadding",
		"framePadding", "itemSpacing", "itemInnerSpacing", "indentSpacing",
		"scrollbarSize", "grabMinSize", "windowBorderSize", "childBorderSize",
		"frameBorderSize", "popupBorderSize", "tabBorderSize", "windowTitleAlign",
		"separatorTextAlign" }, "style", error) ||
		!ReadFiniteFloat(style, "windowRounding", next.style.windowRounding, "style", error) ||
		!ReadFiniteFloat(style, "childRounding", next.style.childRounding, "style", error) ||
		!ReadFiniteFloat(style, "frameRounding", next.style.frameRounding, "style", error) ||
		!ReadFiniteFloat(style, "popupRounding", next.style.popupRounding, "style", error) ||
		!ReadFiniteFloat(style, "scrollbarRounding", next.style.scrollbarRounding, "style", error) ||
		!ReadFiniteFloat(style, "grabRounding", next.style.grabRounding, "style", error) ||
		!ReadFiniteFloat(style, "tabRounding", next.style.tabRounding, "style", error) ||
		!ReadVector2(style, "windowPadding", next.style.windowPadding, "style", error) ||
		!ReadVector2(style, "framePadding", next.style.framePadding, "style", error) ||
		!ReadVector2(style, "itemSpacing", next.style.itemSpacing, "style", error) ||
		!ReadVector2(style, "itemInnerSpacing", next.style.itemInnerSpacing, "style", error) ||
		!ReadFiniteFloat(style, "indentSpacing", next.style.indentSpacing, "style", error) ||
		!ReadFiniteFloat(style, "scrollbarSize", next.style.scrollbarSize, "style", error, true) ||
		!ReadFiniteFloat(style, "grabMinSize", next.style.grabMinSize, "style", error, true) ||
		!ReadFiniteFloat(style, "windowBorderSize", next.style.windowBorderSize, "style", error) ||
		!ReadFiniteFloat(style, "childBorderSize", next.style.childBorderSize, "style", error) ||
		!ReadFiniteFloat(style, "frameBorderSize", next.style.frameBorderSize, "style", error) ||
		!ReadFiniteFloat(style, "popupBorderSize", next.style.popupBorderSize, "style", error) ||
		!ReadFiniteFloat(style, "tabBorderSize", next.style.tabBorderSize, "style", error) ||
		!ReadVector2(style, "windowTitleAlign", next.style.windowTitleAlign, "style", error) ||
		!ReadVector2(style, "separatorTextAlign", next.style.separatorTextAlign, "style", error))
		return false;

	const Json& toolbar = root["toolbar"];
	if (!RequireExactFields(toolbar, { "buttonWidth", "buttonHeight", "buttonSpacing" }, "toolbar", error) ||
		!ReadFiniteFloat(toolbar, "buttonWidth", next.toolbar.buttonWidth, "toolbar", error, true) ||
		!ReadFiniteFloat(toolbar, "buttonHeight", next.toolbar.buttonHeight, "toolbar", error, true) ||
		!ReadFiniteFloat(toolbar, "buttonSpacing", next.toolbar.buttonSpacing, "toolbar", error))
		return false;

	const Json& windows = root["windows"];
	if (!windows.is_object())
	{
		error = "windows must be an object";
		return false;
	}
	std::set<std::string> configuredWindows;
	for (const VansEditorWindowDescriptor& descriptor : VansEditorWindowCatalog::All())
	{
		const std::string key(descriptor.configurationKey);
		const auto found = windows.find(key);
		if (found == windows.end() || !found->is_boolean())
		{
			error = "windows." + key + " must be a boolean";
			return false;
		}
		next.windowDefaults[static_cast<std::size_t>(descriptor.id)] = found->get<bool>();
		configuredWindows.insert(key);
	}
	if (windows.size() != configuredWindows.size())
	{
		error = "windows contains an unknown field";
		return false;
	}

	const Json& package = root["package"];
	if (!RequireExactFields(package, { "platform" }, "package", error) || !package["platform"].is_string() ||
		!Vans::ParseGamePackagePlatform(package["platform"].get<std::string>(), next.packagePlatform))
	{
		if (error.empty()) error = "package.platform is not a supported platform";
		return false;
	}

	configuration = std::move(next);
	error.clear();
	return true;
}

bool VansEditorConfiguration::Load(
	const std::filesystem::path& path,
	VansEditorConfiguration& configuration,
	std::string& error)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		error = "Editor configuration file is unavailable: " + path.string();
		return false;
	}
	const std::string bytes{
		std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	if (!Decode(bytes, configuration, error))
	{
		error = path.string() + ": " + error;
		return false;
	}
	return true;
}
}
