#include "pneumo/coroutines.hpp"

// NOLINTBEGIN
#include "execution_allocation_probe.hpp"
#include <barrier>
#include <gtest/gtest.h>
#include <latch>
#include <set>
#include <thread>

auto execution_tu_probe() -> int;

namespace
{
    namespace ex = pnm::coro::execution;
    using namespace std::chrono_literals;
    using pnm::coro::Task;
    static_assert(ex::scheduler<pnm::coro::ExecutionScheduler>);
    static_assert(ex::scheduler<decltype(std::declval<pnm::coro::ThreadPool&>().getExecutionScheduler())>);
    static_assert(
      std::same_as<ex::completion_signatures_of_t<ex::schedule_result_t<pnm::coro::ExecutionScheduler>>,
                   ex::completion_signatures<ex::set_value_t(),
                                             ex::set_error_t(std::exception_ptr),
                                             ex::set_stopped_t()>>);

    struct Completion
    {
        int values{};
        int errors{};
        int stopped{};
        std::exception_ptr error;
        std::thread::id thread;
    };
    struct Receiver
    {
        using receiver_concept [[maybe_unused]] = ex::receiver_tag;
        Completion* output;
        std::stop_token stop;
        auto get_env() const noexcept { return ex::prop{ ex::get_stop_token, stop }; }
        template<typename... Values>
        auto set_value(Values&&...) && noexcept -> void
        {
            ++output->values;
            output->thread = std::this_thread::get_id();
        }
        auto set_error(std::exception_ptr error) && noexcept -> void
        {
            ++output->errors;
            output->error = std::move(error);
        }
        auto set_stopped() && noexcept -> void { ++output->stopped; }
    };
    struct DestructionSender
    {
        using sender_concept = ex::sender_tag;
        using completion_signatures = ex::completion_signatures<ex::set_value_t()>;
        bool* destroyed;
        template<typename R>
        struct Operation
        {
            using operation_state_concept = ex::operation_state_tag;
            R receiver;
            bool* destroyed;
            ~Operation() { *destroyed = true; }
            auto start() noexcept -> void { ex::set_value(std::move(receiver)); }
        };
        template<typename R>
        auto connect(R receiver) const -> Operation<R>
        {
            return { std::move(receiver), destroyed };
        }
    };
    auto answer(int value = 42) -> Task<int> { co_return value; }
    auto fail() -> Task<int>
    {
        throw std::runtime_error{ "failure" };
        co_return 0;
    }

    auto run(auto function) -> void
    {
        pnm::coro::Context context;
        std::exception_ptr error;
        pnm::coro::co_spawn(context, [&](auto&) -> Task<void> {
            try {
                co_await function(context);
            } catch (...) {
                error = std::current_exception();
            }
            context.stop();
        });
        context.run();
        if (error)
            std::rethrow_exception(error);
    }
}

TEST(CoroutinesExecution, SchedulerIsLazyAndUsesContextQueue)
{
    pnm::coro::Context context;
    pnm::coro::Context other;
    auto scheduler{ context.getExecutionScheduler() };
    EXPECT_EQ(scheduler, context.getExecutionScheduler());
    EXPECT_NE(scheduler, other.getExecutionScheduler());
    Completion completed;
    auto op{ ex::connect(ex::schedule(scheduler), Receiver{ &completed, {} }) };
    EXPECT_EQ(context.poll(), 0);
    std::jthread producer{ [&] { ex::start(op); } };
    producer.join();
    EXPECT_EQ(completed.values, 0);
    EXPECT_EQ(context.poll(1), 1);
    EXPECT_EQ(completed.values, 1);
    EXPECT_EQ(completed.thread, std::this_thread::get_id());
}

