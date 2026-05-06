#pragma once

#include "Brezel/Core/BaseComponent.hpp"
#include "Brezel/Core/Entity.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include <entt/entt.hpp>

namespace Brezel {

class CommandStack;

namespace ComponentRegistry {

struct ComponentTypeInfo {
  StringID saveString;
  std::function<void(Entity, ComponentVisitor &)> reflect;
  std::function<void(Entity)> createComponent;
  std::function<bool(Entity)> hasComponent;
};

inline std::unordered_map<entt::id_type, ComponentTypeInfo> componentInfoById;
inline std::unordered_map<StringID, ComponentTypeInfo> componentInfoByTypeName;

template <typename T> void registerComponent(const char *saveString) {
  StringID sid = StringID::from(saveString);
  ComponentTypeInfo info;
  info.saveString = sid;
  info.reflect = [](Entity entity, ComponentVisitor &visitor) {
    if (auto *component = entity.try_get<T>()) {
      component->reflect(visitor);
    }
  };
  info.createComponent = [](Entity entity) { entity.add<T>(); };
  info.hasComponent = [](Entity entity) { return entity.has<T>(); };
  entt::id_type componentId = entt::type_id<T>().hash();
  componentInfoById[componentId] = info;
  componentInfoByTypeName[sid] = info;
};

inline void reflectEntityComponents(Entity entity, ComponentVisitor &visitor) {
  auto &entt_registry = entity.registry();
  auto entt_entity = entity.handle().entity();
  for (auto [componentTypeId, storage] : entt_registry.storage()) {
    if (storage.contains(entt_entity) &&
        componentInfoById.contains(componentTypeId)) {
      const ComponentTypeInfo &info = componentInfoById.at(componentTypeId);
      if (visitor.beginComponent(info.saveString)) {
        info.reflect(entity, visitor);
        visitor.endComponent();
      }
    }
  }
}

inline bool addReflectEntityComponent(Entity entity, StringID componentTypeName,
                                      ComponentVisitor &visitor) {
  if (componentInfoByTypeName.contains(componentTypeName)) {
    auto &info = componentInfoByTypeName[componentTypeName];
    if (visitor.beginComponent(componentTypeName)) {
      info.createComponent(entity);
      info.reflect(entity, visitor);
      visitor.endComponent();
    }
    return true;
  }
  return false;
}

inline bool reflectEntityComponent(Entity entity, StringID componentTypeName,
                                   ComponentVisitor &visitor) {
  if (componentInfoByTypeName.contains(componentTypeName)) {
    auto &info = componentInfoByTypeName[componentTypeName];
    if (info.hasComponent(entity)) {
      if (visitor.beginComponent(componentTypeName)) {
        info.reflect(entity, visitor);
        visitor.endComponent();
      }
      return true;
    }
  }
  return false;
}

} // namespace ComponentRegistry

} // namespace Brezel