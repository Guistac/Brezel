#pragma once

#include "Brezel/Core/Dispatcher.hpp"
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include <new>

namespace Brezel {

/**
 * @brief Fixed-size type-erased mutation envelope for crossing thread boundaries.
 * 
 * Enforces a strict 512-byte maximum capacity on the stack.
 * Trivially copyable PODs use zero-overhead memcpy and no-op destruction.
 * Payloads with non-trivial lifecycle (e.g. std::shared_ptr payloads for massive
 * data like CAD meshes, dense polygon contours, or multi-point spline paths)
 * are properly constructed, copied, moved, and destroyed via type-erased function pointers.
 */
class AnyMutation {
public:
    static constexpr size_t STORAGE_SIZE = 512;

    AnyMutation() = default;

    ~AnyMutation() {
        reset();
    }

    AnyMutation(const AnyMutation& other) {
        copyFrom(other);
    }

    AnyMutation& operator=(const AnyMutation& other) {
        if (this != &other) {
            reset();
            copyFrom(other);
        }
        return *this;
    }

    AnyMutation(AnyMutation&& other) noexcept {
        moveFrom(std::move(other));
    }

    AnyMutation& operator=(AnyMutation&& other) noexcept {
        if (this != &other) {
            reset();
            moveFrom(std::move(other));
        }
        return *this;
    }

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AnyMutation>>>
    AnyMutation(T&& mutation) {
        using CleanT = std::decay_t<T>;
        static_assert(sizeof(CleanT) <= STORAGE_SIZE,
            "Mutation struct exceeds 512 bytes! For larger payloads, store large data in std::shared_ptr.");
        static_assert(alignof(CleanT) <= 64,
            "Mutation struct alignment exceeds 64-byte storage alignment.");

        new (m_storage) CleanT(std::forward<T>(mutation));

        m_invoker = [](const void* data, Dispatcher& dispatcher) {
            dispatcher.trigger(*reinterpret_cast<const CleanT*>(data));
        };

        if constexpr (!std::is_trivially_destructible_v<CleanT>) {
            m_destructor = [](void* data) {
                reinterpret_cast<CleanT*>(data)->~CleanT();
            };
        }

        if constexpr (!std::is_trivially_copy_constructible_v<CleanT>) {
            m_copier = [](void* dst, const void* src) {
                new (dst) CleanT(*reinterpret_cast<const CleanT*>(src));
            };
        }

        if constexpr (!std::is_trivially_move_constructible_v<CleanT>) {
            m_mover = [](void* dst, void* src) {
                new (dst) CleanT(std::move(*reinterpret_cast<CleanT*>(src)));
            };
        }
    }

    void execute(Dispatcher& dispatcher) const {
        if (m_invoker) {
            m_invoker(m_storage, dispatcher);
        }
    }

    explicit operator bool() const { return m_invoker != nullptr; }

    void reset() noexcept {
        if (m_destructor) {
            m_destructor(m_storage);
            m_destructor = nullptr;
        }
        m_invoker = nullptr;
        m_copier = nullptr;
        m_mover = nullptr;
    }

private:
    void copyFrom(const AnyMutation& other) {
        m_invoker = other.m_invoker;
        m_destructor = other.m_destructor;
        m_copier = other.m_copier;
        m_mover = other.m_mover;
        if (other.m_invoker) {
            if (m_copier) {
                m_copier(m_storage, other.m_storage);
            } else {
                std::memcpy(m_storage, other.m_storage, STORAGE_SIZE);
            }
        }
    }

    void moveFrom(AnyMutation&& other) noexcept {
        m_invoker = other.m_invoker;
        m_destructor = other.m_destructor;
        m_copier = other.m_copier;
        m_mover = other.m_mover;
        if (other.m_invoker) {
            if (m_mover) {
                m_mover(m_storage, other.m_storage);
            } else {
                std::memcpy(m_storage, other.m_storage, STORAGE_SIZE);
            }
        }
        other.reset();
    }

    alignas(64) std::byte m_storage[STORAGE_SIZE]{};
    void (*m_invoker)(const void* data, Dispatcher& dispatcher){nullptr};
    void (*m_destructor)(void* data){nullptr};
    void (*m_copier)(void* dst, const void* src){nullptr};
    void (*m_mover)(void* dst, void* src){nullptr};
};

} // namespace Brezel
