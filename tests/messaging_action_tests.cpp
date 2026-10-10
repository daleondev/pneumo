#include "pneumo/messaging.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace messaging_action_tests
{
    struct Blob
    {
        std::vector<std::byte> bytes;
    };

    struct MoveOnly
    {
        int value{};
        MoveOnly() = default;
        explicit MoveOnly(int number)
          : value{ number }
        {
        }
        MoveOnly(const MoveOnly&) = delete;
        auto operator=(const MoveOnly&) -> MoveOnly& = delete;
        MoveOnly(MoveOnly&&) = default;
        auto operator=(MoveOnly&&) -> MoveOnly& = default;
    };
}

namespace pnm::utils::memory
{
    template<>
    struct SerializationAdapter<messaging_action_tests::Blob>
    {
        static inline std::function<void()> on_serialize{};
        static auto bufferSize(const messaging_action_tests::Blob& value) -> size_t
        {
            return value.bytes.size();
        }
        static auto serialize(const messaging_action_tests::Blob& value, std::span<std::byte> bytes) -> void
        {
            if (auto hook{ std::exchange(on_serialize, {}) })
                hook();
            if (!value.bytes.empty() && value.bytes.front() == std::byte{ 0xff }) {
                throw std::runtime_error{ "encode failure" };
            }
            std::ranges::copy(value.bytes, bytes.begin());
        }
        static auto deserialize(std::span<const std::byte> bytes, messaging_action_tests::Blob& value) -> void
        {
            if (!bytes.empty() && bytes.front() == std::byte{ 0xfe }) {
                throw std::runtime_error{ "decode failure" };
            }
            value.bytes.assign(bytes.begin(), bytes.end());
        }
    };
}

namespace
{
    using namespace std::chrono_literals;
    using namespace pnm::msg;
    using messaging_action_tests::Blob;
    using Execution = ActionExecution<int, int>;
    constexpr ActionGoalOptions OPTIONS{ .accept_timeout{ 5s } };

    struct Observer
    {
        std::vector<char> events{};
        std::vector<int> feedback{};
        std::optional<ActionResult<int>> result{};
        auto callbacks() -> ActionCallbacks<int, int>
        {
            return { .on_accepted{ [this] { events.push_back('a'); } },
                     .on_feedback{ [this](int value) {
                events.push_back('f');
                feedback.push_back(value);
            } },
                     .on_result{ [this](ActionResult<int> value) {
                events.push_back('r');
                result.emplace(std::move(value));
            } } };
        }
    };

    auto sumFactory(int count)
    {
        return [count, sum{ 0 }, progress{ 0 }](Execution& execution) mutable {
            if (execution.cancelRequested()) {
                execution.cancelled(sum);
                return;
            }
            if (progress < count) {
                sum += ++progress;
                execution.feedback(progress);
            }
            if (progress == count) {
                execution.succeed(sum);
            }
        };
    }

    static_assert(!std::copy_constructible<Execution>);
    static_assert(!std::copy_constructible<PendingGoal<int, int>>);
    static_assert(!std::copy_constructible<ActionServer<int, int, int>>);
    static_assert(std::is_nothrow_move_constructible_v<Execution>);
    static_assert(std::is_nothrow_move_constructible_v<PendingGoal<int, int>>);
    static_assert(std::is_nothrow_move_constructible_v<ActionServer<int, int, int>>);
}

TEST(MessagingActionTests, NamedActionsShareProviderAndCheckAllPayloadTypes)
{
    Bus bus{};
    std::string name{ "sum" };
    auto action{ bus.action<int, int, int>(name) };
    name.clear();
    auto server{ action.serve(sumFactory) };
    Observer observer{};
    auto goal{ bus.action<int, int, int>("sum").sendGoal(1, OPTIONS, observer.callbacks()) };
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(goal.poll());
    ASSERT_TRUE(observer.result->has_value());
    EXPECT_EQ(observer.result->value().value, 1);
    EXPECT_THROW((bus.action<float, int, int>("sum")), std::invalid_argument);
    EXPECT_THROW((bus.action<int, float, int>("sum")), std::invalid_argument);
    EXPECT_THROW((bus.action<int, int, float>("sum")), std::invalid_argument);
    EXPECT_THROW((bus.action<int, int, int>("")), std::invalid_argument);
    EXPECT_NO_THROW(bus.topic<int>("sum"));
    EXPECT_NO_THROW((bus.service<int, int>("sum")));
}

TEST(MessagingActionTests, ActionsAndBusesAreIndependentAndIdsDoNotCollide)
{
    Bus first{};
    Bus second{};
    auto server{ first.action<int, int, int>("a").serve(sumFactory) };
    Observer a{};
    Observer b{};
    auto one{ first.action<int, int, int>("b").sendGoal(1, OPTIONS, a.callbacks()) };
    auto two{ second.action<int, int, int>("a").sendGoal(1, OPTIONS, b.callbacks()) };
    EXPECT_NE(one.id(), two.id());
    EXPECT_NE(one.id(), 0);
    ASSERT_TRUE(one.poll());
    ASSERT_TRUE(two.poll());
    EXPECT_EQ(a.result->error(), ActionError::Unavailable);
    EXPECT_EQ(b.result->error(), ActionError::Unavailable);
}

