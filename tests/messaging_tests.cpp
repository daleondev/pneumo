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
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace messaging_tests
{
    struct Payload
    {
        std::vector<std::byte> bytes;
    };

    struct Message
    {
        double value;
        Payload payload;
    };
}

namespace pnm::utils::memory
{
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
    auto publisher = bus.topic<int>(name);
    name.assign("different");
    auto receiver = bus.topic<int>("sensors/temperature");
    std::vector<int> values;
    auto subscription = receiver.subscribe([&](int value) { values.push_back(value); });

    publisher.publish(42);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 42 }));
    EXPECT_EQ(subscription.poll(), 0UZ);
}

TEST(MessagingTopicTests, RejectsIncompatibleTypesEmptyNamesAndEmptyCallbacks)
{
    pnm::msg::Bus bus;
    auto topic = bus.topic<int>("value");
    EXPECT_THROW(bus.topic<float>("value"), std::invalid_argument);
    EXPECT_THROW(bus.topic<int>(""), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(topic.subscribe({})), std::invalid_argument);
}

TEST(MessagingTopicTests, DifferentNamesAndBusesAreIndependent)
{
    pnm::msg::Bus first;
    pnm::msg::Bus second;
    int calls{};
    auto subscription = first.topic<int>("a").subscribe([&](int) { ++calls; });

    first.topic<int>("b").publish(1);
    second.topic<int>("a").publish(2);
    EXPECT_EQ(subscription.poll(), 0UZ);
    EXPECT_EQ(calls, 0);
}

TEST(MessagingTopicTests, SubscribersHaveIndependentQueues)
{
    pnm::msg::Bus bus;
    auto topic = bus.topic<int>("value");
    std::vector<int> fast;
    std::vector<int> slow;
    auto first = topic.subscribe([&](int value) { fast.push_back(value); });
    auto second = topic.subscribe([&](int value) { slow.push_back(value); });

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
    auto topic = bus.topic<int>("value");
    topic.publish(1);
    std::vector<int> values;
    auto subscription = topic.subscribe([&](int value) { values.push_back(value); });
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
    auto topic = bus.topic<Message>("message");
    Message first{};
    Message second{};
    auto a = topic.subscribe([&](const Message& value) { first = value; });
    auto b = topic.subscribe([&](const Message& value) { second = value; });
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
    auto topic = bus.topic<int>("value");
    std::weak_ptr<int> captured;
    {
        auto value = std::make_shared<int>(0);
        captured = value;
        auto subscription = topic.subscribe([value](int number) { *value = number; });
        topic.publish(42);
    }
    EXPECT_TRUE(captured.expired());
    EXPECT_NO_THROW(topic.publish(43));
}

TEST(MessagingTopicTests, SubscriptionMovesTransferOwnership)
{
    pnm::msg::Bus bus;
    auto topic = bus.topic<int>("value");
    int received{};
    int replaced_calls{};
    auto original = topic.subscribe([&](int value) { received += value; });
    auto replacement = topic.subscribe([&](int) { ++replaced_calls; });
    topic.publish(42);
    auto moved = std::move(original);
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
    auto topic = bus.topic<int>("value");
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
    auto topic = bus.topic<int>("value");
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
    auto topic = bus.topic<int>("value");
    std::vector<int> values;
    auto subscription = topic.subscribe([&](int value) {
        if (value == 1) {
            throw std::runtime_error{ "callback failed" };
        }
        values.push_back(value);
    });
    topic.publish(1);
    topic.publish(2);

    EXPECT_THROW(subscription.poll(), std::runtime_error);
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(values, (std::vector{ 2 }));
}

TEST(MessagingTopicTests, TimedPollWakesAndRunsCallbackOnPollingThread)
{
    pnm::msg::Bus bus;
    auto topic = bus.topic<int>("value");
    std::thread::id callback_thread;
    int received{};
    auto subscription = topic.subscribe([&](int value) {
        callback_thread = std::this_thread::get_id();
        received = value;
    });
    std::promise<size_t> completion;
    auto result = completion.get_future();
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
    auto subscription = bus.topic<int>("value").subscribe([](int) {});
    std::promise<size_t> completion;
    auto result = completion.get_future();
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
    auto subscription = bus.topic<int>("value").subscribe([](int) {});
    EXPECT_EQ(subscription.poll(0ms), 0UZ);
    EXPECT_EQ(subscription.poll(-1ms), 0UZ);
    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(subscription.poll(10ms), 0UZ);
    EXPECT_GE(std::chrono::steady_clock::now() - started, 10ms);
}

TEST(MessagingTopicTests, ConcurrentPublishersDeliverSameOrderToEverySubscriber)
{
    pnm::msg::Bus bus;
    auto topic = bus.topic<int>("value");
    std::vector<int> first;
    std::vector<int> second;
    auto a = topic.subscribe([&](int value) { first.push_back(value); });
    auto b = topic.subscribe([&](int value) { second.push_back(value); });
    std::latch start{ 1 };
    std::vector<std::jthread> publishers;
    for (int producer{}; producer < 4; ++producer) {
        publishers.emplace_back([&, producer] {
            auto publisher = bus.topic<int>("value");
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
    auto topic = [] {
        pnm::msg::Bus bus;
        return bus.topic<int>("value");
    }();
    int received{};
    auto subscription = topic.subscribe([&](int value) { received = value; });
    topic.publish(42);
    EXPECT_EQ(subscription.poll(), 1UZ);
    EXPECT_EQ(received, 42);
}

TEST(MessagingTopicTests, LastTopicOwnerClosesSurvivingSubscriptions)
{
    auto subscription = [] {
        pnm::msg::Bus bus;
        auto topic = bus.topic<int>("value");
        auto receiver = topic.subscribe([](int) { ADD_FAILURE() << "Topic has closed"; });
        topic.publish(42);
        return receiver;
    }();
    EXPECT_EQ(subscription.poll(5s), 0UZ);
}

TEST(MessagingTopicTests, MovedFromTopicRejectsOperations)
{
    pnm::msg::Bus bus;
    auto original = bus.topic<int>("value");
    auto moved = std::move(original);
    EXPECT_THROW(original.publish(1), std::logic_error);
    EXPECT_THROW(static_cast<void>(original.subscribe([](int) {})), std::logic_error);
    EXPECT_NO_THROW(moved.publish(2));
}
