#include "pneumo/coroutines.hpp"

// NOLINTBEGIN

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <latch>
#include <limits>
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
    task.resume();
    ASSERT_TRUE(task.getHandle().done());
    EXPECT_EQ(task.await_resume(), 43);
}

TEST(CoroutinesTaskTests, VoidTasksAndMoveOnlyResultsCanBeAwaited)
{
    auto value_task = []() -> pnm::coro::Task<std::unique_ptr<int>> { co_return std::make_unique<int>(42); };
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
        second.resume();
        EXPECT_TRUE(second.getHandle().done());
    }
    EXPECT_EQ(first_value.use_count(), 1);
    EXPECT_EQ(second_value.use_count(), 1);
}

TEST(CoroutinesContextTests, SpawnSchedulesMoveOnlyCallablesAndDrainsReadyQueueOnStop)
{
    pnm::coro::Context context;
    std::vector<int> order;
    pnm::coro::co_spawn(
      context,
      [value = std::make_unique<int>(1), &order](pnm::coro::Context& executor) -> pnm::coro::Task<void> {
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
    }),
                 std::runtime_error);
}

TEST(CoroutinesSleepTests, NonPositiveDurationsCompleteWithoutSuspending)
{
    for (auto duration : { 0ms, -1ms }) {
        auto task{ pnm::coro::sleep(duration) };
        task.resume();
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
        task.resume();
        ASSERT_TRUE(task.getHandle().done());
        EXPECT_EQ(task.await_resume(), expected);
    }
    auto closed{ channel.next() };
    closed.resume();
    EXPECT_FALSE(closed.await_resume());
}

TEST(CoroutinesChannelTests, PushDeliversToOneWaitingConsumerAtATime)
{
    pnm::coro::Channel<std::unique_ptr<int>> channel;
    auto first{ channel.next() };
    auto second{ channel.next() };
    first.resume();
    second.resume();
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
        abandoned.resume();
        ASSERT_FALSE(abandoned.getHandle().done());
    }
    channel.push(42);
    auto next{ channel.next() };
    next.resume();
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
    first.resume();
    second.resume();
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
    first.resume();
    second.resume();
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
    task.resume();
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
    task.resume();
    ASSERT_TRUE(task.getHandle().done());
    EXPECT_FALSE(task.await_resume());
}

