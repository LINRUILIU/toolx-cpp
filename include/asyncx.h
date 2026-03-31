#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "sysx.h"

namespace asyncx
{

    enum class ErrorKind
    {
        None = 0,
        InvalidArgument,
        Timeout,
        QueueFull,
        QueueClosed,
        NotRunning,
        NotFound,
        Internal,
    };

    enum class StopMode
    {
        Drain = 0,
        CancelPending,
    };

    enum class BackpressurePolicy
    {
        Block = 0,
        Reject,
    };

    enum class TaskPriority
    {
        High = 0,
        Normal,
        Low,
    };

    struct Error
    {
        ErrorKind kind{ErrorKind::None};
        bool retryable{false};
        std::string message;
    };

    struct Status
    {
        bool ok{false};
        Error error{};
    };

    template <typename T>
    struct Result
    {
        bool ok{false};
        T value{};
        Error error{};
    };

    struct PoolOptions
    {
        std::size_t worker_count{0};
        std::size_t queue_capacity{1024};
        bool start_immediately{true};
        BackpressurePolicy backpressure_policy{BackpressurePolicy::Block};
    };

    struct Stats
    {
        std::uint64_t submitted{0};
        std::uint64_t completed{0};
        std::uint64_t rejected{0};
        std::uint64_t timed_out{0};
        std::uint64_t backpressure_rejected{0};
    };

    struct SchedulerStats
    {
        std::uint64_t created{0};
        std::uint64_t fired{0};
        std::uint64_t cancelled{0};
        std::uint64_t periodic_rescheduled{0};
        std::size_t pending{0};
    };

    struct MetricsSnapshot
    {
        bool running{false};
        bool accepting{false};
        bool idle{true};
        std::size_t worker_count{0};
        std::size_t active_workers{0};
        std::size_t queue_size{0};
        std::size_t queue_capacity{0};
        std::size_t scheduled_count{0};
        BackpressurePolicy backpressure_policy{BackpressurePolicy::Block};
        Stats execution{};
        SchedulerStats scheduler{};
    };

    const char *ToString(ErrorKind kind) noexcept;
    const char *ToString(StopMode mode) noexcept;
    const char *ToString(BackpressurePolicy policy) noexcept;
    const char *ToString(TaskPriority priority) noexcept;

    Status OkStatus();
    Status MakeErrorStatus(ErrorKind kind, std::string message, bool retryable = false);

    class ThreadPool
    {
    public:
        explicit ThreadPool(PoolOptions options = {});
        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;
        ThreadPool(ThreadPool &&) = delete;
        ThreadPool &operator=(ThreadPool &&) = delete;

        Status Start();
        Status Stop(StopMode mode = StopMode::Drain);
        Status StopAndJoin(StopMode mode = StopMode::Drain);
        Status Join();

        bool IsRunning() const noexcept;
        bool IsAccepting() const noexcept;
        bool IsIdle() const noexcept;
        std::size_t QueueSize() const noexcept;
        std::size_t QueueCapacity() const noexcept;
        std::size_t ScheduledCount() const noexcept;
        std::size_t ActiveWorkerCount() const noexcept;
        std::size_t WorkerCount() const noexcept;
        BackpressurePolicy GetBackpressurePolicy() const noexcept;
        void SetBackpressurePolicy(BackpressurePolicy policy) noexcept;
        Stats GetStats() const noexcept;
        MetricsSnapshot GetMetricsSnapshot() const noexcept;
        Stats ResetStats() noexcept;
        Status WaitForIdle();
        Status WaitForIdleUntil(std::chrono::steady_clock::time_point deadline);

        template <typename Rep, typename Period>
        Status WaitForIdleFor(std::chrono::duration<Rep, Period> timeout)
        {
            const auto casted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
            const auto deadline = sysx::time::SteadyNow() + casted;
            return WaitForIdleUntil(deadline);
        }

        Status Post(std::function<void()> task);
        Status TryPost(std::function<void()> task);
        Status PostUntil(std::chrono::steady_clock::time_point deadline, std::function<void()> task);
        Status PostWithPriority(TaskPriority priority, std::function<void()> task);
        Status TryPostWithPriority(TaskPriority priority, std::function<void()> task);
        Status PostWithPriorityUntil(std::chrono::steady_clock::time_point deadline,
                                     TaskPriority priority,
                                     std::function<void()> task);

        Result<std::uint64_t> PostDelayedUntil(std::chrono::steady_clock::time_point due_time, std::function<void()> task);

        template <typename Rep, typename Period>
        Result<std::uint64_t> PostDelayedFor(std::chrono::duration<Rep, Period> delay, std::function<void()> task)
        {
            const auto casted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay);
            const auto due_time = sysx::time::SteadyNow() + casted;
            return PostDelayedUntil(due_time, std::move(task));
        }