TEST(MessagingActionTests, RegistrationValidatesHandlersOptionsAndCallbacks)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    std::function<std::function<void(Execution&)>(int)> empty{};
    std::function<void(int, Execution)> deferred{};
    EXPECT_THROW(static_cast<void>(action.serve(empty)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(action.serveDeferred(deferred)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(action.serve(sumFactory, ActionOptions{ 0 })), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(action.sendGoal(1, OPTIONS, {})), std::invalid_argument);
    auto server{ action.serve(sumFactory) };
    EXPECT_THROW(static_cast<void>(action.serve(sumFactory)), std::logic_error);
    server.requestStop();
    EXPECT_THROW(static_cast<void>(action.serve(sumFactory)), std::logic_error);
    server.close();
    EXPECT_NO_THROW(static_cast<void>(action.serve(sumFactory)));
}

TEST(MessagingActionTests, ManagedGoalsAdvanceIndependentlyOnlyOnServerPoll)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    int factories{};
    auto server{ action.serve([&](int count) {
        ++factories;
        return sumFactory(count);
    }) };
    Observer first{};
    Observer second{};
    auto a{ action.sendGoal(4, OPTIONS, first.callbacks()) };
    auto b{ action.sendGoal(2, OPTIONS, second.callbacks()) };
    EXPECT_NE(a.id(), b.id());
    EXPECT_FALSE(server.idle());
    EXPECT_FALSE(a.poll());
    EXPECT_EQ(factories, 0);
    EXPECT_EQ(server.poll(), 2UZ);
    EXPECT_EQ(server.poll(), 2UZ);
    EXPECT_TRUE(first.events.empty()); // Neither provider work nor ready() invokes callbacks.
    EXPECT_FALSE(a.ready());
    EXPECT_TRUE(b.ready());
    EXPECT_EQ(factories, 2);
    EXPECT_FALSE(a.poll());
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(first.events, (std::vector<char>{ 'a', 'f' }));
    EXPECT_EQ(first.feedback, (std::vector<int>{ 2 })); // Feedback is coalesced.
    EXPECT_EQ(second.events, (std::vector<char>{ 'a', 'f', 'r' }));
    ASSERT_TRUE(second.result->has_value());
    EXPECT_EQ(second.result->value().value, 3);
    EXPECT_EQ(server.poll(), 1UZ);
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(a.poll());
    EXPECT_EQ(first.result->value().value, 10);
    EXPECT_TRUE(a.poll()); // A completed handle stays complete without repeating callbacks.
    EXPECT_EQ(first.events, (std::vector<char>{ 'a', 'f', 'f', 'r' }));
    EXPECT_TRUE(server.idle());
    EXPECT_EQ(server.poll(), 0UZ);
}

TEST(MessagingActionTests, CancellationWaitsForCleanupAndOnlyAffectsOneGoal)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve([](int count) {
        return [count, cleanup{ 0 }, work{ 0 }](Execution& execution) mutable {
            if (execution.cancelRequested()) {
                if (++cleanup == 2) {
                    execution.cancelled(work);
                }
            }
            else if (++work == count) {
                execution.succeed(work);
            }
        };
    }) };
    Observer first{};
    Observer second{};
    auto a{ action.sendGoal(10, OPTIONS, first.callbacks()) };
    auto b{ action.sendGoal(2, OPTIONS, second.callbacks()) };
    server.poll();
    EXPECT_TRUE(a.requestCancel());
    EXPECT_TRUE(a.requestCancel());
    EXPECT_FALSE(a.ready());
    server.poll();
    EXPECT_FALSE(a.poll());
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(second.result->value().status, ActionStatus::Succeeded);
    server.poll();
    ASSERT_TRUE(a.poll());
    EXPECT_EQ(first.result->value().status, ActionStatus::Cancelled);
    EXPECT_EQ(first.result->value().value, 1);
    EXPECT_FALSE(a.requestCancel());
}

TEST(MessagingActionTests, CancellationBeforeAdmissionIsVisibleInFirstStep)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve(sumFactory) };
    Observer observer{};
    auto goal{ action.sendGoal(10, OPTIONS, observer.callbacks()) };
    EXPECT_TRUE(goal.requestCancel());
    EXPECT_FALSE(goal.poll());
    server.poll();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.events, (std::vector<char>{ 'a', 'r' }));
    EXPECT_EQ(observer.result->value().status, ActionStatus::Cancelled);
    EXPECT_EQ(observer.result->value().value, 0);
}

TEST(MessagingActionTests, DroppingClientRequestsCleanupWithoutInvokingCallbacks)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    int cleaned{};
    auto server{ action.serve([&](int) {
        return [&](Execution& execution) {
            if (execution.cancelRequested()) {
                ++cleaned;
                execution.cancelled(0);
            }
        };
    }) };
    Observer observer{};
    {
        auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
        server.poll();
    }
    EXPECT_FALSE(server.idle());
    server.poll();
    EXPECT_TRUE(server.idle());
    EXPECT_EQ(cleaned, 1);
    EXPECT_TRUE(observer.events.empty());
}

TEST(MessagingActionTests, DeferredAdmissionAndTerminalTransitions)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::optional<Execution> execution{};
    auto server{ action.serveDeferred([&](int, Execution value) { execution.emplace(std::move(value)); }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(execution);
    EXPECT_EQ(execution->id(), goal.id());
    EXPECT_GT(execution->remainingAcceptanceTime(), 0ns);
    EXPECT_FALSE(execution->feedback(1));
    EXPECT_FALSE(execution->succeed(1));
    EXPECT_FALSE(execution->abort(1));
    EXPECT_FALSE(goal.poll());
    EXPECT_TRUE(execution->accept());
    EXPECT_FALSE(execution->accept());
    EXPECT_FALSE(execution->reject());
    EXPECT_EQ(execution->remainingAcceptanceTime(), 0ns);
    EXPECT_TRUE(execution->feedback(12));
    EXPECT_TRUE(execution->abort(42));
    EXPECT_FALSE(execution->succeed(99));
    EXPECT_FALSE(execution->feedback(99));
    EXPECT_FALSE(execution->pending());
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.events, (std::vector<char>{ 'a', 'f', 'r' }));
    EXPECT_EQ(observer.result->value().status, ActionStatus::Aborted);
    EXPECT_EQ(observer.result->value().value, 42);
}

TEST(MessagingActionTests, DeferredRejectionCancellationAndDroppedTokenBeforeAcceptance)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    auto server{ action.serveDeferred([](int choice, Execution execution) {
        if (choice == 1)
            execution.reject();
        if (choice == 2)
            execution.cancelled(0);
        // choice 3 deliberately drops the token without deciding.
    }) };
    Observer rejected{};
    Observer cancelled{};
    Observer dropped{};
    auto a{ action.sendGoal(1, OPTIONS, rejected.callbacks()) };
    auto b{ action.sendGoal(2, OPTIONS, cancelled.callbacks()) };
    auto c{ action.sendGoal(3, OPTIONS, dropped.callbacks()) };
    server.poll();
    ASSERT_TRUE(a.poll());
    ASSERT_TRUE(b.poll());
    ASSERT_TRUE(c.poll());
    EXPECT_EQ(rejected.result->error(), ActionError::Rejected);
    EXPECT_EQ(cancelled.result->value().status, ActionStatus::Cancelled);
    EXPECT_EQ(dropped.result->error(), ActionError::HandlerFailed);
    EXPECT_EQ(cancelled.events, (std::vector<char>{ 'r' }));
}

