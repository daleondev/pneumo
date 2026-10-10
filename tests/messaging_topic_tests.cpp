#include "pneumo/messaging.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <latch>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace messaging_tests
{
    struct HookedMessage
    {
        int value{};
    };

    struct Payload
    {
        std::vector<std::byte> bytes;
    };

    struct Message
    {
        double value;
        Payload payload;
    };

    struct PaddedMessage
    {
        char tag;
        int value;
        auto operator==(const PaddedMessage&) const -> bool = default;
    };
}

namespace pnm::utils::memory
{
    template<>
    struct SerializationAdapter<messaging_tests::HookedMessage>
    {
        static inline std::function<void()> on_serialize{};
        static inline std::function<void()> on_deserialize{};
        static auto bufferSize(const messaging_tests::HookedMessage&) -> size_t { return sizeof(int); }
        static auto serialize(const messaging_tests::HookedMessage& value, std::span<std::byte> bytes) -> void
        {
            pnm::utils::memory::serialize(value.value, bytes);
            if (auto hook{ std::exchange(on_serialize, {}) })
                hook();
        }
        static auto deserialize(std::span<const std::byte> bytes, messaging_tests::HookedMessage& value)
          -> void
        {
            pnm::utils::memory::deserialize(bytes, value.value);
            if (auto hook{ std::exchange(on_deserialize, {}) })
                hook();
        }
    };

    template<>
    struct SerializationAdapter<messaging_tests::Payload>
    {
        static inline std::atomic<int> serialization_calls{};
        static inline std::atomic<int> deserialization_calls{};

        static auto bufferSize(const messaging_tests::Payload& value) -> size_t { return value.bytes.size(); }
        static auto serialize(const messaging_tests::Payload& value, std::span<std::byte> bytes) -> void
        {
            ++serialization_calls;
            std::ranges::copy(value.bytes, bytes.begin());
        }
        static auto deserialize(std::span<const std::byte> bytes, messaging_tests::Payload& value) -> void
        {
            ++deserialization_calls;
            value.bytes.assign(bytes.begin(), bytes.end());
        }
    };
}

namespace
{
    using namespace std::chrono_literals;

    template<typename T>
    concept HasTopic = requires(pnm::msg::Bus& bus) { bus.template topic<T>("test"); };

    struct NonDefaultMessage
    {
        explicit NonDefaultMessage(int number)
          : value{ number }
        {
        }
        int value;
    };

    template<typename T>
    concept CanSubscribe = requires(pnm::msg::Topic<T>& topic, std::function<void(const T&)> callback) {
        topic.subscribe(callback);
    };

    static_assert(HasTopic<int>);
    static_assert(HasTopic<messaging_tests::Message>);
    static_assert(!HasTopic<int*>);
    static_assert(!HasTopic<std::vector<int>>);
    static_assert(HasTopic<NonDefaultMessage>);
    static_assert(!CanSubscribe<NonDefaultMessage>);
    static_assert(!std::copy_constructible<pnm::msg::Subscription<int>>);
    static_assert(std::is_nothrow_move_constructible_v<pnm::msg::Subscription<int>>);
}

TEST(MessagingTopicTests, NameResolvesSharedTopicAndOwnsItsString)
{
    pnm::msg::Bus bus;
    std::string name{ "sensors/temperature" };
    auto publisher{ bus.topic<int>(name) };
    name.assign("different");
    auto receiver{ bus.topic<int>("sensors/temperature") };
    std::vector<int> values;
    auto subscription{ receiver.subscribe([&](int value) { values.push_back(value); }) };

    publisher.publish(42);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 42 }));
    EXPECT_EQ(subscription.poll(), 0UZ);
}

TEST(MessagingTopicTests, RejectsIncompatibleTypesEmptyNamesAndEmptyCallbacks)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    EXPECT_THROW(bus.topic<float>("value"), std::invalid_argument);
    EXPECT_THROW(bus.topic<int>(""), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(topic.subscribe({})), std::invalid_argument);
}

TEST(MessagingTopicTests, DifferentNamesAndBusesAreIndependent)
{
    pnm::msg::Bus first;
    pnm::msg::Bus second;
    int calls{};
    auto subscription{ first.topic<int>("a").subscribe([&](int) { ++calls; }) };

    first.topic<int>("b").publish(1);
    second.topic<int>("a").publish(2);
    EXPECT_EQ(subscription.poll(), 0UZ);
    EXPECT_EQ(calls, 0);
}

