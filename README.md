<p align="center">
  <img src="Logo.jpg" alt="pneumo logo" width="300" />
</p>

<h1 align="center">pneumo</h1>
<p align="center">
  <strong>High-pressure utilities for modern C++.</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
  <a href="https://en.cppreference.com/w/cpp/26"><img src="https://img.shields.io/badge/standard-C%2B%2B26-blue" alt="C++ Standard"></a>
  <br />
  <a href="https://github.com/daleondev/pneumo/actions"><img src="https://github.com/daleondev/pneumo/actions/workflows/cmake-multi-platform.yml/badge.svg" alt="CI Status"></a>
</p>

<hr />

# Pneumo

**pneumo** is a header-only C++26 utility library with seven module targets and one umbrella target:

*   **`pneumo::common`** for shared result, assertion, memory, queue, and bit helpers.
*   **`pneumo::meta`** for compile-time reflection and metaprogramming utilities built on C++26 static reflection.
*   **`pneumo::formatting`** for reflection-aware std::format extensions.
*   **`pneumo::units`** for strongly typed quantities, literals, conversions, and dimensional operations.
*   **`pneumo::logging`** for asynchronous structured logging, configurable routing, source metadata, files, and custom sinks.
*   **`pneumo::messaging`** for named, typed in-process topics, services, and actions with caller-driven dispatch.
*   **`pneumo::coroutines`** for lazy tasks, executor contexts, asynchronous work, timers, and channels.

`pneumo::pneumo` links all seven modules, and `pneumo/pneumo.hpp` is the matching umbrella header.

## Module Overview

### `pneumo::common`

The common module contains small reusable helpers that support the higher-level libraries:

*   `pnm::Result<T>` as `std::expected<T, std::error_code>`.
*   `pnm::utils::memory` serialization and copy helpers with explicit and automatically generated adapters.
*   `pnm::utils::concurrent` thread concepts plus thread start/stop helpers.
*   `pnm::utils::queue` push/pop adapters for queue-like types, optionally guarded by a lock.
*   `pnm::utils::bit` constexpr unsigned bit masks and masked get/set helpers.
*   Debug assertions and portable structure-packing macros.

`memory::serialize`, `deserialize`, and `copy` use `SerializationAdapter<T>` when one is available.
An adapter provides `bufferSize(const T&)`, `serialize(const T&, std::span<std::byte>)`, and
`deserialize(std::span<const std::byte>, T&)`; the latter two return `void` and can throw.
For a non-trivially-copyable class, Pneumo automatically supplies an adapter when reflection confirms
that every base and member can be serialized. Nested eligible classes receive their own adapters,
and explicit specializations override the generated fallback. Existing raw serialization for
trivially serializable types is unchanged.

Generated adapters visit bases first, then members in declaration order, and process array elements
recursively. Each adapter-backed field has a native `std::size_t` byte-length prefix, allowing
variable-sized fields to be restored into empty destinations. This is a same-process, same-ABI format.
Generated adapters throw on malformed input; fields decoded before an error may already be modified.

### `pneumo::meta`

The meta module provides:

*   **Tuple utilities:** Concepts, type transforms, index iteration, and element iteration for `std::tuple`.
*   **Variant utilities:** Concepts, tuple conversion, unique type reduction, reference-wrapper adaptation, and index iteration for `std::variant`.
*   **Type utilities:** Type and namespace name discovery.
*   **Enum reflection:** Scoped-enum counting, names, enumerators, and underlying values.
*   **Structural reflection:** Public fields, nested types, methods, getter discovery, indexed invocation, and name-based static dispatch.
*   **Source access:** Embed translation units, lazily load source files, cache them safely, and render numbered excerpts.

### `pneumo::formatting`

Pneumo extends `std::format` with automatic reflection-aware formatting while preserving standard formatter ergonomics.

Key features include:

