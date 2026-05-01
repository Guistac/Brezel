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
        std::string saveString;
        std::function<void(entt::handle, ComponentVisitor&)> reflect;
        std::function<void(entt::handle)> createComponent;
    };

    inline std::unordered_map<entt::id_type, ComponentTypeInfo> componentInfoById;

    template<typename T>
    void registerComponent(const std::string& saveString){
        ComponentTypeInfo info;
        info.saveString = saveString;
        info.reflect = [](entt::handle handle, ComponentVisitor& visitor){
            if(auto* component = handle.try_get<T>()){
                component->reflect(visitor);
            }
        };
        info.createComponent = [](entt::handle handle){
            auto& component = handle.get_or_emplace<T>();
        };
        entt::id_type componentId = entt::type_id<T>().hash();
        componentInfoById[componentId] = info;
    };


    inline void reflectEntityComponents(entt::handle handle, ComponentVisitor& visitor) {
        auto& reg = *handle.registry();
        auto entity = handle.entity();
        for(auto [componentId, storage] : reg.storage()) {
            if(storage.contains(entity) && componentInfoById.contains(componentId)) {
                const ComponentTypeInfo& info = componentInfoById.at(componentId);
                visitor.beginComponent(info.saveString.c_str());
                info.reflect(handle, visitor);
                visitor.endComponent();
            }
        }
    }

    inline const ComponentTypeInfo* findInfoBySaveName(std::string_view name) {
        for (auto& [id, info] : componentInfoById) {
            if (info.saveString == name) {
                return &info;
            }
        }
        return nullptr;
    }

}