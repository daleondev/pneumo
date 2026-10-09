#include "pneumo/messaging.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <latch>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace messaging_service_tests
{
    struct Blob
    {
        std::vector<std::byte> bytes;
    };

    struct MoveOnly
    {
        int value{};
        MoveOnly() = default;
        explicit MoveOnly(int number)
          : value{ number }
        {
        }
        MoveOnly(const MoveOnly&) = delete;
        auto operator=(const MoveOnly&) -> MoveOnly& = delete;
        MoveOnly(MoveOnly&&) = default;
        auto operator=(MoveOnly&&) -> MoveOnly& = default;
    };
}

namespace pnm::utils::memory
{
    template<>
    struct SerializationAdapter<messaging_service_tests::Blob>
    {
        static auto bufferSize(const messaging_service_tests::Blob& value) -> size_t
        {
            return value.bytes.size();
        }
        static auto serialize(const messaging_service_tests::Blob& value, std::span<std::byte> bytes) -> void
        {
            if (!value.bytes.empty() && value.bytes.front() == std::byte{ 0xff }) {
                throw std::runtime_error{ "Serialization failed" };
            }
            std::ranges::copy(value.bytes, bytes.begin());
        }
        static auto deserialize(std::span<const std::byte> bytes, messaging_service_tests::Blob& value)
          -> void
        {
            if (!bytes.empty() && bytes.front() == std::byte{ 0xfe }) {
                throw std::runtime_error{ "Deserialization failed" };
            }
            value.bytes.assign(bytes.begin(), bytes.end());
        }
    };
}

namespace
{
    using namespace std::chrono_literals;
    using messaging_service_tests::Blob;
    using pnm::msg::ServiceError;
    using pnm::msg::ServiceResult;

    static_assert(!std::copy_constructible<pnm::msg::Reply<int>>);
    static_assert(!std::copy_constructible<pnm::msg::PendingCall<int>>);
    static_assert(!std::copy_constructible<pnm::msg::ServiceServer<int, int>>);
    static_assert(std::is_nothrow_move_constructible_v<pnm::msg::Reply<int>>);
    static_assert(std::is_nothrow_move_constructible_v<pnm::msg::PendingCall<int>>);
    static_assert(std::is_nothrow_move_constructible_v<pnm::msg::ServiceServer<int, int>>);
}

TEST(MessagingServiceTests, NamedServiceSharesProviderAndRejectsTypeMismatches)
{
    pnm::msg::Bus bus;
    std::string name{ "math/double" };
    auto service{ bus.service<int, int>(name) };
    name.clear();
    auto server{ service.serve([](int value) { return 2 * value; }) };
    auto client{ bus.service<int, int>("math/double") };
    auto pending{ client.request(21, 1s) };
    EXPECT_FALSE(pending.ready());
    EXPECT_EQ(server.poll(), 1UZ);
    ASSERT_TRUE(pending.ready());
    EXPECT_EQ(pending.get().value(), 42);
    EXPECT_THROW(static_cast<void>(pending.get()), std::logic_error);
    EXPECT_THROW((bus.service<float, int>("math/double")), std::invalid_argument);
    EXPECT_THROW((bus.service<int, float>("math/double")), std::invalid_argument);
    EXPECT_THROW((bus.service<int, int>("")), std::invalid_argument);
    EXPECT_NO_THROW(bus.topic<int>("math/double")); // Independent topic/service namespaces.
}

TEST(MessagingServiceTests, ServicesAndBusesAreIndependent)
{
    pnm::msg::Bus first;
    pnm::msg::Bus second;
    auto server{ first.service<int, int>("a").serve([](int value) { return value; }) };
    EXPECT_EQ((first.service<int, int>("b").call(1, 1s).error()), ServiceError::Unavailable);
    EXPECT_EQ((second.service<int, int>("a").call(1, 1s).error()), ServiceError::Unavailable);
}