TEST(CoroutinesExecution, SenderWorkPreservesFifoTimerFairnessAndPollLimits)
{
    pnm::coro::Context context;
    auto scheduler{ context.getExecutionScheduler() };
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(scheduler))), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler), scheduler);
    EXPECT_EQ(ex::get_forward_progress_guarantee(scheduler), ex::forward_progress_guarantee::weakly_parallel);
    std::vector<int> order;
    Completion completed;
    auto sender{ [&](int index) {
        return ex::schedule(scheduler) | ex::then([&, index] { order.push_back(index); });
    } };
    auto first{ ex::connect(sender(1), Receiver{ &completed, {} }) };
    auto second{ ex::connect(sender(2), Receiver{ &completed, {} }) };
    ex::start(first);
    ex::start(second);
    auto timer_a{ context.scheduleAt(std::chrono::steady_clock::now(), [&] { order.push_back(0); }) };
    auto timer_b{ context.scheduleAt(std::chrono::steady_clock::now(), [&] { order.push_back(0); }) };
    EXPECT_EQ(context.poll(0), 0);
    for (std::size_t count{ 1 }; count <= 4; ++count) {
        EXPECT_EQ(context.poll(1), 1);
        EXPECT_EQ(order.size(), count);
    }
    EXPECT_EQ(order, (std::vector<int>{ 0, 1, 0, 2 }));
    EXPECT_EQ(completed.values, 2);
    EXPECT_EQ(context.poll(), 0);
}

TEST(CoroutinesExecution, SchedulerReportsStopRejectionAndAbandonmentOnce)
{
    auto context{ std::make_unique<pnm::coro::Context>() };
    auto scheduler{ context->getExecutionScheduler() };
    Completion abandoned;
    auto op{ ex::connect(ex::schedule(scheduler), Receiver{ &abandoned, {} }) };
    ex::start(op);
    context.reset();
    EXPECT_EQ(abandoned.errors, 1);
    Completion expired;
    auto late{ ex::connect(ex::schedule(scheduler), Receiver{ &expired, {} }) };
    ex::start(late);
    EXPECT_EQ(expired.errors, 1);

    pnm::coro::Context stopped_context;
    auto target{ stopped_context.getExecutionScheduler() };
    stopped_context.stop();
    Completion rejected;
    auto rejected_op{ ex::connect(ex::schedule(target), Receiver{ &rejected, {} }) };
    ex::start(rejected_op);
    EXPECT_EQ(rejected.errors, 1);
    std::stop_source source;
    source.request_stop();
    Completion cancelled;
    auto cancelled_op{ ex::connect(ex::schedule(target), Receiver{ &cancelled, source.get_token() }) };
    ex::start(cancelled_op);
    EXPECT_EQ(cancelled.stopped, 1);
    EXPECT_EQ(cancelled.errors, 0);
}

TEST(CoroutinesExecution, QueuedCancellationAndStoppedContextDrain)
{
    pnm::coro::Context context;
    std::stop_source source;
    Completion output;
    auto op{ ex::connect(ex::schedule(context.getExecutionScheduler()),
                         Receiver{ &output, source.get_token() }) };
    ex::start(op);
    source.request_stop();
    context.stop();
    context.run();
    EXPECT_EQ(output.stopped, 1);
}

TEST(CoroutinesExecution, AwaitValuesMoveOnlyTuplesAndComposition)
{
    run([](auto&) -> Task<void> {
        co_await ex::just();
        EXPECT_EQ(co_await ex::just(42), 42);
        auto pointer{ co_await ex::just(std::make_unique<int>(17)) };
        EXPECT_EQ(*pointer, 17);
        auto values{ co_await ex::just(8, std::string{ "value" }) };
        const auto& [number, text]{ values };
        EXPECT_EQ(number, 8);
        EXPECT_EQ(text, "value");
        EXPECT_EQ(co_await (ex::just(4) | ex::then([](int n) { return n * 2; }) |
                            ex::let_value([](int& n) { return ex::just(n + 1); })),
                  9);
        auto variant{ co_await ex::into_variant(ex::just(12)) };
        EXPECT_EQ(std::get<0>(std::get<0>(variant)), 12);
    });
}

