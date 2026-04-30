#pragma once
#include <entt/entt.hpp>
#include <pugixml.hpp>
#include <functional>
#include <string>
#include <vector>
#include "framework/params/Parameter.hpp"

struct BaseComponent {
    CommandStack* undoStack{nullptr};
    BaseComponent() = default;
    virtual ~BaseComponent() = default;
    virtual const char* get_xml_name() const = 0; // Forced override
    virtual void wireParameters(CommandStack* stack) = 0; // Automatic wiring interface
};

#define COMPONENT_IDENTITY(Name) \
    using BaseComponent::BaseComponent; \
    static constexpr const char* xml_name = Name; \
    const char* get_xml_name() const override { return xml_name; }

#define REFLECT_PARAMS(...) \
    void wireParameters(CommandStack* stack) override { \
        this->reflect([stack](const char*, ParameterBase& p) { \
            p.setCommandStack(stack); \
        }); \
    } \
    template<typename Visitor> \
    void reflect(Visitor&& v) { /* Changed to forwarding reference */ \
        auto apply = [&](auto&... params) { (v(params.name().data(), params), ...); }; \
        apply(__VA_ARGS__); \
    }

struct ComponentTypeInfo {
    std::string name;
    // Function pointers for generic save/load
    std::function<void(entt::entity, entt::registry&, pugi::xml_node&)> save;
    std::function<void(entt::entity, entt::registry&, pugi::xml_node&)> load;
};

class ComponentRegistry {
public:
    static ComponentRegistry& instance() {
        static ComponentRegistry inst;
        return inst;
    }

    template<typename T>
    void registerComponent() {
        ComponentTypeInfo info;
        
        // This works now because T (e.g., MotorComponent) 
        // has the static xml_name from the macro.
        info.name = T::xml_name; 

        info.save = [](entt::entity e, entt::registry& reg, pugi::xml_node& node) {
            if (auto* comp = reg.try_get<T>(e)) {
                auto compNode = node.append_child(T::xml_name);
                auto visitor = [&](const char* label, const ParameterBase& p) {
                    compNode.append_attribute(label) = p.toString().c_str();
                };
                comp->reflect(visitor);
            }
        };

        info.load = [](entt::entity e, entt::registry& reg, pugi::xml_node& node) {
            if (auto compNode = node.child(T::xml_name)) {
                auto& comp = reg.emplace<T>(e);
                auto* stack = reg.ctx().get<CommandStack*>();
                
                if constexpr (std::is_base_of_v<BaseComponent, T>) {
                    comp.undoStack = stack;
                }

                auto visitor = [&](const char* label, ParameterBase& p) {
                    p.setCommandStack(stack);
                    if (auto attr = compNode.attribute(label)) {
                        p.fromString(attr.value());
                    }
                };
                comp.reflect(visitor);
            }
        };

        m_types.push_back(info);
    }

    const std::vector<ComponentTypeInfo>& getTypes() const { return m_types; }

private:
    std::vector<ComponentTypeInfo> m_types;
};