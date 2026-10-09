#pragma once

#include "Brezel/Core/Entity.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include <entt/entt.hpp>

namespace Brezel {

template<typename T, typename V>
void reflect(T&, V&) {}

class CommandStack;

namespace ComponentRegistry {
 
enum class ComponentFlag : uint32_t {
  None = 0,
  Transient = 1 << 0, ///< Inspectable in UI and reflected at runtime, but skipped by XML Serializer
};

inline bool isTransient(ComponentFlag flags) noexcept {
  return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ComponentFlag::Transient)) != 0;
}

struct ComponentOptions {
  const char *displayName{nullptr};
  const char *category{nullptr};
  ComponentFlag flags{ComponentFlag::None};
  int isTag{-1}; ///< -1 = auto-detect via std::is_empty_v<T>, 0 = false, 1 = true
};

struct ComponentTypeInfo {
  StringID saveString;
  std::string displayName;
  std::string category;
  bool isTag{false};
  std::function<void(Entity, ComponentVisitor &)> reflect;
  std::function<void(Entity)> createComponent;
  std::function<void(Entity)> removeComponent;
  std::function<void(Entity, Entity)> copyComponent;
  std::function<bool(Entity)> hasComponent;
  std::function<bool(Entity)> customDrawer{nullptr};
  ComponentFlag flags{ComponentFlag::None};
};

inline std::unordered_map<entt::id_type, ComponentTypeInfo> componentInfoById;
inline std::unordered_map<StringID, ComponentTypeInfo> componentInfoByTypeName;

template <typename T>
void registerComponent(const char *saveString, const ComponentOptions &options) {
  StringID sid = StringID::from(saveString);
  ComponentTypeInfo info;
  info.saveString = sid;
  info.flags = options.flags;

  if (options.isTag == -1) {
    info.isTag = std::is_empty_v<T> || (sizeof(T) <= 1);
  } else {
    info.isTag = (options.isTag == 1);
  }

  if (options.displayName && options.displayName[0] != '\0') {
    info.displayName = options.displayName;
  } else {
    info.displayName = saveString;
  }

  if (options.category && options.category[0] != '\0') {
    info.category = options.category;
  } else {
    info.category = info.isTag ? "Tags" : "General";
  }

  info.reflect = [](Entity entity, ComponentVisitor &visitor) {
    if (auto *component = entity.try_get<T>()) {
      reflect(*component, visitor);
    }
  };
  info.createComponent = [](Entity entity) { entity.add<T>(); };
  info.removeComponent = [](Entity entity) { entity.remove<T>(); };
  info.copyComponent = [](Entity src, Entity dst) {
    if constexpr (std::is_copy_constructible_v<T> || std::is_trivially_copyable_v<T>) {
      if (auto *component = src.try_get<T>()) {
        dst.add<T>(*component);
      }
    }
  };
  info.hasComponent = [](Entity entity) { return entity.has<T>(); };

  entt::id_type componentId = entt::type_id<T>().hash();
  componentInfoById[componentId] = info;
  componentInfoByTypeName[sid] = info;
}

template <typename T>
void registerComponent(const char *saveString, ComponentFlag flags = ComponentFlag::None) {
  ComponentOptions opts;
  opts.flags = flags;
  registerComponent<T>(saveString, opts);
}

template <typename T> void setCustomDrawer(std::function<bool(Entity)> drawer) {
  entt::id_type componentId = entt::type_id<T>().hash();
  if (componentInfoById.contains(componentId)) {
    componentInfoById[componentId].customDrawer = drawer;
    StringID sid = componentInfoById[componentId].saveString;
    if (componentInfoByTypeName.contains(sid)) {
      componentInfoByTypeName[sid].customDrawer = drawer;
    }
  }
}

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