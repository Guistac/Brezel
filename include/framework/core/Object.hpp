#pragma once
#include <entt/entt.hpp>
#include "framework/core/Hierarchy.hpp"
#include "framework/serialization/ComponentRegistry.hpp"
#include "framework/commands/CommandStackVisitor.hpp"

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
        T& component = m_registry->emplace<T>(m_entity, std::forward<Args>(args)...);
        auto* stack = m_registry->ctx().get<CommandStack*>();

        // Automatically wire the component if it derives from BaseComponent
        if constexpr (std::is_base_of_v<BaseComponent, T>) {
            component.undoStack = stack;
            CommandStackVisitor visitor(stack);
            component.reflect(visitor);
        }

        return component;
    }

    // Helper to get components
    template<typename T>
    T& get() {
        return m_registry->get<T>(m_entity);
    }

    bool isValid() const { 
        return m_entity != entt::null && m_registry->valid(m_entity); 
    }

    void setParent(Object& parent) {
        auto& myHier = get<HierarchyComponent>();
        auto& parentHier = parent.get<HierarchyComponent>();

        myHier.parent = parent.id();
        parentHier.children.push_back(m_entity);
    }

private:
    entt::entity m_entity;
    entt::registry* m_registry;
};