#pragma once
#include <entt/entt.hpp>
#include <string_view>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

#include "Brezel/Core/CoreComponents.hpp"
#include "Brezel/Core/ComponentRegistry.hpp"
#include "Brezel/Command/CommandStackVisitor.hpp"
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

class Entity {
public:
    Entity(entt::entity entity, entt::registry& registry) : m_handle(registry, entity){}
    Entity(entt::handle handle) : m_handle(handle){}
    Entity(entt::registry& registry) : m_handle(registry, entt::null){}
    Entity(){}

    template<typename T, typename... Args>
    T& add(Args&&... args) {
        T& component = m_handle.emplace<T>(std::forward<Args>(args)...);
        auto* stack = m_handle.registry()->ctx().get<CommandStack*>();

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

    std::string_view getName(){
        if(auto identity = try_get<IdentityComponent>()){
            return identity->name;
        }
        return "";
    }

    std::vector<std::string> getPath(){
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

private:
    entt::handle m_handle;
};

} // namespace Brezel