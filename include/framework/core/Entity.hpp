#pragma once
#include <entt/entt.hpp>
#include "framework/core/CoreComponents.hpp"
#include "framework/core/ComponentRegistry.hpp"
#include "framework/commands/CommandStackVisitor.hpp"
#include "framework/core/UUID.hpp"

class Entity {
public:
    Entity(entt::entity entity, entt::registry& registry) : m_handle(registry, entity){}
    Entity(entt::handle handle) : m_handle(handle){}
    Entity(entt::registry& registry) : m_handle(registry, entt::null){}
    Entity(){}

    // Helper to add components easily
    template<typename T, typename... Args>
    T& add(Args&&... args) {
        T& component = m_handle.emplace<T>(std::forward<Args>(args)...);
        auto* stack = m_handle.registry()->ctx().get<CommandStack*>();

        // Automatically wire the component if it derives from BaseComponent
        if constexpr (std::is_base_of_v<BaseComponent, T>) {
            component.undoStack = stack;
            CommandStackVisitor visitor(stack);
            component.reflect(visitor);
        }

        return component;
    }

    template<typename T>
    bool has() const { return m_handle.try_get<T>(); }

    template<typename T>
    T* try_get() const { return m_handle.try_get<T>(); }

    template<typename T>
    T& get() const { return m_handle.get<T>(); }

    entt::handle handle() const { return m_handle; }

    entt::registry& registry() const { return *m_handle.registry(); }

    bool isValid() const {  return m_handle.valid(); }

    operator bool() const { return m_handle.valid(); }

    void setParent(Entity& parent) {
        auto& myHier = get<HierarchyComponent>();
        auto& parentHier = parent.get<HierarchyComponent>();

        myHier.parent = parent.handle().entity();
        parentHier.children.push_back(m_handle.entity());
    }

    void reflect(EntityVisitor& visitor){
        if(visitor.beginEntity(*this)){
            ComponentRegistry::reflectEntityComponents(m_handle, visitor);
            if(auto* hierachy = m_handle.try_get<HierarchyComponent>()){
                if(!hierachy->children.empty()){
                    if(visitor.beginEntityChildren()){
                        for(auto childEntity : hierachy->children){
                            Entity child(childEntity, *m_handle.registry());
                            if(child.isValid()){
                                child.reflect(visitor);
                            }
                        }
                        visitor.endEntityChildren();
                    }
                }
            }
            visitor.endEntity(*this);
        }
    }

private:
    entt::handle m_handle;
};