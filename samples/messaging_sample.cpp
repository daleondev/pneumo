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

namespace action_examples
{
    struct SumGoal
    {
        std::uint32_t count;
    };

    struct SumFeedback
    {
        std::uint32_t completed;
    };

    struct SumResult
    {
        std::uint64_t sum;
    };

    // bus.action<Goal, Feedback, Result>(name): one provider, many independent goals.
    // All three payload types must be Serializable. Goal IDs distinguish concurrent submissions,
    // even with identical payloads. Messaging creates no threads and has no coroutine dependency.
    //
    // Result types:
    //   ActionStatus: Succeeded, Aborted, Cancelled (terminal execution outcomes).
    //   ActionCompletion<Result>: { ActionStatus status; Result value; }.
    //   ActionResult<Result>: std::expected<ActionCompletion<Result>, ActionError>.
    //   ActionError: Unavailable, Busy, Rejected, Timeout, HandlerFailed, TransportError.
    // A cancelled/aborted execution can therefore return partial results; rejection or a lost
    // connection is an error without a fabricated execution result.
    //
    // serve(factory) owns the per-goal state: the factory returns a step function for each goal.
    // Once the function is stored, the server accepts the goal automatically. Each server.poll()
    // admits queued goals, calls each active step once, and removes completed executions.
    // Both factory and step run on the polling thread and must return promptly. The factory only
    // prepares state; actual work starts in the step, after acceptance. A factory can alternatively
    // return std::expected<Step, ActionError> to reject a goal; exceptions report HandlerFailed.
    //
    // serveDeferred(handler) transfers an execution token to the handler, like a service Reply.
    // This lets an SPI bridge retain it and wait for remote acceptance/feedback/completion.
    // ActionExecution<Feedback, Result> is that move-only token; managed steps borrow it by reference:
    //   accept() / reject() decide admission; acceptance may be deferred by retaining the token.
    //   feedback(value) publishes progress after acceptance.
    //   succeed(result) / abort(result) / cancelled(result) finish execution exactly once.
    //   fail(error) reports infrastructure failure, including an SPI disconnect.
    //   requestCancel() / cancelRequested() support cooperative cancellation and preemption.
    //   id() identifies this goal; pending() stays true until a local terminal outcome exists.
    //   remainingAcceptanceTime() exposes the admission budget.
    // Mutating methods return bool: false when the transition is no longer possible. Only one
    // terminal outcome wins; feedback is ignored after completion. Dropping an unfinished token
    // reports HandlerFailed, never successful cancellation.
    using Execution = pnm::msg::ActionExecution<SumFeedback, SumResult>;

    struct ClientState
    {
        std::uint32_t completed{};
        bool done{};
    };

    auto callbacks(const char* label, ClientState& state) -> pnm::msg::ActionCallbacks<SumFeedback, SumResult>
    {
        return { // All three callbacks run on the client thread, inside PendingGoal::poll().
                 .on_accepted{ [label] { std::println("[client] {} accepted", label); } },
                 .on_feedback{ [label, &state](const SumFeedback& feedback) {
            state.completed = feedback.completed;
            std::println("[client] {} progress: {} steps", label, feedback.completed);
        } },
                 .on_result{ [label, &state](pnm::msg::ActionResult<SumResult> result) {
            state.done = true;
            if (result) {
                // Check status: an engaged expected also includes Aborted and Cancelled.
                const auto* status{ result->status == pnm::msg::ActionStatus::Succeeded   ? "Succeeded"
                                    : result->status == pnm::msg::ActionStatus::Cancelled ? "Cancelled"
                                                                                          : "Aborted" };
                std::println("[client] {} finished: status={}, sum={}", label, status, result->value.sum);
            }
            else {
                std::println("[client] {} error: {}", label, static_cast<int>(result.error()));
            }
        } }
        };
    }

