#pragma once
#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Brezel/Command/CommandStack.hpp"
#include "Brezel/Core/Entity.hpp"
#include "Brezel/Core/UUIDProvider.hpp"

namespace Brezel {

class Project {
public:
    Project(std::string_view name) :
        m_name(name),
        m_idProvider(std::make_unique<SequentialIDProvider>())
        {
            m_registry.ctx().emplace<CommandStack*>(&m_commandStack);
        }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    std::string_view getName() const { return m_name; }
    entt::registry&  getRegistry()   { return m_registry; }
    CommandStack&    getStack()      { return m_commandStack; }

    Entity createEntity(std::string_view displayName) {
        UUID newId = m_idProvider->generate();
        std::string strictName = sanitizeName(displayName);
        return instantiateEntity(strictName, displayName, newId);
    }

    Entity restoreEntity(std::string_view name, std::string_view displayName, UUID existingId) {
        m_idProvider->markAsUsed(existingId);
        return instantiateEntity(name, displayName, existingId);
    }

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

    template<typename Func, typename... Args>
    void forEachTopLevelEntity(Func&& callback, Args&&... args) {
        auto view = m_registry.view<HierarchyComponent>();
        for (auto entityHandle : view) {
            const auto& hierarchy = view.get<HierarchyComponent>(entityHandle);
            if (hierarchy.parent == entt::null) {
                Entity ent(entityHandle, m_registry);
                std::invoke(std::forward<Func>(callback), ent, std::forward<Args>(args)...);
            }
        }
    }

    Entity getEntityByPath(std::string_view path) {
        if (path.empty()) return Entity(m_registry);
        
        std::vector<std::string_view> tokens;
        size_t start = 0, end = 0;
        while ((end = path.find('/', start)) != std::string_view::npos) {
            tokens.push_back(path.substr(start, end - start));
            start = end + 1;
        }
        tokens.push_back(path.substr(start));

        Entity current(m_registry);
        auto view = m_registry.view<IdentityComponent>();
        for (auto e : view) {
            if (view.get<IdentityComponent>(e).name == tokens[0]) {
                current = Entity(e, m_registry);
                break;
            }
        }

        if (!current) return current;

        for (size_t i = 1; i < tokens.size(); ++i) {
            bool found_child = false;
            auto& hier = current.get<HierarchyComponent>();
            for (auto child_e : hier.children) {
                Entity child(child_e, m_registry);
                if (child.has<IdentityComponent>() && child.get<IdentityComponent>().name == tokens[i]) {
                    current = child;
                    found_child = true;
                    break;
                }
            }
            if (!found_child) return Entity(m_registry);
        }

        return current;
    }

    static std::string sanitizeName(std::string_view name) {
        std::string sanitized;
        for (char c : name) {
            if (std::isalnum(c) || c == '_' || c == '-') {
                sanitized += c;
            } else {
                sanitized += '_';
            }
        }
        if (sanitized.empty()) sanitized = "Entity";
        return sanitized;
    }

    IDProvider& ids() { return *m_idProvider; }

    std::string m_name;

private:

    Entity instantiateEntity(std::string_view name, std::string_view displayName, UUID id) {
        auto handle = m_registry.create();
        
        m_registry.emplace<IdentityComponent>(handle, IdentityComponent{
            .name = std::string(name), 
            .displayName = std::string(displayName),
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

} // namespace Brezel