TEST(CoroutinesBinaryChannelTests, DestroyingASuspendedConsumerUnregistersIt)
{
    pnm::coro::RawBinaryChannel raw;
    {
        auto abandoned{ read_raw(raw) };
        abandoned.resume();
    }
    const auto bytes{ encode(std::uint32_t{ 42 }) };
    raw.push(bytes);
    auto next{ read_raw(raw) };
    next.resume();
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

TEST(CoroutinesTaskTests, InvalidOperationsThrowInsteadOfAccessingInvalidFrames)
{
    pnm::coro::Task<int> empty;
    EXPECT_THROW(empty.resume(), std::logic_error);
    EXPECT_THROW(empty.await_resume(), std::logic_error);
    auto task{ answer() };
    EXPECT_THROW(task.await_resume(), std::logic_error);
    task.resume();
    EXPECT_THROW(task.resume(), std::logic_error);
    EXPECT_EQ(task.await_resume(), 42);
    EXPECT_THROW(task.await_resume(), std::logic_error);
    auto moved{ std::move(task) };
    EXPECT_THROW(task.await_resume(), std::logic_error);
    EXPECT_THROW(run_on_context([&]() -> pnm::coro::Task<void> { co_await empty; }), std::logic_error);
}

TEST(CoroutinesTaskTests, RejectingASecondWaiterPreservesTheFirst)
{
    pnm::coro::Channel<int> channel;
    auto child{ channel.next() };
    int result{};
    auto consume = [&]() -> pnm::coro::Task<void> { result = (co_await child).value(); };
    auto first{ consume() };
    auto second{ consume() };
    first.resume();
    second.resume();
    ASSERT_TRUE(second.await_ready());
    EXPECT_THROW(second.await_resume(), std::logic_error);
    channel.push(42);
    ASSERT_TRUE(first.await_ready());
    EXPECT_NO_THROW(first.await_resume());
    EXPECT_EQ(result, 42);
}

TEST(CoroutinesTaskTests, MovingAnAwaitedTaskPreservesTheContinuation)
{
    pnm::coro::Channel<int> channel;
    auto child{ channel.next() };
    int result{};
    auto consume = [&]() -> pnm::coro::Task<void> { result = (co_await child).value(); };
    auto parent{ consume() };
    parent.resume();
    auto moved{ std::move(child) };
    channel.push(42);
    ASSERT_TRUE(parent.await_ready());
    EXPECT_NO_THROW(parent.await_resume());
    EXPECT_EQ(result, 42);
}

TEST(CoroutinesTaskTests, DestroyingAParentDisconnectsItsBorrowedChild)
{
    pnm::coro::Channel<int> channel;
    auto child{ channel.next() };
    bool resumed{};
    auto consume = [&]() -> pnm::coro::Task<void> {
        co_await child;
        resumed = true;
    };
    {
        auto parent{ consume() };
        parent.resume();
    }
    channel.push(42);
    EXPECT_FALSE(resumed);
    ASSERT_TRUE(child.await_ready());
    EXPECT_EQ(child.await_resume(), 42);
}

TEST(CoroutinesContextTests, AbandonedAndRejectedSpawnsReleaseTheirCaptures)
{
    auto token{ std::make_shared<int>(42) };
    {
        pnm::coro::Context context;
        pnm::coro::co_spawn(context, [token](pnm::coro::Context&) -> pnm::coro::Task<void> {
            static_cast<void>(token);
            co_return;
        });
        EXPECT_EQ(token.use_count(), 2);
    }
    EXPECT_EQ(token.use_count(), 1);
    pnm::coro::Context stopped;
    stopped.stop();
    EXPECT_THROW(pnm::coro::co_spawn(stopped,
                                     [token](pnm::coro::Context&) -> pnm::coro::Task<void> {
        static_cast<void>(token);
        co_return;
    }),
                 std::runtime_error);
    EXPECT_EQ(token.use_count(), 1);
}

TEST(CoroutinesContextTests, DetachedFramesAreOwnedUntilScheduled)
{
    auto token{ std::make_shared<int>(42) };
    auto make = [](std::shared_ptr<int> value) -> pnm::coro::DetachedTask {
        static_cast<void>(value);
        co_return;
    };
    {
        auto first{ make(token) };
        auto second{ make(token) };
        EXPECT_EQ(token.use_count(), 3);
        first = std::move(second);
        EXPECT_FALSE(second.getHandle());
        EXPECT_EQ(token.use_count(), 2);
    }
    EXPECT_EQ(token.use_count(), 1);
}

TEST(CoroutinesContextTests, SchedulerSafelyRejectsWorkAfterContextDestruction)
{
    pnm::coro::Scheduler scheduler;
    {
        pnm::coro::Context context;
        scheduler = context.getScheduler();
    }
    auto token{ std::make_shared<int>(42) };
    auto make = [](std::shared_ptr<int> value) -> pnm::coro::DetachedTask {
        static_cast<void>(value);
        co_return;
    };
    EXPECT_THROW(scheduler(make(token)), std::runtime_error);
    EXPECT_EQ(token.use_count(), 1);
}

TEST(CoroutinesContextTests, CancelledQueuedDeliveriesDoNotResumeDestroyedTasks)
{
    pnm::coro::Context context;
    pnm::coro::Channel<int> channel;
    bool resumed{};
    auto consume = [&]() -> pnm::coro::Task<void> {
        co_await channel.next();
        resumed = true;
    };
    {
        auto task{ consume() };
        task.getHandle().promise().scheduler = context.getScheduler();
        task.resume();
        channel.push(42);
        EXPECT_FALSE(task.await_ready());
    }
    context.stop();
    context.run();
    EXPECT_FALSE(resumed);
}

TEST(CoroutinesContextTests, SchedulingFailurePropagatesToTheAwaitingTask)
{
    pnm::coro::Context context;
    pnm::coro::Channel<int> channel;
    auto task{ channel.next() };
    task.getHandle().promise().scheduler = context.getScheduler();
    task.resume();
    context.stop();
    channel.push(42);
    ASSERT_TRUE(task.await_ready());
    EXPECT_THROW(task.await_resume(), std::runtime_error);
}

TEST(CoroutinesAsyncTests, NestedWorkAndTimersResumeOnTheContextThread)
{
    auto caller{ std::this_thread::get_id() };
    auto nested = [&]() -> pnm::coro::Task<void> {
        auto worker{ co_await pnm::coro::runAsync<std::thread::id>(
          [] { return std::this_thread::get_id(); }) };
        EXPECT_NE(worker, caller);
        EXPECT_EQ(std::this_thread::get_id(), caller);
        co_await pnm::coro::sleep(1ms);
        EXPECT_EQ(std::this_thread::get_id(), caller);
    };
    run_on_context([&]() -> pnm::coro::Task<void> {
        co_await nested();
        EXPECT_EQ(std::this_thread::get_id(), caller);
    });
}

TEST(CoroutinesAsyncTests, DestroyingASuspendedTaskDoesNotDestroyItsRunningCallable)
{
    std::latch entered{ 1 };
    std::latch release{ 1 };
    std::promise<void> destroyed;
    auto finished{ destroyed.get_future() };
    std::atomic<bool> resumed{};
    auto work = [token = std::shared_ptr<int>{ new int{ 42 },
                                               [&](int* value) {
        delete value;
        destroyed.set_value();
    } },
                 &entered,
                 &release] {
        entered.count_down();
        release.wait();
        return *token;
    };
    auto consume = [&](auto function) -> pnm::coro::Task<void> {
        co_await pnm::coro::runAsync<int>(std::move(function));
        resumed = true;
    };
    auto task{ consume(std::move(work)) };
    task.resume();
    entered.wait();
    task = {};
    EXPECT_EQ(finished.wait_for(0ms), std::future_status::timeout);
    release.count_down();
    ASSERT_EQ(finished.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(resumed);
}

TEST(CoroutinesAsyncTests, DestructionWaitsForAnExecutingContinuation)
{
    pnm::coro::Channel<int> channel;
    std::latch resumed{ 1 };
    std::latch release{ 1 };
    std::latch destroying{ 1 };
    std::atomic<bool> destroyed{};
    auto consume = [&]() -> pnm::coro::Task<void> {
        co_await channel.next();
        resumed.count_down();
        release.wait();
    };
    auto task{ consume() };
    task.resume();
    std::jthread producer{ [&] { channel.push(42); } };
    resumed.wait();
    std::jthread destroyer{ [task = std::move(task), &destroying, &destroyed]() mutable {
        destroying.count_down();
        task = {};
        destroyed = true;
    } };
    destroying.wait();
    EXPECT_FALSE(destroyed);
    release.count_down();
    producer.join();
    destroyer.join();
    EXPECT_TRUE(destroyed);
}

TEST(CoroutinesChannelTests, LazyReadsKeepStateAfterWrappersAreMovedOrDestroyed)
{
    pnm::coro::Channel<int> channel;
    auto read{ channel.next() };
    {
        auto moved{ std::move(channel) };
        moved.push(42);
    }
    read.resume();
    EXPECT_EQ(read.await_resume(), 42);
    auto moved_from{ channel.next() };
    moved_from.resume();
    EXPECT_FALSE(moved_from.await_resume());
    EXPECT_THROW(channel.push(0), std::logic_error);
}

TEST(CoroutinesChannelTests, ValuesNeedNotBeMoveAssignable)
{
    struct Value
    {
        explicit Value(int value)
          : number{ value }
        {
        }
        Value(Value&&) = default;
        auto operator=(Value&&) -> Value& = delete;
        int number;
    };
    pnm::coro::Channel<Value> channel;
    channel.push(Value{ 11 });
    auto buffered{ channel.next() };
    buffered.resume();
    auto first{ buffered.await_resume() };
    ASSERT_TRUE(first);
    EXPECT_EQ(first->number, 11);
    auto waiting{ channel.next() };
    waiting.resume();
    channel.push(Value{ 42 });
    auto second{ waiting.await_resume() };
    ASSERT_TRUE(second);
    EXPECT_EQ(second->number, 42);
}

TEST(CoroutinesChannelTests, AClosingConsumerCanCancelAnotherConsumer)
{
    pnm::coro::Channel<int> channel;
    auto second{ channel.next() };
    auto cancel = [&]() -> pnm::coro::Task<void> {
        EXPECT_FALSE(co_await channel.next());
        second = {};
    };
    auto first{ cancel() };
    first.resume();
    second.resume();
    channel.close();
    ASSERT_TRUE(first.await_ready());
    EXPECT_NO_THROW(first.await_resume());
    EXPECT_FALSE(second.getHandle());
}

TEST(CoroutinesChannelTests, DeliveriesFromOtherThreadsReturnToTheContext)
{
    pnm::coro::Context context;
    pnm::coro::Channel<int> channel;
    std::latch started{ 1 };
    std::thread::id resumed;
    auto caller{ std::this_thread::get_id() };
    pnm::coro::co_spawn(context, [&](pnm::coro::Context& executor) -> pnm::coro::Task<void> {
        EXPECT_EQ(co_await channel.next(), 42);
        resumed = std::this_thread::get_id();
        executor.stop();
    });
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> pnm::coro::Task<void> {
        started.count_down();
        co_return;
    });
    std::jthread sender{ [&] {
        started.wait();
        channel.push(42);
    } };
    context.run();
    sender.join();
    EXPECT_EQ(resumed, caller);
}

TEST(CoroutinesBinaryChannelTests, LazyReadsKeepStateAfterTheViewIsDestroyed)
{
    pnm::coro::RawBinaryChannel raw;
    auto read = [&] {
        pnm::coro::BinaryChannel<std::uint32_t> view{ raw };
        return view.next();
    }();
    raw.push(encode(std::uint32_t{ 42 }));
    read.resume();
    EXPECT_EQ(read.await_resume(), 42);
}

TEST(CoroutinesBinaryChannelTests, DecodedValuesNeedNotBeDefaultConstructible)
{
    struct Value
    {
        Value() = delete;
        explicit Value(std::uint32_t value)
          : number{ value }
        {
        }
        std::uint32_t number;
    };
    static_assert(std::is_trivially_copyable_v<Value>);
    pnm::coro::RawBinaryChannel raw;
    pnm::coro::BinaryChannel<Value> view{ raw };
    raw.push(encode(Value{ 42 }));
    auto read{ view.next() };
    read.resume();
    auto value{ read.await_resume() };
    ASSERT_TRUE(value);
    EXPECT_EQ(value->number, 42);
}

TEST(CoroutinesBinaryChannelTests, ABroadcastConsumerCanCancelAnotherConsumer)
{
    pnm::coro::RawBinaryChannel raw;
    auto second{ read_raw(raw) };
    auto cancel = [&]() -> pnm::coro::Task<void> {
        EXPECT_TRUE(co_await read_raw(raw));
        second = {};
    };
    auto first{ cancel() };
    first.resume();
    second.resume();
    raw.push(encode(std::uint32_t{ 42 }));
    ASSERT_TRUE(first.await_ready());
    EXPECT_NO_THROW(first.await_resume());
    EXPECT_FALSE(second.getHandle());
}

TEST(CoroutinesContextTests, PollIsNonblockingAndHonorsItsWorkLimit)
{
    pnm::coro::Context context;
    int calls{};
    EXPECT_EQ(context.poll(), 0);
    for (int index{}; index < 3; ++index) {
        pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> pnm::coro::Task<void> {
            ++calls;
            co_return;
        });
    }
    EXPECT_EQ(context.poll(2), 2);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(context.poll(), 1);
    EXPECT_EQ(calls, 3);
}

