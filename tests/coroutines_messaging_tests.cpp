#include "pneumo/coroutines.hpp"

// Keep brace initialization for fixtures; Clang warns on designated scalar brace initializers.
// NOLINTBEGIN(modernize-use-designated-initializers)

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <limits>
#include <thread>

namespace coroutines_messaging_tests
{
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
    struct HookedMove
    {
        static inline std::function<void()> on_move;
        int value{};
        HookedMove() = default;
        explicit HookedMove(int number)
          : value{ number }
        {
        }
        HookedMove(const HookedMove&) = default;
        auto operator=(const HookedMove&) -> HookedMove& = default;
        HookedMove(HookedMove&& other)
          : value{ other.value }
        {
            if (on_move)
                on_move();
        }
        auto operator=(HookedMove&&) -> HookedMove& = default;
    };
    struct Payload
    {
        std::vector<std::byte> bytes;
    };
}
namespace pnm::utils::memory
{
    template<>
    struct SerializationAdapter<coroutines_messaging_tests::HookedMove>
    {
        using Value = coroutines_messaging_tests::HookedMove;
        static inline std::function<void()> on_serialize;
        static auto bufferSize(const Value&) -> size_t { return sizeof(int); }
        static auto serialize(const Value& value, std::span<std::byte> bytes) -> void
        {
            pnm::utils::memory::serialize(value.value, bytes);
            if (auto hook{ std::exchange(on_serialize, {}) })
                hook();
        }
        static auto deserialize(std::span<const std::byte> bytes, Value& value) -> void
        {
            pnm::utils::memory::deserialize(bytes, value.value);
        }
    };
    template<>
    struct SerializationAdapter<coroutines_messaging_tests::Payload>
    {
        using Payload = coroutines_messaging_tests::Payload;
        static auto bufferSize(const Payload& value) -> size_t { return value.bytes.size(); }
        static auto serialize(const Payload& value, std::span<std::byte> bytes) -> void
        {
            if (!value.bytes.empty() && value.bytes.front() == std::byte{ 0xff })
                throw std::runtime_error{ "encode" };
            std::ranges::copy(value.bytes, bytes.begin());
        }
        static auto deserialize(std::span<const std::byte> bytes, Payload& value) -> void
        {
            if (!bytes.empty() && bytes.front() == std::byte{ 0xfe })
                throw std::runtime_error{ "decode" };
            value.bytes.assign(bytes.begin(), bytes.end());
        }
    };
}

namespace
{
    using namespace std::chrono_literals;
    using pnm::coro::Task;
    using pnm::msg::ActionCompletion;
    using pnm::msg::ActionStatus;
    using Execution = pnm::coro::ActionExecution<int, int>;

    struct Environment
    {
        pnm::msg::Bus native;
        pnm::coro::Context context;
        pnm::coro::Bus bus{ context, native };
        ~Environment()
        {
            bus.close();
            context.poll();
        }
        template<typename T>
        auto start(Task<T>& task) -> void
        {
            task.getHandle().promise().executor = &context;
            task.getHandle().promise().scheduler = context.getScheduler();
            task.resume();
        }
        template<typename T>
        auto finish(Task<T>& task) -> T
        {
            const auto deadline{ std::chrono::steady_clock::now() + 3s };
            while (!task.await_ready() && std::chrono::steady_clock::now() < deadline) {
                context.poll();
                std::this_thread::yield();
            }
            if (!task.await_ready())
                throw std::runtime_error{ "Task failed to finish" };
            return task.await_resume();
        }
    };
    auto immediate(int value, std::stop_token) -> Task<int> { co_return value * 2; }
    auto sum(int count, Execution execution) -> Task<ActionCompletion<int>>
    {
        int result{};
        for (int i{ 1 }; i <= count; ++i) {
            if (execution.cancelRequested())
                co_return ActionCompletion<int>{ ActionStatus::Cancelled, result };
            result += i;
            execution.feedback(i);
        }
        co_return ActionCompletion<int>{ ActionStatus::Succeeded, result };
    }
}

TEST(CoroutinesMessagingTopics, NativeInteroperabilityIndependentQueuesAndLatest)
{
    Environment env;
    auto topic{ env.bus.topic<int>("value") };
    auto native{ env.native.topic<int>("value") };
    native.publish(1);
    auto a{ topic.subscribe() };
    auto b{ topic.subscribe() };
    auto first{ a.next() };
    env.start(first);
    env.context.poll();
    EXPECT_FALSE(first.await_ready());
    a.latest();
    EXPECT_EQ(env.finish(first), 1);
    topic.setPublishOnlyOnChange(true);
    EXPECT_FALSE(topic.publish(1));
    EXPECT_TRUE(native.publish(2));
    auto next_a{ a.next() };
    auto next_b{ b.next() };
    env.start(next_a);
    env.start(next_b);
    EXPECT_EQ(env.finish(next_a), 2);
    EXPECT_EQ(env.finish(next_b), 2);
    EXPECT_EQ(env.context.poll(), 0UZ);
}

TEST(CoroutinesMessagingTopics, CancellationClosureAndSingleReader)
{
    Environment env;
    auto topic{ env.bus.topic<int>("value") };
    auto sub{ topic.subscribe() };
    std::stop_source stop;
    auto a{ sub.next(stop.get_token()) };
    env.start(a);
    auto duplicate{ sub.next() };
    env.start(duplicate);
    EXPECT_THROW(env.finish(duplicate), std::logic_error);
    stop.request_stop();
    EXPECT_FALSE(env.finish(a));
    topic.publish(42);
    auto next{ sub.next() };
    env.start(next);
    EXPECT_EQ(env.finish(next), 42);
    auto closed{ sub.next() };
    env.start(closed);
    sub.unsubscribe();
    EXPECT_FALSE(env.finish(closed));
}

