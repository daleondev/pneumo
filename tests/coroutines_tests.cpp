#include "pneumo/coroutines.hpp"

// NOLINTBEGIN

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    static_assert(pnm::coro::Executor<pnm::coro::Context>);
    static_assert(!pnm::coro::Executor<int>);
    static_assert(!std::copy_constructible<pnm::coro::Task<int>>);
    static_assert(std::is_nothrow_move_constructible_v<pnm::coro::Task<int>>);

    auto answer() -> pnm::coro::Task<int> { co_return 42; }

    auto nested_answer() -> pnm::coro::Task<int> { co_return (co_await answer()) + 1; }

    auto fail() -> pnm::coro::Task<int>
    {
        throw std::runtime_error{ "task failure" };
        co_return 0;
    }

    auto hold(std::shared_ptr<int> value) -> pnm::coro::Task<void>
    {
        static_cast<void>(value);
        co_return;
    }

    auto run_on_context(auto func) -> void
    {
        pnm::coro::Context context;
        std::exception_ptr error;
        pnm::coro::co_spawn(context, [&](pnm::coro::Context& executor) -> pnm::coro::Task<void> {
            try {
                co_await func();
            } catch (...) {
                error = std::current_exception();
            }
            executor.stop();
        });
        context.run();
        if (error) {
            std::rethrow_exception(error);
        }
    }

    auto read_raw(pnm::coro::RawBinaryChannel& channel)
      -> pnm::coro::Task<std::optional<pnm::coro::RawBinaryChannel::Bytes>>
    {
        std::optional<pnm::coro::RawBinaryChannel::Bytes> result;
        co_await channel.next(result);
        co_return result;
    }

    template<typename T>
    auto encode(const T& value) -> pnm::coro::RawBinaryChannel::Bytes
    {
        pnm::coro::RawBinaryChannel::Bytes bytes(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
        return bytes;
    }
}

TEST(CoroutinesTaskTests, TasksStartLazilyAndReturnNestedValues)
{
    auto task{ nested_answer() };
    EXPECT_FALSE(task.getHandle().done());
    task.getHandle().resume();
    ASSERT_TRUE(task.getHandle().done());
    EXPECT_EQ(task.await_resume(), 43);
}

TEST(CoroutinesTaskTests, VoidTasksAndMoveOnlyResultsCanBeAwaited)
{
    auto value_task = []() -> pnm::coro::Task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(42);
    };
    int result{};
    run_on_context([&]() -> pnm::coro::Task<void> {
        auto value{ co_await value_task() };
        result = *value;
    });
    EXPECT_EQ(result, 42);
}

TEST(CoroutinesTaskTests, ExceptionsPropagateThroughAwait)
{
    EXPECT_THROW(run_on_context([]() -> pnm::coro::Task<void> { co_await fail(); }), std::runtime_error);
}

TEST(CoroutinesTaskTests, MovingTasksTransfersFrameOwnership)
{
    auto first_value{ std::make_shared<int>(1) };
    auto second_value{ std::make_shared<int>(2) };
    {
        auto first{ hold(first_value) };
        auto second{ hold(second_value) };
        EXPECT_EQ(first_value.use_count(), 2);
        EXPECT_EQ(second_value.use_count(), 2);
        auto moved{ std::move(first) };
        EXPECT_FALSE(first.getHandle());
        second = std::move(moved);
        EXPECT_FALSE(moved.getHandle());
        EXPECT_EQ(second_value.use_count(), 1);
        second.getHandle().resume();
        EXPECT_TRUE(second.getHandle().done());
    }
    EXPECT_EQ(first_value.use_count(), 1);
    EXPECT_EQ(second_value.use_count(), 1);
}

TEST(CoroutinesContextTests, SpawnSchedulesMoveOnlyCallablesAndDrainsReadyQueueOnStop)
{
    pnm::coro::Context context;
    std::vector<int> order;
    pnm::coro::co_spawn(context,
                        [value = std::make_unique<int>(1), &order](pnm::coro::Context& executor)
                          -> pnm::coro::Task<void> {
                            order.push_back(*value);
                            executor.stop();
                            co_return;
                        });
    pnm::coro::co_spawn(context, [&order](pnm::coro::Context&) -> pnm::coro::Task<void> {
        order.push_back(2);
        co_return;
    });
    EXPECT_TRUE(order.empty());
    context.run();
    EXPECT_EQ(order, (std::vector<int>{ 1, 2 }));
}

