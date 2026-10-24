#pragma once

#include "Brezel/Core/Entity.hpp"
#include "Brezel/Core/CoreComponents.hpp"
#include "Brezel/Core/UUIDProvider.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Core/Dispatcher.hpp"
#include "Brezel/Core/Project.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <optional>
#include <vector>

namespace Brezel {

template<typename T, typename V>
void reflect(T&, V&) {}

class CommandStack;

namespace Events {

/**
 * @brief Event emitted when an entity's component is modified via SetComponent or UpdateComponent.
 */
struct ComponentChanged {
    UUID uuid{0};
    Entity entity{};
    StringID componentName;
};

/**
 * @brief Event emitted when an entity is spawned into the scene.
 */
struct EntitySpawned {
    UUID uuid{0};
    Entity entity{};
};

/**
 * @brief Event emitted when an entity is permanently removed from the scene.
 */
struct EntityDestroyed {
    UUID uuid{0};
};

} // namespace Events

namespace Mutations {

/**
 * @brief Standard discrete mutation to update/replace an entity's component state directly via typed C++ object.
 */
template <typename T>
struct SetComponent {
    UUID uuid{0};
    Entity entity{};
    T component{};

    SetComponent() = default;
    SetComponent(UUID id, Entity ent, T comp)
        : uuid(id), entity(ent), component(std::move(comp)) {}
    SetComponent(UUID id, T comp)
        : uuid(id), component(std::move(comp)) {}
};

namespace Internal {

template <typename T>
inline void handleSetComponent(Project& project, const SetComponent<T>& m);

} // namespace Internal
} // namespace Mutations

namespace ComponentRegistry {
 
enum class ComponentFlag : uint32_t {
  None      = 0,
  Transient = 1 << 0, ///< Inspectable in UI and reflected at runtime, but skipped by XML Serializer
  Networked = 1 << 1, ///< Replicated across network (participates in distributed ECS sync)
};

inline constexpr ComponentFlag operator|(ComponentFlag a, ComponentFlag b) noexcept {
  return static_cast<ComponentFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr ComponentFlag operator&(ComponentFlag a, ComponentFlag b) noexcept {
  return static_cast<ComponentFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr ComponentFlag operator~(ComponentFlag a) noexcept {
  return static_cast<ComponentFlag>(~static_cast<uint32_t>(a));
}

inline constexpr ComponentFlag& operator|=(ComponentFlag& a, ComponentFlag b) noexcept {
  a = a | b;
  return a;
}

inline constexpr ComponentFlag& operator&=(ComponentFlag& a, ComponentFlag b) noexcept {
  a = a & b;
  return a;
}

inline bool isTransient(ComponentFlag flags) noexcept {
  return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ComponentFlag::Transient)) != 0;
}

inline bool isNetworked(ComponentFlag flags) noexcept {
  return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ComponentFlag::Networked)) != 0;
}

enum class ReplicationAuthority : uint8_t {
  LocalOnly           = 0, ///< Never replicated across network
  ServerAuthoritative = 1, ///< Show console owns state; replicated Server -> Client
  ClientAuthoritative = 2  ///< AGV hardware owns state; replicated Client -> Server
};

struct ComponentOptions {
  const char *displayName{nullptr};
  const char *category{nullptr};
  ComponentFlag flags{ComponentFlag::None};
  ReplicationAuthority authority{ReplicationAuthority::LocalOnly};
  int isTag{-1}; ///< -1 = auto-detect via std::is_empty_v<T>, 0 = false, 1 = true
  std::function<void(Entity)> onUpdated{nullptr};
  std::function<void(Entity)> onCreated{nullptr};
  std::function<void(Entity)> onRemoved{nullptr};
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
  std::function<void(Entity)> onUpdated{nullptr};
  std::function<void(Entity)> onCreated{nullptr};
  std::function<void(Entity)> onRemoved{nullptr};
  std::function<void(Project&, Dispatcher&)> registerSetHandler{nullptr};
  ComponentFlag flags{ComponentFlag::None};
  ReplicationAuthority authority{ReplicationAuthority::LocalOnly};
};

inline std::unordered_map<::entt::id_type, ComponentTypeInfo> componentInfoById;
inline std::unordered_map<StringID, ComponentTypeInfo> componentInfoByTypeName;
inline Project* s_activeProject{nullptr};
inline Dispatcher* s_activeDispatcher{nullptr};

/**
 * @brief A per-entity replication authority override.
 *
 * Returns the authority of the component `info` on `entity`, or std::nullopt to leave the decision to the next rule
 * (and finally to the type's `info.authority`). A rule reads only the entity's own components, never another entity
 * or any global state, and changes nothing.
 * Thread: any thread that has world access (holds a world lock, or is the world's single writer) when it calls
 * authorityFor().
 */
using AuthorityRule = std::function<std::optional<ReplicationAuthority>(const Entity&, const ComponentTypeInfo&)>;

inline std::vector<AuthorityRule> authorityRules;

/**
 * @brief Registers a per-entity authority rule; rules are evaluated in registration order, the first that answers wins.
 * Thread: startup only (the thread that registers components), before any replication starts; the rule list is
 * read-only afterwards, so authorityFor() takes no lock.
 */
inline void addAuthorityRule(AuthorityRule rule) {
  if (rule) authorityRules.push_back(std::move(rule));
}

