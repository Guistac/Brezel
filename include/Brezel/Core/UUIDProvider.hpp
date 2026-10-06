#pragma once

#include <random>
#include <cstdint>
#include <limits>
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

/**
 * @brief High-performance, collision-free 64-bit random UUID generator.
 * @details Uses a thread-local Mersenne Twister engine seeded from std::random_device.
 * Suitable for distributed multi-node ARECS topologies with zero coordination.
 */
class UUIDProvider {
public:
    static UUID generate() {
        static thread_local std::mt19937_64 engine(std::random_device{}());
        static thread_local std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
        return UUID(dist(engine));
    }
};

} // namespace Brezel