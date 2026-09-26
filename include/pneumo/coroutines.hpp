#pragma once

#include "pneumo/common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pnm::coro
{
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
        struct DetachedTaskPromise;
        template<typename T>
        struct TaskPromise;

        struct InitialAwaiter
        {
            bool* started;
            static auto await_ready() noexcept -> bool { return false; }
            static auto await_suspend(std::coroutine_handle<> /*handle*/) noexcept -> void {}
            auto await_resume() const noexcept -> void { *started = true; }
        };

        struct PromiseBase
        {
            IExecutor* executor{ nullptr };
            Scheduler scheduler;
            std::shared_ptr<std::recursive_mutex> execution_mutex{ std::make_shared<std::recursive_mutex>() };
            bool started{};

            auto initial_suspend() noexcept -> InitialAwaiter { return { &started }; }
        };
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
            auto mutex{ m_handle.promise().execution_mutex };
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
            std::scoped_lock lock{ *handle.promise().execution_mutex };
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
                promise.execution_mutex = waiter.promise().execution_mutex;
                promise.executor = waiter.promise().executor;
                promise.scheduler = waiter.promise().scheduler;
            }
            promise.waiter = waiter;
            return handle;
        }

        static auto result(handle_type handle) -> T
        {
            if (!handle) {
                throw std::logic_error{ "Cannot await an empty task" };
            }
            std::scoped_lock lock{ *handle.promise().execution_mutex };
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
                std::scoped_lock lock{ *mutex };
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
        schedule(task.getHandle());
        static_cast<void>(task.release());
    }

    inline auto IExecutor::getScheduler() -> Scheduler
    {
        // Custom executors must outlive in-flight calls to their scheduling methods.
        return [this, token = getLifeToken()](DetachedTask task) {
            if (token.expired()) {
                throw std::runtime_error{ "Coroutine executor is no longer alive" };
            }
            scheduleOwned(std::move(task));
        };
    }

    class Context : public IExecutor
    {
        struct Work
        {
            std::coroutine_handle<> handle;
            DetachedTask owner;
        };
        struct State
        {
            std::mutex mutex;
            std::condition_variable wake;
            std::deque<Work> queue;
            bool stopped{};
        };

      public:
        Context() = default;
        ~Context() override
        {
            std::deque<Work> abandoned;
            {
                std::scoped_lock lock{ m_state->mutex };
                m_state->stopped = true;
                abandoned.swap(m_state->queue);
                m_state->wake.notify_all();
            }
            // Destroy unstarted owned frames after releasing the queue lock.
        }
        Context(const Context&) = delete;
        auto operator=(const Context&) -> Context& = delete;
        Context(Context&&) = delete;
        auto operator=(Context&&) -> Context& = delete;

        auto run() -> void override
        {
            auto state{ m_state };
            while (true) {
                Work work;
                {
                    std::unique_lock lock{ state->mutex };
                    state->wake.wait(lock, [&] { return state->stopped || !state->queue.empty(); });
                    if (state->queue.empty()) {
                        return;
                    }
                    work = std::move(state->queue.front());
                    state->queue.pop_front();
                }
                if (work.handle && !work.handle.done()) {
                    static_cast<void>(work.owner.release());
                    work.handle.resume();
                }
            }
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
                enqueue(m_state, Work{ .handle = handle, .owner = std::move(task) });
            }
        }
        auto getLifeToken() -> std::weak_ptr<void> override { return m_state; }
        auto getScheduler() -> Scheduler override
        {
            return [weak = std::weak_ptr<State>{ m_state }](DetachedTask task) {
                auto state{ weak.lock() };
                if (!state) {
                    throw std::runtime_error{ "Coroutine context is no longer alive" };
                }
                if (auto handle{ task.getHandle() }) {
                    enqueue(state, Work{ .handle = handle, .owner = std::move(task) });
                }
            };
        }

      private:
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
                    m_mutex = handle.promise().execution_mutex;
                    m_scheduler = handle.promise().scheduler;
                    if (!m_scheduler && handle.promise().executor) {
                        m_scheduler = handle.promise().executor->getScheduler();
                    }
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
            auto rethrowError() const -> void
            {
                if (m_exception) {
                    std::rethrow_exception(m_exception);
                }
            }

          private:
            std::coroutine_handle<> m_handle;
            std::shared_ptr<std::recursive_mutex> m_mutex{ std::make_shared<std::recursive_mutex>() };
            Scheduler m_scheduler;
            std::exception_ptr m_exception;
        };

        // Implicit coroutine calls access static promise hooks through the promise object.
        // NOLINTBEGIN(readability-static-accessed-through-instance)
        inline auto resume_later(std::shared_ptr<Continuation> continuation) -> DetachedTask
        {
            continuation->resume();
            co_return;
        }

        // NOLINTEND(readability-static-accessed-through-instance)

        inline auto Continuation::dispatch() -> void
        {
            auto self{ shared_from_this() };
            if (!m_scheduler) {
                resume();
                return;
            }
            try {
                m_scheduler(resume_later(self));
            } catch (...) {
                // A rejected delivery must complete with an error rather than strand the task.
                std::scoped_lock lock{ *m_mutex };
                m_exception = std::current_exception();
                resume();
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
                std::thread([state = m_state] {
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

    template<typename Rep, typename Period>
    auto sleep(std::chrono::duration<Rep, Period> duration) -> Task<void>
    {
        if (duration > duration.zero()) {
            co_await runAsync<void>([duration] { std::this_thread::sleep_for(duration); });
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
            explicit ChannelAwaiter(std::shared_ptr<ChannelState<T>> state)
              : m_state{ std::move(state) }
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
                if (!m_state->queue.empty()) {
                    m_waiter->result.emplace(std::move(m_state->queue.front()));
                    m_state->queue.pop_front();
                    return true;
                }
                return m_state->closed;
            }
            std::shared_ptr<ChannelState<T>> m_state;
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
                auto deliver = []<typename Value>(auto& waiter, Value&& message) {
                    waiter->linked = false;
                    try {
                        waiter->result.emplace(std::forward<Value>(message));
                    } catch (...) {
                        waiter->exception = std::current_exception();
                    }
                };
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
        auto read_channel(std::shared_ptr<ChannelState<T>> state) -> Task<std::optional<T>>
        {
            co_return co_await ChannelAwaiter<T>{ std::move(state) };
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
        auto next() -> Task<std::optional<T>> { return detail::read_channel(m_state); }

      private:
        std::shared_ptr<detail::ChannelState<T>> m_state{ std::make_shared<detail::ChannelState<T>>() };
    };

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
        task.getHandle().promise().executor = &executor;
        task.getHandle().promise().scheduler = executor.getScheduler();
        executor.scheduleOwned(std::move(task));
    }
}
