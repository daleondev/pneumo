#pragma once

#include "pneumo/coroutines.hpp"
#include "pneumo/messaging.hpp"

#include <cassert>
#include <exception>
#include <expected>
#include <unordered_map>

// Coroutine protocol calls implicitly access static promise hooks through an instance.
// NOLINTBEGIN(readability-static-accessed-through-instance)
namespace pnm::coro
{
    namespace detail::messaging
    {
        using Access = pnm::msg::detail::MessagingAccess;
        using Connection = pnm::msg::detail::WakeupRegistration;
        using Clock = std::chrono::steady_clock;
        inline constexpr size_t DISPATCH_LIMIT{ 64 };
        using StopCallback = std::stop_callback<std::function<void()>>;
        template<typename T>
        auto engaged(std::optional<T>& value) noexcept -> T&
        {
            assert(value.has_value()); // Fully initialized before an adapter is activated.
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
            node->rethrowError();
            if (stop.stop_requested() || Access::closed(node->native))
                co_return std::nullopt;
            {
                std::scoped_lock lock{ node->mutex };
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
        using State = detail::messaging::TopicNode<T>;

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
            return detail::messaging::next_topic(m_state, std::move(stop));
        }
        auto latest() -> void
        {
            check();
            detail::messaging::Access::replay(m_state->native);
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
        Topic(std::shared_ptr<detail::messaging::Runtime> runtime, pnm::msg::Topic<T> topic)
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
        [[nodiscard]] auto subscribe() const -> Subscription<T>
            requires std::default_initializable<T> && std::move_constructible<T>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            auto node{ std::make_shared<detail::messaging::TopicNode<T>>(
              m_runtime, m_native.subscribe([](const T&) {})) };
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
        std::shared_ptr<detail::messaging::Runtime> m_runtime;
        pnm::msg::Topic<T> m_native;
    };

    template<typename Feedback>
    struct ActionCallbacks
    {
        std::function<void()> on_accepted{ nullptr };
        std::function<void(const Feedback&)> on_feedback{ nullptr };
    };

    namespace detail::messaging
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
                detail::messaging::engaged(native).poll();
                if (Access::accepted(detail::messaging::engaged(native)))
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
                    detail::messaging::engaged(native).requestCancel();
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
                if (!ready.exchange(true) && !progress_failed && !forced.load())
                    output.push(std::move(result));
                output.close();
            }
            auto requestStop() -> void override
            {
                detail::messaging::engaged(native).requestCancel();
                wake();
            }
            auto close() -> void override
            {
                detail::messaging::engaged(native).requestCancel();
                forced.store(true);
                // close() is abandonment, never confirmation that remote cleanup finished.
                if (!ready.exchange(true))
                    output.push(
                      pnm::msg::ActionResult<R>{ std::unexpect, pnm::msg::ActionError::Unavailable });
                output.close();
                retire();
            }
            auto failed(const std::exception_ptr& error) noexcept -> void override
            {
                storeError(error);
                detail::messaging::engaged(native).requestCancel();
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
            using Node::Node;
            auto reserve() -> std::shared_ptr<JobSlot>
            {
                std::scoped_lock lock{ jobs_mutex };
                if (stopping.load())
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
        using State = detail::messaging::GoalNode<F, R>;

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
            return m_state ? detail::messaging::engaged(m_state->native).id() : 0;
        }
        auto ready() const -> bool { return m_state && m_state->ready.load(); }
        auto requestCancel() -> bool
        {
            return m_state && detail::messaging::engaged(m_state->native).requestCancel();
        }
        auto result(std::stop_token stop = {}) -> Task<pnm::msg::ActionResult<R>>
        {
            if (!m_state)
                throw std::logic_error{ "Goal was moved from" };
            return detail::messaging::action_result(m_state, std::move(stop));
        }

      private:
        std::shared_ptr<State> m_state;
    };

    template<utils::memory::Serializable F, utils::memory::Serializable R>
    class ActionExecution
    {
      public:
        explicit ActionExecution(std::shared_ptr<detail::messaging::ActionControl<F, R>> control)
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
        std::shared_ptr<detail::messaging::ActionControl<F, R>> m_control;
    };

