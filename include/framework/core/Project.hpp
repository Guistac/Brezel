#pragma once
#include <entt/entt.hpp>
#include <string>
#include <memory>

#include "framework/commands/CommandStack.hpp"
#include "framework/core/Entity.hpp"


class Project {
public:
    Project(std::string_view name) 
        : m_name(name) {
            m_registry.ctx().emplace<CommandStack*>(&m_commandStack);
        }

    // No copying projects - they are unique resources
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    std::string_view getName() const { return m_name; }
    entt::registry&  getRegistry()   { return m_registry; }
    CommandStack&    getStack()      { return m_commandStack; }

    // The Project is now the Factory
    Entity createEntity(std::string_view name) {
        auto entity = m_registry.create();
        
        // Enforce your invariants (Name and Hierarchy)
        m_registry.emplace<NameComponent>(entity, std::string(name));
        m_registry.emplace<HierarchyComponent>(entity);
        
        return Entity(entity, m_registry);
    }
    
    // Optional: A helper to destroy entities safely
    void destroyEntity(Entity& ent) {
        if(ent.isValid()){
            m_registry.destroy(ent.handle().entity());
        }
    }

    void reflect(ProjectVisitor& visitor){
        if(visitor.beginProject()){
            visitor.visit_property("Name", m_name);
            auto view = m_registry.view<HierarchyComponent>();
            for(auto entity : view) {
                if(view.get<HierarchyComponent>(entity).parent == entt::null) {
                    Entity ent(entity, m_registry);
                    ent.reflect(visitor);
                }
            }
            visitor.endProject();
        }
    }


    std::string     m_name;
private:
    entt::registry  m_registry;
    CommandStack    m_commandStack;
};