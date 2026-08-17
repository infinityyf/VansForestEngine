#include "VansUITimelineIntegration.h"

#include "../Public/VansUIElementHandle.h"
#include "../Public/VansUIScreen.h"
#include "../Public/VansUIScreenManager.h"
#include "../Public/VansUISystem.h"
#include "../Public/VansUIViewModel.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"

namespace Vans
{
namespace
{
VansRuntime::VansUIVariant ToUIVariant(const VansTimelineValue& value)
{
	if (const auto* typed = std::get_if<bool>(&value)) return *typed;
	if (const auto* typed = std::get_if<std::int32_t>(&value)) return static_cast<std::int64_t>(*typed);
	if (const auto* typed = std::get_if<std::int64_t>(&value)) return *typed;
	if (const auto* typed = std::get_if<float>(&value)) return static_cast<double>(*typed);
	if (const auto* typed = std::get_if<double>(&value)) return *typed;
	if (const auto* typed = std::get_if<std::string>(&value)) return *typed;
	return {};
}

std::string ToUIString(const VansTimelineValue& value)
{
	if (const auto* typed = std::get_if<std::string>(&value)) return *typed;
	if (const auto* typed = std::get_if<bool>(&value)) return *typed ? "true" : "false";
	if (const auto* typed = std::get_if<std::int32_t>(&value)) return std::to_string(*typed);
	if (const auto* typed = std::get_if<std::int64_t>(&value)) return std::to_string(*typed);
	if (const auto* typed = std::get_if<float>(&value)) return std::to_string(*typed);
	if (const auto* typed = std::get_if<double>(&value)) return std::to_string(*typed);
	return {};
}

bool ToBool(const VansTimelineValue& value, bool fallback)
{
	if (const auto* typed = std::get_if<bool>(&value)) return *typed;
	if (const auto* typed = std::get_if<float>(&value)) return *typed != 0.0f;
	if (const auto* typed = std::get_if<double>(&value)) return *typed != 0.0;
	return fallback;
}

struct UIRestoreState
{
	VansTimelineWriterHandle writer;
	std::shared_ptr<VansRuntime::VansUIScreen> screen;
	std::string targetKind;
	std::string element;
	std::string descriptor;
	std::string previousText;
	bool previousVisible = true;
	bool hadViewModelValue = false;
	VansRuntime::VansUIVariant previousViewModelValue;
};

class UITimelineApplier final : public IVansTimelineOutputApplier
{
public:
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::UIState) + ".Output"); }
	std::string_view StableName() const override { return "UI.UIStateTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget&, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { VansTimelineApplyStatus::Ignored };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const auto text = [&](std::size_t slot)
		{
			const VansTimelineValue* value = reader.ValueAt(context.section->extensionData, slot);
			const auto* typed = value ? std::get_if<std::string>(value) : nullptr;
			return typed ? *typed : std::string{};
		};
		const std::string screenName = text(0);
		const std::string targetKind = text(1);
		const std::string elementName = text(2);
		const std::string descriptor = text(3);
		if (screenName.empty()) return { VansTimelineApplyStatus::Failed, {}, "UI State screen is missing" };
		auto screen = VansRuntime::VansUISystem::Get().GetScreenManager().GetScreenByName(screenName);
		if (!screen) return { VansTimelineApplyStatus::Failed, {}, "UI State screen is not open" };
		VansTimelineValue sampled;
		if (!context.section->channels.empty())
			if (const auto value = VansTimelineEvaluator::SampleChannel(context.section->channels.front(), sample->localTick))
				sampled = *value;
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			UIRestoreState result; result.writer = context.writer; result.screen = screen;
			result.targetKind = targetKind; result.element = elementName; result.descriptor = descriptor;
			if (targetKind == "Screen") result.previousVisible = screen->IsVisible();
			else if (targetKind == "ViewModel")
			{
				if (auto viewModel = screen->GetViewModel())
					if (const auto* previous = viewModel->GetValue(descriptor))
					{ result.hadViewModelValue = true; result.previousViewModelValue = *previous; }
			}
			else
			{
				auto element = screen->FindElement(elementName);
				if (element.IsValid())
				{
					if (descriptor == "Element.Visible") result.previousVisible = element.IsVisible();
					else if (descriptor == "Element.Text") result.previousText = element.GetText();
					else element.TryGetProperty(descriptor, result.previousText);
				}
			}
			return result;
		});
		(void)state;
		if (targetKind == "Screen") ToBool(sampled, true) ? screen->Show() : screen->Hide();
		else if (targetKind == "ViewModel")
		{
			auto viewModel = screen->GetViewModel();
			if (!viewModel || descriptor.empty())
				return { VansTimelineApplyStatus::Failed, {}, "UI ViewModel target is invalid" };
			viewModel->SetValue(descriptor, ToUIVariant(sampled));
		}
		else
		{
			if (elementName.empty() || descriptor.empty())
				return { VansTimelineApplyStatus::Failed, {}, "UI element target is incomplete" };
			auto element = screen->FindElement(elementName);
			if (!element.IsValid()) return { VansTimelineApplyStatus::Failed, {}, "UI element is missing" };
			if (descriptor == "Element.Visible") element.SetVisible(ToBool(sampled, true));
			else if (descriptor == "Element.Text") element.SetText(ToUIString(sampled));
			else element.SetProperty(descriptor, ToUIString(sampled));
		}
		const VansTimelineResourceId resource{ VansStableHash64("UI.Property"),
			VansStableHash64(screenName + "#" + targetKind + "#" + elementName + "#" + descriptor) };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		UIRestoreState* state = m_State.Resolve(token.handle);
		if (!state || !state->screen) return false;
		if (state->targetKind == "Screen") state->previousVisible ? state->screen->Show() : state->screen->Hide();
		else if (state->targetKind == "ViewModel")
		{
			if (auto viewModel = state->screen->GetViewModel())
				state->hadViewModelValue ? viewModel->SetValue(state->descriptor, state->previousViewModelValue)
					: viewModel->RemoveValue(state->descriptor);
		}
		else
		{
			auto element = state->screen->FindElement(state->element);
			if (element.IsValid())
			{
				if (state->descriptor == "Element.Visible") element.SetVisible(state->previousVisible);
				else if (state->descriptor == "Element.Text") element.SetText(state->previousText);
				else element.SetProperty(state->descriptor, state->previousText);
			}
		}
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansTimelineModuleApplierState<UIRestoreState> m_State;
};
}

bool VansRegisterUITimelineExtensions(VansTimelineTrackExtensionRegistry& registry, std::string& error)
{
	using F = VansTimelineValueType;
	return registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::UIState, "UI State", "UI", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::None, VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("screen", F::String, std::string(), true),
			VansMakeTimelineSourceField("targetKind", F::Enum, std::string("Screen"), false,
				{ "Screen", "ViewModel", "Element" }),
			VansMakeTimelineSourceField("element", F::String, std::string()),
			VansMakeTimelineSourceField("descriptorId", F::String, std::string()),
			VansMakeTimelineSourceField("valueType", F::Enum, std::string("Bool"), false,
				{ "Bool", "Int32", "Int64", "Float", "Double", "String" }) },
			{ VansMakeTimelineChannelSchema("value", F::Bool, true, "valueType") }, false, false }), error);
}

bool VansRegisterUITimelineIntegration(VansTimelineApplierRegistry& registry, std::string& error)
{
	return registry.Register(std::make_shared<UITimelineApplier>(), error);
}
}
