#include "pneumo/messaging.hpp"

#include <print>

// NOLINTBEGIN

// using namespace std::chrono_literals;

// struct SensorMessage
// {
//     std::chrono::milliseconds timestamp;
//     float value;
// };

// auto publish(pnm::msg::Bus& bus) -> void
// {
//     auto publisher{ bus.topic<SensorMessage>("sensors/temperature") };
//     for (auto i{ 0UZ }; i < 10; ++i) {
//         publisher.publish({ .timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
//                               std::chrono::steady_clock::now().time_since_epoch()),
//                             .value = static_cast<float>(i) });
//         std::this_thread::sleep_for(1s);
//     }
// }

// auto subscribe(pnm::msg::Bus& bus) -> void
// {
//     auto subscriber{ bus.topic<SensorMessage>("sensors/temperature") };

//     auto i{ 0UZ };
//     subscriber.subscribe([&i](const SensorMessage& msg) {
//         std::println("Temperature at {}: {}", msg.timestamp, msg.value);
//         ++i;
//     });

//     while (i < 10) {
//         subscriber.poll(100ms);
//     }
// }

// auto main() -> int
// {
//     pnm::msg::Bus bus;

//     std::thread publisher{ publish, std::ref(bus) };
//     subscribe(bus);
//     publisher.join();
// }

int main() {}

// NOLINTEND
