#pragma once

#include <entt/entt.hpp>
#include "Brezel/Reflection/Visitor.hpp"

namespace Brezel {

class CommandStack;

struct BaseComponent {
    CommandStack* undoStack = nullptr;
    virtual void reflect(ComponentVisitor& visitor) = 0;
};

namespace ComponentRegistry {

    struct ComponentTypeInfo {
        StringID saveString;
        std::function<void(entt::handle, ComponentVisitor&)> reflect;
        std::function<void(entt::handle)> createComponent;
        std::function<bool(entt::handle)> hasComponent;
    };

    inline std::unordered_map<entt::id_type, ComponentTypeInfo> componentInfoById;
    inline std::unordered_map<StringID, ComponentTypeInfo> componentInfoByTypeName;

    template<typename T>
    void registerComponent(const char* saveString){
        StringID sid = StringID::from(saveString);
        ComponentTypeInfo info;
        info.saveString = sid;
        info.reflect = [](entt::handle handle, ComponentVisitor& visitor){
            if(auto* component = handle.try_get<T>()){
                component->reflect(visitor);
            }
        };
        info.createComponent = [](entt::handle handle){
            auto& component = handle.get_or_emplace<T>();
        };
        info.hasComponent = [](entt::handle handle){
            return handle.try_get<T>() != nullptr;
        };
        entt::id_type componentId = entt::type_id<T>().hash();
        componentInfoById[componentId] = info;
        componentInfoByTypeName[sid] = info;
    };

    inline void reflectEntityComponents(entt::handle handle, ComponentVisitor& visitor) {
        auto& reg = *handle.registry();
        auto entity = handle.entity();
        for(auto [componentTypeId, storage] : reg.storage()) {
            if(storage.contains(entity) && componentInfoById.contains(componentTypeId)) {
                const ComponentTypeInfo& info = componentInfoById.at(componentTypeId);
                if(visitor.beginComponent(info.saveString)){
                    info.reflect(handle, visitor);
                    visitor.endComponent();
                }
            }
        }
    }

    inline bool addReflectEntityComponent(entt::handle handle, StringID componentTypeName, ComponentVisitor& visitor){
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

    inline bool reflectEntityComponent(entt::handle handle, StringID componentTypeName, ComponentVisitor& visitor){
        if(componentInfoByTypeName.contains(componentTypeName)){
            auto& info = componentInfoByTypeName[componentTypeName];
            if(info.hasComponent(handle)){
                if(visitor.beginComponent(componentTypeName)){
                    info.reflect(handle, visitor);
                    visitor.endComponent();
                }
                return true;
            }
        }
        return false;
    }

} // namespace ComponentRegistry

} // namespace Brezel