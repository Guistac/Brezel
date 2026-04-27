#pragma once
#include <entt/entt.hpp>
#include "framework/core/Hierarchy.hpp"

class Object {
public:
    Object(entt::entity entity, entt::registry* registry)
        : m_entity(entity), m_registry(registry) {}

    entt::entity id() const { return m_entity; }

    std::string_view name() const {
        return m_registry->get<NameComponent>(m_entity).name;
    }

    // Helper to add components easily
    template<typename T, typename... Args>
    T& add(Args&&... args) {
        // If the component T can be constructed with a CommandStack* // as the first argument, we fetch it from the registry context.
        if constexpr (std::is_constructible_v<T, CommandStack*, Args...>) {
            auto* stack = m_registry->ctx().get<CommandStack*>();
            return m_registry->emplace<T>(m_entity, stack, std::forward<Args>(args)...);
        } else {
            return m_registry->emplace<T>(m_entity, std::forward<Args>(args)...);
        }
    }

    // Helper to get components
    template<typename T>
    T& get() {
        return m_registry->get<T>(m_entity);
    }

    bool isValid() const { 
        return m_entity != entt::null && m_registry->valid(m_entity); 
    }

private:
    entt::entity m_entity;
    entt::registry* m_registry;
};