TEST(CoroutinesMessagingTopics, DestroyedReaderDoesNotConsumeMessage)
{
    Environment env;
    auto topic{ env.bus.topic<int>("value") };
    auto sub{ topic.subscribe() };
    {
        auto abandoned{ sub.next() };
        env.start(abandoned);
    }
    topic.publish(42);
    auto next{ sub.next() };
    env.start(next);
    EXPECT_EQ(env.finish(next), 42);
}

TEST(CoroutinesMessagingServices, SameContextOwnsProviderAndReturnsResponse)
{
    Environment env;
    auto service{ env.bus.service<int, int>("double") };
    auto server{ service.serve(immediate) };
    auto result{ service.request(21, 1s) };
    EXPECT_EQ(env.context.poll(), 1UZ); // Initial provider registration only; request is lazy.
    env.start(result);
    EXPECT_EQ(env.finish(result).value(), 42);
    EXPECT_EQ(env.context.poll(), 0UZ);
}

TEST(CoroutinesMessagingServices, BothNativeDirectionsAndConcurrentCalls)
{
    Environment env;
    auto service{ env.bus.service<int, int>("double") };
    auto native{ env.native.service<int, int>("double") };
    {
        auto server{ native.serve([](int value) { return value * 2; }) };
        auto request{ service.request(21, 1s) };
        env.start(request);
        server.poll();
        EXPECT_EQ(env.finish(request).value(), 42);
    }
    auto server{ service.serve(immediate) };
    auto a{ native.request(10, 1s) };
    auto b{ native.request(20, 1s) };
    env.context.poll();
    EXPECT_EQ(a.get().value(), 20);
    EXPECT_EQ(b.get().value(), 40);
}

TEST(CoroutinesMessagingServices, TimersCancellationUnavailableAndHandlerException)
{
    Environment env;
    using Error = pnm::msg::ServiceError;
    auto service{ env.bus.service<int, int>("value") };
    auto missing{ service.request(1, 1s) };
    env.start(missing);
    EXPECT_EQ(env.finish(missing).error(), Error::Unavailable);
    auto native{ env.native.service<int, int>("value") };
    std::optional<pnm::msg::Reply<int>> reply;
    auto server{ native.serveDeferred(
      [&](int, pnm::msg::Reply<int> value) { reply.emplace(std::move(value)); }) };
    auto timeout{ service.request(1, 5ms) };
    env.start(timeout);
    server.poll();
    EXPECT_EQ(env.finish(timeout).error(), Error::Timeout);
    std::stop_source stop;
    auto cancelled{ service.request(1, std::chrono::hours::max(), stop.get_token()) };
    env.start(cancelled);
    stop.request_stop();
    EXPECT_EQ(env.finish(cancelled).error(), Error::Cancelled);
    server.close();
    auto throwing{ service.serve([](int, std::stop_token) -> Task<int> {
        throw std::runtime_error{ "handler" };
        co_return 0;
    }) };
    auto failed{ service.request(1, 1s) };
    env.start(failed);
    EXPECT_EQ(env.finish(failed).error(), Error::HandlerFailed);
}

TEST(CoroutinesMessagingServices, NativeCancellationWakesSuspendedCoroutineProvider)
{
    Environment env;
    bool cleanup{};
    auto service{ env.bus.service<int, int>("value") };
    auto server{ service.serve([&](int, std::stop_token stop) -> Task<int> {
        co_await pnm::coro::sleep(env.context, 1h, stop);
        cleanup = true;
        co_return 42;
    }) };
    std::stop_source stop;
    auto call{ env.native.service<int, int>("value").request(1, 1s, stop.get_token()) };
    env.context.poll();
    EXPECT_FALSE(cleanup);
    stop.request_stop();
    env.context.poll();
    EXPECT_TRUE(cleanup);
    EXPECT_EQ(call.get().error(), pnm::msg::ServiceError::Cancelled);
}

TEST(CoroutinesMessagingActions, AcceptanceFeedbackAndAwaitedCompletion)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("sum") };
    auto server{ action.serve(sum) };
    std::vector<int> events;
    auto goal{ action.sendGoal(
      4, { 1s }, { .on_accepted{ [&] { events.push_back(0); } }, .on_feedback{ [&](int value) {
        events.push_back(value);
    } } }) };
    auto result{ goal.result() };
    env.start(result);
    auto completed{ env.finish(result) };
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->status, ActionStatus::Succeeded);
    EXPECT_EQ(completed->value, 10);
    ASSERT_GE(events.size(), 2UZ);
    EXPECT_EQ(events.front(), 0);
    EXPECT_EQ(events.back(), 4);
    auto again{ goal.result() };
    env.start(again);
    EXPECT_THROW(static_cast<void>(env.finish(again)), std::logic_error);
}

TEST(CoroutinesMessagingActions, CancellationWaitsForAsynchronousCleanup)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    bool cleaned{};
    auto server{ action.serve([&](int, Execution execution) -> Task<ActionCompletion<int>> {
        co_await pnm::coro::sleep(env.context, 1h, execution.stopToken());
        co_await pnm::coro::sleep(env.context, 5ms);
        cleaned = true;
        co_return ActionCompletion<int>{ ActionStatus::Cancelled, 42 };
    }) };
    auto goal{ action.sendGoal(1, { 1s }) };
    std::stop_source stop;
    auto result{ goal.result(stop.get_token()) };
    env.start(result);
    env.context.poll();
    stop.request_stop();
    env.context.poll();
    EXPECT_FALSE(cleaned);
    EXPECT_FALSE(result.await_ready());
    auto value{ env.finish(result) };
    EXPECT_TRUE(cleaned);
    EXPECT_EQ(value->status, ActionStatus::Cancelled);
    EXPECT_EQ(value->value, 42);
}

