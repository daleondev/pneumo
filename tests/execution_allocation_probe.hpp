#pragma once
#include <cstddef>

namespace allocation_probe
{
    auto failNext() -> void;
    auto failureConsumed() -> bool;
    auto begin() -> void;
    auto end() -> std::size_t;
}
