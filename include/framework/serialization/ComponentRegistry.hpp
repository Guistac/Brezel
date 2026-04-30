#pragma once

#include <entt/entt.hpp>
#include <pugixml.hpp>
#include "framework/serialization/XmlVisitors.hpp"



struct BaseComponent {
    CommandStack* undoStack = nullptr;
    virtual void reflect(ComponentVisitor& visitor) = 0;
};


namespace ComponentRegistry{

    struct ComponentTypeInfo {
        std::string name;
        // Function pointers for generic save/load
        std::function<void(entt::entity, entt::registry&, pugi::xml_node&)> save;
        std::function<void(entt::entity, entt::registry&, pugi::xml_node&)> load;
    };

    inline std::vector<ComponentTypeInfo> m_types;
    inline const std::vector<ComponentTypeInfo>& getTypes() { return m_types; }

    template<typename T>
    void registerComponent(const std::string& saveName){
        ComponentTypeInfo info;
        
        // This works now because T (e.g., MotorComponent) 
        // has the static xml_name from the macro.
        //info.name = T::xml_name; 
        info.name = saveName;

        info.save = [saveName](entt::entity e, entt::registry& reg, pugi::xml_node& node) {
            if (auto* comp = reg.try_get<T>(e)) {
                auto compNode = node.append_child(saveName);
                XmlSaveVisitor saver(compNode);
                comp->reflect(saver);
            }
        };

        info.load = [saveName](entt::entity e, entt::registry& reg, pugi::xml_node& node) {
            if (auto compNode = node.child(saveName)) {
                auto& comp = reg.emplace<T>(e);
                auto* stack = reg.ctx().get<CommandStack*>();
                
                if constexpr (std::is_base_of_v<BaseComponent, T>) {
                    comp.undoStack = stack;
                }
                XmlLoadVisitor loader(compNode, stack);
                comp.reflect(loader);
            }
        };

        m_types.push_back(info);
    };
}