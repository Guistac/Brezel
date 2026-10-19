#pragma once
#include <entt/entt.hpp>
#include <string_view>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <source_location>
#include <utility>

#include "Brezel/Core/EcsWall.hpp"
#include "Brezel/Core/UUID.hpp"
#include "Brezel/Reflection/Visitor.hpp"

namespace Brezel {

class CommandStack;

/// @brief A high-level handle to an entity within the Brezel ECS.
/// @details This class wraps an `entt::handle` and provides a safe, object-oriented 
/// API for component manipulation and hierarchy navigation.
/// 
/// @note Entity objects are lightweight and can be passed by value. They do not 
/// own the entity data; destroying an Entity object does not destroy the entity 
/// in the registry.
class Entity {
public:
    Entity(entt::entity entity, entt::registry& registry) : m_handle(registry, entity){}
    Entity(entt::handle handle) : m_handle(handle){}
    Entity(entt::registry& registry) : m_handle(registry, entt::null){}
    Entity(){}

    // ECS wall (Brezel/Core/EcsWall.hpp): every accessor that touches the registry takes the caller's
    // std::source_location and checks "holds a read()/write() scope of the owning Project, or is its writer thread".
    // Entities of a plain registry (no Project, e.g. a GUI scratch copy) are not checked. handle() and registry() are
    // unchecked escape hatches.
    using Location = std::source_location;

    /// @brief Adds (or replaces) a component of type T on the entity.
    /// @tparam T The component type. Must be registered with ComponentRegistry.
    /// @param args Arguments forwarded to the component constructor (up to four; the caller's location follows).
    /// @return Reference to the newly created component.
    template<typename T>
    decltype(auto) add(Location loc = Location::current()) { return emplaceChecked<T>(loc); }
    template<typename T, typename A1>
    decltype(auto) add(A1&& a1, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1));
    }
    template<typename T, typename A1, typename A2>
    decltype(auto) add(A1&& a1, A2&& a2, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1), std::forward<A2>(a2));
    }
    template<typename T, typename A1, typename A2, typename A3>
    decltype(auto) add(A1&& a1, A2&& a2, A3&& a3, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3));
    }
    template<typename T, typename A1, typename A2, typename A3, typename A4>
    decltype(auto) add(A1&& a1, A2&& a2, A3&& a3, A4&& a4, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3),
                                 std::forward<A4>(a4));
    }

    /// @brief Adds or replaces a component of type T on the entity (same as add()).
    template<typename T>
    decltype(auto) add_or_replace(Location loc = Location::current()) { return emplaceChecked<T>(loc); }
    template<typename T, typename A1>
    decltype(auto) add_or_replace(A1&& a1, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1));
    }
    template<typename T, typename A1, typename A2>
    decltype(auto) add_or_replace(A1&& a1, A2&& a2, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1), std::forward<A2>(a2));
    }
    template<typename T, typename A1, typename A2, typename A3>
    decltype(auto) add_or_replace(A1&& a1, A2&& a2, A3&& a3, Location loc = Location::current()) {
        return emplaceChecked<T>(loc, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3));
    }

    /// @brief Removes a component of type T from the entity.
    template<typename T>
    void remove(Location loc = Location::current()) { wallCheck(loc); m_handle.remove<T>(); }

    /// @brief Checks if the entity has a component of type T.
    template<typename T>
    bool has(Location loc = Location::current()) const { wallCheck(loc); return m_handle.all_of<T>(); }

    /// @brief Safely retrieves a pointer to a component of type T.
    /// @return Pointer to the component, or nullptr if it doesn't exist.
    template<typename T>
    T* try_get(Location loc = Location::current()) const { wallCheck(loc); return m_handle.try_get<T>(); }

    /// @brief Retrieves a reference to a component of type T.
    /// @warning Throws or asserts if the component does not exist.
    template<typename T>
    T& get(Location loc = Location::current()) const { wallCheck(loc); return m_handle.get<T>(); }

    /// @brief Returns the underlying EnTT handle (unchecked by the ECS wall).
    entt::handle handle() const { return m_handle; }

    /// @brief Returns the EnTT registry this entity belongs to (unchecked by the ECS wall).
    entt::registry& registry() const { return *m_handle.registry(); }

    /// @brief Checks if the entity handle is still valid and connected to a registry.
    bool isValid(Location loc = Location::current()) const {
        if (m_handle.registry()) wallCheck(loc);
        return m_handle.valid();
    }

    /// @brief Equality comparison based on underlying EnTT handle.
    bool operator==(const Entity& other) const noexcept { return m_handle == other.m_handle; }
    bool operator!=(const Entity& other) const noexcept { return !(*this == other); }

    /// @brief Boolean conversion for easy validity checks.
    /// @note Checked by the ECS wall with this line as the site (an operator takes no location); prefer isValid().
    explicit operator bool() const noexcept { return isValid(); }

    /// @name Hierarchy & Navigation
    /// @{
    
    /// @brief Reparents this entity to another entity.
    /// @param parent The new parent entity.
    void setParent(Entity& parent, Location loc = Location::current());

    /// @brief Iterates over all immediate children of this entity.
    /// @param callback A function taking (Entity child).
    template<typename Func, typename... Args>
    void forEachChildEntity(Func&& callback, Args&&... args);

    /// @brief Returns the parent entity, or an invalid Entity if at the root.
    Entity getParent(Location loc = Location::current()) const;

    /// @brief Convenience method to get the entity's debug name from IdentityComponent.
    std::string_view getName(Location loc = Location::current()) const;

    /// @brief Convenience method to get the entity's unique UUID from IdentityComponent.
    UUID getUUID(Location loc = Location::current()) const;

    /// @brief Generates a breadcrumb-style path of the entity (e.g., {"Root", "Parent", "Me"}).
    std::vector<std::string> getPath(Location loc = Location::current()) const;

    /// @brief Clones this entity and all its non-transient components into a new entity with a new UUID.
    Entity clone(std::string_view newName = "") const;
    
    /// @}