TEST(CoroutinesMessagingActions, RejectionTimeoutAndNativeProvider)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    {
        auto server{ action.serve(
          [](int, Execution) -> std::expected<Task<ActionCompletion<int>>, pnm::msg::ActionError> {
            return std::unexpected{ pnm::msg::ActionError::Rejected };
        }) };
        auto goal{ action.sendGoal(1, { 1s }) };
        auto result{ goal.result() };
        env.start(result);
        EXPECT_EQ(env.finish(result).error(), pnm::msg::ActionError::Rejected);
    }
    auto native{ env.native.action<int, int, int>("work") };
    auto server{ native.serve([](int goal) {
        return [goal](pnm::msg::ActionExecution<int, int>& execution) { execution.succeed(goal); };
    }) };
    auto timed{ action.sendGoal(1, { 5ms }) };
    auto timeout{ timed.result() };
    env.start(timeout);
    EXPECT_EQ(env.finish(timeout).error(), pnm::msg::ActionError::Timeout);
    auto goal{ action.sendGoal(42, { 1s }) };
    auto result{ goal.result() };
    env.start(result);
    server.poll();
    EXPECT_EQ(env.finish(result)->value, 42);
}

TEST(CoroutinesMessagingShutdown, BusAndProviderJoinDrainBeforeContextStop)
{
    Environment env;
    auto service{ env.bus.service<int, int>("work") };
    bool cleaned{};
    auto server{ service.serve([&](int, std::stop_token stop) -> Task<int> {
        co_await pnm::coro::sleep(env.context, 1h, stop);
        cleaned = true;
        co_return 42;
    }) };
    auto request{ service.request(1, 1h) };
    env.start(request);
    env.context.poll();
    auto premature{ env.bus.join() };
    env.start(premature);
    EXPECT_THROW(env.finish(premature), std::logic_error);
    env.bus.requestStop();
    auto a{ env.bus.join() };
    auto b{ env.bus.join() };
    auto provider{ server.join() };
    env.start(a);
    env.start(b);
    env.start(provider);
    env.finish(a);
    env.finish(b);
    env.finish(provider);
    EXPECT_TRUE(cleaned);
    EXPECT_FALSE(env.finish(request));
    EXPECT_THROW(env.bus.topic<int>("late"), std::logic_error);
    env.context.stop();
}

TEST(CoroutinesMessagingTopics, CrossThreadWakeupAndConsumerExecutorAffinity)
{
    pnm::msg::Bus native;
    pnm::coro::Context adapter_context;
    pnm::coro::Context consumer_context;
    pnm::coro::Bus bus{ adapter_context, native };
    auto sub{ bus.topic<int>("value").subscribe() };
    std::thread::id adapter_thread;
    std::thread::id resumed_thread;
    std::latch waiting{ 1 };
    int received{};
    pnm::coro::co_spawn(consumer_context, [&](pnm::coro::Context&) -> Task<void> {
        waiting.count_down();
        received = (co_await sub.next()).value();
        resumed_thread = std::this_thread::get_id();
        consumer_context.stop();
    });
    std::jthread adapter{ [&] {
        adapter_thread = std::this_thread::get_id();
        adapter_context.run();
    } };
    std::jthread consumer{ [&] { consumer_context.run(); } };
    waiting.wait();
    native.topic<int>("value").publish(42);
    consumer.join();
    bus.close();
    adapter_context.stop();
    adapter.join();
    EXPECT_EQ(received, 42);
    EXPECT_NE(resumed_thread, adapter_thread);
    EXPECT_NE(resumed_thread, std::this_thread::get_id());
}

TEST(CoroutinesMessagingTopics, PublicationsRaceReaderRegistrationWithoutLoss)
{
    Environment env;
    auto topic{ env.native.topic<int>("value") };
    auto sub{ env.bus.topic<int>("value").subscribe() };
    constexpr int count{ 200 };
    std::jthread publisher{ [&] {
        for (int i{}; i < count; ++i)
            topic.publish(i);
    } };
    for (int i{}; i < count; ++i) {
        auto next{ sub.next() };
        env.start(next);
        EXPECT_EQ(env.finish(next), i);
    }
    publisher.join();
}

TEST(CoroutinesMessagingServices, DestroyedClientCancelsAndProviderRetainsMoveOnlyCallable)
{
    Environment env;
    std::weak_ptr<int> lifetime;
    bool cleaned{};
    auto service{ env.bus.service<int, int>("work") };
    {
        auto capture{ std::make_shared<int>(42) };
        lifetime = capture;
        auto server{ service.serve([owned{ std::make_unique<int>(1) }, capture, &env, &cleaned](
                                     int, std::stop_token stop) -> Task<int> {
            co_await pnm::coro::sleep(env.context, 1h, stop);
            co_await pnm::coro::sleep(env.context, 2ms);
            cleaned = true;
            co_return *capture + *owned;
        }) };
        capture.reset();
        {
            auto request{ service.request(1, 1s) };
            env.start(request);
            env.context.poll();
        }
    }
    EXPECT_FALSE(lifetime.expired());
    env.bus.requestStop();
    auto joined{ env.bus.join() };
    env.start(joined);
    env.finish(joined);
    EXPECT_TRUE(cleaned);
    EXPECT_TRUE(lifetime.expired());
}

TEST(CoroutinesMessagingServices, CapacityTimeoutValidationAndLazyPayloadOwnership)
{
    Environment env;
    auto service{ env.bus.service<int, int>("work") };
    pnm::coro::Channel<bool> release;
    auto server{ service.serve([&](int value, std::stop_token stop) -> Task<int> {
        co_await release.next(stop);
        co_return value;
    }, pnm::msg::ServiceOptions{ 1 }) };
    int input{ 42 };
    auto first{ service.request(input, 1s) };
    input = 9;
    env.start(first);
    env.context.poll();
    auto second{ service.request(2, 1s) };
    env.start(second);
    EXPECT_EQ(env.finish(second).error(), pnm::msg::ServiceError::Busy);
    release.close();
    EXPECT_EQ(env.finish(first).value(), 42);
    auto zero{ service.request(1, 0ms) };
    env.start(zero);
    EXPECT_EQ(env.finish(zero).error(), pnm::msg::ServiceError::Timeout);
    auto nan{ service.request(1, std::chrono::duration<double>{ std::numeric_limits<double>::quiet_NaN() }) };
    env.start(nan);
    EXPECT_THROW(static_cast<void>(env.finish(nan)), std::invalid_argument);
}

