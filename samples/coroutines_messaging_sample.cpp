#include "pneumo/coroutines_messaging.hpp"

#include <chrono>
#include <print>
#include <thread>

// The provider registrations and co_spawn retain coroutine lambda captures until completion.
// NOLINTBEGIN
namespace
{
    using namespace std::chrono_literals;
    using pnm::coro::Task;
    using pnm::msg::ActionCompletion;
    using pnm::msg::ActionStatus;

    struct SumGoal
    {
        int count{};
    };
    struct Progress
    {
        int completed{};
    };
    struct SumResult
    {
        int sum{};
    };
    using Execution = pnm::coro::ActionExecution<Progress, SumResult>;
}

auto main() -> int
{
    pnm::msg::Bus native;
    pnm::coro::Context provider_context;
    pnm::coro::Context client_context;
    pnm::coro::Bus providers{ provider_context, native };
    pnm::coro::Bus clients{ client_context, native };

    auto lookup{ providers.service<int, int>("math/double")
                   .serve([&provider_context](int value, std::stop_token stop) -> Task<int> {
        co_await pnm::coro::sleep(provider_context, 5ms, stop);
        std::println("[provider] Handling lookup for {}", value);
        co_return value * 2;
    }) };
    auto work{ providers.action<SumGoal, Progress, SumResult>("math/sum")
                 .serve([&provider_context](SumGoal goal,
                                            Execution execution) -> Task<ActionCompletion<SumResult>> {
        std::println("[provider] Starting goal {} with {} steps", execution.id(), goal.count);
        int sum{};
        for (int i{ 1 }; i <= goal.count; ++i) {
            if (!co_await pnm::coro::sleep(provider_context, 5ms, execution.stopToken())) {
                co_await pnm::coro::sleep(provider_context, 1ms); // Asynchronous cleanup.
                co_return ActionCompletion<SumResult>{ ActionStatus::Cancelled, SumResult{ sum } };
            }
            sum += i;
            execution.feedback(Progress{ i });
        }
        co_return ActionCompletion<SumResult>{ ActionStatus::Succeeded, SumResult{ sum } };
    }) };

    providers.topic<int>("sensor/temperature_mC").publish(21500);
    pnm::coro::co_spawn(client_context, [&](pnm::coro::Context&) -> Task<void> {
        auto temperature{ clients.topic<int>("sensor/temperature_mC").subscribe() };
        temperature.latest();
        std::println("[client] Retained temperature: {} millidegrees C",
                     (co_await temperature.next()).value());

        auto response{ co_await clients.service<int, int>("math/double").request(21, 1s) };
        if (response)
            std::println("[client] Lookup result: {}", *response);

        auto action{ clients.action<SumGoal, Progress, SumResult>("math/sum") };
        int first_progress{};
        auto first{ action.sendGoal(
          SumGoal{ 20 },
          { 1s },
          { .on_accepted{ [] { std::println("[client] First goal accepted"); } },
            .on_feedback{ [&](const Progress& progress) { first_progress = progress.completed; } } }) };
        auto second{ action.sendGoal(SumGoal{ 4 }, { 1s }) };
        while (first_progress < 3 && !first.ready())
            co_await pnm::coro::sleep(client_context, 1ms);
        first.requestCancel();
        auto cancelled{ co_await first.result() };
        auto succeeded{ co_await second.result() };
        if (cancelled)
            std::println("[client] First status={}, partial sum={}",
                         static_cast<int>(cancelled->status),
                         cancelled->value.sum);
        if (succeeded)
            std::println(
              "[client] Second status={}, sum={}", static_cast<int>(succeeded->status), succeeded->value.sum);

        clients.requestStop();
        providers.requestStop();
        co_await clients.join();
        co_await providers.join();
        std::println("[client] All messaging work drained");
        provider_context.stop();
        client_context.stop();
    });

    // The application owns both threads. Messaging and its coroutine adapters create none.
    std::jthread provider{ [&] { provider_context.run(); } };
    client_context.run();
    provider.join();
}

// NOLINTEND
