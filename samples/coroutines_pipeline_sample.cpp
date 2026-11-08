#include "pneumo/coroutines.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// NOLINTBEGIN

namespace
{
    using namespace std::chrono_literals;

    constexpr int JOB_COUNT{ 9 };
    constexpr int WORKER_COUNT{ 3 };
    constexpr int OBSERVER_COUNT{ 2 };
    constexpr int MAX_IN_FLIGHT{ 4 };
    constexpr int TASK_COUNT{ 1 + WORKER_COUNT + 1 + OBSERVER_COUNT };

    struct Job
    {
        int id;
        std::vector<int> samples;
    };

    // A native, same-process message, not a portable serialization format.
    struct Progress
    {
        std::int64_t checksum{};
        int job{};
        int worker{};
        int attempts{};
        int succeeded{};
    };

    struct Result
    {
        Progress progress;
        std::string error;
    };

    struct Metrics
    {
        int succeeded{};
        int failed{};
        int retried{};
        std::int64_t checksum{};
    };

    struct Pipeline
    {
        explicit Pipeline(pnm::coro::Context& executor)
          : context{ executor }
        {
        }
        pnm::coro::Context& context;
#if PNM_CORO_ENABLE_EXECUTION
        pnm::coro::ThreadPool pool{ WORKER_COUNT };
#endif
        pnm::coro::Channel<std::unique_ptr<Job>> jobs;
        pnm::coro::Channel<Result> results;
        pnm::coro::RawBinaryChannel progress;
        pnm::coro::Channel<bool> permits;
        pnm::coro::Channel<std::string> observer_ready;
        pnm::coro::Channel<std::string> acknowledged;
#if !PNM_CORO_ENABLE_EXECUTION
        pnm::coro::Channel<std::string> finished;
        int workers_remaining{ WORKER_COUNT };
#endif
    };

    class TemporaryFailure : public std::runtime_error
    {
      public:
        using std::runtime_error::runtime_error;
    };

    auto pause(Pipeline& pipeline, std::chrono::milliseconds duration, std::stop_token stop)
      -> pnm::coro::Task<void>
    {
#if PNM_CORO_ENABLE_EXECUTION
        if (!co_await pnm::coro::sleep(pipeline.context, duration, stop))
            throw pnm::coro::OperationStopped{};
#else
        static_cast<void>(pipeline);
        static_cast<void>(stop);
        co_await pnm::coro::sleep(duration);
#endif
    }

    auto produce(Pipeline& pipeline, std::stop_token stop = {}) -> pnm::coro::Task<void>
    {
        for (int id{ 1 }; id <= JOB_COUNT; ++id) {
            // Channel is unbounded. Application-level permits bound the entire pipeline,
            // including queued jobs, active work, and results awaiting observation.
            if (!(co_await pipeline.permits.next(stop))) {
                break;
            }
            auto job{ std::make_unique<Job>(Job{ id, std::vector<int>(8) }) };
            std::iota(job->samples.begin(), job->samples.end(), id * 10);
            std::cout << "[producer] queued job " << id << '\n';
            pipeline.jobs.push(std::move(job));
            co_await pause(pipeline, 5ms, stop);
        }
        pipeline.jobs.close(); // Workers drain queued jobs before next() returns nullopt.
#if !PNM_CORO_ENABLE_EXECUTION
        pipeline.finished.push("producer");
#endif
    }

    auto calculate(Pipeline& pipeline, const Job& job, int attempt) -> pnm::coro::Task<std::int64_t>
    {
        // The callable owns its input. It never touches pipeline state or stdout.
        auto function{ [id{ job.id }, samples{ job.samples }, attempt] {
            // Simulate a legacy blocking API with variable latency.
            std::this_thread::sleep_for(std::chrono::milliseconds{ 15 + (id % 3) * 10 });
            if (id == 3 && attempt == 1) {
                throw TemporaryFailure{ "upstream temporarily unavailable" };
            }
            if (id == 6) {
                throw std::invalid_argument{ "invalid input batch" };
            }
            return std::accumulate(samples.begin(), samples.end(), std::int64_t{});
        } };
#if PNM_CORO_ENABLE_EXECUTION
        co_return co_await pnm::coro::runAsync<std::int64_t>(pipeline.pool.getExecutionScheduler(),
                                                             std::move(function));
#else
        static_cast<void>(pipeline);
        co_return co_await pnm::coro::runAsync<std::int64_t>(std::move(function));
#endif
    }

