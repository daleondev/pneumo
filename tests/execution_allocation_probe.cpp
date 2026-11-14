// Allocation fault injection and counting are confined to the execution tests/benchmark.
// NOLINTBEGIN
#include "execution_allocation_probe.hpp"
#include <atomic>
#include <cstdlib>
#include <new>
#include <optional>

namespace
{
    thread_local std::optional<std::size_t> failure_after;
    std::atomic<bool> recording{};
    std::atomic<std::size_t> allocations{};
}

auto allocation_probe::failNext() -> void { failAfter(0); }
auto allocation_probe::failAfter(std::size_t successful_allocations) -> void
{
    failure_after = successful_allocations;
}
auto allocation_probe::failureConsumed() -> bool
{
    const bool consumed{ !failure_after.has_value() };
    failure_after.reset();
    return consumed;
}
auto allocation_probe::begin() -> void
{
    allocations.store(0);
    recording.store(true);
}
auto allocation_probe::end() -> std::size_t
{
    recording.store(false);
    return allocations.load();
}

auto operator new(std::size_t size) -> void*
{
    if (failure_after.has_value()) {
        if (*failure_after == 0) {
            failure_after.reset();
            throw std::bad_alloc{};
        }
        --*failure_after;
    }
    if (recording.load())
        allocations.fetch_add(1);
    if (auto* result{ std::malloc(size ? size : 1) })
        return result;
    throw std::bad_alloc{};
}
auto operator new[](std::size_t size) -> void* { return ::operator new(size); }
auto operator new(std::size_t size, const std::nothrow_t&) noexcept -> void*
{
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
auto operator new[](std::size_t size, const std::nothrow_t&) noexcept -> void*
{
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
// NOLINTEND
