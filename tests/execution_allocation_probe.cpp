// Allocation fault injection and counting are confined to the execution tests/benchmark.
// NOLINTBEGIN
#include "execution_allocation_probe.hpp"
#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
    thread_local bool fail_next{};
    std::atomic<bool> recording{};
    std::atomic<std::size_t> allocations{};
}

auto allocation_probe::failNext() -> void { fail_next = true; }
auto allocation_probe::failureConsumed() -> bool
{
    const bool consumed{ !fail_next };
    fail_next = false;
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
    if (fail_next) {
        fail_next = false;
        throw std::bad_alloc{};
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