    auto process(Pipeline& pipeline, std::unique_ptr<Job> job, int worker, std::stop_token stop)
      -> pnm::coro::Task<Result>
    {
        Result result{ .progress = { .job = job->id, .worker = worker }, .error = {} };
        constexpr int max_attempts{ 2 };
        for (int attempt{ 1 }; attempt <= max_attempts; ++attempt) {
            result.progress.attempts = attempt;
            try {
                // Exceptions cross both nested Task and runAsync boundaries.
                result.progress.checksum = co_await calculate(pipeline, *job, attempt);
                result.progress.succeeded = 1;
                result.error.clear();
                co_return result;
            }
#if PNM_CORO_ENABLE_EXECUTION
            catch (const pnm::coro::OperationStopped&) {
                throw;
            }
#endif
            catch (const TemporaryFailure& error) {
                result.error = error.what();
            } catch (const std::exception& error) {
                result.error = error.what();
                co_return result; // A bad job does not stop the other workers.
            }
            if (attempt < max_attempts) {
                std::cout << "[worker " << worker << "] retrying job " << job->id << ": " << result.error
                          << '\n';
                // Backoff suspends this worker without blocking the context thread.
                co_await pause(pipeline, 10ms, stop);
            }
        }
        co_return result;
    }

    auto work(Pipeline& pipeline, int worker, std::stop_token stop = {}) -> pnm::coro::Task<void>
    {
        while (auto job{ co_await pipeline.jobs.next(stop) }) {
            std::cout << "[worker " << worker << "] processing job " << (*job)->id << '\n';
            pipeline.results.push(co_await process(pipeline, std::move(*job), worker, stop));
        }
#if !PNM_CORO_ENABLE_EXECUTION
        // Only the last worker closes results, after every job has produced an outcome.
        // All coroutine continuations run on our single context thread, so no atomic is needed.
        if (--pipeline.workers_remaining == 0) {
            pipeline.results.close();
        }
        pipeline.finished.push("worker " + std::to_string(worker));
#endif
    }

    auto publish(Pipeline& pipeline, std::stop_token stop = {}) -> pnm::coro::Task<void>
    {
        for (int observer{}; observer < OBSERVER_COUNT; ++observer) {
            static_cast<void>(co_await pipeline.observer_ready.next(stop));
        }
        while (auto result{ co_await pipeline.results.next(stop) }) {
            if (!result->error.empty()) {
                std::cout << "[result] job " << result->progress.job << " failed: " << result->error << '\n';
            }
            pnm::coro::RawBinaryChannel::Bytes bytes(sizeof(Progress));
            std::memcpy(bytes.data(), &result->progress, sizeof(Progress));
            pipeline.progress.push(std::move(bytes));

            // Broadcast reaches current waiters; it is not a replayable subscription.
            // Wait for both observers to acknowledge and re-enter next() before publishing again.
            for (int observer{}; observer < OBSERVER_COUNT; ++observer) {
                static_cast<void>(co_await pipeline.acknowledged.next(stop));
            }
            pipeline.permits.push(true);
        }
        pipeline.progress.close();
#if !PNM_CORO_ENABLE_EXECUTION
        pipeline.finished.push("publisher");
#endif
    }

    auto observe(Pipeline& pipeline, std::string name, auto on_progress, std::stop_token stop = {})
      -> pnm::coro::Task<void>
    {
        std::stop_callback cancel{ stop, [&pipeline] { pipeline.progress.close(); } };
        pnm::coro::BinaryChannel<Progress> events{ pipeline.progress };
        pipeline.observer_ready.push(name);
        while (auto event{ co_await events.next() }) {
            on_progress(*event);
            pipeline.acknowledged.push(name);
            // No additional suspension between acknowledging and registering the next wait.
        }
#if !PNM_CORO_ENABLE_EXECUTION
        pipeline.finished.push(std::move(name));
#endif
    }

#if PNM_CORO_ENABLE_EXECUTION
    template<std::size_t... I>
    auto workers(Pipeline& pipeline, std::index_sequence<I...>) -> pnm::coro::Task<void>
    {
        std::exception_ptr failure;
        try {
            co_await pnm::coro::execution::when_all(
              pnm::coro::asSender(pipeline.context, [&pipeline](std::stop_token stop) {
                return work(pipeline, static_cast<int>(I) + 1, stop);
            })...);
        } catch (...) {
            failure = std::current_exception();
        }
        pipeline.results.close();
        if (failure)
            std::rethrow_exception(failure);
    }
#else
    auto join(pnm::coro::Context& context, Pipeline& pipeline) -> pnm::coro::Task<void>
    {
        for (int task{}; task < TASK_COUNT; ++task) {
            if (auto name{ co_await pipeline.finished.next() }) {
                std::cout << "[joined] " << *name << '\n';
            }
        }
        // stop() rejects new scheduling. Call it only after workers, timers, and observers finish.
        context.stop();
    }
#endif
}

