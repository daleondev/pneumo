#include "pneumo/coroutines.hpp"

#include <chrono>
#include <iostream>

// The adapters own coroutine factories; referenced resources outlive scope.join().
// NOLINTBEGIN
namespace
{
    namespace ex = pnm::coro::execution;
    using pnm::coro::Task;
    using namespace std::chrono_literals;
}

auto main() -> int
{
    pnm::coro::Context context;
    pnm::coro::ThreadPool workers{ 2 };
    pnm::msg::Bus native;
    pnm::coro::Bus bus{ context, native };
    auto service{ bus.service<int, int>("math/double") };
    auto provider{ service.serve([&](int value, std::stop_token stop) -> Task<int> {
        if (!co_await pnm::coro::sleep(context, 2ms, stop))
            throw pnm::coro::OperationStopped{};
        co_return co_await pnm::coro::runAsync<int>(workers.getExecutionScheduler(),
                                                    [value] { return value * 2; });
    }) };

    std::exception_ptr failure;
    pnm::coro::co_spawn(context, [&](auto&) -> Task<void> {
        ex::counting_scope scope;
        pnm::coro::Channel<bool> ready;
        std::exception_ptr child_error;
        int cleaned{};
        try {
            auto [first, second]{ co_await ex::when_all(
              pnm::coro::asSender(context,
                                  [service](std::stop_token stop) { return service.request(20, 1s, stop); }),
              pnm::coro::asSender(
                context, [service](std::stop_token stop) { return service.request(21, 1s, stop); })) };
            if (!first || !second)
                throw std::runtime_error{ "Service request failed" };
            std::cout << "Concurrent replies on the context thread: " << *first << ", " << *second << '\n';

            for (int id{}; id < 3; ++id) {
                auto operation{ pnm::coro::asSender(context, [&, id](std::stop_token stop) -> Task<void> {
                    ready.push(true);
                    if (id == 2)
                        throw std::runtime_error{ "Demonstrated child failure" };
                    if (!co_await pnm::coro::sleep(context, 1h, stop)) {
                        // The explicit empty token allows asynchronous cleanup after cancellation.
                        co_await pnm::coro::sleep(context, 2ms);
                        ++cleaned;
                        std::cout << "Operation " << id << " finished its cleanup\n";
                    }
                }) };
                // spawn requires error handling before discarding the operation's result.
                ex::spawn(std::move(operation) |
                            ex::upon_error([&](std::exception_ptr error) noexcept { child_error = error; }),
                          scope.get_token());
            }
            for (int id{}; id < 3; ++id)
                co_await ready.next();
        } catch (...) {
            failure = std::current_exception();
        }

        scope.request_stop();
        scope.close();
        co_await scope.join();
        std::cout << "Scope joined: " << cleaned << " cleaned up, " << bool(child_error)
                  << " error handled\n";
        bus.requestStop();
        co_await bus.join();
        context.stop();
    });
    context.run();
    if (failure) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
        }
        return 1;
    }
}
// NOLINTEND