TEST(CoroutinesMessagingServices, BoundedDispatchDoesNotStarveTimers)
{
    Environment env;
    int admitted{};
    auto service{ env.bus.service<int, int>("work") };
    auto server{ service.serve([&](int value, std::stop_token) {
        ++admitted;
        return immediate(value, {});
    }, pnm::msg::ServiceOptions{ 200 }) };
    std::vector<pnm::msg::PendingCall<int>> calls;
    for (int i{}; i < 130; ++i)
        calls.push_back(env.native.service<int, int>("work").request(i, 5s));
    env.context.poll(1);
    EXPECT_EQ(admitted, 64);
    int seen_at_timer{};
    auto timer{ env.context.scheduleAt(std::chrono::steady_clock::now(), [&] { seen_at_timer = admitted; }) };
    env.context.poll(1);
    EXPECT_EQ(seen_at_timer, 64);
    while (env.context.poll() != 0) {
    }
    EXPECT_EQ(admitted, 130);
    for (int i{}; i < 130; ++i)
        EXPECT_EQ(calls[static_cast<size_t>(i)].get().value(), i * 2);
}

TEST(CoroutinesMessagingActions, NativeClientAndIndependentCoroutineGoals)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("sum") };
    auto server{ action.serve(sum) };
    std::optional<pnm::msg::ActionResult<int>> received;
    auto native{ env.native.action<int, int, int>("sum").sendGoal(
      4, { 1s }, { .on_result{ [&](pnm::msg::ActionResult<int> result) {
        received.emplace(std::move(result));
    } } }) };
    auto goal{ action.sendGoal(3, { 1s }) };
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result)->value, 6);
    EXPECT_TRUE(native.poll());
    EXPECT_EQ(received->value().value, 10);
    EXPECT_NE(native.id(), goal.id());
}

TEST(CoroutinesMessagingActions, DroppingHandleSuppressesProgressAndCancelsLazyResult)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    auto server{ action.serve(sum) };
    int callbacks{};
    Task<pnm::msg::ActionResult<int>> result;
    {
        auto goal{ action.sendGoal(
          5, { 1s }, { .on_accepted{ [&] { ++callbacks; } }, .on_feedback{ [&](int) { ++callbacks; } } }) };
        result = goal.result();
    }
    env.start(result);
    EXPECT_EQ(env.finish(result)->status, ActionStatus::Cancelled);
    EXPECT_EQ(callbacks, 0);
}

TEST(CoroutinesMessagingActions, ProgressAndHandlerExceptionsDoNotEscapeTheContext)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    {
        auto server{ action.serve(sum) };
        auto goal{ action.sendGoal(
          4, { 1s }, { .on_feedback{ [](int) { throw std::runtime_error{ "progress" }; } } }) };
        auto result{ goal.result() };
        env.start(result);
        EXPECT_THROW(static_cast<void>(env.finish(result)), std::runtime_error);
    }
    auto server{ action.serve([](int, Execution) -> Task<ActionCompletion<int>> {
        throw std::runtime_error{ "handler" };
        co_return ActionCompletion<int>{ ActionStatus::Succeeded, 0 };
    }) };
    auto goal{ action.sendGoal(4, { 1s }) };
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result).error(), pnm::msg::ActionError::HandlerFailed);
}

TEST(CoroutinesMessagingActions, AdmissionOnlyDeadlineAndCapacityUntilCleanup)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    pnm::coro::Channel<bool> release;
    auto server{ action.serve([&](int, Execution execution) -> Task<ActionCompletion<int>> {
        co_await release.next();
        co_return ActionCompletion<int>{ execution.cancelRequested() ? ActionStatus::Cancelled
                                                                     : ActionStatus::Succeeded,
                                         42 };
    }, pnm::msg::ActionOptions{ 1 }) };
    auto goal{ action.sendGoal(1, { 100ms }) };
    env.context.poll();
    std::this_thread::sleep_for(110ms);
    EXPECT_FALSE(goal.ready());
    goal.requestCancel();
    auto second{ action.sendGoal(2, { 1s }) };
    auto busy{ second.result() };
    env.start(busy);
    EXPECT_EQ(env.finish(busy).error(), pnm::msg::ActionError::Busy);
    release.close();
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result)->status, ActionStatus::Cancelled);
}

TEST(CoroutinesMessagingShutdown, ClosingProviderStillWaitsForItsCoroutineCleanup)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    bool cleanup{};
    auto server{ action.serve([&](int, Execution execution) -> Task<ActionCompletion<int>> {
        co_await pnm::coro::sleep(env.context, 1h, execution.stopToken());
        co_await pnm::coro::sleep(env.context, 2ms);
        cleanup = true;
        co_return ActionCompletion<int>{ ActionStatus::Cancelled, 0 };
    }) };
    auto goal{ action.sendGoal(1, { 1s }) };
    env.context.poll();
    server.close();
    auto joined{ server.join() };
    env.start(joined);
    env.finish(joined);
    EXPECT_TRUE(cleanup);
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result).error(), pnm::msg::ActionError::Unavailable);
}

