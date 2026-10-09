#include "pneumo/messaging.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <latch>
#include <print>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

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

namespace service_examples
{
    struct AddRequest
    {
        std::int32_t left;
        std::int32_t right;
    };

    struct AddResponse
    {
        std::int64_t sum;
    };

    auto local_service_example() -> void
    {
        pnm::msg::Bus bus;
        auto service{ bus.service<AddRequest, AddResponse>("math/add") };
        auto server{ service.serve([](const AddRequest& request) {
            return AddResponse{ static_cast<std::int64_t>(request.left) + request.right };
        }) };

        // Blocking call: the application drives the provider on another thread.
        {
            std::jthread provider{ [&](std::stop_token stop) {
                while (!stop.stop_requested()) {
                    server.poll(10ms);
                }
            } };
            auto result{ service.call(AddRequest{ 20, 22 }, 250ms) };
            if (result) {
                std::println("Service result: {}", result->sum);
            }
            else {
                std::println("Service error: {}", static_cast<int>(result.error()));
            }
        }

        // Nonblocking call: both the handler and completion callback run on this thread.
        auto pending{ service.request(
          AddRequest{ 10, 32 }, 250ms, [](pnm::msg::ServiceResult<AddResponse> result) {
            if (result) {
                std::println("Callback service result: {}", result->sum);
            }
            else {
                std::println("Callback service error: {}", static_cast<int>(result.error()));
            }
        }) };
        while (!pending.poll()) {
            server.poll(10ms);
        }
    }

    auto deferred_service_example() -> void
    {
        pnm::msg::Bus client_bus;
        pnm::msg::Bus remote_bus;
        auto remote{ remote_bus.service<AddRequest, AddResponse>("math/add") };
        auto server{ remote.serve([](const AddRequest& request) {
            return AddResponse{ static_cast<std::int64_t>(request.left) + request.right };
        }) };

        // A runnable two-bus proxy. An SPI bridge would replace the direct forwarding below
        // with encoded frames and store Reply tokens by wire request ID until responses arrive.
        std::vector<pnm::msg::PendingCall<AddResponse>> forwarded;
        auto service{ client_bus.service<AddRequest, AddResponse>("math/add") };
        auto proxy{ service.serveDeferred([&](const AddRequest& request, pnm::msg::Reply<AddResponse> reply) {
            const auto budget{ reply.remainingTime() };
            forwarded.push_back(remote.request(
              request,
              budget,
              [reply{ std::move(reply) }](pnm::msg::ServiceResult<AddResponse> result) mutable {
                if (result) {
                    reply.respond(*result);
                }
                else {
                    reply.fail(result.error());
                }
            }));
        }) };
        auto pending{ service.request(AddRequest{ 30, 12 }, 250ms) };
        proxy.poll(); // The proxy handler returns before the response exists.
        server.poll();
        for (auto& forward : forwarded) {
            forward.poll(); // Complete the original call through its retained Reply token.
        }
        auto result{ pending.get() };
        if (result) {
            std::println("Deferred bridge result: {}", result->sum);
        }
    }

#if 0 // Future SPI integration outline; the application-specific transport helpers do not exist yet.

    // SPI bridge outline: SpiBridge and WireRequest below are illustrative application-side
    // helpers, not proposed pnm::msg types. All functions run in an application loop/task, not an ISR.
    // The bridge owns its transport buffers and pending maps and progresses through bridge.poll().
    // Service ID 1 is explicitly mapped to "math/add" and its wire schema on both processors.
    constexpr std::uint16_t ADD_SERVICE_ID{ 1 };

    // Processor A: register a proxy provider. The application retains the returned server handle
    // and polls it alongside bridge.poll(). Application clients still use service.call()/request().
    auto register_spi_proxy(pnm::msg::Bus& bus, SpiBridge& bridge)
    {
        auto service{ bus.service<AddRequest, AddResponse>("math/add") };
        return service.serveDeferred([&bridge](const AddRequest& request, pnm::msg::Reply<AddResponse> reply) {
            // forward() encodes/owns request bytes before this callback returns, assigns a wire
            // request ID, and stores the moved reply token in A's pending map under that ID.
            // Send service ID, request ID, remaining budget, and encoded payload; never the token.
            const auto budget{ reply.remainingTime() };
            bridge.forward(ADD_SERVICE_ID, request, budget, std::move(reply));
        });
    }

    // Processor B: its application has registered the actual "math/add" provider as above.
    auto receive_spi_request(pnm::msg::Bus& bus, SpiBridge& bridge, const WireRequest& frame) -> void
    {
        // The bridge has already validated the service/schema, frame length, and checksum.
        auto service{ bus.service<AddRequest, AddResponse>("math/add") };
        auto request{ bridge.decode<AddRequest>(frame) };
        auto pending{ service.request(request, frame.remaining_budget) };
        // track() stores B's PendingCall together with A's wire request ID. Later bridge.poll()
        // checks ready(), calls get(), and transmits an encoded result with that same ID.
        bridge.track(frame.request_id, std::move(pending));
    }

    // Processor A: after receiving a response, remove the matching token from its pending map.
    auto receive_spi_response(pnm::msg::Reply<AddResponse> reply,
                              std::expected<AddResponse, pnm::msg::ServiceError> result) -> void
    {
        if (result) {
            reply.respond(*result);
        }
        else {
            reply.fail(result.error());
        }
    }

    // Bridge rules to preserve this API across SPI:
    // - A owns the caller deadline. Send a remaining duration, not a steady_clock timestamp;
    //   B's budget is advisory because link latency and independent clocks prevent exact matching.
    // - IDs include a connection/session identity so reconnects and concurrent clients cannot
    //   confuse responses. Correlate by ID, not service name; responses may arrive out of order.
    // - Ignore unknown/late/duplicate responses. Expire pending maps using reply.pending() and
    //   PendingCall.ready(); report transport failure to affected calls on disconnect.
    // - Set queue/in-flight limits; return Busy when exhausted. No automatic request retries:
    //   a timeout or lost response does not prove that the remote handler did not execute.
    // - Encode payloads and ServiceError explicitly with a versioned wire format. Native object
    //   bytes, std::expected, and the current generated adapters are not a portable SPI format.
#endif
}

auto main() -> int
{
    service_examples::local_service_example();
    service_examples::deferred_service_example();

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