TEST(CoroutinesContextTests, TimerCancellationReleasesQueuedCallback)
{
    pnm::coro::Context context;
    auto token = std::make_shared<int>(42);
    bool called{};
    {
        auto timer =
          context.scheduleAt(std::chrono::steady_clock::now(), [token, &called] { called = true; });
        EXPECT_EQ(token.use_count(), 2);
    }
    EXPECT_EQ(token.use_count(), 1);
    EXPECT_EQ(context.poll(), 0);
    EXPECT_FALSE(called);
    context.stop();
    EXPECT_THROW(static_cast<void>(context.scheduleAt(std::chrono::steady_clock::now(), [] {})),
                 std::runtime_error);
}

TEST(CoroutinesChannelTests, CancellationRemovesOnlyItsOwnWaiter)
{
    pnm::coro::Channel<int> channel;
    std::stop_source stop;
    auto first = channel.next(stop.get_token());
    auto second = channel.next();
    first.resume();
    second.resume();
    stop.request_stop();
    ASSERT_TRUE(first.await_ready());
    EXPECT_FALSE(first.await_resume());
    EXPECT_FALSE(second.await_ready());
    channel.push(42);
    ASSERT_TRUE(second.await_ready());
    EXPECT_EQ(second.await_resume(), 42);
    channel.push(7);
    auto canceled = channel.next(stop.get_token());
    canceled.resume();
    EXPECT_FALSE(canceled.await_resume());
    auto buffered = channel.next();
    buffered.resume();
    EXPECT_EQ(buffered.await_resume(), 7);
}