TEST(CoroutinesExecution, NativeAwaitersKeepTheirFrameAndDoNotAllocateSenderState)
{
    struct Awaiter
    {
        std::coroutine_handle<>* observed;
        int* value;
        static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> frame) const noexcept
        {
            *observed = frame;
            return false;
        }
        int& await_resume() const noexcept { return *value; }
    };
    struct MemberAwaitable
    {
        Awaiter awaiter;
        Awaiter operator co_await() const noexcept { return awaiter; }
    };

    std::coroutine_handle<> direct_frame, member_frame;
    int value{ 7 };
    int* direct_result{};
    int* member_result{};
    std::size_t allocations{};
    auto body{ [&]() -> Task<void> {
        allocation_probe::begin();
        auto&& first{ co_await Awaiter{ &direct_frame, &value } };
        auto&& second{ co_await MemberAwaitable{ { &member_frame, &value } } };
        direct_result = &first;
        member_result = &second;
        allocations = allocation_probe::end();
    } };
    auto task{ body() };
    task.resume();
    ASSERT_TRUE(task.getHandle().done());
    EXPECT_EQ(direct_frame, task.getHandle());
    EXPECT_EQ(member_frame, task.getHandle());
    EXPECT_EQ(direct_result, &value);
    EXPECT_EQ(member_result, &value);
    EXPECT_EQ(allocations, 0U);
}

TEST(CoroutinesExecution, AwaitErrorsAndCatchableStop)
{
    run([](auto& context) -> Task<void> {
        try {
            co_await ex::just_error(std::make_exception_ptr(std::runtime_error{ "error" }));
            ADD_FAILURE();
        } catch (const std::runtime_error& error) {
            EXPECT_STREQ(error.what(), "error");
        }
        try {
            co_await ex::just_error(std::make_error_code(std::errc::invalid_argument));
            ADD_FAILURE();
        } catch (const std::system_error& error) {
            EXPECT_EQ(error.code(), std::errc::invalid_argument);
        }
        try {
            co_await ex::just_error(23);
            ADD_FAILURE();
        } catch (int error) {
            EXPECT_EQ(error, 23);
        }
        bool stopped{};
        try {
            co_await ex::just_stopped();
        } catch (const pnm::coro::OperationStopped&) {
            stopped = true;
        }
        EXPECT_TRUE(stopped);
        // Cleanup remains possible after catching cancellation.
        EXPECT_TRUE(co_await pnm::coro::sleep(context, 1ms));
    });
}

TEST(CoroutinesExecution, TaskSendersComposeAndPreserveDomainValues)
{
    run([](auto& context) -> Task<void> {
        auto [a, b]{ co_await ex::when_all(pnm::coro::asSender(context, answer(3)),
                                           pnm::coro::asSender(context, answer(5))) };
        EXPECT_EQ(a + b, 8);
        auto domain_error{ []() -> Task<std::expected<int, int>> { co_return std::unexpected{ 9 }; } };
        auto value{ co_await pnm::coro::asSender(context, domain_error()) };
        EXPECT_EQ(value.error(), 9);
        bool failed{};
        try {
            static_cast<void>(co_await pnm::coro::asSender(context, fail()));
        } catch (const std::runtime_error&) {
            failed = true;
        }
        EXPECT_TRUE(failed);
    });
}

TEST(CoroutinesExecution, FactoryIsLazyOwnsCapturesAndReceivesStopToken)
{
    pnm::coro::Context context;
    auto alive{ std::make_shared<int>(5) };
    std::weak_ptr weak{ alive };
    bool invoked{};
    auto sender{ pnm::coro::asSender(
      context, [owned{ std::move(alive) }, &invoked](std::stop_token stop) -> Task<int> {
        invoked = true;
        EXPECT_TRUE(stop.stop_possible());
        co_return *owned;
    }) };
    EXPECT_FALSE(invoked);
    Completion output;
    auto op{ ex::connect(std::move(sender), Receiver{ &output, {} }) };
    ex::start(op);
    EXPECT_FALSE(invoked);
    context.poll();
    EXPECT_TRUE(invoked);
    EXPECT_EQ(output.values, 1);
    EXPECT_TRUE(weak.expired());
}

