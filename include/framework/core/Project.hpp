#pragma once
#include <entt/entt.hpp>
#include <string>
#include <memory>

#include "framework/commands/CommandStack.hpp"
#include "framework/core/Entity.hpp"
#include "framework/core/UUIDProvider.hpp"


class Project {
public:
    Project(std::string_view name) :
        m_name(name),
        m_idProvider(std::make_unique<SequentialIDProvider>())
        {
            m_registry.ctx().emplace<CommandStack*>(&m_commandStack);
        }

    // No copying projects - they are unique resources
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    std::string_view getName() const { return m_name; }
    entt::registry&  getRegistry()   { return m_registry; }
    CommandStack&    getStack()      { return m_commandStack; }


    // INTENT: User creates something brand new in the editor
    Entity createEntity(std::string_view name) {
        UUID newId = m_idProvider->generate();
        return instantiateEntity(name, newId);
    }

    // INTENT: The XML loader is reconstructing an existing entity
    // We pass the ID explicitly from the file
    Entity restoreEntity(std::string_view name, UUID existingId) {
        // Safety check: ensure the loader isn't trying to 
        // restore an ID that's already alive in the project
        if (entitiesByUUID.contains(existingId)) {
            //throw DataIntegrityException("Critical: Restore failed. ID already exists.");
        }
        m_idProvider->markAsUsed(existingId);
        return instantiateEntity(name, existingId);
    }

    // Optional: A helper to destroy entities safely
    void destroyEntity(Entity& ent) {
        if(ent.has<IdentityComponent>()){
            auto identity = ent.get<IdentityComponent>();
            entitiesByUUID.erase(identity.uuid);
        }
        if(ent.isValid()){
            m_registry.destroy(ent.handle().entity());
        }
    }

    Entity getEntityByUUID(UUID uuid) {
        auto it = entitiesByUUID.find(uuid);
        if (it != entitiesByUUID.end()) return it->second; 
        else return Entity(m_registry);
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

    IDProvider& ids() { return *m_idProvider; }

    std::string m_name;
private:

    Entity instantiateEntity(std::string_view name, UUID id) {
        auto handle = m_registry.create();
        
        m_registry.emplace<IdentityComponent>(handle, IdentityComponent{
            .name = std::string(name), 
            .uuid = id
        });
        m_registry.emplace<HierarchyComponent>(handle);

        Entity entity(handle, m_registry);
        entitiesByUUID.emplace(id, entity);
        
        return entity;
    }

    entt::registry  m_registry;
    CommandStack    m_commandStack;
    std::unique_ptr<IDProvider> m_idProvider;
    std::unordered_map<UUID, Entity> entitiesByUUID;
};