TEST(MessagingActionTests, TypedRejectionDeliversOnlyItsResultAndIsTerminal)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("rejected") };
    auto server{ action.serveDeferred([](int, Execution execution) {
        EXPECT_TRUE(execution.reject(42));
        EXPECT_FALSE(execution.pending());
        EXPECT_FALSE(execution.cancelRequested());
        EXPECT_FALSE(execution.accept());
        EXPECT_FALSE(execution.reject(99));
        EXPECT_FALSE(execution.reject());
        EXPECT_FALSE(execution.feedback(1));
        EXPECT_FALSE(execution.succeed(1));
    }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    server.poll();
    EXPECT_TRUE(server.idle());
    ASSERT_TRUE(goal.poll());
    ASSERT_TRUE(observer.result);
    ASSERT_TRUE(observer.result->has_value());
    EXPECT_EQ(observer.result->value().status, ActionStatus::Rejected);
    EXPECT_EQ(observer.result->value().value, 42);
    EXPECT_TRUE(goal.poll());
    EXPECT_EQ(observer.events, (std::vector<char>{ 'r' }));
}

TEST(MessagingActionTests, AcceptedGoalIgnoresTypedRejectionWithoutEncoding)
{
    using Adapter = pnm::utils::memory::SerializationAdapter<Blob>;
    Bus bus{};
    auto action{ bus.action<int, int, Blob>("accepted") };
    std::optional<ActionExecution<int, Blob>> execution{};
    auto server{ action.serveDeferred(
      [&](int, ActionExecution<int, Blob> value) { execution.emplace(std::move(value)); }) };
    std::optional<ActionResult<Blob>> result{};
    auto goal{ action.sendGoal(
      1, OPTIONS, { .on_result{ [&](ActionResult<Blob> value) { result.emplace(std::move(value)); } } }) };
    server.poll();
    ASSERT_TRUE(execution);
    ASSERT_TRUE(execution->accept());
    bool encoded{};
    Adapter::on_serialize = [&] { encoded = true; };
    EXPECT_FALSE(execution->reject(Blob{ { std::byte{ 0xff } } }));
    Adapter::on_serialize = {};
    EXPECT_FALSE(encoded);
    EXPECT_TRUE(execution->pending());
    EXPECT_FALSE(goal.ready());
    EXPECT_TRUE(execution->succeed(Blob{ { std::byte{ 1 } } }));
    ASSERT_TRUE(goal.poll());
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().status, ActionStatus::Succeeded);
    EXPECT_EQ(result->value().value.bytes, (std::vector{ std::byte{ 1 } }));
}

TEST(MessagingActionTests, AcceptanceDuringRejectionEncodingPreservesTheAcceptedGoal)
{
    using Adapter = pnm::utils::memory::SerializationAdapter<Blob>;
    for (const auto byte : { std::byte{ 1 }, std::byte{ 0xff } }) {
        Bus bus{};
        auto action{ bus.action<int, int, Blob>("accepted") };
        std::optional<ActionExecution<int, Blob>> execution{};
        auto server{ action.serveDeferred(
          [&](int, ActionExecution<int, Blob> value) { execution.emplace(std::move(value)); }) };
        std::optional<ActionResult<Blob>> result{};
        auto goal{ action.sendGoal(1, OPTIONS, { .on_result{ [&](ActionResult<Blob> value) {
            result.emplace(std::move(value));
        } } }) };
        server.poll();
        ASSERT_TRUE(execution);
        // Exercise a state change after validation, both with and without an encoding exception.
        bool accepted{};
        Adapter::on_serialize = [&] { accepted = execution->accept(); };
        EXPECT_FALSE(execution->reject(Blob{ { byte } }));
        Adapter::on_serialize = {};
        EXPECT_TRUE(accepted);
        EXPECT_TRUE(execution->pending());
        EXPECT_FALSE(goal.ready());
        EXPECT_TRUE(execution->succeed(Blob{}));
        ASSERT_TRUE(goal.poll());
        ASSERT_TRUE(result);
        ASSERT_TRUE(result->has_value());
        EXPECT_EQ(result->value().status, ActionStatus::Succeeded);
    }
}

TEST(MessagingActionTests, TypedRejectionAdapterFailuresReportHandlerFailed)
{
    for (const auto byte : { std::byte{ 0xff }, std::byte{ 0xfe } }) {
        Bus bus{};
        auto action{ bus.action<int, int, Blob>("rejected") };
        auto server{ action.serveDeferred([byte](int, ActionExecution<int, Blob> execution) {
            EXPECT_EQ(execution.reject(Blob{ { byte } }), byte == std::byte{ 0xfe });
        }) };
        int accepted{};
        std::optional<ActionResult<Blob>> result{};
        auto goal{ action.sendGoal(
          1, OPTIONS, { .on_accepted{ [&] { ++accepted; } }, .on_result{ [&](ActionResult<Blob> value) {
            result.emplace(std::move(value));
        } } }) };
        server.poll();
        ASSERT_TRUE(goal.poll());
        ASSERT_TRUE(result);
        ASSERT_FALSE(result->has_value());
        EXPECT_EQ(result->error(), ActionError::HandlerFailed);
        EXPECT_EQ(accepted, 0);
    }
}

TEST(MessagingActionTests, AcceptanceDeadlineExpiresButNeverTimesOutAcceptedWork)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::vector<Execution> executions{};
    auto server{ action.serveDeferred(
      [&](int, Execution execution) { executions.push_back(std::move(execution)); }) };
    Observer expired{};
    Observer accepted{};
    const ActionGoalOptions options{ .accept_timeout{ 100ms } };
    auto a{ action.sendGoal(1, options, expired.callbacks()) };
    auto b{ action.sendGoal(2, options, accepted.callbacks()) };
    server.poll();
    ASSERT_EQ(executions.size(), 2UZ);
    ASSERT_TRUE(executions[1].accept());
    std::this_thread::sleep_for(150ms);
    EXPECT_TRUE(a.ready());
    EXPECT_FALSE(executions[0].accept());
    EXPECT_TRUE(executions[0].cancelRequested());
    EXPECT_EQ(executions[0].remainingAcceptanceTime(), 0ns);
    ASSERT_TRUE(a.poll());
    EXPECT_EQ(expired.result->error(), ActionError::Timeout);
    EXPECT_FALSE(b.ready());
    EXPECT_TRUE(executions[1].succeed(42));
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(accepted.result->value().value, 42);
}