TEST(CoroutinesAsyncTests, ContextSleepUsesTimersAndCanBeInterrupted)
{
    pnm::coro::Context context;
    std::exception_ptr error;
    const auto caller = std::this_thread::get_id();
    pnm::coro::co_spawn(context, [&](pnm::coro::Context& executor) -> pnm::coro::Task<void> {
        try {
            const auto before = std::chrono::steady_clock::now();
            EXPECT_TRUE(co_await pnm::coro::sleep(executor, 1ms));
            EXPECT_GE(std::chrono::steady_clock::now(), before + 1ms);
            EXPECT_EQ(std::this_thread::get_id(), caller);
            std::stop_source stop;
            auto cancel =
              executor.scheduleAt(std::chrono::steady_clock::now() + 1ms, [&] { stop.request_stop(); });
            EXPECT_FALSE(co_await pnm::coro::sleep(executor, 1h, stop.get_token()));
            EXPECT_EQ(std::this_thread::get_id(), caller);
        } catch (...) {
            error = std::current_exception();
        }
        executor.stop();
    });
    context.run();
    EXPECT_FALSE(error);
}

TEST(CoroutinesContextTests, CancellingAndReplacingTheNextTimerWakesRunner)
{
    pnm::coro::Context context;
    auto longTimer = context.scheduleAt(std::chrono::steady_clock::now() + 1h, [] {});
    std::latch started{ 1 };
    std::promise<void> fired;
    auto finished = fired.get_future();
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> pnm::coro::Task<void> {
        started.count_down();
        co_return;
    });
    std::jthread scheduler{ [&] {
        started.wait();
        longTimer.cancel();
        auto shortTimer = context.scheduleAt(std::chrono::steady_clock::now() + 1ms, [&] {
            fired.set_value();
            context.stop();
        });
        EXPECT_EQ(finished.wait_for(2s), std::future_status::ready);
    } };
    context.run();
    scheduler.join();
}

