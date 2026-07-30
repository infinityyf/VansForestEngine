#pragma once

#include "VansUIComponentConfig.h"
#include "VansUIDocument.h"
#include "VansUIElementHandle.h"
#include "VansUIRuntimeHandles.h"

#include <memory>
#include <string>

namespace VansRuntime
{
    class VansUIComponentInstance
    {
    public:
        VansUIComponentInstance(
            VansUIHandleId handle,
            VansUIComponentConfig config,
            std::shared_ptr<VansUIDocument> document);

        VansUIHandleId GetHandleId() const { return m_Handle; }
        const std::string& GetTypeGuid() const { return m_Config.guid; }
        const std::string& GetName() const { return m_Config.name; }
        const VansUIComponentConfig& GetConfig() const { return m_Config; }
        std::shared_ptr<VansUIDocument> GetDocument() const { return m_Document; }

        bool IsValid() const { return m_Document != nullptr; }
        void Close();

        VansUIElementHandle FindElement(const std::string& name);
        void SetProperty(const std::string& name, const VansUIVariant& value);
        VansUIVariant GetProperty(const std::string& name) const;
        void SetState(const std::string& state);
        const std::string& GetState() const { return m_State; }
        void PlayAnimation(const std::string& name);
        const std::string& GetLastAnimation() const { return m_LastAnimation; }

    private:
        const VansUIComponentPropertyConfig* FindPropertyConfig(const std::string& name) const;
        void BindConfiguredEvents();

        VansUIHandleId m_Handle = kInvalidUIHandle;
        VansUIComponentConfig m_Config;
        std::shared_ptr<VansUIDocument> m_Document;
        VansUIVariantMap m_Properties;
        std::string m_State;
        std::string m_LastAnimation;
    };
}