TEST(MessagingActionTests, NonpositiveAdmissionBudgetsDoNotDispatch)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    int calls{};
    auto server{ action.serve([&](int goal) {
        ++calls;
        return sumFactory(goal);
    }) };
    for (const auto timeout : { 0ms, -1ms }) {
        Observer observer{};
        auto goal{ action.sendGoal(1, ActionGoalOptions{ timeout }, observer.callbacks()) };
        ASSERT_TRUE(goal.poll());
        EXPECT_EQ(observer.result->error(), ActionError::Timeout);
    }
    EXPECT_EQ(server.poll(), 0UZ);
    EXPECT_EQ(calls, 0);
}

TEST(MessagingActionTests, CapacityCountsQueuedAndActiveGoalsUntilTerminalCompletion)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::optional<Execution> execution{};
    auto server{ action.serveDeferred([&](int, Execution value) { execution.emplace(std::move(value)); },
                                      ActionOptions{ 1 }) };
    Observer first{};
    Observer excess{};
    auto a{ action.sendGoal(1, OPTIONS, first.callbacks()) };
    auto b{ action.sendGoal(2, OPTIONS, excess.callbacks()) };
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(excess.result->error(), ActionError::Busy);
    server.poll();
    execution->accept();
    a.requestCancel();
    Observer still_full{};
    auto c{ action.sendGoal(3, OPTIONS, still_full.callbacks()) };
    ASSERT_TRUE(c.poll());
    EXPECT_EQ(still_full.result->error(), ActionError::Busy);
    execution->cancelled(0);
    EXPECT_TRUE(server.idle());
    Observer available{};
    auto d{ action.sendGoal(4, OPTIONS, available.callbacks()) };
    EXPECT_FALSE(d.ready());
    server.poll();
    EXPECT_EQ(execution->id(), d.id());
}

TEST(MessagingActionTests, GracefulStopRejectsQueuedGoalsAndDrainsActiveCleanup)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve([](int) {
        return [cleanup{ 0 }](Execution& execution) mutable {
            if (execution.cancelRequested() && ++cleanup == 2)
                execution.cancelled(5);
        };
    }) };
    Observer active{};
    Observer queued{};
    Observer late{};
    auto a{ action.sendGoal(1, OPTIONS, active.callbacks()) };
    server.poll();
    auto b{ action.sendGoal(2, OPTIONS, queued.callbacks()) };
    server.requestStop();
    auto c{ action.sendGoal(3, OPTIONS, late.callbacks()) };
    ASSERT_TRUE(b.poll());
    ASSERT_TRUE(c.poll());
    EXPECT_EQ(queued.result->error(), ActionError::Unavailable);
    EXPECT_EQ(late.result->error(), ActionError::Unavailable);
    EXPECT_FALSE(server.idle());
    server.poll();
    EXPECT_FALSE(server.idle());
    server.poll();
    EXPECT_TRUE(server.idle());
    ASSERT_TRUE(a.poll());
    EXPECT_EQ(active.result->value().status, ActionStatus::Cancelled);
}

TEST(MessagingActionTests, ClosingProviderFailsDeferredWorkAndAllowsReplacement)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::optional<Execution> execution{};
    Observer observer{};
    auto server{ action.serveDeferred([&](int, Execution value) { execution.emplace(std::move(value)); }) };
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    server.poll();
    server.close();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->error(), ActionError::Unavailable);
    EXPECT_TRUE(execution->cancelRequested());
    EXPECT_FALSE(execution->accept());
    EXPECT_EQ(server.poll(), 0UZ);
    EXPECT_TRUE(server.idle());
    EXPECT_NO_THROW(static_cast<void>(action.serve(sumFactory)));
}

TEST(MessagingActionTests, FactoryCanRejectWithExpectedAndOwnMoveOnlyState)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve([seed{ std::make_unique<int>(40) }](int value) {
        auto step{ [owned{ std::make_unique<int>(*seed + value) }](Execution& execution) {
            execution.succeed(*owned);
        } };
        using Step = decltype(step);
        if (value < 0)
            return std::expected<Step, ActionError>{ std::unexpected{ ActionError::Rejected } };
        return std::expected<Step, ActionError>{ std::move(step) };
    }) };
    Observer good{};
    Observer bad{};
    auto a{ action.sendGoal(2, OPTIONS, good.callbacks()) };
    auto b{ action.sendGoal(-1, OPTIONS, bad.callbacks()) };
    server.poll();
    ASSERT_TRUE(a.poll());
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(good.result->value().value, 42);
    EXPECT_EQ(bad.result->error(), ActionError::Rejected);
    EXPECT_EQ(bad.events, (std::vector<char>{ 'r' }));
}

TEST(MessagingActionTests, FactoryAndStepExceptionsAndEmptyStepsFailOnlyTheirGoal)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve([](int choice) -> std::function<void(Execution&)> {
        if (choice == 1)
            throw std::runtime_error{ "factory" };
        if (choice == 2)
            return [](Execution&) { throw std::runtime_error{ "step" }; };
        return {};
    }) };
    for (int choice{ 1 }; choice <= 3; ++choice) {
        Observer observer{};
        auto goal{ action.sendGoal(choice, OPTIONS, observer.callbacks()) };
        EXPECT_NO_THROW(server.poll());
        ASSERT_TRUE(goal.poll());
        EXPECT_EQ(observer.result->error(), ActionError::HandlerFailed);
    }
    EXPECT_TRUE(server.idle());
}

