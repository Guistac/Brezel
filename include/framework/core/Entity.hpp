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

    template<typename Func, typename... Args>
    void forEachChildEntity(Func&& callback, Args&&... args){
        if(auto hierachy = try_get<HierarchyComponent>()){
            if(!hierachy->children.empty()){
                for(auto child : hierachy->children){
                    Entity childEntity(child, registry());
                    if(childEntity.isValid()){
                        // Invoke the callback with the entity and any extra arguments
                        std::invoke(std::forward<Func>(callback), childEntity, std::forward<Args>(args)...);
                    }
                }
            }
        }
    }

    Entity getParent(){
        if(auto hierarchy = try_get<HierarchyComponent>()){
            return Entity(hierarchy->parent, registry());
        }
        return Entity();
    }

    const char* getName(){
        if(auto identity = try_get<IdentityComponent>()){
            return identity->name.c_str();
        }
        return "";
    }

    std::vector<std::string> getPath(){
        std::vector<std::string> path;
        std::function<void(Entity)> addParentToPath = [&](Entity entity){
            Entity parent = entity.getParent();
            if(parent.isValid()){
                std::string parentName = parent.getName();
                path.push_back(parentName);
                addParentToPath(parent);
            }
        };
        path.push_back(getName());
        addParentToPath(*this);
        std::reverse(path.begin(), path.end());
        return path;
    }

private:
    entt::handle m_handle;
};