TEST(CoroutinesContextTests, RepeatingDueTimersDoNotStarveWorkAcrossPollCalls)
{
    pnm::coro::Context context;
    int timer_calls{};
    int task_calls{};
    pnm::coro::Context::Timer timer;
    std::function<void()> repeat = [&] {
        ++timer_calls;
        timer = context.scheduleAt(std::chrono::steady_clock::now(), repeat);
    };
    timer = context.scheduleAt(std::chrono::steady_clock::now(), repeat);
    for (int index{}; index < 3; ++index) {
        pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> pnm::coro::Task<void> {
            ++task_calls;
            co_return;
        });
    }
    for (int step{}; step < 6; ++step) {
        EXPECT_EQ(context.poll(1), 1);
    }
    EXPECT_EQ(task_calls, 3);
    EXPECT_EQ(timer_calls, 3);
    timer.cancel();
    EXPECT_EQ(context.poll(), 0);
}

TEST(CoroutinesContextTests, ReadyWorkDoesNotStarveDueTimers)
{
    pnm::coro::Context context;
    bool fired{};
    int task_calls{};
    for (int index{}; index < 5; ++index) {
        pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> pnm::coro::Task<void> {
            ++task_calls;
            co_return;
        });
    }
    auto timer{ context.scheduleAt(std::chrono::steady_clock::now(), [&] { fired = true; }) };
    EXPECT_EQ(context.poll(2), 2);
    EXPECT_TRUE(fired);
    EXPECT_EQ(task_calls, 1);
    context.stop();
    context.run();
    EXPECT_EQ(task_calls, 5);
}

