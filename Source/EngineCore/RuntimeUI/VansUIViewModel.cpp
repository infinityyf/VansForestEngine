#include "Public/VansUIViewModel.h"

#include <utility>

namespace VansRuntime
{
    void VansUIViewModel::SetBool(const std::string& name, bool value)
    {
        SetValue(name, VansUIVariant(value));
    }

    void VansUIViewModel::SetInt(const std::string& name, std::int64_t value)
    {
        SetValue(name, VansUIVariant(value));
    }

    void VansUIViewModel::SetFloat(const std::string& name, double value)
    {
        SetValue(name, VansUIVariant(value));
    }

    void VansUIViewModel::SetString(const std::string& name, std::string value)
    {
        SetValue(name, VansUIVariant(std::move(value)));
    }

    void VansUIViewModel::SetObject(const std::string& name, VansUIVariantMap value)
    {
        SetValue(name, VansUIVariant(std::move(value)));
    }

    void VansUIViewModel::SetArray(const std::string& name, VansUIVariantArray value)
    {
        SetValue(name, VansUIVariant(std::move(value)));
    }

    void VansUIViewModel::SetValue(const std::string& name, VansUIVariant value)
    {
        if (name.empty())
            return;

        m_Values[name] = std::move(value);
        NotifyPropertyChanged(name);
    }

	void VansUIViewModel::RemoveValue(const std::string& name)
	{
		if (m_Values.erase(name) != 0)
			NotifyPropertyChanged(name);
	}

    bool VansUIViewModel::HasValue(const std::string& name) const
    {
        return m_Values.find(name) != m_Values.end();
    }

    const VansUIVariant* VansUIViewModel::GetValue(const std::string& name) const
    {
        const auto it = m_Values.find(name);
        return it != m_Values.end() ? &it->second : nullptr;
    }

    std::uint64_t VansUIViewModel::AddPropertyChangedHandler(PropertyChangedHandler handler)
    {
        if (!handler)
            return 0;

        const std::uint64_t token = m_NextPropertyChangedToken++;
        m_PropertyChangedHandlers.emplace(token, std::move(handler));
        return token;
    }

    void VansUIViewModel::RemovePropertyChangedHandler(std::uint64_t token)
    {
        if (token == 0)
            return;
        m_PropertyChangedHandlers.erase(token);
    }

    void VansUIViewModel::NotifyPropertyChanged(const std::string& propertyName)
    {
        for (const auto& [token, handler] : m_PropertyChangedHandlers)
        {
            (void)token;
            if (handler)
                handler(propertyName);
        }
    }

    void VansUIViewModel::BindCommand(const std::string& commandName,
                                      CommandHandler handler)
    {
        if (commandName.empty())
            return;

        if (handler)
            m_Commands[commandName] = std::move(handler);
        else
            m_Commands.erase(commandName);

        NotifyPropertyChanged(commandName);
    }

    void VansUIViewModel::BindCommandWithParam(const std::string& commandName,
                                               CommandWithParamHandler handler)
    {
        if (commandName.empty())
            return;

        if (handler)
            m_ParameterizedCommands[commandName] = std::move(handler);
        else
            m_ParameterizedCommands.erase(commandName);

        NotifyPropertyChanged(commandName);
    }

    bool VansUIViewModel::HasCommand(const std::string& commandName) const
    {
        return m_Commands.find(commandName) != m_Commands.end() ||
            m_ParameterizedCommands.find(commandName) != m_ParameterizedCommands.end();
    }

    void VansUIViewModel::SetCommandCanExecute(const std::string& commandName, bool canExecute)
    {
        if (commandName.empty())
            return;

        m_CommandCanExecute[commandName] = canExecute;
        NotifyPropertyChanged(commandName);
    }

    bool VansUIViewModel::CanExecuteCommand(const std::string& commandName) const
    {
        const auto it = m_CommandCanExecute.find(commandName);
        return it == m_CommandCanExecute.end() || it->second;
    }

    bool VansUIViewModel::ExecuteCommand(const std::string& commandName) const
    {
        if (!CanExecuteCommand(commandName))
            return false;

        const auto it = m_Commands.find(commandName);
        if (it == m_Commands.end() || !it->second)
            return false;

        it->second();
        return true;
    }

    bool VansUIViewModel::ExecuteCommandWithParam(const std::string& commandName,
                                                  const std::string& parameter) const
    {
        if (!CanExecuteCommand(commandName))
            return false;

        const auto it = m_ParameterizedCommands.find(commandName);
        if (it == m_ParameterizedCommands.end() || !it->second)
            return false;

        it->second(parameter);
        return true;
    }
}
