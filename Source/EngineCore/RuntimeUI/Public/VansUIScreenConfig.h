#pragma once

#include "VansUIVariant.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansRuntime
{
	enum class VansUIScreenLayer
	{
		WorldSpaceUI,
		HUD,
		Screen,
		ModalDim,
		Modal,
		Tooltip,
		Toast,
		DebugUI
	};

	enum class VansUIInputMode
	{
		PassThrough,
		ConsumeIfHit,
		Exclusive,
		Modal
	};

	struct VansUIScreenInputPolicy
	{
		VansUIInputMode mouse = VansUIInputMode::PassThrough;
		VansUIInputMode keyboard = VansUIInputMode::PassThrough;
		VansUIInputMode gamepad = VansUIInputMode::PassThrough;
		VansUIInputMode touch = VansUIInputMode::PassThrough;
	};

	struct VansUIScreenEventBindingConfig
	{
		std::string source;
		std::string eventName;
		std::string action;
		VansUIVariantMap params;
	};

	struct VansUIScreenPerformanceBudget
	{
		std::uint32_t maxDrawCalls = 80;
		std::uint32_t maxTextureMemoryMB = 32;
		double maxLayoutMs = 1.0;
		std::uint32_t maxBindingUpdatesPerFrame = 200;
		std::uint32_t maxAnimations = 32;
	};

	struct VansUIScreenViewModelConfig
	{
		std::string type;
		std::string source;
		std::string factory;
	};

	struct VansUIScreenStateGroupConfig
	{
		std::string name;
		std::string defaultState;
		std::vector<std::string> values;
	};

	struct VansUIScreenConfig
	{
		std::uint32_t schemaVersion = 1;
		std::string guid;
		std::string name;
		std::string xamlAssetGuid;
		VansUIScreenLayer layer = VansUIScreenLayer::Screen;
		std::int32_t zOrder = 0;
		VansUIScreenInputPolicy inputPolicy;
		VansUIScreenViewModelConfig viewModel;
		std::vector<std::string> themeAssetGuids;
		std::vector<std::string> tokenAssetGuids;
		std::vector<std::string> localizationAssetGuids;
		std::vector<std::string> dependencies;
		std::vector<VansUIScreenEventBindingConfig> events;
		std::vector<VansUIScreenStateGroupConfig> states;
		std::unordered_map<std::string, std::string> animations;
		VansUIScreenPerformanceBudget performanceBudget;
		std::string sourceAssetGuid;
	};
}