TEST(MessagingActionTests, CallbackExceptionsPreserveLaterEventsAndDoNotRepeatCompletion)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve(sumFactory) };
    int results{};
    auto goal{ action.sendGoal(1,
                               OPTIONS,
                               { .on_accepted{ [] { throw std::runtime_error{ "accepted" }; } },
                                 .on_feedback{ [](int) { throw std::runtime_error{ "feedback" }; } },
                                 .on_result{ [&](ActionResult<int>) {
        ++results;
        throw std::runtime_error{ "result" };
    } } }) };
    server.poll();
    EXPECT_THROW(goal.poll(), std::runtime_error);
    EXPECT_THROW(goal.poll(), std::runtime_error);
    EXPECT_THROW(goal.poll(), std::runtime_error);
    EXPECT_TRUE(goal.poll());
    EXPECT_EQ(results, 1);
}

TEST(MessagingActionTests, PollRejectsRecursiveDispatchAndCallbacksCanRequestCancellation)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    std::optional<ActionServer<int, int, int>> server{};
    server.emplace(action.serve([&](int) {
        EXPECT_THROW(server->poll(), std::logic_error);
        return [&](Execution& execution) {
            EXPECT_THROW(server->poll(), std::logic_error);
            if (execution.cancelRequested())
                execution.cancelled(0);
        };
    }));
    std::optional<PendingGoal<int, int>> goal{};
    bool done{};
    goal.emplace(action.sendGoal(1,
                                 OPTIONS,
                                 { .on_accepted{ [&] {
        EXPECT_THROW(goal->poll(), std::logic_error);
        EXPECT_TRUE(goal->requestCancel());
    } },
                                   .on_result{ [&](ActionResult<int> result) {
        ASSERT_TRUE(result);
        EXPECT_EQ(result->status, ActionStatus::Cancelled);
        done = true;
    } } }));
    server->poll();
    EXPECT_FALSE(goal->poll());
    server->poll();
    EXPECT_TRUE(goal->poll());
    EXPECT_TRUE(done);
}

TEST(MessagingActionTests, CallbackCanDestroyItsOwnGoalWithoutDispatchingMoreEvents)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve(sumFactory) };
    std::optional<PendingGoal<int, int>> goal{};
    int callbacks{};
    goal.emplace(action.sendGoal(1,
                                 OPTIONS,
                                 { .on_accepted{ [&] {
        ++callbacks;
        goal.reset();
    } },
                                   .on_feedback{ [&](int) { ++callbacks; } },
                                   .on_result{ [&](ActionResult<int>) { ++callbacks; } } }));
    server.poll();
    EXPECT_FALSE(goal->poll());
    EXPECT_FALSE(goal);
    EXPECT_EQ(callbacks, 1);
}

TEST(MessagingActionTests, ProviderAndClientCallbacksRunOnTheirOwnThreads)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    std::thread::id factory_thread{};
    std::thread::id step_thread{};
    auto server{ action.serve([&](int) {
        factory_thread = std::this_thread::get_id();
        return [&](Execution& execution) {
            step_thread = std::this_thread::get_id();
            execution.feedback(1);
            if (execution.cancelRequested())
                execution.cancelled(10);
        };
    }) };
    const auto client_thread{ std::this_thread::get_id() };
    int callbacks{};
    auto goal{ action.sendGoal(1,
                               OPTIONS,
                               { .on_accepted{ [&] {
        EXPECT_EQ(std::this_thread::get_id(), client_thread);
        ++callbacks;
    } },
                                 .on_feedback{ [&](int) {
        EXPECT_EQ(std::this_thread::get_id(), client_thread);
        ++callbacks;
    } },
                                 .on_result{ [&](ActionResult<int> result) {
        EXPECT_EQ(std::this_thread::get_id(), client_thread);
        ASSERT_TRUE(result);
        EXPECT_EQ(result->status, ActionStatus::Cancelled);
        ++callbacks;
    } } }) };
    std::latch stepped{ 1 };
    std::latch cancel{ 1 };
    std::jthread provider{ [&] {
        server.poll();
        stepped.count_down();
        cancel.wait();
        server.poll();
    } };
    const auto provider_id{ provider.get_id() };
    stepped.wait();
    EXPECT_EQ(callbacks, 0);
    EXPECT_FALSE(goal.poll());
    goal.requestCancel();
    cancel.count_down();
    provider.join();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(factory_thread, provider_id);
    EXPECT_EQ(step_thread, provider_id);
    EXPECT_EQ(callbacks, 4);
}

TEST(MessagingActionTests, TwoBusDeferredProxyForwardsAdmissionFeedbackCancellationAndResult)
{
    Bus client_bus{};
    Bus remote_bus{};
    auto remote{ remote_bus.action<int, int, int>("sum") };
    auto remote_server{ remote.serve(sumFactory) };
    auto local{ client_bus.action<int, int, int>("sum") };
    std::optional<Execution> origin{};
    std::optional<PendingGoal<int, int>> forwarded{};
    auto proxy{ local.serveDeferred([&](int goal, Execution execution) {
        const ActionGoalOptions options{ execution.remainingAcceptanceTime() };
        origin.emplace(std::move(execution));
        forwarded.emplace(remote.sendGoal(goal,
                                          options,
                                          { .on_accepted{ [&] { origin->accept(); } },
                                            .on_feedback{ [&](int value) { origin->feedback(value); } },
                                            .on_result{ [&](ActionResult<int> result) {
            if (!result) {
                origin->fail(result.error());
                return;
            }
            switch (result->status) {
                case ActionStatus::Succeeded:
                    origin->succeed(result->value);
                    break;
                case ActionStatus::Aborted:
                    origin->abort(result->value);
                    break;
                case ActionStatus::Cancelled:
                    origin->cancelled(result->value);
                    break;
                case ActionStatus::Rejected:
                    origin->reject(result->value);
                    break;
            }
        } } }));
    }) };
    Observer observer{};
    auto goal{ local.sendGoal(10, OPTIONS, observer.callbacks()) };
    proxy.poll();
    EXPECT_FALSE(goal.poll());
    EXPECT_TRUE(observer.events.empty());
    EXPECT_NE(goal.id(), forwarded->id());
    remote_server.poll();
    forwarded->poll();
    EXPECT_FALSE(goal.poll());
    EXPECT_EQ(observer.events, (std::vector<char>{ 'a', 'f' }));
    EXPECT_TRUE(goal.requestCancel());
    EXPECT_TRUE(origin->cancelRequested());
    forwarded->requestCancel(); // Bridge forwards the cancel by its mapped wire goal ID.
    EXPECT_FALSE(goal.ready());
    remote_server.poll();
    forwarded->poll();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->value().status, ActionStatus::Cancelled);
    EXPECT_EQ(observer.result->value().value, 1);
}

