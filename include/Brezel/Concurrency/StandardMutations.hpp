#pragma once

#include "Brezel/Core/Entity.hpp"
#include "Brezel/Core/UUID.hpp"
#include "Brezel/Core/Dispatcher.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Concurrency/StreamComponent.hpp"
#include "Brezel/Concurrency/TripleBuffer.hpp"
#include "Brezel/Concurrency/QueueComponent.hpp"
#include "Brezel/Concurrency/RingBuffer.hpp"
#include "Brezel/Serialization/BinaryVisitor.hpp"
#include "Brezel/Core/ComponentRegistry.hpp"
#include <span>
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
 * @brief Standard discrete mutation to duplicate an entity in a Project.
 *
 * The clone gets a new UUID, is registered like any created entity and receives copies of the source's registered,
 * non-transient components. The producer may choose `newUuid` (to find the clone once the writer has applied the
 * mutation); 0 lets the handler generate one. An empty `newName` means "<source name> (Copy)". Brezel registers no
 * handler: duplication policy (which entities may be cloned, which events fire) belongs to the host, which applies the
 * mutation inside its writer's lock instead of calling `Entity::clone()` from another thread.
 */
struct CloneEntity {
    UUID sourceUuid{0};
    Entity source{};
    UUID newUuid{0};
    char newName[64]{0};

    CloneEntity() = default;
    CloneEntity(UUID srcId, Entity src, UUID cloneId, std::string_view cloneName = {})
        : sourceUuid(srcId), source(src), newUuid(cloneId) {
        size_t n = std::min(cloneName.size(), sizeof(newName) - 1);
        std::memcpy(newName, cloneName.data(), n);
        newName[n] = '\0';
    }
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

/**
 * @brief Standard discrete mutation to update/replace an entity's component state via serialized binary payload.
 */
struct UpdateComponent {
    UUID uuid{0};
    Entity entity{};
    StringID componentName;
    std::vector<uint8_t> payload;

    UpdateComponent() = default;
    UpdateComponent(UUID id, Entity ent, StringID name, std::vector<uint8_t> blob)
        : uuid(id), entity(ent), componentName(name), payload(std::move(blob)) {}
    UpdateComponent(UUID id, StringID name, std::vector<uint8_t> blob)
        : uuid(id), componentName(name), payload(std::move(blob)) {}
    UpdateComponent(UUID id, Entity ent, StringID name, std::span<const uint8_t> blobSpan)
        : uuid(id), entity(ent), componentName(name), payload(blobSpan.begin(), blobSpan.end()) {}
    UpdateComponent(UUID id, StringID name, std::span<const uint8_t> blobSpan)
        : uuid(id), componentName(name), payload(blobSpan.begin(), blobSpan.end()) {}
};

/**
 * @brief Standard discrete mutation to remove a component from an entity.
 */
struct RemoveComponent {
    UUID uuid{0};
    Entity entity{};
    StringID componentName;

    RemoveComponent() = default;
    RemoveComponent(UUID id, Entity ent, StringID name)
        : uuid(id), entity(ent), componentName(name) {}
    RemoveComponent(UUID id, StringID name)
        : uuid(id), componentName(name) {}
};

/**
 * @brief Standard discrete mutation to instantiate a registered component on an entity.
 */
struct AddComponent {
    UUID uuid{0};
    Entity entity{};
    StringID componentName;