int main()
{
    pnm::coro::Context context;
    Pipeline pipeline{ context };
    Metrics metrics;
    pipeline.progress.setMode(pnm::coro::ChannelMode::Broadcast);
    for (int slot{}; slot < MAX_IN_FLIGHT; ++slot) {
        pipeline.permits.push(true);
    }

    std::cout << "Processing " << JOB_COUNT << " jobs with " << WORKER_COUNT << " workers; at most "
              << MAX_IN_FLIGHT << " jobs in flight.\n"
              << "Job 3 fails once and retries; job 6 is rejected. Completion order may vary.\n\n";

    auto dashboard{ [&](std::stop_token stop) {
        return observe(pipeline, "dashboard", [completed{ 0 }](const Progress& event) mutable {
            std::cout << "[dashboard] " << ++completed << '/' << JOB_COUNT << " job " << event.job
                      << " on worker " << event.worker << (event.succeeded ? " succeeded" : " failed")
                      << " after " << event.attempts << " attempt(s)\n";
        }, stop);
    } };
    auto metrics_observer{ [&](std::stop_token stop) {
        return observe(pipeline, "metrics", [&metrics](const Progress& event) {
            if (event.succeeded) {
                ++metrics.succeeded;
                metrics.checksum += event.checksum;
            }
            else {
                ++metrics.failed;
            }
            metrics.retried += event.attempts > 1 ? 1 : 0;
        }, stop);
    } };
    std::exception_ptr error;
#if PNM_CORO_ENABLE_EXECUTION
    pnm::coro::co_spawn(context, [&](auto&) -> pnm::coro::Task<void> {
        try {
            co_await pnm::coro::execution::when_all(
              pnm::coro::asSender(context, dashboard),
              pnm::coro::asSender(context, metrics_observer),
              pnm::coro::asSender(context, [&](std::stop_token stop) { return publish(pipeline, stop); }),
              pnm::coro::asSender(context, workers(pipeline, std::make_index_sequence<WORKER_COUNT>{})),
              pnm::coro::asSender(context, [&](std::stop_token stop) { return produce(pipeline, stop); }));
        } catch (...) {
            error = std::current_exception();
        }
        context.stop();
    });
#else
    pnm::coro::co_spawn(context, [&](auto&) { return dashboard({}); });
    pnm::coro::co_spawn(context, [&](auto&) { return metrics_observer({}); });
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) { return publish(pipeline); });
    for (int worker{ 1 }; worker <= WORKER_COUNT; ++worker) {
        // Copy the loop index; the spawned callable can start after this iteration ends.
        pnm::coro::co_spawn(context, [&, worker](pnm::coro::Context&) { return work(pipeline, worker); });
    }
    pnm::coro::co_spawn(context, [&](pnm::coro::Context&) { return produce(pipeline); });
    pnm::coro::co_spawn(context, [&](pnm::coro::Context& executor) { return join(executor, pipeline); });
#endif

    // One event-loop thread owns pipeline bookkeeping and output. Blocking work runs elsewhere.
    context.run();
    if (error)
        std::rethrow_exception(error);
    std::cout << "\nSummary: " << metrics.succeeded << " succeeded, " << metrics.failed << " failed, "
              << metrics.retried << " retried; checksum=" << metrics.checksum << '\n'
              << "All " << TASK_COUNT << " pipeline tasks joined before shutdown.\n";

    // Deterministic totals make the intentionally failed job distinguishable from a broken sample.
    return metrics.succeeded == 8 && metrics.failed == 1 && metrics.retried == 1 && metrics.checksum == 3344
             ? 0
             : 1;
}

// NOLINTEND
