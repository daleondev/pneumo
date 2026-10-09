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
        publisher.publish({ .timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()),
                            .value = static_cast<float>(i) });
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
    std::latch subscribed{ 1 };

    std::thread publisher{ publish, std::ref(bus), std::ref(subscribed) };
    subscribe(bus, subscribed);
    publisher.join();
}

// NOLINTEND