TEST(MessagingServiceTests, RegistrationValidatesHandlersAndRequiresOneProvider)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::function<int(const int&)> empty;
    std::function<void(const int&, pnm::msg::Reply<int>)> empty_deferred;
    EXPECT_THROW(static_cast<void>(service.serve(empty)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(service.serveDeferred(empty_deferred)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(service.serve([](int v) { return v; }, pnm::msg::ServiceOptions{ 0 })),
                 std::invalid_argument);
    auto server{ service.serve([](int value) { return value; }) };
    EXPECT_THROW(static_cast<void>(service.serve([](int v) { return v; })), std::logic_error);
    server.close();
    auto replacement{ service.serve([](int value) { return value + 1; }) };
    auto pending{ service.request(1, 1s) };
    EXPECT_EQ(replacement.poll(), 1UZ);
    EXPECT_EQ(pending.get().value(), 2);
    EXPECT_EQ(server.poll(), 0UZ);
}

TEST(MessagingServiceTests, BlockingCallRunsHandlerOnProviderThread)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::thread::id handler_thread;
    auto server{ service.serve([&](int value) {
        handler_thread = std::this_thread::get_id();
        return value + 1;
    }) };
    std::jthread provider{ [&] { EXPECT_EQ(server.poll(2s), 1UZ); } };
    auto result{ service.call(41, 2s) };
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 42);
    EXPECT_EQ(handler_thread, provider.get_id());
}

TEST(MessagingServiceTests, CallbackOnlyRunsOnClientPollExactlyOnce)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value; }) };
    int calls{};
    std::thread::id callback_thread;
    auto pending{ service.request(42, 1s, [&](ServiceResult<int> result) {
        ++calls;
        callback_thread = std::this_thread::get_id();
        ASSERT_TRUE(result);
        EXPECT_EQ(*result, 42);
    }) };
    EXPECT_FALSE(pending.poll());
    std::jthread provider{ [&] { EXPECT_EQ(server.poll(), 1UZ); } };
    provider.join();
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(pending.ready());
    EXPECT_THROW(static_cast<void>(pending.get()), std::logic_error);
    EXPECT_TRUE(pending.poll());
    EXPECT_FALSE(pending.poll());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(callback_thread, std::this_thread::get_id());
}

TEST(MessagingServiceTests, CallbackErrorsAreQueuedAndEmptyCallbacksRejected)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    int calls{};
    auto pending{ service.request(1, 1s, [&](ServiceResult<int> result) {
        ++calls;
        EXPECT_EQ(result.error(), ServiceError::Unavailable);
    }) };
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(pending.poll());
    EXPECT_EQ(calls, 1);
    std::function<void(ServiceResult<int>)> empty;
    EXPECT_THROW(static_cast<void>(service.request(1, 1s, empty)), std::invalid_argument);
    auto result_request{ service.request(1, 1s) };
    EXPECT_THROW(result_request.poll(), std::logic_error);
    EXPECT_EQ(result_request.get().error(), ServiceError::Unavailable);
}

TEST(MessagingServiceTests, DeferredRepliesCorrelateOutOfOrderAndCompleteOnlyOnce)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::vector<pnm::msg::Reply<int>> replies;
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> reply) { replies.push_back(std::move(reply)); }) };
    auto first{ service.request(1, 1s) };
    auto second{ service.request(2, 1s) };
    EXPECT_EQ(server.poll(), 2UZ);
    EXPECT_FALSE(first.ready());
    EXPECT_FALSE(second.ready());
    ASSERT_EQ(replies.size(), 2UZ);
    EXPECT_GT(replies[1].remainingTime(), 0ns);
    EXPECT_LE(replies[1].remainingTime(), 1s);
    EXPECT_TRUE(replies[1].respond(22));
    EXPECT_FALSE(first.ready());
    EXPECT_EQ(second.get().value(), 22);
    EXPECT_TRUE(replies[0].respond(11));
    EXPECT_FALSE(replies[0].respond(99));
    EXPECT_FALSE(replies[0].fail(ServiceError::TransportError));
    EXPECT_FALSE(replies[0].pending());
    EXPECT_EQ(replies[0].remainingTime(), 0ns);
    EXPECT_EQ(first.get().value(), 11);
}

