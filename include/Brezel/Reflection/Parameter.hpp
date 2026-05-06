#pragma once
#include <string>
#include <type_traits>
#include "Brezel/Foundation/Signal.hpp"
#include "Brezel/Foundation/StringID.hpp"
#include "Brezel/Reflection/ParameterOptions.hpp"

namespace Brezel {

class CommandStack;

class ParameterBase {
public:
    virtual ~ParameterBase() = default;
    virtual StringID name() const = 0;

    virtual std::string toString() const = 0;
    virtual void fromString(const std::string& value) = 0;

    void setCommandStack(CommandStack* stack) { m_stack = stack; }
    bool hasCommandStack(){ return m_stack != nullptr; }

protected:
    CommandStack* m_stack = nullptr;
};

template<typename T>
class Parameter : public ParameterBase {
public:
    Parameter(const char* name, T defaultValue)
        : m_name(StringID::from(name)), m_value(defaultValue) {}

    Parameter(StringID name, T defaultValue)
        : m_name(name), m_value(defaultValue) {}

    const T& get() const { return m_value; }

    void setOptions(const ParameterOptions& options) { m_options = options; }
    const ParameterOptions& getOptions() const { return m_options; }

    void set(const T& newValue); 

    void setDirect(const T& newValue) {
        if (m_value != newValue) {
            m_value = newValue;
            onChange.emit(m_value);
        }
    }

    StringID name() const override { return m_name; }
    
    std::string toString() const override {
        if constexpr (std::is_same_v<T, float>) return std::to_string(m_value);
        if constexpr (std::is_same_v<T, double>) return std::to_string(m_value);
        if constexpr (std::is_same_v<T, int>) return std::to_string(m_value);
        if constexpr (std::is_same_v<T, bool>) return m_value ? "true" : "false";
        if constexpr (std::is_same_v<T, std::string>) return m_value;
        if constexpr (std::is_same_v<T, StringID>) return m_value.toString();
        if constexpr (std::is_enum_v<T>) return std::to_string(static_cast<std::underlying_type_t<T>>(m_value));
        return "";
    }

    void fromString(const std::string& value) override {
        if constexpr (std::is_same_v<T, float>) setDirect(std::stof(value));
        else if constexpr (std::is_same_v<T, double>) setDirect(std::stod(value));
        else if constexpr (std::is_same_v<T, int>) setDirect(std::stoi(value));
        else if constexpr (std::is_same_v<T, bool>) setDirect(value == "true");
        else if constexpr (std::is_same_v<T, std::string>) setDirect(value);
        else if constexpr (std::is_same_v<T, StringID>) setDirect(StringID::from(value));
        else if constexpr (std::is_enum_v<T>) setDirect(static_cast<T>(std::stoi(value)));
    }

    Signal<const T&> onChange;

private:
    StringID m_name;
    T m_value;
    ParameterOptions m_options;
};

} // namespace Brezel