TEST(MessagingTopicTests, SubscribersHaveIndependentQueues)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::vector<int> fast;
    std::vector<int> slow;
    auto first{ topic.subscribe([&](int value) { fast.push_back(value); }) };
    auto second{ topic.subscribe([&](int value) { slow.push_back(value); }) };

    topic.publish(1);
    EXPECT_EQ(first.poll(), 1UZ);
    topic.publish(2);
    EXPECT_EQ(first.poll(), 1UZ);
    EXPECT_EQ(second.poll(), 2UZ);
    EXPECT_EQ(fast, (std::vector{ 1, 2 }));
    EXPECT_EQ(slow, fast);
}

TEST(MessagingTopicTests, SubscribingDoesNotReplayOlderPublications)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    topic.publish(1);
    std::vector<int> values;
    auto subscription{ topic.subscribe([&](int value) { values.push_back(value); }) };
    topic.publish(2);

    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 2 }));
}

TEST(MessagingTopicTests, SerializedDataIsOwnedAndAdaptersAreUsed)
{
    using messaging_tests::Message;
    using Adapter = pnm::utils::memory::SerializationAdapter<messaging_tests::Payload>;
    Adapter::serialization_calls = 0;
    Adapter::deserialization_calls = 0;
    pnm::msg::Bus bus;
    auto topic{ bus.topic<Message>("message") };
    Message first{};
    Message second{};
    auto a{ topic.subscribe([&](const Message& value) { first = value; }) };
    auto b{ topic.subscribe([&](const Message& value) { second = value; }) };
    Message source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 } } } };
    topic.publish(source);
    source.value = 99.0;
    source.payload.bytes.clear();

    ASSERT_EQ(a.poll(), 1UZ);
    ASSERT_EQ(b.poll(), 1UZ);
    EXPECT_EQ(first.value, 21.5);
    EXPECT_EQ(first.payload.bytes, (std::vector{ std::byte{ 1 }, std::byte{ 2 } }));
    EXPECT_EQ(second.payload.bytes, first.payload.bytes);
    EXPECT_EQ(Adapter::serialization_calls.load(), 1);
    EXPECT_EQ(Adapter::deserialization_calls.load(), 2);
}

TEST(MessagingTopicTests, DestroyingSubscriptionReleasesCallbackAndPendingData)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::weak_ptr<int> captured;
    {
        auto value{ std::make_shared<int>(0) };
        captured = value;
        auto subscription{ topic.subscribe([value](int number) { *value = number; }) };
        topic.publish(42);
    }
    EXPECT_TRUE(captured.expired());
    EXPECT_NO_THROW(topic.publish(43));
}

TEST(MessagingTopicTests, SubscriptionMovesTransferOwnership)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    int received{};
    int replaced_calls{};
    auto original{ topic.subscribe([&](int value) { received += value; }) };
    auto replacement{ topic.subscribe([&](int) { ++replaced_calls; }) };
    topic.publish(42);
    auto moved{ std::move(original) };
    replacement = std::move(moved);

    EXPECT_EQ(original.poll(), 0UZ);
    EXPECT_EQ(moved.poll(), 0UZ);
    EXPECT_EQ(replacement.poll(), 1UZ);
    EXPECT_EQ(received, 42);
    EXPECT_EQ(replaced_calls, 0);
}

TEST(MessagingTopicTests, CallbackCanPublishAndRecursivePollIsRejected)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::vector<int> values;
    std::optional<pnm::msg::Subscription<int>> subscription;
    subscription.emplace(topic.subscribe([&](int value) {
        values.push_back(value);
        EXPECT_THROW(subscription->poll(), std::logic_error);
        if (value == 1) {
            topic.publish(2);
        }
    }));
    topic.publish(1);

    EXPECT_EQ(subscription->poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 1 }));
    EXPECT_EQ(subscription->poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 1, 2 }));
}