TEST(MessagingServiceTests, DroppedReplyAndHandlerExceptionsBecomeFailures)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    {
        auto server{ service.serveDeferred([](int, pnm::msg::Reply<int>) {}) };
        auto pending{ service.request(1, 1s) };
        server.poll();
        EXPECT_EQ(pending.get().error(), ServiceError::HandlerFailed);
    }
    auto server{ service.serve([](int) -> int { throw std::runtime_error{ "failure" }; }) };
    auto pending{ service.request(1, 1s) };
    EXPECT_NO_THROW(server.poll());
    EXPECT_EQ(pending.get().error(), ServiceError::HandlerFailed);
}

TEST(MessagingServiceTests, ThrowAfterMovingReplyFailsItButThrowAfterRespondingKeepsResult)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::optional<pnm::msg::Reply<int>> deferred;
    auto server{ service.serveDeferred([&](int value, pnm::msg::Reply<int> reply) {
        if (value == 1) {
            deferred.emplace(std::move(reply));
        }
        else {
            reply.respond(42);
        }
        throw std::runtime_error{ "failure" };
    }) };
    auto first{ service.request(1, 1s) };
    auto second{ service.request(2, 1s) };
    server.poll();
    EXPECT_EQ(first.get().error(), ServiceError::HandlerFailed);
    EXPECT_FALSE(deferred->respond(11));
    EXPECT_EQ(second.get().value(), 42);
}

TEST(MessagingServiceTests, ProviderCloseFailsQueuedAndDeferredCallsAndAllowsReregistration)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::optional<pnm::msg::Reply<int>> reply;
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> incoming) { reply.emplace(std::move(incoming)); }) };
    auto active{ service.request(1, 1s) };
    server.poll();
    auto queued{ service.request(2, 1s) };
    server.close();
    EXPECT_EQ(active.get().error(), ServiceError::Unavailable);
    EXPECT_EQ(queued.get().error(), ServiceError::Unavailable);
    EXPECT_FALSE(reply->respond(42));
    auto replacement{ service.serve([](int v) { return v; }) };
    auto next{ service.request(3, 1s) };
    replacement.poll();
    EXPECT_EQ(next.get().value(), 3);
}

TEST(MessagingServiceTests, TimeoutIncludesQueueTimeAndSkipsExpiredHandlers)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    int calls{};
    auto server{ service.serve([&](int v) {
        ++calls;
        return v;
    }) };
    const auto started{ std::chrono::steady_clock::now() };
    EXPECT_EQ(service.call(1, 10ms).error(), ServiceError::Timeout);
    EXPECT_GE(std::chrono::steady_clock::now() - started, 10ms);
    EXPECT_EQ(service.call(1, 0ms).error(), ServiceError::Timeout);
    EXPECT_EQ(service.call(1, -1s).error(), ServiceError::Timeout);
    EXPECT_EQ(server.poll(), 0UZ);
    EXPECT_EQ(calls, 0);
}

TEST(MessagingServiceTests, DeferredTimeoutRejectsLateReplyAndDispatchesErrorOnPoll)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::optional<pnm::msg::Reply<int>> reply;
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> incoming) { reply.emplace(std::move(incoming)); }) };
    int calls{};
    auto pending{ service.request(1, 10ms, [&](ServiceResult<int> result) {
        ++calls;
        EXPECT_EQ(result.error(), ServiceError::Timeout);
    }) };
    server.poll();
    ASSERT_TRUE(reply);
    std::this_thread::sleep_for(15ms);
    EXPECT_FALSE(reply->respond(42));
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(pending.poll());
    EXPECT_EQ(calls, 1);
}

