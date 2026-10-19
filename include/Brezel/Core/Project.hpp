#pragma once
#include <shared_mutex>
#include <mutex>
#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <source_location>

#include "Brezel/Command/CommandStack.hpp"
#include "Brezel/Core/EcsWall.hpp"
#include "Brezel/Core/Entity.hpp"
#include "Brezel/Core/UUIDProvider.hpp"
#include "Brezel/Core/Dispatcher.hpp"

namespace Brezel {

/// @brief Represents a single Brezel project, containing all entities and systems.
/// @details The Project class owns the EnTT registry and the command stack.
/// It serves as the top-level container for a simulation or game scene.
/// Thread safety is enforced framework-wide via RAII ReadAccess and WriteAccess guards, and checked at runtime by the
/// project's ECS wall (EcsWall.hpp): an Entity access off the writer thread without a live guard is counted and located.
class Project {
public:
    /// @brief Scoped RAII shared-lock guard providing const-only access to the EnTT registry.
    class ReadAccess {
    public:
        ReadAccess(const entt::registry& registry, std::shared_mutex& mutex, const EcsWall* wall = nullptr)
            : m_registry(registry), m_lock(mutex), m_wall(wall) {
            if (m_wall) m_wall->enterScope();
        }
        ~ReadAccess() {
            if (m_wall && m_lock.owns_lock()) m_wall->leaveScope();
        }
        ReadAccess(ReadAccess&&) noexcept = default;
        ReadAccess& operator=(ReadAccess&&) = delete;
        ReadAccess(const ReadAccess&) = delete;
        ReadAccess& operator=(const ReadAccess&) = delete;

        const entt::registry& registry() const { return m_registry; }
        const entt::registry* operator->() const { return &m_registry; }
        const entt::registry& operator*() const { return m_registry; }

        template<typename... Components>
        auto view() const {
            return m_registry.view<const Components...>();
        }

        template<typename... Components, typename... Exclude>
        auto view(entt::exclude_t<Exclude...>) const {
            return m_registry.view<const Components...>(entt::exclude<Exclude...>);
        }

        template<typename T>
        const T& get(entt::entity e) const { return m_registry.get<T>(e); }

        template<typename T>
        const T* try_get(entt::entity e) const { return m_registry.try_get<T>(e); }

        template<typename... T>
        bool all_of(entt::entity e) const { return m_registry.all_of<T...>(e); }

        template<typename... T>
        bool any_of(entt::entity e) const { return m_registry.any_of<T...>(e); }

        bool valid(entt::entity e) const { return m_registry.valid(e); }

        Entity wrap(entt::entity e) const {
            return Entity(e, const_cast<entt::registry&>(m_registry));
        }

        Entity getEntity(entt::entity e) const {
            return Entity(e, const_cast<entt::registry&>(m_registry));
        }

    private:
        const entt::registry& m_registry;
        std::shared_lock<std::shared_mutex> m_lock;
        const EcsWall* m_wall = nullptr;
    };

    /// @brief Scoped RAII unique-lock guard providing mutable access to the EnTT registry.
    class WriteAccess {
    public:
        WriteAccess(entt::registry& registry, std::shared_mutex& mutex, const EcsWall* wall = nullptr)
            : m_registry(registry), m_lock(mutex), m_wall(wall) {
            if (m_wall) m_wall->enterScope();
        }
        ~WriteAccess() {
            if (m_wall && m_lock.owns_lock()) m_wall->leaveScope();
        }
        WriteAccess(WriteAccess&&) noexcept = default;
        WriteAccess& operator=(WriteAccess&&) = delete;
        WriteAccess(const WriteAccess&) = delete;
        WriteAccess& operator=(const WriteAccess&) = delete;

        entt::registry& registry() { return m_registry; }
        entt::registry* operator->() { return &m_registry; }
        entt::registry& operator*() { return m_registry; }

        template<typename... Components>
        auto view() {
            return m_registry.view<Components...>();
        }

        template<typename... Components, typename... Exclude>
        auto view(entt::exclude_t<Exclude...>) {
            return m_registry.view<Components...>(entt::exclude<Exclude...>);
        }

        template<typename T>
        T& get(entt::entity e) { return m_registry.get<T>(e); }

        template<typename T>
        const T& get(entt::entity e) const { return m_registry.get<T>(e); }

        template<typename T>
        T* try_get(entt::entity e) { return m_registry.try_get<T>(e); }

        template<typename T>
        const T* try_get(entt::entity e) const { return m_registry.try_get<T>(e); }

        template<typename... T>
        bool all_of(entt::entity e) const { return m_registry.all_of<T...>(e); }

        template<typename... T>
        bool any_of(entt::entity e) const { return m_registry.any_of<T...>(e); }

        bool valid(entt::entity e) const { return m_registry.valid(e); }
        void clear() { m_registry.clear(); }

        Entity wrap(entt::entity e) {
            return Entity(e, m_registry);
        }

        Entity getEntity(entt::entity e) {
            return Entity(e, m_registry);
        }

