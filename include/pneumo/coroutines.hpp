#pragma once

#include "pneumo/common.hpp"
#include "pneumo/messaging.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef PNM_CORO_ENABLE_EXECUTION
#define PNM_CORO_ENABLE_EXECUTION 0
#endif

#if PNM_CORO_ENABLE_EXECUTION
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>
#endif

namespace pnm::coro
{
#if PNM_CORO_ENABLE_EXECUTION
    namespace execution = ::stdexec;

    class OperationStopped : public std::exception
    {
      public:
        auto what() const noexcept -> const char* override { return "Coroutine operation stopped"; }
    };

    namespace detail::exec
    {
        struct Work
        {
            Work() = default;
            virtual ~Work() = default;
            Work(const Work&) = delete;
            auto operator=(const Work&) -> Work& = delete;
            Work(Work&&) = delete;
            auto operator=(Work&&) -> Work& = delete;
            virtual auto run() noexcept -> void = 0;
            virtual auto fail(std::exception_ptr error) noexcept -> void = 0;
        };

        // An accepted ticket either runs or reports abandonment to its receiver.
        class Ticket
        {
          public:
            Ticket() = default;
            explicit Ticket(std::shared_ptr<Work> work)
              : m_work{ std::move(work) }
            {
            }
            Ticket(const Ticket&) = delete;
            auto operator=(const Ticket&) -> Ticket& = delete;
            Ticket(Ticket&&) noexcept = default;
            auto operator=(Ticket&& other) noexcept -> Ticket&
            {
                if (this != &other) {
                    abandon();
                    m_work = std::move(other.m_work);
                }
                return *this;
            }
            ~Ticket() { abandon(); }
            explicit operator bool() const noexcept { return bool(m_work); }
            auto run() noexcept -> void
            {
                auto work{ std::move(m_work) };
                work->run();
            }

          private:
            auto abandon() noexcept -> void
            {
                if (auto work{ std::move(m_work) }) {
                    try {
                        throw std::runtime_error{ "Executor abandoned queued work" };
                    } catch (...) {
                        work->fail(std::current_exception());
                    }
                }
            }
            std::shared_ptr<Work> m_work;
        };
        struct Binding
        {
            std::weak_ptr<void> identity;
            std::function<void(Ticket&)> enqueue;
        };
        struct ScheduleSender;
    }

    class ExecutionScheduler
    {
      public:
        using scheduler_concept = execution::scheduler_tag;
        ExecutionScheduler() = default; // Inline scheduling for unbound tasks.
        explicit ExecutionScheduler(std::shared_ptr<detail::exec::Binding> binding)
          : m_binding{ std::move(binding) }
        {
        }
        auto schedule() const noexcept -> detail::exec::ScheduleSender;
        auto query(execution::get_completion_scheduler_t<execution::set_value_t> /*unused*/) const noexcept
          -> ExecutionScheduler
        {
            return *this;
        }
        static auto query(execution::get_forward_progress_guarantee_t /*unused*/) noexcept
        {
            return execution::forward_progress_guarantee::weakly_parallel;
        }
        friend auto operator==(const ExecutionScheduler& a, const ExecutionScheduler& b) noexcept -> bool
        {
            if (!a.m_binding || !b.m_binding)
                return !a.m_binding && !b.m_binding;
            return !a.m_binding->identity.owner_before(b.m_binding->identity) &&
                   !b.m_binding->identity.owner_before(a.m_binding->identity);
        }
        // Internal submission primitive; enqueue must throw before transferring its ticket.
        auto submit(const std::shared_ptr<detail::exec::Work>& pending) const noexcept -> void
        {
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
            auto work{ pending };      // Receiver completion may destroy the caller and its argument.
            auto binding{ m_binding }; // The enqueue callback must also survive synchronous completion.
            detail::exec::Ticket ticket{ work };
            try {
                if (!binding) {
                    ticket.run();
                }
                else {
                    if (binding->identity.expired())
                        throw std::runtime_error{ "Coroutine executor is no longer alive" };
                    binding->enqueue(ticket);
                }
            } catch (...) {
                work->fail(std::current_exception());
            }
        }

      private:
        std::shared_ptr<detail::exec::Binding> m_binding;
    };

    namespace detail::exec
    {
        template<typename Receiver>
        struct ScheduleState final : Work
        {
            explicit ScheduleState(Receiver value)
              : receiver{ std::move(value) }
            {
            }
            auto run() noexcept -> void override
            {
                if (completed.exchange(true))
                    return;
                if (execution::get_stop_token(execution::get_env(receiver)).stop_requested())
                    execution::set_stopped(std::move(receiver));
                else
                    execution::set_value(std::move(receiver));
            }
            auto fail(std::exception_ptr error) noexcept -> void override
            {
                if (!completed.exchange(true))
                    execution::set_error(std::move(receiver), std::move(error));
            }
            Receiver receiver;
            std::atomic<bool> completed{ false };
        };
        struct ScheduleSender
        {
            using sender_concept = execution::sender_tag;
            using completion_signatures =
              execution::completion_signatures<execution::set_value_t(),
                                               execution::set_error_t(std::exception_ptr),
                                               execution::set_stopped_t()>;
            ExecutionScheduler scheduler;
            struct Env
            {
                ExecutionScheduler scheduler;
                auto query(
                  execution::get_completion_scheduler_t<execution::set_value_t> /*unused*/) const noexcept
                {
                    return scheduler;
                }
            };
            auto get_env() const noexcept -> Env { return { scheduler }; }
            template<typename Receiver>
            struct Operation
            {
                using operation_state_concept = execution::operation_state_tag;
                ExecutionScheduler scheduler;
                std::shared_ptr<ScheduleState<Receiver>> state;
                auto start() noexcept -> void
                {
                    auto keep_alive{ state };
                    if (execution::get_stop_token(execution::get_env(keep_alive->receiver)).stop_requested())
                        keep_alive->run();
                    else
                        scheduler.submit(std::move(keep_alive));
                }
            };
            template<typename Receiver>
            auto connect(Receiver receiver) const -> Operation<Receiver>
            {
                return { scheduler, std::make_shared<ScheduleState<Receiver>>(std::move(receiver)) };
            }
        };
    }
    inline auto ExecutionScheduler::schedule() const noexcept -> detail::exec::ScheduleSender
    {
        return { *this };
    }
#endif

    class DetachedTask;
    using Scheduler = std::function<void(DetachedTask)>;

    class IExecutor
    {
      public:
        virtual ~IExecutor() = default;
        virtual auto run() -> void = 0;
        virtual auto stop() -> void = 0;
        virtual auto schedule(std::coroutine_handle<> handle) -> void = 0;
        virtual auto getLifeToken() -> std::weak_ptr<void> = 0;
        // Throw only before accepting work. Override this to retain ownership of queued frames.
        virtual auto scheduleOwned(DetachedTask task) -> void;
        virtual auto getScheduler() -> Scheduler;
#if PNM_CORO_ENABLE_EXECUTION
        virtual auto getExecutionScheduler() -> ExecutionScheduler;
#endif

      protected:
        IExecutor() = default;
        IExecutor(const IExecutor&) = default;
        auto operator=(const IExecutor&) -> IExecutor& = default;
        IExecutor(IExecutor&&) noexcept = default;
        auto operator=(IExecutor&&) noexcept -> IExecutor& = default;
    };

    template<typename T>
    concept Executor = std::derived_from<T, IExecutor>;

    namespace detail
    {
#if PNM_CORO_ENABLE_EXECUTION
        // BasicLockable: defer teardown until the outermost resume/destruction lock is released.
        class ExecutionGate
        {
          public:
            struct Action
            {
                explicit Action(std::function<void()> function)
                  : callback{ std::move(function) }
                {
                }
                std::function<void()> callback;
                std::unique_ptr<Action> next;
            };
            auto lock() -> void
            {
                m_mutex.lock();
                ++m_depth;
            }
            auto unlock() noexcept -> void
            {
                std::unique_ptr<Action> actions;
                Action* tail{};
                if (--m_depth == 0) {
                    actions = std::move(m_actions);
                    tail = std::exchange(m_tail, nullptr);
                }
                m_mutex.unlock();
                // No member access after unlocking: actions may release the gate's last owner.
                drain(std::move(actions), tail);
            }
            auto defer(std::unique_ptr<Action> action) -> void
            {
                std::scoped_lock lock{ *this };
                auto* tail{ action.get() };
                if (m_tail)
                    m_tail->next = std::move(action);
                else
                    m_actions = std::move(action);
                m_tail = tail;
            }

          private:
            static auto drain(std::unique_ptr<Action> actions, Action* tail) noexcept -> void
            {
                if (!actions)
                    return;
                struct Pending
                {
                    std::unique_ptr<Action> head;
                    Action* tail{};
                    bool active{};
                };
                // Nested resumptions append work instead of recursively destroying/resuming frames.
                static thread_local Pending pending;
                if (pending.tail)
                    pending.tail->next = std::move(actions);
                else
                    pending.head = std::move(actions);
                pending.tail = tail;
                if (pending.active)
                    return;
                pending.active = true;
                while (pending.head) {
                    auto action{ std::move(pending.head) };
                    pending.head = std::move(action->next);
                    if (!pending.head)
                        pending.tail = nullptr;
                    try {
                        action->callback();
                    } catch (...) {
                        std::terminate();
                    }
                }
                pending.active = false;
            }
            std::recursive_mutex m_mutex;
            std::size_t m_depth{};
            std::unique_ptr<Action> m_actions;
            Action* m_tail{};
        };
#else
        using ExecutionGate = std::recursive_mutex;
#endif

#if PNM_CORO_ENABLE_EXECUTION
        namespace exec
        {
            template<typename Sender>
            class SenderAwaiter;
            template<typename T>
            concept ForeignSender =
              execution::sender<T> && (!requires { typename std::remove_cvref_t<T>::pnm_task_tag; });
        }
#endif
        struct DetachedTaskPromise;
        template<typename T>
        struct TaskPromise;
        struct PromiseBase;

        struct InitialAwaiter
        {
            PromiseBase* promise;
            static auto await_ready() noexcept -> bool { return false; }
            static auto await_suspend(std::coroutine_handle<> /*handle*/) noexcept -> void {}
            auto await_resume() const -> void;
        };

        struct PromiseBase
        {
            IExecutor* executor{ nullptr };
            Scheduler scheduler;
            // Allocate only after the frame has an owner. The supported GCC leaks coroutine
            // frames when construction of their promise throws. Children reuse their parent's gate.
            std::shared_ptr<ExecutionGate> execution_mutex;
            bool started{};

            auto getExecutionMutex() -> const std::shared_ptr<ExecutionGate>&
            {
                if (!execution_mutex)
                    execution_mutex = std::make_shared<ExecutionGate>();
                return execution_mutex;
            }

#if PNM_CORO_ENABLE_EXECUTION
            ExecutionScheduler execution_scheduler;
            std::optional<std::stop_source> execution_stop;
            template<exec::ForeignSender Sender>
            auto await_transform(Sender&& sender) -> exec::SenderAwaiter<std::decay_t<Sender>>;
            template<typename Awaitable>
                requires(!exec::ForeignSender<Awaitable>)
            static auto await_transform(Awaitable&& value) noexcept -> Awaitable&&
            {
                return std::forward<Awaitable>(value);
            }
#endif

            auto initial_suspend() noexcept -> InitialAwaiter { return { this }; }
        };

        inline auto InitialAwaiter::await_resume() const -> void
        {
            promise->started = true;
            // Also support callers resuming a borrowed raw handle instead of Task::resume().
            static_cast<void>(promise->getExecutionMutex());
        }
    }

    // Owns an unstarted frame until release() transfers it to an executor.
    class DetachedTask
    {
      public:
        using promise_type = detail::DetachedTaskPromise;
        using handle_type = std::coroutine_handle<promise_type>;

        DetachedTask() = default;
        explicit DetachedTask(handle_type handle) noexcept
          : m_handle{ handle }
        {
        }
        ~DetachedTask() { reset(); }
        DetachedTask(const DetachedTask&) = delete;
        auto operator=(const DetachedTask&) -> DetachedTask& = delete;
        DetachedTask(DetachedTask&& other) noexcept
          : m_handle{ other.release() }
        {
        }
        auto operator=(DetachedTask&& other) noexcept -> DetachedTask&
        {
            if (this != &other) {
                reset();
                m_handle = other.release();
            }
            return *this;
        }
        auto getHandle() const noexcept -> handle_type { return m_handle; }
        auto release() noexcept -> handle_type { return std::exchange(m_handle, {}); }

      private:
        auto reset() noexcept -> void
        {
            if (m_handle) {
                m_handle.destroy();
            }
        }
        handle_type m_handle;
    };

    template<typename T = void>
    class Task
    {
      public:
        using pnm_task_tag = void;
        using value_type = T;
        using promise_type = detail::TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        Task() = default;
        explicit Task(handle_type handle) noexcept
          : m_handle{ handle }
        {
        }
        ~Task() { reset(); }
        Task(const Task&) = delete;
        auto operator=(const Task&) -> Task& = delete;
        Task(Task&& other) noexcept
          : m_handle{ std::exchange(other.m_handle, {}) }
        {
        }
        auto operator=(Task&& other) noexcept -> Task&
        {
            if (this != &other) {
                reset();
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }

        auto await_ready() const -> bool { return isReady(m_handle); }
        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> waiter) -> std::coroutine_handle<>
        {
            return suspend(m_handle, waiter);
        }
        auto await_resume() -> T { return result(m_handle); }