TEST(MessagingServiceTests, CancellationSkipsQueuedRequestsAndWakesBlockingGet)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    int calls{};
    auto server{ service.serve([&](int v) {
        ++calls;
        return v;
    }) };
    std::stop_source stop;
    auto pending{ service.request(1, 5s, stop.get_token()) };
    std::promise<ServiceError> completion;
    auto result{ completion.get_future() };
    std::latch started{ 1 };
    std::jthread waiter{ [&] {
        started.count_down();
        completion.set_value(pending.get().error());
    } };
    started.wait();
    stop.request_stop();
    ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(result.get(), ServiceError::Cancelled);
    auto precancelled{ service.request(2, 1s, stop.get_token()) };
    EXPECT_EQ(precancelled.get().error(), ServiceError::Cancelled);
    EXPECT_EQ(server.poll(), 0UZ);
    EXPECT_EQ(calls, 0);
}

TEST(MessagingServiceTests, CancelAndDestructionInvalidateDeferredReplies)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::vector<pnm::msg::Reply<int>> replies;
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> reply) { replies.push_back(std::move(reply)); }) };
    auto pending{ service.request(1, 1s) };
    server.poll();
    EXPECT_TRUE(pending.cancel());
    EXPECT_FALSE(pending.cancel());
    EXPECT_EQ(pending.get().error(), ServiceError::Cancelled);
    EXPECT_FALSE(replies[0].respond(42));
    {
        auto abandoned{ service.request(2, 1s) };
        server.poll();
    }
    EXPECT_FALSE(replies[1].pending());
    EXPECT_FALSE(replies[1].respond(42));
}

TEST(MessagingServiceTests, CapacityIncludesDeferredCallsAndRecoversAfterCancellation)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::vector<pnm::msg::Reply<int>> replies;
    auto server{ service.serveDeferred([&](int, pnm::msg::Reply<int> reply) {
        replies.push_back(std::move(reply));
    }, pnm::msg::ServiceOptions{ 1 }) };
    auto first{ service.request(1, 1s) };
    EXPECT_EQ(service.call(2, 1s).error(), ServiceError::Busy);
    server.poll();
    EXPECT_EQ(service.call(3, 1s).error(), ServiceError::Busy);
    first.cancel();
    auto next{ service.request(4, 1s) };
    EXPECT_FALSE(next.ready());
    server.poll();
    ASSERT_EQ(replies.size(), 2UZ);
    EXPECT_TRUE(replies.back().respond(4));
    EXPECT_EQ(next.get().value(), 4);
}

TEST(MessagingServiceTests, SerializationOwnsRequestsAndResponses)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<Blob, Blob>("value") };
    std::optional<pnm::msg::Reply<Blob>> reply;
    Blob received;
    auto server{ service.serveDeferred([&](const Blob& request, pnm::msg::Reply<Blob> incoming) {
        received = request;
        reply.emplace(std::move(incoming));
    }) };
    Blob request{ { std::byte{ 42 } } };
    auto pending{ service.request(request, 1s) };
    request.bytes.clear();
    server.poll();
    EXPECT_EQ(received.bytes, (std::vector{ std::byte{ 42 } }));
    Blob response{ { std::byte{ 43 } } };
    ASSERT_TRUE(reply->respond(response));
    response.bytes.clear();
    auto result{ pending.get() };
    ASSERT_TRUE(result);
    EXPECT_EQ(result->bytes, (std::vector{ std::byte{ 43 } }));
}

TEST(MessagingServiceTests, AdapterFailuresCompleteCallsWithoutEscapingServerPoll)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<Blob, Blob>("value") };
    auto server{ service.serve([](const Blob& request) {
        if (request.bytes.front() == std::byte{ 1 }) {
            return Blob{ { std::byte{ 0xff } } };
        }
        return Blob{ { std::byte{ 0xfe } } };
    }) };
    EXPECT_THROW(static_cast<void>(service.request(Blob{ { std::byte{ 0xff } } }, 1s)), std::runtime_error);
    auto bad_request{ service.request(Blob{ { std::byte{ 0xfe } } }, 1s) };
    auto bad_response_encode{ service.request(Blob{ { std::byte{ 1 } } }, 1s) };
    auto bad_response_decode{ service.request(Blob{ { std::byte{ 2 } } }, 1s) };
    EXPECT_NO_THROW(server.poll());
    EXPECT_EQ(bad_request.get().error(), ServiceError::HandlerFailed);
    EXPECT_EQ(bad_response_encode.get().error(), ServiceError::HandlerFailed);
    EXPECT_EQ(bad_response_decode.get().error(), ServiceError::HandlerFailed);
}

