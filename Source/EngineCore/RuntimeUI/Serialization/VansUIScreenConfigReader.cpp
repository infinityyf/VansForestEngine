#include "VansUIScreenConfigReader.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <utility>

namespace VansRuntime
{
	namespace
	{
		VansUIScreenLayer ParseLayer(const std::string& value)
		{
			if (value == "WorldSpaceUI") return VansUIScreenLayer::WorldSpaceUI;
			if (value == "HUD") return VansUIScreenLayer::HUD;
			if (value == "ModalDim") return VansUIScreenLayer::ModalDim;
			if (value == "Modal") return VansUIScreenLayer::Modal;
			if (value == "Tooltip") return VansUIScreenLayer::Tooltip;
			if (value == "Toast") return VansUIScreenLayer::Toast;
			if (value == "DebugUI") return VansUIScreenLayer::DebugUI;
			return VansUIScreenLayer::Screen;
		}

		VansUIInputMode ParseInputMode(const std::string& value)
		{
			if (value == "ConsumeIfHit") return VansUIInputMode::ConsumeIfHit;
			if (value == "Exclusive") return VansUIInputMode::Exclusive;
			if (value == "Modal") return VansUIInputMode::Modal;
			return VansUIInputMode::PassThrough;
		}

		std::string ReadGuidReference(const Vans::VansSerializedValue& root, const char* fieldName)
		{
			const Vans::VansSerializedValue* field = Vans::FindObjectField(root, fieldName);
			if (!field || field->kind != Vans::VansSerializedValue::Kind::Object)
				return {};
			return Vans::ReadSerializedStringField(*field, "guid");
		}

		void ReadGuidReferenceArray(const Vans::VansSerializedValue& root, const char* fieldName, std::vector<std::string>& out)
		{
			const Vans::VansSerializedValue* field = Vans::FindObjectField(root, fieldName);
			if (!field || field->kind != Vans::VansSerializedValue::Kind::Array)
				return;
			for (const Vans::VansSerializedValue& item : field->arrayItems)
				if (item.kind == Vans::VansSerializedValue::Kind::Object)
				{
					const std::string guid = Vans::ReadSerializedStringField(item, "guid");
					if (!guid.empty()) out.push_back(guid);
				}
		}

		void ReadStringArray(const Vans::VansSerializedValue& root, const char* fieldName, std::vector<std::string>& out)
		{
			const Vans::VansSerializedValue* field = Vans::FindObjectField(root, fieldName);
			if (!field || field->kind != Vans::VansSerializedValue::Kind::Array)
				return;
			for (const Vans::VansSerializedValue& item : field->arrayItems)
				if (item.kind == Vans::VansSerializedValue::Kind::String) out.push_back(item.stringValue);
		}

		VansUIVariant ReadVariant(const Vans::VansSerializedValue& value)
		{
			switch (value.kind)
			{
			case Vans::VansSerializedValue::Kind::Bool:
				return VansUIVariant(value.boolValue);
			case Vans::VansSerializedValue::Kind::Int:
				return VansUIVariant(value.intValue);
			case Vans::VansSerializedValue::Kind::Float:
				return VansUIVariant(value.floatValue);
			case Vans::VansSerializedValue::Kind::String:
				return VansUIVariant(value.stringValue);
			case Vans::VansSerializedValue::Kind::Array:
			{
				VansUIVariantArray result;
				result.reserve(value.arrayItems.size());
				for (const Vans::VansSerializedValue& item : value.arrayItems)
					result.push_back(ReadVariant(item));
				return VansUIVariant(std::move(result));
			}
			case Vans::VansSerializedValue::Kind::Object:
			{
				VansUIVariantMap result;
				for (const auto& [name, field] : value.objectFields)
					result.emplace(name, ReadVariant(field));
				return VansUIVariant(std::move(result));
			}
			case Vans::VansSerializedValue::Kind::Null:
			default:
				return VansUIVariant();
			}
		}

		VansUIVariantMap ReadVariantMapField(
			const Vans::VansSerializedValue& root,
			const char* fieldName)
		{
			VansUIVariantMap result;
			const Vans::VansSerializedValue* field = Vans::FindObjectField(root, fieldName);
			if (!field || field->kind != Vans::VansSerializedValue::Kind::Object)
				return result;

			for (const auto& [name, value] : field->objectFields)
				result.emplace(name, ReadVariant(value));
			return result;
		}
	}