TEST(CoroutinesExecution, InvalidTasksAndPrecancelledFactories)
{
    pnm::coro::Context context;
    EXPECT_THROW(pnm::coro::asSender(context, Task<int>{}), std::logic_error);
    auto task{ answer() };
    task.resume();
    EXPECT_THROW(pnm::coro::asSender(context, std::move(task)), std::logic_error);
    bool called{};
    auto sender{ pnm::coro::asSender(context, [&](std::stop_token) {
        called = true;
        return answer();
    }) };
    std::stop_source source;
    source.request_stop();
    Completion output;
    auto op{ ex::connect(std::move(sender), Receiver{ &output, source.get_token() }) };
    ex::start(op);
    EXPECT_FALSE(called);
    EXPECT_EQ(output.stopped, 1);
    EXPECT_EQ(context.poll(), 0);
}

TEST(CoroutinesExecution, ScopeCancellationJoinsAfterAsynchronousCleanup)
{
    run([](auto& context) -> Task<void> {
        ex::counting_scope scope;
        pnm::coro::Channel<bool> ready;
        pnm::coro::Channel<bool> wait;
        bool cleaned{};
        std::exception_ptr error;
        ex::spawn(pnm::coro::asSender(context,
                                      [&](std::stop_token stop) -> Task<void> {
            ready.push(true);
            EXPECT_FALSE(co_await wait.next(stop));
            EXPECT_TRUE(stop.stop_requested());
            bool caught{};
            try {
                co_await ex::schedule(context.getExecutionScheduler());
            } catch (const pnm::coro::OperationStopped&) {
                caught = true;
            }
            EXPECT_TRUE(caught);
            co_await ex::unstoppable(ex::schedule(context.getExecutionScheduler()));
            EXPECT_TRUE(co_await pnm::coro::sleep(context, 1ms));
            cleaned = true;
        }) | ex::upon_error([&](std::exception_ptr failure) noexcept { error = failure; }),
                  scope.get_token());
        co_await ready.next();
        scope.request_stop();
        scope.close();
        co_await scope.join();
        EXPECT_TRUE(cleaned);
        EXPECT_FALSE(error);
    });
}

TEST(CoroutinesExecution, DirectChannelWaitCancellationWaitsForCleanup)
{
    pnm::coro::Context context;
    pnm::coro::Channel<int> input;
    pnm::coro::Channel<bool> cleanup;
    std::stop_source source;
    Completion output;
    bool cancelled{};
    bool cleaned{};
    const auto owner{ std::this_thread::get_id() };
    auto capture{ std::make_shared<int>(42) };
    std::weak_ptr lifetime{ capture };
    auto sender{ pnm::coro::asSender(context,
                                     [&, owned{ std::move(capture) }](std::stop_token stop) -> Task<void> {
        EXPECT_FALSE(co_await input.wait(stop));
        EXPECT_EQ(std::this_thread::get_id(), owner);
        cancelled = true;
        // Cancellation must leave the task alive until its explicit cleanup finishes.
        EXPECT_TRUE(co_await cleanup.wait());
        EXPECT_EQ(*owned, 42);
        cleaned = true;
    }) };
    auto operation{ ex::connect(std::move(sender), Receiver{ &output, source.get_token() }) };
    ex::start(operation);
    context.poll();
    std::jthread cancel{ [&] { source.request_stop(); } };
    cancel.join();
    EXPECT_FALSE(cancelled); // The cancellation continuation is queued on the context.
    context.poll();
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(cleaned);
    EXPECT_FALSE(lifetime.expired());
    EXPECT_EQ(output.values + output.errors + output.stopped, 0);
    cleanup.push(true);
    context.poll();
    EXPECT_TRUE(cleaned);
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(output.values, 1);
    EXPECT_EQ(output.errors + output.stopped, 0);
    EXPECT_EQ(output.thread, owner);
}