TEST(MessagingTopicTests, CallbackCanUnsubscribeAndDiscardRemainingMessages)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    int calls{};
    std::optional<pnm::msg::Subscription<int>> subscription;
    subscription.emplace(topic.subscribe([&](int) {
        ++calls;
        subscription->unsubscribe();
    }));
    topic.publish(1);
    topic.publish(2);

    EXPECT_EQ(subscription->poll(), 1UZ);
    topic.publish(3);
    EXPECT_EQ(subscription->poll(), 0UZ);
    EXPECT_EQ(calls, 1);
}

TEST(MessagingTopicTests, CallbackFailureKeepsRemainingMessagesAndAllowsPollingAgain)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::vector<int> values;
    auto subscription{ topic.subscribe([&](int value) {
        if (value == 1) {
            throw std::runtime_error{ "callback failed" };
        }
        values.push_back(value);
    }) };
    topic.publish(1);
    topic.publish(2);

    EXPECT_THROW(subscription.poll(), std::runtime_error);
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 2 }));
}

TEST(MessagingTopicTests, TimedPollWakesAndRunsCallbackOnPollingThread)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::thread::id callback_thread;
    int received{};
    auto subscription{ topic.subscribe([&](int value) {
        callback_thread = std::this_thread::get_id();
        received = value;
    }) };
    std::promise<size_t> completion;
    auto result{ completion.get_future() };
    std::latch started{ 1 };
    std::jthread receiver{ [&] {
        started.count_down();
        completion.set_value(subscription.poll(5s));
    } };
    started.wait();
    EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
    topic.publish(42);
    ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(result.get(), 1UZ);
    EXPECT_EQ(received, 42);
    EXPECT_EQ(callback_thread, receiver.get_id());
}

TEST(MessagingTopicTests, UnsubscribeWakesWaitingPoll)
{
    pnm::msg::Bus bus;
    auto subscription{ bus.topic<int>("value").subscribe([](int) {}) };
    std::promise<size_t> completion;
    auto result{ completion.get_future() };
    std::latch started{ 1 };
    std::jthread receiver{ [&] {
        started.count_down();
        completion.set_value(subscription.poll(5s));
    } };
    started.wait();
    EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
    subscription.unsubscribe();
    ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(result.get(), 0UZ);
}

TEST(MessagingTopicTests, PollReturnsOnTimeoutAndNonpositiveDurations)
{
    pnm::msg::Bus bus;
    auto subscription{ bus.topic<int>("value").subscribe([](int) {}) };
    EXPECT_EQ(subscription.poll(0ms), 0UZ);
    EXPECT_EQ(subscription.poll(-1ms), 0UZ);
    const auto started{ std::chrono::steady_clock::now() };
    EXPECT_EQ(subscription.poll(10ms), 0UZ);
    EXPECT_GE(std::chrono::steady_clock::now() - started, 10ms);
}

TEST(MessagingTopicTests, ConcurrentPublishersDeliverSameOrderToEverySubscriber)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::vector<int> first;
    std::vector<int> second;
    auto a{ topic.subscribe([&](int value) { first.push_back(value); }) };
    auto b{ topic.subscribe([&](int value) { second.push_back(value); }) };
    std::latch start{ 1 };
    std::vector<std::jthread> publishers;
    for (int producer{}; producer < 4; ++producer) {
        publishers.emplace_back([&, producer] {
            auto publisher{ bus.topic<int>("value") };
            start.wait();
            for (int value{}; value < 100; ++value) {
                publisher.publish(producer * 100 + value);
            }
        });
    }
    start.count_down();
    for (auto& publisher : publishers) {
        publisher.join();
    }

    EXPECT_EQ(a.poll(), 400UZ);
    EXPECT_EQ(b.poll(), 400UZ);
    EXPECT_EQ(first, second);
    std::ranges::sort(first);
    std::vector<int> expected(400);
    std::iota(expected.begin(), expected.end(), 0);
    EXPECT_EQ(first, expected);
}

TEST(MessagingTopicTests, TopicHandlesCanOutliveBus)
{
    auto topic{ [] {
        pnm::msg::Bus bus;
        return bus.topic<int>("value");
    }() };
    int received{};
    auto subscription{ topic.subscribe([&](int value) { received = value; }) };
    topic.publish(42);
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(received, 42);
}