TEST(CoroutinesContextTests, FairDispatchPreservesQueueAndDeadlineOrderWhenStopped)
{
    pnm::coro::Context context;
    std::vector<int> timer_order;
    std::vector<int> task_order;
    const auto now{ std::chrono::steady_clock::now() };
    auto later{ context.scheduleAt(now, [&] { timer_order.push_back(2); }) };
    auto earlier{ context.scheduleAt(now - 1ms, [&] { timer_order.push_back(1); }) };
    auto same_deadline{ context.scheduleAt(now, [&] { timer_order.push_back(3); }) };
    auto future{ context.scheduleAt(now + 1h, [&] { timer_order.push_back(4); }) };
    for (int index{ 1 }; index <= 3; ++index) {
        pnm::coro::co_spawn(context, [&, index](pnm::coro::Context&) -> pnm::coro::Task<void> {
            task_order.push_back(index);
            co_return;
        });
    }
    EXPECT_EQ(context.poll(0), 0);
    EXPECT_TRUE(task_order.empty());
    EXPECT_TRUE(timer_order.empty());
    context.stop();
    context.run();
    EXPECT_EQ(timer_order, (std::vector<int>{ 1, 2, 3 }));
    EXPECT_EQ(task_order, (std::vector<int>{ 1, 2, 3 }));
}

TEST(CoroutinesSleepTests, OversizedContextSleepsStayPendingAndCanBeCancelled)
{
    auto check = [](auto duration) {
        pnm::coro::Context context;
        std::stop_source stop;
        auto task{ pnm::coro::sleep(context, duration, stop.get_token()) };
        task.resume();
        EXPECT_FALSE(task.await_ready());
        EXPECT_EQ(context.poll(), 0);
        EXPECT_FALSE(task.await_ready());
        stop.request_stop();
        ASSERT_TRUE(task.await_ready());
        EXPECT_FALSE(task.await_resume());
        EXPECT_EQ(context.poll(), 0);
    };
    check(std::chrono::steady_clock::duration::max());
    check(std::chrono::hours::max());
    check(std::chrono::duration<std::uint64_t>::max());
    check(std::chrono::duration<double>::max());
    check(std::chrono::duration<double>{ std::numeric_limits<double>::infinity() });
    // A naive integral duration_cast overflows count * 2 even though the final ticks fit.
    check(std::chrono::duration<std::int64_t, std::ratio<2, 3'000'000'000>>::max());
}

TEST(CoroutinesSleepTests, DeadlineConversionSaturatesAtTheClockBoundary)
{
    using Clock = std::chrono::steady_clock;
    const auto near_limit{ Clock::time_point::max() - Clock::duration{ 10 } };
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(Clock::duration{ 9 }, near_limit),
              Clock::time_point::max() - Clock::duration{ 1 });
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(Clock::duration{ 10 }, near_limit), Clock::time_point::max());
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(Clock::duration{ 11 }, near_limit), Clock::time_point::max());
    const Clock::time_point epoch;
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(1500ms, epoch), epoch + 1500ms);
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(std::chrono::duration<double>{ 1.25 }, epoch),
              epoch + 1250ms);
}

TEST(CoroutinesSleepTests, ContextSleepPreservesImmediateCompletionAndCancellationPriority)
{
    pnm::coro::Context context;
    std::stop_source stop;
    stop.request_stop();
    for (auto duration : { 0ms, -1ms }) {
        auto completed{ pnm::coro::sleep(context, duration) };
        completed.resume();
        ASSERT_TRUE(completed.await_ready());
        EXPECT_TRUE(completed.await_resume());
        auto canceled{ pnm::coro::sleep(context, duration, stop.get_token()) };
        canceled.resume();
        ASSERT_TRUE(canceled.await_ready());
        EXPECT_FALSE(canceled.await_resume());
    }
    EXPECT_EQ(context.poll(), 0);
}

TEST(CoroutinesSleepTests, InvalidFloatingDurationPropagatesAnException)
{
    pnm::coro::Context context;
    auto task{ pnm::coro::sleep(context,
                                std::chrono::duration<double>{ std::numeric_limits<double>::quiet_NaN() }) };
    task.resume();
    ASSERT_TRUE(task.await_ready());
    EXPECT_THROW(task.await_resume(), std::invalid_argument);
    EXPECT_EQ(context.poll(), 0);
}

TEST(CoroutinesSleepTests, SaturatedDeadlineCanBeCancelledWhileContextRuns)
{
    pnm::coro::Context context;
    std::stop_source stop;
    bool elapsed{ true };
    auto wait = [&]() -> pnm::coro::Task<void> {
        elapsed =
          co_await pnm::coro::sleep(context, std::chrono::steady_clock::duration::max(), stop.get_token());
        context.stop();
    };
    auto task{ wait() };
    task.resume();
    ASSERT_FALSE(task.await_ready());
    std::jthread cancel{ [&] {
        std::this_thread::sleep_for(1ms);
        stop.request_stop();
    } };
    context.run();
    cancel.join();
    ASSERT_TRUE(task.await_ready());
    EXPECT_NO_THROW(task.await_resume());
    EXPECT_FALSE(elapsed);
}

