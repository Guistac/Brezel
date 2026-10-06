#pragma once

#include "Brezel/Core/Dispatcher.hpp"
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Brezel {

/**
 * @brief Fixed-size type-erased mutation envelope for crossing thread boundaries.
 * 
 * Enforces a strict 256-byte maximum capacity and POD / trivial copyability at compile time.
 * For mutations carrying massive data (e.g., CAD meshes, multi-point spline paths),
 * store the large payload in a std::shared_ptr inside the mutation struct.
 */
class AnyMutation {
public:
    static constexpr size_t STORAGE_SIZE = 256;

    AnyMutation() = default;

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AnyMutation>>>
    AnyMutation(T&& mutation) {
        using CleanT = std::decay_t<T>;
        static_assert(sizeof(CleanT) <= STORAGE_SIZE,
            "Mutation struct exceeds 256 bytes! For larger payloads, store large data in std::shared_ptr.");
        static_assert(std::is_trivially_copyable_v<CleanT>,
            "Mutation struct must be trivially copyable POD for thread-safe queue transfer.");

        std::memcpy(m_storage, &mutation, sizeof(CleanT));
        m_invoker = [](const void* data, Dispatcher& dispatcher) {
            dispatcher.trigger(*reinterpret_cast<const CleanT*>(data));
        };
    }

    void execute(Dispatcher& dispatcher) const {
        if (m_invoker) {
            m_invoker(m_storage, dispatcher);
        }
    }

    explicit operator bool() const { return m_invoker != nullptr; }

private:
    alignas(16) std::byte m_storage[STORAGE_SIZE]{};
    void (*m_invoker)(const void* data, Dispatcher& dispatcher){nullptr};
};

static_assert(std::is_trivially_copyable_v<AnyMutation>, "AnyMutation must be trivially copyable");

} // namespace Brezel