    auto local_action_example() -> void
    {
        pnm::msg::Bus bus{};
        std::latch provider_ready{ 1 };

        // Thread 1: application-owned provider. The server manages each goal's state and lifetime.
        std::jthread provider{ [&](std::stop_token stop) {
            auto action{ bus.action<SumGoal, SumFeedback, SumResult>("math/sum") };
            auto server{ action.serve([](SumGoal goal) {
                // Factory: called once per goal by server.poll(), on the provider thread.
                std::println("[provider] Received goal with {} steps", goal.count);
                return [goal, completed{ std::uint32_t{ 0 } }, sum{ std::uint64_t{ 0 } }](
                         Execution& execution) mutable {
                    // Step: called once per server.poll(), on the same provider thread.
                    // Each goal has its own captured count/sum. The server owns this lambda;
                    // execution is borrowed for this call and must not be moved or retained.
                    if (execution.cancelRequested()) {
                        // Finish cleanup before confirming cancellation. This calculation has
                        // nothing to clean up; other operations may need several more steps.
                        execution.cancelled(SumResult{ sum });
                        return;
                    }

                    if (completed < goal.count) {
                        sum += ++completed; // Actual work: sum 1 + 2 + ... + goal.count.
                        execution.feedback(SumFeedback{ completed }); // Queue feedback for the client.
                    }
                    if (completed == goal.count) {
                        execution.succeed(SumResult{ sum }); // The server then removes this step function.
                    }
                };
            }, pnm::msg::ActionOptions{ 8 }) }; // Pending admission and active jobs.
            provider_ready.count_down();

            while (!stop.stop_requested()) {
                server.poll(); // Admit goals, advance their steps, and remove completed executions.
                std::this_thread::sleep_for(10ms); // Application chooses its work cadence.
            }

            // Reject queued/new goals and request cancellation for all active executions.
            // Continue stepping until cleanup finishes, including if the client exited early.
            server.requestStop();
            while (!server.idle()) {
                server.poll();
                std::this_thread::sleep_for(10ms);
            }
        } };

        // Thread 2: the calling/client thread. Wait until the provider has registered its server.
        provider_ready.wait();
        auto action{ bus.action<SumGoal, SumFeedback, SumResult>("math/sum") };
        // Only this thread accesses ClientState, including through its callbacks; no mutex needed.
        ClientState first_state{};
        ClientState second_state{};
        const pnm::msg::ActionGoalOptions options{ .accept_timeout{ 250ms } };
        auto first{ action.sendGoal(SumGoal{ 10 }, options, callbacks("First", first_state)) };
        auto second{ action.sendGoal(SumGoal{ 4 }, options, callbacks("Second", second_state)) };
        // first.id() and second.id() differ; cancelling one never cancels the other.

        bool cancel_sent{};
        while (!first_state.done || !second_state.done) {
            first.poll(); // Invoke this goal's acceptance, feedback, and result callbacks here.
            second.poll();
            if (!cancel_sent && !first_state.done && first_state.completed >= 3) {
                first.requestCancel(); // Provider observes this in its step; does not wait for cleanup.
                cancel_sent = true;
            }
            std::this_thread::sleep_for(1ms);
        }

        provider.request_stop();
        provider.join(); // Join while the bus is still alive; messaging owns neither thread.
        // With both goals accepted, Second succeeds with sum 10. First normally cancels with a
        // partial sum, but may finish before cancellation reaches it; progress depends on scheduling.
    }