TEST(CoroutinesContextTests, InitialOwnedExecutionSerializesAccessToBorrowedChild)
{
    pnm::coro::Context context;
    std::latch entered{ 1 };
    std::latch release{ 1 };
    auto body{ [&]() -> pnm::coro::Task<void> {
        entered.count_down();
        release.wait();
        co_return;
    } };
    auto child{ body() };
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) -> pnm::coro::Task<void> { co_await child; });
    std::jthread runner{ [&] { context.poll(); } };
    entered.wait();
    std::promise<void> checking;
    auto observer{ std::async(std::launch::async, [&] {
        checking.set_value();
        return child.await_ready();
    }) };
    checking.get_future().wait();
    EXPECT_EQ(observer.wait_for(10ms), std::future_status::timeout);
    release.count_down();
    runner.join();
    EXPECT_TRUE(observer.get());
}

TEST(CoroutinesChannelTests, CancellationRacesDeliveryWithoutLosingTheQueuedValue)
{
    for (int iteration{}; iteration < 200; ++iteration) {
        pnm::coro::Channel<int> channel;
        std::stop_source stop;
        auto read{ channel.next(stop.get_token()) };
        read.resume();
        std::latch start{ 1 };
        std::jthread producer{ [&] {
            start.wait();
            channel.push(42);
        } };
        std::jthread canceller{ [&] {
            start.wait();
            stop.request_stop();
        } };
        start.count_down();
        producer.join();
        canceller.join();
        ASSERT_TRUE(read.await_ready());
        auto result{ read.await_resume() };
        if (!result) {
            auto remaining{ channel.next() };
            remaining.resume();
            ASSERT_TRUE(remaining.await_ready());
            result = remaining.await_resume();
        }
        EXPECT_EQ(result, 42);
        channel.close();
        auto end{ channel.next() };
        end.resume();
        EXPECT_FALSE(end.await_resume());
    }
}

TEST(CoroutinesChannelTests, ConsumerDestructionRacesDeliveryAndClose)
{
    for (int iteration{}; iteration < 200; ++iteration) {
        pnm::coro::Channel<int> channel;
        auto read{ channel.next() };
        read.resume();
        std::latch start{ 1 };
        std::jthread producer{ [&] {
            start.wait();
            if (iteration % 2 == 0)
                channel.push(42);
            else
                channel.close();
        } };
        start.count_down();
        read = {};
        producer.join();
        channel.close();
    }
}

TEST(CoroutinesContextTests, ThrowingTimerLeavesOtherWorkDispatchable)
{
    pnm::coro::Context context;
    const auto now{ std::chrono::steady_clock::now() };
    auto failing{ context.scheduleAt(now, [] { throw std::runtime_error{ "timer" }; }) };
    bool called{};
    auto succeeding{ context.scheduleAt(now, [&] { called = true; }) };
    EXPECT_THROW(context.poll(), std::runtime_error);
    EXPECT_EQ(context.poll(), 1);
    EXPECT_TRUE(called);
    EXPECT_EQ(context.poll(), 0);
}

TEST(CoroutinesSleepTests, FractionalClockTicksRoundUp)
{
    using Clock = std::chrono::steady_clock;
    using Fractional = std::chrono::duration<double, Clock::period>;
    const Clock::time_point now{};
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(Fractional{ 0.25 }, now), now + Clock::duration{ 1 });
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(Fractional{ 1.25 }, now), now + Clock::duration{ 2 });
    EXPECT_EQ(pnm::coro::detail::sleep_deadline(Fractional{ 2.0 }, now), now + Clock::duration{ 2 });
}

TEST(CoroutinesSleepTests, ThreadSleepRejectsNaNWithoutStartingAWorker)
{
    auto task{ pnm::coro::sleep(std::chrono::duration<double>{ std::numeric_limits<double>::quiet_NaN() }) };
    task.resume();
    ASSERT_TRUE(task.await_ready());
    EXPECT_THROW(task.await_resume(), std::invalid_argument);
}

// NOLINTEND
