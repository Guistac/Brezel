#pragma once
#include <entt/entt.hpp>
#include <string_view>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

#include "Brezel/Core/BaseComponent.hpp"
#include "Brezel/Command/CommandStackVisitor.hpp"
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

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

    /// @brief Adds a component of type T to the entity.
    /// @tparam T The component type. Must be registered with ComponentRegistry.
    /// @param args Arguments forwarded to the component constructor.
    /// @return Reference to the newly created component.
    template<typename T, typename... Args>
    T& add(Args&&... args) {
        T& component = m_handle.emplace<T>(std::forward<Args>(args)...);
        auto* stack = m_handle.registry()->ctx().get<CommandStack*>();

        // Framework logic: initialize undo stack and trigger initial reflection
        if constexpr (std::is_base_of_v<BaseComponent, T>) {
            component.undoStack = stack;
            CommandStackVisitor visitor(stack);
            component.reflect(visitor);
        }

        return component;
    }

    /// @brief Checks if the entity has a component of type T.
    template<typename T>
    bool has() const { return m_handle.try_get<T>(); }

    /// @brief Safely retrieves a pointer to a component of type T.
    /// @return Pointer to the component, or nullptr if it doesn't exist.
    template<typename T>
    T* try_get() const { return m_handle.try_get<T>(); }

    /// @brief Retrieves a reference to a component of type T.
    /// @warning Throws or asserts if the component does not exist.
    template<typename T>
    T& get() const { return m_handle.get<T>(); }

    /// @brief Returns the underlying EnTT handle.
    entt::handle handle() const { return m_handle; }

    /// @brief Returns the EnTT registry this entity belongs to.
    entt::registry& registry() const { return *m_handle.registry(); }

    /// @brief Checks if the entity handle is still valid and connected to a registry.
    bool isValid() const {  return m_handle.valid(); }

    /// @brief Boolean conversion for easy validity checks.
    operator bool() const { return m_handle.valid(); }

    /// @name Hierarchy & Navigation
    /// @{
    
    /// @brief Reparents this entity to another entity.
    /// @param parent The new parent entity.
    void setParent(Entity& parent);

    /// @brief Iterates over all immediate children of this entity.
    /// @param callback A function taking (Entity child).
    template<typename Func, typename... Args>
    void forEachChildEntity(Func&& callback, Args&&... args);

    /// @brief Returns the parent entity, or an invalid Entity if at the root.
    Entity getParent();

    /// @brief Convenience method to get the entity's debug name from IdentityComponent.
    std::string_view getName();

    /// @brief Generates a breadcrumb-style path of the entity (e.g., {"Root", "Parent", "Me"}).
    std::vector<std::string> getPath();
    
    /// @}

private:
    entt::handle m_handle;
};

} // namespace Brezel

#include "Brezel/Core/CoreComponents.hpp"

namespace Brezel {

inline void Entity::setParent(Entity& parent) {
    auto& myHier = get<HierarchyComponent>();
    auto& parentHier = parent.get<HierarchyComponent>();

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

inline Entity Entity::getParent(){
    if(auto hierarchy = try_get<HierarchyComponent>()){
        return hierarchy->parent;
    }
    return Entity();
}

inline std::string_view Entity::getName(){
    if(auto identity = try_get<IdentityComponent>()){
        return identity->name;
    }
    return "";
}

inline std::vector<std::string> Entity::getPath(){
    std::vector<std::string> path;
    std::function<void(Entity)> addParentToPath = [&](Entity entity){
        Entity parent = entity.getParent();
        if(parent.isValid()){
            path.push_back(std::string(parent.getName()));
            addParentToPath(parent);
        }
    };
    path.push_back(std::string(getName()));
    addParentToPath(*this);
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace Brezel

#include "Brezel/Core/ComponentRegistry.hpp"