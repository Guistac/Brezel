#pragma once
#include <string>
#include "framework/foundation/Signal.hpp"

class CommandStack;

class ParameterBase {
public:
    virtual ~ParameterBase() = default;
    virtual std::string_view name() const = 0;

    // These allow the serializer to get/set values as strings
    virtual std::string toString() const = 0;
    virtual void fromString(const std::string& value) = 0;

    void setCommandStack(CommandStack* stack) { m_stack = stack; }
    bool hasCommandStack(){ return m_stack != nullptr; }

protected:
    CommandStack* m_stack = nullptr; // Reference to the global undo service
};

template<typename T>
class Parameter : public ParameterBase {
public:
    Parameter(std::string_view name, T defaultValue)
        : m_name(name), m_value(defaultValue) {}

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
    
    std::string toString() const override {
        if constexpr (std::is_same_v<T, float>) return std::to_string(m_value);
        if constexpr (std::is_same_v<T, std::string>) return m_value;
        // Add more types as needed
    }

    void fromString(const std::string& value) override {
        if constexpr (std::is_same_v<T, float>) setDirect(std::stof(value));
        if constexpr (std::is_same_v<T, std::string>) setDirect(value);
    }

    Signal<const T&> onChange;

private:
    std::string m_name;
    T m_value;
};