*   **Automatic reflection:** Format public aggregates without writing boilerplate.
*   **Non-intrusive adapters:** Format classes with private members or custom layouts through `pnm::fmt::Adapter`.
*   **Enum support:** Format scoped enums by name, with an optional verbose `Type::Enumerator` form.
*   **Pointer and optional support:** Built-in handling for raw pointers, smart pointers, and `std::optional`.
*   **Stream and string fallbacks:** Support `operator<<`, `toString()`, `to_string()`, and matching free functions when no adapter is present.
*   **Optional serialization:** JSON and TOML output via [Glaze](https://github.com/stephenberry/glaze) when enabled; YAML output is available when Glaze metadata exists for the formatted type.

### `pneumo::units`

The units module provides strongly typed quantities backed by base units plus user-defined literals, conversions, and constrained arithmetic.

The current built-in quantity types are:

*   `Distance`
*   `Area`
*   `Time`
*   `ByteSize`
*   `Mass`
*   `Temperature`
*   `Current`
*   `Voltage`
*   `Force`
*   `Energy`
*   `Power`
*   `Pressure`
*   `Frequency`
*   `DataRate`
*   `Velocity`
*   `Acceleration`
*   `Jerk`
*   `Angle`
*   `AngularVelocity`
*   `AngularAcceleration`
*   `AngularJerk`
*   `Ratio`
*   `Unitless`

The current built-in cross-quantity operations include:

*   `Distance * Distance -> Area`
*   `Area / Distance -> Distance`
*   `Distance / Time -> Velocity`
*   `Distance / Velocity -> Time`
*   `Velocity / Time -> Acceleration`
*   `Velocity / Acceleration -> Time`
*   `Acceleration / Time -> Jerk`
*   `AngularVelocity / Time -> AngularAcceleration`
*   `AngularAcceleration / Time -> AngularJerk`
*   `Angle / Time -> AngularVelocity`
*   `Angle / AngularVelocity -> Time`
*   `AngularVelocity * Time -> Angle` (also in reverse order)
*   `Quantity * Ratio -> Quantity` (also in reverse order, including `Ratio * Ratio`)
*   `Quantity / Ratio -> Quantity` (same-type division, including `Ratio / Ratio`, returns `double`)
*   `Quantity * Unitless -> Quantity` (also in reverse order; mixed `Ratio`/`Unitless` products produce `Unitless`)
*   `Quantity / Unitless -> Quantity` (`Unitless / Unitless` returns `double`)
*   `1 / Unitless -> Unitless`
*   `Unitless / Time -> Frequency` (also accepts chrono durations)
*   `Unitless / Frequency -> Time`
*   `Mass * Acceleration -> Force`
*   `Force / Mass -> Acceleration`
*   `Force * Distance -> Energy`
*   `Energy / Force -> Distance`
*   `Energy / Time -> Power`
*   `Energy / Power -> Time`
*   `Force / Area -> Pressure`
*   `Force / Pressure -> Area`
*   `Voltage * Current -> Power`
*   `Power / Voltage -> Current`
*   `Power / Current -> Voltage`
*   `1 / Time -> Frequency`
*   `Ratio / Time -> Frequency` (also accepts chrono durations)
*   `1 / Frequency -> Time`
*   `Time * Frequency -> double`
*   `ByteSize / Time -> DataRate`
*   `ByteSize / DataRate -> Time`
*   The corresponding inverse multiplications for quotient relations, such as `Time * Velocity`, `Acceleration * Time`, `DataRate * Time`, `Pressure * Area`, and `Time * Power`

Temperature uses Celsius as its base unit and provides affine conversions to Kelvin and Fahrenheit. Angles use radians as their base unit and convert to and from degrees and revolutions (`rev`). Angular velocity uses radians per second (`rad_s`) as its base unit, with `60_rpm` equal to `2π rad/s`. It is distinct from `Frequency`.

Ratios use `fraction` as their base unit: `100_percent == 1_fraction`, and `(50_percent).get()` is `0.5`. Ratios support quantity scaling with `*`, `/`, `*=`, and `/=` without implicit conversion to `double`. Use `Ratio::create(value)` to construct a ratio from a normalized scalar, including the result of same-type quantity division. Percentages can be negative or exceed 100.

`Unitless` represents generic dimensionless values, such as gains and counts, and streams as a bare number. Construct one with `1.5_unitless` or `Unitless::create(value)` and read it with `.get()`. It supports scalar arithmetic and quantity scaling with `*`, `/`, `*=`, and `/=`. Chrono durations can also be multiplied or divided by `Unitless`. For example, `1.5_unitless * 8_V == 12_V` and `50_percent * 2_unitless == 1_unitless`. Same-type quantity division still returns `double`; wrap its result explicitly with `Unitless::create(6_m / 3_m)`. `Unitless` and `Ratio` remain distinct types with no implicit conversion to each other or to `double`.

### `pneumo::logging`

The logging module provides:

*   `Trace`, `Debug`, `Info`, `Warn`, `Error`, and `Critical` levels with compile-time checked format strings.
*   Asynchronous logging by default and the `pnm::log::immediate` tag for synchronous writes.
*   Built-in stdout, stderr, and cached file sinks. The default route sends `Trace` through `Warn` to stdout and `Error` through `Critical` to stderr.
*   Per-level default sinks, removable global sinks, per-call explicit sinks, minimum levels, and flush thresholds.
*   Optional file name/path, line, column, function, embedded source excerpts, and stacktraces on each sink.
*   Compile-time-checked per-sink timestamp formats and optional level labels.
*   Default level colors, per-sink palettes, and per-message ANSI color overrides.
*   User-defined sinks through `pnm::log::ISink` and `pnm::log::SinkBase`.

### `pneumo::messaging`

Link `pneumo::messaging` and include `<pneumo/messaging.hpp>` to use `pnm::msg`.
Messaging connects modules **inside one process**, including modules on different application threads.
It creates no threads and has no coroutine dependency. The application drives dispatch by polling.

| Pattern | Use | Provider/publisher | Client/consumer |
| :--- | :--- | :--- | :--- |
| Topic | State updates and data streams; many publishers and subscribers | `publish(message)` | `subscribe(callback)`, `poll()`, `latest()` |
| Service | Short request/response operations; one provider, many clients | `serve(handler)` or `serveDeferred(handler)`, server `poll()` | Blocking `call()` or nonblocking `request()` |
| Action | Longer operations with progress and cooperative cancellation | `serve(factory)` or `serveDeferred(handler)`, server `poll()` | `sendGoal()`, goal `poll()`, `requestCancel()` |

Each `Bus` has separate topic, service, and action name registries. Reusing a name within one registry
returns the same endpoint if all message types match; a type mismatch or empty name throws
`std::invalid_argument`. Different buses are independent. A service or action allows one registered
provider at a time; registering another throws `std::logic_error`.

Payloads must be unqualified value types satisfying `pnm::utils::memory::Serializable`. Messages are
serialized into owned buffers and decoded at dispatch, so the sender may reuse its input after
submission. Subscribers need default-constructible payloads. Services and actions require all payloads
to be default constructible; responses, action feedback, and action results must also be movable.
The local serialization format is not a portable wire protocol.
Payload references passed to handlers and callbacks are borrowed for that invocation; copy any data
that needs to survive it.

#### Threads, ownership, and dispatch

| Application thread | Operations executed there |
| :--- | :--- |
| Publisher/client | Submission and serialization; blocking service waits if requested |
| Subscription poller | Topic deserialization and subscriber callbacks |
| Provider poller | Request/goal deserialization, service handlers, action factories and steps |
| Client poller | Service result callbacks; action acceptance, feedback, and result callbacks |

Publishing or producing feedback does **not** invoke consumer callbacks. Even immediate errors are
reported through client polling or service `get()`/`call()`. User callbacks, adapters, and comparators
run outside messaging's locks. Adapters and comparators must tolerate concurrent calls when used from
multiple threads. Callback-owned application state only needs synchronization if other threads also
access it.

Endpoint handles (`Topic`, `Service`, `Action`) are copyable and can outlive their bus. Subscriptions,
server registrations, pending operations, replies, and action execution tokens are move-only. Keep
these handles alive for the operation's lifetime. Lookups and submissions are synchronized, but do
not concurrently move or destroy a handle that another thread is using. The bus must remain alive
while looking up endpoints.

Only one caller may dispatch a given subscription, server, or pending operation at a time. Concurrent
or recursive dispatch throws `std::logic_error`; independent handles can be polled by different
threads. Closing a handle can wake/fail pending work, but an invocation already in flight may finish.
`poll()` does not intentionally wait for work; timed topic/service polling can wait. Mutex contention,
serialization, and user code can still take time. This is not a lock-free or hard-real-time API.

The examples below use these headers and a local bus:

```cpp
#include <pneumo/messaging.hpp>

#include <chrono>
#include <cstdint>
#include <print>
#include <stop_token>
#include <thread>
#include <utility>

using namespace std::chrono_literals;
pnm::msg::Bus bus{};
```

#### Local topics

Each subscription owns an independent queue: consuming a message never removes it from another
subscription. Topics retain the last publication even without subscribers. A late subscriber requests
it explicitly with `latest()`, which invokes its callback on the calling thread and returns `void`.

```cpp
auto temperature{ bus.topic<std::int32_t>("sensors/temperature_mC") };
temperature.setPublishOnlyOnChange(true);
temperature.publish(21500);

auto display{ temperature.subscribe([](std::int32_t value) {
    std::println("Temperature: {} millidegrees C", value);
}) };
display.latest();             // Calls the callback with the retained 21500.
temperature.publish(21500);   // Returns false; no publication because the value is unchanged.
temperature.publish(22000);   // Returns true; queues a message for each subscriber.
display.poll();               // Calls this subscription's callback with 22000.
```

`latest()` does nothing without a retained value or after closure. It does not consume queued messages;
repeated calls repeat the callback, and a subsequent `poll()` may deliver the same value again.
Publications before subscription are not otherwise queued.

Change detection is off by default and shared by every handle to a topic. It uses `operator==`, or a
custom comparator passed to `setPublishOnlyOnChange(true, equal)`. Without either, enabling detection
throws `std::invalid_argument`. It compares decoded values rather than padding bytes, requires a
default-constructible payload, and never replaces the retained value when suppressing a publication.
Comparators may be retried, so avoid side effects. Suppression saves queueing and callbacks;
serialization still occurs for every attempted publication.

`poll()` processes the queue size observed at entry. `poll(timeout)` waits for data or closure, then
processes the observed queue size. Both return the number of callbacks completed. Publishers preserve
one publication order across all subscribers. A callback may publish or unsubscribe. Deserialization
or callback exceptions propagate and consume the failing queued message; later messages remain queued.
An encoding exception prevents publication. Allocation failure during fan-out can leave a publication
partially delivered; the operation does not provide a transactional guarantee across subscribers.

Destroying or unsubscribing a subscription discards its queue and retained value and wakes a waiting
poll. Destroying the last bus/topic owner closes surviving subscriptions. **Topic queues are unbounded**:
a slow consumer needs application-level rate control, change suppression, or another buffering policy.

#### Local services

A synchronous handler returns the response. Here the application runs its provider on a separate
thread, allowing the calling thread to use both blocking and callback-based requests:

```cpp
auto service{ bus.service<std::int32_t, std::int64_t>("math/double") };
auto server{ service.serve([](std::int32_t value) {
    return std::int64_t{ value } * 2;
}) };
std::jthread provider{ [&](std::stop_token stop) {
    while (!stop.stop_requested()) {
        server.poll(10ms); // Runs handlers on this provider thread.
    }
} };

auto result{ service.call(21, 1s) };
if (result) {
    std::println("Blocking result: {}", *result);
}

auto pending{ service.request(21, 1s, [](pnm::msg::ServiceResult<std::int64_t> response) {
    if (response) {
        std::println("Callback result: {}", *response);
    }
}) };
while (!pending.poll()) { // Runs the completion callback on this client thread.
    std::this_thread::sleep_for(1ms);
}
provider.request_stop();
provider.join();
```

`ServiceResult<Response>` is `std::expected<Response, ServiceError>`. Errors are `Unavailable`, `Busy`,
`Timeout`, `Cancelled`, `HandlerFailed`, and `TransportError`; application-specific outcomes belong in
the response payload. Registration accepts `ServiceOptions{ max_pending }`, defaulting to 64 queued
or executing requests, including deferred replies. Excess requests complete as `Busy`.

- `call(request, timeout, stop_token)` waits for completion. Another thread must drive the provider
  and any required transport; calling it on the sole provider thread can only time out.
- `request(request, timeout, callback, stop_token)` returns `PendingCall<Response>`. Its nonblocking
  `poll()` returns `true` only when it invokes the completion callback, exactly once.
- Omitting the callback provides `ready()` and blocking, single-consumption `get()` instead. Do not
  mix `get()` and callback polling. Handlers and service callbacks may capture move-only objects.
- The optional stop token or `pending.cancel()` abandons the response and produces `Cancelled`.
  Destroying the pending handle also abandons it without invoking a callback. This does **not** stop
  an already-running handler or undo remote side effects.

Timeouts include serialization and queueing. Nonpositive durations expire immediately; NaN throws
`std::invalid_argument`; oversized positive durations saturate. Topic and service polling use the same
timeout validation. Expiration is checked by polling, waiting, and readiness/reply operations, without
a timer thread. Expired/cancelled queued requests are skipped. The first terminal result wins.

For externally completed work, `serveDeferred()` passes `(const Request&, Reply<Response>)`. Move the
reply token into application-owned storage, then call `respond(response)` or `fail(error)` later.
`pending()` and `remainingTime()` support expiry checks and forwarding a remaining budget. Late or
duplicate completions return `false`. Dropping an unanswered reply or throwing from a handler yields
`HandlerFailed`. Request encoding exceptions propagate from submission; decoding and response encoding
failures become `HandlerFailed`. Client callback exceptions propagate and consume that completion.
Closing or destroying the server reports `Unavailable` to outstanding calls and permits re-registration.

#### Local actions

An action has a goal, feedback payload, and result payload. `serve(factory)` calls the factory once per
goal and stores its returned step function. Each `server.poll()` admits queued goals, invokes each
active step once, and removes completed executions. Acceptance is automatic after successful factory
creation. A factory may return `std::expected<Step, ActionError>` to reject a goal instead.

```cpp
struct SumGoal { std::uint32_t count; };
struct SumFeedback { std::uint32_t completed; };
struct SumResult { std::uint64_t sum; };
using Execution = pnm::msg::ActionExecution<SumFeedback, SumResult>;

auto action{ bus.action<SumGoal, SumFeedback, SumResult>("math/sum") };
auto server{ action.serve([](SumGoal goal) {
    return [goal, completed{ std::uint32_t{ 0 } }, sum{ std::uint64_t{ 0 } }]
           (Execution& execution) mutable {
        if (execution.cancelRequested()) {
            execution.cancelled(SumResult{ sum }); // Confirm cleanup is finished.
            return;
        }
        if (completed < goal.count) {
            sum += ++completed; // One addition per provider poll; state is private to this goal.
            execution.feedback(SumFeedback{ completed });
        }
        if (completed == goal.count) {
            execution.succeed(SumResult{ sum });
        }
    };
}) };
std::jthread provider{ [&](std::stop_token stop) {
    while (!stop.stop_requested()) {
        server.poll();
        std::this_thread::sleep_for(10ms);
    }
    server.requestStop(); // Reject queued/new goals, ask active executions to cancel.
    while (!server.idle()) {
        server.poll(); // Continue cleanup before destroying the server.
        std::this_thread::sleep_for(10ms);
    }
} };

std::uint32_t progress{}; // Accessed only by the client thread, including its callbacks.
auto goal{ action.sendGoal(SumGoal{ 100 }, pnm::msg::ActionGoalOptions{ 1s }, {
    .on_accepted{ [] { std::println("Goal accepted"); } },
    .on_feedback{ [&](const SumFeedback& value) { progress = value.completed; } },
    .on_result{ [](pnm::msg::ActionResult<SumResult> result) {
        if (!result) {
            std::println("Action error: {}", static_cast<int>(result.error()));
        }
        else if (result->status == pnm::msg::ActionStatus::Succeeded) {
            std::println("Sum: {}", result->value.sum);
        }
        else {
            std::println("Stopped with partial sum: {}", result->value.sum);
        }
    } }
}) };
while (!goal.poll()) {
    if (progress >= 3) goal.requestCancel(); // Idempotent request, not immediate completion.
    std::this_thread::sleep_for(1ms);
}
provider.request_stop();
provider.join();
```

Each submission has a distinct process-local `GoalId`, even for identical payloads. Each goal's state
is independent; cancelling one does not cancel the others. Steps share the provider thread and must
return promptly. Execution starts after provider acceptance, without waiting for the client to observe
`on_accepted`. The borrowed execution reference must not be moved or retained by a managed step.

`ActionResult<Result>` is `std::expected<ActionCompletion<Result>, ActionError>`. A completion contains
`status` (`Succeeded`, `Aborted`, or `Cancelled`) and `value`; **an engaged expected does not necessarily
mean success**. Aborted and cancelled executions can return partial results. Errors are `Unavailable`,
`Busy`, `Rejected`, `Timeout`, `HandlerFailed`, and `TransportError`.

- `goal.poll()` delivers acceptance, at most one latest feedback, and completion in that order, on
  the calling thread. Intermediate feedback is coalesced. A result callback is required; the others
  are optional. Action callbacks use `std::function` and require copyable captures. Factories, steps,
  and deferred handlers can own move-only state.
- `poll()` stays `true` after consuming completion, without repeating the callback. `ready()` reports
  a terminal outcome without dispatching callbacks. Callback exceptions propagate; the current event
  is consumed while later events remain available to a subsequent poll.
- `requestCancel()` only records a request. The step must observe `cancelRequested()`, finish cleanup,
  and call `cancelled(result)`. Cleanup may take several polls. Success can win a cancellation race.
  Application policy decides whether a new goal preempts an existing one; preemption is not automatic.
- `ActionGoalOptions{ accept_timeout }` defaults to 250 ms and only limits admission, including queue
  time. Accepted execution has no implicit timeout. Admission expiry also latches cancellation for a
  deferred provider/bridge. Use application timers to request cancellation of long-running work.
- `ActionOptions{ max_goals }` defaults to 64 pending/active goals. Cancellation alone does not release
  an active goal's slot. Destroying a client handle requests cancellation without invoking callbacks.
- `server.requestStop()` stops admission and requests cancellation of active goals. Keep polling until
  `idle()` and join the provider thread. `idle()` describes outstanding goals, not whether every thread
  has returned from an in-flight call. `close()`/destruction instead unregisters immediately and reports
  `Unavailable`; it cannot perform application cleanup or prove that remote work has stopped.

`serveDeferred()` passes `(const Goal&, ActionExecution<Feedback, Result>)`. Retain that owning token,
explicitly `accept()` or `reject()`, then publish feedback and call `succeed()`, `abort()`, or `cancelled()`.
This supports delayed remote admission. `fail(error)` reports infrastructure failure. Dropping an
unfinished token or throwing from a factory/step/handler reports `HandlerFailed`. Payload encoding and
decoding rules match services; malformed client feedback also reports `HandlerFailed`. Applications must
use appropriate resource ownership/cleanup for exceptional failures, not rely solely on cancellation.

See [`samples/messaging_sample.cpp`](samples/messaging_sample.cpp) for the runnable two-thread action
example with two concurrent goals, plus topics, services, and a deferred two-bus service proxy.

#### Extending local messaging over SPI

Two processors instantiate **separate buses**. An application bridge connects selected endpoints;
clients keep using the same local API. Pneumo supplies the messaging and deferred-completion primitives,
not an SPI driver, framing, reconnection protocol, or portable codec.

A practical frame contains a protocol/schema version, message kind, stable endpoint ID, session ID,
request/goal sequence, payload length, and integrity check. Requests/goals also carry a remaining time
budget. Encode fixed-width integers and other fields explicitly with agreed byte order and bounds;
do not transmit native structs, `std::expected`, reply/execution tokens, pointers, or generated-adapter
bytes. The latter can contain padding and native `size_t` lengths. The receiver validates the complete
frame and decodes a typed payload before submitting it to its bus.

For a topic, processor A's bridge subscribes and encodes publications; processor B decodes each frame
and publishes locally under the corresponding topic name. Poll subscriptions and process received frames
in an application loop/task. The ISR only queues received bytes or signals available work: bus operations
can allocate and lock. Make routes directional, or attach origin/deduplication metadata, to prevent an
incoming publication from being forwarded back indefinitely. The SPI controller must clock transfers;
a peripheral interrupt alone does not deliver a frame to its peer.

For services, the proxy retains the originating reply until the remote response arrives. This is the
shape used in the sample; `SpiBridge`, `forwardService`, and codec helpers below are application code:

```cpp
// Processor A: register this proxy and retain/poll it for the lifetime of the bridge.
auto service{ bus.service<std::int32_t, std::int64_t>("math/double") };
constexpr std::uint16_t DOUBLE_SERVICE_ID{ 1 };
auto proxy{ service.serveDeferred([&](const std::int32_t& request, pnm::msg::Reply<std::int64_t> reply) {
    const auto budget{ reply.remainingTime() };
    // Encode/own the request; allocate a session-scoped wire ID and retain the moved reply under it.
    bridge.forwardService(DOUBLE_SERVICE_ID, request, budget, std::move(reply));
}) };

// Processor B: after validating a received request frame, submit it to the real local provider.
// Keep the returned PendingCall in a bounded map until its callback has been polled.
auto forwarded{ remote_service.request(decode_request(frame.payload), frame.remaining_budget,
    [&bridge, id{ frame.request_id }](pnm::msg::ServiceResult<std::int64_t> result) {
        bridge.sendServiceResponse(id, result); // Explicitly encode value OR error with the same ID.
    }) };

// Processor A: a response frame identifies the retained reply. Decode the payload/error, then:
if (result) reply.respond(*result);
else reply.fail(result.error());
```

The bridge owns the encoded buffers and pending maps. Its application loop polls the local proxy,
transport, remote provider, and forwarded pending calls as appropriate on each processor. On A, expire
entries using `reply.pending()`/`remainingTime()`; a disconnect reports `TransportError`. On B, keep each
wire ID associated with its own `PendingCall`, so concurrent/out-of-order responses reach the right caller.

For actions, use `serveDeferred()` with a retained execution token on A and `sendGoal()` on B:

| Event | Bridge operation |
| :--- | :--- |
| Submit | Allocate `(session, wire goal ID)`, encode goal/budget; B retains its local `PendingGoal` under that ID |
| Remote acceptance/rejection | Send decision to A; only then call its retained `accept()`/`reject()` |
| Remote feedback | Send a sequenced feedback frame; A calls `execution.feedback(value)` |
| Local cancellation request | Observe `execution.cancelRequested()` on A; send Cancel; B calls its mapped goal's `requestCancel()` |
| Remote terminal outcome | Encode status and result/error; A calls `succeed`, `abort`, `cancelled`, or `fail` |

The two local goal IDs need not match; the bridge maps them through the wire ID. A cancellation
acknowledgement means only that the request was received. Report `Cancelled` exclusively after remote
cleanup is confirmed. Continue forwarding a latched cancellation even if the originating client times
out or drops its handle, retaining a bounded cleanup record until completion or session expiry.

For all bridged operations:

- Bound frame sizes, receive/transmit queues, and pending maps. Coalesce action feedback so progress
  cannot crowd out acceptance, cancellation, or terminal frames; reject excess work as `Busy`.
- Include session identity, reject stale/unknown IDs, and order/deduplicate per-goal events. Ensure
  Submit precedes Cancel, or retain an early cancel until its Submit arrives. Cache completed request
  IDs/results for the protocol's retry window so duplicate frames do not execute work twice.
- Send a remaining duration, not a `steady_clock` timestamp. The origin owns its deadline; remote
  budgets are advisory because transport latency and clocks differ.
- Do not automatically resubmit operations after timeout or reconnection. A lost response or
  `TransportError` does not prove that execution failed to start or stopped. Define a remote watchdog
  or lease policy if work must stop when the link disappears.

The sample includes disabled SPI outlines for services and actions; the transport-specific helpers are
intentionally illustrative. The local API, two-thread example, and two-bus proxy use implemented code.

### `pneumo::coroutines`

Link `pneumo::coroutines` and include `<pneumo/coroutines.hpp>` to use the `pnm::coro` namespace. The target supplies the common module and platform thread dependency; it is also included by `pneumo::pneumo`.

*   `Task<T>` and `Task<void>` are lazy, move-only coroutine results with exception propagation.
*   `Context` runs scheduled coroutine handles; `co_spawn(context, callable)` starts a task whose callable accepts the executor by reference.
*   `runAsync<T>(callable)` runs work on a detached thread and delivers its result or exception to the awaiting task.
*   `sleep(duration)` uses a worker thread for positive durations; nonpositive durations complete immediately. Both sleep overloads round positive fractional clock ticks up, saturate oversized deadlines, and reject NaN durations.
*   `sleep(context, duration, stop_token)` uses a context timer and returns `false` when cancelled. Oversized positive durations saturate to the clock's maximum deadline; NaN durations throw `std::invalid_argument`.
*   `Context::poll(limit)` processes ready work without waiting, with a default limit of 64 callbacks or resumptions. Due timers and queued tasks alternate when both are ready, preserving deadline order and queue order respectively. `scheduleAt(deadline, callback)` returns a timer whose destruction cancels its queued callback.
*   `Channel<T>` queues values for individual consumers. Closing a channel wakes waiting consumers and allows buffered values to drain before `next()` returns `std::nullopt`.
*   `RawBinaryChannel` supports broadcast and load-balancing delivery; `BinaryChannel<T>` decodes same-process, trivially copyable payloads of the expected size.

See [`samples/coroutines_sample.cpp`](samples/coroutines_sample.cpp) for a producer/consumer example using task composition, async work, a timer, and channel closure. Build and run it with:

```bash
cmake --build --preset gcc-debug --target coroutines_sample coroutines_tests
./build/gcc-debug/samples/coroutines_sample
ctest --preset gcc-debug -R Coroutines --output-on-failure
```

The larger [`samples/coroutines_pipeline_sample.cpp`](samples/coroutines_pipeline_sample.cpp) processes nine jobs with three workers. It demonstrates move-only jobs, application-level backpressure with channel permits, nested async calculations, timed retries, per-job error handling, and broadcast results consumed by independent dashboard and metrics observers. Acknowledgements ensure both observers receive every broadcast; a completion channel joins all seven pipeline tasks before stopping the context.

```bash
cmake --build --preset gcc-debug --target coroutines_pipeline_sample
./build/gcc-debug/samples/coroutines_pipeline_sample
```

Job 3 deliberately fails once and retries; job 6 is rejected permanently. Completion order varies, but the final summary is always `8 succeeded, 1 failed, 1 retried; checksum=3344`. The example uses one context thread for continuations and separate threads for blocking calculations.

Tasks have one consumer and one result: await an unstarted task, or start it with `Task::resume()` and retrieve its result after completion. Invalid operations on empty, running, or already-consumed tasks throw `std::logic_error`. Raw handles from `getHandle()` are borrowed; callers must manage their lifetime and synchronization. A separately owned task awaited by reference must outlive the await.

`co_spawn` transfers ownership to the context, which releases queued, unstarted frames if destroyed. Nested tasks inherit the executor, so async work, timers, and channel waits resume on a thread running that context. Unbound tasks can resume on the worker or channel producer thread. Keep the context alive until spawned work finishes. `Context::stop()` rejects new scheduling and drains already queued work; it does not cancel outstanding operations. A rejected continuation resumes inline to propagate the scheduling exception to its awaiting task.

`co_spawn` is detached: an uncaught exception in its top-level coroutine terminates the process.
Catch errors there when the application needs to report or recover from them. `runAsync` starts one
worker thread per call; it does not provide a bounded worker pool.

For the built-in async, timer, and channel waits, destroying a suspended `Task` disconnects its continuation safely. Detached workers and thread-based sleeps still run to completion; destroying a context-backed sleep cancels its queued timer. Any objects borrowed by worker callables must remain alive until the workers finish. Channel reads retain their shared state even when the original channel wrapper is moved or destroyed. Custom executors must outlive scheduling calls; the default `scheduleOwned` transfers frames to `schedule`, whose accepted work must eventually run. Executors that can abandon queued work should override `scheduleOwned` to retain and release that ownership.

#### Coroutine messaging

Include `<pneumo/coroutines.hpp>` and link `pneumo::coroutines` to use topics, services, and
actions from coroutines. A `pnm::coro::Bus` binds an existing `pnm::msg::Bus` to a context. Names and
payload types resolve to the same endpoints as ordinary messaging, so coroutine and non-coroutine
modules can communicate directly.

```cpp
pnm::msg::Bus native;
pnm::coro::Context context;
pnm::coro::Bus messaging{ context, native };
```

The adapter creates no threads. Messaging readiness schedules context work; context timers enforce
service and action-admission deadlines. There is no periodic messaging poller. Run `context.run()` on
an application-owned thread, or drive `context.poll()` in an existing loop. Provider admission is
limited to 64 requests/goals per dispatch so other work and timers can run.

`pnm::msg` remains independent of coroutines. The existing `Channel`, `RawBinaryChannel`, and
`BinaryChannel` APIs remain available with their existing semantics.

**Topics.** Subscribe immediately, then await individual messages:

```cpp
auto topic{ messaging.topic<int>("sensor/temperature_mC") };
auto subscription{ topic.subscribe() };
subscription.latest(); // Explicitly queues the retained value for this subscription, if any.
while (auto value{ co_await subscription.next(stop) }) {
    use_temperature(*value);
}
```

`next()` returns `Task<std::optional<T>>`; the stop token is optional. Each subscription retains its own
native message queue, including publications received between awaits. There is no second stream queue.
One read may be outstanding per subscription; overlapping reads throw `std::logic_error`. Cancelling a
pending read returns `nullopt` without consuming a message. If delivery already claimed a message, that
delivery wins the race. Cancellation does not unsubscribe; a subsequent read can continue.

`latest()` appends an explicit replay without removing queued publications. It does not automatically
replay on subscription, and does nothing without a retained value. `unsubscribe()` or destruction drops
the queue and wakes a reader with `nullopt`. `publish()` and `setPublishOnlyOnChange()` retain their native
behavior. Topic decoding exceptions propagate through `next()` and consume only that failing message.

**Services.** A client awaits the existing `ServiceResult<Response>`. A provider owns a coroutine for
each request, with an owned payload and a cancellation token:

```cpp
auto service{ messaging.service<int, int>("math/double") };
auto server{ service.serve([](int request, std::stop_token stop) -> pnm::coro::Task<int> {
    // Other awaitable work may use stop; short calculations can just return.
    if (stop.stop_requested()) co_return 0;
    co_return request * 2;
}) };
auto response{ co_await service.request(21, std::chrono::seconds{ 1 }, stop) };
if (response) use_response(*response);
```

`request()` is lazy: it owns its input when called, but submits and starts its deadline when execution
begins. The same context can host the client and provider because the wait suspends rather than blocks.
Cancellation returns `ServiceError::Cancelled`; it also requests cancellation of an executing coroutine
provider. Timeouts and abandoned clients similarly signal the provider's token once observed. A handler
may ignore cancellation, so abandoning a response does not guarantee that side effects stop.

`serve(handler, ServiceOptions{})` bounds pending calls and live coroutine handlers. A cancelled or
timed-out handler continues occupying an execution slot until its cleanup finishes; further admission
returns `Busy` while all slots are occupied.
Exceptions from the handler become `HandlerFailed`. Request encoding exceptions propagate through the
client task; native decoding/response-encoding failures remain `HandlerFailed`.

**Actions.** Submission is immediate and returns a move-only goal handle. Acceptance and feedback
callbacks run on the bound context, and the final result is awaited:

```cpp
auto action{ messaging.action<Goal, Feedback, Result>("robot/move") };
auto goal{ action.sendGoal(target, pnm::msg::ActionGoalOptions{ std::chrono::seconds{ 1 } }, {
    .on_accepted{ [] { report_accepted(); } },
    .on_feedback{ [](const Feedback& value) { report_progress(value); } }
}) };
auto result{ co_await goal.result(stop) };
```

Both progress callbacks are optional and use copyable `std::function` captures. Feedback is coalesced,
and acceptance precedes feedback and completion. `result()` has one active consumer and can be consumed
once. `id()`, `ready()`, and `requestCancel()` are also available. A progress callback exception requests
cancellation, suppresses further progress callbacks, and propagates through `result()`.

An action provider receives an owned goal and a copyable execution view. It returns an explicit status
and result instead of calling a terminal method on the execution:

```cpp
using Execution = pnm::coro::ActionExecution<Feedback, Result>;
auto server{ action.serve([](Goal goal, Execution execution)
    -> pnm::coro::Task<pnm::msg::ActionCompletion<Result>> {
    // execution.id(), feedback(value), cancelRequested(), and stopToken() are available.
    auto result{ co_await perform_steps(goal, execution) };
    if (execution.cancelRequested()) {
        co_await finish_cleanup();
        co_return pnm::msg::ActionCompletion<Result>{ pnm::msg::ActionStatus::Cancelled, std::move(result) };
    }
    co_return pnm::msg::ActionCompletion<Result>{ pnm::msg::ActionStatus::Succeeded, std::move(result) };
}) };
```

Acceptance occurs after successful creation and registration of the coroutine job. To reject before
acceptance, use a factory returning
`std::expected<Task<ActionCompletion<Result>>, pnm::msg::ActionError>`; validation happens in that
factory before it returns the task. Empty tasks and handler exceptions produce `HandlerFailed`.
`ActionOptions` bounds queued/active goals and live coroutine handlers, including cleanup after an
early terminal error. The deadline only applies to admission.

A stop request during `goal.result(stop)` requests provider cancellation and **continues waiting for the
actual terminal outcome**. The provider must observe cancellation, finish cleanup, and return `Cancelled`.
It may instead finish successfully if completion wins the race. An engaged result can also contain
`Aborted`; always inspect its status. Destroying the goal handle requests cancellation and suppresses
its progress callbacks, without waiting for remote cleanup.

**Ownership and shutdown.** Provider callable objects stay alive until their jobs finish, including
move-only captures and coroutine-lambda captures. Payloads must be serializable, default constructible,
and movable; copying is not required. Keep referenced application objects alive yourself. Lazy task
wrappers retain endpoint state and owned arguments rather than borrowing the wrapper's `this` pointer.

Provider code, decoding, and action progress callbacks execute on their bound context. Awaiting tasks
resume on their inherited executor; this can be a different context. Unbound tasks resume on the
completion thread. As with other coroutine operations, rejected scheduling can resume a continuation
inline to propagate an error; do not stop a context before its messaging work has drained.

```cpp
messaging.requestStop(); // Stop admission, close subscriptions, request cancellation.
co_await messaging.join();
context.stop();
```

Coroutine provider registrations also expose `requestStop()`, `join()`, and `close()`. `join()` requires
stop to have been requested and permits multiple waiters. Keep driving the context until it finishes.
Service shutdown unregisters the provider and fails outstanding calls while its coroutine jobs unwind;
action shutdown rejects pending goals and permits active goals to finish cooperative cleanup.

`close()` unregisters/abandons immediately, but retains executing local jobs while they unwind; use
`join()` afterward to await their completion. Closing reports failure, not confirmed cancellation.
Destruction is a fallback, not asynchronous cleanup. The underlying bus and context must outlive the
bound view and its in-flight work. Synchronize destruction with context execution and in-flight API calls, and do not join a
provider or bus from one of the jobs that the join itself must wait for. A handler that ignores
cancellation, or a live remote goal that never responds, can prevent graceful shutdown from finishing.
Dropping a remote client does not prove that the remote work stopped.

See [`samples/coroutines_messaging_sample.cpp`](samples/coroutines_messaging_sample.cpp) for topics,
an awaited service, two action jobs, cancellation with asynchronous cleanup, and shutdown using two
application-owned context threads:

```bash
cmake --build --preset gcc-debug --target coroutines_messaging_sample coroutines_messaging_tests
./build/gcc-debug/samples/coroutines_messaging_sample
ctest --preset gcc-debug -R CoroutinesMessaging --output-on-failure
```

**Remote bridges.** Coroutine clients use the same named endpoints as the
[SPI bridge outlines](#extending-local-messaging-over-spi). A native `serveDeferred()` proxy can retain
its reply/execution token while frames travel across SPI. Calling `reply.respond()` or reporting an
action event on the receive thread wakes the coroutine client's context automatically; it does not
require a coroutine-aware transport. On the peer, a native client can call a coroutine provider in the
same way. The bridge owner still drives its native proxy and transport, maps IDs, forwards cancellation,
and validates frames. Two-bus integration tests cover service forwarding and action
acceptance/feedback/cancellation/result forwarding. No SPI driver or portable wire codec is provided.

### CMake Targets and Headers

| Target | Header(s) | Purpose |
| :--- | :--- | :--- |
| `pneumo::common` | `pneumo/common.hpp` | Shared result, assertion, memory, queue, and bit helpers |
| `pneumo::meta` | `pneumo/meta.hpp` | Reflection and metaprogramming utilities |
| `pneumo::formatting` | `pneumo/formatting.hpp` | Reflection-based formatting and optional serialization |
| `pneumo::units` | `pneumo/units.hpp` | Strong quantity types, literals, conversions, and derived operations |
| `pneumo::logging` | `pneumo/logging.hpp` | Asynchronous logging, routing, metadata, files, and custom sinks |
| `pneumo::messaging` | `pneumo/messaging.hpp` | Named, typed topics, services, and actions with caller-driven dispatch |
| `pneumo::coroutines` | `pneumo/coroutines.hpp` | Tasks, executors, timers, channels, and event-driven messaging adapters |
| `pneumo::pneumo` | `pneumo/pneumo.hpp` | Convenience target and umbrella header for all modules |

## Requirements

*   All modules require a compiler/toolchain with C++26 static reflection support. **`pneumo::common`** uses reflection to check the subobjects of types accepted by its `Serializable` concept.
*   The CMake targets propagate the required reflection flags, including when linking only **`pneumo::common`**.
*   **CMake 4.2.0+**

The checked-in CMake presets in this repository currently target GCC 16 and the Clang P2996 toolchain configured in `CMakePresets.json`.

If you use formatting, units, or logging on a toolchain where `<format>` or `<print>` live in a separate support library, you may also need to link that library explicitly. The sample targets use `stdc++exp` with the supported Linux toolchains.

## Installation

### CMake FetchContent

You can include pneumo in your CMake project with `FetchContent`:

```cmake
include(FetchContent)

FetchContent_Declare(
        pneumo
        GIT_REPOSITORY https://github.com/daleondev/pneumo.git
        GIT_TAG        v0.2.0
)
FetchContent_MakeAvailable(pneumo)

target_link_libraries(your_target PRIVATE pneumo::common)
# or
target_link_libraries(your_target PRIVATE pneumo::meta)
# or
target_link_libraries(your_target PRIVATE pneumo::formatting)
# or
target_link_libraries(your_target PRIVATE pneumo::units)
# or
target_link_libraries(your_target PRIVATE pneumo::logging)
# or
target_link_libraries(your_target PRIVATE pneumo::pneumo)
```

When you want the full library surface, pair `pneumo::pneumo` with:

```cpp
#include <pneumo/pneumo.hpp>
```

You can still include the specific module headers directly when you only want part of the library.

## Common Examples

```cpp
#include <pneumo/common.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>

auto main() -> int
{
    constexpr std::uint32_t source{ 0x1234ABCDU };
    std::array<std::byte, sizeof(source)> bytes{};
    std::uint32_t restored{};

    pnm::utils::memory::copy(bytes, source);
    pnm::utils::memory::copy(restored, bytes);

    auto queue = std::queue<int>{};
    pnm::utils::queue::push(queue, 42);
    const auto value = pnm::utils::queue::pop(queue);

    std::uint8_t flags{};
    pnm::utils::bit::set<0>(flags);
    pnm::utils::bit::set_masked_checked<2, 3>(flags, std::uint8_t{ 0b101 });

    PNM_ASSERT(value.has_value(), "queue unexpectedly empty");
    return restored == source && pnm::utils::bit::check<0>(flags) ? 0 : 1;
}
```

`PNM_ASSERT` is enabled by the `PNM_ENABLE_ASSERTS` definition, which the CMake targets add in Debug configurations. Failed assertions print the expression and source location before breaking into the debugger. `PNM_PACK_BEGIN`, `PNM_PACK_BEGIN_N(n)`, and `PNM_PACK_END` provide portable structure-packing scopes.

## Logging Examples

### Basic routing and metadata

```cpp
#include <pneumo/logging.hpp>

PNM_META_SOURCE_EMBED_CURRENT

auto main() -> int
{
    pnm::log::initialize(); // Optional eager initialization; logging also initializes lazily.

    pnm::log::std_out
      ->sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line)
      .timestampFormat("{:%Y-%m-%d %H:%M:%S}");

    pnm::log::std_err
      ->sourceInfo(pnm::log::SourceField::Function)
      .showLevel(false);

    pnm::log::info("Server started on port {}", 8080);
    pnm::log::error("Request {} failed", 17);

    pnm::log::warn(
      pnm::log::file("pneumo.log")
        .mode(pnm::log::FileMode::Append)
        .flushOn(pnm::log::Level::Error)
        .sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line)
        .sourceExcerpt(1),
      "Slow response: {} ms",
      250);

    pnm::log::info(pnm::log::immediate, "Written synchronously");
}
```

Normal calls are queued to a background worker. Passing `pnm::log::immediate` performs the write before the call returns. An explicit per-call sink routes only to that sink; otherwise a record is sent to its level’s default sink and every registered global sink. The backend drains queued records and closes cached files during process shutdown.

Immediate calls and the background worker share an output lock. Each record's output operations, including partial writes and flushing, are serialized against logging from other threads. Immediate calls may overtake queued messages; they do not drain the queue. The lock permits a custom sink to log immediately to another sink on the same thread. Direct calls to sink methods and changes to sink configuration are outside this synchronization; configure sinks before logging starts.

Default routes can be changed per level or level range:

```cpp
pnm::log::set_default_sink(
  pnm::log::Level::Trace, pnm::log::Level::Warn, pnm::log::std_out);
pnm::log::set_default_sink(
  pnm::log::Level::Error, pnm::log::Level::Critical, pnm::log::std_err);

const auto handle = pnm::log::add_global_sink(
  pnm::log::file("all.log").flushOn(pnm::log::Level::Error));

pnm::log::remove_global_sink(handle);
pnm::log::reset_default_sinks();
```

Source excerpts require the translation unit to be registered with `PNM_META_SOURCE_EMBED_CURRENT`, `PNM_META_SOURCE_EMBED_BEGIN`/`PNM_META_SOURCE_EMBED_END`, or otherwise available to `pnm::meta::source::excerpt` at runtime.

### Stacktraces

On toolchains providing `std::stacktrace`, enable a calling-thread stacktrace for a sink:

```cpp
#if defined(__cpp_lib_stacktrace)
pnm::log::std_err->sourceStacktrace();        // frame list only
pnm::log::std_err->sourceStacktrace(true, 1); // excerpts with one surrounding line
pnm::log::std_err->sourceStacktrace(pnm::log::Level::Error); // only Error and Critical
pnm::log::error("Operation failed");

pnm::log::error(pnm::log::immediate,
                pnm::log::file("errors.log").sourceStacktrace(true),
                "Written synchronously with a stacktrace");
#endif
```

`.sourceStacktrace(show_excerpts = false, context_size = 0)` enables stacktraces while preserving the sink's other source settings. `.sourceInfo(SourceField::Stacktrace)` selects the frame list alone. As with source excerpts, calling `.sourceInfo(...)` replaces all source settings; omit `Stacktrace` to disable it.

Use `.sourceStacktrace(min_level, show_excerpts = false, context_size = 0)` to set a separate stacktrace threshold on the same sink. For example, `.minLevel(Level::Trace).sourceStacktrace(Level::Error, true, 1)` accepts every log level but adds stacktraces and excerpts only for Error and Critical. `Level::Off` disables stacktraces without suppressing messages or other source information. The original overload and `.sourceInfo(SourceField::Stacktrace)` use `Level::Trace`, preserving stacktraces for all accepted levels. Calling `.sourceInfo(...)` also resets the stacktrace threshold. Like other sink configuration, set these options before concurrent logging starts.

A trace is captured once per message, on the calling thread, only if a routed sink accepts the level, enables stacktraces, and its stacktrace threshold accepts the level. Lower levels incur no stack capture unless another routed sink requests it. Async records retain that trace for the worker to format through `pnm::meta::source::stacktrace`. Each sink independently applies its stacktrace threshold when formatting and chooses whether to show frames and excerpts. The trace follows the message and any single-call-site excerpt, outside the colored header. Logger implementation frames are filtered using their function names or source locations, and the remaining frames are numbered from zero. Unidentified frames are retained.

Frame names and source locations depend on the toolchain and available debug information; optimized or inlined calls may appear differently or be absent. Build with debug information (for example, GCC's `-g`) for source excerpts, and embed or retain the corresponding source files. Unavailable excerpts are skipped while frames remain visible; an empty capture leaves the message intact. These settings are available only when `__cpp_lib_stacktrace` is defined; the currently supported Clang/libc++ toolchain does not provide them.

### Colors

Stdout and stderr automatically use ANSI foreground colors when attached to a terminal on POSIX systems. Redirected output, files, and custom sinks stay plain by default. The default palette is:

| Level | Color |
| :--- | :--- |
| Trace | Bright black (gray) |
| Debug | Cyan |
| Info | Green |
| Warn | Yellow |
| Error | Red |
| Critical | Bright red |

Configure a sink's palette with the same builder style as source metadata:

```cpp
pnm::log::std_out
  ->sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line)
  .color(pnm::log::Level::Info, pnm::log::cyan)
  .color(pnm::log::Level::Warn, pnm::log::bright_yellow);

pnm::log::info("Uses the configured Info color");
pnm::log::info(pnm::log::red, "Message {}", 42); // overrides the level color
pnm::log::info(pnm::log::no_color, "Plain message");
pnm::log::info(pnm::log::immediate, pnm::log::std_out, pnm::log::green, "Ready");
```

Named colors are `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, and `white`, with `bright_` versions of each. They are constants of type `pnm::log::Color`; `no_color` is `Color::None`. When ANSI output is enabled, `no_color` emits a reset (`\x1b[0m`) to restore the terminal's default styling.

Use `.colors(ColorMode::Auto)` for terminal detection, `.colors(ColorMode::Always)` (or `.colors()`) to force ANSI output, and `.colors(ColorMode::Never)` to suppress all logger-generated ANSI sequences, including resets. A per-message color overrides the palette but still respects the sink's color mode, so file output stays plain even when the same message is colored on a terminal. A custom sink can override `isTerminal()` to participate in automatic detection, or enable colors explicitly. `.resetColors()` restores the default palette without changing the mode.

Color covers the header (timestamp, level, source metadata, and separator); a reset is emitted before the message. Messages and source excerpts remain plain. The color argument goes immediately before the format string, after any `immediate` tag and explicit sink, and works with both synchronous and asynchronous calls. Configure sinks before sending messages through them, as with the existing source and timestamp settings.

### Custom sinks

Derive from `pnm::log::SinkBase<YourSink>` and implement the transport operations. The logger handles formatting, filtering, source metadata, partial writes, and exception isolation around the sink.

```cpp
#include <pneumo/logging.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>

class CountingSink : public pnm::log::SinkBase<CountingSink>
{
  public:
    auto isOpen() const -> bool override { return m_open.load(); }

    auto open() -> pnm::Result<> override
    {
        m_open.store(true);
        return {};
    }

    auto close() -> pnm::Result<> override
    {
        m_open.store(false);
        return {};
    }

    auto write(std::span<const std::byte> data) -> pnm::Result<std::size_t> override
    {
        m_bytes.fetch_add(data.size());
        return data.size();
    }

    auto flush() -> pnm::Result<> override { return {}; }

  private:
    std::atomic_bool m_open{};
    std::atomic_size_t m_bytes{};
};

auto main() -> int
{
    auto sink = std::make_shared<CountingSink>();
    pnm::log::info(sink, "Custom transport message");
}
```

## Formatting Examples

For formatting utilities:

```cpp
#include <pneumo/formatting.hpp>

#include <memory>
#include <optional>
#include <print>
#include <string>
#include <tuple>
#include <vector>
```

### 1. Aggregate Reflection

Public aggregate member names and values are discovered automatically:

```cpp
struct Point
{
    int x;
    int y;
};

struct Config
{
    int id;
    std::string name;
    std::vector<double> values;
    Point resolution;
    bool is_active;
};

int main()
{
    Config cfg{ 101, "SimulationConfig", { 0.5, 1.2, 3.14 }, { 1920, 1080 }, true };

    std::println("{}", cfg);
    // Output: [ Config: { id: 101, name: SimulationConfig, values: [0.5, 1.2, 3.14],
    // resolution: [ Point: { x: 1920, y: 1080 } ], is_active: true } ]

    std::println("{:p}", cfg);
    /* Output:
    Config: {
      id: 101,
      name: SimulationConfig,
      values: [0.5, 1.2, 3.14],
      resolution: {
        x: 1920,
        y: 1080
      },
      is_active: true
    }
    */
}
```

### 2. Adapters (Encapsulated Classes)

For classes with private members or custom layouts, define a `pnm::fmt::Adapter` specialization.

```cpp
class User
{
  public:
    User(std::string name, std::string role)
      : m_name(name)
      , m_role(role)
    {
    }
    const std::string& getName() const { return m_name; }
    const std::string& getRole() const { return m_role; }

  private:
    std::string m_name;
    std::string m_role;
};

template<>
struct pnm::fmt::Adapter<User>
{
    using Fields = std::tuple<pnm::fmt::Field<"name", &User::getName>,
                              pnm::fmt::Field<"role", &User::getRole>>;
};

int main()
{
    User user("Alice", "Admin");

    std::println("{}", user);
    // Output: [ User: { name: Alice, role: Admin } ]
}
```

### 3. Scoped Enums

Scoped enums are automatically formatted by enumerator name, with an optional verbose form.

```cpp
enum class Status
{
    Idle,
    Processing,
    Completed
};

int main()
{
    Status s = Status::Processing;

    std::println("{}", s);
    // Output: Processing

    std::println("{:v}", s);
    // Output: Status::Processing
}
```

### 4. Pointers & Optionals

The formatting module handles `nullptr`, `std::optional`, and smart pointers gracefully.

```cpp
struct Point
{
    int x;
    int y;
};

int main()
{
    std::optional<int> opt_val = 123;
    std::println("{}", opt_val);
    // Output: [ 123 ]

    std::optional<int> empty_opt;
    std::println("{}", empty_opt);
    // Output: [ null ]

    auto ptr = std::make_unique<Point>(10, 20);
    std::println("{}", ptr);
    // Output: [ (0x...) -> [ Point: { x: 10, y: 20 } ] ]
}
```

### 5. Serialization (JSON / TOML / YAML)

If enabled via the `PFMT_ENABLE_JSON`, `PFMT_ENABLE_TOML`, or `PFMT_ENABLE_YAML` CMake options, you can format supported objects directly into serialized strings using Glaze.

**Format Specifiers:**

*   `{:j}` - Compact JSON
*   `{:pj}` - Pretty JSON
*   `{:y}` - YAML when Glaze metadata is available for the type
*   `{:t}` - TOML

```cpp
struct Point
{
    int x;
    int y;
};

struct Config
{
    int id;
    std::string name;
    std::vector<double> values;
    Point resolution;
    bool is_active;
};

int main()
{
    Config cfg{ 101, "SimulationConfig", { 0.5, 1.2, 3.14 }, { 1920, 1080 }, true };

    std::println("{:j}", cfg);
    // JSON output is available when PFMT_ENABLE_JSON is ON.

    std::println("{:pj}", cfg);
    // Pretty JSON output is available when PFMT_ENABLE_JSON is ON.

    std::println("{:t}", cfg);
    // TOML output is available when PFMT_ENABLE_TOML is ON.
}
```

For adapter-backed types, Glaze metadata is generated automatically inside `pneumo/formatting.hpp`. That makes adapters the easiest path when you want reflection-aware formatting plus YAML serialization.

### 6. Custom formatting

Custom formatting can be supplied in several ways:

*   `pnm::fmt::Adapter<T>`
*   `operator<<`
*   `toString()` or `to_string()` members
*   matching `toString(T)` or `to_string(T)` free functions

The current precedence is: `Adapter` > stream insertion > string-conversion hooks > reflection.

```cpp
#include <ostream>

struct Point
{
    int x;
    int y;
};

std::ostream& operator<<(std::ostream& os, const Point& p)
{
    return os << "Resolution is " << p.x << "x" << p.y;
}

struct Config
{
    int id;
    std::string name;
    std::vector<double> values;
    Point resolution;
    bool is_active;

    std::string toString() const
    {
        return "Config with id: " + std::to_string(id) +
            " has " + std::to_string(values.size()) + " values";
    }
};

int main()
{
    Config cfg{ 101, "SimulationConfig", { 0.5, 1.2, 3.14 }, { 1920, 1080 }, true };

    std::println("{}", cfg);
    // Output: Config with id: 101 has 3 values

    std::println("{}", cfg.resolution);
    // Output: Resolution is 1920x1080
}
```

## Units Examples

For units utilities:

```cpp
#include <pneumo/units.hpp>

#include <chrono>
#include <iostream>
#include <type_traits>

using namespace pnm::units::literals;
```

### 1. Literals and Unit Conversion

```cpp
int main()
{
    const auto distance = 3.5_km;

    std::cout << distance.get() << " m\n";
    std::cout << distance.get<pnm::units::DistanceUnits::km>() << " km\n";
    std::cout << distance.get<pnm::units::DistanceUnits::cm>() << " cm\n";
}
```

### 2. Dimensional Arithmetic

```cpp
int main()
{
    const auto trip_distance = 42.0_km;
    const auto trip_time = 35.0_min;
    const auto average_speed = trip_distance / trip_time;

    static_assert(std::same_as<std::remove_cvref_t<decltype(average_speed)>, pnm::units::Velocity>);

    std::cout << average_speed.get<pnm::units::VelocityUnits::km_h>() << " km/h\n";
}
```

### 3. Storage and Throughput

```cpp
int main()
{
    const auto size = 90.0_MB;
    const auto duration = 30.0_s;
    const auto rate = size / duration;

    static_assert(std::same_as<std::remove_cvref_t<decltype(rate)>, pnm::units::DataRate>);

    std::cout << rate.get<pnm::units::DataRateUnits::MB_s>() << " MB/s\n";
}
```

### 4. Acceleration Chain

```cpp
int main()
{
    const auto highway_speed = 100.0_km_h;
    const auto zero_to_hundred = 8.0_s;
    const auto average_acceleration = highway_speed / zero_to_hundred;
    const auto speed_after_three_seconds = average_acceleration * 3.0_s;

    static_assert(std::same_as<std::remove_cvref_t<decltype(average_acceleration)>,
                               pnm::units::Acceleration>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(speed_after_three_seconds)>,
                               pnm::units::Velocity>);

    std::cout << average_acceleration.get<pnm::units::AccelerationUnits::m_s2>() << " m/s^2\n";
    std::cout << speed_after_three_seconds.get<pnm::units::VelocityUnits::km_h>() << " km/h\n";
}
```

### 5. Mechanical and Electrical Units

```cpp
int main()
{
    const auto mass = 2.0_kg;
    const auto gravity = 9.81_m_s2;
    const auto force = mass * gravity;
    const auto lift_height = 3.0_m;
    const auto work = force * lift_height;
    const auto duration = 4.0_s;
    const auto motor_power = work / duration;
    const auto current = motor_power / 12.0_V;

    static_assert(std::same_as<std::remove_cvref_t<decltype(force)>, pnm::units::Force>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(work)>, pnm::units::Energy>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(motor_power)>, pnm::units::Power>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(current)>, pnm::units::Current>);

    std::cout << force.get<pnm::units::ForceUnits::N>() << " N\n";
    std::cout << work.get<pnm::units::EnergyUnits::J>() << " J\n";
    std::cout << motor_power.get<pnm::units::PowerUnits::W>() << " W\n";
    std::cout << current.get<pnm::units::CurrentUnits::A>() << " A\n";
}
```

### 6. Chrono Interoperability

The units literal namespace also exposes the standard chrono literals, so both Pneumo's `_s`, `_ms`, `_min`, and `_h` literals and chrono's `s`, `ms`, `min`, and `h` literals are available through the same `using namespace` declaration.

```cpp
int main()
{
    const pnm::units::Time timeout = 1500ms; // implicit chrono-to-Time conversion
    const auto distance = 42.0_km;
    const auto speed = distance / 35min;        // chrono duration in dimensional arithmetic

    const std::chrono::duration<double, std::milli> floating_ms = timeout;
    const auto whole_ms = static_cast<std::chrono::milliseconds>(timeout);
    const auto frequency = 1.0 / 250ms;

    std::cout << floating_ms.count() << " ms\n";
    std::cout << whole_ms.count() << " whole ms\n";
    std::cout << speed.get<pnm::units::VelocityUnits::km_h>() << " km/h\n";
    std::cout << frequency.get<pnm::units::FrequencyUnits::Hz>() << " Hz\n";
}
```

Chrono durations convert implicitly to `Time`. `Time` converts implicitly to chrono durations with floating-point representations. Conversion to integral chrono durations is explicit because it can truncate; use `static_cast` or `toChrono<Duration>()`. The parameterless `toChrono()` returns `std::chrono::duration<double>`, which is useful with chrono APIs whose duration type is determined through template argument deduction.

Quantities also support stream insertion with unit suffixes. The rendered unit is chosen from the largest registered unit that keeps the absolute converted value at least `1`; zero and non-finite values render with the base unit. Compound suffixes use `/` for display, so `m_s` renders as `m/s`. Use `get<Unit>()` when you need a specific presentation unit.

### 7. Percentages and Rotation

```cpp
using namespace pnm::units::literals;

const auto setting = 50_percent;
const auto speed = 2000_rpm * setting;    // 1000 rpm
const auto rotation = speed * 500ms;      // Angle; chrono durations also work
const auto turn_time = 1_rev / speed;     // Time for one revolution
const auto measured_speed = 180_deg / 1_s; // 30 rpm
const auto relative_speed = pnm::units::Ratio::create(speed / 2000_rpm);

std::cout << setting.get<pnm::units::RatioUnits::percent>() << "%\n";
std::cout << speed.get<pnm::units::AngularVelocityUnits::rpm>() << " rpm\n";
std::cout << rotation.get<pnm::units::AngleUnits::rev>() << " rev\n";
```

Automatic stream output follows the usual magnitude-based unit selection: `50_percent` prints as `50percent`, `100_percent` as `1one`, and angular velocities may print in `rad/s`. Use explicit `get<Unit>()` calls as above for `%` or a fixed rpm display. The period of a rotation is `1_rev / angular_velocity`; `1 / frequency` remains the separate frequency/period relation.

## Meta Examples

For meta utilities:

```cpp
#include <pneumo/meta.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <ratio>
#include <string>
#include <tuple>
#include <variant>
```

### 1. Fixed Strings

```cpp
constexpr auto NAME = pnm::meta::string::FixedString{ "sample" };

static_assert(NAME.size() == 6UZ);
static_assert(static_cast<std::string_view>(NAME) == "sample");
```

### 2. Tuple Utilities

```cpp
using TupleA = std::tuple<int, double>;
using TupleB = std::tuple<double, float, int>;

static_assert(pnm::meta::tuple::Tuple<TupleA>);
static_assert(!pnm::meta::tuple::Tuple<int>);

static_assert(pnm::meta::tuple::ContainsType<TupleA, int>);
static_assert(!pnm::meta::tuple::ContainsType<TupleA, float>);

using Concatenated = pnm::meta::tuple::concat_types_t<TupleA, TupleB>;
static_assert(std::same_as<Concatenated, std::tuple<int, double, double, float, int>>);

using Unique = pnm::meta::tuple::unique_types_t<Concatenated>;
static_assert(std::same_as<Unique, std::tuple<int, double, float>>);

static_assert(pnm::meta::tuple::count<TupleA>() == 2UZ);
static_assert(std::same_as<pnm::meta::tuple::at_t<0, TupleA>, int>);
static_assert(std::same_as<pnm::meta::tuple::at_t<1, TupleA>, double>);

using TupleAsVariant = pnm::meta::tuple::to_variant_t<TupleA>;
static_assert(std::same_as<TupleAsVariant, std::variant<int, double>>);

using CvrefTuple = std::tuple<const int&, volatile double&&, const std::string>;
using CvrefRemoved = pnm::meta::tuple::remove_cvref_types_t<CvrefTuple>;
static_assert(std::same_as<CvrefRemoved, std::tuple<int, double, std::string>>);

constexpr auto EXPECTED_SUM{ 6 };

constexpr auto FOR_EACH_SUM_VALID = [] -> bool {
    auto values = std::tuple{ 1, 2, 3 };
    auto sum{ 0 };
    pnm::meta::tuple::for_each_element([&sum](int value) { sum += value; }, values);
    return sum == EXPECTED_SUM;
};

static_assert(FOR_EACH_SUM_VALID());

constexpr auto REVERSE_FOR_EACH_CAN_BREAK = [] -> bool {
    auto visited{ 0 };
    pnm::meta::tuple::for_each<TupleB, pnm::meta::Iteration::Reverse>([&visited](auto index) {
        ++visited;
        if constexpr (index == 1) {
            return pnm::meta::Loop::Break;
        }
        else {
            return pnm::meta::Loop::Continue;
        }
    });
    return visited == 2;
};

static_assert(REVERSE_FOR_EACH_CAN_BREAK());
```

### 3. Variant Utilities

```cpp
using VariantInput = std::variant<int, double, int, float>;

static_assert(pnm::meta::variant::Variant<VariantInput>);
static_assert(!pnm::meta::variant::Variant<int>);

using VariantTuple = pnm::meta::variant::to_tuple_t<VariantInput>;
static_assert(std::same_as<VariantTuple, std::tuple<int, double, int, float>>);

using VariantUnique = pnm::meta::variant::unique_types_t<VariantInput>;
static_assert(std::same_as<VariantUnique, std::variant<int, double, float>>);

using VariantRef = pnm::meta::variant::to_reference_wrapper_t<std::variant<int, const double>>;
static_assert(
  std::same_as<VariantRef,
               std::variant<std::reference_wrapper<int>, std::reference_wrapper<const double>>>);

constexpr auto VARIANT_REVERSE_FOR_EACH_CAN_BREAK = [] -> bool {
    auto visited{ 0 };
    pnm::meta::variant::for_each<VariantInput, pnm::meta::Iteration::Reverse>([&visited](auto index) {
        ++visited;
        if constexpr (index == 2) {
            return pnm::meta::Loop::Break;
        }
        else {
            return pnm::meta::Loop::Continue;
        }
    });
    return visited == 2;
};

static_assert(VARIANT_REVERSE_FOR_EACH_CAN_BREAK());

using MyVariant = std::variant<int, double, int, float>;
pnm::meta::variant::for_each<MyVariant>([](auto index) {
    using Type = std::variant_alternative_t<index, MyVariant>;
    constexpr auto type_name{ pnm::meta::type::name<Type>() };
    std::cout << "Variant alternative at index " << index << ": " << type_name << '\n';
});

// Output:
// Variant alternative at index 0: int
// Variant alternative at index 1: double
// Variant alternative at index 2: int
// Variant alternative at index 3: float
```

### 4. Type and Namespace Utilities

```cpp
namespace sample::detail
{
    struct Widget
    {
    };
}

static_assert(pnm::meta::type::name<sample::detail::Widget>() == "Widget");
static_assert(pnm::meta::type::namespace_name<sample::detail::Widget>() == "sample::detail");
static_assert(pnm::meta::type::namespaces<sample::detail::Widget>()[0] == "sample");
static_assert(pnm::meta::type::namespaces<sample::detail::Widget>()[1] == "detail");
static_assert(pnm::meta::type::StdType<std::string>);
static_assert(!pnm::meta::type::StdType<sample::detail::Widget>);
```

### 5. Enum Reflection

```cpp
enum class SampleState : std::uint8_t
{
    Idle = 0,
    Running = 2,
    Done = 4
};

static_assert(pnm::meta::enumeration::ScopedEnum<SampleState>);
static_assert(!pnm::meta::enumeration::ScopedEnum<int>);

static_assert(pnm::meta::enumeration::count<SampleState>() == 3UZ);

constexpr auto SAMPLE_ENUMERATORS = pnm::meta::enumeration::enumerators<SampleState>();
static_assert(SAMPLE_ENUMERATORS[0] == SampleState::Idle);
static_assert(SAMPLE_ENUMERATORS[1] == SampleState::Running);
static_assert(SAMPLE_ENUMERATORS[2] == SampleState::Done);

constexpr auto SAMPLE_UNDERLYING = pnm::meta::enumeration::underlying_enumerators<SampleState>();
static_assert(std::same_as<decltype(SAMPLE_UNDERLYING), const std::array<std::uint8_t, 3>>);
static_assert(SAMPLE_UNDERLYING[0] == 0);
static_assert(SAMPLE_UNDERLYING[1] == 2);
static_assert(SAMPLE_UNDERLYING[2] == 4);

constexpr auto SAMPLE_ENUMERATOR_NAMES = pnm::meta::enumeration::enumerator_names<SampleState>();
static_assert(SAMPLE_ENUMERATOR_NAMES[0] == "Idle");
static_assert(SAMPLE_ENUMERATOR_NAMES[1] == "Running");
static_assert(SAMPLE_ENUMERATOR_NAMES[2] == "Done");

static_assert(pnm::meta::enumeration::name<SampleState>() == "SampleState");
static_assert(pnm::meta::enumeration::enumerator_name(SampleState::Running) == "Running");
```

### 6. Structural Reflection

```cpp
struct SampleAggregate
{
    int id;
    double weight;
};

template<typename T, typename Ratio, bool IsBase = false>
struct SampleNestedTemplate
{
};

struct SampleNestedTypes
{
    using Index = int;
    using Ratio = std::ratio<2>;
    using TemplateAlias = SampleNestedTemplate<double, std::ratio<3>>;

  private:
    using Hidden = SampleNestedTemplate<float, std::ratio<5>>;
};

struct SampleDispatcher
{
    static constexpr auto triple(int value) -> int { return value * 3; }
};

static_assert(pnm::meta::structural::field_count<SampleAggregate>() == 2UZ);
constexpr auto SAMPLE_FIELD_NAMES = pnm::meta::structural::field_names<SampleAggregate>();
static_assert(SAMPLE_FIELD_NAMES[0] == "id");
static_assert(SAMPLE_FIELD_NAMES[1] == "weight");

static_assert(std::same_as<pnm::meta::structural::field_type_t<0, SampleAggregate>, int>);
static_assert(std::same_as<pnm::meta::structural::field_type_t<1, SampleAggregate>, double>);
static_assert(std::same_as<pnm::meta::structural::field_types_t<SampleAggregate>,
                           std::tuple<int, double>>);

constexpr auto SAMPLE_ID_VALUE = 7;
constexpr auto SAMPLE_WEIGHT_VALUE = 1.5;

constexpr auto SAMPLE_FIELD_GET_VALID = [] -> bool {
    auto value = SampleAggregate{ .id = SAMPLE_ID_VALUE, .weight = SAMPLE_WEIGHT_VALUE };
    return pnm::meta::structural::get<0>(value) == SAMPLE_ID_VALUE &&
           pnm::meta::structural::get<1>(value) == SAMPLE_WEIGHT_VALUE;
};

static_assert(SAMPLE_FIELD_GET_VALID());

using NestedTypes = pnm::meta::structural::nested_types_t<SampleNestedTypes>;
static_assert(std::same_as<NestedTypes,
                           std::tuple<int,
                                      std::ratio<2>,
                                      SampleNestedTemplate<double, std::ratio<3>>>>);

static_assert(std::same_as<pnm::meta::structural::nested_type_t<0, SampleNestedTypes>, int>);
static_assert(pnm::meta::structural::nested_type_name<0, SampleNestedTypes>() == "Index");

constexpr auto SAMPLE_NESTED_TYPE_NAMES = pnm::meta::structural::nested_type_names<SampleNestedTypes>();
static_assert(SAMPLE_NESTED_TYPE_NAMES[1] == "Ratio");
static_assert(SAMPLE_NESTED_TYPE_NAMES[2] == "TemplateAlias");

using AggregateInfo = pnm::meta::structural::Info<SampleAggregate>;
using NestedInfo = pnm::meta::structural::Info<SampleNestedTypes>;
static_assert(AggregateInfo::NAME == "SampleAggregate");
static_assert(AggregateInfo::numMembers() == 2UZ);
static_assert(AggregateInfo::MEMBER_NAMES[0] == "id");
static_assert(NestedInfo::numNestedTypes() == 3UZ);
static_assert(NestedInfo::NESTED_TYPE_NAMES[0] == "Index");

static_assert(pnm::meta::structural::dispatch<SampleDispatcher, "triple">(7) == 21);
```

### 7. Source Embedding and Excerpts

```cpp
PNM_META_SOURCE_EMBED_CURRENT

auto main() -> int
{
    std::cout << *pnm::meta::source::excerpt(__FILE__, __LINE__, 1) << std::endl;
    return 0;
}
```

`PNM_META_SOURCE_EMBED_CURRENT` embeds the current translation unit once, and
`pnm::meta::source::excerpt(file, line, context_size)` returns a
`pnm::Result<std::string>` containing a numbered excerpt when the source is
available. On ELF targets, embedded sources are immutable, allocation-free
linker descriptors: the macro does not run a global constructor or touch the
runtime source-registry mutex before `main()`.

### Available CMake Configuration Options

| Option | Description | Default |
| :--- | :--- | :--- |
| `PFMT_ENABLE_JSON` | Enable JSON support via Glaze | `OFF` |
| `PFMT_ENABLE_TOML` | Enable TOML support via Glaze | `OFF` |
| `PFMT_ENABLE_YAML` | Enable YAML support via Glaze | `OFF` |
| `PNM_BUILD_SAMPLES` | Build sample executables | `ON` for top-level builds, otherwise `OFF` |
| `PNM_BUILD_TESTS` | Build unit tests | `ON` for top-level builds, otherwise `OFF` |
| `ENABLE_CLANG_TIDY` | Run Clang-Tidy during the build when a Clang-Tidy executable is configured | `OFF` |

## Build Instructions

For a ready-to-use GCC 16 and Clang/P2996 environment, open the repository in its
VS Code devcontainer. VS Code uses the published `pneumo-devcontainer:latest`
image, and GitHub CI uses `pneumo-ci:latest`, which provides its compiler toolchain.
No local toolchain image build is needed. See [container setup and image
updates](.containers/README.md) for details.

### Build the library, samples, and tests

```bash
cmake --preset clang-release
cmake --build --preset clang-release
```

### Run the samples:

```bash
# Common sample
./build/clang-release/samples/common_sample

# Formatting sample
./build/clang-release/samples/formatting_sample

# Meta sample
./build/clang-release/samples/meta_sample

# Units sample
./build/clang-release/samples/units_sample

# Logging sample
./build/clang-release/samples/logging_sample

# Coroutines sample
./build/clang-release/samples/coroutines_sample

# Messaging sample
./build/clang-release/samples/messaging_sample
```

### Run the tests:

```bash
ctest --preset clang-release --output-on-failure
```

### Lint the project:

```bash
cmake --preset clang-tidy
cmake --build --preset clang-tidy --clean-first
```

The `clang-tidy` preset enables `ENABLE_CLANG_TIDY`, keeps sample builds on, and turns `PNM_BUILD_TESTS` off.

### Available CMake Presets

| Preset Name | Description | Compiler |
| :--- | :--- | :--- |
| `gcc-debug` / `gcc-release` | Build using GCC | `g++` |
| `clang-debug` / `clang-release` | Build using Clang with libc++ | `clang++` (`-stdlib=libc++`) |
| `clang-tidy` | Debug build with Clang-Tidy enabled and tests disabled | `clang++` + `clang-tidy` |

## Compiler Support

This library is header-only, but `pneumo/meta.hpp`, `pneumo/formatting.hpp`, `pneumo/units.hpp`, and `pneumo/logging.hpp` require a compiler/toolchain with C++26 static reflection support.

The checked-in CMake presets in this repository currently target:

- **Linux**: GCC 16 via `gcc-debug` and `gcc-release`
- **Linux**: the Clang P2996 toolchain configured in `CMakePresets.json` via `clang-debug`, `clang-release`, and `clang-tidy`

Windows and MSVC are not supported by the current release or CI configuration.

## License

Pneumo is distributed under the [MIT License](LICENSE). Optional serialization support fetches Glaze 7.0.2, and the test build fetches GoogleTest 1.17.0. Their exact license texts are reproduced in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt); neither dependency is vendored into this repository.

Changes included in each release are recorded in [CHANGELOG.md](CHANGELOG.md).

## Acknowledgements

**pneumo** is made possible by these incredible open-source projects:

- **[Glaze 7.0.2](https://github.com/stephenberry/glaze/tree/v7.0.2)**: Extremely fast C++ library for JSON, YAML, and TOML serialization.
- **[GoogleTest 1.17.0](https://github.com/google/googletest/tree/v1.17.0)**: Industrial-strength testing framework.

For third-party license information, please see [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