private:
    /// @brief Any thread: records an access made without a scope when the registry belongs to a Project.
    void wallCheck(const Location& loc) const noexcept {
        const entt::registry* reg = m_handle.registry();
        if (!reg) return;
        if (EcsWall* const* wall = reg->ctx().find<EcsWall*>()) (*wall)->check(loc);
    }

    template<typename T, typename... Args>
    decltype(auto) emplaceChecked(const Location& loc, Args&&... args) {
        wallCheck(loc);
        return m_handle.emplace_or_replace<T>(std::forward<Args>(args)...);
    }

    entt::handle m_handle;
};

} // namespace Brezel

#include "Brezel/Core/CoreComponents.hpp"

namespace Brezel {

inline void Entity::setParent(Entity& parent, Location loc) {
    auto& myHier = get<HierarchyComponent>(loc);
    auto& parentHier = parent.get<HierarchyComponent>(loc);

    myHier.parent = parent;
    parentHier.children.push_back(*this);
}

template<typename Func, typename... Args>
inline void Entity::forEachChildEntity(Func&& callback, Args&&... args){
    if(auto hierachy = try_get<HierarchyComponent>()){
        if(!hierachy->children.empty()){
            for(auto& child : hierachy->children){
                if(child.isValid()){
                    std::invoke(std::forward<Func>(callback), child, std::forward<Args>(args)...);
                }
            }
        }
    }
}

inline Entity Entity::getParent(Location loc) const {
    if(auto hierarchy = try_get<HierarchyComponent>(loc)){
        return hierarchy->parent;
    }
    return Entity();
}

inline std::string_view Entity::getName(Location loc) const {
    if(auto identity = try_get<IdentityComponent>(loc)){
        return identity->name;
    }
    return "";
}

inline UUID Entity::getUUID(Location loc) const {
    if(auto identity = try_get<IdentityComponent>(loc)){
        return identity->uuid;
    }
    return UUID{};
}

inline std::vector<std::string> Entity::getPath(Location loc) const {
    std::vector<std::string> path;
    std::function<void(Entity)> addParentToPath = [&](Entity entity){
        Entity parent = entity.getParent(loc);
        if(parent.isValid(loc)){
            path.push_back(std::string(parent.getName(loc)));
            addParentToPath(parent);
        }
    };
    path.push_back(std::string(getName(loc)));
    addParentToPath(*this);
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace Brezel