TEST(MessagingTopicTests, LastTopicOwnerClosesSurvivingSubscriptions)
{
    auto subscription{ [] {
        pnm::msg::Bus bus;
        auto topic{ bus.topic<int>("value") };
        auto receiver{ topic.subscribe([](int) { ADD_FAILURE() << "Topic has closed"; }) };
        topic.publish(42);
        return receiver;
    }() };
    EXPECT_EQ(subscription.poll(5s), 0UZ);
}

TEST(MessagingTopicTests, MovedFromTopicRejectsOperations)
{
    pnm::msg::Bus bus;
    auto original{ bus.topic<int>("value") };
    auto moved{ std::move(original) };
    EXPECT_THROW(original.publish(1), std::logic_error);
    EXPECT_THROW(static_cast<void>(original.subscribe([](int) {})), std::logic_error);
    EXPECT_THROW(original.setPublishOnlyOnChange(true), std::logic_error);
    EXPECT_NO_THROW(moved.publish(2));
}

TEST(MessagingTopicTests, LatestInvokesCallbackForLateSubscriberWithoutQueueing)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    topic.publish(1);
    topic.publish(2);
    std::vector<int> values;
    std::thread::id callback_thread;
    auto subscription{ topic.subscribe([&](int value) {
        values.push_back(value);
        callback_thread = std::this_thread::get_id();
    }) };

    EXPECT_TRUE(values.empty());
    subscription.latest();
    EXPECT_EQ(values, (std::vector{ 2 }));
    EXPECT_EQ(callback_thread, std::this_thread::get_id());
    EXPECT_EQ(subscription.poll(), 0UZ);
    subscription.latest();
    EXPECT_EQ(values, (std::vector{ 2, 2 }));
}

TEST(MessagingTopicTests, LatestDoesNothingBeforePublicationOrAfterUnsubscribe)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    int calls{};
    auto subscription{ topic.subscribe([&](int) { ++calls; }) };
    subscription.latest();
    EXPECT_EQ(calls, 0);
    topic.publish(1);
    subscription.unsubscribe();
    subscription.latest();
    EXPECT_EQ(calls, 0);
}

TEST(MessagingTopicTests, LatestDoesNotConsumeAnySubscriptionQueue)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::vector<int> first;
    std::vector<int> second;
    auto a{ topic.subscribe([&](int value) { first.push_back(value); }) };
    auto b{ topic.subscribe([&](int value) { second.push_back(value); }) };
    topic.publish(1);
    topic.publish(2);

    a.latest();
    EXPECT_EQ(first, (std::vector{ 2 }));
    EXPECT_TRUE(second.empty());
    EXPECT_EQ(a.poll(), 2UZ);
    EXPECT_EQ(b.poll(), 2UZ);
    EXPECT_EQ(first, (std::vector{ 2, 1, 2 }));
    EXPECT_EQ(second, (std::vector{ 1, 2 }));
    b.latest();
    EXPECT_EQ(second, (std::vector{ 1, 2, 2 }));
}

TEST(MessagingTopicTests, LatestRetainsOwnedAdapterPayloadAndTransfersOnMove)
{
    using messaging_tests::Message;
    pnm::msg::Bus bus;
    auto topic{ bus.topic<Message>("value") };
    Message source{ 12.5, { { std::byte{ 42 } } } };
    topic.publish(source);
    source.payload.bytes.clear();
    source.value = 99;
    Message received{};
    int calls{};
    auto original{ topic.subscribe([&](const Message& message) {
        received = message;
        ++calls;
    }) };
    auto moved{ std::move(original) };
    original.latest();
    EXPECT_EQ(calls, 0);
    moved.latest();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(received.value, 12.5);
    EXPECT_EQ(received.payload.bytes, (std::vector{ std::byte{ 42 } }));
    received.payload.bytes.clear();
    moved.latest();
    EXPECT_EQ(received.payload.bytes, (std::vector{ std::byte{ 42 } }));
}

TEST(MessagingTopicTests, LatestFailurePreservesQueueAndAllowsSubsequentDispatch)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    bool fail{ true };
    std::vector<int> values;
    auto subscription{ topic.subscribe([&](int value) {
        if (fail) {
            throw std::runtime_error{ "callback failed" };
        }
        values.push_back(value);
    }) };
    topic.publish(42);
    EXPECT_THROW(subscription.latest(), std::runtime_error);
    fail = false;
    subscription.latest();
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 42, 42 }));
}

