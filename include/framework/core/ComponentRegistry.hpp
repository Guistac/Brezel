#pragma once

#include <entt/entt.hpp>

#include "framework/core/Visitor.hpp"

class CommandStack;

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
    inline std::unordered_map<std::string, ComponentTypeInfo> componentInfoByTypeName;

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
        componentInfoByTypeName[saveString] = info;
    };


    inline void reflectEntityComponents(entt::handle handle, ComponentVisitor& visitor) {
        auto& reg = *handle.registry();
        auto entity = handle.entity();
        for(auto [componentTypeId, storage] : reg.storage()) {
            if(storage.contains(entity) && componentInfoById.contains(componentTypeId)) {
                const ComponentTypeInfo& info = componentInfoById.at(componentTypeId);
                if(visitor.beginComponent(info.saveString.c_str())){
                    info.reflect(handle, visitor);
                    visitor.endComponent();
                }
            }
        }
    }

    inline bool addReflectEntityComponent(entt::handle handle, const char* componentTypeName, ComponentVisitor& visitor){
        if(componentInfoByTypeName.contains(componentTypeName)){
            auto& info = componentInfoByTypeName[componentTypeName];
            if(visitor.beginComponent(componentTypeName)){
                info.createComponent(handle);
                info.reflect(handle, visitor);
                visitor.endComponent();
            }
            return true;
        }
        return false;
    }

}