	bool VansUIScreenConfigReader::Read(
		const Vans::VansSerializedValue& root,
		VansUIScreenConfig& config,
		std::vector<std::string>& diagnostics)
	{
		if (root.kind != Vans::VansSerializedValue::Kind::Object)
		{
			diagnostics.push_back("UI screen config root must be an object.");
			return false;
		}

		config.schemaVersion = static_cast<std::uint32_t>(
			std::max<std::int64_t>(1, Vans::ReadSerializedIntField(root, "schemaVersion", 1)));
		config.guid = Vans::ReadSerializedStringField(root, "guid");
		config.name = Vans::ReadSerializedStringField(root, "name");
		config.xamlAssetGuid = ReadGuidReference(root, "xaml");
		config.layer = ParseLayer(Vans::ReadSerializedStringField(root, "layer", "Screen"));
		config.zOrder = static_cast<std::int32_t>(Vans::ReadSerializedIntField(root, "zOrder", 0));

		if (const Vans::VansSerializedValue* inputPolicy = Vans::FindObjectField(root, "inputPolicy");
			inputPolicy && inputPolicy->kind == Vans::VansSerializedValue::Kind::Object)
		{
			config.inputPolicy.mouse = ParseInputMode(Vans::ReadSerializedStringField(*inputPolicy, "mouse", "PassThrough"));
			config.inputPolicy.keyboard = ParseInputMode(Vans::ReadSerializedStringField(*inputPolicy, "keyboard", "PassThrough"));
			config.inputPolicy.gamepad = ParseInputMode(Vans::ReadSerializedStringField(*inputPolicy, "gamepad", "PassThrough"));
			config.inputPolicy.touch = ParseInputMode(Vans::ReadSerializedStringField(*inputPolicy, "touch", "PassThrough"));
		}

		ReadGuidReferenceArray(root, "themes", config.themeAssetGuids);
		ReadGuidReferenceArray(root, "tokens", config.tokenAssetGuids);
		ReadGuidReferenceArray(root, "localization", config.localizationAssetGuids);
		ReadStringArray(root, "dependencies", config.dependencies);

		if (const Vans::VansSerializedValue* viewModel = Vans::FindObjectField(root, "viewModel");
			viewModel && viewModel->kind == Vans::VansSerializedValue::Kind::Object)
		{
			config.viewModel.type = Vans::ReadSerializedStringField(*viewModel, "type");
			config.viewModel.source = Vans::ReadSerializedStringField(*viewModel, "source");
			config.viewModel.factory = Vans::ReadSerializedStringField(*viewModel, "factory");
		}

		if (const Vans::VansSerializedValue* events = Vans::FindObjectField(root, "events");
			events && events->kind == Vans::VansSerializedValue::Kind::Array)
		{
			for (const Vans::VansSerializedValue& event : events->arrayItems)
			{
				if (event.kind != Vans::VansSerializedValue::Kind::Object)
					continue;
				VansUIScreenEventBindingConfig binding;
				binding.source = Vans::ReadSerializedStringField(event, "source");
				binding.eventName = Vans::ReadSerializedStringField(event, "event");
				binding.action = Vans::ReadSerializedStringField(event, "action");
				binding.params = ReadVariantMapField(event, "params");
				if (!binding.source.empty() && !binding.eventName.empty() && !binding.action.empty())
					config.events.push_back(std::move(binding));
			}
		}

		if (const Vans::VansSerializedValue* states = Vans::FindObjectField(root, "states");
			states && states->kind == Vans::VansSerializedValue::Kind::Array)
		{
			for (const Vans::VansSerializedValue& stateValue : states->arrayItems)
			{
				if (stateValue.kind != Vans::VansSerializedValue::Kind::Object)
					continue;

				VansUIScreenStateGroupConfig state;
				state.name = Vans::ReadSerializedStringField(stateValue, "name");
				state.defaultState = Vans::ReadSerializedStringField(stateValue, "default");
				ReadStringArray(stateValue, "values", state.values);
				if (!state.name.empty())
					config.states.push_back(std::move(state));
			}
		}

		if (const Vans::VansSerializedValue* animations = Vans::FindObjectField(root, "animations");
			animations && animations->kind == Vans::VansSerializedValue::Kind::Object)
		{
			for (const auto& [name, value] : animations->objectFields)
			{
				if (value.kind == Vans::VansSerializedValue::Kind::String)
					config.animations.emplace(name, value.stringValue);
			}
		}

		if (const Vans::VansSerializedValue* budget = Vans::FindObjectField(root, "performanceBudget");
			budget && budget->kind == Vans::VansSerializedValue::Kind::Object)
		{
			config.performanceBudget.maxDrawCalls = static_cast<std::uint32_t>(
				std::max<std::int64_t>(0, Vans::ReadSerializedIntField(*budget, "maxDrawCalls", config.performanceBudget.maxDrawCalls)));
			config.performanceBudget.maxTextureMemoryMB = static_cast<std::uint32_t>(
				std::max<std::int64_t>(0, Vans::ReadSerializedIntField(*budget, "maxTextureMemoryMB", config.performanceBudget.maxTextureMemoryMB)));
			if (const Vans::VansSerializedValue* maxLayoutMs = Vans::FindObjectField(*budget, "maxLayoutMs"))
			{
				config.performanceBudget.maxLayoutMs = Vans::ReadSerializedNumber(
					*maxLayoutMs,
					config.performanceBudget.maxLayoutMs);
			}
			config.performanceBudget.maxBindingUpdatesPerFrame = static_cast<std::uint32_t>(
				std::max<std::int64_t>(0, Vans::ReadSerializedIntField(*budget, "maxBindingUpdatesPerFrame", config.performanceBudget.maxBindingUpdatesPerFrame)));
			config.performanceBudget.maxAnimations = static_cast<std::uint32_t>(
				std::max<std::int64_t>(0, Vans::ReadSerializedIntField(*budget, "maxAnimations", config.performanceBudget.maxAnimations)));
		}

		if (config.guid.empty())
			diagnostics.push_back("UI screen config is missing required field: guid.");
		if (config.name.empty())
			diagnostics.push_back("UI screen config is missing required field: name.");
		if (config.xamlAssetGuid.empty())
			diagnostics.push_back("UI screen config is missing required field: xaml.");

		return diagnostics.empty();
	}
}