TEST(CoroutinesExecution, DirectChannelWaitAbandonmentCompletesItsSender)
{
    auto context{ std::make_unique<pnm::coro::Context>() };
    pnm::coro::Channel<int> channel;
    Completion output;
    auto capture{ std::make_shared<int>(42) };
    std::weak_ptr lifetime{ capture };
    auto sender{ pnm::coro::asSender(*context,
                                     [&, owned{ std::move(capture) }](std::stop_token stop) -> Task<void> {
        EXPECT_EQ(co_await channel.wait(stop), *owned);
    }) };
    auto operation{ ex::connect(std::move(sender), Receiver{ &output, {} }) };
    ex::start(operation);
    context->poll();
    channel.push(42);
    EXPECT_EQ(output.values + output.errors + output.stopped, 0);
    context.reset();
    EXPECT_EQ(output.errors, 1);
    EXPECT_EQ(output.values + output.stopped, 0);
    EXPECT_TRUE(lifetime.expired());
}

TEST(CoroutinesExecution, ExplicitWorkersReturnToContextAndRespectPoolSize)
{
    pnm::coro::ThreadPool pool{ 2 };
    EXPECT_THROW(pnm::coro::ThreadPool{ 0 }, std::invalid_argument);
    run([&](auto& context) -> Task<void> {
        auto owner{ std::this_thread::get_id() };
        std::set<std::thread::id> workers;
        for (int i{}; i < 20; ++i) {
            auto worker{ co_await pnm::coro::runAsync<std::thread::id>(
              pool.getExecutionScheduler(), [] { return std::this_thread::get_id(); }) };
            EXPECT_NE(worker, owner);
            EXPECT_EQ(std::this_thread::get_id(), owner);
            workers.insert(worker);
        }
        EXPECT_LE(workers.size(), 2);
        auto value{ co_await ex::on(pool.getExecutionScheduler(), ex::just(13)) };
        EXPECT_EQ(value, 13);
        EXPECT_EQ(std::this_thread::get_id(), owner);
        EXPECT_EQ(co_await ex::continues_on(ex::just(15), context.getExecutionScheduler()), 15);
        EXPECT_EQ(co_await ex::read_env(ex::get_start_scheduler), context.getExecutionScheduler());
        co_await ex::schedule(pool.getExecutionScheduler());
        EXPECT_EQ(std::this_thread::get_id(), owner);
        auto result{ co_await pnm::coro::runAsync<std::unique_ptr<int>>(
          pool.getExecutionScheduler(),
          [value{ std::make_unique<int>(42) }]() mutable { return std::move(value); }) };
        EXPECT_EQ(*result, 42);
    });
}

TEST(CoroutinesExecution, ForeignTaskRunsOnCompatibleScheduler)
{
    pnm::coro::ThreadPool pool{ 1 };
    auto foreign{ []() -> ex::task<int> { co_return 19; } };
    run([&](auto&) -> Task<void> {
        EXPECT_EQ(co_await ex::starts_on(pool.getExecutionScheduler(), foreign()), 19);
    });
}

TEST(CoroutinesExecution, SuspendedTaskDestructionRetainsWorkerState)
{
    std::latch entered{ 1 };
    std::latch release{ 1 };
    std::latch finished{ 1 };
    auto owned{ std::make_shared<int>(1) };
    std::weak_ptr weak{ owned };
    bool resumed{};
    pnm::coro::ThreadPool pool{ 1 };
    auto function{ [&, value{ std::move(owned) }]() -> Task<void> {
        co_await ex::starts_on(pool.getExecutionScheduler(), ex::just() | ex::then([&, copy{ value }] {
            entered.count_down();
            release.wait();
            EXPECT_EQ(*copy, 1);
            finished.count_down();
        }));
        resumed = true;
    } };
    auto task{ function() };
    task.resume();
    entered.wait();
    task = {};
    EXPECT_FALSE(weak.expired());
    release.count_down();
    finished.wait();
    EXPECT_FALSE(resumed);
}

TEST(CoroutinesExecution, HeaderOnlyParallelSchedulerAcrossTranslationUnits)
{
    EXPECT_EQ(execution_tu_probe(), 7);
    auto result{ ex::sync_wait(ex::on(ex::get_parallel_scheduler(), ex::just(11))) };
    EXPECT_EQ(std::get<0>(*result), 11);
}

