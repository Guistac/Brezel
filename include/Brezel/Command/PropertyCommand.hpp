#pragma once
#include "Brezel/Command/Command.hpp"
#include "Brezel/Command/CommandStack.hpp"
#include "Brezel/Reflection/Parameter.hpp"

namespace Brezel {

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
        return "Change " + m_param.name().toString();
    }

private:
    Parameter<T>& m_param;
    T m_oldValue;
    T m_newValue;
};

// Implementation of Parameter::set (declared in Parameter.hpp)
template<typename T>
void Parameter<T>::set(const T& newValue) {
    if (m_value == newValue) return;

    if (m_stack) {
        auto cmd = std::make_unique<PropertyCommand<T>>(*this, newValue);
        m_stack->pushAndExecute(std::move(cmd));
    } else {
        setDirect(newValue);
    }
}

} // namespace Brezel