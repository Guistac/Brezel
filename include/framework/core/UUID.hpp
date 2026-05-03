#pragma once

struct UUID {
    uint64_t value = 0;

    // Explicit constructors prevent accidental conversion from integers
    UUID() = default;
    explicit UUID(uint64_t val) : value(val) {}

    // Comparison operators for logic and sets
    bool operator==(const UUID& other) const { return value == other.value; }
    bool operator!=(const UUID& other) const { return value != other.value; }
    bool operator<(const UUID& other) const { return value < other.value; }

    // Helper for null checks
    bool isValid() const { return value != 0; }
    
    // Explicit conversion back to primitive if absolutely needed
    explicit operator uint64_t() const { return value; }
};

// Provide a hash function so it can be used in unordered_maps
namespace std {
    template<>
    struct hash<UUID> {
        size_t operator()(const UUID& uuid) const {
            return std::hash<uint64_t>{}(uuid.value);
        }
    };
}