TEST(CoroutinesExecution, InlineCompletionDoesNotRecurseThroughResume)
{
    auto function{ []() -> Task<int> {
        int sum{};
        for (int i{}; i < 10000; ++i)
            sum += co_await ex::just(1);
        co_return sum;
    } };
    auto task{ function() };
    task.resume();
    EXPECT_EQ(task.await_resume(), 10000);
}

TEST(CoroutinesExecution, OperationIsDestroyedBeforeAwaitingCodeContinues)
{
    run([](auto&) -> Task<void> {
        bool destroyed{};
        co_await DestructionSender{ &destroyed };
        EXPECT_TRUE(destroyed);
    });
}

TEST(CoroutinesExecution, WhenAllCancelsSiblingAndWaitsForItsCleanup)
{
    run([](auto& context) -> Task<void> {
        pnm::coro::Channel<bool> entered;
        pnm::coro::Channel<bool> wait;
        bool cleaned{};
        bool failed{};
        try {
            co_await ex::when_all(pnm::coro::asSender(context,
                                                      [&](std::stop_token stop) -> Task<void> {
                entered.push(true);
                EXPECT_FALSE(co_await wait.next(stop));
                co_await pnm::coro::sleep(context, 1ms);
                cleaned = true;
            }),
                                  pnm::coro::asSender(context, [&](std::stop_token) -> Task<void> {
                co_await entered.next();
                throw std::runtime_error{ "cancel sibling" };
            }));
        } catch (const std::runtime_error&) {
            failed = true;
        }
        EXPECT_TRUE(failed);
        EXPECT_TRUE(cleaned);
    });
}

TEST(CoroutinesExecution, TaskStopBecomesStoppedAndCanBeHandledAsAValue)
{
    run([](auto& context) -> Task<void> {
        auto stopped{ []() -> Task<int> {
            co_await ex::just_stopped();
            co_return 0;
        } };
        EXPECT_EQ(co_await (pnm::coro::asSender(context, stopped()) | ex::upon_stopped([] { return 21; })),
                  21);
    });
}

TEST(CoroutinesExecution, SpawnFutureAndUnstoppableCleanup)
{
    run([](auto& context) -> Task<void> {
        ex::counting_scope scope;
        auto future{ ex::spawn_future(pnm::coro::asSender(context, answer(37)), scope.get_token()) };
        EXPECT_EQ(co_await std::move(future), 37);
        scope.close();
        co_await scope.join();
        std::stop_source source;
        source.request_stop();
        auto sender{ ex::write_env(
          ex::unstoppable(ex::schedule(context.getExecutionScheduler()) | ex::then([] { return 29; })),
          ex::prop{ ex::get_stop_token, source.get_token() }) };
        EXPECT_EQ(co_await std::move(sender), 29);
    });
}

TEST(CoroutinesExecution, DestructionRacesCompletionWithoutResumingADestroyedFrame)
{
    for (int iteration{}; iteration < 100; ++iteration) {
        std::latch entered{ 1 };
        std::latch release{ 1 };
        auto value{ std::make_shared<int>(iteration) };
        pnm::coro::ThreadPool pool{ 1 };
        auto factory{ [&, owned{ std::move(value) }]() -> Task<void> {
            co_await ex::starts_on(pool.getExecutionScheduler(), ex::just() | ex::then([&, capture{ owned }] {
                entered.count_down();
                release.wait();
                EXPECT_EQ(*capture, iteration);
            }));
        } };
        auto task{ factory() };
        task.resume();
        entered.wait();
        std::jthread destroy{ [&] { task = {}; } };
        release.count_down();
        destroy.join();
        // Pool destruction joins all callbacks before this iteration's borrowed data is released.
    }
}