TEST(MessagingActionTests, TransportFailureDoesNotFabricateCancellation)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    auto server{ action.serveDeferred([owned{ std::make_unique<int>(1) }](int, Execution execution) {
        execution.accept();
        EXPECT_EQ(*owned, 1);
        execution.fail(ActionError::TransportError);
    }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    server.poll();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->error(), ActionError::TransportError);
}

TEST(MessagingActionTests, AdapterBackedPayloadsAreOwnedAndDecodedAtDispatch)
{
    Bus bus{};
    auto action{ bus.action<Blob, Blob, Blob>("blob") };
    Blob received{};
    auto server{ action.serveDeferred([&](const Blob& blob, ActionExecution<Blob, Blob> execution) {
        received = blob;
        execution.accept();
        execution.feedback(blob);
        execution.succeed(blob);
    }) };
    Blob source{ { std::byte{ 1 }, std::byte{ 2 } } };
    Blob feedback{};
    std::optional<ActionResult<Blob>> result{};
    auto goal{ action.sendGoal(
      source,
      OPTIONS,
      { .on_feedback{ [&](const Blob& value) { feedback = value; } },
        .on_result{ [&](ActionResult<Blob> value) { result.emplace(std::move(value)); } } }) };
    source.bytes.clear();
    server.poll();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(received.bytes, (std::vector<std::byte>{ std::byte{ 1 }, std::byte{ 2 } }));
    EXPECT_EQ(feedback.bytes, received.bytes);
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().value.bytes, received.bytes);
}

TEST(MessagingActionTests, AdapterFailuresBecomeHandlerFailedExceptSubmissionEncoding)
{
    Bus bus{};
    auto action{ bus.action<Blob, Blob, Blob>("blob") };
    auto server{ action.serveDeferred([](const Blob& blob, ActionExecution<Blob, Blob> execution) {
        execution.accept();
        // Select a malformed feedback/result to exercise both encoding and client decoding.
        const Blob invalid{ { blob.bytes[1] } };
        if (blob.bytes[0] == std::byte{ 1 })
            execution.feedback(invalid);
        else
            execution.succeed(invalid);
        execution.succeed(Blob{});
    }) };
    for (auto value : { Blob{ { std::byte{ 0xfe } } },
                        Blob{ { std::byte{ 1 }, std::byte{ 0xff } } },
                        Blob{ { std::byte{ 2 }, std::byte{ 0xff } } },
                        Blob{ { std::byte{ 1 }, std::byte{ 0xfe } } },
                        Blob{ { std::byte{ 2 }, std::byte{ 0xfe } } } }) {
        std::optional<ActionResult<Blob>> result{};
        auto goal{ action.sendGoal(
          value, OPTIONS, { .on_feedback{ [](const Blob&) {} }, .on_result{ [&](ActionResult<Blob> output) {
            result.emplace(std::move(output));
        } } }) };
        server.poll();
        ASSERT_TRUE(goal.poll());
        ASSERT_TRUE(result);
        EXPECT_EQ(result->error(), ActionError::HandlerFailed);
    }
    EXPECT_THROW(static_cast<void>(action.sendGoal(
                   Blob{ { std::byte{ 0xff } } }, OPTIONS, { .on_result{ [](ActionResult<Blob>) {} } })),
                 std::runtime_error);
}

TEST(MessagingActionTests, ConcurrentSubmissionsKeepDistinctGoalState)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto server{ action.serve(sumFactory) };
    std::vector<std::vector<PendingGoal<int, int>>> handles{ 4 };
    std::vector<std::jthread> clients{};
    std::atomic<int> results{};
    for (int client{}; client < 4; ++client) {
        clients.emplace_back([&, client] {
            for (int i{}; i < 8; ++i) {
                handles[client].push_back(
                  action.sendGoal(2, OPTIONS, { .on_result{ [&](ActionResult<int> result) {
                    ASSERT_TRUE(result);
                    EXPECT_EQ(result->value, 3);
                    ++results;
                } } }));
            }
        });
    }
    clients.clear(); // Join all submitting threads before driving the provider.
    EXPECT_EQ(server.poll(), 32UZ);
    EXPECT_EQ(server.poll(), 32UZ);
    for (auto& group : handles)
        for (auto& goal : group)
            EXPECT_TRUE(goal.poll());
    EXPECT_EQ(results.load(), 32);
    EXPECT_TRUE(server.idle());
}

TEST(MessagingActionTests, MovingHandlesTransfersOwnershipAndCancelsReplacedGoals)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("sum") };
    auto original_server{ action.serve(sumFactory) };
    auto server{ std::move(original_server) };
    EXPECT_TRUE(original_server.idle());
    Observer first{};
    Observer second{};
    auto a{ action.sendGoal(5, OPTIONS, first.callbacks()) };
    auto b{ action.sendGoal(1, OPTIONS, second.callbacks()) };
    const auto id{ b.id() };
    a = std::move(b);
    EXPECT_EQ(a.id(), id);
    EXPECT_EQ(b.id(), 0);
    EXPECT_THROW(b.poll(), std::logic_error);
    EXPECT_FALSE(b.requestCancel());
    server.poll();
    ASSERT_TRUE(a.poll());
    EXPECT_TRUE(first.events.empty());
    EXPECT_EQ(second.result->value().value, 1);
    EXPECT_TRUE(server.idle());
    auto moved{ std::move(action) };
    EXPECT_THROW(static_cast<void>(action.sendGoal(1, OPTIONS, first.callbacks())), std::logic_error);
    EXPECT_THROW(static_cast<void>(action.serve(sumFactory)), std::logic_error);
}

TEST(MessagingActionTests, ConcurrentServerPollIsRejectedWhileStopCanSignalAnActiveStep)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    std::latch entered{ 1 };
    std::latch released{ 1 };
    auto server{ action.serve([&](int) {
        return [&](Execution& execution) {
            entered.count_down();
            released.wait();
            EXPECT_TRUE(execution.cancelRequested());
            execution.cancelled(0);
        };
    }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    std::jthread worker{ [&] { server.poll(); } };
    entered.wait();
    EXPECT_THROW(server.poll(), std::logic_error);
    server.requestStop();
    EXPECT_FALSE(server.idle());
    released.count_down();
    worker.join();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->value().status, ActionStatus::Cancelled);
}

