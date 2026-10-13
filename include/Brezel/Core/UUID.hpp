#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string_view>

namespace Brezel {

struct UUID {
    uint64_t value = 0;

    UUID() = default;
    explicit UUID(uint64_t val) : value(val) {}

    bool operator==(const UUID& other) const { return value == other.value; }
    bool operator!=(const UUID& other) const { return value != other.value; }
    bool operator<(const UUID& other) const { return value < other.value; }

    bool isValid() const { return value != 0; }
    explicit operator uint64_t() const { return value; }

    /// @brief Deterministically derives a 64-bit UUID from an arbitrary byte sequence using 64-bit FNV-1a.
    /// @note Guarantees the returned UUID is valid (non-zero).
    static UUID deriveFromBytes(const void* data, size_t length) noexcept {
        if (!data || length == 0) {
            return UUID(1);
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < length; ++i) {
            hash ^= static_cast<uint64_t>(bytes[i]);
            hash *= 1099511628211ULL;
        }
        if (hash == 0) hash = 1;
        return UUID(hash);
    }

    /// @brief Deterministically derives a 64-bit UUID from a string identifier.
    static UUID deriveFromString(std::string_view str) noexcept {
        return deriveFromBytes(str.data(), str.size());
    }
};

} // namespace Brezel

namespace std {
    template<>
    struct hash<Brezel::UUID> {
        size_t operator()(const Brezel::UUID& uuid) const {
            return std::hash<uint64_t>{}(uuid.value);
        }
    };
}