        // Disconnect a borrowed child if its awaiting parent is destroyed first.
        class Awaiter
        {
          public:
            explicit Awaiter(handle_type handle) noexcept
              : m_handle{ handle }
            {
            }
            ~Awaiter()
            {
                if (m_waiter) {
                    auto& promise{ m_handle.promise() };
                    std::scoped_lock lock{ *promise.execution_mutex };
                    if (promise.waiter == m_waiter) {
                        promise.waiter = {};
                    }
                }
            }
            Awaiter(const Awaiter&) = delete;
            auto operator=(const Awaiter&) -> Awaiter& = delete;
            Awaiter(Awaiter&&) = delete;
            auto operator=(Awaiter&&) -> Awaiter& = delete;
            auto await_ready() const -> bool { return isReady(m_handle); }
            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> waiter) -> std::coroutine_handle<>
            {
                auto next{ suspend(m_handle, waiter) };
                m_waiter = waiter;
                return next;
            }
            auto await_resume() -> T { return result(m_handle); }

          private:
            handle_type m_handle;
            std::coroutine_handle<> m_waiter;
        };

        auto operator co_await() noexcept -> Awaiter { return Awaiter{ m_handle }; }

        // Prefer this to resuming the borrowed raw handle when a completion can race with the caller.
        auto resume() -> void
        {
            if (!m_handle) {
                throw std::logic_error{ "Cannot resume an empty task" };
            }
            auto mutex{ m_handle.promise().getExecutionMutex() };
            std::scoped_lock lock{ *mutex };
            if (m_handle.promise().started) {
                throw std::logic_error{ "Task has already been started" };
            }
            m_handle.resume();
        }

        auto getHandle() const noexcept -> handle_type { return m_handle; }

      private:
        static auto isReady(handle_type handle) -> bool
        {
            if (!handle) {
                return true;
            }
            auto mutex{ handle.promise().execution_mutex };
            std::unique_lock<detail::ExecutionGate> lock;
            if (mutex)
                lock = std::unique_lock{ *mutex };
            return handle.done();
        }

        template<typename Promise>
        static auto suspend(handle_type handle, std::coroutine_handle<Promise> waiter)
          -> std::coroutine_handle<>
        {
            auto& promise{ handle.promise() };
            if (promise.started || promise.waiter) {
                throw std::logic_error{ "A running task cannot be awaited again" };
            }
            if constexpr (std::derived_from<Promise, detail::PromiseBase>) {
                promise.execution_mutex = waiter.promise().getExecutionMutex();
                promise.executor = waiter.promise().executor;
                promise.scheduler = waiter.promise().scheduler;
#if PNM_CORO_ENABLE_EXECUTION
                promise.execution_scheduler = waiter.promise().execution_scheduler;
                promise.execution_stop = waiter.promise().execution_stop;
#endif
            }
            else {
                static_cast<void>(promise.getExecutionMutex());
            }
            promise.waiter = waiter;
            return handle;
        }

        static auto result(handle_type handle) -> T
        {
            if (!handle) {
                throw std::logic_error{ "Cannot await an empty task" };
            }
            auto mutex{ handle.promise().execution_mutex };
            std::unique_lock<detail::ExecutionGate> lock;
            if (mutex)
                lock = std::unique_lock{ *mutex };
            auto& promise{ handle.promise() };
            if (!handle.done() || promise.consumed) {
                throw std::logic_error{ "Task result is not ready or has already been consumed" };
            }
            promise.consumed = true;
            if (promise.exception) {
                std::rethrow_exception(promise.exception);
            }
            if constexpr (!std::is_void_v<T>) {
                if (!promise.value) {
                    throw std::bad_optional_access{};
                }
                return std::move(*promise.value);
            }
        }

        auto reset() noexcept -> void
        {
            if (m_handle) {
                // Awaited children share this lock, including symmetric transfers back to their parents.
                auto mutex{ m_handle.promise().execution_mutex };
                std::unique_lock<detail::ExecutionGate> lock;
                if (mutex)
                    lock = std::unique_lock{ *mutex };
                m_handle.destroy();
            }
        }
        handle_type m_handle;
    };

    namespace detail
    {
        struct DetachedTaskPromise : PromiseBase
        {
            auto get_return_object() -> DetachedTask
            {
                return DetachedTask{ DetachedTask::handle_type::from_promise(*this) };
            }
            static auto final_suspend() noexcept -> std::suspend_never { return {}; }
            static auto unhandled_exception() noexcept -> void { std::terminate(); }
            static auto return_void() noexcept -> void {}
        };

        template<typename T>
        struct TaskPromiseBase : PromiseBase
        {
            std::coroutine_handle<> waiter;
            std::exception_ptr exception;
            bool consumed{};

            static auto final_suspend() noexcept
            {
                struct Awaiter
                {
                    static auto await_ready() noexcept -> bool { return false; }
                    static auto await_suspend(std::coroutine_handle<TaskPromise<T>> handle) noexcept
                      -> std::coroutine_handle<>
                    {
                        return handle.promise().waiter ? handle.promise().waiter : std::noop_coroutine();
                    }
                    static auto await_resume() noexcept -> void {}
                };
                return Awaiter{};
            }
            auto unhandled_exception() noexcept -> void { exception = std::current_exception(); }
        };

        template<typename T>
        struct TaskPromise : TaskPromiseBase<T>
        {
            std::optional<T> value;
            auto get_return_object() -> Task<T>
            {
                return Task<T>{ Task<T>::handle_type::from_promise(*this) };
            }
            auto return_value(T result) -> void { value.emplace(std::move(result)); }
        };

        template<>
        struct TaskPromise<void> : TaskPromiseBase<void>
        {
            auto get_return_object() -> Task<void>
            {
                return Task<void>{ Task<void>::handle_type::from_promise(*this) };
            }
            static auto return_void() noexcept -> void {}
        };
    }

    inline auto IExecutor::scheduleOwned(DetachedTask task) -> void
    {
        if (auto handle{ task.getHandle() })
            static_cast<void>(handle.promise().getExecutionMutex());
        schedule(task.getHandle());
        static_cast<void>(task.release());
    }

    inline auto IExecutor::getScheduler() -> Scheduler
    {
        // Custom executors must outlive in-flight calls to their scheduling methods.
        return [this, token{ getLifeToken() }](DetachedTask task) {
            if (token.expired()) {
                throw std::runtime_error{ "Coroutine executor is no longer alive" };
            }
            scheduleOwned(std::move(task));
        };
    }

#if PNM_CORO_ENABLE_EXECUTION
    namespace detail::exec
    {
        // Coroutine protocol hooks are implicitly invoked through the promise/awaiter objects.
        // NOLINTBEGIN(readability-static-accessed-through-instance)
        inline auto dispatch_ticket(std::shared_ptr<Ticket> ticket) -> DetachedTask
        {
            ticket->run();
            co_return;
        }
        // NOLINTEND(readability-static-accessed-through-instance)
    }
    inline auto IExecutor::getExecutionScheduler() -> ExecutionScheduler
    {
        return ExecutionScheduler{ std::make_shared<detail::exec::Binding>(
          getLifeToken(), [scheduler{ getScheduler() }](detail::exec::Ticket& ticket) {
            // Retain the ticket while admission can still fail. Destruction of a rejected
            // frame must not report abandonment before submit() can report the actual error.
            auto pending{ std::make_shared<detail::exec::Ticket>(std::move(ticket)) };
            try {
                auto task{ detail::exec::dispatch_ticket(pending) };
                static_cast<void>(task.getHandle().promise().getExecutionMutex());
                scheduler(std::move(task));
            } catch (...) {
                ticket = std::move(*pending);
                throw;
            }
        }) };
    }