TEST(CoroutinesMessagingShutdown, StoppedContextRejectsNewWorkWithoutStrandingClients)
{
    Environment env;
    auto topic{ env.bus.topic<int>("value") };
    auto subscription{ topic.subscribe() };
    auto service{ env.bus.service<int, int>("value") };
    auto provider{ env.native.service<int, int>("value").serve([](int value) { return value; }) };
    env.context.poll();
    env.context.stop();
    auto read{ subscription.next() };
    env.start(read);
    EXPECT_THROW(static_cast<void>(env.finish(read)), std::runtime_error);
    auto call{ service.request(1, 1s) };
    env.start(call);
    EXPECT_THROW(static_cast<void>(env.finish(call)), std::runtime_error);
}

TEST(CoroutinesMessagingPayloads, MoveOnlyValuesWorkForEveryPattern)
{
    using Value = coroutines_messaging_tests::MoveOnly;
    Environment env;
    auto topic{ env.bus.topic<Value>("value") };
    auto sub{ topic.subscribe() };
    topic.publish(Value{ 42 });
    auto next{ sub.next() };
    env.start(next);
    EXPECT_EQ(env.finish(next)->value, 42);
    auto service{ env.bus.service<Value, Value>("work") };
    auto server{ service.serve([](Value value, std::stop_token) -> Task<Value> { co_return value; }) };
    auto call{ service.request(Value{ 42 }, 1s) };
    env.start(call);
    EXPECT_EQ(env.finish(call)->value, 42);
    auto action{ env.bus.action<Value, Value, Value>("work") };
    auto worker{ action.serve(
      [](Value value, pnm::coro::ActionExecution<Value, Value> execution) -> Task<ActionCompletion<Value>> {
        execution.feedback(value);
        co_return ActionCompletion<Value>{ ActionStatus::Succeeded, std::move(value) };
    }) };
    int feedback{};
    auto goal{ action.sendGoal(
      Value{ 42 }, { 1s }, { .on_feedback{ [&](const Value& value) { feedback = value.value; } } }) };
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result)->value.value, 42);
    EXPECT_EQ(feedback, 42);
}

TEST(CoroutinesMessagingPayloads, AdapterOwnershipAndFailures)
{
    using Value = coroutines_messaging_tests::Payload;
    Environment env;
    auto topic{ env.bus.topic<Value>("value") };
    auto sub{ topic.subscribe() };
    Value value{ { std::byte{ 42 } } };
    topic.publish(value);
    value.bytes.clear();
    auto next{ sub.next() };
    env.start(next);
    EXPECT_EQ(env.finish(next)->bytes.size(), 1UZ);
    topic.publish(Value{ { std::byte{ 0xfe } } });
    auto invalid{ sub.next() };
    env.start(invalid);
    EXPECT_THROW(static_cast<void>(env.finish(invalid)), std::runtime_error);
    auto service{ env.bus.service<Value, Value>("work") };
    auto provider{ service.serve([](Value input, std::stop_token) -> Task<Value> { co_return input; }) };
    auto encode{ service.request(Value{ { std::byte{ 0xff } } }, 1s) };
    env.start(encode);
    EXPECT_THROW(static_cast<void>(env.finish(encode)), std::runtime_error);
    auto decode{ service.request(Value{ { std::byte{ 0xfe } } }, 1s) };
    env.start(decode);
    EXPECT_EQ(env.finish(decode).error(), pnm::msg::ServiceError::HandlerFailed);
    auto action{ env.bus.action<Value, Value, Value>("work") };
    auto worker{ action.serve(
      [](Value input, pnm::coro::ActionExecution<Value, Value>) -> Task<ActionCompletion<Value>> {
        co_return ActionCompletion<Value>{ ActionStatus::Succeeded, std::move(input) };
    }) };
    auto goal{ action.sendGoal(Value{ { std::byte{ 0xfe } } }, { 1s }) };
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result).error(), pnm::msg::ActionError::HandlerFailed);
}

