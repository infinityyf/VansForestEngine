#include "VansUIDocumentValidator.h"

#include <algorithm>
#include <unordered_set>

namespace VansRuntime
{
	bool VansUIDocumentValidator::ValidateScreenConfig(
		const VansUIScreenConfig& config,
		std::vector<std::string>& diagnostics)
	{
		if (config.guid.empty())
			diagnostics.push_back("UI screen guid must not be empty.");
		if (config.name.empty())
			diagnostics.push_back("UI screen name must not be empty.");
		if (config.xamlAssetGuid.empty())
			diagnostics.push_back("UI screen xaml path must not be empty.");
		for (const VansUIScreenEventBindingConfig& event : config.events)
		{
			if (event.source.empty() || event.eventName.empty() || event.action.empty())
				diagnostics.push_back("UI event bindings require source, event, and action.");
			if (!event.eventName.empty() && event.eventName != "Click")
				diagnostics.push_back("UI event binding currently supports Click only: " + event.eventName);
		}
		for (const std::string& theme : config.themeAssetGuids)
			if (theme.empty())
				diagnostics.push_back("UI screen theme path must not be empty.");
		for (const std::string& tokens : config.tokenAssetGuids)
			if (tokens.empty())
				diagnostics.push_back("UI screen token path must not be empty.");
		for (const std::string& localization : config.localizationAssetGuids)
			if (localization.empty())
				diagnostics.push_back("UI screen localization path must not be empty.");
		for (const std::string& dependency : config.dependencies)
			if (dependency.empty())
				diagnostics.push_back("UI screen dependency path must not be empty.");

		std::unordered_set<std::string> stateGroupNames;
		for (const VansUIScreenStateGroupConfig& stateGroup : config.states)
		{
			if (stateGroup.name.empty())
				diagnostics.push_back("UI screen state group name must not be empty.");
			if (!stateGroup.name.empty() && !stateGroupNames.insert(stateGroup.name).second)
				diagnostics.push_back("UI screen state group name is duplicated: " + stateGroup.name);
			if (!stateGroup.defaultState.empty() &&
				std::find(stateGroup.values.begin(), stateGroup.values.end(), stateGroup.defaultState) == stateGroup.values.end())
			{
				diagnostics.push_back("UI screen default state is not listed in values: " + stateGroup.name);
			}
		}
		for (const auto& [name, storyboard] : config.animations)
		{
			if (name.empty() || storyboard.empty())
				diagnostics.push_back("UI screen animation entries require a name and storyboard.");
		}
		if (config.performanceBudget.maxDrawCalls == 0)
			diagnostics.push_back("UI screen performanceBudget.maxDrawCalls must be greater than zero.");
		if (config.performanceBudget.maxTextureMemoryMB == 0)
			diagnostics.push_back("UI screen performanceBudget.maxTextureMemoryMB must be greater than zero.");
		if (config.performanceBudget.maxLayoutMs <= 0.0)
			diagnostics.push_back("UI screen performanceBudget.maxLayoutMs must be greater than zero.");
		if (config.performanceBudget.maxBindingUpdatesPerFrame == 0)
			diagnostics.push_back("UI screen performanceBudget.maxBindingUpdatesPerFrame must be greater than zero.");
		return diagnostics.empty();
	}

	bool VansUIDocumentValidator::ValidateComponentConfig(
		const VansUIComponentConfig& config,
		std::vector<std::string>& diagnostics)
	{
		if (config.guid.empty())
			diagnostics.push_back("UI component guid must not be empty.");
		if (config.name.empty())
			diagnostics.push_back("UI component name must not be empty.");
		if (config.xamlAssetGuid.empty())
			diagnostics.push_back("UI component xaml path must not be empty.");

		for (const VansUIComponentPropertyConfig& property : config.publicProperties)
		{
			if (property.name.empty())
				diagnostics.push_back("UI component public property name must not be empty.");
			if (property.type.empty())
				diagnostics.push_back("UI component public property type must not be empty.");
		}
		std::unordered_set<std::string> propertyNames;
		for (const VansUIComponentPropertyConfig& property : config.publicProperties)
		{
			if (!property.name.empty() && !propertyNames.insert(property.name).second)
				diagnostics.push_back("UI component public property name is duplicated: " + property.name);
		}
		for (const VansUIComponentEventConfig& event : config.events)
		{
			if (event.name.empty())
				diagnostics.push_back("UI component event name must not be empty.");
			if (event.source.empty())
				diagnostics.push_back("UI component event source must not be empty.");
			if (!event.name.empty() && event.name != "Click")
				diagnostics.push_back("UI component event currently supports Click only: " + event.name);
			if (!event.actionParam.empty() && propertyNames.find(event.actionParam) == propertyNames.end())
				diagnostics.push_back("UI component actionParam must refer to a public property: " + event.actionParam);
		}
		std::unordered_set<std::string> stateNames;
		bool hasDefaultState = false;
		for (const VansUIComponentStateConfig& state : config.states)
		{
			if (!state.name.empty() && !stateNames.insert(state.name).second)
				diagnostics.push_back("UI component state name is duplicated: " + state.name);
			hasDefaultState = hasDefaultState || state.isDefault;
		}
		if (!config.states.empty() && !hasDefaultState)
			diagnostics.push_back("UI component states require one default state.");
		return diagnostics.empty();
	}
}
