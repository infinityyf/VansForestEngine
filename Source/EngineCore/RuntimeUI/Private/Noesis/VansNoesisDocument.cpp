#include "VansNoesisDocument.h"

#include "VansNoesisInputAdapter.h"
#include "../../Public/VansUIViewModel.h"

#include <NsCore/Boxing.h>
#include <NsCore/DynamicCast.h>
#include <NsCore/ReflectionImplement.h>
#include <NsCore/String.h>
#include <NsGui/BaseButton.h>
#include <NsGui/BaseCommand.h>
#include <NsGui/ContentControl.h>
#include <NsGui/DependencyObject.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/INotifyPropertyChanged.h>
#include <NsGui/IRenderer.h>
#include <NsGui/IView.h>
#include <NsGui/LogicalTreeHelper.h>
#include <NsGui/TextBlock.h>
#include <NsGui/UIElement.h>

#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VansRuntime
{
    namespace
    {
        std::string VariantToDisplayString(const VansUIVariant& value)
        {
            return std::visit([](const auto& typedValue) -> std::string
            {
                using T = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return {};
                else if constexpr (std::is_same_v<T, bool>)
                    return typedValue ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return std::to_string(typedValue);
                else if constexpr (std::is_same_v<T, double>)
                    return std::to_string(typedValue);
                else if constexpr (std::is_same_v<T, std::string>)
                    return typedValue;
                else if constexpr (std::is_same_v<T, VansUIHandleId>)
                    return std::to_string(typedValue);
                else
                    return {};
            }, value.value);
        }

        bool StartsWith(const std::string& value, const std::string& prefix)
        {
            return value.size() >= prefix.size() &&
                value.compare(0, prefix.size(), prefix) == 0;
        }

        bool EndsWith(const std::string& value, const std::string& suffix)
        {
            return value.size() >= suffix.size() &&
                value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        void AddCandidate(std::vector<std::string>& candidates,
                          std::unordered_set<std::string>& seen,
                          std::string value)
        {
            if (!value.empty() && seen.insert(value).second)
                candidates.push_back(std::move(value));
        }

        std::vector<std::string> BuildCommandCandidates(const std::string& elementName)
        {
            std::vector<std::string> bases;
            std::unordered_set<std::string> seenBases;
            AddCandidate(bases, seenBases, elementName);

            if (EndsWith(elementName, "Command"))
                AddCandidate(bases, seenBases, elementName.substr(0, elementName.size() - 7));
            if (EndsWith(elementName, "Button"))
                AddCandidate(bases, seenBases, elementName.substr(0, elementName.size() - 6));
            if (StartsWith(elementName, "Btn") && elementName.size() > 3)
                AddCandidate(bases, seenBases, elementName.substr(3));
            if (StartsWith(elementName, "Button") && elementName.size() > 6)
                AddCandidate(bases, seenBases, elementName.substr(6));

            std::vector<std::string> candidates;
            std::unordered_set<std::string> seenCandidates;
            for (const std::string& base : bases)
            {
                AddCandidate(candidates, seenCandidates, base);
                AddCandidate(candidates, seenCandidates, base + "Command");
                AddCandidate(candidates, seenCandidates, "Open" + base);
                AddCandidate(candidates, seenCandidates, "Open" + base + "Command");
                AddCandidate(candidates, seenCandidates, "Close" + base);
                AddCandidate(candidates, seenCandidates, "Close" + base + "Command");
            }
            return candidates;
        }

        std::string CommandParameterToString(Noesis::BaseComponent* parameter)
        {
            if (!parameter)
                return {};

            if (Noesis::Boxing::CanUnbox<Noesis::String>(parameter))
                return Noesis::Boxing::Unbox<Noesis::String>(parameter).Str();

            return {};
        }

        class VansNoesisViewModelAdapter final : public Noesis::BaseComponent,
                                                 public Noesis::INotifyPropertyChanged
        {
        public:
            explicit VansNoesisViewModelAdapter(VansUIViewModel* viewModel)
                : m_ViewModel(viewModel)
            {}

            Noesis::PropertyChangedEventHandler& PropertyChanged() override
            {
                return m_PropertyChanged;
            }

            void NotifyPropertyChanged(const std::string& propertyName)
            {
                if (m_PropertyChanged)
                    m_PropertyChanged(this, Noesis::PropertyChangedEventArgs(
                        Noesis::Symbol(propertyName.c_str())));
            }

            NS_IMPLEMENT_INTERFACE_FIXUP

        private:
            VansUIViewModel* m_ViewModel = nullptr;
            Noesis::PropertyChangedEventHandler m_PropertyChanged;

            NS_IMPLEMENT_INLINE_REFLECTION(VansNoesisViewModelAdapter, Noesis::BaseComponent,
                "ForestEngine.VansNoesisViewModelAdapter")
            {
                NsImpl<Noesis::INotifyPropertyChanged>();
            }
        };

        class VansNoesisCommandAdapter final : public Noesis::BaseCommand
        {
        public:
            VansNoesisCommandAdapter(VansUIViewModel* viewModel, std::string commandName)
                : m_ViewModel(viewModel)
                , m_CommandName(std::move(commandName))
            {}

            bool CanExecute(Noesis::BaseComponent* /*param*/) const override
            {
                return m_ViewModel && m_ViewModel->CanExecuteCommand(m_CommandName);
            }

            void Execute(Noesis::BaseComponent* param) const override
            {
                if (!m_ViewModel)
                    return;

                const std::string parameter = CommandParameterToString(param);
                if (!parameter.empty() &&
                    m_ViewModel->ExecuteCommandWithParam(m_CommandName, parameter))
                {
                    return;
                }

                if (!m_ViewModel->ExecuteCommand(m_CommandName) && parameter.empty())
                    m_ViewModel->ExecuteCommandWithParam(m_CommandName, {});
            }

        private:
            VansUIViewModel* m_ViewModel = nullptr;
            std::string m_CommandName;

            NS_IMPLEMENT_INLINE_REFLECTION(VansNoesisCommandAdapter, Noesis::BaseCommand,
                "ForestEngine.VansNoesisCommandAdapter")
            {
            }
        };

        void BindCommandsRecursive(Noesis::FrameworkElement* element,
                                   VansUIViewModel& viewModel,
                                   std::vector<Noesis::Ptr<Noesis::BaseComponent>>& commandAdapters)
        {
            if (!element)
                return;

            if (auto* button = Noesis::DynamicCast<Noesis::BaseButton*>(element))
            {
                const char* rawName = element->GetName();
                const std::string elementName = rawName ? rawName : "";
                for (const std::string& candidate : BuildCommandCandidates(elementName))
                {
                    if (!viewModel.HasCommand(candidate))
                        continue;

                    Noesis::Ptr<VansNoesisCommandAdapter> command =
                        *new VansNoesisCommandAdapter(&viewModel, candidate);
                    button->SetCommand(command.GetPtr());
                    commandAdapters.push_back(command);
                    break;
                }
            }

            const uint32_t childCount = Noesis::LogicalTreeHelper::GetChildrenCount(element);
            for (uint32_t i = 0; i < childCount; ++i)
            {
                Noesis::Ptr<Noesis::BaseComponent> child =
                    Noesis::LogicalTreeHelper::GetChild(element, i);
                if (auto* childElement = Noesis::DynamicCast<Noesis::FrameworkElement*>(child.GetPtr()))
                    BindCommandsRecursive(childElement, viewModel, commandAdapters);
            }
        }
    }

    VansNoesisDocument::VansNoesisDocument(
        Noesis::Ptr<Noesis::IView> view,
        Noesis::Ptr<Noesis::FrameworkElement> content,
        std::string sourcePath,
        VansNoesisInputAdapter* inputAdapter)
        : m_View(std::move(view))
        , m_Content(std::move(content))
        , m_SourcePath(std::move(sourcePath))
        , m_InputAdapter(inputAdapter)
    {
        if (m_InputAdapter && m_View)
            m_InputAdapter->AddView(m_View.GetPtr());
    }

    VansNoesisDocument::~VansNoesisDocument()
    {
        if (m_ViewModel && m_ViewModelChangedToken != 0)
            m_ViewModel->RemovePropertyChangedHandler(m_ViewModelChangedToken);

        if (m_InputAdapter && m_View)
            m_InputAdapter->RemoveView(m_View.GetPtr());

        m_Content.Reset();
        m_View.Reset();
    }

    void VansNoesisDocument::Show()
    {
        m_Visible = true;
    }

    void VansNoesisDocument::Hide()
    {
        m_Visible = false;
    }

    void VansNoesisDocument::SetVisible(bool visible)
    {
        m_Visible = visible;
    }

    bool VansNoesisDocument::IsVisible() const
    {
        return m_Visible;
    }

    void VansNoesisDocument::SetSize(uint32_t width, uint32_t height)
    {
        if (m_View)
            m_View->SetSize(width, height);
    }

    void VansNoesisDocument::SetDataContext(VansUIViewModel* vm)
    {
        if (m_ViewModel && m_ViewModelChangedToken != 0)
            m_ViewModel->RemovePropertyChangedHandler(m_ViewModelChangedToken);

        m_ViewModel = vm;
        m_ViewModelChangedToken = 0;
        m_CommandAdapters.clear();
        m_ViewModelAdapter.Reset();

        if (!m_ViewModel)
        {
            if (m_Content)
                m_Content->SetDataContext(nullptr);
            return;
        }

        m_ViewModelAdapter = *new VansNoesisViewModelAdapter(m_ViewModel);
        if (m_Content)
            m_Content->SetDataContext(m_ViewModelAdapter.GetPtr());

        ApplyViewModel();
        BindViewModelCommands();
        m_ViewModelChangedToken = m_ViewModel->AddPropertyChangedHandler(
            [this](const std::string& propertyName)
            {
                ApplyViewModelProperty(propertyName);
                if (auto* adapter =
                    Noesis::DynamicCast<VansNoesisViewModelAdapter*>(m_ViewModelAdapter.GetPtr()))
                {
                    adapter->NotifyPropertyChanged(propertyName);
                }
                BindViewModelCommands();
                if (m_View)
                    m_View->Update(0.0);
            });
    }

    VansUIElementHandle VansNoesisDocument::FindElement(const std::string& name)
    {
        VansUIElementHandle handle;
        if (!m_Content)
            return handle;

        Noesis::Ptr<Noesis::FrameworkElement> found =
            Noesis::LogicalTreeHelper::FindLogicalNode(m_Content.GetPtr(), name.c_str());
        if (found)
            handle.m_NativeElement = found.GetPtr();

        return handle;
    }

    void VansNoesisDocument::ApplyViewModel()
    {
        if (!m_ViewModel)
            return;

        for (const auto& [name, value] : m_ViewModel->GetValues())
        {
            VansUIElementHandle element = FindElement(name);
            if (element.IsValid())
                element.SetText(VariantToDisplayString(value));
        }
    }

    void VansNoesisDocument::ApplyViewModelProperty(const std::string& propertyName)
    {
        if (!m_ViewModel || propertyName.empty())
            return;

        const VansUIVariant* value = m_ViewModel->GetValue(propertyName);
        if (!value)
            return;

        VansUIElementHandle element = FindElement(propertyName);
        if (element.IsValid())
            element.SetText(VariantToDisplayString(*value));
    }

    void VansNoesisDocument::BindViewModelCommands()
    {
        m_CommandAdapters.clear();
        if (!m_Content || !m_ViewModel)
            return;

        BindCommandsRecursive(m_Content.GetPtr(), *m_ViewModel, m_CommandAdapters);
    }

    const std::string& VansNoesisDocument::GetSourcePath() const
    {
        return m_SourcePath;
    }

    void VansNoesisDocument::Update(double totalTimeSeconds)
    {
        if (m_View && m_Visible)
            m_View->Update(totalTimeSeconds);
    }

    void VansNoesisDocument::RenderOffscreen()
    {
        if (!m_View || !m_Visible)
            return;

        m_View->GetRenderer()->UpdateRenderTree();
        m_View->GetRenderer()->RenderOffscreen();
    }

    void VansNoesisDocument::Render()
    {
        if (m_View && m_Visible)
            m_View->GetRenderer()->Render();
    }
}
