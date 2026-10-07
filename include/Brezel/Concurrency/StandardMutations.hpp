#pragma once

#include "Brezel/Core/Entity.hpp"
#include "Brezel/Core/UUID.hpp"
#include "Brezel/Core/Dispatcher.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Concurrency/StreamComponent.hpp"
#include "Brezel/Concurrency/TripleBuffer.hpp"
#include "Brezel/Concurrency/QueueComponent.hpp"
#include "Brezel/Concurrency/RingBuffer.hpp"
#include <string_view>
#include <cstring>
#include <algorithm>
#include <type_traits>

namespace Brezel::Mutations {

/**
 * @brief Standard discrete mutation to permanently destroy an entity from a Project.
 */
struct DestroyEntity {
    UUID uuid{0};
    Entity entity{};
};

/**
 * @brief Standard discrete mutation to clear all entities from a Project.
 */
struct ClearProject {
    bool resetState{false};
};

/**
 * @brief Standard discrete mutation to create an entity in a Project with optional UUID and name.
 */
struct CreateEntity {
    UUID uuid{0};
    char name[64]{0};

    CreateEntity() = default;
    CreateEntity(UUID id, std::string_view entityName) : uuid(id) {
        size_t n = std::min(entityName.size(), sizeof(name) - 1);
        std::memcpy(name, entityName.data(), n);
        name[n] = '\0';
    }
    explicit CreateEntity(std::string_view entityName) : CreateEntity(UUID(0), entityName) {}
};

/**
 * @brief Standard discrete mutation to rename an entity in a Project.
 */
struct RenameEntity {
    UUID uuid{0};
    Entity entity{};
    char newName[64]{0};

    RenameEntity() = default;
    RenameEntity(UUID id, Entity ent, std::string_view name) : uuid(id), entity(ent) {
        size_t n = std::min(name.size(), sizeof(newName) - 1);
        std::memcpy(newName, name.data(), n);
        newName[n] = '\0';
    }
};

/**
 * @brief Standard discrete mutation to attach a lock-free TripleBuffer StreamComponent to an entity.
 */
template <typename T, typename Tag = void>
struct AttachStream {
    UUID uuid{0};
    Entity entity{};
    TripleBuffer<T>* rawBuffer{nullptr};
};

/**
 * @brief Standard discrete mutation to detach a lock-free TripleBuffer StreamComponent from an entity.
 */
template <typename T, typename Tag = void>
struct DetachStream {
    UUID uuid{0};
    Entity entity{};
};

/**
 * @brief Standard discrete mutation to attach a lock-free RingBuffer QueueComponent to an entity.
 */
template <typename T, std::size_t Capacity = 64, typename Tag = void>
struct AttachQueue {
    UUID uuid{0};
    Entity entity{};
    RingBuffer<T, Capacity>* rawQueue{nullptr};
};

/**
 * @brief Standard discrete mutation to detach a lock-free RingBuffer QueueComponent from an entity.
 */
template <typename T, std::size_t Capacity = 64, typename Tag = void>
struct DetachQueue {
    UUID uuid{0};
    Entity entity{};
};

static_assert(std::is_trivially_copyable_v<DestroyEntity>, "DestroyEntity must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<ClearProject>, "ClearProject must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<CreateEntity>, "CreateEntity must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<RenameEntity>, "RenameEntity must be trivially copyable POD");

// ==============================================================================
// Standard Mutation Handlers Implementation
// ==============================================================================

namespace Internal {

inline void handleDestroyEntity(Project& project, const DestroyEntity& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid()) {
        project.destroyEntity(ent);
    }
}

inline void handleClearProject(Project& project, const ClearProject& m) {
    (void)m;
    project.clear();
}

inline void handleCreateEntity(Project& project, const CreateEntity& m) {
    if (m.uuid.isValid()) {
        project.createEntityWithUUID(m.uuid, m.name);
    } else {
        project.createEntity(m.name);
    }
}

inline void handleRenameEntity(Project& project, const RenameEntity& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid() && ent.template has<IdentityComponent>()) {
        auto& identity = ent.template get<IdentityComponent>();
        identity.name = m.newName;
        identity.displayName = m.newName;
    }
}

template <typename T, typename Tag>
inline void handleAttachStream(Project& project, const AttachStream<T, Tag>& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid()) {
        StreamComponent<T, Tag> stream{};
        stream.rawBuffer = m.rawBuffer;
        ent.template add_or_replace<StreamComponent<T, Tag>>(stream);
    }
}

template <typename T, typename Tag>
inline void handleDetachStream(Project& project, const DetachStream<T, Tag>& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid() && ent.template has<StreamComponent<T, Tag>>()) {
        ent.template remove<StreamComponent<T, Tag>>();
    }
}

template <typename T, std::size_t Capacity, typename Tag>
inline void handleAttachQueue(Project& project, const AttachQueue<T, Capacity, Tag>& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid()) {
        QueueComponent<T, Capacity, Tag> queue{};
        queue.rawQueue = m.rawQueue;
        ent.template add_or_replace<QueueComponent<T, Capacity, Tag>>(queue);
    }
}

template <typename T, std::size_t Capacity, typename Tag>
inline void handleDetachQueue(Project& project, const DetachQueue<T, Capacity, Tag>& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid() && ent.template has<QueueComponent<T, Capacity, Tag>>()) {
        ent.template remove<QueueComponent<T, Capacity, Tag>>();
    }
}

} // namespace Internal

/**
 * @brief Registers standard lifecycle mutation handlers (DestroyEntity, ClearProject, CreateEntity, RenameEntity) on a Dispatcher.
 */
inline void registerStandardMutationHandlers(Project& project, Dispatcher& dispatcher) {
    dispatcher.sink<DestroyEntity>().connect<&Internal::handleDestroyEntity>(project);
    dispatcher.sink<ClearProject>().connect<&Internal::handleClearProject>(project);
    dispatcher.sink<CreateEntity>().connect<&Internal::handleCreateEntity>(project);
    dispatcher.sink<RenameEntity>().connect<&Internal::handleRenameEntity>(project);
}

/**
 * @brief Registers standard StreamComponent attach/detach mutation handlers for payload type T and optional Tag.
 */
template <typename T, typename Tag = void>
inline void registerStreamMutationHandlers(Project& project, Dispatcher& dispatcher) {
    dispatcher.sink<AttachStream<T, Tag>>().template connect<&Internal::handleAttachStream<T, Tag>>(project);
    dispatcher.sink<DetachStream<T, Tag>>().template connect<&Internal::handleDetachStream<T, Tag>>(project);
}

/**
 * @brief Registers standard QueueComponent attach/detach mutation handlers for payload type T, Capacity, and optional Tag.
 */
template <typename T, std::size_t Capacity = 64, typename Tag = void>
inline void registerQueueMutationHandlers(Project& project, Dispatcher& dispatcher) {
    dispatcher.sink<AttachQueue<T, Capacity, Tag>>().template connect<&Internal::handleAttachQueue<T, Capacity, Tag>>(project);
    dispatcher.sink<DetachQueue<T, Capacity, Tag>>().template connect<&Internal::handleDetachQueue<T, Capacity, Tag>>(project);
}

} // namespace Brezel::Mutations