    namespace detail::messaging
    {
        template<typename Q, typename R>
        struct ServiceProviderNode final : ProviderNode
        {
            using ProviderNode::ProviderNode;
            auto drain() -> void override
            {
                Access::poll(detail::messaging::engaged(native), DISPATCH_LIMIT);
                if (Access::queued(detail::messaging::engaged(native)))
                    wake();
            }
            auto requestStop() -> void override
            {
                detail::messaging::engaged(native).close();
                stopJobs();
            }
            auto close() -> void override
            {
                detail::messaging::engaged(native).close();
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
                Access::poll(detail::messaging::engaged(native), DISPATCH_LIMIT);
                if (Access::queued(detail::messaging::engaged(native)))
                    wake();
            }
            auto requestStop() -> void override
            {
                detail::messaging::engaged(native).requestStop();
                stopJobs();
            }
            auto close() -> void override
            {
                detail::messaging::engaged(native).close();
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
    using ServiceServer = detail::messaging::Server<detail::messaging::ServiceProviderNode<Q, R>>;
    template<typename G, typename F, typename R>
    using ActionServer = detail::messaging::Server<detail::messaging::ActionProviderNode<G, F, R>>;

    template<utils::memory::Serializable Q, utils::memory::Serializable R>
    class Service
    {
        using Runtime = detail::messaging::Runtime;
        using Provider = detail::messaging::ServiceProviderNode<Q, R>;
        using Access = detail::messaging::Access;

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
            return detail::messaging::request(
              m_runtime, m_native, std::move(value), timeout, std::move(stop));
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
            auto provider{ std::make_shared<Provider>(m_runtime) };
            provider->native.emplace(Access::serve(
              m_native, [weak{ std::weak_ptr{ provider } }, function](Q& request, pnm::msg::Reply<R> reply) {
                auto owner{ weak.lock() };
                auto slot{ owner ? owner->reserve() : nullptr };
                if (!slot) {
                    reply.fail(pnm::msg::ServiceError::Unavailable);
                    return;
                }
                auto job{ std::make_shared<detail::messaging::ServiceJob<R>>(owner->runtime,
                                                                             std::move(reply)) };
                job->slot = std::move(slot);
                job->callable = function;
                auto work{ std::invoke(*function, std::move(request), job->cancellation.get_token()) };
                try {
                    job->observe(job->native);
                    job->arm(Access::deadline(job->native));
                    job->task = detail::messaging::run_service(job, std::move(work));
                    owner->runtime->add(job);
                    owner->track(job);
                    job->activate();
                    detail::messaging::start(job);
                } catch (...) {
                    job->failed(std::current_exception());
                    job->finish();
                }
            }, options));
            try {
                provider->observe(detail::messaging::engaged(provider->native));
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
        using Runtime = detail::messaging::Runtime;
        using Access = detail::messaging::Access;
        using Provider = detail::messaging::ActionProviderNode<G, F, R>;

      public:
        Action(std::shared_ptr<Runtime> runtime, pnm::msg::Action<G, F, R> action)
          : m_runtime{ std::move(runtime) }
          , m_native{ std::move(action) }
        {
        }
        [[nodiscard]] auto sendGoal(G value,
                                    pnm::msg::ActionGoalOptions options,
                                    ActionCallbacks<F> callbacks = {}) const -> PendingGoal<F, R>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            auto node{ std::make_shared<detail::messaging::GoalNode<F, R>>(m_runtime, std::move(callbacks)) };
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
                node->observe(detail::messaging::engaged(node->native));
                node->arm(Access::deadline(detail::messaging::engaged(node->native)));
                m_runtime->add(node);
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
            auto provider{ std::make_shared<Provider>(m_runtime) };
            provider->native.emplace(Access::serve(m_native,
                                                   [weak{ std::weak_ptr{ provider } }, function](
                                                     G& goal, pnm::msg::ActionExecution<F, R> execution) {
                auto owner{ weak.lock() };
                auto slot{ owner ? owner->reserve() : nullptr };
                if (!slot) {
                    execution.fail(pnm::msg::ActionError::Unavailable);
                    return;
                }
                auto job{ std::make_shared<detail::messaging::ActionJob<F, R>>(owner->runtime,
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
                    job->task = detail::messaging::run_action(job, std::move(work));
                    job->activate();
                    detail::messaging::start(job);
                } catch (...) {
                    job->failed(std::current_exception());
                    job->finish();
                }
            },
                                                   options));
            try {
                provider->observe(detail::messaging::engaged(provider->native));
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
          : m_runtime{ std::make_shared<detail::messaging::Runtime>(context) }
          , m_bus{ bus }
        {
        }
        ~Bus() { close(); }
        Bus(const Bus&) = delete;
        auto operator=(const Bus&) -> Bus& = delete;
        Bus(Bus&&) = delete;
        auto operator=(Bus&&) -> Bus& = delete;
        template<utils::memory::Serializable T>
        auto topic(std::string_view name) -> Topic<T>
        {
            if (!m_runtime)
                throw std::logic_error{ "Endpoint was moved from" };
            m_runtime->checkOpen();
            return Topic<T>{ m_runtime, m_bus.topic<T>(name) };
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
        auto requestStop() -> void { m_runtime->stop(false); }
        auto close() -> void { m_runtime->stop(true); }
        auto join() -> Task<void> { return detail::messaging::Runtime::join(m_runtime); }

      private:
        std::shared_ptr<detail::messaging::Runtime> m_runtime;
        pnm::msg::Bus& m_bus;
    };
}

// NOLINTEND(readability-static-accessed-through-instance)
