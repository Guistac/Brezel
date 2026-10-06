#pragma once

#include <entt/entt.hpp>

namespace Brezel {

/**
 * @brief High-performance event dispatcher primitive.
 * Wraps entt::dispatcher inside the Brezel framework boundary to maintain EnTT quarantine in application code.
 */
using Dispatcher = entt::dispatcher;

} // namespace Brezel