/**
 * @brief The replication authority of component `info` on `entity`: the first registered rule that answers, else the
 * type's `info.authority` (with no rule registered, always `info.authority`).
 * Thread: any thread with world access to `entity` (holds a world lock, or is the world's single writer); the rules
 * read the entity's components.
 */
inline ReplicationAuthority authorityFor(const Entity& entity, const ComponentTypeInfo& info) {
  for (const auto& rule : authorityRules) {
    if (auto authority = rule(entity, info)) return *authority;
  }
  return info.authority;
}

} // namespace ComponentRegistry

namespace Mutations::Internal {

template <typename T>
inline void handleSetComponent(Project& project, const SetComponent<T>& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (!ent.isValid()) return;

    ent.template add_or_replace<T>(m.component);

    auto it = ComponentRegistry::componentInfoById.find(::entt::type_id<T>().hash());
    if (it != ComponentRegistry::componentInfoById.end()) {
        if (it->second.onUpdated) {
            it->second.onUpdated(ent);
        }
        if (auto* disp = project.getDispatcher()) {
            disp->trigger(Events::ComponentChanged{ m.uuid.isValid() ? m.uuid : ent.getUUID(), ent, it->second.saveString });
        }
    }
}

} // namespace Mutations::Internal

namespace ComponentRegistry {

template <typename T>
void registerComponent(const char *saveString, const ComponentOptions &options) {
  StringID sid = StringID::from(saveString);
  ::entt::id_type componentId = ::entt::type_id<T>().hash();

  ComponentTypeInfo info;
  info.saveString = sid;
  info.flags = options.flags;
  info.authority = options.authority;
  info.onUpdated = options.onUpdated;
  info.onCreated = options.onCreated;
  info.onRemoved = options.onRemoved;

  if (options.isTag == -1) {
    info.isTag = (options.category && std::string_view(options.category) == "Tags") || std::is_empty_v<T>;
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

  // Preserve existing custom drawer if component was previously registered or drawer assigned
  if (componentInfoById.contains(componentId) && componentInfoById[componentId].customDrawer) {
    info.customDrawer = componentInfoById[componentId].customDrawer;
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

  info.registerSetHandler = [](Project& project, Dispatcher& dispatcher) {
    dispatcher.sink<Mutations::SetComponent<T>>().template connect<&Mutations::Internal::handleSetComponent<T>>(project);
  };

  if (s_activeProject && s_activeDispatcher) {
    info.registerSetHandler(*s_activeProject, *s_activeDispatcher);
  }

  componentInfoById[componentId] = info;
  componentInfoByTypeName[sid] = info;
}

template <typename T>
void registerComponent(const char *saveString, ComponentFlag flags = ComponentFlag::None) {
  ComponentOptions opts;
  opts.flags = flags;
  registerComponent<T>(saveString, opts);
}

template <typename T>
void registerComponent(const char *saveString, ComponentFlag flags, ReplicationAuthority authority) {
  ComponentOptions opts;
  opts.flags = flags;
  opts.authority = authority;
  registerComponent<T>(saveString, opts);
}

template <typename T> void setCustomDrawer(std::function<bool(Entity)> drawer) {
  ::entt::id_type componentId = ::entt::type_id<T>().hash();
  componentInfoById[componentId].customDrawer = drawer;
  if (componentInfoById.contains(componentId)) {
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
      if (info.saveString == StringID::from("IdentityComponent")) continue;
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
      if (!info.hasComponent || !info.hasComponent(entity)) {
        info.createComponent(entity);
      }
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

inline Entity cloneEntity(Entity source, std::string_view newName = "") {
  if (!source.isValid()) return Entity{};

  std::string originalName = std::string(source.getName());
  if (originalName.empty()) originalName = "Entity";
  std::string name = newName.empty() ? (originalName + " (Copy)") : std::string(newName);

  UUID newUuid = UUIDProvider::generate();
  Entity newEntity(source.registry().create(), source.registry());
  newEntity.add<IdentityComponent>(name, name, newUuid);
  newEntity.add<HierarchyComponent>();

  auto& registry = source.registry();
  auto rawEntt = source.handle().entity();

  for (auto [componentTypeId, storage] : registry.storage()) {
    if (storage.contains(rawEntt) && componentInfoById.contains(componentTypeId)) {
      const auto& info = componentInfoById.at(componentTypeId);
      if (info.saveString == StringID::from("IdentityComponent")) continue;
      if (info.saveString == StringID::from("HierarchyComponent")) continue;
      if (isTransient(info.flags)) continue;

      if (info.copyComponent) {
        info.copyComponent(source, newEntity);
      }
    }
  }
  return newEntity;
}

} // namespace ComponentRegistry

inline void registerBrezelCoreComponents() {
  static bool s_registered = false;
  if (s_registered) return;
  s_registered = true;

  ComponentRegistry::registerComponent<IdentityComponent>("IdentityComponent", {
    .displayName = "Identity",
    .category = "Core",
    .flags = ComponentRegistry::ComponentFlag::Networked,
    .authority = ComponentRegistry::ReplicationAuthority::ServerAuthoritative
  });
}

inline Entity Entity::clone(std::string_view newName) const {
  return ComponentRegistry::cloneEntity(*this, newName);
}

} // namespace Brezel