TEST(MessagingTopicTests, LatestCallbackCanPublishAndRejectsRecursiveDispatch)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    std::optional<pnm::msg::Subscription<int>> subscription;
    subscription.emplace(topic.subscribe([&](int value) {
        EXPECT_THROW(subscription->latest(), std::logic_error);
        EXPECT_THROW(subscription->poll(), std::logic_error);
        if (value == 1) {
            topic.publish(2);
        }
    }));
    topic.publish(1);
    subscription->latest();
    EXPECT_EQ(subscription->poll(), 2UZ);
}

TEST(MessagingTopicTests, LastTopicOwnerClearsLatest)
{
    auto subscription{ [] {
        pnm::msg::Bus bus;
        auto topic{ bus.topic<int>("value") };
        topic.publish(1);
        return topic.subscribe([](int) { ADD_FAILURE() << "Topic has closed"; });
    }() };
    subscription.latest();
}

TEST(MessagingTopicTests, ChangeDetectionIsOptionalAndSharedAcrossTopicHandles)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    auto other{ bus.topic<int>("value") };
    std::vector<int> values;
    auto subscription{ topic.subscribe([&](int value) { values.push_back(value); }) };
    EXPECT_TRUE(topic.publish(1));
    EXPECT_TRUE(topic.publish(1));
    other.setPublishOnlyOnChange(true);
    EXPECT_FALSE(topic.publish(1));
    EXPECT_TRUE(other.publish(2));
    EXPECT_FALSE(topic.publish(2));
    EXPECT_TRUE(topic.publish(1));
    other.setPublishOnlyOnChange(false);
    EXPECT_TRUE(topic.publish(1));

    EXPECT_EQ(subscription.poll(), 5UZ);
    EXPECT_EQ(values, (std::vector{ 1, 1, 2, 1, 1 }));
}

TEST(MessagingTopicTests, ChangeDetectionRetainsPublicationWithoutSubscribers)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    topic.setPublishOnlyOnChange(true);
    EXPECT_TRUE(topic.publish(0));
    EXPECT_FALSE(topic.publish(0));
    int received{ -1 };
    auto subscription{ topic.subscribe([&](int value) { received = value; }) };
    subscription.latest();
    EXPECT_EQ(received, 0);
    EXPECT_EQ(subscription.poll(), 0UZ);
}

TEST(MessagingTopicTests, ChangeDetectionUsesEqualityInsteadOfObjectPadding)
{
    using messaging_tests::PaddedMessage;
    pnm::msg::Bus bus;
    auto topic{ bus.topic<PaddedMessage>("value") };
    topic.setPublishOnlyOnChange(true);
    PaddedMessage first{};
    PaddedMessage second{};
    std::ranges::fill(std::as_writable_bytes(std::span{ &first, 1 }), std::byte{ 0x11 });
    std::ranges::fill(std::as_writable_bytes(std::span{ &second, 1 }), std::byte{ 0x22 });
    first.tag = 'a';
    first.value = 42;
    second.tag = 'a';
    second.value = 42;
    EXPECT_TRUE(topic.publish(first));
    EXPECT_FALSE(topic.publish(second));
    second.value = 43;
    EXPECT_TRUE(topic.publish(second));
}

TEST(MessagingTopicTests, CustomEqualitySupportsAdaptedMessagesAndKeepsLastPublishedValue)
{
    using messaging_tests::Message;
    pnm::msg::Bus bus;
    auto topic{ bus.topic<Message>("value") };
    EXPECT_THROW(topic.setPublishOnlyOnChange(true), std::invalid_argument);
    EXPECT_NO_THROW(topic.setPublishOnlyOnChange(false));
    topic.setPublishOnlyOnChange(true, [](const Message& left, const Message& right) {
        return left.payload.bytes == right.payload.bytes;
    });
    EXPECT_TRUE(topic.publish(Message{ 1.0, { { std::byte{ 42 } } } }));
    EXPECT_FALSE(topic.publish(Message{ 2.0, { { std::byte{ 42 } } } }));
    Message received{};
    auto subscription{ topic.subscribe([&](const Message& message) { received = message; }) };
    subscription.latest();
    EXPECT_EQ(received.value, 1.0);
    EXPECT_TRUE(topic.publish(Message{ 3.0, { { std::byte{ 43 } } } }));
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(received.value, 3.0);
    EXPECT_EQ(received.payload.bytes, (std::vector{ std::byte{ 43 } }));
}

