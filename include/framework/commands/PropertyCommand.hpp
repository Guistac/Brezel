#pragma once
#include "framework/commands/Command.hpp"
#include "framework/commands/CommandStack.hpp"
#include "framework/params/Parameter.hpp"

template<typename T>
class PropertyCommand : public Command {
public:
    PropertyCommand(Parameter<T>& param, T newValue) 
        : m_param(param), m_oldValue(param.get()), m_newValue(newValue) {}

    void execute() override { 
        m_param.setDirect(m_newValue); 
    }

    void undo() override { 
        m_param.setDirect(m_oldValue); 
    }

    std::string getDescription() const override {
        return "Change " + std::string(m_param.name());
    }

private:
    Parameter<T>& m_param;
    T m_oldValue;
    T m_newValue;
};

// Now we can implement Parameter::set because PropertyCommand is defined
template<typename T>
void Parameter<T>::set(const T& newValue) {
    if (m_value == newValue) return;

    if (m_stack) {
        // Create a command and push it to the stack
        auto cmd = std::make_unique<PropertyCommand<T>>(*this, newValue);
        m_stack->pushAndExecute(std::move(cmd));
    } else {
        // Fallback if no undo system is present
        setDirect(newValue);
    }
}