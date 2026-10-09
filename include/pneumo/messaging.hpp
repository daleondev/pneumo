#pragma once

#include "common.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
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
                    throw std::logic_error{ "Subscription callback is already being dispatched" };
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

    class Bus
    {
        struct Entry
        {
            std::type_index type;
            std::shared_ptr<void> state;
        };

      public:
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
    };
}
