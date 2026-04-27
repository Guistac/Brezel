#pragma once
#include <string>
#include "framework/foundation/Signal.hpp"

class CommandStack;

class ParameterBase {
public:
    virtual ~ParameterBase() = default;
    virtual std::string_view name() const = 0;
};

template<typename T>
class Parameter : public ParameterBase {
public:
    Parameter(std::string_view name, T defaultValue, CommandStack* stack = nullptr) 
        : m_name(name), m_value(defaultValue), m_stack(stack) {}

    const T& get() const { return m_value; }

    // Use this for UI/Scripts: It pushes a command to the undo stack
    void set(const T& newValue); 

    // Use this for the Command/System: It just changes the value
    void setDirect(const T& newValue) {
        if (m_value != newValue) {
            m_value = newValue;
            onChange.emit(m_value);
        }
    }

    std::string_view name() const override { return m_name; }
    
    void setCommandStack(CommandStack* stack) { m_stack = stack; }

    Signal<const T&> onChange;

private:
    std::string m_name;
    T m_value;
    CommandStack* m_stack; // Reference to the global undo service
};