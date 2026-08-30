#pragma once

#include <cstdint>

namespace wz::core::graph
{
    using NodeHandle = std::uint32_t;
    using EdgeHandle = std::uint32_t;

    inline constexpr NodeHandle INVALID_NODE = 0xFFFF'FFFFu;
    inline constexpr EdgeHandle INVALID_EDGE = 0xFFFF'FFFEu;
}