        template<typename T, typename... Args>
        decltype(auto) emplace(entt::entity e, Args&&... args) {
            return m_registry.emplace<T>(e, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        decltype(auto) emplace_or_replace(entt::entity e, Args&&... args) {
            return m_registry.emplace_or_replace<T>(e, std::forward<Args>(args)...);
        }

        template<typename T>
        void remove(entt::entity e) { m_registry.remove<T>(e); }

        void destroy(entt::entity e) { m_registry.destroy(e); }

    private:
        entt::registry& m_registry;
        std::unique_lock<std::shared_mutex> m_lock;
        const EcsWall* m_wall = nullptr;
    };

    Project(std::string_view name) :
        m_name(name)
        {
            m_registry.ctx().emplace<CommandStack*>(&m_commandStack);
            m_registry.ctx().emplace<EcsWall*>(&m_wall);
        }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    /// @brief Returns the name of the project.
    std::string_view getName() const { return m_name; }

    /// @brief Acquire RAII shared-lock read access to the EnTT registry (const-only).
    [[nodiscard]] ReadAccess read() const { return ReadAccess(m_registry, m_mutex, &m_wall); }

    /// @brief Acquire RAII unique-lock write access to the EnTT registry (mutable).
    [[nodiscard]] WriteAccess write() { return WriteAccess(m_registry, m_mutex, &m_wall); }

    /// @brief Records the single writer thread, the one thread allowed to access entities without a scope.
    /// Called by the owner of the writer loop on that thread; an empty id clears it. Any thread.
    void setWriterThread(std::thread::id id) noexcept { m_wall.setWriterThread(id); }

    /// @brief The project's ECS wall: violation total and recorded sites. Any thread.
    EcsWall& ecsWall() noexcept { return m_wall; }
    const EcsWall& ecsWall() const noexcept { return m_wall; }

    /// @brief Returns the project's undo/redo command stack.
    CommandStack&    getStack()      { return m_commandStack; }

    /// @brief Clears all entities and UUID mappings from the project.
    void clear() {
        m_registry.clear();
        entitiesByUUID.clear();
    }

    /// @brief Creates a new entity with a unique ID and default components.
    /// @param displayName A human-readable name (not necessarily unique).
    /// @return A new Entity handle.
    Entity createEntity(std::string_view displayName) {
        UUID newId = UUIDProvider::generate();
        std::string strictName = sanitizeName(displayName);
        return instantiateEntity(strictName, displayName, newId);
    }

    /// @brief Restores an entity during loading (preserving its original UUID).
    Entity restoreEntity(std::string_view name, std::string_view displayName, UUID existingId) {
        return instantiateEntity(name, displayName, existingId);
    }

    /// @brief Creates an entity with an explicitly specified UUID, or auto-generates if invalid (0).
    Entity createEntityWithUUID(UUID id, std::string_view displayName) {
        UUID newId = id.isValid() ? id : UUIDProvider::generate();
        std::string strictName = sanitizeName(displayName);
        return instantiateEntity(strictName, displayName, newId);
    }

    /// @brief Permanently removes an entity from the project.
    void destroyEntity(Entity& ent, std::source_location loc = std::source_location::current()) {
        if(ent.has<IdentityComponent>(loc)){
            auto identity = ent.get<IdentityComponent>(loc);
            entitiesByUUID.erase(identity.uuid);
        }
        if(ent.isValid(loc)){
            m_registry.destroy(ent.handle().entity());
        }
    }

    /// @brief Fast lookup of an entity by its unique UUID.
    Entity getEntityByUUID(UUID uuid) {
        auto it = entitiesByUUID.find(uuid);
        if (it != entitiesByUUID.end()) return it->second; 
        else return Entity(m_registry);
    }

    /// @brief Returns the map of all active entities indexed by UUID.
    const std::unordered_map<UUID, Entity>& getEntities() const noexcept { return entitiesByUUID; }

    /// @brief Resolves an entity either by persistent UUID (primary) or local handle fallback.
    Entity resolveEntity(UUID uuid, Entity fallback = {}, std::source_location loc = std::source_location::current()) {
        if (uuid.isValid()) {
            auto ent = getEntityByUUID(uuid);
            if (ent.isValid(loc)) return ent;
        }
        if (fallback.isValid(loc) && &fallback.registry() == &m_registry) {
            return fallback;
        }
        return Entity(m_registry);
    }

    /// @brief Iterates over all entities that do not have a parent.
    /// @param callback A function taking (Entity ent).
    template<typename Func, typename... Args>
    void forEachTopLevelEntity(Func&& callback, Args&&... args) {
        auto view = m_registry.view<HierarchyComponent>();
        for (auto entityHandle : view) {
            const auto& hierarchy = view.get<HierarchyComponent>(entityHandle);
            if (!hierarchy.parent) {
                Entity ent(entityHandle, m_registry);
                std::invoke(std::forward<Func>(callback), ent, std::forward<Args>(args)...);
            }
        }
    }

    /// @brief Resolves an entity handle from a path string (e.g., "Parent/Child/Grandchild").
    /// @return An Entity handle (check isValid() to see if lookup succeeded).
    Entity getEntityByPath(std::string_view path, std::source_location loc = std::source_location::current()) {
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

        if (!current.isValid(loc)) return current;

        for (size_t i = 1; i < tokens.size(); ++i) {
            bool found_child = false;
            auto& hier = current.get<HierarchyComponent>(loc);
            for (auto child : hier.children) {
                if (child.has<IdentityComponent>(loc) && child.get<IdentityComponent>(loc).name == tokens[i]) {
                    current = child;
                    found_child = true;
                    break;
                }
            }
            if (!found_child) return Entity(m_registry);
        }

        return current;
    }

    /// @brief Converts a display name into a "strict" path-safe name.
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

    /// @brief Associates a mutation Dispatcher with this project.
    void setDispatcher(Dispatcher* dispatcher) noexcept { m_dispatcher = dispatcher; }

    /// @brief Gets the associated mutation Dispatcher, or nullptr if none set.
    Dispatcher* getDispatcher() const noexcept { return m_dispatcher; }

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

    mutable EcsWall m_wall;         // declared first: outlives the registry, prints its summary last
    entt::registry  m_registry;
    mutable std::shared_mutex m_mutex;
    CommandStack    m_commandStack;
    std::unordered_map<UUID, Entity> entitiesByUUID;
    Dispatcher*     m_dispatcher{nullptr};
};

} // namespace Brezel