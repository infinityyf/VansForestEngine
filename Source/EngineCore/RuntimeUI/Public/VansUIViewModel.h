#pragma once

#include "VansUIVariant.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace VansRuntime
{
    class VansUIViewModel
    {
    public:
        using CommandHandler = std::function<void()>;
        using CommandWithParamHandler = std::function<void(const std::string&)>;
        using PropertyChangedHandler = std::function<void(const std::string&)>;

        virtual ~VansUIViewModel() = default;

        void SetBool(const std::string& name, bool value);
        void SetInt(const std::string& name, std::int64_t value);
        void SetFloat(const std::string& name, double value);
        void SetString(const std::string& name, std::string value);
        void SetObject(const std::string& name, VansUIVariantMap value);
        void SetArray(const std::string& name, VansUIVariantArray value);
        void SetValue(const std::string& name, VansUIVariant value);
		void RemoveValue(const std::string& name);

        bool HasValue(const std::string& name) const;
        const VansUIVariant* GetValue(const std::string& name) const;
        const VansUIVariantMap& GetValues() const { return m_Values; }

        std::uint64_t AddPropertyChangedHandler(PropertyChangedHandler handler);
        void RemovePropertyChangedHandler(std::uint64_t token);
        void NotifyPropertyChanged(const std::string& propertyName);

        void BindCommand(const std::string& commandName, CommandHandler handler);
        void BindCommandWithParam(const std::string& commandName, CommandWithParamHandler handler);
        bool HasCommand(const std::string& commandName) const;
        void SetCommandCanExecute(const std::string& commandName, bool canExecute);
        bool CanExecuteCommand(const std::string& commandName) const;
        bool ExecuteCommand(const std::string& commandName) const;
        bool ExecuteCommandWithParam(const std::string& commandName,
                                     const std::string& parameter) const;

    private:
        VansUIVariantMap m_Values;
        std::unordered_map<std::string, CommandHandler> m_Commands;
        std::unordered_map<std::string, CommandWithParamHandler> m_ParameterizedCommands;
        std::unordered_map<std::string, bool> m_CommandCanExecute;
        std::unordered_map<std::uint64_t, PropertyChangedHandler> m_PropertyChangedHandlers;
        std::uint64_t m_NextPropertyChangedToken = 1;
    };
}