TEST(CoroutinesMessagingRegistration, EmptyHandlersDuplicatesAndZeroCapacityAreRejected)
{
    Environment env;
    auto service{ env.bus.service<int, int>("work") };
    std::function<Task<int>(int, std::stop_token)> empty_service;
    EXPECT_THROW(static_cast<void>(service.serve(empty_service)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(service.serve(immediate, { 0 })), std::invalid_argument);
    auto server{ service.serve(immediate) };
    EXPECT_THROW(static_cast<void>(service.serve(immediate)), std::logic_error);
    auto action{ env.bus.action<int, int, int>("work") };
    std::function<Task<ActionCompletion<int>>(int, Execution)> empty_action;
    EXPECT_THROW(static_cast<void>(action.serve(empty_action)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(action.serve(sum, { 0 })), std::invalid_argument);
    auto worker{ action.serve(sum) };
    EXPECT_THROW(static_cast<void>(action.serve(sum)), std::logic_error);
}

TEST(CoroutinesMessagingRemote, ExistingDeferredServiceBridgeWakesCoroutineClient)
{
    Environment local;
    Environment remote;
    auto remote_service{ remote.bus.service<int, int>("double") };
    auto provider{ remote_service.serve(immediate) };
    auto bridge{ local.native.service<int, int>("double").serveDeferred(
      [&](int value, pnm::msg::Reply<int> reply) {
        pnm::coro::co_spawn(
          remote.context,
          [value, remote_service, reply{ std::move(reply) }](pnm::coro::Context&) mutable -> Task<void> {
            auto result{ co_await remote_service.request(value, reply.remainingTime()) };
            if (result)
                reply.respond(*result);
            else
                reply.fail(result.error());
        });
    }) };
    auto request{ local.bus.service<int, int>("double").request(21, 1s) };
    local.start(request);
    bridge.poll(); // The transport owner drives its native proxy.
    remote.context.poll();
    EXPECT_EQ(local.finish(request).value(), 42);
}

TEST(CoroutinesMessagingRemote, DeferredActionBridgePreservesAcceptanceFeedbackAndCancellation)
{
    Environment local;
    Environment remote;
    auto remote_action{ remote.bus.action<int, int, int>("work") };
    auto provider{ remote_action.serve([&](int, Execution execution) -> Task<ActionCompletion<int>> {
        execution.feedback(7);
        co_await pnm::coro::sleep(remote.context, 1h, execution.stopToken());
        co_return ActionCompletion<int>{ ActionStatus::Cancelled, 42 };
    }) };
    std::shared_ptr<pnm::msg::ActionExecution<int, int>> execution;
    std::stop_source remote_cancel;
    auto bridge{ local.native.action<int, int, int>("work").serveDeferred(
      [&](int value, pnm::msg::ActionExecution<int, int> incoming) {
        execution = std::make_shared<pnm::msg::ActionExecution<int, int>>(std::move(incoming));
        auto forwarded{ remote_action.sendGoal(
          value,
          { execution->remainingAcceptanceTime() },
          { .on_accepted{ [execution] { execution->accept(); } },
            .on_feedback{ [execution](int progress) { execution->feedback(progress); } } }) };
        pnm::coro::co_spawn(remote.context,
                            [execution, goal{ std::move(forwarded) }, stop{ remote_cancel.get_token() }](
                              pnm::coro::Context&) mutable -> Task<void> {
            auto result{ co_await goal.result(stop) };
            if (!result)
                execution->fail(result.error());
            else if (result->status == ActionStatus::Cancelled)
                execution->cancelled(result->value);
            else if (result->status == ActionStatus::Aborted)
                execution->abort(result->value);
            else
                execution->succeed(result->value);
        });
    }) };
    int feedback{};
    auto goal{ local.bus.action<int, int, int>("work").sendGoal(
      1, { 1s }, { .on_feedback{ [&](int value) { feedback = value; } } }) };
    auto result{ goal.result() };
    local.start(result);
    bridge.poll();
    remote.context.poll();
    local.context.poll();
    EXPECT_EQ(feedback, 7);
    EXPECT_FALSE(result.await_ready());
    goal.requestCancel();
    ASSERT_TRUE(execution->cancelRequested());
    remote_cancel.request_stop(); // Represents forwarding the cancel frame to the peer.
    remote.context.poll();
    auto completed{ local.finish(result) };
    EXPECT_EQ(completed->status, ActionStatus::Cancelled);
    EXPECT_EQ(completed->value, 42);
}

TEST(CoroutinesMessagingTopics, ReaderDestructionRacesCancellationSafely)
{
    Environment env;
    auto topic{ env.bus.topic<int>("value") };
    auto subscription{ topic.subscribe() };
    for (int i{}; i < 32; ++i) {
        std::stop_source stop;
        auto read{ subscription.next(stop.get_token()) };
        env.start(read);
        std::latch start{ 1 };
        std::jthread canceller{ [&] {
            start.wait();
            stop.request_stop();
        } };
        start.count_down();
        read = {};
        canceller.join();
        env.context.poll();
        topic.publish(i);
        auto next{ subscription.next() };
        env.start(next);
        EXPECT_EQ(env.finish(next), i);
    }
}

TEST(CoroutinesMessagingShutdown, SubscriptionRegistrationRacesShutdown)
{
    pnm::msg::Bus native;
    pnm::coro::Context context;
    pnm::coro::Bus bus{ context, native };
    auto topic{ bus.topic<int>("value") };
    std::latch started{ 1 };
    std::jthread executor{ [&] { context.run(); } };
    std::jthread creator{ [&] {
        auto first{ topic.subscribe() };
        started.count_down();
        for (int i{}; i < 100; ++i) {
            try {
                auto subscription{ topic.subscribe() };
            } catch (const std::logic_error&) {
                break;
            }
        }
    } };
    started.wait();
    bus.requestStop();
    creator.join();
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> Task<void> {
        co_await bus.join();
        context.stop();
    });
    executor.join();
}

TEST(CoroutinesMessagingTopics, ConcurrentPublishersPreserveOrderAcrossCoroutineSubscribers)
{
    Environment env;
    auto topic{ env.native.topic<int>("value") };
    auto first{ env.bus.topic<int>("value").subscribe() };
    auto second{ env.bus.topic<int>("value").subscribe() };
    constexpr int count{ 200 };
    const auto collect{ [](pnm::coro::Subscription<int>& subscription) -> Task<std::vector<int>> {
        std::vector<int> values;
        for (int i{}; i < count; ++i)
            values.push_back((co_await subscription.next()).value());
        co_return values;
    } };
    auto a{ collect(first) };
    auto b{ collect(second) };
    env.start(a);
    env.start(b);
    std::jthread left{ [&] {
        for (int i{}; i < count / 2; ++i)
            topic.publish(i);
    } };
    std::jthread right{ [&] {
        for (int i{ count / 2 }; i < count; ++i)
            topic.publish(i);
    } };
    auto received{ env.finish(a) };
    EXPECT_EQ(env.finish(b), received);
    left.join();
    right.join();
    std::ranges::sort(received);
    for (int i{}; i < count; ++i)
        EXPECT_EQ(received[static_cast<size_t>(i)], i);
}

TEST(CoroutinesMessagingShutdown, ReaderDestructionRacesStoppedContextCancellation)
{
    for (int iteration{}; iteration < 16; ++iteration) {
        Environment env;
        auto subscription{ env.bus.topic<int>("value").subscribe() };
        std::stop_source stop;
        auto read{ subscription.next(stop.get_token()) };
        env.start(read);
        env.context.poll();
        env.context.stop();
        std::latch start{ 1 };
        std::jthread canceller{ [&] {
            start.wait();
            stop.request_stop();
        } };
        start.count_down();
        read = {};
        canceller.join();
    }
}

TEST(CoroutinesMessagingRegistration, StoppedContextRejectsSynchronousRegistrations)
{
    Environment env;
    auto service{ env.bus.service<int, int>("work") };
    auto action{ env.bus.action<int, int, int>("work") };
    auto topic{ env.bus.topic<int>("value") };
    env.context.stop();
    EXPECT_THROW(static_cast<void>(service.serve(immediate)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(action.serve(sum)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(topic.subscribe()), std::runtime_error);
}

TEST(CoroutinesMessagingActions, ClosingDuringResultTransferPreservesTheClaimedOutcome)
{
    using Value = coroutines_messaging_tests::HookedMove;
    Environment env;
    auto action{ env.bus.action<int, int, Value>("work") };
    auto provider{ env.native.action<int, int, Value>("work").serve([](int value) {
        return
          [value](pnm::msg::ActionExecution<int, Value>& execution) { execution.succeed(Value{ value }); };
    }) };
    auto goal{ action.sendGoal(42, { 1s }) };
    provider.poll();
    bool closed{};
    Value::on_move = [&] {
        if (!closed && goal.ready()) {
            closed = true;
            env.bus.close();
        }
    };
    auto result{ goal.result() };
    env.start(result);
    env.context.poll();
    Value::on_move = {};
    ASSERT_TRUE(closed);
    ASSERT_TRUE(result.await_ready());
    auto outcome{ result.await_resume() };
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->status, ActionStatus::Succeeded);
    EXPECT_EQ(outcome->value.value, 42);
}

TEST(CoroutinesMessagingServices, CancelledHandlersHoldCapacityUntilCleanupFinishes)
{
    Environment env;
    auto service{ env.bus.service<int, int>("work") };
    pnm::coro::Channel<bool> cleanup;
    int started{};
    auto server{ service.serve([&](int value, std::stop_token stop) -> Task<int> {
        ++started;
        co_await pnm::coro::sleep(env.context, 1h, stop);
        co_await cleanup.next();
        co_return value;
    }, pnm::msg::ServiceOptions{ 1 }) };
    auto client{ env.native.service<int, int>("work") };
    auto first{ client.request(1, 1s) };
    env.context.poll();
    EXPECT_EQ(started, 1);
    first.cancel();
    env.context.poll();
    auto second{ client.request(2, 1s) };
    env.context.poll();
    EXPECT_TRUE(second.ready());
    if (second.ready()) {
        EXPECT_EQ(second.get().error(), pnm::msg::ServiceError::Busy);
    }
    EXPECT_EQ(started, 1);
    second.cancel();
    cleanup.close();
    env.context.poll();
    auto third{ client.request(3, 1s) };
    env.context.poll();
    EXPECT_EQ(started, 2);
    third.cancel();
    env.context.poll();
    server.requestStop();
    auto joined{ server.join() };
    env.start(joined);
    env.finish(joined);
}

TEST(CoroutinesMessagingTopics, ReadRegistrationRacesUnsubscribeWithoutStrandingReader)
{
    for (int iteration{}; iteration < 500; ++iteration) {
        Environment env;
        auto topic{ env.bus.topic<int>("value") };
        auto subscription{ topic.subscribe() };
        env.context.poll();
        auto read{ subscription.next() };
        std::latch start{ 1 };
        std::jthread close{ [&] {
            start.wait();
            subscription.unsubscribe();
            env.context.poll();
        } };
        start.count_down();
        read.resume(); // Unbound: registration can race the adapter's context dispatch.
        close.join();
        env.context.poll();
        ASSERT_TRUE(read.await_ready()) << iteration;
        EXPECT_FALSE(read.await_resume());
    }
}

TEST(CoroutinesMessagingActions, FailedFeedbackHoldsCapacityUntilTheHandlerExits)
{
    using Feedback = coroutines_messaging_tests::Payload;
    Environment env;
    pnm::coro::Channel<bool> cleanup;
    int started{};
    auto action{ env.bus.action<int, Feedback, int>("work") };
    auto server{ action.serve(
      [&](int value, pnm::coro::ActionExecution<Feedback, int> execution) -> Task<ActionCompletion<int>> {
        ++started;
        if (value == 1) {
            EXPECT_FALSE(execution.feedback(Feedback{ { std::byte{ 0xff } } }));
        }
        co_await cleanup.next();
        co_return ActionCompletion<int>{ ActionStatus::Succeeded, value };
    }, pnm::msg::ActionOptions{ 1 }) };
    auto first{ action.sendGoal(1, { 1s }) };
    auto failed{ first.result() };
    env.start(failed);
    EXPECT_EQ(env.finish(failed).error(), pnm::msg::ActionError::HandlerFailed);
    auto second{ action.sendGoal(2, { 1s }) };
    auto busy{ second.result() };
    env.start(busy);
    EXPECT_EQ(env.finish(busy).error(), pnm::msg::ActionError::Busy);
    EXPECT_EQ(started, 1);
    cleanup.close();
    env.context.poll();
    auto third{ action.sendGoal(3, { 1s }) };
    auto result{ third.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result)->value, 3);
    EXPECT_EQ(started, 2);
}

TEST(CoroutinesMessagingServices, EmptyTasksAndThrowingFactoriesReleaseTheirSlots)
{
    Environment env;
    auto service{ env.bus.service<int, int>("work") };
    auto server{ service.serve([](int value, std::stop_token stop) -> Task<int> {
        if (value == 0)
            return {};
        if (value == 1)
            throw std::runtime_error{ "factory" };
        return immediate(value, stop);
    }, pnm::msg::ServiceOptions{ 1 }) };
    for (int value{ 0 }; value < 3; ++value) {
        auto call{ service.request(value, 1s) };
        env.start(call);
        auto result{ env.finish(call) };
        if (value < 2)
            EXPECT_EQ(result.error(), pnm::msg::ServiceError::HandlerFailed);
        else
            EXPECT_EQ(result.value(), 4);
    }
}

TEST(CoroutinesMessagingActions, InvalidTasksStatusesAndFactoriesReleaseTheirSlots)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    auto invalid{ [](int, Execution) -> Task<ActionCompletion<int>> {
        // Deliberately exercise defensive validation of an invalid provider status.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        co_return ActionCompletion<int>{ static_cast<ActionStatus>(255), 0 };
    } };
    auto server{ action.serve([&](int value, Execution execution) -> Task<ActionCompletion<int>> {
        if (value == 0)
            return {};
        if (value == 1)
            throw std::runtime_error{ "factory" };
        if (value == 2)
            return invalid(value, std::move(execution));
        return sum(value, std::move(execution));
    }, pnm::msg::ActionOptions{ 1 }) };
    for (int value{ 0 }; value < 4; ++value) {
        auto goal{ action.sendGoal(value, { 1s }) };
        auto result{ goal.result() };
        env.start(result);
        auto outcome{ env.finish(result) };
        if (value < 3)
            EXPECT_EQ(outcome.error(), pnm::msg::ActionError::HandlerFailed);
        else
            EXPECT_EQ(outcome->value, 6);
    }
}