TEST(MessagingServiceTests, MoveOnlyHandlerCallbackAndResponseAreSupported)
{
    using messaging_service_tests::MoveOnly;
    pnm::msg::Bus bus;
    auto service{ bus.service<int, MoveOnly>("value") };
    auto server{ service.serve(
      [offset{ std::make_unique<int>(2) }](int value) { return MoveOnly{ value + *offset }; }) };
    int received{};
    auto pending{ service.request(
      40, 1s, [capture{ std::make_unique<int>(1) }, &received](ServiceResult<MoveOnly> result) {
        received = result->value + *capture;
    }) };
    server.poll();
    EXPECT_TRUE(pending.poll());
    EXPECT_EQ(received, 43);
}

TEST(MessagingServiceTests, DeferredProxyForwardsBetweenBusesUsingMoveOnlyReplyCapture)
{
    pnm::msg::Bus local_bus;
    pnm::msg::Bus remote_bus;
    auto local{ local_bus.service<int, int>("value") };
    auto remote{ remote_bus.service<int, int>("value") };
    auto provider{ remote.serve([](int value) { return value * 2; }) };
    std::vector<pnm::msg::PendingCall<int>> forwards;
    auto proxy{ local.serveDeferred([&](int value, pnm::msg::Reply<int> reply) {
        const auto remaining{ reply.remainingTime() };
        forwards.push_back(
          remote.request(value, remaining, [reply{ std::move(reply) }](ServiceResult<int> result) mutable {
            if (result) {
                reply.respond(*result);
            }
            else {
                reply.fail(result.error());
            }
        }));
    }) };
    auto pending{ local.request(21, 1s) };
    proxy.poll();
    EXPECT_FALSE(pending.ready());
    provider.poll();
    EXPECT_FALSE(pending.ready());
    ASSERT_EQ(forwards.size(), 1UZ);
    EXPECT_TRUE(forwards.front().poll());
    EXPECT_EQ(pending.get().value(), 42);
}

TEST(MessagingServiceTests, ExplicitRemoteFailureReachesClient)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serveDeferred([](int, pnm::msg::Reply<int> reply) {
        EXPECT_TRUE(reply.fail(ServiceError::TransportError));
        EXPECT_FALSE(reply.respond(42));
    }) };
    auto pending{ service.request(1, 1s) };
    server.poll();
    EXPECT_EQ(pending.get().error(), ServiceError::TransportError);
}

TEST(MessagingServiceTests, CallbackExceptionsConsumeResultAndRecursiveDispatchIsRejected)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value; }) };
    std::optional<pnm::msg::PendingCall<int>> pending;
    pending.emplace(service.request(1, 1s, [&](ServiceResult<int>) {
        EXPECT_THROW(pending->poll(), std::logic_error);
        throw std::runtime_error{ "callback failed" };
    }));
    server.poll();
    EXPECT_THROW(pending->poll(), std::runtime_error);
    EXPECT_FALSE(pending->poll());
}

TEST(MessagingServiceTests, CallbackCanDestroyItsOwnHandle)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value; }) };
    std::optional<pnm::msg::PendingCall<int>> pending;
    int calls{};
    pending.emplace(service.request(1, 1s, [&](ServiceResult<int>) {
        pending.reset();
        ++calls;
    }));
    server.poll();
    EXPECT_TRUE(pending->poll());
    EXPECT_FALSE(pending);
    EXPECT_EQ(calls, 1);
}

