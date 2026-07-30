#pragma once

#include "../../Public/VansUIDocument.h"
#include "../../Public/VansUIElementHandle.h"

#include <NsCore/BaseComponent.h>
#include <NsCore/Ptr.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/IRenderer.h>
#include <NsGui/IView.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VansRuntime
{
    class VansNoesisInputAdapter;
    class VansUIViewModel;

    class VansNoesisDocument : public VansUIDocument
    {
    public:
        VansNoesisDocument(
            Noesis::Ptr<Noesis::IView> view,
            Noesis::Ptr<Noesis::FrameworkElement> content,
            std::string sourcePath,
            VansNoesisInputAdapter* inputAdapter);

        ~VansNoesisDocument() override;

        void Show() override;
        void Hide() override;
        void SetVisible(bool visible) override;
        bool IsVisible() const override;

        void SetSize(uint32_t width, uint32_t height) override;
        void SetDataContext(VansUIViewModel* vm) override;

        VansUIElementHandle FindElement(const std::string& name) override;
        const std::string& GetSourcePath() const override;

        void Update(double totalTimeSeconds) override;
        void RenderOffscreen() override;
        void Render() override;

        Noesis::IView* GetView() const { return m_View.GetPtr(); }
        Noesis::FrameworkElement* GetContent() const { return m_Content.GetPtr(); }

    private:
        void ApplyViewModel();
        void ApplyViewModelProperty(const std::string& propertyName);
        void BindViewModelCommands();

        Noesis::Ptr<Noesis::IView> m_View;
        Noesis::Ptr<Noesis::FrameworkElement> m_Content;
        Noesis::Ptr<Noesis::BaseComponent> m_ViewModelAdapter;
        std::vector<Noesis::Ptr<Noesis::BaseComponent>> m_CommandAdapters;

        std::string m_SourcePath;
        bool m_Visible = true;

        VansNoesisInputAdapter* m_InputAdapter = nullptr;
        VansUIViewModel* m_ViewModel = nullptr;
        std::uint64_t m_ViewModelChangedToken = 0;
    };
}