    // Client/lifecycle contract:
    // - sendGoal() returns a move-only PendingGoal<Feedback, Result>. Its poll() never blocks,
    //   delivers on_result exactly once, and returns true once that callback has been consumed.
    //   Callback dispatch has one caller per handle, as with subscriptions and service calls.
    // - accept_timeout covers submission until acceptance, not the entire execution. No execution
    //   deadline is imposed here; applications can request cancellation using their own timers.
    //   Expiry also latches a cancellation request for the provider/bridge. With no background
    //   thread, deadline checks happen through polling/provider operations.
    // - requestCancel() is idempotent while pending. Its bool reports whether a request could be
    //   recorded, NOT whether the operation stopped. Completion may win a cancellation race.
    // - Before acceptance, reject() reports Rejected; cancelled(result) may confirm that a pending
    //   goal was stopped before doing any work. Otherwise terminal execution results follow accept().
    // - Acceptance is delivered before feedback/results for accepted goals. Undelivered feedback
    //   is coalesced to the latest value per goal; acceptance and terminal results are retained.
    // - Destroying PendingGoal requests cancellation and discards callbacks. It cannot guarantee
    //   that work has stopped. server.requestStop() rejects queued/new goals with Unavailable and
    //   requests cancellation for active executions; keep polling until server.idle() before teardown.
    //   idle() means no queued or active goals; client callbacks may still await client polling.
    // - A cancellation requested before acceptance stays latched for the first step. serve() never
    //   reports Cancelled on behalf of the step: the step must confirm cleanup has finished.
    // - Explicit admission/preemption can use serveDeferred(): requestCancel() on the old Execution,
    //   finish its cleanup, then accept the retained replacement. serve() permits concurrent goals
    //   up to max_goals; it never implicitly cancels another goal when a new one arrives.

#if 0 // Application-specific SPI transport helpers are not implemented.
    // Future SPI proxy: SpiActionBridge is an illustrative application helper, not a library type.
    // The application retains/polls this server and the bridge; callbacks never run in the ISR.
    constexpr std::uint16_t SUM_ACTION_ID{ 2 };

    auto register_spi_action_proxy(pnm::msg::Bus& bus, SpiActionBridge& bridge)
    {
        auto action{ bus.action<SumGoal, SumFeedback, SumResult>("math/sum") };
        return action.serveDeferred([&bridge](const SumGoal& goal, Execution execution) {
            const auto budget{ execution.remainingAcceptanceTime() };
            // Encode/own the payload and retain the token under a session-scoped wire goal ID.
            // Do not accept locally yet: remote admission may reject this goal or report Busy.
            bridge.forwardGoal(SUM_ACTION_ID, goal, budget, std::move(execution));
        });
    }

    // The same action API on the receiving processor is sufficient for the bridge:
    //   Submit frame       -> local action.sendGoal(...); retain PendingGoal under the wire ID.
    //   on_accepted        -> Accepted frame -> originating Execution.accept().
    //   on_feedback        -> Feedback frame -> originating Execution.feedback(value).
    //   on_result          -> Result/error frame -> succeed/abort/cancelled/reject/fail on the token.
    //   cancelRequested()  -> Cancel frame -> receiving PendingGoal.requestCancel().
    // Thus handles, callbacks and execution tokens stay local; only IDs, budgets and encoded data cross SPI.
    //
    // Transport contract:
    // - Map the action name/schema explicitly on both processors. Use (session, goal sequence) IDs
    //   and ordered/deduplicated per-goal events; never correlate merely by action name or payload.
    // - Latch a cancel received before its Submit, or guarantee Submit/Cancel ordering. Retain
    //   completed IDs for the session's replay window so a duplicate Submit cannot execute twice.
    // - A cancel acknowledgement means only "request received". Report Cancelled exclusively from
    //   the remote terminal result after cleanup; cancellation may race with success or abortion.
    // - Acceptance timeout or TransportError means the remote outcome may be unknown. Best-effort
    //   cancellation can still be sent, but neither error proves the remote operation has stopped.
    //   Forward cancellation even after the caller times out or drops its handle; keep a bounded
    //   cleanup record until the remote terminal result arrives or the session expires.
    //   Define a remote watchdog/lease policy if stopping on connection loss is required.
    // - Send the remaining acceptance duration, never a steady_clock timestamp. The client owns
    //   its deadline; the remote budget is advisory because clocks and transport delays differ.
    // - Bound pending maps/queues, coalesce feedback, and retain terminal outcomes until acknowledged
    //   or the session expires. Never automatically resubmit a goal after timeout or reconnection.
    // - Use versioned portable payload/status/error encoding, not native struct or adapter bytes.
#endif
}

auto main() -> int
{
    service_examples::local_service_example();
    service_examples::deferred_service_example();
    action_examples::local_action_example();

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