TEST(CoroutinesContextTests, StopWakesIdleRunner)
{
    pnm::coro::Context context;
    std::jthread runner{ [&] { context.run(); } };
    context.stop();
    runner.join();
}

TEST(CoroutinesContextTests, EmptyScheduledHandlesAreIgnored)
{
    pnm::coro::Context context;
    context.schedule({});
    context.stop();
    EXPECT_NO_THROW(context.run());
}

TEST(CoroutinesContextTests, LifeTokenExpiresWithContext)
{
    std::weak_ptr<void> token;
    {
        pnm::coro::Context context;
        token = context.getLifeToken();
        EXPECT_FALSE(token.expired());
    }
    EXPECT_TRUE(token.expired());
}

TEST(CoroutinesAsyncTests, WorkRunsOnAnotherThreadAndReturnsAValue)
{
    const auto caller{ std::this_thread::get_id() };
    std::thread::id worker;
    run_on_context([&]() -> pnm::coro::Task<void> {
        worker = co_await pnm::coro::runAsync<std::thread::id>([] { return std::this_thread::get_id(); });
    });
    EXPECT_NE(worker, caller);
    EXPECT_NE(worker, std::thread::id{});
}

TEST(CoroutinesAsyncTests, VoidWorkCompletesBeforeTheAwaiterContinues)
{
    int value{};
    run_on_context([&]() -> pnm::coro::Task<void> {
        co_await pnm::coro::runAsync<void>([&] { value = 7; });
        value *= 2;
    });
    EXPECT_EQ(value, 14);
}

TEST(CoroutinesAsyncTests, CallableIsOwnedUntilLazyTaskStarts)
{
    auto task{ pnm::coro::runAsync<int>([value = std::make_unique<int>(42)] { return *value; }) };
    int result{};
    run_on_context([&]() -> pnm::coro::Task<void> { result = co_await std::move(task); });
    EXPECT_EQ(result, 42);
}

TEST(CoroutinesAsyncTests, WorkerExceptionsPropagateToTheAwaiter)
{
    EXPECT_THROW(run_on_context([]() -> pnm::coro::Task<void> {
        co_await pnm::coro::runAsync<void>([] { throw std::runtime_error{ "worker failure" }; });
    }), std::runtime_error);
}

TEST(CoroutinesSleepTests, NonPositiveDurationsCompleteWithoutSuspending)
{
    for (auto duration : { 0ms, -1ms }) {
        auto task{ pnm::coro::sleep(duration) };
        task.getHandle().resume();
        EXPECT_TRUE(task.getHandle().done());
        EXPECT_NO_THROW(task.await_resume());
    }
}

TEST(CoroutinesSleepTests, PositiveDurationDelaysContinuation)
{
    const auto start{ std::chrono::steady_clock::now() };
    run_on_context([]() -> pnm::coro::Task<void> { co_await pnm::coro::sleep(5ms); });
    EXPECT_GE(std::chrono::steady_clock::now() - start, 5ms);
}

TEST(CoroutinesChannelTests, BufferedValuesDrainInOrderAfterClose)
{
    pnm::coro::Channel<std::string> channel;
    channel.push("first");
    channel.push("second");
    channel.close();
    channel.close();
    channel.push("ignored");
    for (const auto* expected : { "first", "second" }) {
        auto task{ channel.next() };
        task.getHandle().resume();
        ASSERT_TRUE(task.getHandle().done());
        EXPECT_EQ(task.await_resume(), expected);
    }
    auto closed{ channel.next() };
    closed.getHandle().resume();
    EXPECT_FALSE(closed.await_resume());
}

TEST(CoroutinesChannelTests, PushDeliversToOneWaitingConsumerAtATime)
{
    pnm::coro::Channel<std::unique_ptr<int>> channel;
    auto first{ channel.next() };
    auto second{ channel.next() };
    first.getHandle().resume();
    second.getHandle().resume();
    channel.push(std::make_unique<int>(11));
    EXPECT_TRUE(first.getHandle().done());
    EXPECT_FALSE(second.getHandle().done());
    auto value{ first.await_resume() };
    ASSERT_TRUE(value);
    EXPECT_EQ(**value, 11);
    channel.close();
    ASSERT_TRUE(second.getHandle().done());
    EXPECT_FALSE(second.await_resume());
}