#endif

    class Context : public IExecutor
    {
        struct Work
        {
            std::coroutine_handle<> handle;
            DetachedTask owner;
#if PNM_CORO_ENABLE_EXECUTION
            // Keep existing partial aggregate initializers warning-free when execution is enabled.
            // NOLINTNEXTLINE(readability-redundant-member-init)
            detail::exec::Ticket sender{};
#endif
        };
        struct TimerEntry
        {
            std::function<void()> callback;
        };
        static constexpr std::size_t DEFAULT_POLL_LIMIT{ 64 };
        using Clock = std::chrono::steady_clock;
        using Timers = std::multimap<Clock::time_point, std::shared_ptr<TimerEntry>>;
        struct State
        {
            std::mutex mutex;
            std::condition_variable wake;
            std::deque<Work> queue;
            Timers timers;
#if PNM_CORO_ENABLE_EXECUTION
            std::shared_ptr<detail::exec::Binding> execution_binding;
#endif
            bool stopped{};
            bool destroying{};
            bool prefer_ready_work{};
        };

      public:
        Context() = default;
        ~Context() override
        {
            {
                std::scoped_lock lock{ m_state->mutex };
                m_state->stopped = true;
                m_state->destroying = true;
                m_state->wake.notify_all();
            }
            // Drain without allocating a replacement container in this noexcept destructor.
            // Receivers and frame destructors run outside the lock and may re-enter poll().
            for (;;) {
                Work abandoned;
                {
                    std::scoped_lock lock{ m_state->mutex };
                    if (m_state->queue.empty())
                        break;
                    abandoned = std::move(m_state->queue.front());
                    m_state->queue.pop_front();
                }
            }
        }
        Context(const Context&) = delete;
        auto operator=(const Context&) -> Context& = delete;
        Context(Context&&) = delete;
        auto operator=(Context&&) -> Context& = delete;

        // Drive from one thread only. poll() integrates with an existing application loop.
        auto run() -> void override
        {
            auto state{ m_state };
            while (runOne(state, true)) {
            }
        }
        auto poll(std::size_t limit = DEFAULT_POLL_LIMIT) -> std::size_t
        {
            auto state{ m_state };
            std::size_t count{};
            while (count < limit && runOne(state, false)) {
                ++count;
            }
            return count;
        }

        class Timer
        {
          public:
            Timer() = default;
            Timer(const Timer&) = delete;
            auto operator=(const Timer&) -> Timer& = delete;
            Timer(Timer&& other) noexcept
              : m_state{ std::move(other.m_state) }
              , m_entry{ std::move(other.m_entry) }
              , m_deadline{ other.m_deadline }
            {
            }
            auto operator=(Timer&& other) noexcept -> Timer&
            {
                if (this != &other) {
                    cancel();
                    m_state = std::move(other.m_state);
                    m_entry = std::move(other.m_entry);
                    m_deadline = other.m_deadline;
                }
                return *this;
            }
            ~Timer() { cancel(); }
            // Cancels a queued callback; a callback already executing may finish.
            auto cancel() noexcept -> void
            {
                auto state{ m_state.lock() };
                auto entry{ m_entry.lock() };
                m_entry.reset();
                if (!state || !entry) {
                    return;
                }
                Timers::node_type removed;
                {
                    std::scoped_lock lock{ state->mutex };
                    const auto [first, last]{ state->timers.equal_range(m_deadline) };
                    for (auto it{ first }; it != last; ++it) {
                        if (it->second == entry) {
                            removed = state->timers.extract(it);
                            break;
                        }
                    }
                }
                state->wake.notify_all();
            }

          private:
            friend class Context;
            Timer(const std::shared_ptr<State>& state,
                  const std::shared_ptr<TimerEntry>& entry,
                  Clock::time_point deadline)
              : m_state{ state }
              , m_entry{ entry }
              , m_deadline{ deadline }
            {
            }
            std::weak_ptr<State> m_state;
            std::weak_ptr<TimerEntry> m_entry;
            Clock::time_point m_deadline;
        };

        [[nodiscard]] auto scheduleAt(Clock::time_point deadline, std::function<void()> callback) -> Timer
        {
            auto entry{ std::make_shared<TimerEntry>(std::move(callback)) };
            {
                std::scoped_lock lock{ m_state->mutex };
                if (m_state->stopped) {
                    throw std::runtime_error{ "Cannot schedule a timer on a stopped context" };
                }
                m_state->timers.emplace(deadline, entry);
            }
            m_state->wake.notify_all();
            return Timer{ m_state, entry, deadline };
        }

        auto stop() -> void override
        {
            std::scoped_lock lock{ m_state->mutex };
            m_state->stopped = true;
            m_state->wake.notify_all();
        }

        auto schedule(std::coroutine_handle<> handle) -> void override
        {
            if (handle) {
                enqueue(m_state, Work{ .handle = handle, .owner = {} });
            }
        }
        auto scheduleOwned(DetachedTask task) -> void override
        {
            if (auto handle{ task.getHandle() }) {
                static_cast<void>(handle.promise().getExecutionMutex());
                enqueue(m_state, Work{ .handle = handle, .owner = std::move(task) });
            }
        }
        auto getLifeToken() -> std::weak_ptr<void> override { return m_state; }
        auto getScheduler() -> Scheduler override
        {
            return [weak{ std::weak_ptr<State>{ m_state } }](DetachedTask task) {
                auto state{ weak.lock() };
                if (!state) {
                    throw std::runtime_error{ "Coroutine context is no longer alive" };
                }
                if (auto handle{ task.getHandle() }) {
                    static_cast<void>(handle.promise().getExecutionMutex());
                    enqueue(state, Work{ .handle = handle, .owner = std::move(task) });
                }
            };
        }

#if PNM_CORO_ENABLE_EXECUTION
        auto getExecutionScheduler() -> ExecutionScheduler override
        {
            std::scoped_lock binding_lock{ m_state->mutex };
            if (!m_state->execution_binding) {
                // Reuse the adapter without extending the lifetime of the context's state.
                m_state->execution_binding =
                  std::make_shared<detail::exec::Binding>(getLifeToken(), [weak{ std::weak_ptr {
                                                              m_state
                                                          } }](detail::exec::Ticket& ticket) {
                    auto state{ weak.lock() };
                    if (!state)
                        throw std::runtime_error{ "Coroutine context is no longer alive" };
                    std::scoped_lock lock{ state->mutex };
                    if (state->stopped)
                        throw std::runtime_error{ "Cannot schedule work on a stopped context" };
                    // Allocate before transferring ownership, so failure leaves the ticket with submit().
                    state->queue.emplace_back();
                    state->queue.back().sender = std::move(ticket);
                    state->wake.notify_one();
                });
            }
            return ExecutionScheduler{ m_state->execution_binding };
        }
#endif

      private:
        static auto runOne(const std::shared_ptr<State>& state, bool wait) -> bool
        {
            Work work;
            std::shared_ptr<TimerEntry> timer;
            {
                std::unique_lock lock{ state->mutex };
                for (;;) {
                    if (state->destroying)
                        return false;
                    const bool timer_ready{ !state->timers.empty() &&
                                            state->timers.begin()->first <= Clock::now() };
                    // Alternate ready sources, including across separate poll() calls.
                    if (timer_ready && (state->queue.empty() || !state->prefer_ready_work)) {
                        timer = std::move(state->timers.begin()->second);
                        state->timers.erase(state->timers.begin());
                        state->prefer_ready_work = true;
                        break;
                    }
                    if (!state->queue.empty()) {
                        work = std::move(state->queue.front());
                        state->queue.pop_front();
                        state->prefer_ready_work = false;
                        break;
                    }
                    if (state->stopped || !wait) {
                        return false;
                    }
                    if (state->timers.empty()) {
                        state->wake.wait(lock);
                    }
                    else {
                        const auto deadline{ state->timers.begin()->first };
                        state->wake.wait_until(lock, deadline);
                    }
                }
            }
            if (timer) {
                timer->callback();
            }
#if PNM_CORO_ENABLE_EXECUTION
            else if (work.sender) {
                work.sender.run();
            }
#endif
            else if (work.handle && !work.handle.done()) {
                // Owned launches share this lock with nested tasks and their continuations.
                // In particular, scheduler rejection may resume a completion on another thread.
                auto mutex{ work.owner.getHandle() ? work.owner.getHandle().promise().execution_mutex
                                                   : nullptr };
                std::unique_lock<detail::ExecutionGate> execution;
                if (mutex)
                    execution = std::unique_lock{ *mutex };
                static_cast<void>(work.owner.release());
                work.handle.resume();
            }
            return true;
        }

        static auto enqueue(const std::shared_ptr<State>& state, Work work) -> void
        {
            std::scoped_lock lock{ state->mutex };
            if (state->stopped) {
                throw std::runtime_error{ "Cannot schedule work on a stopped context" };
            }
            state->queue.push_back(std::move(work));
            state->wake.notify_one();
        }
        std::shared_ptr<State> m_state{ std::make_shared<State>() };
    };

    namespace detail
    {
        // A queued delivery owns this ticket, never a pointer into a suspended awaiter.
        class Continuation : public std::enable_shared_from_this<Continuation>
        {
          public:
            template<typename Promise>
            explicit Continuation(std::coroutine_handle<Promise> handle)
              : m_handle{ handle }
            {
                if constexpr (std::derived_from<Promise, PromiseBase>) {
                    m_mutex = handle.promise().getExecutionMutex();
                    m_scheduler = handle.promise().scheduler;
                    if (!m_scheduler && handle.promise().executor) {
                        m_scheduler = handle.promise().executor->getScheduler();
                    }
                }
                else {
                    m_mutex = std::make_shared<ExecutionGate>();
                }
            }

            auto cancel() -> void
            {
                std::scoped_lock lock{ *m_mutex };
                m_handle = {};
            }
            auto resume() -> void
            {
                auto mutex{ m_mutex };
                std::scoped_lock lock{ *mutex };
                if (auto handle{ std::exchange(m_handle, {}) }) {
                    handle.resume();
                }
            }
            auto dispatch() -> void;
            auto reject(const std::exception_ptr& error) -> void
            {
                std::scoped_lock lock{ *m_mutex };
                m_exception = error;
                resume();
            }
            auto rethrowError() const -> void
            {
                if (m_exception) {
                    std::rethrow_exception(m_exception);
                }
            }

          private:
            std::coroutine_handle<> m_handle;
            std::shared_ptr<ExecutionGate> m_mutex;
            Scheduler m_scheduler;
            std::exception_ptr m_exception;
        };

        // The dispatcher retains this delivery until scheduling succeeds, so rejection keeps
        // its original exception. Once accepted, its queued frame owns abandonment reporting.
        struct ContinuationDelivery
        {
            explicit ContinuationDelivery(std::shared_ptr<Continuation> continuation)
              : pending{ std::move(continuation) }
            {
            }
            ContinuationDelivery(const ContinuationDelivery&) = delete;
            auto operator=(const ContinuationDelivery&) -> ContinuationDelivery& = delete;
            ContinuationDelivery(ContinuationDelivery&&) = delete;
            auto operator=(ContinuationDelivery&&) -> ContinuationDelivery& = delete;
            ~ContinuationDelivery()
            {
                if (pending) {
                    try {
                        throw std::runtime_error{ "Executor abandoned a queued continuation" };
                    } catch (...) {
                        pending->reject(std::current_exception());
                    }
                }
            }
            auto run() -> void
            {
                auto continuation{ std::move(pending) };
                continuation->resume();
            }
            std::shared_ptr<Continuation> pending;
        };

        // Implicit coroutine calls access static promise hooks through the promise object.
        // NOLINTBEGIN(readability-static-accessed-through-instance)
        inline auto resume_later(std::shared_ptr<ContinuationDelivery> delivery) -> DetachedTask
        {
            delivery->run();
            co_return;
        }

        // NOLINTEND(readability-static-accessed-through-instance)

        inline auto Continuation::dispatch() -> void
        {
            auto self{ weak_from_this().lock() };
            if (!self)
                std::terminate(); // Continuations are always created with shared ownership.
            if (!m_scheduler) {
                resume();
                return;
            }
            std::shared_ptr<ContinuationDelivery> delivery;
            try {
                delivery = std::make_shared<ContinuationDelivery>(self);
                auto task{ resume_later(delivery) };
                static_cast<void>(task.getHandle().promise().getExecutionMutex());
                m_scheduler(std::move(task));
            } catch (...) {
                // A rejected delivery must complete with an error rather than strand the task.
                if (delivery)
                    delivery->pending.reset();
                reject(std::current_exception());
            }
        }

        template<typename Function, typename T>
        class AsyncAwaiter
        {
            struct State
            {
                Function function;
                std::optional<std::conditional_t<std::is_void_v<T>, bool, T>> result;
                std::exception_ptr exception;
                std::shared_ptr<Continuation> continuation;
            };

          public:
            explicit AsyncAwaiter(Function function)
              : m_state{ std::make_shared<State>(std::move(function), std::nullopt, nullptr, nullptr) }
            {
            }
            ~AsyncAwaiter()
            {
                if (m_state->continuation) {
                    m_state->continuation->cancel();
                }
            }
            AsyncAwaiter(const AsyncAwaiter&) = delete;
            auto operator=(const AsyncAwaiter&) -> AsyncAwaiter& = delete;
            AsyncAwaiter(AsyncAwaiter&&) = delete;
            auto operator=(AsyncAwaiter&&) -> AsyncAwaiter& = delete;
            static auto await_ready() noexcept -> bool { return false; }

            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> handle) -> void
            {
                m_state->continuation = std::make_shared<Continuation>(handle);
                std::thread([state{ m_state }] {
                    try {
                        if constexpr (std::is_void_v<T>) {
                            std::invoke(state->function);
                        }
                        else {
                            state->result.emplace(std::invoke(state->function));
                        }
                    } catch (...) {
                        state->exception = std::current_exception();
                    }
                    state->continuation->dispatch();
                }).detach();
            }
            auto await_resume() -> T
            {
                m_state->continuation->rethrowError();
                if (m_state->exception) {
                    std::rethrow_exception(m_state->exception);
                }
                if constexpr (!std::is_void_v<T>) {
                    if (!m_state->result) {
                        throw std::bad_optional_access{};
                    }
                    return std::move(*m_state->result);
                }
            }

          private:
            std::shared_ptr<State> m_state;
        };
    }

    template<typename T, typename Function>
        requires std::invocable<Function&>
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto runAsync(Function function) -> Task<T>
    {
        co_return co_await detail::AsyncAwaiter<Function, T>{ std::move(function) };
    }

