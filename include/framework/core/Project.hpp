#pragma once
#include <entt/entt.hpp>
#include <string>
#include <memory>

#include "framework/commands/CommandStack.hpp"
#include "framework/core/Object.hpp"


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
    Object createObject(std::string_view name) {
        auto entity = m_registry.create();
        
        // Enforce your invariants (Name and Hierarchy)
        m_registry.emplace<NameComponent>(entity, std::string(name));
        m_registry.emplace<HierarchyComponent>(entity);
        
        return Object(entity, m_registry);
    }

    Object createEmptyObject(){
        auto entity = m_registry.create();
        return Object(entity, m_registry);
    }
    
    // Optional: A helper to destroy objects safely
    void destroyObject(Object& obj) {
        if(obj.isValid()){
            m_registry.destroy(obj.handle().entity());
        }
    }

    void reflect(ProjectVisitor& visitor){
        visitor.beginProject();
        visitor.visit_property("Name", m_name);
        auto view = m_registry.view<HierarchyComponent>();
        for(auto entity : view) {
            if(view.get<HierarchyComponent>(entity).parent == entt::null) {
                Object object(entity, m_registry);
                object.reflect(visitor);
            }
        }
        visitor.endProject();
    }

private:
    std::string     m_name;
    entt::registry  m_registry;
    CommandStack    m_commandStack;
};