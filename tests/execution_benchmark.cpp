#include "execution_allocation_probe.hpp"
#include "pneumo/coroutines.hpp"
#include <chrono>
#include <iostream>

// NOLINTBEGIN
namespace
{
    using pnm::coro::Task;
    constexpr int ITERATIONS{ 500 };

    auto measure(const char* name, auto operation) -> void
    {
        pnm::coro::Context context;
        const auto start{ std::chrono::steady_clock::now() };
        allocation_probe::begin();
        pnm::coro::co_spawn(context, [&](auto&) -> Task<void> {
            for (int i{}; i < ITERATIONS; ++i)
                co_await operation(context);
            context.stop();
        });
        context.run();
        const auto allocations{ allocation_probe::end() };
        const auto elapsed{
            std::chrono::duration<double, std::micro>{ std::chrono::steady_clock::now() - start }.count()
        };
        std::cout << name << ": " << elapsed / ITERATIONS << " us/iteration, "
                  << static_cast<double>(allocations) / ITERATIONS << " ordinary new allocations/iteration\n";
    }
}

auto main() -> int
{
    namespace ex = pnm::coro::execution;
    pnm::coro::ThreadPool pool{ 2 };
    auto scheduler{ pool.getExecutionScheduler() };
    ex::sync_wait(ex::schedule(scheduler));
    ex::sync_wait(ex::schedule(ex::get_parallel_scheduler()));
    measure("Parallel scheduler (shared workers)", [](auto&) { return pnm::coro::runAsync<void>([] {}); });
    measure("Fixed pool (two dedicated workers)",
            [&](auto&) { return pnm::coro::runAsync<void>(scheduler, [] {}); });
    measure("Two composed pool operations", [&](auto& context) -> Task<void> {
        co_await ex::when_all(pnm::coro::asSender(context, pnm::coro::runAsync<void>(scheduler, [] {})),
                              pnm::coro::asSender(context, pnm::coro::runAsync<void>(scheduler, [] {})));
    });
}
// NOLINTEND