TEST(MessagingServiceTests, ServerRejectsRecursivePollAndCanCloseInsideHandler)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::optional<pnm::msg::ServiceServer<int, int>> server;
    server.emplace(service.serve([&](int value) {
        EXPECT_THROW(server->poll(), std::logic_error);
        server->close();
        return value;
    }));
    auto pending{ service.request(1, 1s) };
    EXPECT_EQ(server->poll(), 1UZ);
    EXPECT_EQ(pending.get().error(), ServiceError::Unavailable);
}

TEST(MessagingServiceTests, ConcurrentClientsReceiveTheirOwnResults)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value * 3; }) };
    std::jthread provider{ [&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            server.poll(5ms);
        }
    } };
    std::atomic<int> successes{};
    std::vector<std::jthread> clients;
    for (int i{}; i < 4; ++i) {
        clients.emplace_back([&, i] {
            auto client{ bus.service<int, int>("value") };
            for (int j{}; j < 4; ++j) {
                const auto input{ 10 * i + j };
                auto result{ client.call(input, 2s) };
                if (result && *result == input * 3) {
                    ++successes;
                }
            }
        });
    }
    for (auto& client : clients) {
        client.join();
    }
    EXPECT_EQ(successes.load(), 16);
}

TEST(MessagingServiceTests, HandlesMoveAndServiceCanOutliveBus)
{
    auto original{ [] {
        pnm::msg::Bus bus;
        return bus.service<int, int>("value");
    }() };
    auto service{ std::move(original) };
    EXPECT_THROW(static_cast<void>(original.request(1, 1s)), std::logic_error);
    EXPECT_THROW(static_cast<void>(original.serve([](int v) { return v; })), std::logic_error);
    auto first_server{ service.serve([](int value) { return value; }) };
    auto server{ std::move(first_server) };
    EXPECT_EQ(first_server.poll(), 0UZ);
    auto first_call{ service.request(42, 1s) };
    auto pending{ std::move(first_call) };
    EXPECT_FALSE(first_call.ready());
    EXPECT_FALSE(first_call.cancel());
    EXPECT_THROW(static_cast<void>(first_call.get()), std::logic_error);
    server.poll();
    EXPECT_EQ(pending.get().value(), 42);
}

TEST(MessagingServiceTests, MovingOverCallAndReplyAbandonsPreviousOperation)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    std::vector<pnm::msg::Reply<int>> replies;
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> reply) { replies.push_back(std::move(reply)); }) };
    auto first{ service.request(1, 1s) };
    auto second{ service.request(2, 1s) };
    auto third{ service.request(3, 1s) };
    server.poll();
    first = std::move(second);
    EXPECT_FALSE(replies[0].pending());
    replies[1] = std::move(replies[2]);
    EXPECT_EQ(first.get().error(), ServiceError::HandlerFailed);
    EXPECT_FALSE(replies[2].respond(33));
    EXPECT_TRUE(replies[1].respond(33));
    EXPECT_EQ(third.get().value(), 33);
}

TEST(MessagingServiceTests, TimeoutValidationAndSaturation)
{
    pnm::msg::Bus bus;
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value; }) };
    const std::chrono::duration<double> nan{ std::numeric_limits<double>::quiet_NaN() };
    EXPECT_THROW(static_cast<void>(service.request(1, nan)), std::invalid_argument);
    EXPECT_THROW(server.poll(nan), std::invalid_argument);
    auto huge{ service.request(42, std::chrono::hours::max()) };
    EXPECT_FALSE(huge.ready());
    server.poll();
    EXPECT_EQ(huge.get().value(), 42);
    std::stop_source stop;
    auto infinity{ service.request(
      1, std::chrono::duration<double>{ std::numeric_limits<double>::infinity() }, stop.get_token()) };
    EXPECT_FALSE(infinity.ready());
    stop.request_stop();
    EXPECT_EQ(infinity.get().error(), ServiceError::Cancelled);
}

