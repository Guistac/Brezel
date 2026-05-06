#pragma once

#include <string_view>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Brezel {

/**
 * @brief Hashed, interned string ID.
 * Provides O(1) comparison and can be computed at compile-time.
 */
class StringID {
public:
    using HashType = uint64_t;

    static constexpr HashType FNV_OFFSET_BASIS = 14695981039346656037ULL;
    static constexpr HashType FNV_PRIME = 1099511628211ULL;

    static constexpr HashType hash_string(const char* str) {
        HashType hash = FNV_OFFSET_BASIS;
        while (str && *str) {
            hash ^= static_cast<HashType>(*str++);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    static constexpr HashType hash_string_view(std::string_view sv) {
        HashType hash = FNV_OFFSET_BASIS;
        for (char c : sv) {
            hash ^= static_cast<HashType>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    constexpr StringID() : m_hash(0) {}
    
    // Non-constexpr constructor for implicit conversion that performs registration
    StringID(const char* s) : m_hash(hash_string(s)) {
        if (s) register_string(m_hash, s);
    }
    
    explicit constexpr StringID(HashType hash) : m_hash(hash) {}

    static StringID from(std::string_view sv) {
        HashType h = hash_string_view(sv);
        register_string(h, sv);
        return StringID(h);
    }

    HashType hash() const { return m_hash; }
    bool isValid() const { return m_hash != 0; }

    bool operator==(const StringID& other) const { return m_hash == other.m_hash; }
    bool operator!=(const StringID& other) const { return m_hash != other.m_hash; }
    bool operator<(const StringID& other) const { return m_hash < other.m_hash; }

    std::string toString() const {
        std::lock_guard lock(get_registry_mutex());
        auto& reg = get_registry();
        auto it = reg.find(m_hash);
        if (it != reg.end()) return it->second;
        return "Unknown_SID_" + std::to_string(m_hash);
    }

private:
    static void register_string(HashType h, std::string_view sv) {
        std::lock_guard lock(get_registry_mutex());
        get_registry()[h] = std::string(sv);
    }

    static std::unordered_map<HashType, std::string>& get_registry() {
        static std::unordered_map<HashType, std::string> registry;
        return registry;
    }

    static std::mutex& get_registry_mutex() {
        static std::mutex mtx;
        return mtx;
    }

    HashType m_hash;
};

inline consteval StringID operator""_sid(const char* str, size_t) {
    return StringID(StringID::hash_string(str));
}

} // namespace Brezel

#include <spdlog/fmt/fmt.h>

namespace fmt {
    template<>
    struct formatter<Brezel::StringID> {
        constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
        template<typename FormatContext>
        auto format(const Brezel::StringID& sid, FormatContext& ctx) const {
            return fmt::format_to(ctx.out(), "{}", sid.toString());
        }
    };
}

namespace std {
    template<>
    struct hash<Brezel::StringID> {
        size_t operator()(const Brezel::StringID& sid) const {
            return static_cast<size_t>(sid.hash());
        }
    };
}