TEST(CoroutinesMessagingActions, DestroyedResultWaiterCanBeReplacedWithoutLosingCompletion)
{
    Environment env;
    auto action{ env.bus.action<int, int, int>("work") };
    std::optional<pnm::msg::ActionExecution<int, int>> execution;
    auto server{ env.native.action<int, int, int>("work").serveDeferred(
      [&](int, pnm::msg::ActionExecution<int, int> incoming) {
        incoming.accept();
        execution.emplace(std::move(incoming));
    }) };
    auto goal{ action.sendGoal(42, { 1s }) };
    auto first{ goal.result() };
    env.start(first);
    server.poll();
    env.context.poll();
    auto duplicate{ goal.result() };
    env.start(duplicate);
    EXPECT_THROW(static_cast<void>(env.finish(duplicate)), std::logic_error);
    first = {};
    ASSERT_TRUE(execution);
    EXPECT_TRUE(execution->cancelRequested());
    EXPECT_TRUE(execution->succeed(42));
    auto second{ goal.result() };
    env.start(second);
    EXPECT_EQ(env.finish(second)->value, 42);
}

TEST(CoroutinesMessagingShutdown, MultipleJoinsWaitForCleanupAndRejectUnrequestedShutdown)
{
    Environment env;
    pnm::coro::Channel<bool> cleanup;
    auto action{ env.bus.action<int, int, int>("work") };
    auto server{ action.serve([&](int, Execution) -> Task<ActionCompletion<int>> {
        co_await cleanup.next();
        co_return ActionCompletion<int>{ ActionStatus::Cancelled, 0 };
    }) };
    auto goal{ action.sendGoal(1, { 1s }) };
    env.context.poll();
    auto early_bus{ env.bus.join() };
    auto early_server{ server.join() };
    env.start(early_bus);
    env.start(early_server);
    EXPECT_THROW(env.finish(early_bus), std::logic_error);
    EXPECT_THROW(env.finish(early_server), std::logic_error);
    env.bus.requestStop();
    auto first{ env.bus.join() };
    auto second{ env.bus.join() };
    auto provider{ server.join() };
    env.start(first);
    env.start(second);
    env.start(provider);
    EXPECT_FALSE(first.await_ready());
    EXPECT_FALSE(second.await_ready());
    EXPECT_FALSE(provider.await_ready());
    EXPECT_THROW(static_cast<void>(env.bus.topic<int>("closed")), std::logic_error);
    cleanup.close();
    env.finish(first);
    env.finish(second);
    env.finish(provider);
    auto finished{ env.bus.join() };
    env.start(finished);
    env.finish(finished);
}