TEST(MessagingServiceTests, SaturatedBlockingWaitCanBeCancelledAndServerWaitCanBeClosed)
{
    pnm::msg::Bus bus{};
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value; }) };
    std::stop_source stop{};
    auto pending{ service.request(1, std::chrono::hours::max(), stop.get_token()) };
    auto result{ std::async(std::launch::async, [&] { return pending.get(); }) };
    EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
    stop.request_stop();
    EXPECT_EQ(result.get().error(), ServiceError::Cancelled);
    server.poll(); // Retire the cancelled queued call before testing an empty provider wait.
    auto waiting{ std::async(std::launch::async, [&] { return server.poll(std::chrono::hours::max()); }) };
    EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);
    server.close();
    EXPECT_EQ(waiting.get(), 0UZ);
}

TEST(MessagingServiceTests, ConcurrentServerPollAndClientConsumptionAreRejected)
{
    pnm::msg::Bus bus{};
    auto service{ bus.service<int, int>("value") };
    std::latch entered{ 1 };
    std::latch released{ 1 };
    auto server{ service.serve([&](int value) {
        entered.count_down();
        released.wait();
        return value;
    }) };
    auto pending{ service.request(42, 5s) };
    std::jthread provider{ [&] { server.poll(); } };
    entered.wait();
    EXPECT_THROW(server.poll(), std::logic_error);
    server.close();
    released.count_down();
    provider.join();
    EXPECT_EQ(pending.get().error(), ServiceError::Unavailable);

    auto replacement{ service.serve([](int value) { return value; }) };
    std::latch callback_entered{ 1 };
    std::latch callback_released{ 1 };
    auto callback_call{ service.request(42, 5s, [&](ServiceResult<int> result) {
        EXPECT_EQ(result.value(), 42);
        callback_entered.count_down();
        callback_released.wait();
    }) };
    replacement.poll();
    std::jthread consumer{ [&] { EXPECT_TRUE(callback_call.poll()); } };
    callback_entered.wait();
    EXPECT_THROW(callback_call.poll(), std::logic_error);
    EXPECT_THROW(static_cast<void>(callback_call.get()), std::logic_error);
    callback_released.count_down();
    consumer.join();
}

TEST(MessagingServiceTests, RetiringCancelledRequestReleasesCallbackOutsideProviderLock)
{
    pnm::msg::Bus bus{};
    auto service{ bus.service<int, int>("value") };
    auto server{ service.serve([](int value) { return value; }) };
    int released{};
    auto capture{ std::shared_ptr<int>{ new int{ 0 }, [&](int* value) {
        delete value;
        ++released;
        server.close(); // Reenter the provider while the old request is being retired.
    } } };
    {
        auto pending{ service.request(1, 5s, [capture](ServiceResult<int>) {}) };
        capture.reset();
    }
    EXPECT_EQ(released, 0);
    auto next{ service.request(2, 5s) };
    EXPECT_EQ(released, 1);
    EXPECT_EQ(next.get().error(), ServiceError::Unavailable);
}

TEST(MessagingServiceTests, ReplyAndCancellationRaceHasOneTerminalResult)
{
    pnm::msg::Bus bus{};
    auto service{ bus.service<int, int>("value") };
    std::optional<pnm::msg::Reply<int>> reply{};
    auto server{ service.serveDeferred(
      [&](int, pnm::msg::Reply<int> value) { reply.emplace(std::move(value)); }) };
    for (int iteration{}; iteration < 16; ++iteration) {
        auto pending{ service.request(iteration, 5s) };
        server.poll();
        std::latch start{ 1 };
        bool responded{};
        bool cancelled{};
        std::jthread producer{ [&] {
            start.wait();
            responded = reply->respond(42);
        } };
        std::jthread consumer{ [&] {
            start.wait();
            cancelled = pending.cancel();
        } };
        start.count_down();
        producer.join();
        consumer.join();
        EXPECT_NE(responded, cancelled);
        auto result{ pending.get() };
        if (responded) {
            ASSERT_TRUE(result);
            EXPECT_EQ(*result, 42);
        }
        else {
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error(), ServiceError::Cancelled);
        }
    }
}
