#pragma once
#include "Brezel/Concurrency/RingBuffer.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include <memory>
#include <type_traits>
#include <cstddef>

namespace Brezel {

/**
 * @brief Generic ECS component bridging an entity to a lock-free Single-Producer Single-Consumer (SPSC) RingBuffer.
 * 
 * In EnTT, components in contiguous arrays can be moved when other entities are destroyed (swap-and-pop).
 * QueueComponent holds a pointer / shared_ptr to a RingBuffer at a fixed memory location,
 * allowing discrete event streams (such as MotionCommands, Mode switches, Cues, and E-Stops) to be transferred
 * across the NRT/RT boundary losslessly in strict FIFO order while the ECS safely manages entity lifecycles.
 * 
 * Works for any trivially copyable Plain Old Data (POD) payload:
 * - Outbound (e.g. ECS World -> RT Motion Controller)
 * - Inbound (e.g. Hardware Sensor Event FIFO -> ECS World)
 * 
 * If an entity requires multiple distinct queues of the SAME payload type T (e.g. Primary vs Emergency channel),
 * distinguish them using the optional Tag:
 *   QueueComponent<Payload, 64, PrimaryTag>
 *   QueueComponent<Payload, 16, EmergencyTag>
 */
template <typename T, std::size_t Capacity = 64, typename Tag = void>
struct QueueComponent {
    static_assert(std::is_trivially_copyable_v<T>, "QueueComponent payload T must be trivially copyable POD");
    static_assert(Capacity > 0, "Capacity must be greater than 0");

    std::shared_ptr<RingBuffer<T, Capacity>> queue;
    RingBuffer<T, Capacity>* rawQueue{nullptr}; // Non-owning fallback (e.g. static slot or stack fixture)
    uint64_t lastTimestampNs{0};
    uint32_t statusWord{0};
    bool isConnected{false};

    QueueComponent() = default;

    explicit QueueComponent(std::shared_ptr<RingBuffer<T, Capacity>> q)
        : queue(std::move(q)) {}

    explicit QueueComponent(RingBuffer<T, Capacity>* raw)
        : rawQueue(raw) {}

    RingBuffer<T, Capacity>* get() const {
        return queue ? queue.get() : rawQueue;
    }

    RingBuffer<T, Capacity>* getQueue() const {
        return get();
    }

    bool isValid() const {
        return get() != nullptr;
    }

    bool push(const T& value) {
        auto* q = get();
        return q ? q->push(value) : false;
    }

    bool pop(T& out_value) {
        auto* q = get();
        return q ? q->pop(out_value) : false;
    }

    bool empty() const {
        auto* q = get();
        return q ? q->empty() : true;
    }

    std::size_t size() const {
        auto* q = get();
        return q ? q->size() : 0;
    }
};

template <typename T, std::size_t Capacity, typename TagType, typename V>
void reflect(QueueComponent<T, Capacity, TagType>& queue, V& v) {
    v.visit_property("isConnected", queue.isConnected, {Tag::ReadOnly});
    int status = static_cast<int>(queue.statusWord);
    v.visit_property("statusWord", status, {Tag::ReadOnly});
    float lastTimestampSec = static_cast<float>(queue.lastTimestampNs) * 1e-9f;
    v.visit_property("lastTimestampSec", lastTimestampSec, {Tag::ReadOnly});
}

} // namespace Brezel