TEST(MessagingTopicTests, EqualityFailurePreservesLatestAndQueuedMessages)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    topic.setPublishOnlyOnChange(true,
                                 [](int, int) -> bool { throw std::runtime_error{ "comparison failed" }; });
    std::vector<int> values;
    auto subscription{ topic.subscribe([&](int value) { values.push_back(value); }) };
    EXPECT_TRUE(topic.publish(1));
    EXPECT_THROW(topic.publish(2), std::runtime_error);
    subscription.latest();
    EXPECT_EQ(values, (std::vector{ 1 }));
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 1, 1 }));
    topic.setPublishOnlyOnChange(false);
    EXPECT_TRUE(topic.publish(2));
}

TEST(MessagingTopicTests, ConcurrentIdenticalPublicationsAreSuppressedForEverySubscriber)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    topic.setPublishOnlyOnChange(true);
    int first{};
    int second{};
    auto a{ topic.subscribe([&](int value) { first = value; }) };
    auto b{ topic.subscribe([&](int value) { second = value; }) };
    std::atomic<int> published{};
    std::latch start{ 1 };
    std::vector<std::jthread> publishers;
    for (int i{}; i < 4; ++i) {
        publishers.emplace_back([&] {
            auto handle{ bus.topic<int>("value") };
            start.wait();
            for (int j{}; j < 100; ++j) {
                if (handle.publish(42)) {
                    ++published;
                }
            }
        });
    }
    start.count_down();
    for (auto& publisher : publishers) {
        publisher.join();
    }
    EXPECT_EQ(published.load(), 1);
    EXPECT_EQ(a.poll(), 1UZ);
    EXPECT_EQ(b.poll(), 1UZ);
    EXPECT_EQ(first, 42);
    EXPECT_EQ(second, 42);
}

TEST(MessagingTopicTests, ConfigurationChangesDuringComparisonAreRespected)
{
    pnm::msg::Bus bus;
    auto topic{ bus.topic<int>("value") };
    topic.setPublishOnlyOnChange(true, [&](int left, int right) {
        topic.setPublishOnlyOnChange(false);
        return left == right;
    });
    EXPECT_TRUE(topic.publish(1));
    EXPECT_TRUE(topic.publish(1));
}

TEST(MessagingTopicTests, SerializationCanReleaseThePublishingHandle)
{
    using Message = messaging_tests::HookedMessage;
    using Adapter = pnm::utils::memory::SerializationAdapter<Message>;
    pnm::msg::Bus bus{};
    auto topic{ std::make_unique<pnm::msg::Topic<Message>>(bus.topic<Message>("value")) };
    int received{};
    auto subscription{ topic->subscribe([&](const Message& value) { received = value.value; }) };
    Adapter::on_serialize = [&] { topic.reset(); };
    EXPECT_TRUE(topic->publish(Message{ 42 }));
    EXPECT_FALSE(topic);
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(received, 42);
}

TEST(MessagingTopicTests, ComparisonCanReleaseThePublishingHandle)
{
    pnm::msg::Bus bus{};
    auto topic{ std::make_unique<pnm::msg::Topic<int>>(bus.topic<int>("value")) };
    std::vector<int> received{};
    auto subscription{ topic->subscribe([&](int value) { received.push_back(value); }) };
    topic->publish(1);
    topic->setPublishOnlyOnChange(true, [&](int previous, int next) {
        topic.reset();
        return previous == next;
    });
    EXPECT_TRUE(topic->publish(2));
    EXPECT_FALSE(topic);
    EXPECT_EQ(subscription.poll(), 2UZ);
    EXPECT_EQ(received, (std::vector{ 1, 2 }));
}