TEST(CoroutinesExecution, ConcurrentServiceSendersPreserveResultsAndProviderAdmission)
{
    run([](auto& context) -> Task<void> {
        pnm::msg::Bus native;
        pnm::coro::Bus bus{ context, native };
        auto service{ bus.service<int, int>("execution/service") };
        int active{};
        int peak{};
        auto provider{ service.serve([&](int input, std::stop_token stop) -> Task<int> {
            peak = std::max(peak, ++active);
            co_await pnm::coro::sleep(context, 1ms, stop);
            --active;
            co_return input * 2;
        }) };
        auto [a, b]{ co_await ex::when_all(
          pnm::coro::asSender(context,
                              [service](std::stop_token stop) { return service.request(20, 1s, stop); }),
          pnm::coro::asSender(context,
                              [service](std::stop_token stop) { return service.request(21, 1s, stop); })) };
        EXPECT_EQ(a.value(), 40);
        EXPECT_EQ(b.value(), 42);
        EXPECT_EQ(peak, 2);
        EXPECT_EQ(active, 0);
        bus.requestStop();
        co_await bus.join();
    });
}

TEST(CoroutinesExecution, ActionSenderPreservesCancellationAcknowledgmentAndPartialResult)
{
    run([](auto& context) -> Task<void> {
        pnm::msg::Bus native;
        pnm::coro::Bus bus{ context, native };
        auto action{ bus.action<int, int, int>("execution/action") };
        pnm::coro::Channel<bool> entered;
        bool cleaned{};
        auto provider{ action.serve([&](int value, pnm::coro::ActionExecution<int, int> control)
                                      -> Task<pnm::msg::ActionCompletion<int>> {
            entered.push(true);
            EXPECT_FALSE(co_await pnm::coro::sleep(context, 1h, control.stopToken()));
            co_await pnm::coro::sleep(context, 1ms);
            cleaned = true;
            co_return pnm::msg::ActionCompletion<int>{ pnm::msg::ActionStatus::Cancelled, value / 2 };
        }) };
        auto goal{ action.sendGoal(42, { 1s }) };
        co_await entered.next();
        goal.requestCancel();
        auto result{ co_await pnm::coro::asSender(context, goal.result()) };
        EXPECT_TRUE(cleaned);
        EXPECT_EQ(result->status, pnm::msg::ActionStatus::Cancelled);
        EXPECT_EQ(result->value, 21);
        bus.requestStop();
        co_await bus.join();
    });
}

TEST(CoroutinesExecution, QueueAllocationFailureCompletesWithTheOriginalError)
{
    pnm::coro::Context context;
    auto scheduler{ context.getExecutionScheduler() };
    using Operation = decltype(ex::connect(ex::schedule(scheduler), Receiver{}));
    std::vector<std::unique_ptr<Operation>> operations;
    std::vector<std::unique_ptr<Completion>> results;
    bool allocation_failed{};
    for (int i{}; i < 1024 && !allocation_failed; ++i) {
        results.push_back(std::make_unique<Completion>());
        auto* output{ results.back().get() };
        operations.push_back(
          std::make_unique<Operation>(ex::connect(ex::schedule(scheduler), Receiver{ output, {} })));
        allocation_probe::failNext();
        ex::start(*operations.back());
        allocation_failed = allocation_probe::failureConsumed();
        if (allocation_failed) {
            EXPECT_EQ(output->errors, 1);
            EXPECT_THROW(std::rethrow_exception(output->error), std::bad_alloc);
        }
    }
    EXPECT_TRUE(allocation_failed);
    context.poll(2048);
    for (const auto& output : results)
        EXPECT_EQ(output->values + output->errors, 1);
}

TEST(CoroutinesExecution, DefaultCustomExecutorAdapterStillWorks)
{
    struct Custom : pnm::coro::IExecutor
    {
        pnm::coro::Context context;
        auto run() -> void override { context.run(); }
        auto stop() -> void override { context.stop(); }
        auto schedule(std::coroutine_handle<> handle) -> void override { context.schedule(handle); }
        auto scheduleOwned(pnm::coro::DetachedTask task) -> void override
        {
            context.scheduleOwned(std::move(task));
        }
        auto getLifeToken() -> std::weak_ptr<void> override { return context.getLifeToken(); }
    };
    Custom custom;
    Completion output;
    auto op{ ex::connect(ex::schedule(custom.getExecutionScheduler()), Receiver{ &output, {} }) };
    ex::start(op);
    custom.stop();
    custom.run();
    EXPECT_EQ(output.values, 1);
}