TEST(MessagingActionTests, ConcurrentClientPollIsRejectedWithoutLosingCompletion)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    auto server{ action.serve(sumFactory) };
    std::latch entered{ 1 };
    std::latch released{ 1 };
    int results{};
    auto goal{ action.sendGoal(1,
                               OPTIONS,
                               { .on_accepted{ [&] {
        entered.count_down();
        released.wait();
    } },
                                 .on_result{ [&](ActionResult<int> result) {
        ASSERT_TRUE(result);
        ++results;
    } } }) };
    server.poll();
    std::jthread client{ [&] { EXPECT_TRUE(goal.poll()); } };
    entered.wait();
    EXPECT_THROW(goal.poll(), std::logic_error);
    released.count_down();
    client.join();
    EXPECT_EQ(results, 1);
    EXPECT_TRUE(goal.poll());
}

TEST(MessagingActionTests, CompletionCanWinACancellationRequest)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::optional<Execution> execution{};
    auto server{ action.serveDeferred([&](int, Execution value) { execution.emplace(std::move(value)); }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    server.poll();
    execution->accept();
    EXPECT_TRUE(goal.requestCancel());
    EXPECT_TRUE(execution->succeed(42)); // Cancellation request is not a terminal transition.
    EXPECT_FALSE(execution->cancelled(0));
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->value().status, ActionStatus::Succeeded);
}

TEST(MessagingActionTests, JobCaptureDestructionRunsOutsideProviderLocks)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    std::optional<ActionServer<int, int, int>> server{};
    int released{};
    auto cleanup{ [&](int* value) {
        delete value;
        ++released;
        EXPECT_TRUE(server->idle()); // Reentering a read operation must not deadlock.
    } };
    server.emplace(action.serve([&](int) {
        return [owned{ std::unique_ptr<int, decltype(cleanup)>{ new int{ 42 }, cleanup } }](
                 Execution& execution) { execution.succeed(*owned); };
    }));
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    server->poll();
    EXPECT_EQ(released, 1);
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->value().value, 42);
}

TEST(MessagingActionTests, ServerCanCloseFromInsideAStepWithoutInvalidatingTheInvocation)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    std::optional<ActionServer<int, int, int>> server{};
    server.emplace(action.serve([&](int) {
        return [&](Execution& execution) {
            server->close();
            EXPECT_TRUE(execution.cancelRequested());
            EXPECT_FALSE(execution.succeed(42));
        };
    }));
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    server->poll();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->error(), ActionError::Unavailable);
}

TEST(MessagingActionTests, StopDuringFactoryCreationDoesNotAcceptOrRunTheStep)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    std::latch entered{ 1 };
    std::latch released{ 1 };
    int steps{};
    auto server{ action.serve([&](int) {
        entered.count_down();
        released.wait();
        return [&](Execution&) { ++steps; };
    }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    std::jthread provider{ [&] { server.poll(); } };
    entered.wait();
    server.requestStop();
    released.count_down();
    provider.join();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.events, (std::vector<char>{ 'r' }));
    EXPECT_EQ(observer.result->error(), ActionError::Unavailable);
    EXPECT_EQ(steps, 0);
    EXPECT_TRUE(server.idle());
}

TEST(MessagingActionTests, ExpiredFactoryDoesNotStartExecution)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    int steps{};
    auto server{ action.serve([&](int) {
        std::this_thread::sleep_for(150ms);
        return [&](Execution&) { ++steps; };
    }) };
    Observer observer{};
    auto goal{ action.sendGoal(1, ActionGoalOptions{ 100ms }, observer.callbacks()) };
    server.poll();
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(observer.result->error(), ActionError::Timeout);
    EXPECT_EQ(observer.events, (std::vector<char>{ 'r' }));
    EXPECT_EQ(steps, 0);
}

TEST(MessagingActionTests, DeferredExceptionsInvalidateMovedTokensButPreserveAlreadyCompletedResults)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    std::optional<Execution> retained{};
    auto server{ action.serveDeferred([&](int value, Execution execution) {
        if (value == 1)
            retained.emplace(std::move(execution));
        else {
            execution.accept();
            execution.succeed(42);
        }
        throw std::runtime_error{ "handler" };
    }) };
    Observer failed{};
    Observer succeeded{};
    auto a{ action.sendGoal(1, OPTIONS, failed.callbacks()) };
    auto b{ action.sendGoal(2, OPTIONS, succeeded.callbacks()) };
    server.poll();
    ASSERT_TRUE(a.poll());
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(failed.result->error(), ActionError::HandlerFailed);
    EXPECT_FALSE(retained->accept());
    EXPECT_TRUE(retained->cancelRequested());
    EXPECT_EQ(succeeded.result->value().value, 42);
}

TEST(MessagingActionTests, RetiringExpiredQueuedGoalReleasesCallbacksOutsideProviderLock)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("work") };
    auto server{ action.serve(sumFactory, ActionOptions{ 1 }) };
    int released{};
    auto capture{ std::shared_ptr<int>{ new int{ 0 }, [&](int* value) {
        delete value;
        ++released;
        static_cast<void>(server.idle()); // Can acquire the same provider mutex safely.
    } } };
    {
        auto goal{ action.sendGoal(
          1, ActionGoalOptions{ 100ms }, { .on_result{ [capture](ActionResult<int>) {} } }) };
        capture.reset();
        ASSERT_FALSE(goal.ready());
        std::this_thread::sleep_for(150ms);
        EXPECT_TRUE(goal.ready());
        EXPECT_TRUE(server.idle()); // Removes the weak registration before the queued bytes.
    }
    EXPECT_EQ(released, 0);
    Observer observer{};
    auto next{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
    EXPECT_EQ(released, 1);
    EXPECT_FALSE(next.ready());
    server.poll();
    ASSERT_TRUE(next.poll());
    EXPECT_EQ(observer.result->value().value, 1);
}

