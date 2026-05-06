#pragma once
#include <cstdint>
#include <functional>

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
