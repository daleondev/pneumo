#pragma once

#include "pneumo/common.hpp"

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pnm::coro
{
    // ---------- Context ----------

    class IExecutor
    {
      public:
        virtual ~IExecutor() = default;
        virtual auto run() -> void = 0;
        virtual auto stop() -> void = 0;
        virtual auto schedule(std::coroutine_handle<> handle) -> void = 0;
        virtual auto getLifeToken() -> std::weak_ptr<void> = 0;

      protected:
        IExecutor() = default;
        IExecutor(const IExecutor&) = default;
        auto operator=(const IExecutor&) -> IExecutor& = default;
        IExecutor(IExecutor&&) noexcept = default;
        auto operator=(IExecutor&&) noexcept -> IExecutor& = default;
    };

    template<typename T>
    concept Executor = std::is_base_of_v<IExecutor, T>;

    class Context : public IExecutor
    {
      public:
        Context() = default;
        ~Context() override = default;
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        auto run() -> void override
        {
            while (true) {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });

                if (!m_running && m_queue.empty()) {
                    break;
                }

                while (auto handle{ utils::queue::pop(m_queue) }) {
                    lock.unlock();
                    if (*handle && !handle->done()) {
                        handle->resume();
                    }
                    lock.lock();
                }
            }
        }

        auto stop() -> void override
        {
            std::scoped_lock lock{ m_mutex };
            m_running = false;
            m_cv.notify_all();
        }

        auto schedule(std::coroutine_handle<> handle) -> void override
        {
            utils::queue::push(m_queue, handle, m_mutex);
            m_cv.notify_one();
        }

        auto getLifeToken() -> std::weak_ptr<void> override { return m_lifeToken; }

      private:
        bool m_running{ true };
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<std::coroutine_handle<>> m_queue;
        std::shared_ptr<bool> m_lifeToken{ std::make_shared<bool>(true) };
    };

    // ---------- Task ----------

    namespace detail
    {
        struct DetachedTaskPromise;
        template<typename T>
        struct TaskPromise;
    }

    template<typename T>
    class Task;

    class DetachedTask
    {
      public:
        using promise_type = detail::DetachedTaskPromise;
        using handle_type = std::coroutine_handle<promise_type>;

        DetachedTask(handle_type handle)
          : m_handle{ handle }
        {
        }
        ~DetachedTask() = default;

        DetachedTask(const DetachedTask&) = delete;
        auto operator=(const DetachedTask&) -> DetachedTask& = delete;
        DetachedTask(DetachedTask&& other) noexcept
          : m_handle{ std::exchange(other.m_handle, nullptr) }
        {
        }
        auto operator=(DetachedTask&& other) noexcept -> DetachedTask&
        {
            m_handle = std::exchange(other.m_handle, nullptr);
            return *this;
        }

        auto getHandle() const -> const handle_type& { return m_handle; }

      private:
        handle_type m_handle;
    };

    template<typename T>
    class Task
    {
      public:
        using promise_type = detail::TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        Task(handle_type handle)
          : m_handle{ handle }
        {
        }
        ~Task()
        {
            if (m_handle)
                m_handle.destroy();
        }

        Task(const Task&) = delete;
        auto operator=(const Task&) -> Task& = delete;

        Task(Task&& other) noexcept
          : m_handle{ std::exchange(other.m_handle, nullptr) }
        {
        }
        auto operator=(Task&& other) noexcept -> Task&
        {
            if (this != &other) {
                if (m_handle)
                    m_handle.destroy();
                m_handle = std::exchange(other.m_handle, nullptr);
            }
            return *this;
        }

        auto await_ready() const noexcept -> bool { return !m_handle || m_handle.done(); }
        auto await_suspend(std::coroutine_handle<> waiter) noexcept -> std::coroutine_handle<>
        {
            m_handle.promise().waiter = waiter;
            return m_handle;
        }
        auto await_resume() -> T
        {
            if (m_handle.promise().exception)
                std::rethrow_exception(m_handle.promise().exception);
            if constexpr (!std::is_void_v<T>) {
                auto& value{ m_handle.promise().value };
                if (!value) {
                    throw std::bad_optional_access{};
                }
                return std::move(*value);
            }
        }

        auto getHandle() const -> const handle_type& { return m_handle; }

      private:
        handle_type m_handle;
    };

    namespace detail
    {
        struct DetachedTaskPromise
        {
            auto get_return_object() -> DetachedTask;
            static auto initial_suspend() -> std::suspend_always { return {}; }
            static auto final_suspend() noexcept -> std::suspend_never { return {}; }
            static auto unhandled_exception() -> void { std::terminate(); }
            static auto return_void() -> void {}
        };

        template<typename T>
        struct TaskPromiseBase
        {
            std::coroutine_handle<> waiter;
            std::exception_ptr exception;
            IExecutor* executor{ nullptr };

            static auto initial_suspend() -> std::suspend_always { return {}; }
            static auto final_suspend() noexcept
            {
                struct Awaiter
                {
                    static auto await_ready() noexcept -> bool { return false; }
                    static auto await_suspend(std::coroutine_handle<TaskPromise<T>> h) noexcept
                      -> std::coroutine_handle<>
                    {
                        return h.promise().waiter ? h.promise().waiter : std::noop_coroutine();
                    }
                    static auto await_resume() noexcept -> void {}
                };
                return Awaiter{};
            }
            auto unhandled_exception() -> void { exception = std::current_exception(); }
            template<typename U>
            auto await_transform(Task<U>&& child_task) -> Task<U>&&
            {
                return std::move(child_task);
            }
            template<typename U>
            auto await_transform(U&& task) -> U&&
            {
                return std::forward<U>(task);
            }
        };

        template<typename T = void>
        struct TaskPromise : public TaskPromiseBase<T>
        {
            auto get_return_object() -> Task<T> { return Task<T>::handle_type::from_promise(*this); }
            std::optional<T> value{};
            auto return_value(T val) -> void { value.emplace(std::move(val)); }
        };

        template<>
        struct TaskPromise<void> : public TaskPromiseBase<void>
        {
            auto get_return_object() -> Task<void> { return Task<void>::handle_type::from_promise(*this); }
            static auto return_void() -> void {}
        };

        inline auto DetachedTaskPromise::get_return_object() -> DetachedTask
        {
            return { std::coroutine_handle<DetachedTaskPromise>::from_promise(*this) };
        }
    }

    template<typename T>
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto runAsync(auto func) -> Task<T>
    {
        struct Awaiter
        {
            std::decay_t<decltype(func)> f;
            std::optional<std::conditional_t<std::is_void_v<T>, bool, T>> result{};
            std::exception_ptr exception;

            static auto await_ready() -> bool { return false; }
            void await_suspend(std::coroutine_handle<> h)
            {
                std::thread([this, h]() mutable {
                    try {
                        if constexpr (std::is_void_v<T>)
                            f();
                        else
                            result.emplace(f());
                    } catch (...) {
                        exception = std::current_exception();
                    }
                    h.resume();
                }).detach();
            }
            T await_resume()
            {
                if (exception) {
                    std::rethrow_exception(exception);
                }
                if constexpr (!std::is_void_v<T>) {
                    if (!result) {
                        throw std::bad_optional_access{};
                    }
                    return std::move(*result);
                }
            }
        };
        co_return co_await Awaiter{ .f = std::move(func), .result = {}, .exception = {} };
    }

    template<typename Rep, typename Period>
    auto sleep(std::chrono::duration<Rep, Period> duration) -> Task<void>
    {
        struct Awaiter
        {
            std::chrono::duration<Rep, Period> d;
            bool await_ready() { return d.count() <= 0; }
            void await_suspend(std::coroutine_handle<> h)
            {
                std::thread([this, h]() {
                    std::this_thread::sleep_for(d);
                    h.resume();
                }).detach();
            }
            static auto await_resume() -> void {}
        };
        co_await Awaiter{ duration };
        co_return;
    }

    // ---------- Channel ----------

    namespace detail
    {
        struct RawBinaryAwaiter;
    }

    enum class ChannelMode : uint8_t
    {
        Broadcast,
        LoadBalancer
    };

    class RawBinaryChannel
    {
      public:
        using Bytes = std::vector<std::byte>;

        auto setMode(ChannelMode mode) -> void
        {
            std::scoped_lock lock(m_state->mutex);
            m_state->mode = mode;
        }

        auto push(Bytes raw) -> void;
        auto close() -> void;

        auto next(std::optional<Bytes>& dest) -> detail::RawBinaryAwaiter;

      protected:
        struct Waiter
        {
            std::coroutine_handle<> handle;
            IExecutor* executor{ nullptr };
            // For broadcast, we need a place to put the result
            std::optional<Bytes>* result_dest{ nullptr };
            std::weak_ptr<void> life_token;
            detail::RawBinaryAwaiter* awaiter_ptr{ nullptr };
        };

        struct State
        {
            std::mutex mutex;
            std::deque<Bytes> queue;
            bool closed{ false };
            std::list<Waiter> waiters;
            ChannelMode mode{ ChannelMode::Broadcast };
        };

      private:
        std::shared_ptr<State> m_state{ std::make_shared<State>() };

        friend struct detail::RawBinaryAwaiter;
        template<typename T>
            requires std::is_trivially_copyable_v<T>
        friend class BinaryChannel;
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    class BinaryChannel
    {
      public:
        BinaryChannel() = default;
        BinaryChannel(const RawBinaryChannel& raw)
          : m_state(raw.m_state)
        {
        }

        auto setMode(ChannelMode mode) -> void
        {
            if (m_state) {
                std::scoped_lock lock(m_state->mutex);
                m_state->mode = mode;
            }
        }

        auto next() -> Task<std::optional<T>>;

      private:
        std::shared_ptr<RawBinaryChannel::State> m_state;
    };

    template<typename T>
    class Channel
    {
      private:
        struct Waiter
        {
            std::coroutine_handle<> handle;
            IExecutor* executor{ nullptr };
            std::optional<T>* dest{ nullptr };
            std::weak_ptr<void> life_token;
        };

        struct State
        {
            std::mutex mutex;
            std::deque<T> queue;
            std::list<Waiter> waiters;
            bool closed{ false };
        };

        struct Awaiter
        {
            std::shared_ptr<State> state;
            std::optional<T> result;

            explicit Awaiter(std::shared_ptr<State> shared_state)
              : state{ std::move(shared_state) }
            {
            }
            Awaiter(const Awaiter&) = delete;
            auto operator=(const Awaiter&) -> Awaiter& = delete;
            Awaiter(Awaiter&&) = delete;
            auto operator=(Awaiter&&) -> Awaiter& = delete;

            ~Awaiter()
            {
                std::scoped_lock lock{ state->mutex };
                std::erase_if(state->waiters, [this](const Waiter& waiter) { return waiter.dest == &result; });
            }

            auto await_ready() -> bool
            {
                std::scoped_lock lock(state->mutex);
                if (!state->queue.empty()) {
                    result = std::move(state->queue.front());
                    state->queue.pop_front();
                    return true;
                }
                return state->closed;
            }

            template<typename P>
            auto await_suspend(std::coroutine_handle<P> h) -> bool
            {
                std::scoped_lock lock(state->mutex);
                if (auto value{ utils::queue::pop(state->queue) }) {
                    result.emplace(std::move(*value));
                    return false;
                }
                if (state->closed) {
                    return false;
                }

                IExecutor* ex = nullptr;
                std::weak_ptr<void> token;
                if constexpr (requires { h.promise().executor; }) {
                    ex = h.promise().executor;
                    if (ex) {
                        token = ex->getLifeToken();
                    }
                }

                state->waiters.push_back({ .handle = h,
                                           .executor = ex,
                                           .dest = &result,
                                           .life_token = std::move(token) });
                return true;
            }

            auto await_resume() -> std::optional<T> { return std::move(result); }
        };

      public:
        auto push(T val) -> void
        {
            std::unique_lock lock(m_state->mutex);
            if (m_state->closed) {
                return;
            }

            if (m_state->waiters.empty()) {
                m_state->queue.push_back(std::move(val));
                return;
            }

            auto waiter = std::move(m_state->waiters.front());
            m_state->waiters.pop_front();

            if (waiter.dest) {
                *waiter.dest = std::move(val);
            }

            lock.unlock();

            if (waiter.executor) {
                if (auto token = waiter.life_token.lock()) {
                    waiter.executor->schedule(waiter.handle);
                }
            }
            else {
                waiter.handle.resume();
            }
        }

        auto close() -> void
        {
            std::unique_lock lock(m_state->mutex);
            if (m_state->closed) {
                return;
            }
            m_state->closed = true;
            auto waiters = std::move(m_state->waiters);
            lock.unlock();

            for (auto& w : waiters) {
                if (w.executor) {
                    if (auto token = w.life_token.lock()) {
                        w.executor->schedule(w.handle);
                    }
                }
                else {
                    w.handle.resume();
                }
            }
        }

        auto next() -> Task<std::optional<T>> { co_return co_await Awaiter{ m_state }; }

      private:
        std::shared_ptr<State> m_state{ std::make_shared<State>() };
    };

    namespace detail
    {
        struct RawBinaryAwaiter
        {
            std::shared_ptr<RawBinaryChannel::State> state;
            std::optional<RawBinaryChannel::Bytes>* dest;
            std::optional<std::list<RawBinaryChannel::Waiter>::iterator> m_iterator;

            RawBinaryAwaiter(std::shared_ptr<RawBinaryChannel::State> shared_state,
                             std::optional<RawBinaryChannel::Bytes>& destination)
              : state{ std::move(shared_state) }
              , dest{ &destination }
            {
            }
            RawBinaryAwaiter(const RawBinaryAwaiter&) = delete;
            auto operator=(const RawBinaryAwaiter&) -> RawBinaryAwaiter& = delete;
            RawBinaryAwaiter(RawBinaryAwaiter&&) = delete;
            auto operator=(RawBinaryAwaiter&&) -> RawBinaryAwaiter& = delete;

            ~RawBinaryAwaiter()
            {
                std::scoped_lock lock(state->mutex);
                if (m_iterator) {
                    state->waiters.erase(*m_iterator);
                }
            }

            void unlink() { m_iterator = std::nullopt; }

            auto await_ready() const -> bool
            {
                std::scoped_lock lock(state->mutex);
                dest->reset();
                if (auto raw{ utils::queue::pop(state->queue) }) {
                    dest->emplace(std::move(*raw));
                    return true;
                }
                return state->closed;
            }

            template<typename P>
            auto await_suspend(std::coroutine_handle<P> handle) -> bool
            {
                std::scoped_lock lock(state->mutex);

                if (auto raw{ utils::queue::pop(state->queue) }) {
                    dest->emplace(std::move(*raw));
                    return false;
                }
                if (state->closed) {
                    return false;
                }

                IExecutor* executor{ nullptr };
                std::weak_ptr<void> life_token;

                if constexpr (requires { handle.promise().executor; }) {
                    executor = handle.promise().executor;
                    if (executor) {
                        life_token = executor->getLifeToken();
                    }
                }

                m_iterator = state->waiters.insert(state->waiters.end(),
                                                   { .handle = handle,
                                                     .executor = executor,
                                                     .result_dest = dest,
                                                     .life_token = std::move(life_token),
                                                     .awaiter_ptr = this });
                return true;
            }

            static auto await_resume() -> void
            {
                // Result is already in 'dest' or dest is nullopt (closed)
                // If we resumed normally, 'unlink()' was already called by 'push'.
            }
        };

        static auto resume_waiter(auto& waiter) -> void
        {
            if (waiter.executor) {
                if (auto token = waiter.life_token.lock()) {
                    waiter.executor->schedule(waiter.handle);
                }
            }
            else {
                waiter.handle.resume();
            }
        }
    }

    inline auto RawBinaryChannel::next(std::optional<Bytes>& dest) -> detail::RawBinaryAwaiter
    {
        return detail::RawBinaryAwaiter{ m_state, dest };
    }

    inline auto RawBinaryChannel::push(Bytes raw) -> void
    {
        std::unique_lock lock(m_state->mutex);
        if (m_state->closed) {
            return;
        }

        if (m_state->waiters.empty()) {
            // store until new waiter spawns
            m_state->queue.push_back(std::move(raw));
            return;
        }

        if (m_state->mode == ChannelMode::LoadBalancer) {
            auto waiter{ std::move(m_state->waiters.front()) };
            m_state->waiters.pop_front();

            // detach from awaiter so it doesnt try to erase itself on destruction
            if (waiter.awaiter_ptr) {
                waiter.awaiter_ptr->unlink();
            }

            if (waiter.result_dest) {
                waiter.result_dest->emplace(std::move(raw));
            }

            lock.unlock();
            detail::resume_waiter(waiter);
        }
        else {
            auto to_resume{ std::move(m_state->waiters) };
            m_state->waiters.clear();

            for (auto& waiter : to_resume) {
                // detach from awaiter so it doesnt try to erase itself on destruction
                if (waiter.awaiter_ptr) {
                    waiter.awaiter_ptr->unlink();
                }

                if (waiter.result_dest) {
                    waiter.result_dest->emplace(raw);
                }
            }

            lock.unlock();
            for (auto& waiter : to_resume) {
                detail::resume_waiter(waiter);
            }
        }
    }

    inline auto RawBinaryChannel::close() -> void
    {
        std::list<Waiter> to_resume;
        {
            std::scoped_lock lock(m_state->mutex);
            if (m_state->closed) {
                return;
            }
            m_state->closed = true;
            to_resume = std::move(m_state->waiters);
            for (auto& waiter : to_resume) {
                if (waiter.awaiter_ptr) {
                    waiter.awaiter_ptr->unlink();
                }
            }
        }

        for (auto& waiter : to_resume) {
            detail::resume_waiter(waiter);
        }
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    auto BinaryChannel<T>::next() -> Task<std::optional<T>>
    {
        if (!m_state) {
            co_return std::nullopt;
        }
        RawBinaryChannel raw{};
        raw.m_state = m_state;

        std::optional<RawBinaryChannel::Bytes> result{};
        co_await raw.next(result);

        if (!result || result->size() != sizeof(T)) {
            co_return std::nullopt;
        }

        T val{};
        std::memcpy(&val, result->data(), sizeof(T));
        co_return val;
    }

    // ---------- Spawn ----------

    namespace detail
    {
        template<typename Ex, typename Coro>
        auto co_spawn_impl(Ex* ex, Coro coro) -> DetachedTask
        {
            co_await std::invoke(std::move(coro), *ex);
        }
    }

    template<Executor Ex, std::invocable<Ex&> Coro>
    auto co_spawn(Ex& ex, Coro&& coro) -> void
    {
        auto detached{ detail::co_spawn_impl(&ex, std::forward<Coro>(coro)) };
        ex.schedule(detached.getHandle());
    }
}
