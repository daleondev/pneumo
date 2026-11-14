#include "pneumo/coroutines.hpp"

// NOLINTBEGIN
#include "execution_allocation_probe.hpp"
#include <barrier>
#include <gtest/gtest.h>

namespace
{
    namespace ex = pnm::coro::execution;
    using pnm::coro::Task;

    struct CallbackReceiver
    {
        using receiver_concept = ex::receiver_tag;
        std::function<void()> value;
        std::function<void(std::exception_ptr)> error;
        auto set_value() && noexcept -> void
        {
            auto callback{ std::move(value) };
            callback();
        }
        auto set_error(std::exception_ptr failure) && noexcept -> void
        {
            auto callback{ std::move(error) };
            callback(std::move(failure));
        }
        auto set_stopped() && noexcept -> void { ADD_FAILURE(); }
    };

    struct CustomExecutor : pnm::coro::IExecutor
    {
        std::shared_ptr<int> life{ std::make_shared<int>() };
        std::function<pnm::coro::Scheduler()> scheduler_factory;
        auto run() -> void override {}
        auto stop() -> void override {}
        auto schedule(std::coroutine_handle<> handle) -> void override { handle.resume(); }
        auto getLifeToken() -> std::weak_ptr<void> override { return life; }
        auto getScheduler() -> pnm::coro::Scheduler override { return scheduler_factory(); }
    };

    struct Lifetime
    {
        bool* destroyed;
        ~Lifetime() { *destroyed = true; }
    };

    auto answer() -> Task<int> { co_return 42; }
}

TEST(CoroutinesExecution, InlineCustomSchedulerSurvivesReentrantOperationDestruction)
{
    bool destroyed{};
    bool survived{};
    CustomExecutor executor;
    executor.scheduler_factory = [&] {
        return [lifetime{ std::make_shared<Lifetime>(&destroyed) }, &survived](pnm::coro::DetachedTask task) {
            // Save external addresses before completion, so the regression fails without accessing
            // a destroyed closure when the last scheduler owner is released by the receiver.
            auto* destroyed_flag{ lifetime->destroyed };
            auto* survived_flag{ &survived };
            task.release().resume();
            *survived_flag = !*destroyed_flag;
        };
    };
    using Operation =
      decltype(ex::connect(ex::schedule(executor.getExecutionScheduler()), CallbackReceiver{}));
    std::unique_ptr<Operation> operation;
    operation = std::make_unique<Operation>(
      ex::connect(ex::schedule(executor.getExecutionScheduler()),
                  CallbackReceiver{ [&] { operation.reset(); }, [](std::exception_ptr) { ADD_FAILURE(); } }));
    ex::start(*operation);
    EXPECT_TRUE(survived);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(operation);
}

TEST(CoroutinesExecution, CustomSchedulerRejectionPreservesTheOriginalException)
{
    CustomExecutor executor;
    executor.scheduler_factory = [] { return [](pnm::coro::DetachedTask) { throw 42; }; };
    int completions{};
    std::exception_ptr error;
    auto operation{ ex::connect(ex::schedule(executor.getExecutionScheduler()),
                                CallbackReceiver{ [] { ADD_FAILURE(); }, [&](std::exception_ptr failure) {
        ++completions;
        error = std::move(failure);
    } }) };
    ex::start(operation);
    ASSERT_EQ(completions, 1);
    ASSERT_TRUE(error);
    try {
        std::rethrow_exception(error);
    } catch (int value) {
        EXPECT_EQ(value, 42);
    } catch (...) {
        ADD_FAILURE() << "Custom executor rejection was replaced by an abandonment error";
    }
}

TEST(CoroutinesExecution, SenderAllocationFailuresDoNotCancelTheAwaitingTask)
{
    pnm::coro::Context context;
    int failures{};
    bool exhausted{};
    std::exception_ptr error;
    pnm::coro::co_spawn(context, [&](auto&) -> Task<void> {
        try {
            auto token{ co_await ex::read_env(ex::get_stop_token) };
            auto scheduler{ context.getExecutionScheduler() };
            for (std::size_t allocation{}; allocation < 64; ++allocation) {
                allocation_probe::failAfter(allocation);
                try {
                    co_await ex::just();
                } catch (const std::bad_alloc&) {
                    ++failures;
                }
                const bool triggered{ allocation_probe::failureConsumed() };
                // Let any deferred cleanup run before inspecting the task's persistent token.
                co_await ex::unstoppable(ex::schedule(scheduler));
                EXPECT_FALSE(token.stop_requested()) << "Allocation index " << allocation;
                if (!triggered) {
                    exhausted = true;
                    break;
                }
            }
        } catch (...) {
            static_cast<void>(allocation_probe::failureConsumed());
            error = std::current_exception();
        }
        context.stop();
    });
    context.run();
    EXPECT_GT(failures, 0);
    EXPECT_TRUE(exhausted);
    EXPECT_FALSE(error);
}

