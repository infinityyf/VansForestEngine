#pragma once

#include "VansUIRuntimeHandles.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace VansRuntime
{
    class VansUIComponentInstance;

    class VansUIComponentRegistry
    {
    public:
        static VansUIComponentRegistry& Get();

        std::shared_ptr<VansUIComponentInstance> LoadComponent(const std::string& configPath);
        std::shared_ptr<VansUIComponentInstance> GetComponent(VansUIHandleId handle) const;
        void CloseComponent(VansUIHandleId handle);
        void CloseAll();

    private:
        VansUIComponentRegistry() = default;

        VansUIHandleId m_NextHandle = 1;
        std::unordered_map<VansUIHandleId, std::shared_ptr<VansUIComponentInstance>> m_Components;
    };
}
