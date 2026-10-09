#pragma once

#include "common.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pnm::msg
{
    class Bus;

    template<utils::memory::Serializable T>
    class Topic;

    enum class ServiceError : std::uint8_t
    {
        Unavailable,
        Busy,
        Timeout,
        Cancelled,
        HandlerFailed,
        TransportError
    };

    template<typename Response>
    using ServiceResult = std::expected<Response, ServiceError>;

    struct ServiceOptions
    {
        // Includes queued requests and deferred replies that have not completed.
        static constexpr size_t DEFAULT_MAX_PENDING{ 64 };
        size_t max_pending{ DEFAULT_MAX_PENDING };
    };

    template<utils::memory::Serializable Response>
    class Reply;

    template<utils::memory::Serializable Response>
    class PendingCall;

    template<utils::memory::Serializable Request, utils::memory::Serializable Response>
    class ServiceServer;

    template<utils::memory::Serializable Request, utils::memory::Serializable Response>
    class Service;

    namespace detail
    {
        using MessageBytes = std::shared_ptr<const std::vector<std::byte>>;

        template<typename T>
        using MessageEqual = std::function<bool(const T&, const T&)>;

        class DispatchGuard
        {
          public:
            explicit DispatchGuard(std::atomic_flag& flag)
              : m_flag{ flag }
            {
                if (m_flag.test_and_set(std::memory_order_acquire)) {
                    throw std::logic_error{ "Concurrent or recursive dispatch is not allowed" };
                }
            }
            ~DispatchGuard() { m_flag.clear(std::memory_order_release); }
            DispatchGuard(const DispatchGuard&) = delete;
            auto operator=(const DispatchGuard&) -> DispatchGuard& = delete;
            DispatchGuard(DispatchGuard&&) = delete;
            auto operator=(DispatchGuard&&) -> DispatchGuard& = delete;

          private:
            std::atomic_flag& m_flag;
        };

        template<typename T>
        struct SubscriptionState
        {
            explicit SubscriptionState(std::function<void(const T&)> handler)
              : callback{ std::move(handler) }
            {
            }

            auto enqueue(const MessageBytes& bytes) -> void
            {
                std::scoped_lock lock{ mutex };
                if (!closed) {
                    messages.push_back(bytes);
                    latest = bytes;
                    ready.notify_one();
                }
            }

            auto close() -> void
            {
                std::scoped_lock lock{ mutex };
                closed = true;
                messages.clear();
                latest.reset();
                ready.notify_all();
            }

            std::mutex mutex;
            std::condition_variable ready;
            std::deque<MessageBytes> messages;
            MessageBytes latest;
            bool closed{};
            std::atomic_flag dispatching;
            std::function<void(const T&)> callback;
        };

        template<typename T>
        struct TopicState
        {
            ~TopicState()
            {
                for (const auto& weak : subscriptions) {
                    if (auto subscription{ weak.lock() }) {
                        subscription->close();
                    }
                }
            }

            TopicState() = default;
            TopicState(const TopicState&) = delete;
            auto operator=(const TopicState&) -> TopicState& = delete;
            TopicState(TopicState&&) = delete;
            auto operator=(TopicState&&) -> TopicState& = delete;

            std::mutex mutex;
            std::vector<std::weak_ptr<SubscriptionState<T>>> subscriptions;
            MessageBytes latest;
            std::shared_ptr<const MessageEqual<T>> equal;
        };
    }

    template<utils::memory::Serializable T>
    class Subscription
    {
      public:
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

        // Discard pending messages and wake a waiting poll. An in-flight callback may finish.
        auto unsubscribe() -> void
        {
            if (m_state) {
                m_state->close();
            }
        }

        auto poll() -> size_t { return poll(std::chrono::milliseconds::zero()); }

        // Invoke the callback with the latest publication, without consuming queued messages.
        // A late subscriber can explicitly retrieve the value published before subscribing.
        auto latest() -> void
        {
            auto state{ m_state };
            if (!state) {
                return;
            }
            detail::DispatchGuard dispatching{ state->dispatching };
            detail::MessageBytes bytes;
            {
                std::scoped_lock lock{ state->mutex };
                bytes = state->latest;
            }
            if (bytes) {
                T message{};
                if (!utils::memory::deserialize(*bytes, message)) {
                    throw std::runtime_error{ "Failed to deserialize topic message" };
                }
                state->callback(message);
            }
        }

        // Wait for the first message, then process the queue size observed on waking.
        // Callbacks run here, outside queue locks. Concurrent or recursive dispatching is rejected.
        template<typename Rep, typename Period>
        auto poll(const std::chrono::duration<Rep, Period>& timeout) -> size_t
        {
            auto state{ m_state };
            if (!state) {
                return 0;
            }
            detail::DispatchGuard dispatching{ state->dispatching };

            size_t pending{};
            {
                std::unique_lock lock{ state->mutex };
                if (timeout > std::chrono::duration<Rep, Period>::zero()) {
                    state->ready.wait_for(
                      lock, timeout, [&] { return state->closed || !state->messages.empty(); });
                }
                pending = state->messages.size();
            }

            size_t processed{};
            while (processed < pending) {
                detail::MessageBytes bytes;
                {
                    std::scoped_lock lock{ state->mutex };
                    if (state->closed || state->messages.empty()) {
                        break;
                    }
                    bytes = std::move(state->messages.front());
                    state->messages.pop_front();
                }
                T message{};
                if (!utils::memory::deserialize(*bytes, message)) {
                    throw std::runtime_error{ "Failed to deserialize topic message" };
                }
                state->callback(message);
                ++processed;
            }
            return processed;
        }

      private:
        friend class Topic<T>;
        explicit Subscription(std::shared_ptr<detail::SubscriptionState<T>> state)
          : m_state{ std::move(state) }
        {
        }

        std::shared_ptr<detail::SubscriptionState<T>> m_state;
    };

    template<utils::memory::Serializable T>
    class Topic
    {
        static_assert(std::same_as<T, std::remove_cvref_t<T>>,
                      "Topic messages must be unqualified value types");

      public:
        // Configuration is shared by all handles to this topic. Equality compares the current
        // value with the decoded last publication, avoiding comparisons of object padding.
        auto setPublishOnlyOnChange(bool enabled, detail::MessageEqual<T> equal = {}) const -> void
            requires std::default_initializable<T>
        {
            if (!m_state) {
                throw std::logic_error{ "Cannot configure a moved-from topic" };
            }
            if (enabled && !equal) {
                if constexpr (std::equality_comparable<T>) {
                    equal = std::equal_to<T>{};
                }
                else {
                    throw std::invalid_argument{ "Change detection requires operator== or a comparator" };
                }
            }
            auto comparator{ enabled ? std::make_shared<const detail::MessageEqual<T>>(std::move(equal))
                                     : nullptr };
            std::scoped_lock lock{ m_state->mutex };
            m_state->equal.swap(comparator);
        }

        // Return false when change detection suppresses this publication.
        auto publish(const T& message) const -> bool
        {
            if (!m_state) {
                throw std::logic_error{ "Cannot publish through a moved-from topic" };
            }
            auto bytes{ std::make_shared<const std::vector<std::byte>>(utils::memory::serialize(message)) };
            // Retain subscribers until after the topic lock is released, including their callbacks.
            std::vector<std::shared_ptr<detail::SubscriptionState<T>>> subscribers;
            for (;;) {
                detail::MessageBytes previous;
                std::shared_ptr<const detail::MessageEqual<T>> equal;
                std::unique_lock lock{ m_state->mutex };
                previous = m_state->latest;
                equal = m_state->equal;
                if constexpr (std::default_initializable<T>) {
                    if (previous && equal) {
                        // Adapters and comparators run outside locks. Retry if a concurrent
                        // publisher or configuration change invalidates the comparison.
                        lock.unlock();
                        const auto unchanged{ isUnchanged(previous, message, *equal) };
                        lock.lock();
                        if (m_state->latest != previous || m_state->equal != equal) {
                            continue;
                        }
                        if (unchanged) {
                            return false;
                        }
                    }
                }
                std::erase_if(m_state->subscriptions, [](const auto& weak) { return weak.expired(); });
                for (const auto& weak : m_state->subscriptions) {
                    if (auto subscriber{ weak.lock() }) {
                        subscribers.push_back(std::move(subscriber));
                    }
                }
                m_state->latest = bytes;
                // Serializing fan-out preserves one publication order across all subscribers.
                for (const auto& subscriber : subscribers) {
                    subscriber->enqueue(bytes);
                }
                return true;
            }
        }

        [[nodiscard]] auto subscribe(std::function<void(const T&)> callback) const -> Subscription<T>
            requires std::default_initializable<T>
        {
            if (!m_state) {
                throw std::logic_error{ "Cannot subscribe through a moved-from topic" };
            }
            if (!callback) {
                throw std::invalid_argument{ "A subscription requires a callback" };
            }
            auto state{ std::make_shared<detail::SubscriptionState<T>>(std::move(callback)) };
            {
                std::scoped_lock lock{ m_state->mutex };
                std::erase_if(m_state->subscriptions, [](const auto& weak) { return weak.expired(); });
                state->latest = m_state->latest;
                m_state->subscriptions.emplace_back(state);
            }
            return Subscription<T>{ std::move(state) };
        }

      private:
        friend class Bus;

        static auto isUnchanged(const detail::MessageBytes& previous,
                                const T& message,
                                const detail::MessageEqual<T>& equal) -> bool
            requires std::default_initializable<T>
        {
            T previous_message{};
            if (!utils::memory::deserialize(*previous, previous_message)) {
                throw std::runtime_error{ "Failed to deserialize topic message" };
            }
            return equal(previous_message, message);
        }

        explicit Topic(std::shared_ptr<detail::TopicState<T>> state)
          : m_state{ std::move(state) }
        {
        }

        std::shared_ptr<detail::TopicState<T>> m_state;
    };

    namespace detail
    {
        using ServiceClock = std::chrono::steady_clock;

        template<typename Rep, typename Period>
        auto service_deadline(const std::chrono::duration<Rep, Period>& timeout) -> ServiceClock::time_point
        {
            const auto now{ ServiceClock::now() };
            const auto ticks{ std::chrono::duration<long double, ServiceClock::period>{ timeout }.count() };
            if (std::isnan(ticks)) {
                throw std::invalid_argument{ "Service timeout cannot be NaN" };
            }
            if (ticks <= 0) {
                return now;
            }
            const auto rounded{ std::ceil(ticks) };
            if (rounded >= static_cast<long double>(ServiceClock::duration::max().count())) {
                return ServiceClock::time_point::max();
            }
            const ServiceClock::duration delay{ static_cast<ServiceClock::rep>(rounded) };
            if (now >= ServiceClock::time_point::max() - delay) {
                return ServiceClock::time_point::max();
            }
            return now + delay;
        }

        template<typename Response>
        using ServiceCompletion = std::function<void(ServiceResult<Response>)>;

        // The supported Clang libc++ lacks move_only_function. Preserve move-only captures
        // by giving the erased function shared ownership of its callable when necessary.
        template<typename Signature, typename Function>
        auto own_service_function(Function&& function) -> std::function<Signature>
        {
            if constexpr (std::copy_constructible<std::decay_t<Function>>) {
                return std::function<Signature>{ std::forward<Function>(function) };
            }
            else {
                auto owned{ std::make_shared<std::decay_t<Function>>(std::forward<Function>(function)) };
                return [owned](auto&&... args) -> decltype(auto) {
                    return std::invoke(*owned, std::forward<decltype(args)>(args)...);
                };
            }
        }

        template<typename Response>
        class ServiceCallState
        {
          public:
            using EncodedResult = std::expected<MessageBytes, ServiceError>;

            ServiceCallState(ServiceClock::time_point end,
                             std::stop_token token,
                             ServiceCompletion<Response> completion)
              : m_callback{ std::move(completion) }
              , m_deadline{ end }
              , m_stop{ std::move(token) }
            {
            }

            auto finish(MessageBytes value, std::optional<ServiceError> failure = {}) -> bool
            {
                std::scoped_lock lock{ m_mutex };
                refreshLocked();
                if (m_finished) {
                    return false;
                }
                m_response = std::move(value);
                m_error = failure;
                m_finished = true;
                m_changed.notify_all();
                return true;
            }

            auto fail(ServiceError failure) -> bool { return finish({}, failure); }

            auto pending() -> bool
            {
                std::scoped_lock lock{ m_mutex };
                refreshLocked();
                return !m_finished;
            }

            auto remainingTime() -> ServiceClock::duration
            {
                std::scoped_lock lock{ m_mutex };
                refreshLocked();
                return m_finished
                         ? ServiceClock::duration::zero()
                         : std::max(ServiceClock::duration::zero(), m_deadline - ServiceClock::now());
            }

            auto take(bool wait) -> std::optional<EncodedResult>
            {
                std::unique_lock lock{ m_mutex };
                if (m_consumed) {
                    if (wait) {
                        throw std::logic_error{ "Service result was already consumed" };
                    }
                    return std::nullopt;
                }
                refreshLocked();
                if (wait && !m_finished) {
                    m_changed.wait_until(lock, m_stop, m_deadline, [this] { return m_finished; });
                    refreshLocked();
                }
                if (!m_finished) {
                    return std::nullopt;
                }
                m_consumed = true;
                if (m_error) {
                    return EncodedResult{ std::unexpected{ *m_error } };
                }
                return EncodedResult{ std::move(m_response) };
            }

          private:
            friend class pnm::msg::PendingCall<Response>;
            // Called only while holding m_mutex. The first terminal result wins.
            auto refreshLocked() -> void
            {
                if (m_finished) {
                    return;
                }
                if (m_stop.stop_requested()) {
                    m_error = ServiceError::Cancelled;
                }
                else if (ServiceClock::now() >= m_deadline) {
                    m_error = ServiceError::Timeout;
                }
                if (m_error) {
                    m_finished = true;
                    m_changed.notify_all();
                }
            }

            std::atomic_flag m_dispatching;
            ServiceCompletion<Response> m_callback;
            std::mutex m_mutex;
            std::condition_variable_any m_changed;
            ServiceClock::time_point m_deadline;
            std::stop_token m_stop;
            MessageBytes m_response;
            std::optional<ServiceError> m_error;
            bool m_finished{};
            bool m_consumed{};
        };

        template<typename Request, typename Response>
        struct ServiceProvider
        {
            using Handler = std::function<void(const Request&, Reply<Response>)>;
            using CallState = ServiceCallState<Response>;

            struct QueuedRequest
            {
                MessageBytes bytes;
                std::shared_ptr<CallState> call;
            };

            ServiceProvider(Handler handler, ServiceOptions options)
              : callback{ std::move(handler) }
              , max_pending{ options.max_pending }
            {
            }

            auto open() -> bool
            {
                std::scoped_lock lock{ mutex };
                return !closed;
            }

            auto enqueue(MessageBytes bytes, const std::shared_ptr<CallState>& call) -> void
            {
                // Keep callback-owning states alive until the provider lock has been released.
                std::vector<std::shared_ptr<CallState>> retained;
                std::scoped_lock lock{ mutex };
                if (closed) {
                    call->fail(ServiceError::Unavailable);
                    return;
                }
                retained.reserve(calls.size());
                std::erase_if(calls, [&](const auto& weak) {
                    auto previous{ weak.lock() };
                    if (!previous) {
                        return true;
                    }
                    const auto done{ !previous->pending() };
                    retained.push_back(std::move(previous));
                    return done;
                });
                std::erase_if(requests, [](const auto& queued) { return !queued.call->pending(); });
                if (!call->pending()) {
                    return;
                }
                if (calls.size() >= max_pending) {
                    call->fail(ServiceError::Busy);
                    return;
                }
                calls.emplace_back(call);
                requests.push_back(QueuedRequest{ std::move(bytes), call });
                changed.notify_one();
            }

            auto close() -> void
            {
                std::deque<QueuedRequest> discarded;
                std::vector<std::weak_ptr<CallState>> outstanding;
                {
                    std::scoped_lock lock{ mutex };
                    closed = true;
                    discarded.swap(requests);
                    outstanding.swap(calls);
                    changed.notify_all();
                }
                for (const auto& weak : outstanding) {
                    if (auto call{ weak.lock() }) {
                        call->fail(ServiceError::Unavailable);
                    }
                }
            }

            std::mutex mutex;
            std::condition_variable changed;
            std::deque<QueuedRequest> requests;
            std::vector<std::weak_ptr<CallState>> calls;
            std::atomic_flag dispatching;
            Handler callback;
            size_t max_pending;
            bool closed{};
        };

        template<typename Request, typename Response>
        struct ServiceState
        {
            std::mutex mutex;
            std::weak_ptr<ServiceProvider<Request, Response>> provider;
        };
    }

    // Owns the right to complete one call. Keep this token to reply after a handler returns.
    template<utils::memory::Serializable Response>
    class Reply
    {
      public:
        ~Reply() { fail(ServiceError::HandlerFailed); }
        Reply(const Reply&) = delete;
        auto operator=(const Reply&) -> Reply& = delete;
        Reply(Reply&&) noexcept = default;
        auto operator=(Reply&& other) noexcept -> Reply&
        {
            if (this != &other) {
                fail(ServiceError::HandlerFailed);
                m_state = std::move(other.m_state);
            }
            return *this;
        }

        auto pending() const -> bool { return m_state && m_state->pending(); }

        auto remainingTime() const -> detail::ServiceClock::duration
        {
            return m_state ? m_state->remainingTime() : detail::ServiceClock::duration::zero();
        }

        auto respond(const Response& response) -> bool
        {
            auto state{ m_state };
            if (!state || !state->pending()) {
                return false;
            }
            try {
                auto bytes{ std::make_shared<const std::vector<std::byte>>(
                  utils::memory::serialize(response)) };
                return state->finish(std::move(bytes));
            } catch (...) {
                state->fail(ServiceError::HandlerFailed);
                return false;
            }
        }

        auto fail(ServiceError error) -> bool { return m_state && m_state->fail(error); }

      private:
        template<utils::memory::Serializable RequestType, utils::memory::Serializable ResponseType>
        friend class ServiceServer;

        explicit Reply(std::shared_ptr<detail::ServiceCallState<Response>> state)
          : m_state{ std::move(state) }
        {
        }

        std::shared_ptr<detail::ServiceCallState<Response>> m_state;
    };

    template<utils::memory::Serializable Response>
    class PendingCall
    {
      public:
        ~PendingCall() { cancel(); }
        PendingCall(const PendingCall&) = delete;
        auto operator=(const PendingCall&) -> PendingCall& = delete;
        PendingCall(PendingCall&&) noexcept = default;
        auto operator=(PendingCall&& other) noexcept -> PendingCall&
        {
            if (this != &other) {
                cancel();
                m_state = std::move(other.m_state);
            }
            return *this;
        }

        auto ready() const -> bool { return m_state && !m_state->pending(); }
        auto cancel() -> bool { return m_state && m_state->fail(ServiceError::Cancelled); }

        // Callback requests are consumed only by poll(); result requests only by get().
        auto get() -> ServiceResult<Response>
        {
            auto state{ requireState() };
            detail::DispatchGuard dispatching{ state->m_dispatching };
            if (state->m_callback) {
                throw std::logic_error{ "Use poll() to dispatch a service completion callback" };
            }
            auto result{ state->take(true) };
            if (!result) {
                throw std::logic_error{ "Service wait ended without a result" };
            }
            return decode(*result);
        }

        // Never waits. The callback runs once, on this thread, including for errors.
        auto poll() -> bool
        {
            auto state{ requireState() };
            detail::DispatchGuard dispatching{ state->m_dispatching };
            if (!state->m_callback) {
                throw std::logic_error{ "Use get() to retrieve a service result without a callback" };
            }
            auto result{ state->take(false) };
            if (!result) {
                return false;
            }
            state->m_callback(decode(*result));
            return true;
        }

      private:
        template<utils::memory::Serializable RequestType, utils::memory::Serializable ResponseType>
        friend class Service;

        explicit PendingCall(std::shared_ptr<detail::ServiceCallState<Response>> state)
          : m_state{ std::move(state) }
        {
        }

        auto requireState() const -> std::shared_ptr<detail::ServiceCallState<Response>>
        {
            if (!m_state) {
                throw std::logic_error{ "Service request handle was moved from" };
            }
            return m_state;
        }

        static auto decode(const typename detail::ServiceCallState<Response>::EncodedResult& encoded)
          -> ServiceResult<Response>
        {
            if (!encoded) {
                return std::unexpected{ encoded.error() };
            }
            try {
                Response value{};
                if (utils::memory::deserialize(**encoded, value)) {
                    return value;
                }
            } catch (...) {
                // Report response decoding failures through the result.
                return std::unexpected{ ServiceError::HandlerFailed };
            }
            return std::unexpected{ ServiceError::HandlerFailed };
        }

        std::shared_ptr<detail::ServiceCallState<Response>> m_state;
    };

    template<utils::memory::Serializable Request, utils::memory::Serializable Response>
    class ServiceServer
    {
        using Provider = detail::ServiceProvider<Request, Response>;

      public:
        ~ServiceServer() { close(); }
        ServiceServer(const ServiceServer&) = delete;
        auto operator=(const ServiceServer&) -> ServiceServer& = delete;
        ServiceServer(ServiceServer&&) noexcept = default;
        auto operator=(ServiceServer&& other) noexcept -> ServiceServer&
        {
            if (this != &other) {
                close();
                m_provider = std::move(other.m_provider);
            }
            return *this;
        }

        auto close() -> void
        {
            if (m_provider) {
                m_provider->close();
            }
        }

        auto poll() -> size_t { return poll(std::chrono::milliseconds::zero()); }

        template<typename Rep, typename Period>
        auto poll(const std::chrono::duration<Rep, Period>& timeout) -> size_t
        {
            auto provider{ m_provider };
            if (!provider) {
                return 0;
            }
            detail::DispatchGuard dispatching{ provider->dispatching };
            const auto deadline{ detail::service_deadline(timeout) };
            size_t pending{};
            {
                std::unique_lock lock{ provider->mutex };
                provider->changed.wait_until(
                  lock, deadline, [&] { return provider->closed || !provider->requests.empty(); });
                pending = provider->requests.size();
            }
            size_t processed{};
            for (size_t i{}; i < pending; ++i) {
                typename Provider::QueuedRequest queued;
                {
                    std::scoped_lock lock{ provider->mutex };
                    if (provider->closed || provider->requests.empty()) {
                        break;
                    }
                    queued = std::move(provider->requests.front());
                    provider->requests.pop_front();
                }
                if (queued.call->pending()) {
                    dispatch(*provider, queued);
                    ++processed;
                }
            }
            return processed;
        }

      private:
        friend class Service<Request, Response>;
        explicit ServiceServer(std::shared_ptr<Provider> provider)
          : m_provider{ std::move(provider) }
        {
        }

        static auto dispatch(Provider& provider, const typename Provider::QueuedRequest& queued) -> void
        {
            try {
                Request request{};
                if (!utils::memory::deserialize(*queued.bytes, request)) {
                    queued.call->fail(ServiceError::HandlerFailed);
                    return;
                }
                if (queued.call->pending()) {
                    provider.callback(request, Reply<Response>{ queued.call });
                }
            } catch (...) {
                queued.call->fail(ServiceError::HandlerFailed);
            }
        }

        std::shared_ptr<Provider> m_provider;
    };

    template<utils::memory::Serializable Request, utils::memory::Serializable Response>
    class Service
    {
        static_assert(std::same_as<Request, std::remove_cvref_t<Request>> &&
                        std::same_as<Response, std::remove_cvref_t<Response>>,
                      "Service messages must be unqualified value types");
        static_assert(std::default_initializable<Request> && std::default_initializable<Response> &&
                        std::move_constructible<Response>,
                      "Service messages must be default constructible and responses movable");
        using Provider = detail::ServiceProvider<Request, Response>;

      public:
        template<typename Handler>
            requires std::is_invocable_r_v<Response, Handler&, const Request&>
        [[nodiscard]] auto serve(Handler&& function, ServiceOptions options = {}) const
          -> ServiceServer<Request, Response>
        {
            auto handler{ detail::own_service_function<Response(const Request&)>(
              std::forward<Handler>(function)) };
            if (!handler) {
                throw std::invalid_argument{ "A service requires a handler" };
            }
            return serveDeferred(
              [handler{ std::move(handler) }](const Request& request, Reply<Response> reply) mutable {
                reply.respond(handler(request));
            }, options);
        }

        template<typename Handler>
            requires std::invocable<Handler&, const Request&, Reply<Response>>
        [[nodiscard]] auto serveDeferred(Handler&& function, ServiceOptions options = {}) const
          -> ServiceServer<Request, Response>
        {
            if (!m_state) {
                throw std::logic_error{ "Cannot serve through a moved-from service" };
            }
            auto handler{ detail::own_service_function<void(const Request&, Reply<Response>)>(
              std::forward<Handler>(function)) };
            if (!handler || options.max_pending == 0) {
                throw std::invalid_argument{ "A service requires a handler and a positive pending limit" };
            }
            auto provider{ std::make_shared<Provider>(std::move(handler), options) };
            std::shared_ptr<Provider> previous;
            std::scoped_lock lock{ m_state->mutex };
            previous = m_state->provider.lock();
            if (previous && previous->open()) {
                throw std::logic_error{ "Service already has a provider" };
            }
            m_state->provider = provider;
            return ServiceServer<Request, Response>{ std::move(provider) };
        }

        template<typename Rep, typename Period>
        [[nodiscard]] auto request(const Request& value,
                                   const std::chrono::duration<Rep, Period>& timeout,
                                   const std::stop_token& stop = {}) const -> PendingCall<Response>
        {
            return submit(value, detail::service_deadline(timeout), {}, stop);
        }

        template<typename Rep, typename Period, typename Callback>
            requires std::invocable<Callback&, ServiceResult<Response>>
        [[nodiscard]] auto request(const Request& value,
                                   const std::chrono::duration<Rep, Period>& timeout,
                                   Callback&& function,
                                   const std::stop_token& stop = {}) const -> PendingCall<Response>
        {
            const auto deadline{ detail::service_deadline(timeout) };
            auto callback{ detail::own_service_function<void(ServiceResult<Response>)>(
              std::forward<Callback>(function)) };
            if (!callback) {
                throw std::invalid_argument{ "A callback request requires a completion callback" };
            }
            return submit(value, deadline, std::move(callback), stop);
        }

        template<typename Rep, typename Period>
        auto call(const Request& value,
                  const std::chrono::duration<Rep, Period>& timeout,
                  const std::stop_token& stop = {}) const -> ServiceResult<Response>
        {
            return request(value, timeout, stop).get();
        }

      private:
        friend class Bus;
        explicit Service(std::shared_ptr<detail::ServiceState<Request, Response>> state)
          : m_state{ std::move(state) }
        {
        }

        auto submit(const Request& value,
                    detail::ServiceClock::time_point deadline,
                    detail::ServiceCompletion<Response> callback,
                    const std::stop_token& stop) const -> PendingCall<Response>
        {
            if (!m_state) {
                throw std::logic_error{ "Cannot request through a moved-from service" };
            }
            auto state{ std::make_shared<detail::ServiceCallState<Response>>(
              deadline, stop, std::move(callback)) };
            auto pending{ PendingCall<Response>{ state } };
            if (!state->pending()) {
                return pending;
            }
            std::shared_ptr<Provider> provider;
            {
                std::scoped_lock lock{ m_state->mutex };
                provider = m_state->provider.lock();
            }
            if (!provider || !provider->open()) {
                state->fail(ServiceError::Unavailable);
                return pending;
            }
            auto bytes{ std::make_shared<const std::vector<std::byte>>(utils::memory::serialize(value)) };
            provider->enqueue(std::move(bytes), state);
            return pending;
        }

        std::shared_ptr<detail::ServiceState<Request, Response>> m_state;
    };

    class Bus
    {
        struct Entry
        {
            std::type_index type;
            std::shared_ptr<void> state;
        };

      public:
        template<utils::memory::Serializable Request, utils::memory::Serializable Response>
        auto service(std::string_view name) -> Service<Request, Response>
        {
            if (name.empty()) {
                throw std::invalid_argument{ "A service requires a name" };
            }
            using State = detail::ServiceState<Request, Response>;
            std::string key{ name };
            std::scoped_lock lock{ m_mutex };
            if (const auto found{ m_services.find(key) }; found != m_services.end()) {
                if (found->second.type != std::type_index{ typeid(State) }) {
                    throw std::invalid_argument{ "Service '" + key + "' has different message types" };
                }
                return Service<Request, Response>{ std::static_pointer_cast<State>(found->second.state) };
            }
            auto state{ std::make_shared<State>() };
            m_services.emplace(std::move(key), Entry{ std::type_index{ typeid(State) }, state });
            return Service<Request, Response>{ std::move(state) };
        }

        template<utils::memory::Serializable T>
        auto topic(std::string_view name) -> Topic<T>
        {
            if (name.empty()) {
                throw std::invalid_argument{ "A topic requires a name" };
            }
            std::string key{ name };
            std::scoped_lock lock{ m_mutex };
            if (const auto found{ m_topics.find(key) }; found != m_topics.end()) {
                if (found->second.type != std::type_index{ typeid(T) }) {
                    throw std::invalid_argument{ "Topic '" + key + "' has a different message type" };
                }
                return Topic<T>{ std::static_pointer_cast<detail::TopicState<T>>(found->second.state) };
            }
            auto state{ std::make_shared<detail::TopicState<T>>() };
            m_topics.emplace(std::move(key), Entry{ std::type_index{ typeid(T) }, state });
            return Topic<T>{ std::move(state) };
        }

      private:
        std::mutex m_mutex;
        std::unordered_map<std::string, Entry> m_topics;
        std::unordered_map<std::string, Entry> m_services;
    };
}