TEST(CoroutinesExecution, EmptyContextDestructionDoesNotAllocate)
{
    auto context{ std::make_unique<pnm::coro::Context>() };
    allocation_probe::failNext();
    context.reset();
    const bool allocated{ allocation_probe::failureConsumed() };
    EXPECT_FALSE(allocated);
}

TEST(CoroutinesExecution, ContextAbandonmentSurvivesAllocationFailure)
{
    auto context{ std::make_unique<pnm::coro::Context>() };
    int completions{};
    auto operation{ ex::connect(ex::schedule(context->getExecutionScheduler()), CallbackReceiver{ [] {
        ADD_FAILURE();
    }, [&](std::exception_ptr) { ++completions; } }) };
    ex::start(operation);
    allocation_probe::failNext();
    context.reset();
    static_cast<void>(allocation_probe::failureConsumed());
    EXPECT_EQ(completions, 1);
}

TEST(CoroutinesExecution, AbandonedTaskContinuationCompletesAndReleasesItsFactory)
{
    auto context{ std::make_unique<pnm::coro::Context>() };
    bool destroyed{};
    int completions{};
    auto sender{ pnm::coro::asSender(
      *context, [lifetime{ std::make_shared<Lifetime>(&destroyed) }](std::stop_token) -> Task<void> {
        co_await ex::just();
        static_cast<void>(lifetime);
    }) };
    auto operation{ ex::connect(std::move(sender),
                                CallbackReceiver{ [] { ADD_FAILURE(); }, [&](std::exception_ptr error) {
        EXPECT_TRUE(error);
        EXPECT_TRUE(destroyed);
        ++completions;
    } }) };
    ex::start(operation);
    ASSERT_EQ(context->poll(1), 1); // The task has queued its continuation after the inline sender.
    ASSERT_EQ(completions, 0);
    context.reset();
    EXPECT_EQ(completions, 1);
    EXPECT_TRUE(destroyed);
}

TEST(CoroutinesExecution, TaskLaunchAllocationFailuresCompleteOnceAndReleaseCaptures)
{
    int failures{};
    bool exhausted{};
    for (std::size_t allocation{}; allocation < 64; ++allocation) {
        pnm::coro::Context context;
        bool destroyed{};
        int completions{};
        auto sender{ pnm::coro::asSender(
          context, [lifetime{ std::make_shared<Lifetime>(&destroyed) }](std::stop_token) -> Task<void> {
            co_await ex::just();
            static_cast<void>(lifetime);
        }) };
        auto operation{ ex::connect(std::move(sender),
                                    CallbackReceiver{ [&] { ++completions; }, [&](std::exception_ptr) {
            ++completions;
            ++failures;
        } }) };
        allocation_probe::failAfter(allocation);
        ex::start(operation);
        context.poll();
        const bool triggered{ allocation_probe::failureConsumed() };
        EXPECT_EQ(completions, 1) << "Allocation index " << allocation;
        EXPECT_TRUE(destroyed) << "Allocation index " << allocation;
        if (!triggered) {
            exhausted = true;
            break;
        }
    }
    EXPECT_GT(failures, 0);
    EXPECT_TRUE(exhausted);
}