    AddComponent() = default;
    AddComponent(UUID id, Entity ent, StringID name)
        : uuid(id), entity(ent), componentName(name) {}
    AddComponent(UUID id, StringID name)
        : uuid(id), componentName(name) {}
};

static_assert(std::is_trivially_copyable_v<DestroyEntity>, "DestroyEntity must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<ClearProject>, "ClearProject must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<CreateEntity>, "CreateEntity must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<CloneEntity>, "CloneEntity must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<RenameEntity>, "RenameEntity must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<RemoveComponent>, "RemoveComponent must be trivially copyable POD");
static_assert(std::is_trivially_copyable_v<AddComponent>, "AddComponent must be trivially copyable POD");

// ==============================================================================
// Standard Mutation Handlers Implementation
// ==============================================================================

namespace Internal {

inline void handleDestroyEntity(Project& project, const DestroyEntity& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (ent.isValid()) {
        UUID id = m.uuid.isValid() ? m.uuid : ent.getUUID();
        project.destroyEntity(ent);
        if (auto* disp = project.getDispatcher()) {
            disp->trigger(Events::EntityDestroyed{ id });
        }
    }
}

inline void handleClearProject(Project& project, const ClearProject& m) {
    (void)m;
    project.clear();
}

inline void handleCreateEntity(Project& project, const CreateEntity& m) {
    Entity ent;
    if (m.uuid.isValid()) {
        ent = project.createEntityWithUUID(m.uuid, m.name);
    } else {
        ent = project.createEntity(m.name);
    }
    if (auto* disp = project.getDispatcher()) {
        disp->trigger(Events::EntitySpawned{ ent.getUUID(), ent });
    }
}

inline void handleRenameEntity(Project& project, const RenameEntity& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    // A name authored by the remote host of a mirror is never renamed locally (ComponentRegistry::acceptsLocalRename)
    if (ent.isValid() && ent.template has<IdentityComponent>() && ComponentRegistry::acceptsLocalRename(ent)) {
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

inline void handleUpdateComponent(Project& project, const UpdateComponent& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (!ent.isValid()) return;

    auto it = ComponentRegistry::componentInfoByTypeName.find(m.componentName);
    if (it == ComponentRegistry::componentInfoByTypeName.end()) return;
    const auto& compInfo = it->second;

    if (compInfo.hasComponent && !compInfo.hasComponent(ent)) {
        if (compInfo.createComponent) {
            compInfo.createComponent(ent);
            if (compInfo.onCreated) compInfo.onCreated(ent);
        }
    }

    if (!m.payload.empty() && compInfo.reflect) {
        BinaryDeserializer deser(m.payload.data(), m.payload.size(), BinaryPropertyFilter::All);
        if (deser.beginComponent(m.componentName)) {
            compInfo.reflect(ent, deser);
            deser.endComponent();
        } else {
            BinaryDeserializer rawDeser(m.payload.data(), m.payload.size(), BinaryPropertyFilter::All);
            compInfo.reflect(ent, rawDeser);
        }
    }

    if (compInfo.onUpdated) {
        compInfo.onUpdated(ent);
    }

    if (auto* disp = project.getDispatcher()) {
        disp->trigger(Events::ComponentChanged{ m.uuid.isValid() ? m.uuid : ent.getUUID(), ent, m.componentName });
    }
}

inline void handleRemoveComponent(Project& project, const RemoveComponent& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (!ent.isValid()) return;

    auto it = ComponentRegistry::componentInfoByTypeName.find(m.componentName);
    if (it != ComponentRegistry::componentInfoByTypeName.end() && it->second.removeComponent) {
        it->second.removeComponent(ent);
        if (it->second.onRemoved) {
            it->second.onRemoved(ent);
        }
    }
}

inline void handleAddComponent(Project& project, const AddComponent& m) {
    auto ent = project.resolveEntity(m.uuid, m.entity);
    if (!ent.isValid()) return;

    auto it = ComponentRegistry::componentInfoByTypeName.find(m.componentName);
    if (it != ComponentRegistry::componentInfoByTypeName.end() && it->second.createComponent) {
        if (!it->second.hasComponent || !it->second.hasComponent(ent)) {
            it->second.createComponent(ent);
            if (it->second.onCreated) {
                it->second.onCreated(ent);
            }
        }
    }
}

} // namespace Internal

/**
 * @brief Registers standard lifecycle mutation handlers on a Dispatcher.
 */
inline void registerStandardMutationHandlers(Project& project, Dispatcher& dispatcher) {
    project.setDispatcher(&dispatcher);
    ComponentRegistry::s_activeProject = &project;
    ComponentRegistry::s_activeDispatcher = &dispatcher;
    registerBrezelCoreComponents();

    dispatcher.sink<DestroyEntity>().connect<&Internal::handleDestroyEntity>(project);
    dispatcher.sink<ClearProject>().connect<&Internal::handleClearProject>(project);
    dispatcher.sink<CreateEntity>().connect<&Internal::handleCreateEntity>(project);
    dispatcher.sink<RenameEntity>().connect<&Internal::handleRenameEntity>(project);
    dispatcher.sink<UpdateComponent>().connect<&Internal::handleUpdateComponent>(project);
    dispatcher.sink<RemoveComponent>().connect<&Internal::handleRemoveComponent>(project);
    dispatcher.sink<AddComponent>().connect<&Internal::handleAddComponent>(project);

    for (const auto& [id, info] : ComponentRegistry::componentInfoById) {
        if (info.registerSetHandler) {
            info.registerSetHandler(project, dispatcher);
        }
    }
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