        Result<std::uint64_t> ScheduleEvery(std::chrono::steady_clock::duration interval,
                                            std::function<void()> task,
                                            bool run_immediately = false);
        Status CancelScheduled(std::uint64_t task_id);

        template <typename Rep, typename Period>
        Status PostFor(std::chrono::duration<Rep, Period> timeout, std::function<void()> task)
        {
            const auto casted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
            const auto deadline = sysx::time::SteadyNow() + casted;
            return PostUntil(deadline, std::move(task));
        }

        template <typename Fn, typename... Args>
        auto Submit(Fn &&fn, Args &&...args) -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            return SubmitWithPriority(TaskPriority::Normal,
                                      std::forward<Fn>(fn),
                                      std::forward<Args>(args)...);
        }

        template <typename Fn, typename... Args>
        auto SubmitWithPriority(TaskPriority priority, Fn &&fn, Args &&...args)
            -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            using ReturnType = std::invoke_result_t<Fn, Args...>;

            auto bound = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
            auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound));

            Result<std::future<ReturnType>> out;
            std::future<ReturnType> future = task->get_future();
            Status status = EnqueueTask([task]() mutable
                                        { (*task)(); },
                                        std::nullopt,
                                        ShouldWaitOnBackpressure(),
                                        priority);
            if (!status.ok)
            {
                out.ok = false;
                out.error = std::move(status.error);
                return out;
            }

            out.ok = true;
            out.value = std::move(future);
            return out;
        }

        template <typename Fn, typename... Args>
        auto TrySubmit(Fn &&fn, Args &&...args) -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            return TrySubmitWithPriority(TaskPriority::Normal,
                                         std::forward<Fn>(fn),
                                         std::forward<Args>(args)...);
        }

        template <typename Fn, typename... Args>
        auto TrySubmitWithPriority(TaskPriority priority, Fn &&fn, Args &&...args)
            -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            using ReturnType = std::invoke_result_t<Fn, Args...>;

            auto bound = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
            auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound));

            Result<std::future<ReturnType>> out;
            std::future<ReturnType> future = task->get_future();
            Status status = EnqueueTask([task]() mutable
                                        { (*task)(); },
                                        std::nullopt,
                                        false,
                                        priority);
            if (!status.ok)
            {
                out.ok = false;
                out.error = std::move(status.error);
                return out;
            }

            out.ok = true;
            out.value = std::move(future);
            return out;
        }

        template <typename Fn, typename... Args>
        auto SubmitUntil(std::chrono::steady_clock::time_point deadline, Fn &&fn, Args &&...args)
            -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            return SubmitWithPriorityUntil(deadline,
                                           TaskPriority::Normal,
                                           std::forward<Fn>(fn),
                                           std::forward<Args>(args)...);
        }

        template <typename Fn, typename... Args>
        auto SubmitWithPriorityUntil(std::chrono::steady_clock::time_point deadline,
                                     TaskPriority priority,
                                     Fn &&fn,
                                     Args &&...args)
            -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            using ReturnType = std::invoke_result_t<Fn, Args...>;

            auto bound = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
            auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound));

            Result<std::future<ReturnType>> out;
            std::future<ReturnType> future = task->get_future();
            Status status = EnqueueTask([task]() mutable
                                        { (*task)(); },
                                        deadline,
                                        true,
                                        priority);
            if (!status.ok)
            {
                out.ok = false;
                out.error = std::move(status.error);
                return out;
            }

            out.ok = true;
            out.value = std::move(future);
            return out;
        }

        template <typename Rep, typename Period, typename Fn, typename... Args>
        auto SubmitFor(std::chrono::duration<Rep, Period> timeout, Fn &&fn, Args &&...args)
            -> Result<std::future<std::invoke_result_t<Fn, Args...>>>
        {
            const auto casted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
            const auto deadline = sysx::time::SteadyNow() + casted;
            return SubmitUntil(deadline, std::forward<Fn>(fn), std::forward<Args>(args)...);
        }

    private:
        using Task = std::function<void()>;
        using OptionalDeadline = std::optional<std::chrono::steady_clock::time_point>;

        struct QueuedTask
        {
            Task task;
            TaskPriority priority{TaskPriority::Normal};
            std::uint64_t sequence{0};
        };

        struct ScheduledTask
        {
            std::uint64_t id{0};
            Task task;
            std::chrono::steady_clock::time_point due{};
            std::chrono::steady_clock::duration interval{};
            bool periodic{false};
        };

        Status EnqueueTask(Task task, OptionalDeadline deadline, bool wait_for_slot, TaskPriority priority);
        Status WaitForIdleImpl(OptionalDeadline deadline);
        void WorkerLoop();
        void SchedulerLoop();
        bool IsIdleLocked() const noexcept;
        bool ShouldWaitOnBackpressure() const noexcept;
        std::vector<ScheduledTask>::iterator FindScheduledByIdLocked(std::uint64_t task_id);
        std::vector<ScheduledTask>::iterator FindNextDueLocked();

        PoolOptions options_{};

        mutable sysx::sync::Mutex mu_{};
        sysx::sync::ConditionVariable cv_not_empty_{};
        sysx::sync::ConditionVariable cv_not_full_{};
        sysx::sync::ConditionVariable cv_idle_{};
        sysx::sync::ConditionVariable cv_scheduler_{};

        std::vector<sysx::thread::Thread> workers_{};
        sysx::thread::Thread scheduler_thread_{};
        std::deque<QueuedTask> queue_{};
        std::vector<ScheduledTask> scheduled_tasks_{};
        std::size_t active_workers_{0};
        std::uint64_t next_scheduled_id_{1};
        std::uint64_t next_task_sequence_{1};
        std::size_t next_dispatch_slot_{0};

        bool running_{false};
        bool accepting_{false};
        bool stop_requested_{false};
        bool scheduler_stop_requested_{false};
        StopMode stop_mode_{StopMode::Drain};
        BackpressurePolicy backpressure_policy_{BackpressurePolicy::Block};

        std::atomic<std::uint64_t> submitted_{0};
        std::atomic<std::uint64_t> completed_{0};
        std::atomic<std::uint64_t> rejected_{0};
        std::atomic<std::uint64_t> timed_out_{0};
        std::atomic<std::uint64_t> backpressure_rejected_{0};
        std::atomic<std::uint64_t> scheduled_created_{0};
        std::atomic<std::uint64_t> scheduled_fired_{0};
        std::atomic<std::uint64_t> scheduled_cancelled_{0};
        std::atomic<std::uint64_t> periodic_rescheduled_{0};
    };

    template <typename T>
    Status WaitAll(std::vector<std::future<T>> &futures)
    {
        for (auto &future : futures)
        {
            if (!future.valid())
            {
                return MakeErrorStatus(ErrorKind::InvalidArgument, "future must be valid", false);
            }
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        return OkStatus();
    }

    template <typename T, typename Rep, typename Period>
    Status WaitAllFor(std::vector<std::future<T>> &futures, std::chrono::duration<Rep, Period> timeout)
    {
        const auto casted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
        const auto deadline = sysx::time::SteadyNow() + casted;

        for (auto &future : futures)
        {
            if (!future.valid())
            {
                return MakeErrorStatus(ErrorKind::InvalidArgument, "future must be valid", false);
            }
        }

        for (auto &future : futures)
        {
            if (future.wait_until(deadline) == std::future_status::timeout)
            {
                return MakeErrorStatus(ErrorKind::Timeout, "wait all timed out", true);
            }
        }

        return OkStatus();
    }

    template <typename T>
    Result<std::size_t> WaitAnyUntil(std::vector<std::future<T>> &futures,
                                     std::chrono::steady_clock::time_point deadline)
    {
        Result<std::size_t> out;
        if (futures.empty())
        {
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "futures must not be empty", false).error;
            return out;
        }

        for (auto &future : futures)
        {
            if (!future.valid())
            {
                out.ok = false;
                out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "future must be valid", false).error;
                return out;
            }
        }

        for (;;)
        {
            for (std::size_t i = 0; i < futures.size(); ++i)
            {
                const auto status = futures[i].wait_for(std::chrono::milliseconds(0));
                if (status == std::future_status::ready || status == std::future_status::deferred)
                {
                    out.ok = true;
                    out.value = i;
                    return out;
                }
            }

            if (sysx::time::SteadyNow() >= deadline)
            {
                out.ok = false;
                out.error = MakeErrorStatus(ErrorKind::Timeout, "wait any timed out", true).error;
                return out;
            }

            sysx::time::SleepFor(std::chrono::milliseconds(1));
        }
    }

    template <typename T, typename Rep, typename Period>
    Result<std::size_t> WaitAnyFor(std::vector<std::future<T>> &futures,
                                   std::chrono::duration<Rep, Period> timeout)
    {
        const auto casted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
        const auto deadline = sysx::time::SteadyNow() + casted;
        return WaitAnyUntil(futures, deadline);
    }

    template <typename T>
    Result<std::size_t> WaitAny(std::vector<std::future<T>> &futures)
    {
        Result<std::size_t> out;
        if (futures.empty())
        {
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "futures must not be empty", false).error;
            return out;
        }

        for (auto &future : futures)
        {
            if (!future.valid())
            {
                out.ok = false;
                out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "future must be valid", false).error;
                return out;
            }
        }

        for (;;)
        {
            for (std::size_t i = 0; i < futures.size(); ++i)
            {
                const auto status = futures[i].wait_for(std::chrono::milliseconds(0));
                if (status == std::future_status::ready || status == std::future_status::deferred)
                {
                    out.ok = true;
                    out.value = i;
                    return out;
                }
            }
            sysx::time::SleepFor(std::chrono::milliseconds(1));
        }
    }

} // namespace asyncx