TEST(CoroutinesExecution, QueriedStopTokenRemainsConnectedAfterTheAwait)
{
    run([](auto& context) -> Task<void> {
        ex::counting_scope scope;
        pnm::coro::Channel<bool> ready;
        pnm::coro::Channel<bool> wait;
        bool cancelled{};
        std::exception_ptr error;
        ex::spawn(pnm::coro::asSender(context,
                                      [&](std::stop_token supplied) -> Task<void> {
            auto token{ co_await ex::read_env(ex::get_stop_token) };
            EXPECT_EQ(token, supplied);
            ready.push(true);
            EXPECT_FALSE(co_await wait.next(token));
            cancelled = token.stop_requested();
        }) | ex::upon_error([&](std::exception_ptr failure) noexcept { error = failure; }),
                  scope.get_token());
        co_await ready.next();
        scope.request_stop();
        scope.close();
        co_await scope.join();
        EXPECT_TRUE(cancelled);
        EXPECT_FALSE(error);
    });
}

TEST(CoroutinesExecution, ReceiverCanDestroyItsOperationDuringCompletion)
{
    struct CallbackReceiver
    {
        using receiver_concept [[maybe_unused]] = ex::receiver_tag;
        std::function<void()> callback;
        auto set_value() && noexcept -> void
        {
            auto function{ std::move(callback) };
            function();
        }
        auto set_error(std::exception_ptr) && noexcept -> void { ADD_FAILURE(); }
        auto set_stopped() && noexcept -> void { ADD_FAILURE(); }
    };
    pnm::coro::Context context;
    auto scheduler{ context.getExecutionScheduler() };
    using Operation = decltype(ex::connect(ex::schedule(scheduler), CallbackReceiver{}));
    std::unique_ptr<Operation> operation;
    bool called{};
    operation = std::make_unique<Operation>(ex::connect(ex::schedule(scheduler), CallbackReceiver{ [&] {
        operation.reset();
        called = true;
    } }));
    ex::start(*operation);
    context.poll();
    EXPECT_TRUE(called);
    EXPECT_FALSE(operation);

    auto capture{ std::make_shared<int>(1) };
    std::weak_ptr weak{ capture };
    auto sender{ pnm::coro::asSender(context, [owned{ std::move(capture) }](std::stop_token) -> Task<void> {
        EXPECT_EQ(*owned, 1);
        co_return;
    }) };
    using TaskOperation = decltype(ex::connect(std::move(sender), CallbackReceiver{}));
    std::unique_ptr<TaskOperation> task_operation;
    task_operation = std::make_unique<TaskOperation>(ex::connect(std::move(sender), CallbackReceiver{ [&] {
        EXPECT_TRUE(weak.expired()); // Captures and coroutine resources precede receiver notification.
        task_operation.reset();
    } }));
    ex::start(*task_operation);
    context.poll();
    EXPECT_FALSE(task_operation);
}

TEST(CoroutinesExecution, CancellationRacesTaskCompletionAndCallbackDeregistration)
{
    pnm::coro::ThreadPool pool{ 1 };
    for (int iteration{}; iteration < 100; ++iteration) {
        pnm::coro::Context context;
        std::stop_source source;
        std::barrier rendezvous{ 2 };
        Completion output;
        auto sender{ pnm::coro::asSender(context, [&](std::stop_token stop) -> Task<int> {
            co_await pnm::coro::runAsync<void>(pool.getExecutionScheduler(),
                                               [&] { rendezvous.arrive_and_wait(); });
            if (stop.stop_requested())
                throw pnm::coro::OperationStopped{};
            co_return 1;
        }) };
        auto operation{ ex::connect(std::move(sender), Receiver{ &output, source.get_token() }) };
        ex::start(operation);
        context.poll();
        std::jthread cancel{ [&] {
            rendezvous.arrive_and_wait();
            source.request_stop();
        } };
        while (output.values + output.errors + output.stopped == 0) {
            context.poll();
            std::this_thread::yield();
        }
        cancel.join();
        EXPECT_EQ(output.errors, 0);
        EXPECT_EQ(output.values + output.stopped, 1);
    }
}