TEST(MessagingTopicTests, AdapterFailuresPreservePublicationAndConsumeOnlyTheFailingQueuedMessage)
{
    using Message = messaging_tests::HookedMessage;
    using Adapter = pnm::utils::memory::SerializationAdapter<Message>;
    pnm::msg::Bus bus{};
    auto topic{ bus.topic<Message>("value") };
    std::vector<int> received{};
    auto subscription{ topic.subscribe([&](const Message& value) { received.push_back(value.value); }) };
    topic.publish(Message{ 1 });
    Adapter::on_serialize = [] { throw std::runtime_error{ "encode" }; };
    EXPECT_THROW(topic.publish(Message{ 2 }), std::runtime_error);
    subscription.latest();
    EXPECT_EQ(received, (std::vector{ 1 }));
    topic.publish(Message{ 3 });
    Adapter::on_deserialize = [] { throw std::runtime_error{ "decode" }; };
    EXPECT_THROW(subscription.poll(), std::runtime_error);
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(received, (std::vector{ 1, 3 }));
}

TEST(MessagingTopicTests, TimeoutValidationAndSaturation)
{
    pnm::msg::Bus bus{};
    auto subscription{ bus.topic<int>("value").subscribe([](int) {}) };
    EXPECT_THROW(subscription.poll(std::chrono::duration<double>{ std::numeric_limits<double>::quiet_NaN() }),
                 std::invalid_argument);
    const auto check{ [](auto timeout) {
        pnm::msg::Bus local_bus{};
        auto subscriber{ local_bus.topic<int>("value").subscribe([](int) {}) };
        std::promise<size_t> completion{};
        auto result{ completion.get_future() };
        std::latch entered{ 1 };
        std::jthread thread{ [&] {
            entered.count_down();
            completion.set_value(subscriber.poll(timeout));
        } };
        entered.wait();
        EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
        subscriber.unsubscribe();
        EXPECT_EQ(result.get(), 0UZ);
    } };
    check(std::chrono::hours::max());
    check(std::chrono::duration<double>{ std::numeric_limits<double>::infinity() });
}

TEST(MessagingTopicTests, ConcurrentPollAndLatestAreRejectedAndSelfDestructionStopsDispatch)
{
    pnm::msg::Bus bus{};
    auto topic{ bus.topic<int>("value") };
    std::latch entered{ 1 };
    std::latch released{ 1 };
    auto subscription{ topic.subscribe([&](int) {
        entered.count_down();
        released.wait();
    }) };
    topic.publish(1);
    std::jthread worker{ [&] { EXPECT_EQ(subscription.poll(), 1UZ); } };
    entered.wait();
    EXPECT_THROW(subscription.poll(), std::logic_error);
    EXPECT_THROW(subscription.latest(), std::logic_error);
    subscription.unsubscribe();
    released.count_down();
    worker.join();

    std::optional<pnm::msg::Subscription<int>> self{};
    int calls{};
    self.emplace(topic.subscribe([&](int) {
        ++calls;
        self.reset();
    }));
    topic.publish(2);
    topic.publish(3);
    EXPECT_EQ(self->poll(), 1UZ);
    EXPECT_FALSE(self);
    EXPECT_EQ(calls, 1);
}

TEST(MessagingTopicTests, BoundedSubscriptionsKeepTheirSelectedHistory)
{
    pnm::msg::Bus bus;
    auto topic=bus.topic<int>("bounded");
    std::vector<int> latest, oldest;
    auto a=topic.subscribe([&](int v){latest.push_back(v);},pnm::msg::SubscriptionOptions::latestOnly());
    auto b=topic.subscribe([&](int v){oldest.push_back(v);},{2,pnm::msg::OverflowPolicy::DropNewest});
    for(int i=0;i<10;++i) topic.publish(i);
    EXPECT_EQ(a.statistics().queued,1); EXPECT_EQ(a.statistics().dropped,9);
    EXPECT_EQ(b.statistics().queued,2); EXPECT_EQ(b.statistics().dropped,8);
    a.poll(); b.poll(); EXPECT_EQ(latest,(std::vector<int>{9})); EXPECT_EQ(oldest,(std::vector<int>{0,1}));
    b.latest(); EXPECT_EQ(oldest.back(),9);
}

TEST(MessagingTopicTests, PublicReadinessRegistrationIsInitialAndDisconnectable)
{
    pnm::msg::Bus bus; auto topic=bus.topic<int>("watch");
    auto sub=topic.subscribe([](int){}); int wakes{};
    auto registration=pnm::msg::watch(sub,[&]{++wakes;});
    EXPECT_EQ(wakes,1); topic.publish(1); EXPECT_EQ(wakes,2);
    registration.reset(); topic.publish(2); EXPECT_EQ(wakes,2);
}
