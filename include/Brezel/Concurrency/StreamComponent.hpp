#pragma once
#include "Brezel/Concurrency/TripleBuffer.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include <memory>
#include <type_traits>

namespace Brezel {

/**
 * @brief Generic ECS component bridging an entity to a lock-free TripleBuffer at a stable memory address.
 * 
 * In EnTT, components in contiguous arrays can be moved when other entities are destroyed (swap-and-pop).
 * StreamComponent holds a pointer / shared_ptr to a Simpson TripleBuffer at a fixed memory location,
 * allowing external real-time or network worker threads to read and write wait-free while the ECS
 * safely manages entity lifecycles.
 * 
 * Works for any trivially copyable Plain Old Data (POD) payload in any direction:
 * - Inbound (e.g. Hardware/Network -> ECS World)
 * - Outbound (e.g. ECS World -> Hardware/Network)
 * 
 * If an entity requires multiple distinct streams of the SAME payload type T (e.g. Primary vs
 * Backup network interface, or Front vs Rear scanner), distinguish them using the optional Tag:
 *   StreamComponent<Payload, PrimaryTag>
 *   StreamComponent<Payload, BackupTag>
 */
template <typename T, typename Tag = void>
struct StreamComponent {
    static_assert(std::is_trivially_copyable_v<T>, "StreamComponent payload T must be trivially copyable POD");

    std::shared_ptr<TripleBuffer<T>> buffer;
    TripleBuffer<T>* rawBuffer{nullptr}; // Non-owning fallback (e.g. static slot or stack fixture)
    uint64_t lastTimestampNs{0};
    uint32_t statusWord{0};
    bool isConnected{false};

    TripleBuffer<T>* get() const {
        return buffer ? buffer.get() : rawBuffer;
    }

    TripleBuffer<T>* getBuffer() const {
        return get();
    }

    bool isValid() const {
        return get() != nullptr;
    }
};

template <typename T, typename TagType, typename V>
void reflect(StreamComponent<T, TagType>& stream, V& v) {
    v.visit_property("isConnected", stream.isConnected, {Tag::ReadOnly});
    int status = static_cast<int>(stream.statusWord);
    v.visit_property("statusWord", status, {Tag::ReadOnly});
    float lastTimestampSec = static_cast<float>(stream.lastTimestampNs) * 1e-9f;
    v.visit_property("lastTimestampSec", lastTimestampSec, {Tag::ReadOnly});
}

} // namespace Brezel
