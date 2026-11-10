#include "pneumo/messaging.hpp"

// NOLINTBEGIN
#include "execution_allocation_probe.hpp"
#include <barrier>
#include <chrono>
#include <exception>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    auto without_allocations(auto&& function) -> void
    {
        std::exception_ptr error;
        allocation_probe::failNext();
        try {
            function();
        } catch (...) {
            error = std::current_exception();
        }
        const bool allocated{ allocation_probe::failureConsumed() };
        EXPECT_FALSE(error);
        EXPECT_FALSE(allocated);
    }
}

TEST(MessagingLifetimeTests, ServiceShutdownDoesNotAllocate)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("allocation-service") };
    std::optional<pnm::msg::Reply<int>> reply;
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> pending) { reply.emplace(std::move(pending)); }) };
    auto active{ service.request(1, 1h) };
    server.poll();
    auto queued{ service.request(2, 1h) };
    without_allocations([&] { server.close(); });
    // Do not block if the regression left the provider open.
    ASSERT_TRUE(active.ready());
    ASSERT_TRUE(queued.ready());
    EXPECT_EQ(active.get().error(), pnm::msg::ServiceError::Unavailable);
    EXPECT_EQ(queued.get().error(), pnm::msg::ServiceError::Unavailable);
    without_allocations([&] { server.close(); });
}

TEST(MessagingLifetimeTests, ActionShutdownDoesNotAllocate)
{
    pnm::msg::Bus bus;
    auto action{ bus.action<int, int, int>("allocation-action") };
    auto server{ action.serve([](int) { return [](auto&) {}; }) };
    std::vector<pnm::msg::ActionError> failures;
    auto callback{ [&](pnm::msg::ActionResult<int> result) {
        ASSERT_FALSE(result);
        failures.push_back(result.error());
    } };
    auto active{ action.sendGoal(1, { .accept_timeout = 1h }, { .on_result = callback }) };
    server.poll();
    auto queued{ action.sendGoal(2, { .accept_timeout = 1h }, { .on_result = callback }) };
    without_allocations([&] { server.close(); });
    EXPECT_TRUE(active.poll());
    EXPECT_TRUE(queued.poll());
    EXPECT_EQ(failures,
              (std::vector{ pnm::msg::ActionError::Unavailable, pnm::msg::ActionError::Unavailable }));
    without_allocations([&] { server.close(); });
}

TEST(MessagingLifetimeTests, ActionStopDoesNotAllocateAndWaitsForActiveCleanup)
{
    pnm::msg::Bus bus;
    auto action{ bus.action<int, int, int>("allocation-action-stop") };
    std::optional<pnm::msg::ActionExecution<int, int>> execution;
    auto server{ action.serveDeferred([&](int, pnm::msg::ActionExecution<int, int> pending) {
        pending.accept();
        execution.emplace(std::move(pending));
    }) };
    std::optional<pnm::msg::ActionResult<int>> active_result;
    std::optional<pnm::msg::ActionResult<int>> queued_result;
    auto active{ action.sendGoal(1, { .accept_timeout = 1h }, { .on_result = [&](auto result) {
        active_result.emplace(std::move(result));
    } }) };
    server.poll();
    auto queued{ action.sendGoal(2, { .accept_timeout = 1h }, { .on_result = [&](auto result) {
        queued_result.emplace(std::move(result));
    } }) };
    without_allocations([&] { server.requestStop(); });
    ASSERT_TRUE(execution);
    EXPECT_TRUE(execution->cancelRequested());
    EXPECT_TRUE(execution->pending());
    EXPECT_FALSE(active.poll());
    EXPECT_FALSE(server.idle());
    EXPECT_TRUE(queued.poll());
    ASSERT_TRUE(queued_result);
    ASSERT_FALSE(*queued_result);
    EXPECT_EQ(queued_result->error(), pnm::msg::ActionError::Unavailable);
    without_allocations([&] { server.requestStop(); });
    EXPECT_TRUE(execution->cancelled(7));
    EXPECT_TRUE(active.poll());
    ASSERT_TRUE(active_result && *active_result);
    EXPECT_EQ(active_result->value().status, pnm::msg::ActionStatus::Cancelled);
    EXPECT_EQ(active_result->value().value, 7);
    EXPECT_TRUE(server.idle());
}

TEST(MessagingLifetimeTests, ConcurrentStopAndPruningCancelEveryRemainingGoal)
{
    for (int iteration{}; iteration < 32; ++iteration) {
        pnm::msg::Bus bus;
        auto action{ bus.action<int, int, int>("stop-prune") };
        std::vector<pnm::msg::ActionExecution<int, int>> executions;
        std::vector<pnm::msg::PendingGoal<int, int>> goals;
        auto server{ action.serveDeferred([&](int, pnm::msg::ActionExecution<int, int> execution) {
            execution.accept();
            executions.push_back(std::move(execution));
        }) };
        for (int index{}; index < 16; ++index)
            goals.push_back(action.sendGoal(index, { .accept_timeout = 1h }, { .on_result = [](auto) {} }));
        server.poll();
        std::barrier start{ 2 };
        std::jthread stopping{ [&] {
            start.arrive_and_wait();
            server.requestStop();
        } };
        start.arrive_and_wait();
        for (std::size_t index{}; index < executions.size(); index += 2) {
            EXPECT_TRUE(executions[index].succeed(1));
            EXPECT_FALSE(server.idle());
        }
        stopping.join();
        for (std::size_t index{ 1 }; index < executions.size(); index += 2) {
            EXPECT_TRUE(executions[index].cancelRequested());
            EXPECT_TRUE(executions[index].cancelled(0));
        }
        EXPECT_TRUE(server.idle());
        for (auto& goal : goals)
            EXPECT_TRUE(goal.poll());
    }
}

TEST(MessagingLifetimeTests, QueuedServiceCaptureCanDestroyItsServerDuringClose)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("reentrant-service-close") };
    std::optional<pnm::msg::ServiceServer<int, int>> server;
    server.emplace(service.serve([](int value) { return value; }));
    bool released{};
    auto capture{ std::shared_ptr<int>{ new int{}, [&](int* value) {
        delete value;
        released = true;
        server.reset();
    } } };
    {
        auto call{ service.request(1, 1h, [capture](auto) {}) };
        capture.reset();
    }
    EXPECT_FALSE(released);
    server->close();
    EXPECT_TRUE(released);
    EXPECT_FALSE(server);
}

TEST(MessagingLifetimeTests, QueuedActionCaptureCanDestroyItsServerDuringClose)
{
    pnm::msg::Bus bus;
    auto action{ bus.action<int, int, int>("reentrant-action-close") };
    std::optional<pnm::msg::ActionServer<int, int, int>> server;
    server.emplace(action.serve([](int) { return [](auto&) {}; }));
    bool released{};
    auto capture{ std::shared_ptr<int>{ new int{}, [&](int* value) {
        delete value;
        released = true;
        server.reset();
    } } };
    {
        auto goal{ action.sendGoal(1, { .accept_timeout = 1h }, { .on_result = [capture](auto) {} }) };
        capture.reset();
    }
    EXPECT_FALSE(released);
    server->close();
    EXPECT_TRUE(released);
    EXPECT_FALSE(server);
}
// NOLINTEND