TEST(CoroutinesChannelTests, DestroyingASuspendedConsumerUnregistersIt)
{
    pnm::coro::Channel<int> channel;
    {
        auto abandoned{ channel.next() };
        abandoned.getHandle().resume();
        ASSERT_FALSE(abandoned.getHandle().done());
    }
    channel.push(42);
    auto next{ channel.next() };
    next.getHandle().resume();
    ASSERT_TRUE(next.getHandle().done());
    EXPECT_EQ(next.await_resume(), 42);
}

TEST(CoroutinesBinaryChannelTests, BroadcastDeliversToEveryWaitingConsumer)
{
    pnm::coro::RawBinaryChannel raw;
    pnm::coro::BinaryChannel<std::uint32_t> first_view{ raw };
    pnm::coro::BinaryChannel<std::uint32_t> second_view{ raw };
    auto first{ first_view.next() };
    auto second{ second_view.next() };
    first.getHandle().resume();
    second.getHandle().resume();
    raw.push(encode(std::uint32_t{ 42 }));
    ASSERT_TRUE(first.getHandle().done());
    ASSERT_TRUE(second.getHandle().done());
    EXPECT_EQ(first.await_resume(), 42);
    EXPECT_EQ(second.await_resume(), 42);
}

TEST(CoroutinesBinaryChannelTests, LoadBalancerDeliversEachMessageToOneWaiter)
{
    pnm::coro::RawBinaryChannel raw;
    raw.setMode(pnm::coro::ChannelMode::LoadBalancer);
    auto first{ read_raw(raw) };
    auto second{ read_raw(raw) };
    first.getHandle().resume();
    second.getHandle().resume();
    const auto bytes{ encode(std::uint32_t{ 1 }) };
    raw.push(bytes);
    ASSERT_TRUE(first.getHandle().done());
    EXPECT_FALSE(second.getHandle().done());
    EXPECT_EQ(first.await_resume(), bytes);
    raw.close();
    ASSERT_TRUE(second.getHandle().done());
    EXPECT_FALSE(second.await_resume());
}

TEST(CoroutinesBinaryChannelTests, UnboundViewReturnsNoValue)
{
    pnm::coro::BinaryChannel<std::uint32_t> view;
    auto task{ view.next() };
    task.getHandle().resume();
    ASSERT_TRUE(task.getHandle().done());
    EXPECT_FALSE(task.await_resume());
}

TEST(CoroutinesBinaryChannelTests, ClosedChannelClearsAReusedDestination)
{
    pnm::coro::RawBinaryChannel raw;
    std::optional<pnm::coro::RawBinaryChannel::Bytes> result{ encode(std::uint32_t{ 42 }) };
    raw.close();
    auto awaiter{ raw.next(result) };
    ASSERT_TRUE(awaiter.await_ready());
    awaiter.await_resume();
    EXPECT_FALSE(result);
}

TEST(CoroutinesBinaryChannelTests, InvalidPayloadSizeReturnsNoValue)
{
    pnm::coro::RawBinaryChannel raw;
    pnm::coro::BinaryChannel<std::uint32_t> view{ raw };
    raw.push({ std::byte{ 1 } });
    auto task{ view.next() };
    task.getHandle().resume();
    ASSERT_TRUE(task.getHandle().done());
    EXPECT_FALSE(task.await_resume());
}

TEST(CoroutinesBinaryChannelTests, DestroyingASuspendedConsumerUnregistersIt)
{
    pnm::coro::RawBinaryChannel raw;
    {
        auto abandoned{ read_raw(raw) };
        abandoned.getHandle().resume();
    }
    const auto bytes{ encode(std::uint32_t{ 42 }) };
    raw.push(bytes);
    auto next{ read_raw(raw) };
    next.getHandle().resume();
    ASSERT_TRUE(next.getHandle().done());
    EXPECT_EQ(next.await_resume(), bytes);
}

TEST(CoroutinesBinaryChannelTests, PushBetweenReadyAndSuspendDeliversTheQueuedValue)
{
    pnm::coro::RawBinaryChannel raw;
    std::optional<pnm::coro::RawBinaryChannel::Bytes> result;
    auto awaiter{ raw.next(result) };
    ASSERT_FALSE(awaiter.await_ready());
    const auto bytes{ encode(std::uint32_t{ 42 }) };
    raw.push(bytes);
    EXPECT_FALSE(awaiter.await_suspend(std::noop_coroutine()));
    awaiter.await_resume();
    EXPECT_EQ(result, bytes);
}

// NOLINTEND