#if PNM_CORO_ENABLE_EXECUTION
    namespace detail::exec
    {
        template<typename T>
        auto required(std::optional<T>& value) -> T&
        {
            if (!value.has_value())
                throw std::bad_optional_access{};
            return *value;
        }

        using Environment =
          execution::env<execution::prop<execution::get_start_scheduler_t, ExecutionScheduler>,
                         execution::prop<execution::get_stop_token_t, std::stop_token>>;

        inline auto environment(ExecutionScheduler scheduler, std::stop_token token) -> Environment
        {
            return { execution::prop{ execution::get_start_scheduler, std::move(scheduler) },
                     execution::prop{ execution::get_stop_token, std::move(token) } };
        }

        template<typename... Values>
        using OwnedTuple = std::tuple<std::decay_t<Values>...>;
        template<typename... Alternatives>
        struct SingleValue
        {
            static_assert(sizeof...(Alternatives) == 0,
                          "Awaiting alternative value signatures requires execution::into_variant");
            using type = std::tuple<>;
        };
        template<typename Value>
        struct SingleValue<Value>
        {
            using type = Value;
        };

        template<typename Error>
        auto exception(Error&& error) noexcept -> std::exception_ptr
        {
            if constexpr (std::same_as<std::decay_t<Error>, std::exception_ptr>) {
                return std::forward<Error>(error);
            }
            else {
                try {
                    if constexpr (std::same_as<std::decay_t<Error>, std::error_code>)
                        throw std::system_error{ error };
                    else
                        throw std::forward<Error>(error);
                } catch (...) {
                    return std::current_exception();
                }
            }
        }

        template<typename Sender>
        struct AwaitState
        {
            using Values =
              typename execution::value_types_of_t<Sender, Environment, OwnedTuple, SingleValue>::type;
            struct Receiver
            {
                using receiver_concept = execution::receiver_tag;
                AwaitState* state;
                auto get_env() const noexcept -> Environment
                {
                    return environment(state->scheduler, state->stop.get_token());
                }
                template<typename... Args>
                auto set_value(Args&&... args) && noexcept -> void
                {
                    auto self{ state->keep_alive };
                    try {
                        self->values.emplace(std::forward<Args>(args)...);
                    } catch (...) {
                        self->error = std::current_exception();
                    }
                    self->finish();
                }
                template<typename Error>
                auto set_error(Error&& failure) && noexcept -> void
                {
                    auto self{ state->keep_alive };
                    self->error = exception(std::forward<Error>(failure));
                    self->finish();
                }
                auto set_stopped() && noexcept -> void
                {
                    auto self{ state->keep_alive };
                    self->stopped = true;
                    self->finish();
                }
            };
            using Operation = execution::connect_result_t<Sender, Receiver>;
            AwaitState(Sender sender,
                       ExecutionScheduler target,
                       std::stop_source source,
                       std::shared_ptr<Continuation> ticket)
              : scheduler{ std::move(target) }
              , stop{ std::move(source) }
              , continuation{ std::move(ticket) }
              // Direct initialization supports immovable operation states through guaranteed elision.
              , operation{ new Operation(execution::connect(std::move(sender), Receiver{ this })) }
            {
            }

            auto finish() noexcept -> void
            {
                auto self{ std::move(keep_alive) };
                gate->defer(std::move(completion));
            }
            ExecutionScheduler scheduler;
            std::stop_source stop;
            std::optional<Values> values;
            std::exception_ptr error;
            bool stopped{};
            bool started{};
            std::atomic<bool> completed{ false };
            std::shared_ptr<Continuation> continuation;
            std::shared_ptr<AwaitState> keep_alive;
            std::shared_ptr<ExecutionGate> gate;
            std::unique_ptr<Operation> operation;
            std::unique_ptr<ExecutionGate::Action> completion;
        };

        template<typename Sender>
        class SenderAwaiter
        {
            using State = AwaitState<Sender>;

          public:
            SenderAwaiter(PromiseBase& promise, Sender sender)
              : m_sender{ std::move(sender) }
              , m_scheduler{ promise.execution_scheduler }
              , m_source{ required(promise.execution_stop) }
              , m_gate{ promise.getExecutionMutex() }
            {
            }
            SenderAwaiter(const SenderAwaiter&) = delete;
            auto operator=(const SenderAwaiter&) -> SenderAwaiter& = delete;
            SenderAwaiter(SenderAwaiter&&) = delete;
            auto operator=(SenderAwaiter&&) -> SenderAwaiter& = delete;
            ~SenderAwaiter()
            {
                if (m_state)
                    m_state->continuation->cancel();
                if (m_cleanup && !m_state->completed.load(std::memory_order_acquire))
                    m_gate->defer(std::move(m_cleanup));
            }
            static auto await_ready() noexcept -> bool { return false; }
            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> handle) -> void
            {
                m_state = std::make_shared<State>(
                  std::move(m_sender), m_scheduler, m_source, std::make_shared<Continuation>(handle));
                auto state{ m_state };
                state->gate = m_gate;
                m_cleanup = std::make_unique<ExecutionGate::Action>([state] {
                    if (state->started && !state->completed.load(std::memory_order_acquire))
                        state->stop.request_stop();
                });
                state->completion = std::make_unique<ExecutionGate::Action>([state] {
                    state->operation.reset();
                    state->completed.store(true, std::memory_order_release);
                    state->continuation->dispatch();
                });
                state->keep_alive = state;
                state->started = true;
                execution::start(*state->operation);
                // Inline completion is drained after start() and the coroutine execution lock return.
            }
            auto await_resume()
            {
                m_state->continuation->rethrowError();
                if (m_state->error)
                    std::rethrow_exception(m_state->error);
                if (m_state->stopped)
                    throw OperationStopped{};
                constexpr auto count{ std::tuple_size_v<typename State::Values> };
                if constexpr (count == 1)
                    return std::move(std::get<0>(required(m_state->values)));
                else if constexpr (count > 1)
                    return std::move(required(m_state->values));
            }

          private:
            Sender m_sender;
            ExecutionScheduler m_scheduler;
            std::stop_source m_source;
            std::shared_ptr<ExecutionGate> m_gate;
            std::shared_ptr<State> m_state;
            std::unique_ptr<ExecutionGate::Action> m_cleanup;
        };
    }

    template<detail::exec::ForeignSender Sender>
    auto detail::PromiseBase::await_transform(Sender&& sender) -> exec::SenderAwaiter<std::decay_t<Sender>>
    {
        if (!execution_stop)
            execution_stop.emplace();
        return exec::SenderAwaiter<std::decay_t<Sender>>{ *this, std::forward<Sender>(sender) };
    }

    class ThreadPool
    {
      public:
        explicit ThreadPool(std::size_t worker_count)
          : m_pool{ checked(worker_count) }
        {
        }
        auto getExecutionScheduler() noexcept { return m_pool.get_scheduler(); }

      private:
        static auto checked(std::size_t count) -> std::uint32_t
        {
            if (count == 0 || count > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument{ "Invalid thread pool worker count" };
            return static_cast<std::uint32_t>(count);
        }
        ::exec::static_thread_pool m_pool;
    };

    template<typename T, execution::scheduler Sch, typename Function>
        requires std::invocable<Function&>
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto runAsync(Sch scheduler, Function function) -> Task<T>
    {
        co_return co_await execution::starts_on(std::move(scheduler),
                                                execution::just() | execution::then(std::move(function)));
    }

    namespace detail::exec
    {
        template<typename T>
        auto validate_task(const Task<T>& task) -> void
        {
            const auto handle{ task.getHandle() };
            if (!handle)
                throw std::logic_error{ "Cannot adapt an empty task" };
            auto mutex{ handle.promise().execution_mutex };
            std::unique_lock<ExecutionGate> lock;
            if (mutex)
                lock = std::unique_lock{ *mutex };
            if (handle.promise().started || handle.promise().waiter || handle.promise().consumed)
                throw std::logic_error{ "Only an unstarted task can be adapted" };
        }

        template<typename T>
        struct TaskFactory
        {
            Task<T> task;
            auto operator()(const std::stop_token& /*unused*/) -> Task<T> { return std::move(task); }
        };

        template<typename Factory>
        using FactoryTask = std::invoke_result_t<Factory&, std::stop_token>;

        template<typename Factory, typename Receiver>
        struct TaskState final
          : Work
          , std::enable_shared_from_this<TaskState<Factory, Receiver>>
        {
            using Value = typename FactoryTask<Factory>::value_type;
            using Stored = std::conditional_t<std::is_void_v<Value>, bool, Value>;
            struct ForwardStop
            {
                std::weak_ptr<TaskState> state;
                auto operator()() const noexcept -> void
                {
                    if (auto self{ state.lock() })
                        self->stop.request_stop();
                }
            };
            using Token = execution::stop_token_of_t<execution::env_of_t<Receiver>>;
            using StopCallback = execution::stop_callback_for_t<Token, ForwardStop>;

            TaskState(Factory function,
                      Receiver target,
                      ExecutionScheduler execution_target,
                      Scheduler legacy_target)
              : factory{ std::move(function) }
              , receiver{ std::move(target) }
              , scheduler{ std::move(execution_target) }
              , legacy{ std::move(legacy_target) }
            {
            }

            auto startOperation() noexcept -> void
            {
                try {
                    auto self{ this->shared_from_this() };
                    cancellation.emplace(execution::get_stop_token(execution::get_env(receiver)),
                                         ForwardStop{ self });
                    if (stop.stop_requested()) {
                        stopped = true;
                        complete();
                    }
                    else {
                        scheduler.submit(std::move(self));
                    }
                } catch (...) {
                    fail(std::current_exception());
                }
            }
            auto run() noexcept -> void override
            {
                try {
                    if (stop.stop_requested()) {
                        stopped = true;
                        complete();
                        return;
                    }
                    auto self{ this->shared_from_this() };
                    driver.emplace(runTask(self));
                    auto& promise{ driver->getHandle().promise() };
                    gate = promise.getExecutionMutex();
                    finish_action = std::make_unique<ExecutionGate::Action>([self] { self->complete(); });
                    promise.scheduler = legacy;
                    promise.execution_scheduler = scheduler;
                    promise.execution_stop = stop;
                    driver->resume();
                } catch (...) {
                    fail(std::current_exception());
                }
            }
            auto fail(std::exception_ptr failure) noexcept -> void override
            {
                if (completed.load())
                    return;
                error = failure;
                complete();
            }
            static auto runTask(std::shared_ptr<TaskState> self) -> Task<void>
            {
                try {
                    auto task{ std::invoke(required(self->factory), self->stop.get_token()) };
                    validate_task(task);
                    if constexpr (std::is_void_v<Value>)
                        co_await task;
                    else
                        self->value.emplace(co_await task);
                } catch (const OperationStopped&) {
                    self->stopped = true;
                } catch (...) {
                    self->error = std::current_exception();
                }
                self->gate->defer(std::move(self->finish_action));
            }
            auto complete() noexcept -> void
            {
                if (completed.exchange(true))
                    return;
                // Called after the driver releases its execution lock, before notifying the receiver.
                driver.reset();
                finish_action.reset();
                factory.reset();
                cancellation.reset();
                if (error)
                    execution::set_error(std::move(receiver), std::move(error));
                else if (stopped)
                    execution::set_stopped(std::move(receiver));
                else if constexpr (std::is_void_v<Value>)
                    execution::set_value(std::move(receiver));
                else {
                    if (!value.has_value())
                        std::terminate();
                    execution::set_value(std::move(receiver), std::move(*value));
                }
            }
            std::optional<Factory> factory;
            Receiver receiver;
            ExecutionScheduler scheduler;
            Scheduler legacy;
            std::stop_source stop;
            std::optional<StopCallback> cancellation;
            std::optional<Task<void>> driver;
            std::shared_ptr<ExecutionGate> gate;
            std::unique_ptr<ExecutionGate::Action> finish_action;
            std::optional<Stored> value;
            std::exception_ptr error;
            bool stopped{};
            std::atomic<bool> completed{ false };
        };

        template<typename T>
        struct TaskCompletions
        {
            using type = execution::completion_signatures<execution::set_value_t(T),
                                                          execution::set_error_t(std::exception_ptr),
                                                          execution::set_stopped_t()>;
        };
        template<>
        struct TaskCompletions<void>
        {
            using type = execution::completion_signatures<execution::set_value_t(),
                                                          execution::set_error_t(std::exception_ptr),
                                                          execution::set_stopped_t()>;
        };
        template<typename Factory>
        struct TaskSender
        {
            using sender_concept = execution::sender_tag;
            using completion_signatures =
              typename TaskCompletions<typename FactoryTask<Factory>::value_type>::type;
            Factory factory;
            ExecutionScheduler scheduler;
            Scheduler legacy;
            auto get_env() const noexcept -> ScheduleSender::Env { return { scheduler }; }
            template<typename Receiver>
            struct Operation
            {
                using operation_state_concept = execution::operation_state_tag;
                std::shared_ptr<TaskState<Factory, Receiver>> state;
                auto start() noexcept -> void
                {
                    auto self{ state };
                    self->startOperation();
                }
            };
            template<typename Receiver>
            auto connect(Receiver receiver) && -> Operation<Receiver>
            {
                return { std::make_shared<TaskState<Factory, Receiver>>(
                  std::move(factory), std::move(receiver), std::move(scheduler), std::move(legacy)) };
            }
        };
    }

    template<typename Factory>
        requires std::invocable<std::decay_t<Factory>&, std::stop_token> &&
                 requires { typename detail::exec::FactoryTask<std::decay_t<Factory>>::pnm_task_tag; }
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto asSender(Context& context, Factory&& factory)
    {
        auto scheduler{ context.getExecutionScheduler() };
        auto legacy{ context.getScheduler() };
        return detail::exec::TaskSender<std::decay_t<Factory>>{ std::forward<Factory>(factory),
                                                                std::move(scheduler),
                                                                std::move(legacy) };
    }

    template<typename T>
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto asSender(Context& context, Task<T> task)
    {
        detail::exec::validate_task(task);
        return asSender(context, detail::exec::TaskFactory<T>{ std::move(task) });
    }
#endif

    namespace detail
    {
        template<typename Rep, typename Period>
        auto sleep_deadline(std::chrono::duration<Rep, Period> duration,
                            std::chrono::steady_clock::time_point now)
          -> std::chrono::steady_clock::time_point
        {
            using Clock = std::chrono::steady_clock;
            using FloatingRep = std::conditional_t<std::is_floating_point_v<Rep>, Rep, long double>;
            using FloatingTicks = std::chrono::duration<FloatingRep, Clock::period>;
            // Avoid integral scaling overflow while retaining floating inputs' conversion precision.
            const auto ticks{ FloatingTicks{ duration }.count() };
            if (std::isnan(ticks)) {
                throw std::invalid_argument{ "Sleep duration must not be NaN" };
            }
            if (ticks <= 0) {
                return now;
            }
            const auto rounded{ std::ceil(ticks) };
            if (rounded >= static_cast<long double>(Clock::duration::max().count())) {
                return Clock::time_point::max();
            }
            const Clock::duration delay{ static_cast<Clock::rep>(rounded) };
            // Subtract the nonnegative delay from max before adding it to now.
            if (now >= Clock::time_point::max() - delay) {
                return Clock::time_point::max();
            }
            return now + delay;
        }
    }

    template<typename Rep, typename Period>
    auto sleep(std::chrono::duration<Rep, Period> duration) -> Task<void>
    {
        const auto now{ std::chrono::steady_clock::now() };
        const auto deadline{ detail::sleep_deadline(duration, now) };
        if (deadline > now) {
            co_await runAsync<void>([deadline] { std::this_thread::sleep_until(deadline); });
        }
    }

    enum class ChannelMode : uint8_t
    {
        Broadcast,
        LoadBalancer
    };

    namespace detail
    {
        template<typename T>
        struct ChannelWaiter
        {
            std::optional<T> result;
            std::exception_ptr exception;
            std::shared_ptr<Continuation> continuation;
            bool linked{};
            // The callback takes a shared waiter reference before dispatching. Destruction
            // of an awaiting task cannot join a callback blocked on its execution mutex.
            std::unique_ptr<std::stop_callback<std::function<void()>>> cancellation;
        };

        template<typename T>
        struct ChannelState
        {
            std::mutex mutex;
            std::deque<T> queue;
            std::list<std::shared_ptr<ChannelWaiter<T>>> waiters;
            bool closed{};
            ChannelMode mode{ ChannelMode::LoadBalancer };
        };

        template<typename T>
        class ChannelAwaiter
        {
          public:
            explicit ChannelAwaiter(std::shared_ptr<ChannelState<T>> state, std::stop_token stop = {})
              : m_state{ std::move(state) }
              , m_stop{ std::move(stop) }
            {
            }
            ~ChannelAwaiter()
            {
                if (m_waiter->continuation) {
                    m_waiter->continuation->cancel();
                }
                if (m_state) {
                    std::scoped_lock lock{ m_state->mutex };
                    if (m_waiter->linked) {
                        m_state->waiters.remove(m_waiter);
                    }
                }
            }
            ChannelAwaiter(const ChannelAwaiter&) = delete;
            auto operator=(const ChannelAwaiter&) -> ChannelAwaiter& = delete;
            ChannelAwaiter(ChannelAwaiter&&) = delete;
            auto operator=(ChannelAwaiter&&) -> ChannelAwaiter& = delete;

            auto await_ready() -> bool
            {
                if (!m_state) {
                    return true;
                }
                std::scoped_lock lock{ m_state->mutex };
                return takeReady();
            }
            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> handle) -> bool
            {
                if (m_stop.stop_possible()) {
                    m_waiter->cancellation = std::make_unique<std::stop_callback<std::function<void()>>>(
                      m_stop, [state{ std::weak_ptr{ m_state } }, waiter{ std::weak_ptr{ m_waiter } }] {
                        auto pending{ waiter.lock() };
                        auto channel{ state.lock() };
                        if (!pending || !channel) {
                            return;
                        }
                        {
                            std::scoped_lock lock{ channel->mutex };
                            if (!pending->linked) {
                                return;
                            }
                            channel->waiters.remove(pending);
                            pending->linked = false;
                        }
                        pending->continuation->dispatch();
                    });
                }
                std::scoped_lock lock{ m_state->mutex };
                if (takeReady()) {
                    return false;
                }
                m_waiter->continuation = std::make_shared<Continuation>(handle);
                m_state->waiters.push_back(m_waiter);
                m_waiter->linked = true;
                return true;
            }
            auto await_resume() -> std::optional<T>
            {
                if (m_waiter->continuation) {
                    m_waiter->continuation->rethrowError();
                }
                if (m_waiter->exception) {
                    std::rethrow_exception(m_waiter->exception);
                }
                return std::move(m_waiter->result);
            }

          private:
            auto takeReady() -> bool
            {
                if (m_stop.stop_requested()) {
                    return true;
                }
                if (!m_state->queue.empty()) {
                    m_waiter->result.emplace(std::move(m_state->queue.front()));
                    m_state->queue.pop_front();
                    return true;
                }
                return m_state->closed;
            }
            std::shared_ptr<ChannelState<T>> m_state;
            std::stop_token m_stop;
            std::shared_ptr<ChannelWaiter<T>> m_waiter{ std::make_shared<ChannelWaiter<T>>() };
        };

        template<typename T>
        auto push_channel(const std::shared_ptr<ChannelState<T>>& state, T value) -> void
        {
            if (!state) {
                throw std::logic_error{ "Cannot push to a moved-from channel" };
            }
            std::list<std::shared_ptr<ChannelWaiter<T>>> ready;
            {
                std::scoped_lock lock{ state->mutex };
                if (state->closed) {
                    return;
                }
                if (state->waiters.empty()) {
                    state->queue.push_back(std::move(value));
                    return;
                }
                auto deliver{ []<typename Value>(auto& waiter, Value&& message) {
                    waiter->linked = false;
                    try {
                        waiter->result.emplace(std::forward<Value>(message));
                    } catch (...) {
                        waiter->exception = std::current_exception();
                    }
                } };
                if (!std::copy_constructible<T> || state->mode == ChannelMode::LoadBalancer) {
                    ready.splice(ready.end(), state->waiters, state->waiters.begin());
                    deliver(ready.front(), std::move(value));
                }
                else {
                    ready.splice(ready.end(), state->waiters);
                    if constexpr (std::copy_constructible<T>) {
                        for (const auto& waiter : ready) {
                            deliver(waiter, value);
                        }
                    }
                }
            }
            for (const auto& waiter : ready) {
                waiter->continuation->dispatch();
            }
        }

        template<typename T>
        auto close_channel(const std::shared_ptr<ChannelState<T>>& state) -> void
        {
            if (!state) {
                return;
            }
            std::list<std::shared_ptr<ChannelWaiter<T>>> ready;
            {
                std::scoped_lock lock{ state->mutex };
                if (state->closed) {
                    return;
                }
                state->closed = true;
                ready.splice(ready.end(), state->waiters);
                for (const auto& waiter : ready) {
                    waiter->linked = false;
                }
            }
            for (const auto& waiter : ready) {
                waiter->continuation->dispatch();
            }
        }

        template<typename T>
        auto read_channel(std::shared_ptr<ChannelState<T>> state, std::stop_token stop = {})
          -> Task<std::optional<T>>
        {
            co_return co_await ChannelAwaiter<T>{ std::move(state), std::move(stop) };
        }

        class RawBinaryAwaiter
        {
          public:
            using Bytes = std::vector<std::byte>;
            RawBinaryAwaiter(std::shared_ptr<ChannelState<Bytes>> state, std::optional<Bytes>& destination)
              : m_awaiter{ std::move(state) }
              , m_destination{ &destination }
            {
            }
            auto await_ready() -> bool { return m_awaiter.await_ready(); }
            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> handle) -> bool
            {
                return m_awaiter.await_suspend(handle);
            }
            auto await_resume() -> void { *m_destination = m_awaiter.await_resume(); }

          private:
            ChannelAwaiter<Bytes> m_awaiter;
            std::optional<Bytes>* m_destination;
        };
    }

    template<typename T>
    class Channel
    {
      public:
        auto push(T value) -> void { detail::push_channel(m_state, std::move(value)); }
        auto close() -> void { detail::close_channel(m_state); }
        // Cancellation returns nullopt without closing the channel or consuming a queued value.
        auto next(std::stop_token stop = {}) -> Task<std::optional<T>>
        {
            return detail::read_channel(m_state, std::move(stop));
        }

      private:
        std::shared_ptr<detail::ChannelState<T>> m_state{ std::make_shared<detail::ChannelState<T>>() };
    };

    // Timer-backed, interruptible sleep. The context must be driven until this task finishes.
    // Returns false when canceled. No worker thread is created. Oversized deadlines saturate.
    template<typename Rep, typename Period>
    // The externally owned context must outlive this task; retain the reference-based API.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    auto sleep(Context& context, std::chrono::duration<Rep, Period> duration, std::stop_token stop = {})
      -> Task<bool>
    {
        if (stop.stop_requested()) {
            co_return false;
        }
        if (duration <= duration.zero()) {
            co_return true;
        }
        Channel<bool> elapsed;
        auto timer{ context.scheduleAt(detail::sleep_deadline(duration, std::chrono::steady_clock::now()),
                                       [elapsed]() mutable { elapsed.push(true); }) };
        co_return (co_await elapsed.next(std::move(stop))).value_or(false);
    }

    class RawBinaryChannel
    {
      public:
        using Bytes = std::vector<std::byte>;
        RawBinaryChannel() { m_state->mode = ChannelMode::Broadcast; }
        auto setMode(ChannelMode mode) -> void
        {
            if (!m_state) {
                throw std::logic_error{ "Cannot configure a moved-from channel" };
            }
            std::scoped_lock lock{ m_state->mutex };
            m_state->mode = mode;
        }
        auto push(Bytes bytes) -> void { detail::push_channel(m_state, std::move(bytes)); }
        auto close() -> void { detail::close_channel(m_state); }
        auto next(std::optional<Bytes>& destination) -> detail::RawBinaryAwaiter
        {
            return { m_state, destination };
        }

      private:
        std::shared_ptr<detail::ChannelState<Bytes>> m_state{
            std::make_shared<detail::ChannelState<Bytes>>()
        };
        template<typename T>
            requires(std::is_trivially_copyable_v<T> && !std::is_array_v<T>)
        friend class BinaryChannel;
    };

    template<typename T>
        requires(std::is_trivially_copyable_v<T> && !std::is_array_v<T>)
    class BinaryChannel
    {
      public:
        BinaryChannel() = default;
        BinaryChannel(const RawBinaryChannel& raw)
          : m_state{ raw.m_state }
        {
        }
        auto setMode(ChannelMode mode) -> void
        {
            if (m_state) {
                std::scoped_lock lock{ m_state->mutex };
                m_state->mode = mode;
            }
        }
        auto next() -> Task<std::optional<T>> { return read(m_state); }

      private:
        static auto read(std::shared_ptr<detail::ChannelState<RawBinaryChannel::Bytes>> state)
          -> Task<std::optional<T>>
        {
            auto bytes{ co_await detail::read_channel(std::move(state)) };
            if (!bytes || bytes->size() != sizeof(T)) {
                co_return std::nullopt;
            }
            std::array<std::byte, sizeof(T)> representation{};
            std::ranges::copy(*bytes, representation.begin());
            co_return std::bit_cast<T>(representation);
        }
        std::shared_ptr<detail::ChannelState<RawBinaryChannel::Bytes>> m_state;
    };

    namespace detail
    {
        template<typename Ex, typename Coro>
        auto co_spawn_impl(Ex* executor, Coro coro) -> DetachedTask
        {
            co_await std::invoke(std::move(coro), *executor);
        }
    }

    template<Executor Ex, std::invocable<Ex&> Coro>
    auto co_spawn(Ex& executor, Coro&& coro) -> void
    {
        auto task{ detail::co_spawn_impl(&executor, std::forward<Coro>(coro)) };
        static_cast<void>(task.getHandle().promise().getExecutionMutex());
        task.getHandle().promise().executor = &executor;
        task.getHandle().promise().scheduler = executor.getScheduler();
#if PNM_CORO_ENABLE_EXECUTION
        task.getHandle().promise().execution_scheduler = executor.getExecutionScheduler();
#endif
        executor.scheduleOwned(std::move(task));
    }

    // Transfer ownership of an unstarted task to the executor. Rejected or
    // abandoned launches release its frame, just like the callable overload.
    // Exceptions escaping the task retain the detached-task termination policy.
    template<Executor Ex>
    auto co_spawn(Ex& executor, Task<void> task) -> void
    {
        co_spawn(executor, [work{ std::move(task) }](Ex&) mutable { return std::move(work); });
    }

    // Coroutine protocol calls implicitly access static promise hooks through an instance.
    // NOLINTBEGIN(readability-static-accessed-through-instance)
    namespace detail::msg
    {
        using Access = pnm::msg::detail::MessagingAccess;
        using Connection = pnm::msg::detail::WakeupRegistration;
        using Clock = std::chrono::steady_clock;
        inline constexpr size_t DISPATCH_LIMIT{ 64 };
        using StopCallback = std::stop_callback<std::function<void()>>;
        template<typename T>
        auto engaged(std::optional<T>& value) noexcept -> T&
        {
            if (!value.has_value())
                std::terminate(); // Fully initialized before an adapter is activated, in all builds.
            return *value;
        }

        struct Cancellation : std::enable_shared_from_this<Cancellation>
        {
            static auto make(const std::stop_token& stop, std::function<void()> function)
              -> std::shared_ptr<Cancellation>
            {
                auto state{ std::make_shared<Cancellation>() };
                if (stop.stop_possible()) {
                    state->callback = std::make_unique<StopCallback>(
                      stop, [weak{ std::weak_ptr{ state } }, function{ std::move(function) }] {
                        if (auto keep_alive{ weak.lock() })
                            function();
                    });
                }
                return state;
            }
            std::unique_ptr<StopCallback> callback;
        };

        struct Node;

        struct Runtime : std::enable_shared_from_this<Runtime>
        {
            explicit Runtime(Context& executor)
              : context{ executor }
              , scheduler{ executor.getScheduler() }
            {
            }
            auto add(const std::shared_ptr<Node>& node) -> void;
            auto remove(Node* node) -> void;
            auto stop(bool immediately) -> void;
            auto checkOpen() const -> void
            {
                if (stopping.load())
                    throw std::logic_error{ "Coroutine messaging is stopping" };
            }
            static auto join(std::shared_ptr<Runtime> runtime) -> Task<void>
            {
                if (!runtime->stopping.load())
                    throw std::logic_error{ "Request stop before joining messaging" };
                co_await runtime->drained.next();
            }
            Context& context;
            Scheduler scheduler;
            std::atomic<bool> stopping{ false };
            std::mutex mutex;
            struct Entry
            {
                std::weak_ptr<Node> node;
                bool stop_sent{};
                bool close_sent{};
            };
            std::unordered_map<Node*, Entry> nodes;
            Channel<bool> drained;
        };

        struct Node : std::enable_shared_from_this<Node>
        {
            explicit Node(std::shared_ptr<Runtime> owner)
              : runtime{ std::move(owner) }
            {
            }
            virtual ~Node() { runtime->remove(this); }
            Node(const Node&) = delete;
            auto operator=(const Node&) -> Node& = delete;
            Node(Node&&) = delete;
            auto operator=(Node&&) -> Node& = delete;
            virtual auto drain() -> void = 0;
            virtual auto requestStop() -> void = 0;
            virtual auto close() -> void { requestStop(); }
            virtual auto failed(const std::exception_ptr& error) noexcept -> void = 0;

            auto wake() noexcept -> void
            {
                if (!enabled.load(std::memory_order_acquire) || retired.load() || queued.exchange(true))
                    return;
                try {
                    runtime->scheduler(dispatch(shared_from_this()));
                } catch (...) {
                    queued.store(false);
                    enabled.store(false);
                    failed(std::current_exception());
                }
            }
            auto activate() -> void
            {
                enabled.store(true, std::memory_order_release);
                wake(); // Includes readiness observed before notification registration.
            }
            template<typename Handle>
            auto observe(Handle& handle) -> void
            {
                connection = Access::watch(handle, [weak{ weak_from_this() }]() noexcept {
                    if (auto node{ weak.lock() })
                        node->wake();
                });
            }
            auto arm(Clock::time_point deadline) -> void
            {
                timer = runtime->context.scheduleAt(deadline, [weak{ weak_from_this() }] {
                    if (auto node{ weak.lock() })
                        node->wake();
                });
            }
            auto cancelTimer() -> void
            {
                std::scoped_lock lock{ lifecycle };
                timer.cancel();
            }
            auto retire() -> void
            {
                if (retired.exchange(true))
                    return;
                {
                    std::scoped_lock lock{ lifecycle };
                    connection.reset();
                    timer.cancel();
                }
                runtime->remove(this);
            }
            auto storeError(const std::exception_ptr& error) -> void
            {
                std::scoped_lock lock{ error_mutex };
                if (!exception)
                    exception = error;
            }
            auto rethrowError() -> void
            {
                std::exception_ptr error;
                {
                    std::scoped_lock lock{ error_mutex };
                    error = exception;
                }
                if (error)
                    std::rethrow_exception(error);
            }
            std::shared_ptr<Runtime> runtime;
            std::atomic<bool> retired{ false };

          public:
            // NOLINTBEGIN(readability-static-accessed-through-instance)
            static auto dispatch(std::shared_ptr<Node> node) -> DetachedTask
            {
                node->queued.store(false);
                if (!node->retired.load()) {
                    try {
                        node->drain();
                    } catch (...) {
                        node->enabled.store(false);
                        node->failed(std::current_exception());
                    }
                }
                co_return;
            }
            // NOLINTEND(readability-static-accessed-through-instance)
            std::atomic<bool> enabled{ false };
            std::atomic<bool> queued{ false };
            Connection connection;
            Context::Timer timer;
            std::mutex lifecycle;
            std::mutex error_mutex;
            std::exception_ptr exception;
        };

        inline auto Runtime::add(const std::shared_ptr<Node>& node) -> void
        {
            std::scoped_lock lock{ mutex };
            checkOpen();
            nodes.emplace(node.get(), Entry{ .node{ node } });
        }
        inline auto Runtime::remove(Node* node) -> void
        {
            bool complete{};
            {
                std::scoped_lock lock{ mutex };
                if (auto found{ nodes.find(node) }; found != nodes.end()) {
                    nodes.erase(found);
                }
                complete = stopping.load() && nodes.empty();
            }
            if (complete)
                drained.close();
        }
        inline auto Runtime::stop(bool immediately) -> void
        {
            {
                std::scoped_lock lock{ mutex };
                stopping.store(true);
            }
            // Shutdown does not allocate. Mark one registration at a time, retaining its
            // strong reference until after unlocking; callbacks may remove registrations.
            for (;;) {
                std::shared_ptr<Node> next;
                {
                    std::scoped_lock lock{ mutex };
                    auto found{ std::ranges::find_if(nodes, [immediately](const auto& item) {
                        return immediately ? !item.second.close_sent : !item.second.stop_sent;
                    }) };
                    if (found == nodes.end())
                        break;
                    found->second.stop_sent = true;
                    if (immediately)
                        found->second.close_sent = true;
                    next = found->second.node.lock();
                }
                if (next) {
                    if (immediately)
                        next->close();
                    else
                        next->requestStop();
                }
            }
            remove(nullptr);
        }

        // One value plus a close-only wakeup channel; no second payload queue is introduced.
        template<typename T>
        class Completion
        {
            struct State
            {
                std::mutex mutex;
                std::optional<T> value;
                bool finished{};
                Channel<bool> signal;
            };

          public:
            auto push(T value) -> void
            {
                {
                    std::scoped_lock lock{ m_state->mutex };
                    if (m_state->finished)
                        return;
                    m_state->value.emplace(std::move(value));
                    m_state->finished = true;
                }
                m_state->signal.close();
            }
            auto close() -> void
            {
                {
                    std::scoped_lock lock{ m_state->mutex };
                    m_state->finished = true;
                }
                m_state->signal.close();
            }
            auto next() -> Task<std::optional<T>> { return read(m_state); }

          private:
            static auto read(std::shared_ptr<State> state) -> Task<std::optional<T>>
            {
                co_await state->signal.next();
                std::scoped_lock lock{ state->mutex };
                co_return std::move(state->value);
            }
            std::shared_ptr<State> m_state{ std::make_shared<State>() };
        };

        template<typename T>
        struct TopicRead
        {
            Completion<T> output;
            std::exception_ptr error;
        };

        template<typename T>
        struct TopicNode final : Node
        {
            TopicNode(std::shared_ptr<Runtime> owner, pnm::msg::Subscription<T> subscription)
              : Node{ std::move(owner) }
              , native{ std::move(subscription) }
            {
            }
            auto drain() -> void override
            {
                std::shared_ptr<TopicRead<T>> read;
                pnm::msg::detail::MessageBytes bytes;
                const auto closed{ Access::closed(native) };
                {
                    std::scoped_lock lock{ mutex };
                    if (waiting) {
                        bytes = Access::take(native);
                        if (closed || bytes)
                            read = std::exchange(waiting, {});
                    }
                }
                if (read) {
                    try {
                        if (bytes) {
                            T value{};
                            if (!utils::memory::deserialize(*bytes, value))
                                throw std::runtime_error{ "Failed to deserialize topic message" };
                            read->output.push(std::move(value));
                        }
                    } catch (...) {
                        read->error = std::current_exception();
                    }
                    read->output.close();
                }
                if (closed)
                    retire();
            }
            auto requestStop() -> void override
            {
                native.unsubscribe();
                wake();
            }
            auto failed(const std::exception_ptr& error) noexcept -> void override
            {
                storeError(error);
                native.unsubscribe();
                std::shared_ptr<TopicRead<T>> read;
                {
                    std::scoped_lock lock{ mutex };
                    read = std::exchange(waiting, {});
                }
                if (read) {
                    read->error = error;
                    read->output.close();
                }
                retire();
            }
            auto cancel(const std::shared_ptr<TopicRead<T>>& read) -> void
            {
                bool removed{};
                {
                    std::scoped_lock lock{ mutex };
                    if (waiting == read) {
                        waiting.reset();
                        removed = true;
                    }
                }
                if (removed)
                    read->output.close();
            }
            pnm::msg::Subscription<T> native;
            std::mutex mutex;
            std::shared_ptr<TopicRead<T>> waiting;
            std::atomic_flag reading;
        };

        template<typename T>
        struct ReadLease
        {
            ReadLease(std::shared_ptr<TopicNode<T>> owner, std::shared_ptr<TopicRead<T>> value)
              : node{ std::move(owner) }
              , read{ std::move(value) }
            {
            }
            ReadLease(const ReadLease&) = delete;
            auto operator=(const ReadLease&) -> ReadLease& = delete;
            ReadLease(ReadLease&&) = delete;
            auto operator=(ReadLease&&) -> ReadLease& = delete;

            std::shared_ptr<TopicNode<T>> node;
            std::shared_ptr<TopicRead<T>> read;
            ~ReadLease()
            {
                node->cancel(read);
                node->reading.clear();
            }
        };

        template<typename T>
        auto next_topic(std::shared_ptr<TopicNode<T>> node, std::stop_token stop) -> Task<std::optional<T>>
        {
            auto read{ std::make_shared<TopicRead<T>>() };
            if (node->reading.test_and_set())
                throw std::logic_error{ "A subscription already has a reader" };
            ReadLease<T> lease{ node, read };
            {
                std::scoped_lock lock{ node->mutex };
                // Register atomically with drain()/failed(), which can retire a closed node.
                node->rethrowError();
                if (stop.stop_requested() || Access::closed(node->native))
                    co_return std::nullopt;
                node->waiting = read;
            }
            auto cancellation{ Cancellation::make(stop, [node, read] { node->cancel(read); }) };
            node->wake();
            auto value{ co_await read->output.next() };
            if (read->error)
                std::rethrow_exception(read->error);
            co_return value;
        }

        template<typename R>
        struct CallNode final : Node
        {
            CallNode(std::shared_ptr<Runtime> owner, pnm::msg::PendingCall<R> call)
              : Node{ std::move(owner) }
              , native{ std::move(call) }
            {
            }
            auto drain() -> void override
            {
                if (!native.ready())
                    return;
                output.push(native.get());
                output.close();
                retire();
            }
            auto requestStop() -> void override
            {
                native.cancel();
                wake();
            }
            auto failed(const std::exception_ptr& error) noexcept -> void override
            {
                storeError(error);
                native.cancel();
                output.close();
                retire();
            }
            pnm::msg::PendingCall<R> native;
            Completion<pnm::msg::ServiceResult<R>> output;
        };

        template<typename NodeType>
        struct CancelLease
        {
            explicit CancelLease(std::shared_ptr<NodeType> owner)
              : node{ std::move(owner) }
            {
            }
            CancelLease(const CancelLease&) = delete;
            auto operator=(const CancelLease&) -> CancelLease& = delete;
            CancelLease(CancelLease&&) = delete;
            auto operator=(CancelLease&&) -> CancelLease& = delete;

            std::shared_ptr<NodeType> node;
            ~CancelLease()
            {
                if (!node->retired.load())
                    node->requestStop();
            }
        };

        template<typename Q, typename R, typename Rep, typename Period>
        auto request(std::shared_ptr<Runtime> runtime,
                     pnm::msg::Service<Q, R> service,
                     Q value,
                     std::chrono::duration<Rep, Period> timeout,
                     std::stop_token stop) -> Task<pnm::msg::ServiceResult<R>>
        {
            if (!runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            runtime->checkOpen();
            auto node{ std::make_shared<CallNode<R>>(runtime, service.request(value, timeout, stop)) };
            CancelLease<CallNode<R>> lease{ node };
            try {
                node->observe(node->native);
                node->arm(Access::deadline(node->native));
                runtime->add(node);
                node->activate();
            } catch (...) {
                node->failed(std::current_exception());
            }
            auto result{ co_await node->output.next() };
            node->rethrowError();
            if (!result)
                throw std::runtime_error{ "Service completion was abandoned" };
            co_return std::move(*result);
        }
    }

    template<utils::memory::Serializable T>
    class Subscription
    {
        using State = detail::msg::TopicNode<T>;

      public:
        explicit Subscription(std::shared_ptr<State> state)
          : m_state{ std::move(state) }
        {
        }
        ~Subscription() { unsubscribe(); }
        Subscription(const Subscription&) = delete;
        auto operator=(const Subscription&) -> Subscription& = delete;
        Subscription(Subscription&&) noexcept = default;
        auto operator=(Subscription&& other) noexcept -> Subscription&
        {
            if (this != &other) {
                unsubscribe();
                m_state = std::move(other.m_state);
            }
            return *this;
        }
        auto next(std::stop_token stop = {}) -> Task<std::optional<T>>
        {
            check();
            return detail::msg::next_topic(m_state, std::move(stop));
        }
        auto latest() -> void
        {
            check();
            detail::msg::Access::replay(m_state->native);
        }
        auto statistics() const -> pnm::msg::SubscriptionStatistics
        {
            check();
            return m_state->native.statistics();
        }
        auto unsubscribe() -> void
        {
            if (m_state)
                m_state->requestStop();
        }

      private:
        auto check() const -> void
        {
            if (!m_state)
                throw std::logic_error{ "Subscription was moved from" };
        }
        std::shared_ptr<State> m_state;
    };

    template<utils::memory::Serializable T>
    class Topic
    {
      public:
        Topic(std::shared_ptr<detail::msg::Runtime> runtime, pnm::msg::Topic<T> topic)
          : m_runtime{ std::move(runtime) }
          , m_native{ std::move(topic) }
        {
        }
        auto publish(const T& value) const -> bool
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            return m_native.publish(value);
        }
        auto setPublishOnlyOnChange(bool enabled, pnm::msg::detail::MessageEqual<T> equal = {}) const -> void
            requires std::default_initializable<T>
        {
            m_native.setPublishOnlyOnChange(enabled, std::move(equal));
        }
        [[nodiscard]] auto subscribe(pnm::msg::SubscriptionOptions options = {}) const -> Subscription<T>
            requires std::default_initializable<T> && std::move_constructible<T>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            auto node{ std::make_shared<detail::msg::TopicNode<T>>(
              m_runtime, m_native.subscribe([](const T&) {}, options)) };
            try {
                node->observe(node->native);
                m_runtime->add(node);
                node->activate();
                node->rethrowError();
            } catch (...) {
                node->failed(std::current_exception());
                throw;
            }
            return Subscription<T>{ std::move(node) };
        }

      private:
        std::shared_ptr<detail::msg::Runtime> m_runtime;
        pnm::msg::Topic<T> m_native;
    };

    template<typename Feedback>
    struct ActionCallbacks
    {
        std::function<void()> on_accepted{ nullptr };
        std::function<void(const Feedback&)> on_feedback{ nullptr };
    };

    namespace detail::msg
    {
        template<typename F, typename R>
        struct GoalNode final : Node
        {
            GoalNode(std::shared_ptr<Runtime> owner, ActionCallbacks<F> handlers)
              : Node{ std::move(owner) }
              , callbacks{ std::move(handlers) }
            {
            }
            auto drain() -> void override
            {
                detail::msg::engaged(native).poll();
                if (Access::accepted(detail::msg::engaged(native)))
                    cancelTimer();
                if (terminal.load())
                    retire();
            }
            template<typename Function>
            auto progress(Function&& function) -> void
            {
                if (progress_failed || abandoned.load() || forced.load())
                    return;
                try {
                    std::invoke(std::forward<Function>(function));
                } catch (...) {
                    progress_failed = true;
                    storeError(std::current_exception());
                    detail::msg::engaged(native).requestCancel();
                    ready.store(true);
                    output.close();
                }
            }
            auto abandon() -> void
            {
                abandoned.store(true);
                requestStop();
            }
            auto complete(pnm::msg::ActionResult<R> result) -> void
            {
                terminal.store(true);
                // Only the winner publishes or closes the completion. A concurrent close
                // must not finish the output while this result is still being moved into it.
                if (!ready.exchange(true))
                    output.push(std::move(result));
            }
            auto requestStop() -> void override
            {
                detail::msg::engaged(native).requestCancel();
                wake();
            }
            auto close() -> void override
            {
                detail::msg::engaged(native).requestCancel();
                forced.store(true);
                // close() is abandonment, never confirmation that remote cleanup finished.
                if (!ready.exchange(true))
                    output.push(
                      pnm::msg::ActionResult<R>{ std::unexpect, pnm::msg::ActionError::Unavailable });
                retire();
            }
            auto failed(const std::exception_ptr& error) noexcept -> void override
            {
                storeError(error);
                detail::msg::engaged(native).requestCancel();
                forced.store(true);
                ready.store(true);
                output.close();
                retire();
            }
            std::optional<pnm::msg::PendingGoal<F, R>> native;
            ActionCallbacks<F> callbacks;
            Completion<pnm::msg::ActionResult<R>> output;
            std::atomic<bool> ready{ false };
            std::atomic<bool> terminal{ false };
            std::atomic<bool> forced{ false };
            std::atomic<bool> abandoned{ false };
            bool progress_failed{}; // Context thread only.
            std::mutex consumption;
            bool waiting{};
            bool consumed{};
        };

        template<typename F, typename R>
        struct ResultLease
        {
            explicit ResultLease(std::shared_ptr<GoalNode<F, R>> owner)
              : node{ std::move(owner) }
            {
            }
            ResultLease(const ResultLease&) = delete;
            auto operator=(const ResultLease&) -> ResultLease& = delete;
            ResultLease(ResultLease&&) = delete;
            auto operator=(ResultLease&&) -> ResultLease& = delete;

            std::shared_ptr<GoalNode<F, R>> node;
            bool consumed{};
            ~ResultLease()
            {
                {
                    std::scoped_lock lock{ node->consumption };
                    node->waiting = false;
                    node->consumed = consumed;
                }
                if (!consumed)
                    node->requestStop();
            }
        };

        template<typename F, typename R>
        auto action_result(std::shared_ptr<GoalNode<F, R>> node, std::stop_token stop)
          -> Task<pnm::msg::ActionResult<R>>
        {
            {
                std::scoped_lock lock{ node->consumption };
                if (node->waiting || node->consumed)
                    throw std::logic_error{ "Action result has one consumer" };
                node->waiting = true;
            }
            ResultLease<F, R> lease{ node };
            auto cancellation{ Cancellation::make(stop, [node] { node->requestStop(); }) };
            auto result{ co_await node->output.next() };
            lease.consumed = true;
            node->rethrowError();
            if (!result)
                throw std::runtime_error{ "Action completion was abandoned" };
            co_return std::move(*result);
        }

        struct ProviderNode;
        struct JobSlot
        {
            JobSlot(const JobSlot&) = delete;
            auto operator=(const JobSlot&) -> JobSlot& = delete;
            JobSlot(JobSlot&&) = delete;
            auto operator=(JobSlot&&) -> JobSlot& = delete;

            explicit JobSlot(std::shared_ptr<ProviderNode> owner)
              : provider{ std::move(owner) }
            {
            }
            ~JobSlot();
            std::shared_ptr<ProviderNode> provider;
        };
        struct ProviderNode : Node
        {
            ProviderNode(std::shared_ptr<Runtime> owner, size_t limit)
              : Node{ std::move(owner) }
              , max_active{ limit }
            {
            }
            auto reserve() -> std::shared_ptr<JobSlot>
            {
                std::scoped_lock lock{ jobs_mutex };
                if (stopping.load() || active >= max_active)
                    return {};
                auto slot{ std::make_shared<JobSlot>(
                  std::static_pointer_cast<ProviderNode>(shared_from_this())) };
                ++active;
                return slot;
            }
            auto track(const std::shared_ptr<Node>& job) -> void
            {
                bool stop{};
                {
                    std::scoped_lock lock{ jobs_mutex };
                    std::erase_if(jobs, [](const auto& weak) { return weak.expired(); });
                    stop = stopping.load();
                    if (!stop)
                        jobs.emplace_back(job);
                }
                if (stop)
                    job->requestStop();
            }
            auto stopJobs() -> void
            {
                std::vector<std::weak_ptr<Node>> snapshot;
                {
                    std::scoped_lock lock{ jobs_mutex };
                    stopping.store(true);
                    snapshot.swap(jobs);
                }
                // The native registration already handles fail-versus-graceful semantics.
                // All local jobs need the same cooperative stop request in either case.
                for (const auto& weak : snapshot) {
                    if (auto job{ weak.lock() })
                        job->requestStop();
                }
                release(false);
            }
            auto release(bool slot) -> void
            {
                bool complete{};
                {
                    std::scoped_lock lock{ jobs_mutex };
                    if (slot)
                        --active;
                    complete = stopping.load() && active == 0;
                }
                if (complete) {
                    drained.close();
                    retire();
                }
            }
            static auto join(std::shared_ptr<ProviderNode> provider) -> Task<void>
            {
                if (!provider->stopping.load())
                    throw std::logic_error{ "Request stop before joining a provider" };
                co_await provider->drained.next();
            }
            std::atomic<bool> stopping{ false };
            Channel<bool> drained;

          public:
            std::mutex jobs_mutex;
            std::vector<std::weak_ptr<Node>> jobs;
            size_t active{};
            const size_t max_active;
        };
        inline JobSlot::~JobSlot() { provider->release(true); }

        struct JobNode : Node
        {
            using Node::Node;
            auto finish() -> void
            {
                task = {};
                retire();
                slot.reset();
            }
            Task<void> task;
            std::shared_ptr<JobSlot> slot;
            std::shared_ptr<void> callable; // Coroutine lambdas borrow their callable object.
        };
        struct JobLease
        {
            explicit JobLease(std::shared_ptr<JobNode> owner)
              : job{ std::move(owner) }
            {
            }
            ~JobLease()
            {
                if (job)
                    job->finish();
            }
            JobLease(const JobLease&) = delete;
            auto operator=(const JobLease&) -> JobLease& = delete;
            JobLease(JobLease&&) noexcept = default;
            auto operator=(JobLease&&) -> JobLease& = delete;
            std::shared_ptr<JobNode> job;
        };
        // The scheduled frame owns the registry job even before it starts. Abandoning a queued
        // launch retires its child frame and releases its provider slot through JobLease.
        // NOLINTBEGIN(readability-static-accessed-through-instance)
        inline auto launch(JobLease lease) -> DetachedTask
        {
            try {
                co_await lease.job->task;
            } catch (...) {
                lease.job->failed(std::current_exception());
            }
        }
        // NOLINTEND(readability-static-accessed-through-instance)
        inline auto start(const std::shared_ptr<JobNode>& job) -> void
        {
            auto ticket{ launch(JobLease{ job }) };
            ticket.getHandle().promise().scheduler = job->runtime->scheduler;
            ticket.getHandle().promise().executor = &job->runtime->context;
#if PNM_CORO_ENABLE_EXECUTION
            ticket.getHandle().promise().execution_scheduler = job->runtime->context.getExecutionScheduler();
#endif
            try {
                job->runtime->scheduler(std::move(ticket));
            } catch (...) {
                job->failed(std::current_exception());
            }
        }

        template<typename R>
        struct ServiceJob final : JobNode
        {
            ServiceJob(std::shared_ptr<Runtime> owner, pnm::msg::Reply<R> reply)
              : JobNode{ std::move(owner) }
              , native{ std::move(reply) }
            {
            }
            auto drain() -> void override
            {
                if (!native.pending()) {
                    cancellation.request_stop();
                    cancelTimer();
                }
            }
            auto requestStop() -> void override { cancellation.request_stop(); }
            auto close() -> void override
            {
                native.fail(pnm::msg::ServiceError::Unavailable);
                requestStop();
            }
            auto failed(const std::exception_ptr& /*error*/) noexcept -> void override
            {
                native.fail(pnm::msg::ServiceError::HandlerFailed);
                requestStop();
            }
            pnm::msg::Reply<R> native;
            std::stop_source cancellation;
        };
        template<typename R>
        auto run_service(std::shared_ptr<ServiceJob<R>> job, Task<R> work) -> Task<void>
        {
            try {
                job->native.respond(co_await work);
            } catch (...) {
                job->native.fail(pnm::msg::ServiceError::HandlerFailed);
            }
        }

        template<typename F, typename R>
        struct ActionControl
        {
            explicit ActionControl(pnm::msg::ActionExecution<F, R> execution)
              : native{ std::move(execution) }
            {
            }
            pnm::msg::ActionExecution<F, R> native;
            std::stop_source cancellation;
        };
        template<typename F, typename R>
        struct ActionJob final : JobNode
        {
            ActionJob(std::shared_ptr<Runtime> owner, pnm::msg::ActionExecution<F, R> execution)
              : JobNode{ std::move(owner) }
              , control{ std::make_shared<ActionControl<F, R>>(std::move(execution)) }
            {
            }
            auto drain() -> void override
            {
                if (control->native.cancelRequested())
                    control->cancellation.request_stop();
            }
            auto requestStop() -> void override
            {
                control->native.requestCancel();
                control->cancellation.request_stop();
            }
            auto close() -> void override
            {
                control->native.fail(pnm::msg::ActionError::Unavailable);
                requestStop();
            }
            auto failed(const std::exception_ptr& /*error*/) noexcept -> void override
            {
                control->native.fail(pnm::msg::ActionError::HandlerFailed);
                requestStop();
            }
            std::shared_ptr<ActionControl<F, R>> control;
        };
        template<typename F, typename R>
        auto run_action(std::shared_ptr<ActionJob<F, R>> job, Task<pnm::msg::ActionCompletion<R>> work)
          -> Task<void>
        {
            try {
                auto result{ co_await work };
                switch (result.status) {
                    case pnm::msg::ActionStatus::Succeeded:
                        job->control->native.succeed(result.value);
                        break;
                    case pnm::msg::ActionStatus::Aborted:
                        job->control->native.abort(result.value);
                        break;
                    case pnm::msg::ActionStatus::Cancelled:
                        job->control->native.cancelled(result.value);
                        break;
                    default:
                        job->control->native.fail(pnm::msg::ActionError::HandlerFailed);
                        break;
                }
            } catch (...) {
                job->control->native.fail(pnm::msg::ActionError::HandlerFailed);
            }
        }
    }

    template<utils::memory::Serializable F, utils::memory::Serializable R>
    class PendingGoal
    {
        using State = detail::msg::GoalNode<F, R>;

      public:
        explicit PendingGoal(std::shared_ptr<State> state)
          : m_state{ std::move(state) }
        {
        }
        ~PendingGoal()
        {
            if (m_state)
                m_state->abandon();
        }
        PendingGoal(const PendingGoal&) = delete;
        auto operator=(const PendingGoal&) -> PendingGoal& = delete;
        PendingGoal(PendingGoal&&) noexcept = default;
        auto operator=(PendingGoal&& other) noexcept -> PendingGoal&
        {
            if (this != &other) {
                if (m_state)
                    m_state->abandon();
                m_state = std::move(other.m_state);
            }
            return *this;
        }
        auto id() const -> pnm::msg::GoalId
        {
            return m_state ? detail::msg::engaged(m_state->native).id() : 0;
        }
        auto ready() const -> bool { return m_state && m_state->ready.load(); }
        auto requestCancel() -> bool
        {
            return m_state && detail::msg::engaged(m_state->native).requestCancel();
        }
        auto result(std::stop_token stop = {}) -> Task<pnm::msg::ActionResult<R>>
        {
            if (!m_state)
                throw std::logic_error{ "Goal was moved from" };
            return detail::msg::action_result(m_state, std::move(stop));
        }

      private:
        std::shared_ptr<State> m_state;
    };

    template<utils::memory::Serializable F, utils::memory::Serializable R>
    class ActionExecution
    {
      public:
        explicit ActionExecution(std::shared_ptr<detail::msg::ActionControl<F, R>> control)
          : m_control{ std::move(control) }
        {
        }
        auto id() const -> pnm::msg::GoalId { return m_control ? m_control->native.id() : 0; }
        auto feedback(const F& value) -> bool { return m_control && m_control->native.feedback(value); }
        auto stopToken() const -> std::stop_token
        {
            return m_control ? m_control->cancellation.get_token() : std::stop_token{};
        }
        auto cancelRequested() const -> bool { return m_control && m_control->native.cancelRequested(); }

      private:
        std::shared_ptr<detail::msg::ActionControl<F, R>> m_control;
    };

    namespace detail::msg
    {
        template<typename Q, typename R>
        struct ServiceProviderNode final : ProviderNode
        {
            using ProviderNode::ProviderNode;
            auto drain() -> void override
            {
                Access::poll(detail::msg::engaged(native), DISPATCH_LIMIT);
                if (Access::queued(detail::msg::engaged(native)))
                    wake();
            }
            auto requestStop() -> void override
            {
                detail::msg::engaged(native).close();
                stopJobs();
            }
            auto close() -> void override
            {
                detail::msg::engaged(native).close();
                stopJobs();
            }
            auto failed(const std::exception_ptr& error) noexcept -> void override
            {
                storeError(error);
                close();
            }
            std::optional<pnm::msg::ServiceServer<Q, R>> native;
        };
        template<typename G, typename F, typename R>
        struct ActionProviderNode final : ProviderNode
        {
            using ProviderNode::ProviderNode;
            auto drain() -> void override
            {
                Access::poll(detail::msg::engaged(native), DISPATCH_LIMIT);
                if (Access::queued(detail::msg::engaged(native)))
                    wake();
            }
            auto requestStop() -> void override
            {
                detail::msg::engaged(native).requestStop();
                stopJobs();
            }
            auto close() -> void override
            {
                detail::msg::engaged(native).close();
                stopJobs();
            }
            auto failed(const std::exception_ptr& error) noexcept -> void override
            {
                storeError(error);
                close();
            }
            std::optional<pnm::msg::ActionServer<G, F, R>> native;
        };

        template<typename State>
        class Server
        {
          public:
            explicit Server(std::shared_ptr<State> state)
              : m_state{ std::move(state) }
            {
            }
            ~Server() { close(); }
            Server(const Server&) = delete;
            auto operator=(const Server&) -> Server& = delete;
            Server(Server&&) noexcept = default;
            auto operator=(Server&& other) noexcept -> Server&
            {
                if (this != &other) {
                    close();
                    m_state = std::move(other.m_state);
                }
                return *this;
            }
            auto requestStop() -> void
            {
                if (m_state)
                    m_state->requestStop();
            }
            auto close() -> void
            {
                if (m_state)
                    m_state->close();
            }
            auto join() -> Task<void>
            {
                if (!m_state)
                    throw std::logic_error{ "Provider was moved from" };
                return ProviderNode::join(m_state);
            }

          private:
            std::shared_ptr<State> m_state;
        };
    }

    template<typename Q, typename R>
    using ServiceServer = detail::msg::Server<detail::msg::ServiceProviderNode<Q, R>>;
    template<typename G, typename F, typename R>
    using ActionServer = detail::msg::Server<detail::msg::ActionProviderNode<G, F, R>>;

    template<utils::memory::Serializable Q, utils::memory::Serializable R>
    class Service
    {
        using Runtime = detail::msg::Runtime;
        using Provider = detail::msg::ServiceProviderNode<Q, R>;
        using Access = detail::msg::Access;

      public:
        Service(std::shared_ptr<Runtime> runtime, pnm::msg::Service<Q, R> service)
          : m_runtime{ std::move(runtime) }
          , m_native{ std::move(service) }
        {
        }
        template<typename Rep, typename Period>
        auto request(Q value, std::chrono::duration<Rep, Period> timeout, std::stop_token stop = {}) const
          -> Task<pnm::msg::ServiceResult<R>>
        {
            return detail::msg::request(m_runtime, m_native, std::move(value), timeout, std::move(stop));
        }
        template<typename Handler>
            requires std::is_invocable_r_v<Task<R>, Handler&, Q, std::stop_token>
        [[nodiscard]] auto serve(Handler&& handler, pnm::msg::ServiceOptions options = {}) const
          -> ServiceServer<Q, R>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            auto function{ std::make_shared<std::function<Task<R>(Q, std::stop_token)>>(
              pnm::msg::detail::own_function<Task<R>(Q, std::stop_token)>(std::forward<Handler>(handler))) };
            if (!*function)
                throw std::invalid_argument{ "A service requires a coroutine handler" };
            auto provider{ std::make_shared<Provider>(m_runtime, options.max_pending) };
            provider->native.emplace(Access::serve(
              m_native, [weak{ std::weak_ptr{ provider } }, function](Q& request, pnm::msg::Reply<R> reply) {
                auto owner{ weak.lock() };
                auto slot{ owner ? owner->reserve() : nullptr };
                if (!slot) {
                    reply.fail(owner && !owner->stopping.load() ? pnm::msg::ServiceError::Busy
                                                                : pnm::msg::ServiceError::Unavailable);
                    return;
                }
                auto job{ std::make_shared<detail::msg::ServiceJob<R>>(owner->runtime, std::move(reply)) };
                job->slot = std::move(slot);
                job->callable = function;
                auto work{ std::invoke(*function, std::move(request), job->cancellation.get_token()) };
                try {
                    job->observe(job->native);
                    job->arm(Access::deadline(job->native));
                    job->task = detail::msg::run_service(job, std::move(work));
                    owner->runtime->add(job);
                    owner->track(job);
                    job->activate();
                    detail::msg::start(job);
                } catch (...) {
                    job->failed(std::current_exception());
                    job->finish();
                }
            }, options));
            try {
                provider->observe(detail::msg::engaged(provider->native));
                m_runtime->add(provider);
                provider->activate();
                provider->rethrowError();
            } catch (...) {
                provider->close();
                throw;
            }
            return ServiceServer<Q, R>{ std::move(provider) };
        }

      private:
        std::shared_ptr<Runtime> m_runtime;
        pnm::msg::Service<Q, R> m_native;
    };

    template<utils::memory::Serializable G, utils::memory::Serializable F, utils::memory::Serializable R>
    class Action
    {
        using Runtime = detail::msg::Runtime;
        using Access = detail::msg::Access;
        using Provider = detail::msg::ActionProviderNode<G, F, R>;

      public:
        Action(std::shared_ptr<Runtime> runtime, pnm::msg::Action<G, F, R> action)
          : m_runtime{ std::move(runtime) }
          , m_native{ std::move(action) }
        {
        }
        [[nodiscard]] auto sendGoal(const G& value,
                                    pnm::msg::ActionGoalOptions options,
                                    ActionCallbacks<F> callbacks = {}) const -> PendingGoal<F, R>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            auto node{ std::make_shared<detail::msg::GoalNode<F, R>>(m_runtime, std::move(callbacks)) };
            auto weak{ std::weak_ptr{ node } };
            node->native.emplace(m_native.sendGoal(value,
                                                   options,
                                                   { .on_accepted{ [weak] {
                if (auto state{ weak.lock() })
                    state->progress([&] {
                        if (state->callbacks.on_accepted)
                            state->callbacks.on_accepted();
                    });
            } },
                                                     .on_feedback{ [weak](const F& feedback) {
                if (auto state{ weak.lock() })
                    state->progress([&] {
                        if (state->callbacks.on_feedback)
                            state->callbacks.on_feedback(feedback);
                    });
            } },
                                                     .on_result{ [weak](pnm::msg::ActionResult<R> result) {
                if (auto state{ weak.lock() })
                    state->complete(std::move(result));
            } } }));
            try {
                node->observe(detail::msg::engaged(node->native));
                node->arm(Access::deadline(detail::msg::engaged(node->native)));
                node->runtime->add(node);
                node->activate();
            } catch (...) {
                node->failed(std::current_exception());
            }
            return PendingGoal<F, R>{ std::move(node) };
        }
        template<typename Handler>
            requires std::invocable<Handler&, G, ActionExecution<F, R>>
        [[nodiscard]] auto serve(Handler&& handler, pnm::msg::ActionOptions options = {}) const
          -> ActionServer<G, F, R>
        {
            using Work = Task<pnm::msg::ActionCompletion<R>>;
            using Produced = std::invoke_result_t<Handler&, G, ActionExecution<F, R>>;
            static_assert(
              std::same_as<Produced, Work> ||
                std::same_as<Produced, std::expected<Work, pnm::msg::ActionError>>,
              "An action handler returns Task<ActionCompletion<Result>>, optionally wrapped in expected");
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            auto function{ std::make_shared<std::function<Produced(G, ActionExecution<F, R>)>>(
              pnm::msg::detail::own_function<Produced(G, ActionExecution<F, R>)>(
                std::forward<Handler>(handler))) };
            if (!*function)
                throw std::invalid_argument{ "An action requires a coroutine handler" };
            auto provider{ std::make_shared<Provider>(m_runtime, options.max_goals) };
            provider->native.emplace(Access::serve(m_native,
                                                   [weak{ std::weak_ptr{ provider } }, function](
                                                     G& goal, pnm::msg::ActionExecution<F, R> execution) {
                auto owner{ weak.lock() };
                auto slot{ owner ? owner->reserve() : nullptr };
                if (!slot) {
                    execution.fail(owner && !owner->stopping.load() ? pnm::msg::ActionError::Busy
                                                                    : pnm::msg::ActionError::Unavailable);
                    return;
                }
                auto job{ std::make_shared<detail::msg::ActionJob<F, R>>(owner->runtime,
                                                                         std::move(execution)) };
                job->slot = std::move(slot);
                job->callable = function;
                auto produced{ std::invoke(
                  *function, std::move(goal), ActionExecution<F, R>{ job->control }) };
                Work work;
                if constexpr (std::same_as<Produced, Work>)
                    work = std::move(produced);
                else {
                    if (!produced) {
                        job->control->native.fail(produced.error());
                        return;
                    }
                    work = std::move(*produced);
                }
                if (!work.getHandle()) {
                    job->control->native.fail(pnm::msg::ActionError::HandlerFailed);
                    return;
                }
                try {
                    job->observe(job->control->native);
                    owner->runtime->add(job);
                    owner->track(job);
                    if (!job->control->native.accept()) {
                        job->finish();
                        return;
                    }
                    job->task = detail::msg::run_action(job, std::move(work));
                    job->activate();
                    detail::msg::start(job);
                } catch (...) {
                    job->failed(std::current_exception());
                    job->finish();
                }
            },
                                                   options));
            try {
                provider->observe(detail::msg::engaged(provider->native));
                m_runtime->add(provider);
                provider->activate();
                provider->rethrowError();
            } catch (...) {
                provider->close();
                throw;
            }
            return ActionServer<G, F, R>{ std::move(provider) };
        }

      private:
        std::shared_ptr<Runtime> m_runtime;
        pnm::msg::Action<G, F, R> m_native;
    };

    class Bus
    {
      public:
        Bus(Context& context, pnm::msg::Bus& bus)
          : m_runtime{ std::make_shared<detail::msg::Runtime>(context) }
          , m_bus{ bus }
        {
        }
        ~Bus() { close(); }
        Bus(const Bus&) = delete;
        auto operator=(const Bus&) -> Bus& = delete;
        Bus(Bus&&) = delete;
        auto operator=(Bus&&) -> Bus& = delete;
        template<std::meta::info Member>
        auto topic() -> Topic<typename pnm::msg::TopicMetadata<Member>::Message>
        {
            using Metadata = pnm::msg::TopicMetadata<Member>;
            return topic<typename Metadata::Message>(Metadata::name);
        }
        template<utils::memory::Serializable T>
        auto topic(std::string_view name) -> Topic<T>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            return Topic<T>{ m_runtime, m_bus.topic<T>(name) };
        }
        template<std::meta::info Member>
        auto service() -> Service<typename pnm::msg::ServiceMetadata<Member>::Request,
                                  typename pnm::msg::ServiceMetadata<Member>::Response>
        {
            using Metadata = pnm::msg::ServiceMetadata<Member>;
            return service<typename Metadata::Request, typename Metadata::Response>(Metadata::name);
        }
        template<utils::memory::Serializable Q, utils::memory::Serializable R>
        auto service(std::string_view name) -> Service<Q, R>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            return Service<Q, R>{ m_runtime, m_bus.service<Q, R>(name) };
        }
        template<utils::memory::Serializable G, utils::memory::Serializable F, utils::memory::Serializable R>
        auto action(std::string_view name) -> Action<G, F, R>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            return Action<G, F, R>{ m_runtime, m_bus.action<G, F, R>(name) };
        }
        template<std::meta::info Member>
        auto action() -> Action<typename pnm::msg::ActionMetadata<Member>::Goal,
                                typename pnm::msg::ActionMetadata<Member>::Feedback,
                                typename pnm::msg::ActionMetadata<Member>::Result>
        {
            using Metadata = pnm::msg::ActionMetadata<Member>;
            return action<typename Metadata::Goal, typename Metadata::Feedback, typename Metadata::Result>(
              Metadata::name);
        }
        auto requestStop() -> void { m_runtime->stop(false); }
        auto close() -> void { m_runtime->stop(true); }
        auto join() -> Task<void> { return detail::msg::Runtime::join(m_runtime); }

      private:
        std::shared_ptr<detail::msg::Runtime> m_runtime;
        pnm::msg::Bus& m_bus;
    };
    // NOLINTEND(readability-static-accessed-through-instance)
}