TEST(CoroutinesMessagingActions, EncodingCanReleaseTheSubmittingEndpoint)
{
    using Value = coroutines_messaging_tests::HookedMove;
    Environment env;
    auto action{ std::make_unique<pnm::coro::Action<Value, int, int>>(
      env.bus.action<Value, int, int>("work")) };
    auto provider{ env.native.action<Value, int, int>("work").serve([](const Value& value) {
        return [number{ value.value }](pnm::msg::ActionExecution<int, int>& execution) {
            execution.succeed(number);
        };
    }) };
    pnm::utils::memory::SerializationAdapter<Value>::on_serialize = [&] { action.reset(); };
    auto goal{ action->sendGoal(Value{ 42 }, { 1s }) };
    EXPECT_FALSE(action);
    provider.poll();
    auto result{ goal.result() };
    env.start(result);
    EXPECT_EQ(env.finish(result)->value, 42);
}

TEST(CoroutinesMessagingActions, ThrowingResultTransferFailsAwaiterWithoutStrandingShutdown)
{
    using Value = coroutines_messaging_tests::HookedMove;
    static_assert(!std::is_nothrow_move_constructible_v<ActionCompletion<Value>>);
    Environment env;
    auto provider{ env.native.action<int, int, Value>("work").serve([](int value) {
        return
          [value](pnm::msg::ActionExecution<int, Value>& execution) { execution.succeed(Value{ value }); };
    }) };
    auto goal{ env.bus.action<int, int, Value>("work").sendGoal(42, { 1s }) };
    provider.poll();
    Value::on_move = [&] {
        if (goal.ready())
            throw std::runtime_error{ "result move" };
    };
    auto result{ goal.result() };
    env.start(result);
    env.context.poll();
    Value::on_move = {};
    ASSERT_TRUE(result.await_ready());
    EXPECT_THROW(static_cast<void>(result.await_resume()), std::runtime_error);
    env.bus.requestStop();
    auto joined{ env.bus.join() };
    env.start(joined);
    env.finish(joined);
}

// NOLINTEND(modernize-use-designated-initializers)