TEST(CoroutinesExecution, FailedFirstResumeLeavesTheTaskOwnedAndRetryable)
{
    auto task{ answer() };
    allocation_probe::failNext();
    bool failed{};
    try {
        task.resume();
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    const bool triggered{ allocation_probe::failureConsumed() };
    ASSERT_TRUE(triggered);
    ASSERT_TRUE(failed);
    EXPECT_FALSE(task.await_ready());
    task.resume();
    EXPECT_EQ(task.await_resume(), 42);
}

TEST(CoroutinesExecution, RawHandleStartupFailureIsStoredInTheTask)
{
    auto task{ answer() };
    allocation_probe::failNext();
    task.getHandle().resume();
    const bool triggered{ allocation_probe::failureConsumed() };
    ASSERT_TRUE(triggered);
    ASSERT_TRUE(task.await_ready());
    EXPECT_THROW(task.await_resume(), std::bad_alloc);
}

TEST(CoroutinesExecution, ConcurrentSchedulerQueriesShareOneAdapter)
{
    pnm::coro::Context context;
    std::barrier ready{ 2 };
    std::optional<pnm::coro::ExecutionScheduler> first;
    std::optional<pnm::coro::ExecutionScheduler> second;
    std::jthread a{ [&] {
        ready.arrive_and_wait();
        first = context.getExecutionScheduler();
    } };
    std::jthread b{ [&] {
        ready.arrive_and_wait();
        second = context.getExecutionScheduler();
    } };
    a.join();
    b.join();
    EXPECT_EQ(first, second);
    allocation_probe::failNext();
    auto cached{ context.getExecutionScheduler() };
    const bool allocated{ allocation_probe::failureConsumed() };
    EXPECT_FALSE(allocated);
    EXPECT_EQ(cached, *first);
}

TEST(CoroutinesExecution, ServiceAdmissionAllocationFailuresReleaseJobsAndProviderSlots)
{
    bool exhausted{};
    for (std::size_t allocation{}; allocation < 128; ++allocation) {
        std::weak_ptr<int> lifetime;
        {
            pnm::coro::Context context;
            pnm::msg::Bus native;
            pnm::coro::Bus bus{ context, native };
            auto capture{ std::make_shared<int>(42) };
            lifetime = capture;
            auto server{ bus.service<int, int>("allocation-service")
                           .serve([owned{ std::move(capture) }](int, std::stop_token) -> Task<int> {
                co_return *owned;
            }) };
            context.poll();
            auto call{ native.service<int, int>("allocation-service").request(1, std::chrono::hours{ 1 }) };
            allocation_probe::failAfter(allocation);
            try {
                context.poll(1024);
            } catch (const std::bad_alloc&) {
            }
            exhausted = !allocation_probe::failureConsumed();
            context.poll(1024);
            EXPECT_TRUE(call.ready()) << "Allocation index " << allocation;
            server.close();
            auto joined{ server.join() };
            joined.resume();
            context.poll(1024);
            EXPECT_TRUE(joined.await_ready()) << "Allocation index " << allocation;
            if (joined.await_ready()) {
                EXPECT_NO_THROW(joined.await_resume());
            }
        }
        EXPECT_TRUE(lifetime.expired()) << "Allocation index " << allocation;
        if (exhausted)
            break;
    }
    EXPECT_TRUE(exhausted);
}

TEST(CoroutinesExecution, ActionAdmissionAllocationFailuresReleaseJobsAndProviderSlots)
{
    bool exhausted{};
    for (std::size_t allocation{}; allocation < 128; ++allocation) {
        std::weak_ptr<int> lifetime;
        {
            pnm::coro::Context context;
            pnm::msg::Bus native;
            pnm::coro::Bus bus{ context, native };
            auto capture{ std::make_shared<int>(42) };
            lifetime = capture;
            auto server{ bus.action<int, int, int>("allocation-action")
                           .serve([owned{ std::move(capture) }](int, pnm::coro::ActionExecution<int, int>)
                                    -> Task<pnm::msg::ActionCompletion<int>> {
                co_return pnm::msg::ActionCompletion<int>{ pnm::msg::ActionStatus::Succeeded, *owned };
            }) };
            context.poll();
            auto goal{ native.action<int, int, int>("allocation-action")
                         .sendGoal(
                           1, { .accept_timeout = std::chrono::hours{ 1 } }, { .on_result = [](auto) {} }) };
            allocation_probe::failAfter(allocation);
            try {
                context.poll(1024);
            } catch (const std::bad_alloc&) {
            }
            exhausted = !allocation_probe::failureConsumed();
            context.poll(1024);
            EXPECT_TRUE(goal.poll()) << "Allocation index " << allocation;
            server.close();
            auto joined{ server.join() };
            joined.resume();
            context.poll(1024);
            EXPECT_TRUE(joined.await_ready()) << "Allocation index " << allocation;
            if (joined.await_ready()) {
                EXPECT_NO_THROW(joined.await_resume());
            }
        }
        EXPECT_TRUE(lifetime.expired()) << "Allocation index " << allocation;
        if (exhausted)
            break;
    }
    EXPECT_TRUE(exhausted);
}
// NOLINTEND
