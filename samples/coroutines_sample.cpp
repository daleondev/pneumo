#include "pneumo/coroutines.hpp"

#include <chrono>
#include <iostream>

// NOLINTBEGIN

namespace
{
    auto doubled(int value) -> pnm::coro::Task<int> { co_return value * 2; }

    auto produce(pnm::coro::Channel<int>& channel) -> pnm::coro::Task<void>
    {
        channel.push(co_await pnm::coro::runAsync<int>([] { return 6; }));
        co_await pnm::coro::sleep(std::chrono::milliseconds{ 5 });
        channel.push(co_await doubled(21));
        channel.close();
    }

    auto consume(pnm::coro::Context& context, pnm::coro::Channel<int>& channel) -> pnm::coro::Task<void>
    {
        while (auto value{ co_await channel.next() }) {
            std::cout << "Received: " << *value << '\n';
        }
        context.stop();
    }
}

int main()
{
    pnm::coro::Context context;
    pnm::coro::Channel<int> channel;

    // Spawned callables stay alive until their coroutine finishes.
    pnm::coro::co_spawn(context, [&](pnm::coro::Context& executor) {
        return consume(executor, channel);
    });
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) { return produce(channel); });

    // The consumer stops the event loop after the producer closes the channel.
    context.run();
}

// NOLINTEND