TEST(MessagingActionTests, MovingExecutionAndServerAbandonsOnlyTheirPreviousOperations)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::vector<Execution> executions{};
    auto server{ action.serveDeferred(
      [&](int, Execution execution) { executions.push_back(std::move(execution)); }) };
    Observer first{};
    Observer second{};
    auto a{ action.sendGoal(1, OPTIONS, first.callbacks()) };
    auto b{ action.sendGoal(2, OPTIONS, second.callbacks()) };
    server.poll();
    executions[0] = std::move(executions[1]);
    EXPECT_EQ(executions[0].id(), b.id());
    EXPECT_EQ(executions[1].id(), 0);
    EXPECT_FALSE(executions[1].accept());
    EXPECT_FALSE(executions[1].feedback(0));
    EXPECT_FALSE(executions[1].succeed(0));
    ASSERT_TRUE(a.poll());
    EXPECT_EQ(first.result->error(), ActionError::HandlerFailed);
    auto replacement{ bus.action<int, int, int>("other").serve(sumFactory) };
    server = std::move(replacement);
    ASSERT_TRUE(b.poll());
    EXPECT_EQ(second.result->error(), ActionError::Unavailable);
    EXPECT_FALSE(executions[0].pending());
    EXPECT_TRUE(replacement.idle());
    EXPECT_EQ(replacement.poll(), 0UZ);
}

TEST(MessagingActionTests, HandlesOutliveBusAndNewGoalsFromStepsWaitForTheNextPoll)
{
    auto action{ [] {
        Bus bus{};
        return bus.action<int, int, int>("work");
    }() };
    Observer first{};
    Observer second{};
    std::optional<PendingGoal<int, int>> nested{};
    auto server{ action.serve([&](int value) {
        return [&, value](Execution& execution) {
            if (value == 1)
                nested.emplace(action.sendGoal(2, OPTIONS, second.callbacks()));
            execution.succeed(value);
        };
    }) };
    auto goal{ action.sendGoal(1, OPTIONS, first.callbacks()) };
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(goal.poll());
    ASSERT_TRUE(nested);
    EXPECT_FALSE(nested->ready());
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(nested->poll());
    EXPECT_EQ(second.result->value().value, 2);
}

TEST(MessagingActionTests, ConcurrentTerminalReportsDeliverOnlyOneOutcome)
{
    Bus bus{};
    auto action{ bus.action<int, int, int>("remote") };
    std::optional<Execution> execution{};
    auto server{ action.serveDeferred([&](int, Execution value) { execution.emplace(std::move(value)); }) };
    for (int iteration{}; iteration < 16; ++iteration) {
        Observer observer{};
        auto goal{ action.sendGoal(1, OPTIONS, observer.callbacks()) };
        server.poll();
        execution->accept();
        std::latch start{ 1 };
        bool succeeded{};
        bool aborted{};
        std::jthread first{ [&] {
            start.wait();
            succeeded = execution->succeed(1);
        } };
        std::jthread second{ [&] {
            start.wait();
            aborted = execution->abort(2);
        } };
        start.count_down();
        first.join();
        second.join();
        EXPECT_NE(succeeded, aborted);
        ASSERT_TRUE(goal.poll());
        ASSERT_TRUE(observer.result->has_value());
        EXPECT_EQ(observer.result->value().status,
                  succeeded ? ActionStatus::Succeeded : ActionStatus::Aborted);
        EXPECT_EQ(observer.result->value().value, succeeded ? 1 : 2);
        EXPECT_TRUE(goal.poll());
        EXPECT_EQ(observer.events, (std::vector<char>{ 'a', 'r' }));
    }
}

TEST(MessagingActionTests, MoveOnlyPayloadsCanBeDecodedAndTheResultTransferred)
{
    using messaging_action_tests::MoveOnly;
    Bus bus{};
    auto action{ bus.action<MoveOnly, MoveOnly, MoveOnly>("work") };
    auto server{ action.serve([](const MoveOnly& goal) {
        return [value{ goal.value }](ActionExecution<MoveOnly, MoveOnly>& execution) {
            execution.feedback(MoveOnly{ value });
            execution.succeed(MoveOnly{ value * 2 });
        };
    }) };
    int progress{};
    std::optional<ActionResult<MoveOnly>> result{};
    auto goal{ action.sendGoal(
      MoveOnly{ 21 },
      OPTIONS,
      { .on_feedback{ [&](const MoveOnly& value) { progress = value.value; } },
        .on_result{ [&](ActionResult<MoveOnly> value) { result.emplace(std::move(value)); } } }) };
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(goal.poll());
    EXPECT_EQ(progress, 21);
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().status, ActionStatus::Succeeded);
    EXPECT_EQ(result->value().value.value, 42);
}

TEST(MessagingAction, TypedRejectionIsTerminalWithoutAcceptance)
{
    pnm::msg::Bus bus;
    auto action=bus.action<int,int,int>("typed-rejection");
    auto server=action.serveDeferred([](int,auto execution){
        EXPECT_TRUE(execution.reject(42));
        EXPECT_FALSE(execution.accept());
        EXPECT_FALSE(execution.cancelled(9));
    });
    bool accepted{},completed{};
    auto goal=action.sendGoal(1,{std::chrono::seconds(1)},{
        .on_accepted=[&]{accepted=true;},
        .on_result=[&](auto result){ASSERT_TRUE(result);EXPECT_EQ(result->status,pnm::msg::ActionStatus::Rejected);EXPECT_EQ(result->value,42);completed=true;}
    });
    server.poll();goal.poll();EXPECT_FALSE(accepted);EXPECT_TRUE(completed);
}

TEST(MessagingAction, TypedRejectionCannotReplaceAcceptedWork)
{
    pnm::msg::Bus bus;auto action=bus.action<int,int,int>("accepted-rejection");
    auto server=action.serveDeferred([](int,auto execution){
        EXPECT_TRUE(execution.accept());EXPECT_FALSE(execution.reject(42));EXPECT_TRUE(execution.succeed(7));
    });
    bool accepted{},completed{};
    auto goal=action.sendGoal(1,{std::chrono::seconds(1)},{
        .on_accepted=[&]{accepted=true;},
        .on_result=[&](auto result){ASSERT_TRUE(result);EXPECT_EQ(result->status,pnm::msg::ActionStatus::Succeeded);EXPECT_EQ(result->value,7);completed=true;}
    });
    server.poll();goal.poll();EXPECT_TRUE(accepted);EXPECT_TRUE(completed);
}
