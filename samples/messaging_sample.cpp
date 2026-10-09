#include "pneumo/messaging.hpp"

#include <chrono>
#include <functional>
#include <latch>
#include <print>
#include <thread>

// NOLINTBEGIN

using namespace std::chrono_literals;

struct SensorMessage
{
    std::chrono::milliseconds timestamp;
    float value;
};

auto publish(pnm::msg::Bus& bus, std::latch& subscribed) -> void
{
    auto publisher{ bus.topic<SensorMessage>("sensors/temperature") };
    subscribed.wait();
    for (auto i{ 0UZ }; i < 10; ++i) {
        publisher.publish(SensorMessage{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch()),
                                         static_cast<float>(i) });
        std::this_thread::sleep_for(1s);
    }
}

auto subscribe(pnm::msg::Bus& bus, std::latch& subscribed) -> void
{
    auto subscriber{ bus.topic<SensorMessage>("sensors/temperature") };

    auto i{ 0UZ };
    auto subscription{ subscriber.subscribe([&i](const SensorMessage& msg) {
        std::println("Temperature at {}: {}", msg.timestamp, msg.value);
        ++i;
    }) };
    subscribed.count_down();

    while (i < 10) {
        subscription.poll(100ms);
    }
}

auto main() -> int
{
    pnm::msg::Bus bus;
    auto mode{ bus.topic<int>("system/mode") };
    mode.setPublishOnlyOnChange(true);
    mode.publish(1);
    auto mode_subscription{ mode.subscribe([](int value) { std::println("Mode: {}", value); }) };
    mode_subscription.latest(); // Explicitly dispatch the value published before subscribing.
    mode.publish(1);            // Suppressed: the payload has not changed.
    mode.publish(2);
    mode_subscription.poll(); // Dispatch only the changed value.

    std::latch subscribed{ 1 };

    std::thread publisher{ publish, std::ref(bus), std::ref(subscribed) };
    subscribe(bus, subscribed);
    publisher.join();
}

// NOLINTEND
